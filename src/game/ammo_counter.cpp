#include <cmath>
#include <unordered_map>

#include "xdk/hle.h"

// The ammo panel (0xB1F90) shows the new weapon's name in place of the ammo for a byte count of frames after a
// switch: the rate cut to a byte, one a frame. Past 255 fps the byte wraps, so the name shows too briefly or not at
// all. It counts 60 fps frames instead, a second, on a clock that runs on the game's own step.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint32_t kNameFrames = 0x8EE;    // byte on the controller
constexpr uint32_t kController = 0x40;     // from the stack pointer at 0xB231A; in ecx at 0xB2341
constexpr uint8_t kNameTime = 60;          // a second of 60 fps frames

std::unordered_map<uint32_t, double> g_due;  // controller -> 60 fps frames not counted yet

}  // namespace

// at 0xB231A, in place of the count's start up to 0xB232F
bool AmmoNameStart(X86Context& ctx, uint8_t*) {
    uint32_t controller = kernel::R32(ctx.esp + kController);
    G<uint8_t>(controller + kNameFrames) = kNameTime;
    g_due[controller] = 0.0;
    return true;
}

// at 0xB2341, in place of the count's step up to 0xB2349
bool AmmoNameStep(X86Context& ctx, uint8_t*) {
    double& due = g_due[ctx.ecx];
    due += G<float>(kStepScale);
    double frames = std::floor(due);
    due -= frames;
    uint8_t& left = G<uint8_t>(ctx.ecx + kNameFrames);
    left = frames >= left ? 0 : uint8_t(left - frames);
    return true;
}
