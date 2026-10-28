#include "xdk/hle.h"

// A flash grenade (0x22210) blinds a player for frames at the rate, then 0xAB0B0 fades the white out over the frames
// left. The fade (0x243B0) takes its time in 60 fps frames, so past 60 fps the white lingered. It gets 60 fps frames.
namespace {

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate

}  // namespace

// at 0xAB161, the fade's time is on the stack
void FlashFadeTime(X86Context& ctx, uint8_t*) {
    kernel::G<float>(ctx.esp) *= kernel::G<float>(kStepScale);
}
