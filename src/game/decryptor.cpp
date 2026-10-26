#include <cmath>
#include <unordered_map>

#include "xdk/hle.h"

// A decrypting keypad (0xD1550, state 5) steps once a frame: up while a decryptor shot touches it, down 4 while none
// does. Shots leave at a rate in time, so past 60 fps most frames have none and it never opens. It steps once per
// 60 fps frame instead, on the first frame a shot touches it, else on the last.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint16_t kDecrypting = 5;

// keypad object
constexpr uint32_t kState = 0xD0;
constexpr uint32_t kHits = 0xAC;  // list: +0x4 next, +0x4C the object that hit
constexpr uint32_t kNextHit = 0x4;
constexpr uint32_t kHitObject = 0x4C;

// the object that hit: a shot's data holds its weapon record, item id first
constexpr uint32_t kKind = 0xDB;
constexpr uint8_t kShot = 5;
constexpr uint32_t kShotData = 0xBC;
constexpr uint32_t kShotWeapon = 0x38;
constexpr uint16_t kDecryptor = 0x56;
constexpr uint16_t kDecryptorUpgraded = 0x57;

struct KeypadStep {
    double due = 0.0;      // 60 fps frames not stepped yet
    bool stepped = false;  // in the current 60 fps frame
};

bool DecryptorShot(uint32_t object) {
    if (!object || G<uint8_t>(object + kKind) != kShot) return false;
    uint32_t weapon = G<uint32_t>(G<uint32_t>(object + kShotData) + kShotWeapon);
    uint16_t item = G<uint16_t>(weapon);
    return item == kDecryptor || item == kDecryptorUpgraded;
}

bool TouchedByDecryptor(uint32_t keypad) {
    for (uint32_t hit = G<uint32_t>(keypad + kHits); hit; hit = G<uint32_t>(hit + kNextHit)) {
        if (DecryptorShot(G<uint32_t>(hit + kHitObject))) return true;
    }
    return false;
}

// true when this frame runs the keypad's step
bool UpdateKeypadStep(KeypadStep& step, bool touched) {
    bool run = touched && !step.stepped;
    step.stepped |= run;
    step.due += G<float>(kStepScale);
    if (step.due < 1.0) return run;
    run |= !step.stepped;
    step.due -= std::floor(step.due);
    step.stepped = false;
    return run;
}

}  // namespace

// a keypad's update, cdecl (keypad)
X86_EXTERN_FUNC(__imp__sub_000D1550);
X86_FUNC(sub_000D1550) {
    static std::unordered_map<uint32_t, KeypadStep> steps;
    uint32_t keypad = kernel::R32(ctx.esp + 4);
    if (G<uint16_t>(keypad + kState) != kDecrypting) {
        steps.erase(keypad);
        return __imp__sub_000D1550(ctx, base);
    }
    if (!UpdateKeypadStep(steps[keypad], TouchedByDecryptor(keypad))) {
        kernel::Return(ctx, 0, 0);
        return;
    }
    __imp__sub_000D1550(ctx, base);
}
