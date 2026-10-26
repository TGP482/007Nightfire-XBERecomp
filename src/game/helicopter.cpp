#include <cmath>
#include <unordered_map>

#include "xdk/hle.h"

// A helicopter (kind 0x3B, 0x6BFB0) flies its path in 0x6BC20: each frame its place on the path goes up by the path's
// step (+0xC, set by 0x6B930 for a 60 fps frame), so past 60 fps it flew faster. The step is scaled by 60 over the rate.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint32_t kPathStep = 0xC;        // the path, in edi
constexpr uint32_t kPathPlace = 0x5C;      // the helicopter's data, in esi
constexpr uint32_t kDetached = 0x74;       // the bool argument, from the stack pointer at 0x6BC68
constexpr uint32_t kCallArgs = 0x1C;       // 0xD8DF0's arguments, which the code pops next

}  // namespace

// at 0x6BC68, in place of the place's step and the argument's test up to the jump at 0x6BC7A
bool HelicopterPathStep(X86Context& ctx, uint8_t*) {
    float& place = G<float>(ctx.esi + kPathPlace);
    place += G<float>(ctx.edi + kPathStep) * G<float>(kStepScale);
    bool detached = G<uint8_t>(ctx.esp + kDetached) != 0;
    ctx.esp += kCallArgs;
    return detached;
}

// Its gun (0x6BCF0) fires once a count of frames reaches 5, its muzzle flash (+0x48's +0xD0) shows for 2 frames and its
// start delay (+0x76) counts frames, so past 60 fps it fired faster and started sooner. They count 60 fps frames
// instead, on a clock of each helicopter's own that runs on the game's step.
namespace {

constexpr uint32_t kStartDelay = 0x76;  // u16 frames, the helicopter's data in esi
constexpr uint32_t kCount = 0xD0;       // u16 frames of the gun (esi) or the flash (ebx)
constexpr uint16_t kGunFires = 5;

uint32_t g_ticks = 0;  // 60 fps frames in the helicopter's frame that runs now

uint16_t CountDown(uint16_t count) { return count > g_ticks ? uint16_t(count - g_ticks) : 0; }

}  // namespace

// a helicopter's frame, cdecl (helicopter)
X86_EXTERN_FUNC(__imp__sub_0006BFB0);
X86_FUNC(sub_0006BFB0) {
    static std::unordered_map<uint32_t, double> due;
    double& left = due[kernel::R32(ctx.esp + 4)];
    left += G<float>(kStepScale);
    double ticks = std::floor(left);
    left -= ticks;
    g_ticks = uint32_t(ticks);
    __imp__sub_0006BFB0(ctx, base);
}

// at 0x6C003, in place of the start delay's step up to its test at 0x6C00B; true while it still counts
bool HelicopterStartDelay(X86Context& ctx, uint8_t*) {
    uint16_t& delay = G<uint16_t>(ctx.esi + kStartDelay);
    delay = CountDown(delay);
    ctx.eax = delay;
    return delay != 0;
}

// at 0x6C035, in place of the flash's step up to 0x6C04B
bool HelicopterFlash(X86Context& ctx, uint8_t*) {
    uint16_t& flash = G<uint16_t>(ctx.ebx + kCount);
    flash = CountDown(flash);
    ctx.eax = flash;
    return true;
}

// at 0x6BD77, in place of the gun's count up to its test at 0x6BD81; true when it does not fire
bool HelicopterGunCount(X86Context& ctx, uint8_t*) {
    uint16_t& count = G<uint16_t>(ctx.esi + kCount);
    bool fires = g_ticks && count >= kGunFires;
    ctx.ecx = ctx.eax + 1;
    count = uint16_t(count + g_ticks);
    return !fires;
}
