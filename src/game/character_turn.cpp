#include "xdk/hle.h"

// A character's steering (0x43070) turns it by a share of the gap each frame, 0.1 at 60 fps. The share (+0x3AC) was
// set once at its creation (0x419BE) from that frame's rate, so later rates turned it too slowly or too fast.
// Its avoidance of other characters (0x42CC0) looks ahead by each one's move last frame (+0x38C) and tests it
// against fixed distances, so at a high frame rate everyone seemed to stand still and was steered round too late.
namespace {

constexpr uint32_t kBrains = 0x1E5634;  // every character's brain, linked by +0x4
constexpr uint32_t kNextBrain = 0x4;
constexpr uint32_t kTurnShare = 0x3AC;
constexpr uint32_t kFrameMove = 0x38C;
constexpr uint32_t kTurnAt60 = 0x15D350;  // 0.1
constexpr uint32_t kRateOver60 = 0x17C0FC;

void ScaleFrameMoves(float scale) {
    for (uint32_t brain = kernel::R32(kBrains); brain; brain = kernel::R32(brain + kNextBrain)) {
        kernel::G<float>(brain + kFrameMove) *= scale;
    }
}

}  // namespace

// (character and brain, the gap): the share and the moves are those of a 60 fps frame
X86_EXTERN_FUNC(__imp__sub_00043070);
X86_FUNC(sub_00043070) {
    uint32_t brain = kernel::R32(kernel::R32(ctx.esp + 4) + 4);
    float frames = kernel::G<float>(kRateOver60);
    kernel::G<float>(brain + kTurnShare) = kernel::G<float>(kTurnAt60) / frames;
    ScaleFrameMoves(frames);
    __imp__sub_00043070(ctx, base);
    ScaleFrameMoves(1.0f / frames);
}
