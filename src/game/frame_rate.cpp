#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

#include "xdk/hle.h"

// The game steps a fixed 1/60 s each frame (1/50 on PAL): its main loop passes that rate to 0x6B040, which sets the
// step the whole game reads (the rate at 0x17C0F4, one over it at 0x17C104). It gets the rate the frames really come
// at instead, so the game keeps its speed at any frame rate.

namespace {

constexpr uint32_t kRate = 4;   // both arguments, the rate twice, from the stack pointer
constexpr uint32_t kRate2 = 8;
constexpr double kMinRate = 30.0;  // a longer frame slows the game down, as on the console
constexpr double kMaxRate = 1000.0;
constexpr std::chrono::duration<double> kMinFrame{1.0 / kMaxRate};
// of each frame's own length, so its jitter does not reach the step; lengths, not rates, so the steps add up to real
// time when frames vary
constexpr double kSmoothing = 0.1;

}  // namespace

X86_EXTERN_FUNC(__imp__sub_0006B040);
X86_FUNC(sub_0006B040) {
    using clock = std::chrono::steady_clock;
    static clock::time_point last;
    static double frame = 0.0;  // seconds
    // a frame quicker than the shortest step waits out the rest, so the step stays the frame's own length
    while (frame != 0.0 && clock::now() - last < kMinFrame) std::this_thread::yield();
    clock::time_point now = clock::now();
    if (frame == 0.0) {
        frame = 1.0 / double(kernel::R32(ctx.esp + kRate));
    } else {
        double seconds = std::chrono::duration<double>(now - last).count();
        frame += (std::clamp(seconds, 1.0 / kMaxRate, 1.0 / kMinRate) - frame) * kSmoothing;
    }
    last = now;
    uint32_t whole = uint32_t(std::lround(1.0 / frame));
    kernel::W32(ctx.esp + kRate, whole);
    kernel::W32(ctx.esp + kRate2, whole);
    __imp__sub_0006B040(ctx, base);
}
