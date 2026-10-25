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
constexpr uint32_t kPlayerWeapon = 0xB8;
constexpr uint32_t kWeaponFlags = 0x50;  // bit 0 set by the manual aim button, 0xBA8F0
constexpr uint8_t kManualAim = 1;
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

// mouse counts the player's look took while steering a missile, for its turn
float g_missile_dx = 0.0f;
float g_missile_dy = 0.0f;

bool Looks(uint16_t state) { return state < 8 || (state > 12 && state != 16); }

// in manual aim or zoomed in
bool Aiming(uint32_t player, uint32_t controller) {
    bool manual = G<uint8_t>(kernel::R32(player + kPlayerWeapon) + kWeaponFlags) & kManualAim;
    return manual || G<float>(controller + kControllerZoom) > 1.0f;
}

// the look adds the stick's pitch to the pitch the frame started with
void ScalePadLook(uint32_t controller, bool aim, float pitch_before) {
    const game::Options& o = game::Opt();
    float x = o.pad_horizontal_sensitivity * (aim ? o.pad_aim_horizontal_sensitivity : 1.0f);
    float y = o.pad_vertical_sensitivity * (aim ? o.pad_aim_vertical_sensitivity : 1.0f);
    G<float>(controller + kControllerYawStep) *= x;
    float& pitch = G<float>(controller + kControllerPitch);
    pitch = pitch_before + (pitch - pitch_before) * y;
}

void MouseLook(uint32_t player, uint32_t controller, bool aim) {
    float dx;
    float dy;
    if (!input::TakeMouse(dx, dy)) return;
    uint16_t state = G<uint16_t>(player + kPlayerState);
    if (state == kGuiding) {
        g_missile_dx += dx;
        g_missile_dy += dy;
    }
    if (!Looks(state)) return;
    g_missile_dx = 0.0f;
    g_missile_dy = 0.0f;
    if (aim) {
        dx *= game::Opt().aim_horizontal_sensitivity;
        dy *= game::Opt().aim_vertical_sensitivity;
    }
    float per_count = kRadiansPerCount / std::max(G<float>(controller + kControllerZoom), 1.0f);
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
// as with Auto Aim off, whatever the setting
X86_EXTERN_FUNC(__imp__sub_000B9950);
X86_FUNC(sub_000B9950) {
    uint32_t controller = kernel::R32(kernel::R32(ctx.esp + 4) + kPlayerController);
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
