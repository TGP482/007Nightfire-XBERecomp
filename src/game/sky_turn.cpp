#include "xdk/hle.h"

// Equinox (level 0x700001B) turns its sky and sun in 0xD9D60 by a set angle each frame the game runs, so past 60 fps
// the station spun faster. The angle grows by 60 over the rate instead.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;     // 60 over the rate
constexpr uint32_t kPaused = 0x1F65D0;        // u8
constexpr uint32_t kTurnPerFrame = 0x161D1C;  // 1/1440 radian

}  // namespace

// at 0xDA072, in place of the frame count times the turn up to 0xDA097
bool SkyTurn(X86Context& ctx, uint8_t*) {
    static double frames = 0.0;  // 60 fps frames
    if (!G<uint8_t>(kPaused)) frames += G<float>(kStepScale);
    x87_push(ctx, frames * G<float>(kTurnPerFrame));
    return true;
}
