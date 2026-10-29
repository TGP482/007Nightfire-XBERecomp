#include "xdk/hle.h"

// A shot's update (0x23670) tests its path as a sweep when its step this frame is at least half its radius, else as a
// sphere of its radius. A Ronin shot's radius is 6.7, and the sphere test loses its hit on Bond, but its 60 fps step
// of 15.5 always sweeps on the console. Past 277 fps its step got shorter and the Ronin missed. The test is chosen by
// the 60 fps step.
namespace {

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint32_t kStep = 0x10;           // this frame's step, from the stack pointer
constexpr double kHalf = 0.5;

}  // namespace

// at 0x23E86, in place of the test up to 0x23E97; st0 holds the radius
bool ShotSweeps(X86Context& ctx, uint8_t*) {
    double step = kernel::G<float>(ctx.esp + kStep) / kernel::G<float>(kStepScale);
    return kHalf * X87_ST(0) <= step;
}
