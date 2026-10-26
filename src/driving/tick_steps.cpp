#include <cmath>

#include "driving/frame_rate.h"
#include "xdk/hle.h"

// What the game does once per update was sized for a console tick. These hooks (config/driving.toml) scale a value
// on the x87 stack or on the guest stack to the ticks the update covers, or skip the work on an update that crossed
// no tick. Ease takes a per tick share, Decay applies a per tick factor.
namespace {

using kernel::G;
using kernel::R32;

double& Top(X86Context& ctx) { return X87_ST(0); }
float& Slot(X86Context& ctx, uint32_t offset) { return G<float>(ctx.esp + offset); }
void AmountSlot(X86Context& ctx, uint32_t offset) { Slot(ctx, offset) *= float(driving::UpdateTicks()); }
void EaseSlot(X86Context& ctx, uint32_t offset) { Slot(ctx, offset) = float(driving::Ease(Slot(ctx, offset))); }
void DecaySlot(X86Context& ctx, uint32_t offset) { Slot(ctx, offset) = float(driving::Decay(Slot(ctx, offset))); }

}  // namespace

// skips
bool DrivingTickless(X86Context&, uint8_t*) { return !driving::UpdateTicked(); }
bool DrivingFrameTickless(X86Context&, uint8_t*) { return driving::FrameTicksCrossed() == 0; }

// st0
void DrivingStepAmount(X86Context& ctx, uint8_t*) { Top(ctx) *= driving::UpdateTicks(); }
void DrivingStepRate(X86Context& ctx, uint8_t*) { Top(ctx) /= driving::UpdateTicks(); }
void DrivingStepFactor(X86Context& ctx, uint8_t*) { Top(ctx) = driving::Decay(Top(ctx)); }
// before a countdown takes 1.0 off st0
void DrivingCountdownStep(X86Context& ctx, uint8_t*) { Top(ctx) += 1.0 - driving::UpdateTicks(); }

// guest stack slots, before a call or at a function's entry
void DrivingEsp4Amount(X86Context& ctx, uint8_t*) { AmountSlot(ctx, 0x4); }
void DrivingEsp8Amount(X86Context& ctx, uint8_t*) { AmountSlot(ctx, 0x8); }
void DrivingEspCAmount(X86Context& ctx, uint8_t*) { AmountSlot(ctx, 0xC); }
void DrivingEsp4Factor(X86Context& ctx, uint8_t*) { DecaySlot(ctx, 0x4); }
void DrivingEsp4Ease(X86Context& ctx, uint8_t*) { EaseSlot(ctx, 0x4); }
void DrivingEspCEase(X86Context& ctx, uint8_t*) { EaseSlot(ctx, 0xC); }

// a lerp's share in a local, before the vector lerp reads it
void DrivingEsp14Ease(X86Context& ctx, uint8_t*) { EaseSlot(ctx, 0x14); }
void DrivingEsp18Ease(X86Context& ctx, uint8_t*) { EaseSlot(ctx, 0x18); }
void DrivingEsp24Ease(X86Context& ctx, uint8_t*) { EaseSlot(ctx, 0x24); }
void DrivingEsp48Ease(X86Context& ctx, uint8_t*) { EaseSlot(ctx, 0x48); }
void DrivingEsp5CEase(X86Context& ctx, uint8_t*) { EaseSlot(ctx, 0x5C); }
void DrivingEsp94Ease(X86Context& ctx, uint8_t*) { EaseSlot(ctx, 0x94); }

// the vector the first argument points at, before a call
void DrivingEsp0VectorAmount(X86Context& ctx, uint8_t*) {
    uint32_t vector = R32(ctx.esp);
    for (uint32_t i = 0; i < 3; i++) G<float>(vector + i * 4) *= float(driving::UpdateTicks());
}
