#include <algorithm>
#include <iterator>
#include <utility>

#include "common/mouse_keyboard.h"
#include "xdk/hle.h"

// The mouse turns the view itself: the game's turn and look are stick rates, ramped and capped by 0xB7420.
namespace {

using kernel::G;

constexpr uint32_t kPlayerState = 0xD2;  // u16; 8 to 12 and 16 have no look, 0xB7610 leaves them
constexpr uint32_t kPlayerController = 0xBC;
constexpr uint32_t kControllerPlayer = 0x8DE;
// radians this frame, right negative; 0xAD520 turns the player by it after the look
constexpr uint32_t kControllerYawStep = 0x28;
// quarter turns, up positive; 0xB7A50 clamps it to -1..1 after the look
constexpr uint32_t kControllerPitch = 0x838;
constexpr uint32_t kControllerZoom = 0x860;  // the view's magnification, 1 unzoomed; the stick's look divides by it
constexpr uint32_t kControllerLooking = 0xF0;  // set while the stick moves the view up or down

constexpr float kRadiansPerCount = 0.04f * 0.017453292f;  // at sensitivity 1, as the other recomps
constexpr float kQuarterTurnsPerRadian = 0.63661975f;

// the game's Auto Aim setting, player 1's; the second while 0x260018 is set
constexpr uint32_t kAutoAim[] = {0x1FE6D1, 0x1FE6D2};

bool Looks(uint16_t state) { return state < 8 || (state > 12 && state != 16); }

}  // namespace

// the player's look each frame, from 0xAD520 (player)
X86_EXTERN_FUNC(__imp__sub_000B7610);
X86_FUNC(sub_000B7610) {
    uint32_t player = kernel::R32(ctx.esp + 4);
    __imp__sub_000B7610(ctx, base);
    uint32_t controller = kernel::R32(player + kPlayerController);
    float dx;
    float dy;
    if (G<int8_t>(controller + kControllerPlayer) != 0 || !input::TakeMouse(dx, dy)) return;
    if (!Looks(G<uint16_t>(player + kPlayerState))) return;
    float per_count = kRadiansPerCount / std::max(G<float>(controller + kControllerZoom), 1.0f);
    G<float>(controller + kControllerYawStep) -= dx * per_count;
    G<float>(controller + kControllerPitch) -= dy * per_count * kQuarterTurnsPerRadian;
    if (dy != 0.0f) G<uint8_t>(controller + kControllerLooking) = 1;
}

// the sight's pull onto targets each frame (player): the mouse aims for itself, so on the keyboard and mouse it runs
// as with Auto Aim off, whatever the setting
X86_EXTERN_FUNC(__imp__sub_000B9950);
X86_FUNC(sub_000B9950) {
    uint32_t controller = kernel::R32(kernel::R32(ctx.esp + 4) + kPlayerController);
    if (G<int8_t>(controller + kControllerPlayer) != 0 || !input::KeyboardActive()) {
        __imp__sub_000B9950(ctx, base);
        return;
    }
    uint8_t setting[std::size(kAutoAim)];
    for (size_t i = 0; i < std::size(kAutoAim); i++) setting[i] = std::exchange(G<uint8_t>(kAutoAim[i]), 0);
    __imp__sub_000B9950(ctx, base);
    for (size_t i = 0; i < std::size(kAutoAim); i++) G<uint8_t>(kAutoAim[i]) = setting[i];
}
