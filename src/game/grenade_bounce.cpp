#include <bit>

#include "xdk/hle.h"

// A bouncing shot such as a grenade (its hit 0x21A00) is pushed off what it hit by the rate over 60 (0x17C0FC) and
// rests once its speed falls under 0.01 times 60 over the rate (0x17C100). Both are distances in 60 fps frames, so
// past 60 fps each bounce lifted it further and it rested later, and it flew and rolled too far. They take the
// console's rate.
namespace {

constexpr uint32_t kConsoleRate = 0x17C0F0;  // int
constexpr float kFramesPerSecond = 60.0f;

}  // namespace

// at 0x21FA9, edx holds the push off
void GrenadeBouncePush(X86Context& ctx, uint8_t*) {
    uint32_t rate = kernel::R32(kConsoleRate);
    if (rate) ctx.edx = std::bit_cast<uint32_t>(float(rate) / kFramesPerSecond);
}

// at 0x22036, st0 holds the step scale the resting speed is scaled by
void GrenadeRestSpeed(X86Context& ctx, uint8_t*) {
    uint32_t rate = kernel::R32(kConsoleRate);
    if (rate) X87_ST(0) = double(kFramesPerSecond / float(rate));
}
