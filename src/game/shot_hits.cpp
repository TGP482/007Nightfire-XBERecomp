#include <algorithm>
#include <unordered_map>

#include "xdk/hle.h"

// A shot's update (0x23670) tests the path ahead from where it is, a sweep as long as its step but at least 1 when the
// step is at least half its radius, else a sphere of its radius, then moves on its step; a hit stops it where it moved
// to. On the console that is one test per 60 fps step, so a grapple shot (step 0.67) stopped up to a step further on
// than a short high frame rate step, and held Bond lower in a shaft, and past 277 fps a Ronin shot (radius 6.7) took
// the sphere, which loses its hit on Bond. A shot tests once per 60 fps frame, from where it is, with the 60 fps step
// cut by its range as that step is: a short ranged stunner shot otherwise took a long thin sweep where the console
// takes its sphere. Testing from where it was a 60 fps frame ago tested the first step twice and never the last.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint32_t kMove = 0x14;           // this frame's step, from the stack pointer at 0x23DC3
constexpr uint32_t kSweepLength = 0x24;    // from the stack pointer at 0x23F32
// the shot's data, in esi
constexpr uint32_t kTravelled = 0xCC;   // this frame's step included
constexpr uint32_t kSweepReach = 0xB4;  // a hit's distance is its share of this
constexpr uint32_t kRecord = 0x38;      // its weapon record
constexpr uint32_t kRange = 0x1C;       // in the record
constexpr double kHalf = 0.5;
constexpr float kShortestSweep = 1.0f;

std::unordered_map<uint32_t, double> g_due;  // shot -> 60 fps frames since its last test

float TravelledBefore(X86Context& ctx) { return G<float>(ctx.esi + kTravelled) - G<float>(ctx.esp + kMove); }

// the 60 fps step from here, up to the range
float ConsoleStep(X86Context& ctx) {
    float step = G<float>(ctx.esp + kMove) / G<float>(kStepScale);
    float left = G<float>(kernel::R32(ctx.esi + kRecord) + kRange) - TravelledBefore(ctx);
    return std::min(step, left);
}

}  // namespace

// at 0x23DC3, before the test; true skips it (shot in edi, its data in esi)
bool ShotWaits(X86Context& ctx, uint8_t*) {
    double& due = g_due[ctx.edi];
    if (TravelledBefore(ctx) == 0.0f) {
        due = 0.0;
        return false;
    }
    due += G<float>(kStepScale);
    if (due < 1.0) return true;
    due -= 1.0;
    return false;
}

// at 0x23E86, in place of the choice up to 0x23E97; st0 holds the radius
bool ShotSweeps(X86Context& ctx, uint8_t*) {
    return kHalf * X87_ST(0) <= ConsoleStep(ctx);
}

// at 0x23F32, the sweep's length is set
void ShotSweepFrom(X86Context& ctx, uint8_t*) {
    float length = std::max(ConsoleStep(ctx), kShortestSweep);
    G<float>(ctx.esp + kSweepLength) = length;
    G<float>(ctx.esi + kSweepReach) = length;
}
