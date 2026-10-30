#include "xdk/hle.h"

// A character's steering (0x43070) turns it by a share of the gap each frame, 0.1 at 60 fps. The share (+0x3AC) was
// set once at its creation (0x419BE) from that frame's rate, so later rates turned it too slowly or too fast.
namespace {

constexpr uint32_t kTurnShare = 0x3AC;    // of the character's brain
constexpr uint32_t kTurnAt60 = 0x15D350;  // 0.1
constexpr uint32_t kRateOver60 = 0x17C0FC;

}  // namespace

// (character and brain, the gap): the share follows this frame's rate, as the game's other turn does at 0x43489
X86_EXTERN_FUNC(__imp__sub_00043070);
X86_FUNC(sub_00043070) {
    uint32_t brain = kernel::R32(kernel::R32(ctx.esp + 4) + 4);
    kernel::G<float>(brain + kTurnShare) = kernel::G<float>(kTurnAt60) / kernel::G<float>(kRateOver60);
    __imp__sub_00043070(ctx, base);
}
