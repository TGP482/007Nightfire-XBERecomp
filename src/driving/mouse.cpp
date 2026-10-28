#include <algorithm>
#include <chrono>
#include <cmath>

#include "app/app.h"
#include "common/gyro.h"
#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "common/options.h"
#include "driving/frame_rate.h"
#include "xdk/hle.h"

// A turret's look (0xB7AE0) sets its camera's turn rates from the sticks, turns a frame (+0x700 across, right
// positive; +0x704 up and down, down positive), and the camera's update (0x60110) eases its turn (+0x6F8, +0x6FC)
// towards them by a fixed step, then turns by it. For that update the controller's sensitivity scales the rates and
// the mouse and the gyro add their own turn to both, so the easing is the sticks' alone. Out of a turret (the sniper
// at the start of the game) the player's character looks by its own angles (0x857D0), which the mouse and gyro turn.
namespace {

using kernel::G;
using kernel::R32;

constexpr uint32_t kPlayer = 0x239220;
constexpr uint32_t kPlayerCamera = 0x47C;  // its turret's camera while it is in one
// camera
constexpr uint32_t kRateAcross = 0x700;
constexpr uint32_t kRateUpDown = 0x704;
constexpr uint32_t kTurnAcross = 0x6F8;
constexpr uint32_t kTurnUpDown = 0x6FC;
// the target a zoom locks (0x5F680 from 0xB7AE0): the update turns towards it and slows the turn near it, until
// it marks the camera free (+0x708)
constexpr uint32_t kTarget = 0x6C;
constexpr uint32_t kTargetFree = 0x708;
// character: its look angles in turns, which 0x857D0 turns by the stick's rates and then clamps (0x83190); the
// stick's right turns the yaw up, its up turns the pitch down
constexpr uint32_t kCharacterPitch = 0x1E8;
constexpr uint32_t kCharacterYaw = 0x1EC;
constexpr uint32_t kCharacterInput = 0x354;  // its controller input, which only the player's character has
// its view angle in degrees, 33 unzoomed; 0x857D0 scales the stick's turn by it over 33, so zoomed in turns slower
constexpr uint32_t kCharacterFov = 0xB4;
constexpr float kUnzoomedFov = 33.0f;
constexpr uint32_t kCharacterAim = 0x354;  // its input: +0xB set while 0x82E00 keeps the sight on a target
constexpr uint32_t kAimLocked = 0xB;
constexpr uint32_t kAiming = 0xC;  // byte in its input, set by 0x8A310 while the player aims
// the aim's sight, turns from the view's middle, which 0x857D0 moves within its dead zone and draws (+0x1D0, +0x1D4)
constexpr uint32_t kCharacterSightPitch = 0x1E0;
constexpr uint32_t kCharacterSightYaw = 0x1E4;
// the character's camera type record, as 0x857D0 finds it: the dead zone, turns on the unzoomed view
constexpr uint32_t kCameraTypes = 0x1EC358;
constexpr uint32_t kCameraTypeSize = 0x100;
constexpr uint32_t kCharacterCameraType = 0x27C;  // signed byte
constexpr uint32_t kDeadZonePitch = 0x40;
constexpr uint32_t kDeadZoneYaw = 0x44;
// of each turn the update keeps (0x60379), eased to the update; the mouse and gyro are divided by it to turn in full
constexpr double kTurnKept = 0.65;

// the angles are whole turns: the rotations (0x116C70, 0x7A1E0) take the sine of 2 pi times them
constexpr float kTurnsPerDegree = 1.0f / 360.0f;
constexpr float kTurnsPerCount = 0.04f * kTurnsPerDegree;  // at sensitivity 1, as on foot

struct LookTurn {
    float across = 0.0f;
    float up_down = 0.0f;
};

LookTurn MouseTurn(bool aim) {
    float dx;
    float dy;
    if (!input::TakeMouse(dx, dy, aim)) return {};
    return {dx * kTurnsPerCount, dy * kTurnsPerCount};
}

// the gyro's yaw is left positive and its pitch down positive
LookTurn GyroTurn() {
    const game::Options& o = game::Opt();
    gyro::Turn turn = gyro::Take(o.gyro);
    if (app::MenuOpen()) return {};
    float yaw = o.invert_pad_x ? -turn.yaw : turn.yaw;
    return {-yaw * kTurnsPerDegree, turn.pitch * kTurnsPerDegree};
}

uint32_t CameraRecord(uint32_t character) {
    int32_t type = G<int8_t>(character + kCharacterCameraType);
    return R32(kCameraTypes) + uint32_t(type) * kCameraTypeSize;
}

// the value moved by step within its reach; what is left over
float StepWithin(float& value, float step, float reach) {
    float moved = std::clamp(value + step, -reach, reach);
    float rest = value + step - moved;
    value = moved;
    return rest;
}

// while aiming the mouse moves the sight, and past the dead zone the view turns by the rest, as the sticks turn it
LookTurn MoveSight(uint32_t character, LookTurn mouse, float zoom) {
    uint32_t record = CameraRecord(character);
    float& yaw = G<float>(character + kCharacterSightYaw);
    float& pitch = G<float>(character + kCharacterSightPitch);
    float across = mouse.across * zoom;
    float up_down = mouse.up_down * zoom;
    float rest_across = StepWithin(yaw, across, G<float>(record + kDeadZoneYaw) * zoom);
    float rest_up_down = StepWithin(pitch, up_down, G<float>(record + kDeadZonePitch) * zoom);
    return {rest_across / zoom, rest_up_down / zoom};
}

// the camera's rate and turn on one axis for the update, put back after it
struct Axis {
    uint32_t rate;
    uint32_t turn;
    float saved_rate;
    float added;
};

Axis ApplyAxis(uint32_t camera, uint32_t rate, uint32_t turn, float sensitivity, float added) {
    float saved = G<float>(camera + rate);
    G<float>(camera + rate) = saved * sensitivity + added;
    G<float>(camera + turn) += added;
    return {camera + rate, camera + turn, saved, added};
}

void RestoreAxis(const Axis& axis) {
    G<float>(axis.rate) = axis.saved_rate;
    G<float>(axis.turn) -= axis.added;
}

}  // namespace

// a character's look each frame, thiscall
X86_EXTERN_FUNC(__imp__sub_000857D0);
X86_FUNC(sub_000857D0) {
    uint32_t character = ctx.ecx;
    uint32_t input = R32(character + kCharacterInput);
    if (input) {
        bool aiming = G<uint8_t>(input + kAiming);
        LookTurn mouse = MouseTurn(aiming);
        LookTurn gyro = GyroTurn();
        float zoom = G<float>(character + kCharacterFov) / kUnzoomedFov;
        bool moved = mouse.across != 0.0f || mouse.up_down != 0.0f;
        if (moved && aiming) mouse = MoveSight(character, mouse, zoom);
        G<float>(character + kCharacterYaw) += (mouse.across + gyro.across) * zoom;
        G<float>(character + kCharacterPitch) += (mouse.up_down + gyro.up_down) * zoom;
    }
    __imp__sub_000857D0(ctx, base);
}

// a camera's update, thiscall
X86_EXTERN_FUNC(__imp__sub_00060110);
X86_FUNC(sub_00060110) {
    uint32_t camera = ctx.ecx;
    uint32_t player = R32(kPlayer);
    if (!player || R32(player + kPlayerCamera) != camera) return __imp__sub_00060110(ctx, base);
    const game::Options& o = game::Opt();
    bool pad = !input::KeyboardActive();
    LookTurn mouse = MouseTurn(false);
    LookTurn gyro = GyroTurn();
    float kept = float(driving::Ease(kTurnKept));
    float across_sensitivity = pad ? o.pad_horizontal_sensitivity : 1.0f;
    float up_down_sensitivity = pad ? o.pad_vertical_sensitivity : 1.0f;
    float added_across = (mouse.across + gyro.across) / kept;
    float added_up_down = (mouse.up_down + gyro.up_down) / kept;
    Axis across = ApplyAxis(camera, kRateAcross, kTurnAcross, across_sensitivity, added_across);
    Axis up_down = ApplyAxis(camera, kRateUpDown, kTurnUpDown, up_down_sensitivity, added_up_down);
    // the mouse aims for itself: no pull onto the locked target
    uint32_t target = R32(camera + kTarget);
    uint8_t was_free = G<uint8_t>(camera + kTargetFree);
    if (!pad) kernel::W32(camera + kTarget, 0);
    __imp__sub_00060110(ctx, base);
    kernel::W32(camera + kTarget, target);
    G<uint8_t>(camera + kTargetFree) = was_free;
    RestoreAxis(across);
    RestoreAxis(up_down);
}

// the sight's pull onto a target (character, angle across out, angle up and down out), thiscall: true when it pulls.
// The mouse aims for itself, so on the keyboard and mouse it never does.
X86_EXTERN_FUNC(__imp__sub_00082E00);
X86_FUNC(sub_00082E00) {
    if (!input::KeyboardActive()) return __imp__sub_00082E00(ctx, base);
    G<uint8_t>(R32(ctx.ecx + kCharacterAim) + kAimLocked) = 0;
    kernel::Return(ctx, 0, 2);
}

// Underwater the player's sub (SUB_PHYSICS, its physics 0x644C0) steers by the mouse: the mouse pushes the left stick,
// which falls back to the middle, so the sub turns across and up and down while the mouse moves. A remote torpedo
// keeps the mouse for its camera.
namespace {

constexpr uint32_t kVehicleDriver = 0x2C8;  // 0 the player
constexpr uint32_t kSubUpdates = 4;         // updates since the player's sub last ran its physics
constexpr float kFullStickCounts = 100.0f;  // of the mouse, at sensitivity 1
constexpr double kRecentreSeconds = 0.1;    // the stick falls back by 1/e
constexpr float kStickFull = 32767.0f;

struct SubStick {
    float x = 0.0f;  // right positive
    float y = 0.0f;  // up positive
    bool seen = false;
    uint32_t seen_update = 0;
    std::chrono::steady_clock::time_point last;
};

SubStick g_sub;

bool InPlayerSub() {
    uint32_t player = R32(kPlayer);
    bool recent = g_sub.seen && driving::UpdateCount() - g_sub.seen_update <= kSubUpdates;
    return recent && player && !R32(player + kPlayerCamera) && !app::MenuOpen();
}

int16_t PushStick(int16_t keys, float mouse) {
    return int16_t(std::clamp(float(keys) + mouse * kStickFull, -kStickFull, kStickFull));
}

}  // namespace

// a sub's physics, thiscall
X86_EXTERN_FUNC(__imp__sub_000644C0);
X86_FUNC(sub_000644C0) {
    if (R32(ctx.ecx + kVehicleDriver) == 0) {
        g_sub.seen = true;
        g_sub.seen_update = driving::UpdateCount();
    }
    __imp__sub_000644C0(ctx, base);
}

// keyboard.cpp, once its keys are on the pad
void DrivingSubMouse(input::Pad& pad) {
    auto now = std::chrono::steady_clock::now();
    double seconds = std::chrono::duration<double>(now - g_sub.last).count();
    g_sub.last = now;
    if (!InPlayerSub()) {
        g_sub.x = 0.0f;
        g_sub.y = 0.0f;
        return;
    }
    float fall = float(std::exp(-seconds / kRecentreSeconds));
    g_sub.x *= fall;
    g_sub.y *= fall;
    float dx;
    float dy;
    if (input::TakeMouse(dx, dy)) {
        g_sub.x = std::clamp(g_sub.x + dx / kFullStickCounts, -1.0f, 1.0f);
        g_sub.y = std::clamp(g_sub.y - dy / kFullStickCounts, -1.0f, 1.0f);
    }
    pad.lx = PushStick(pad.lx, g_sub.x);
    pad.ly = PushStick(pad.ly, g_sub.y);
}
