#include "xdk/hle.h"

// A triggered scenery animation (kind 0x27, its update 0x6C6B0) plays once its trigger is set, its frame going up by
// its speed each game frame, so past 60 fps it played faster. The speed is scaled by 60 over the rate.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
// the animation's data, in esi
constexpr uint32_t kFrame = 0x0;
constexpr uint32_t kSpeed = 0x4;

}  // namespace

// at 0x6C7B4, in place of loading the frame plus the speed up to 0x6C7B9
bool TriggeredAnimationStep(X86Context& ctx, uint8_t*) {
    float speed = G<float>(ctx.esi + kSpeed) * G<float>(kStepScale);
    x87_push(ctx, G<float>(ctx.esi + kFrame) + speed);
    return true;
}
