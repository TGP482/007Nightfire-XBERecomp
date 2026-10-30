#include <algorithm>

#include "driving/frame_rate.h"
#include "xdk/hle.h"

// The HUD's red hit screen (0xD8820) fades with a count (+0x328, 20 at most, 0xD87A0) taken down one each draw, so
// it went by in a few frames at high fps. It now counts down the console ticks the frame covers.
namespace {

using kernel::G;

constexpr uint32_t kHitCount = 0x328;  // int, of the HUD

}  // namespace

// the hit screen's draw, thiscall: the HUD
X86_EXTERN_FUNC(__imp__sub_000D8820);
X86_FUNC(sub_000D8820) {
    int32_t& count = G<int32_t>(ctx.ecx + kHitCount);
    int32_t before = count;
    __imp__sub_000D8820(ctx, base);
    if (count == before - 1) count = std::max(before - int32_t(driving::FrameTicksCrossed()), 0);
}
