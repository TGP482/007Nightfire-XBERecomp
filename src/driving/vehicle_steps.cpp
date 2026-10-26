#include "driving/frame_rate.h"
#include "xdk/hle.h"

// Car steps that need more than a generic scale (config/driving.toml), each sized for one console tick.
namespace {

using kernel::G;

// per tick constants
constexpr uint32_t kSpinBrake = 0x18A4C0;  // 0.25
constexpr uint32_t kSpinCoast = 0x189EE0;  // 0.95
constexpr uint32_t kWobbleSlow = 0x18F538;
constexpr uint32_t kWobbleFast = 0x18F534;
constexpr uint32_t kBankShare = 0x18B914;  // 0.05

// vehicle (0x6A840, vt 0x18F580)
constexpr uint32_t kWheelSpin = 0x274;  // wheel turn per tick
constexpr uint32_t kHandling = 0x23C;
constexpr uint32_t kUprightTicks = 0xB8;  // in the handling record, ticks to stand back up

// the upright ease (0x62FE0): three rows of three, 0x10 apart
constexpr uint32_t kRows = 3;
constexpr uint32_t kRowStride = 0x10;

}  // namespace

// after a wheel's turn was added (0x68D6C, 0x68D80), vehicle in ebx
void DrivingWheelTurn(X86Context& ctx, uint8_t*) { driving::AddSteps(ctx, G<float>(ctx.ebx + kWheelSpin)); }

void DrivingWheelSpinBrake(X86Context& ctx, uint8_t*) { driving::ApplyFactor(ctx, G<float>(kSpinBrake)); }
void DrivingWheelSpinCoast(X86Context& ctx, uint8_t*) { driving::ApplyFactor(ctx, G<float>(kSpinCoast)); }

// the two phases of the body's size wobble (+0x3E4, +0x3E8)
void DrivingWobbleSlow(X86Context& ctx, uint8_t*) { driving::AddSteps(ctx, G<float>(kWobbleSlow)); }
void DrivingWobbleFast(X86Context& ctx, uint8_t*) { driving::AddSteps(ctx, G<float>(kWobbleFast)); }

// the flying vehicle's bank and pitch ease, before the share is applied
void DrivingPlaneBankEase(X86Context& ctx, uint8_t*) { driving::ScaleShare(ctx, G<float>(kBankShare)); }

// at 0x6323A: each value moved 1/N of the way from the old orientation (eax) to the target, into ebx; vehicle in edi
void DrivingCarUprightEase(X86Context& ctx, uint8_t*) {
    int32_t ticks = G<int32_t>(G<uint32_t>(ctx.edi + kHandling) + kUprightTicks);
    if (ticks <= 0) return;
    double share = 1.0 / ticks;
    double scale = driving::Ease(share) / share;
    for (uint32_t row = 0; row < kRows; row++) {
        for (uint32_t i = 0; i < 3; i++) {
            uint32_t offset = row * kRowStride + i * 4;
            float old = G<float>(ctx.eax + offset);
            float& value = G<float>(ctx.ebx + offset);
            value = float(old + (value - old) * scale);
        }
    }
}
