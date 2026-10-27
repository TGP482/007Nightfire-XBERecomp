#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "xdk/hle.h"

// A level timer (kind 0x21, made by 0x20040) turns its delay in seconds into frames at the rate it was made at
// (0x2011A), then its update (0x20290) counts one off each frame, so a timer made at one rate and run at another,
// such as Countdown's server room doors, came late. Its frames are made and counted at the console's rate.
namespace {

using kernel::G;

constexpr uint32_t kConsoleRate = 0x17C0F0;  // int
constexpr uint32_t kFrameTime = 0x17C104;    // one over the rate
constexpr uint32_t kData = 0xBC;
// its data
constexpr uint32_t kFramesLeft = 0xC;  // int, negative until started
constexpr uint32_t kFlag = 0x12;       // u16

}  // namespace

// at 0x2011A, in place of the delay in seconds times the rate
bool TimerDelayFrames(X86Context& ctx, uint8_t*) {
    uint32_t rate = kernel::R32(kConsoleRate);
    if (!rate) return false;
    X87_ST(0) *= double(rate);
    return true;
}

// cdecl (timer)
X86_EXTERN_FUNC(__imp__sub_00020290);
X86_FUNC(sub_00020290) {
    static std::unordered_map<uint32_t, double> due;  // console frames not counted yet
    uint32_t timer = kernel::R32(ctx.esp + 4);
    uint32_t data = timer ? G<uint32_t>(timer + kData) : 0;
    if (!data || !G<uint16_t>(data + kFlag) || G<int32_t>(data + kFramesLeft) <= 0) {
        due.erase(data);
        return __imp__sub_00020290(ctx, base);
    }
    double& frames = due[data];
    frames += kernel::R32(kConsoleRate) * G<float>(kFrameTime);
    int32_t ticks = int32_t(std::floor(frames));
    frames -= ticks;
    int32_t& left = G<int32_t>(data + kFramesLeft);
    left = std::max(left - ticks, 0) + 1;  // the update takes one more off
    __imp__sub_00020290(ctx, base);
    if (left == 0) due.erase(data);
}
