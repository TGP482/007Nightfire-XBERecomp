#include <cmath>
#include <unordered_map>

#include "xdk/hle.h"

// A thrown bomb (weapon 0x3B, its shot's frame 0x22210) loses health each frame a laser shot touches it: 60 over the
// rate cut to a whole number, times a weapon factor for 0x4F. Past 60 fps the cut is 0 and lasers never stop it. The
// damage builds up on the bomb instead, so each second does what it does at 60 fps.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;    // 60 over the rate
constexpr uint32_t kStrongFactor = 0x15D424;  // weapon 0x4F's damage per 60 fps frame
constexpr uint16_t kLaser = 0x4E;
constexpr uint16_t kStrongLaser = 0x4F;

std::unordered_map<uint32_t, double> g_due;  // bomb's shot data -> damage not taken yet

double DamagePerFrame(uint16_t weapon) {
    if (weapon == kLaser) return 1.0;
    if (weapon == kStrongLaser) return G<float>(kStrongFactor);
    return 0.0;
}

}  // namespace

// at 0x22965, in place of the touching shot's damage up to its test at 0x2298F; the touching shot's weapon in di,
// the bomb's shot data in ebx
bool BombLaserDamage(X86Context& ctx, uint8_t*) {
    double& due = g_due[ctx.ebx];
    due += DamagePerFrame(uint16_t(ctx.edi)) * G<float>(kStepScale);
    double whole = std::floor(due);
    due -= whole;
    ctx.eax = uint32_t(whole);
    return true;
}
