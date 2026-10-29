#include <cmath>
#include <unordered_map>

#include "xdk/hle.h"

// A decrypting keypad (0xD1550, state 5) steps once a frame: up while a decryptor shot touches it, down 4 while none
// does. A part the laser cuts (state 0, type 1) does too: worn down while a laser shot touches it, healed once none has
// for 2 frames. Shots come 60 a second, one a frame on the console, so past 60 fps most frames have none. Each shot
// steps it as it lands, and a frame without one steps it once per 60 fps frame only after a 60 fps frame with no shot,
// as the next shot may land a frame late.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate

// target object
constexpr uint32_t kState = 0xD0;
constexpr uint16_t kIdle = 0;
constexpr uint16_t kDecrypting = 5;
constexpr uint32_t kData = 0xBC;
constexpr uint32_t kHits = 0xAC;  // list: +0x4 next, +0x4C the object that hit
constexpr uint32_t kNextHit = 0x4;
constexpr uint32_t kHitObject = 0x4C;
// its data
constexpr uint32_t kType = 0x0;  // u16
constexpr uint16_t kLaserCut = 1;

// the object that hit: a shot's data holds its weapon record, item id first
constexpr uint32_t kKind = 0xDB;
constexpr uint8_t kShot = 5;
constexpr uint32_t kShotWeapon = 0x38;

// the weapons whose shots step a target, by item id
struct Shots {
    uint16_t item;
    uint16_t upgraded;
};
constexpr Shots kDecryptor = {0x56, 0x57};
constexpr Shots kLaser = {0x4E, 0x4F};

constexpr double kShotGap = 2.0;  // 60 fps frames since the last shot before one counts as none

bool ShotOf(uint32_t object, const Shots& shots) {
    if (!object || G<uint8_t>(object + kKind) != kShot) return false;
    uint32_t weapon = G<uint32_t>(G<uint32_t>(object + kData) + kShotWeapon);
    uint16_t item = G<uint16_t>(weapon);
    return item == shots.item || item == shots.upgraded;
}

bool TouchedBy(uint32_t target, const Shots& shots) {
    for (uint32_t hit = G<uint32_t>(target + kHits); hit; hit = G<uint32_t>(hit + kNextHit)) {
        if (ShotOf(G<uint32_t>(hit + kHitObject), shots)) return true;
    }
    return false;
}

// the shots that step it, null when it steps every frame
const Shots* StepShots(uint32_t target) {
    uint16_t state = G<uint16_t>(target + kState);
    if (state == kDecrypting) return &kDecryptor;
    bool laser_cut = state == kIdle && G<uint16_t>(G<uint32_t>(target + kData) + kType) == kLaserCut;
    return laser_cut ? &kLaser : nullptr;
}

// true when this frame runs the target's step; idle is 60 fps frames since a shot last touched it
bool UpdateIdle(double& idle, bool touched) {
    if (touched) {
        idle = 0.0;
        return true;
    }
    double before = idle;
    idle += G<float>(kStepScale);
    return idle >= kShotGap && std::floor(idle) > std::floor(before);
}

}  // namespace

// a keypad's or a laser cut part's update, cdecl (target)
X86_EXTERN_FUNC(__imp__sub_000D1550);
X86_FUNC(sub_000D1550) {
    static std::unordered_map<uint32_t, double> idle;
    uint32_t target = kernel::R32(ctx.esp + 4);
    const Shots* shots = StepShots(target);
    if (!shots) {
        idle.erase(target);
        return __imp__sub_000D1550(ctx, base);
    }
    if (!UpdateIdle(idle[target], TouchedBy(target, *shots))) {
        kernel::Return(ctx, 0, 0);
        return;
    }
    __imp__sub_000D1550(ctx, base);
}
