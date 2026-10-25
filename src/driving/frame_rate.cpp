#include <algorithm>
#include <chrono>

#include "xdk/hle.h"

// The engine steps on a 1000 / 60 ms timer (0x10AE50), cut to 16 ms: its callback (0x5B9F0) counts ticks and the main
// loop's frame (0x5BA80) runs the game once a new tick and draws only then, which holds it at 62.5 fps. A tick is
// counted each frame instead, and the step the game moves by (1/60 at 0x189FD4) is the frame's own length.
namespace {

constexpr uint32_t kStep = 0x189FD4;  // seconds
// ticks, and every second tick (0x5B9F0)
constexpr uint32_t kTicks = 0x1E5204;
constexpr uint32_t kHalfTicks = 0x1E5208;
constexpr double kMinRate = 30.0;  // a longer frame slows the game down, as on the console
constexpr double kSmoothing = 0.1;  // of each frame's length, so its jitter does not reach the step

}  // namespace

// the timer's tick, which the frame counts now
X86_FUNC(sub_0005B9F0) { kernel::Return(ctx, 0, 0); }

// the main loop's frame, thiscall (scheduler)
X86_EXTERN_FUNC(__imp__sub_0005BA80);
X86_FUNC(sub_0005BA80) {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point last = Clock::now();
    static double step = 1.0 / 60;
    Clock::time_point now = Clock::now();
    double seconds = std::chrono::duration<double>(now - last).count();
    last = now;
    step += (std::min(seconds, 1.0 / kMinRate) - step) * kSmoothing;
    kernel::G<float>(kStep) = float(step);
    uint32_t ticks = ++kernel::G<uint32_t>(kTicks);
    if (ticks % 2 == 0) kernel::G<uint32_t>(kHalfTicks)++;
    __imp__sub_0005BA80(ctx, base);
}
