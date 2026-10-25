#include "app/app.h"
#include "common/gyro.h"
#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "common/options.h"
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
// character: its look angles in turns, which 0x857D0 turns by the stick's rates and then clamps (0x83190); the
// stick's right turns the yaw up, its up turns the pitch down
constexpr uint32_t kCharacterPitch = 0x1E8;
constexpr uint32_t kCharacterYaw = 0x1EC;
constexpr uint32_t kCharacterInput = 0x354;  // its controller input, which only the player's character has
// its view angle in degrees, 33 unzoomed; 0x857D0 scales the stick's turn by it over 33, so zoomed in turns slower
constexpr uint32_t kCharacterFov = 0xB4;
constexpr float kUnzoomedFov = 33.0f;

// the angles are whole turns: the rotations (0x116C70, 0x7A1E0) take the sine of 2 pi times them
constexpr float kTurnsPerDegree = 1.0f / 360.0f;
constexpr float kTurnsPerCount = 0.04f * kTurnsPerDegree;  // at sensitivity 1, as on foot

struct LookTurn {
    float across = 0.0f;
    float up_down = 0.0f;
};

LookTurn MouseTurn() {
    float dx;
    float dy;
    if (!input::TakeMouse(dx, dy)) return {};
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
    if (R32(character + kCharacterInput)) {
        LookTurn mouse = MouseTurn();
        LookTurn gyro = GyroTurn();
        float zoom = G<float>(character + kCharacterFov) / kUnzoomedFov;
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
    LookTurn mouse = MouseTurn();
    LookTurn gyro = GyroTurn();
    float across_sensitivity = pad ? o.pad_horizontal_sensitivity : 1.0f;
    float up_down_sensitivity = pad ? o.pad_vertical_sensitivity : 1.0f;
    Axis across = ApplyAxis(camera, kRateAcross, kTurnAcross, across_sensitivity, mouse.across + gyro.across);
    Axis up_down = ApplyAxis(camera, kRateUpDown, kTurnUpDown, up_down_sensitivity, mouse.up_down + gyro.up_down);
    __imp__sub_00060110(ctx, base);
    RestoreAxis(across);
    RestoreAxis(up_down);
}
