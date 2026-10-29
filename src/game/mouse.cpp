#include <algorithm>
#include <iterator>
#include <utility>

#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "common/options.h"
#include "xdk/hle.h"

// The mouse turns the view itself: the game's turn and look are stick rates, ramped and capped by 0xB7420.
namespace {

using kernel::G;

constexpr uint32_t kPlayerState = 0xD2;  // u16; 8 to 12 and 16 have no look, 0xB7610 leaves them
constexpr uint16_t kGuiding = 10;        // steering a guided missile, which 0x23670 turns by the sticks
constexpr uint16_t kHanging = 6;         // on a cable; 0xAFB50 turns Bond round if his yaw is 0.0002 off the cable's
constexpr uint32_t kPlayerWeapon = 0xB8;
constexpr uint32_t kWeaponFlags = 0x50;  // bit 0 set by the manual aim button, 0xBA8F0
constexpr uint8_t kManualAim = 1;
constexpr uint32_t kWeaponRecord = 0x52;  // byte: its record, 0x18CFA0 on, 0x10C apart
constexpr uint32_t kRecords = 0x18CFA0;
constexpr uint32_t kRecordSize = 0x10C;
constexpr uint32_t kRecordFlags = 0x68;
constexpr uint8_t kSightStays = 0x40;  // manual aim keeps the sight in the middle and looks instead
constexpr uint32_t kPlayerController = 0xBC;
constexpr uint32_t kControllerPlayer = 0x8DE;
constexpr int8_t kPlayer1 = 0;  // the gyro turns this player alone
// radians this frame, right negative, zeroed at its start; the stick's turn is in it once the look has run, then
// 0xAD520 turns the player by it
constexpr uint32_t kControllerYawStep = 0x28;
// quarter turns, up positive; 0xB7A50 clamps it to -1..1 after the look
constexpr uint32_t kControllerPitch = 0x838;
constexpr uint32_t kControllerZoom = 0x860;  // the view's magnification, 1 unzoomed; the stick's look divides by it
constexpr uint32_t kControllerLooking = 0xF0;  // set while the stick moves the view up or down
// manual aim's sight on the view, right and up positive, which the sticks move (0xB764E) up to its reach and the
// sight's pull (0xB9950) eases the drawn one towards
constexpr uint32_t kControllerSightX = 0xD8;
constexpr uint32_t kControllerSightY = 0xDC;
constexpr uint32_t kControllerShownX = 0xE0;
constexpr uint32_t kControllerShownY = 0xE4;
constexpr uint32_t kSightReachX = 0x161B40;  // 0.296
constexpr uint32_t kSightReachY = 0x15D590;  // 0.4

constexpr float kRadiansPerDegree = 0.017453292f;
constexpr float kRadiansPerCount = 0.04f * kRadiansPerDegree;  // at sensitivity 1, as the other recomps
constexpr float kQuarterTurnsPerRadian = 0.63661975f;

// the game's Auto Aim setting, player 1's; the second while 0x260018 is set
constexpr uint32_t kAutoAim[] = {0x1FE6D1, 0x1FE6D2};
constexpr uint32_t kPlayerSettings = 0x158;  // apart, as the players' action blocks

// the guided missile (0x23670): its owner and that player's controller
constexpr uint32_t kMissileData = 0xBC;
constexpr uint32_t kMissileOwner = 0x24;
// its turn this frame at 0x23838, radians: across right positive, up and down up negative
constexpr uint32_t kMissileTurnAcross = 0x3C;
constexpr uint32_t kMissileTurnUpDown = 0x38;

// a mounted turret (kind 0x47, 0x27FC0): the controller's +0x808 while the player uses it, and its aim in radians,
// right lower and down higher, as its sticks move it
constexpr uint32_t kControllerMounted = 0x808;
constexpr uint32_t kObjectKind = 0xDB;
constexpr uint8_t kTurret = 0x47;
constexpr uint32_t kObjectState = 0xD0;
constexpr uint16_t kTurretManned = 1;
constexpr uint32_t kTurretPlayer = 0xD2;
constexpr uint32_t kObjectData = 0xBC;
constexpr uint32_t kTurretPitch = 0x5C;
constexpr uint32_t kTurretYaw = 0x60;

// mouse counts the player's look took while steering a missile, for its turn
float g_missile_dx = 0.0f;
float g_missile_dy = 0.0f;

bool Looks(uint16_t state) { return state < 8 || (state > 12 && state != 16); }

bool ManualAim(uint32_t player) {
    return G<uint8_t>(kernel::R32(player + kPlayerWeapon) + kWeaponFlags) & kManualAim;
}

// manual aim with a weapon whose sight moves
bool SightMoves(uint32_t player) {
    if (!ManualAim(player)) return false;
    int8_t index = G<int8_t>(kernel::R32(player + kPlayerWeapon) + kWeaponRecord);
    uint32_t record = kRecords + uint32_t(index) * kRecordSize;
    return !(G<uint8_t>(record + kRecordFlags) & kSightStays);
}

// in manual aim or zoomed in
bool Aiming(uint32_t player, uint32_t controller) {
    return ManualAim(player) || G<float>(controller + kControllerZoom) > 1.0f;
}

// the value moved by step within its reach; what is left over
float StepWithin(float& value, float step, float reach) {
    float moved = std::clamp(value + step, -reach, reach);
    float rest = value + step - moved;
    value = moved;
    return rest;
}

// the sight moves by the mouse, and past its reach the view turns by the rest, as the sticks turn it at the edge
void MoveSight(uint32_t controller, float across, float up) {
    float turn_across = StepWithin(G<float>(controller + kControllerSightX), across, G<float>(kSightReachX));
    float turn_up = StepWithin(G<float>(controller + kControllerSightY), up, G<float>(kSightReachY));
    G<float>(controller + kControllerYawStep) -= turn_across;
    G<float>(controller + kControllerPitch) += turn_up * kQuarterTurnsPerRadian;
    if (turn_up != 0.0f) G<uint8_t>(controller + kControllerLooking) = 1;
}

// the look adds the stick's pitch to the pitch the frame started with
void ScalePadLook(uint32_t controller, bool aim, float pitch_before) {
    const game::Options& o = game::Opt();
    float x = aim ? o.pad_aim_horizontal_sensitivity : o.pad_horizontal_sensitivity;
    float y = aim ? o.pad_aim_vertical_sensitivity : o.pad_vertical_sensitivity;
    G<float>(controller + kControllerYawStep) *= x;
    float& pitch = G<float>(controller + kControllerPitch);
    pitch = pitch_before + (pitch - pitch_before) * y;
}

bool OnTurret(uint32_t controller) {
    uint32_t mounted = kernel::R32(controller + kControllerMounted);
    return mounted && G<uint8_t>(mounted + kObjectKind) == kTurret;
}

void MouseLook(uint32_t player, uint32_t controller, bool aim) {
    if (OnTurret(controller)) return;  // the turret takes the mouse
    float dx;
    float dy;
    if (!input::TakeMouse(dx, dy, aim)) return;
    uint16_t state = G<uint16_t>(player + kPlayerState);
    if (state == kGuiding) {
        g_missile_dx += dx;
        g_missile_dy += dy;
    }
    if (!Looks(state)) return;
    if (state == kHanging) dx = 0.0f;
    g_missile_dx = 0.0f;
    g_missile_dy = 0.0f;
    float per_count = kRadiansPerCount / std::max(G<float>(controller + kControllerZoom), 1.0f);
    if (SightMoves(player)) {
        MoveSight(controller, dx * per_count, -dy * per_count);
        return;
    }
    G<float>(controller + kControllerYawStep) -= dx * per_count;
    G<float>(controller + kControllerPitch) -= dy * per_count * kQuarterTurnsPerRadian;
    if (dy != 0.0f) G<uint8_t>(controller + kControllerLooking) = 1;
}

// The gyro turns player 1's view beside the stick, as the mouse does: degrees for degrees times its sensitivity,
// narrowed by the zoom. The game clamps the pitch after the look.
void GyroLook(uint32_t player, uint32_t controller) {
    const game::Options& o = game::Opt();
    gyro::Turn turn = gyro::Take(o.gyro);
    if (app::MenuOpen() || !Looks(G<uint16_t>(player + kPlayerState))) return;
    float& pitch = G<float>(controller + kControllerPitch);
    if (turn.recenter) pitch = 0;
    // the gyro's yaw is left positive, as the yaw step, and its pitch down positive
    float yaw = o.invert_pad_x ? -turn.yaw : turn.yaw;
    float per_degree = kRadiansPerDegree / std::max(G<float>(controller + kControllerZoom), 1.0f);
    G<float>(controller + kControllerYawStep) += yaw * per_degree;
    pitch -= turn.pitch * per_degree * kQuarterTurnsPerRadian;
    if (turn.pitch != 0.0f) G<uint8_t>(controller + kControllerLooking) = 1;
}

}  // namespace

// the player's look each frame, from 0xAD520 (player)
X86_EXTERN_FUNC(__imp__sub_000B7610);
X86_FUNC(sub_000B7610) {
    uint32_t player = kernel::R32(ctx.esp + 4);
    uint32_t controller = kernel::R32(player + kPlayerController);
    float pitch = G<float>(controller + kControllerPitch);
    __imp__sub_000B7610(ctx, base);
    bool aim = Aiming(player, controller);
    bool keyboard = G<int8_t>(controller + kControllerPlayer) == input::KeyboardPlayer();
    if (!keyboard || !input::KeyboardActive()) ScalePadLook(controller, aim, pitch);
    if (keyboard) MouseLook(player, controller, aim);
    if (G<int8_t>(controller + kControllerPlayer) == kPlayer1) GyroLook(player, controller);
}

// the sight's pull onto targets each frame (player): the mouse aims for itself, so on the keyboard and mouse it runs
// as with Auto Aim off, whatever the setting, and the drawn sight is where the mouse put it
X86_EXTERN_FUNC(__imp__sub_000B9950);
X86_FUNC(sub_000B9950) {
    uint32_t player_object = kernel::R32(ctx.esp + 4);
    uint32_t controller = kernel::R32(player_object + kPlayerController);
    int player = G<int8_t>(controller + kControllerPlayer);
    if (player != input::KeyboardPlayer() || !input::KeyboardActive()) {
        __imp__sub_000B9950(ctx, base);
        return;
    }
    uint32_t own = uint32_t(player) * kPlayerSettings;
    uint8_t setting[std::size(kAutoAim)];
    for (size_t i = 0; i < std::size(kAutoAim); i++) setting[i] = std::exchange(G<uint8_t>(kAutoAim[i] + own), 0);
    __imp__sub_000B9950(ctx, base);
    for (size_t i = 0; i < std::size(kAutoAim); i++) G<uint8_t>(kAutoAim[i] + own) = setting[i];
    if (!SightMoves(player_object)) return;
    G<float>(controller + kControllerShownX) = G<float>(controller + kControllerSightX);
    G<float>(controller + kControllerShownY) = G<float>(controller + kControllerSightY);
}

// a turret's frame (turret): the mouse aims it beside the sticks, before the frame clamps the aim
X86_EXTERN_FUNC(__imp__sub_00027FC0);
X86_FUNC(sub_00027FC0) {
    uint32_t turret = kernel::R32(ctx.esp + 4);
    bool manned = G<uint16_t>(turret + kObjectState) == kTurretManned;
    float dx;
    float dy;
    if (manned && G<int16_t>(turret + kTurretPlayer) == input::KeyboardPlayer() && input::TakeMouse(dx, dy)) {
        uint32_t data = kernel::R32(turret + kObjectData);
        G<float>(data + kTurretYaw) -= dx * kRadiansPerCount;
        G<float>(data + kTurretPitch) += dy * kRadiansPerCount;
    }
    __imp__sub_00027FC0(ctx, base);
}

// at 0x23838 in a guided missile's flight (the missile in edi): the mouse turns it beside the sticks
void GuidedMissileMouse(X86Context& ctx, uint8_t*) {
    uint32_t owner = kernel::R32(kernel::R32(ctx.edi + kMissileData) + kMissileOwner);
    uint32_t controller = kernel::R32(owner + kPlayerController);
    if (G<int8_t>(controller + kControllerPlayer) != input::KeyboardPlayer()) return;
    float dx;
    float dy;
    if (input::TakeMouse(dx, dy)) {
        g_missile_dx += dx;
        g_missile_dy += dy;
    }
    G<float>(ctx.esp + kMissileTurnAcross) += g_missile_dx * kRadiansPerCount;
    G<float>(ctx.esp + kMissileTurnUpDown) += g_missile_dy * kRadiansPerCount;
    g_missile_dx = 0.0f;
    g_missile_dy = 0.0f;
}
