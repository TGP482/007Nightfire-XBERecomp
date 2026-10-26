#include <cmath>
#include <unordered_map>

#include "xdk/hle.h"

// At a mission's end (0xAC690) the HUD's mission complete or failed panel (slot 12, 0xB1E50) slides its text by 60
// over the rate each frame, cut to whole pixels, and the mission goes on once it is in place or A is pressed. Past
// 60 fps the cut leaves it still, so the screen stayed until A. It slides whole pixels of a 60 fps clock instead.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint32_t kSlot = 8;              // the HUD slot, from the stack pointer
constexpr uint32_t kSlotShown = 0x1C;

std::unordered_map<uint32_t, double> g_due;  // slot -> 60 fps frames not slid yet

}  // namespace

// a mission end panel's frame, cdecl (player, slot, descriptor)
X86_EXTERN_FUNC(__imp__sub_000B1E50);
X86_FUNC(sub_000B1E50) {
    uint32_t slot = kernel::R32(ctx.esp + kSlot);
    if (!slot || !G<uint8_t>(slot + kSlotShown)) {
        g_due.erase(slot);
        return __imp__sub_000B1E50(ctx, base);
    }
    double& due = g_due[slot];
    float& scale = G<float>(kStepScale);
    float saved = scale;
    due += saved;
    scale = float(std::floor(due));
    due -= scale;
    __imp__sub_000B1E50(ctx, base);
    scale = saved;
}
