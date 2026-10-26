#include <bit>

#include "driving/frame_rate.h"
#include "xdk/hle.h"

// People and props that step by a fixed amount per console tick (config/driving.toml).
namespace {

using kernel::G;

constexpr uint32_t kHeadingStep = 0x18A4C4;  // 0.0025 of a turn

}  // namespace

// at 0x2A40F: edx holds a walker's distance per tick along its path (0x1DDF0C)
void DrivingPathStep(X86Context& ctx, uint8_t*) {
    float step = std::bit_cast<float>(ctx.edx) * float(driving::UpdateTicks());
    ctx.edx = std::bit_cast<uint32_t>(step);
}

// before the heading (+0x68, 0xBAF60) turns one step toward its target, up or down
void DrivingHeadingStep(X86Context& ctx, uint8_t*) { driving::AddSteps(ctx, G<float>(kHeadingStep)); }
void DrivingHeadingStepBack(X86Context& ctx, uint8_t*) { driving::AddSteps(ctx, -G<float>(kHeadingStep)); }
