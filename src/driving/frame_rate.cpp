#include <algorithm>
#include <chrono>
#include <cmath>

#include "driving/frame_rate.h"
#include "xdk/hle.h"

// The engine steps on a 16 ms timer (0x10AE50): its callback (0x5B9F0) counts ticks and the main loop's frame (0x5BA80)
// runs one game update per tick, so it holds at 62.5 fps. Each frame now runs as many updates as it needs so that
// none covers more than a tick, each moving by its share of the frame (the step at 0x234E30). The game clock's tick
// count (0x234E34) still counts whole console ticks of real time, so everything timed in ticks keeps its pace.
namespace {

using kernel::G;

// scheduler (0x1E520C)
constexpr uint32_t kTicks = 0x1E5204;
constexpr uint32_t kHalfTicks = 0x1E5208;
// game clock (0x233FF0)
constexpr uint32_t kRate = 0x234E2C;  // updates per second
constexpr uint32_t kStep = 0x234E30;  // seconds
constexpr uint32_t kClockState = 0xE34;
constexpr uint32_t kClockCount = 0xE44;
constexpr uint32_t kPaused = 3;

constexpr double kTickSeconds = 0.016;
constexpr double kMinRate = 30.0;   // a longer frame slows the game down, as on the console
constexpr double kSmoothing = 0.1;  // of each frame's length, so its jitter does not reach the step

struct Time {
    double frame_ticks = 1.0;
    double update_ticks = 1.0;
    double carry = 0.0;  // of a tick, not counted yet
    uint32_t frame_crossed = 0;
    bool update_ticked = false;
    uint32_t updates = 0;
    uint32_t ticks = 0;  // added to the scheduler, for its half count
};

Time g_time;

double FrameSeconds() {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point last = Clock::now();
    static double frame = kTickSeconds;
    Clock::time_point now = Clock::now();
    double seconds = std::chrono::duration<double>(now - last).count();
    last = now;
    frame += (std::min(seconds, 1.0 / kMinRate) - frame) * kSmoothing;
    return frame;
}

void AddSchedulerTicks(uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        G<uint32_t>(kTicks)++;
        if (++g_time.ticks % 2 == 0) G<uint32_t>(kHalfTicks)++;
    }
}

}  // namespace

namespace driving {

double UpdateTicks() { return g_time.update_ticks; }
bool UpdateTicked() { return g_time.update_ticked; }
double FrameTicks() { return g_time.frame_ticks; }
uint32_t FrameTicksCrossed() { return g_time.frame_crossed; }
uint32_t UpdateCount() { return g_time.updates; }

double Ease(double gain) {
    if (gain <= 0.0 || gain >= 1.0) return gain;
    return 1.0 - std::pow(1.0 - gain, g_time.update_ticks);
}

double Decay(double factor) {
    if (factor <= 0.0) return factor;
    return std::pow(factor, g_time.update_ticks);
}

void AddSteps(X86Context& ctx, double step) { X87_ST(0) += step * (g_time.update_ticks - 1.0); }

void ApplyFactor(X86Context& ctx, double factor) {
    if (factor > 0.0) X87_ST(0) *= Decay(factor) / factor;
}

void ScaleShare(X86Context& ctx, double share) {
    if (share > 0.0 && share < 1.0) X87_ST(0) *= Ease(share) / share;
}

}  // namespace driving

// the timer's tick, which the frame counts now
X86_FUNC(sub_0005B9F0) { kernel::Return(ctx, 0, 0); }

// the main loop's frame, thiscall (scheduler)
X86_EXTERN_FUNC(__imp__sub_0005BA80);
X86_FUNC(sub_0005BA80) {
    double ticks = FrameSeconds() / kTickSeconds;
    uint32_t updates = uint32_t(std::ceil(ticks));
    g_time.frame_ticks = ticks;
    g_time.update_ticks = ticks / updates;
    g_time.frame_crossed = 0;
    int32_t rate = G<int32_t>(kRate);
    if (rate > 0) G<float>(kStep) = float(g_time.update_ticks / rate);
    AddSchedulerTicks(updates);
    __imp__sub_0005BA80(ctx, base);
}

// the game clock's update, thiscall: counts the console ticks real time crossed
X86_FUNC(sub_000B2620) {
    uint32_t clock = ctx.ecx;
    g_time.update_ticked = false;
    if (G<uint32_t>(clock + kClockState) != kPaused) {
        g_time.carry += g_time.update_ticks;
        uint32_t whole = uint32_t(g_time.carry);
        g_time.carry -= whole;
        G<uint32_t>(clock + kClockCount) += whole;
        g_time.update_ticked = whole > 0;
        g_time.frame_crossed += whole;
        g_time.updates++;
    }
    kernel::Return(ctx, ctx.eax, 0);
}
