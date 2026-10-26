#include "xdk/hle.h"

// A helicopter (kind 0x3B, 0x6BFB0) flies its path in 0x6BC20: each frame its place on the path goes up by the path's
// step (+0xC, set by 0x6B930 for a 60 fps frame), so past 60 fps it flew faster. The step is scaled by 60 over the rate.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint32_t kPathStep = 0xC;        // the path, in edi
constexpr uint32_t kPathPlace = 0x5C;      // the helicopter's data, in esi
constexpr uint32_t kDetached = 0x74;       // the bool argument, from the stack pointer at 0x6BC68
constexpr uint32_t kCallArgs = 0x1C;       // 0xD8DF0's arguments, which the code pops next

}  // namespace

// at 0x6BC68, in place of the place's step and the argument's test up to the jump at 0x6BC7A
bool HelicopterPathStep(X86Context& ctx, uint8_t*) {
    float& place = G<float>(ctx.esi + kPathPlace);
    place += G<float>(ctx.edi + kPathStep) * G<float>(kStepScale);
    bool detached = G<uint8_t>(ctx.esp + kDetached) != 0;
    ctx.esp += kCallArgs;
    return detached;
}
