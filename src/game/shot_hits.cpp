#include <algorithm>
#include <array>
#include <unordered_map>

#include "xdk/hle.h"

// A shot's update (0x23670) tests the path ahead from where it is, a sweep as long as its step but at least 1 when the
// step is at least half its radius, else a sphere of its radius, then moves on its step; a hit stops it where it moved
// to. On the console that is one test per 60 fps step, so a grapple shot (step 0.67) stopped up to a step further on
// than a short high frame rate step, and held Bond lower in a shaft, and past 277 fps a Ronin shot (radius 6.7) took
// the sphere, which loses its hit on Bond. A shot tests once per 60 fps frame, from where it was a 60 fps frame ago, as
// the 60 fps step would.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint32_t kStep = 0x10;           // this frame's step, capped by its range, from the stack pointer
constexpr uint32_t kMove = 0x14;           // this frame's step, from the stack pointer at 0x23DC3
constexpr uint32_t kSweepLength = 0x24;    // from the stack pointer at 0x23F32
constexpr uint32_t kPosition = 0x24;       // of the shot, in edi
// the shot's data, in esi
constexpr uint32_t kSphereCentre = 0x7C;
constexpr uint32_t kSweepStart = 0x58;
constexpr uint32_t kSweepEnd = 0x64;  // the start until 0x23F3A adds the sweep
constexpr uint32_t kTravelled = 0xCC;
constexpr double kHalf = 0.5;
constexpr float kShortestSweep = 1.0f;

using Vector = std::array<float, 3>;

struct ShotTest {
    double due = 0.0;  // 60 fps frames since the last test
    Vector last;       // where the shot was at the last test
    Vector from;       // where this test starts
};
std::unordered_map<uint32_t, ShotTest> g_tests;

Vector Read(uint32_t address) { return {G<float>(address), G<float>(address + 4), G<float>(address + 8)}; }

void Write(uint32_t address, const Vector& v) {
    for (uint32_t i = 0; i < v.size(); i++) G<float>(address + i * 4) = v[i];
}

float ConsoleStep(uint32_t esp) { return G<float>(esp + kStep) / G<float>(kStepScale); }

}  // namespace

// at 0x23DC3, before the test; true skips it (shot in edi, its data in esi)
bool ShotWaits(X86Context& ctx, uint8_t*) {
    ShotTest& test = g_tests[ctx.edi];
    Vector here = Read(ctx.edi + kPosition);
    if (G<float>(ctx.esi + kTravelled) == G<float>(ctx.esp + kMove)) {
        test = {0.0, here, here};
        return false;
    }
    test.due += G<float>(kStepScale);
    if (test.due < 1.0) return true;
    test.due -= 1.0;
    test.from = test.last;
    test.last = here;
    return false;
}

// at 0x23E86, in place of the choice up to 0x23E97; st0 holds the radius
bool ShotSweeps(X86Context& ctx, uint8_t*) {
    return kHalf * X87_ST(0) <= ConsoleStep(ctx.esp);
}

// at 0x23EB8, the sphere is placed
void ShotSphereFrom(X86Context& ctx, uint8_t*) {
    Write(ctx.esi + kSphereCentre, g_tests[ctx.edi].from);
}

// at 0x23F32, the sweep's start and length are set
void ShotSweepFrom(X86Context& ctx, uint8_t*) {
    const Vector& from = g_tests[ctx.edi].from;
    Write(ctx.esi + kSweepStart, from);
    Write(ctx.esi + kSweepEnd, from);
    G<float>(ctx.esp + kSweepLength) = std::max(ConsoleStep(ctx.esp), kShortestSweep);
}
