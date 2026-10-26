#include "driving/frame_rate.h"
#include "xdk/hle.h"

// The player camera (vt 0x1918AC) updates in a command each tick (0x877E0), only when the tick count moved. It runs
// once per update instead, and its eases and steps per tick scale to the update (config/driving.toml).
namespace {

using kernel::G;

// player camera
constexpr uint32_t kLastUpdate = 0x148;  // the tick count it last ran on, an update count now
constexpr uint32_t kClockState = 0x234E24;
constexpr uint32_t kPaused = 3;

// per tick shares and steps
constexpr uint32_t kShakeShare = 0x1C3E68;   // 0.1, times the camera type's own
constexpr uint32_t kShakeLocal = 0x10;       // that type's value, on the stack at 0x8755F
constexpr uint32_t kChaseGainShare = 0x1C3E50;  // 0.02
constexpr uint32_t kBumperShare = 0x1C3E54;  // 0.4
constexpr uint32_t kAimInputShare = 0x1EC34C;
constexpr uint32_t kAimSettleShare = 0x18B8EC;  // 0.8
constexpr uint32_t kAimKick = 0x1918D0;      // 1/72

// an animated camera (0x82400): its ticks left, set from the animation's length in ticks
constexpr uint32_t kAnimTicksLeft = 0x13C;

void ScaleRegisterShare(X86Context& ctx, uint32_t record, uint32_t offset) {
    driving::ScaleShare(ctx, G<float>(record + offset));
}

}  // namespace

// at 0x877FE, camera in esi: true while it already ran this update; eax is what 0x87814 stores as the last one
bool DrivingCameraStale(X86Context& ctx, uint8_t*) {
    ctx.eax = driving::UpdateCount();
    bool paused = G<uint32_t>(kClockState) == kPaused;
    return !paused && G<uint32_t>(ctx.esi + kLastUpdate) == ctx.eax;
}

void DrivingShakeEase(X86Context& ctx, uint8_t*) {
    driving::ScaleShare(ctx, G<float>(kShakeShare) * G<float>(ctx.esp + kShakeLocal));
}

void DrivingChaseGainEase(X86Context& ctx, uint8_t*) { driving::ScaleShare(ctx, G<float>(kChaseGainShare)); }
void DrivingBumperEase(X86Context& ctx, uint8_t*) { driving::ScaleShare(ctx, G<float>(kBumperShare)); }
void DrivingAimSettleEase(X86Context& ctx, uint8_t*) { driving::ScaleShare(ctx, G<float>(kAimSettleShare)); }
void DrivingAimKick(X86Context& ctx, uint8_t*) { driving::AddSteps(ctx, G<float>(kAimKick)); }

// at 0x85E45 the share is in st1 too, for the second axis
void DrivingAimInputEase(X86Context& ctx, uint8_t*) {
    float share = G<float>(kAimInputShare);
    driving::ScaleShare(ctx, share);
    X87_ST(1) = driving::Ease(share);
}

// shares in the camera type's record
void DrivingEdi20Ease(X86Context& ctx, uint8_t*) { ScaleRegisterShare(ctx, ctx.edi, 0x20); }
void DrivingEbx50Ease(X86Context& ctx, uint8_t*) { ScaleRegisterShare(ctx, ctx.ebx, 0x50); }
void DrivingEsi84Ease(X86Context& ctx, uint8_t*) { ScaleRegisterShare(ctx, ctx.esi, 0x84); }
void DrivingEsi90Ease(X86Context& ctx, uint8_t*) { ScaleRegisterShare(ctx, ctx.esi, 0x90); }
void DrivingEsi9CEase(X86Context& ctx, uint8_t*) { ScaleRegisterShare(ctx, ctx.esi, 0x9C); }

// at 0x826B4: the animated camera's ticks left only drop on a tick; eax holds them for the test at 0x826D8
bool DrivingAnimTicksLeft(X86Context& ctx, uint8_t*) {
    if (driving::UpdateTicked()) return false;
    ctx.eax = G<uint32_t>(ctx.esi + kAnimTicksLeft);
    return true;
}
