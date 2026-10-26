#include <algorithm>

#include "xdk/hle.h"

// A thrown bomb (weapon 0x3B, its shot's frame 0x22210) loses health for each laser shot touching it: 60 over the rate
// cut to a whole number, times a weapon factor for 0x4F. The laser fires at 60 shots a second at any frame rate, so past
// 60 fps the scale shrank each shot's damage (to 0 for 0x4E). A shot does its 60 fps damage instead.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;    // 60 over the rate
constexpr uint32_t kStrongFactor = 0x15D424;  // weapon 0x4F's damage per 60 fps frame
constexpr uint16_t kLaser = 0x4E;
constexpr uint16_t kStrongLaser = 0x4F;

float DamagePerFrame(uint16_t weapon) {
    if (weapon == kLaser) return 1.0f;
    if (weapon == kStrongLaser) return G<float>(kStrongFactor);
    return 0.0f;
}

}  // namespace

// at 0x22965, in place of the touching shot's damage up to its test at 0x2298F; the touching shot's weapon in di
bool BombLaserDamage(X86Context& ctx, uint8_t*) {
    float scale = std::max(G<float>(kStepScale), 1.0f);
    ctx.eax = uint32_t(DamagePerFrame(uint16_t(ctx.edi)) * scale);
    return true;
}
