#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "xdk/hle.h"

// A level timer's update (0x20290) counts its frames down once a game frame and sets its flag at zero, so past 60 fps
// timed events such as Countdown's server room doors came late. It counts 60 fps frames instead.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint32_t kData = 0xBC;
// its data
constexpr uint32_t kFramesLeft = 0xC;  // int, negative until started
constexpr uint32_t kFlag = 0x12;       // u16

}  // namespace

// cdecl (timer)
X86_EXTERN_FUNC(__imp__sub_00020290);
X86_FUNC(sub_00020290) {
    static std::unordered_map<uint32_t, double> due;  // 60 fps frames not counted yet
    uint32_t timer = kernel::R32(ctx.esp + 4);
    uint32_t data = timer ? G<uint32_t>(timer + kData) : 0;
    if (!data || !G<uint16_t>(data + kFlag) || G<int32_t>(data + kFramesLeft) <= 0) {
        due.erase(data);
        return __imp__sub_00020290(ctx, base);
    }
    double& frames = due[data];
    frames += G<float>(kStepScale);
    int32_t ticks = int32_t(std::floor(frames));
    frames -= ticks;
    int32_t& left = G<int32_t>(data + kFramesLeft);
    left = std::max(left + 1 - ticks, 1);  // the update takes one more off
    __imp__sub_00020290(ctx, base);
}
