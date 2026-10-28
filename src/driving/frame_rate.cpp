#include <algorithm>
#include <chrono>
#include <cmath>
#include <initializer_list>

#include "common/game_options.h"
#include "driving/frame_rate.h"
#include "xdk/hle.h"

// The engine steps on a 16 ms timer (0x10AE50): its callback (0x5B9F0) counts ticks and the main loop's frame (0x5BA80)
// runs one game update per tick, so it holds at 62.5 fps. Each frame now runs as many updates as it needs so that
// none covers more than a tick, each moving by its share of the frame (the step at 0x234E30). The game clock's tick
// count (0x234E34) still counts whole console ticks of real time, so everything timed in ticks keeps its pace.
// In-game cutscenes run one whole tick per update, as on the console, so their scripted shots and cues land on time;
// a frame without a tick draws again, between the last two ticks (cutscene_motion.cpp).
namespace {

using kernel::G;

// scheduler (0x1E520C)
constexpr uint32_t kTicks = 0x1E5204;
constexpr uint32_t kHalfTicks = 0x1E5208;
constexpr uint32_t kDrawList = 0x0;  // its vt +4 runs a phase (tick, phase)
constexpr uint32_t kRunPhase = 0x4;
constexpr uint32_t kPhases = 8;
constexpr uint32_t kLastTick = 0x14;
constexpr uint32_t kOwnLoop = 0x1D;  // byte, set: the frame runs its other loop (0x5BA9E)
constexpr uint32_t kSpeed = 0x20;
constexpr uint32_t kRunCommands = 0x5A600;
// game clock
constexpr uint32_t kClock = 0x233FF0;
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
    double whole_carry = 0.0;  // of a tick, for cutscenes
    double between = 1.0;      // share of the next tick a cutscene frame is drawn at
    uint32_t frames = 0;
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

// how far the drawn frame is towards the scheduler's next tick: a speed under 1 ticks every few counts (0x5BB45
// truncates), and a frame that runs ticks runs all it has
double ShareOfNextTick(uint32_t scheduler) {
    float speed = G<float>(scheduler + kSpeed);
    if (speed <= 0.0f || G<uint8_t>(scheduler + kOwnLoop)) return 1.0;
    uint32_t pending = G<uint32_t>(kTicks) - G<uint32_t>(scheduler + kLastTick);
    uint32_t due = uint32_t(pending * speed);
    if (due > 1) return 1.0;  // a slow frame shows its last tick
    bool runs = due > 0;
    double counts_per_tick = speed >= 1.0f ? 1.0 : std::ceil(1.0 / speed - 1e-6);
    double since = (runs ? 0.0 : pending) + g_time.whole_carry;
    return std::min(since / counts_per_tick, 1.0);
}

// the draw list's phases and their commands, as the scheduler runs them after its last tick (0x5BBA9)
void DrawAgain(X86Context& ctx, uint32_t scheduler) {
    uint32_t list = G<uint32_t>(scheduler + kDrawList);
    uint32_t tick = G<uint32_t>(scheduler + kLastTick);
    for (uint32_t phase = 0; phase < kPhases; phase++) {
        ctx.ecx = list;
        kernel::CallGuest(ctx, G<uint32_t>(G<uint32_t>(list) + kRunPhase), {tick, phase});
    }
    kernel::CallGuest(ctx, kRunCommands, {});
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
double TickCarry() { return g_time.carry; }
double Between() { return g_time.between; }
uint32_t FrameCount() { return g_time.frames; }

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
    uint32_t scheduler = ctx.ecx;
    bool cutscene = driving::Cutscene();
    double ticks = FrameSeconds() / kTickSeconds;
    uint32_t updates = uint32_t(std::ceil(ticks));
    if (cutscene) {
        g_time.whole_carry += ticks;
        updates = uint32_t(g_time.whole_carry);
        g_time.whole_carry -= updates;
        ticks = updates;
    }
    g_time.frame_ticks = ticks;
    g_time.update_ticks = updates ? ticks / updates : 1.0;
    g_time.frame_crossed = 0;
    int32_t rate = G<int32_t>(kRate);
    if (rate > 0) G<float>(kStep) = float(g_time.update_ticks / rate);
    AddSchedulerTicks(updates);
    g_time.frames++;
    bool paused = G<uint32_t>(kClock + kClockState) == kPaused;
    g_time.between = cutscene && !paused ? ShareOfNextTick(scheduler) : 1.0;
    uint32_t last_tick = G<uint32_t>(scheduler + kLastTick);
    __imp__sub_0005BA80(ctx, base);
    if (cutscene && G<uint32_t>(scheduler + kLastTick) == last_tick) DrawAgain(ctx, scheduler);
    game::SaveChangedOptions(ctx);
    game::FollowOutputAspect();
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
