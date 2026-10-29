#include "xdk/hle.h"

// A flash grenade (0x22210) blinds a player (0xABDC0) for its time in frames of the rate it went off at, which 0xAB0B0
// counts down one per frame, so any later frame rate stretched the white. The time is kept in 60 fps frames instead,
// as a level's screen flash (0xA4200) gives it, and counts down by 60 over the rate.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint32_t kBlindLeft = 0x8AC;     // float, in the player data (esi)
constexpr uint32_t kBlindTime = 4;         // 0xABDC0's second argument, from the stack pointer

}  // namespace

// at 0x22750, the grenade's call to 0xABDC0
void FlashBlindTime(X86Context& ctx, uint8_t*) {
    G<float>(ctx.esp + kBlindTime) *= G<float>(kStepScale);
}

// at 0xAB0C3, in place of the count's step up to 0xAB0D5
bool FlashBlindStep(X86Context& ctx, uint8_t*) {
    G<float>(ctx.esi + kBlindLeft) -= G<float>(kStepScale);
    return true;
}
