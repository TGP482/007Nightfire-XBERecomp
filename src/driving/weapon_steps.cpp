#include "driving/frame_rate.h"
#include "xdk/hle.h"

// Homing missile steps that need more than a generic scale (config/driving.toml), each sized for one console tick.
namespace {

using kernel::G;

// per tick constants
constexpr uint32_t kTurnBack = 0x1C3684;  // 0.005, the turn rate relaxing
constexpr uint32_t kFadeIn = 0x18B928;    // 1/15

}  // namespace

// after one step was taken off the turn rate (0x601F0, 0x60246)
void DrivingMissileTurnBack(X86Context& ctx, uint8_t*) {
    driving::AddSteps(ctx, -G<float>(kTurnBack));
}

// after one step was added to the fade in (0x60A73)
void DrivingMissileFadeIn(X86Context& ctx, uint8_t*) {
    driving::AddSteps(ctx, G<float>(kFadeIn));
}

// after 0x60AFF: the wreck's countdown (eax less one in ecx, stored to +0x8C) only drops on a tick; at zero it fires
void DrivingMissileWreckCountdown(X86Context& ctx, uint8_t*) {
    if (!driving::UpdateTicked() && ctx.eax != 0) ctx.ecx = ctx.eax;
}
