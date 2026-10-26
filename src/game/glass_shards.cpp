#include "xdk/hle.h"

// A breakable (kind 0x20, its update 0x20460) of type 2, such as a window, breaks in 0x1FC90 into shards of its mesh
// (0x1F840, list at 0x1DC564). Their wait before falling and their life count frames, their spin and fall add a 60 fps
// share each frame (0x1FD60) and their alpha is life times the rate (0x1FE50), so past 60 fps the glass fell sooner and
// faster. The times are kept in 60 fps seconds and the spin and fall are scaled by 60 over the rate.
namespace {

using kernel::G;

constexpr uint32_t kRate = 0x17C0F8;       // float
constexpr uint32_t kFrameTime = 0x17C104;  // one over the rate
constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint32_t kGravity = 0x1F6648;
constexpr uint32_t kGravityScale = 0x15D348;
constexpr uint32_t kLifeFrames = 0x15D400;
constexpr float kConsoleRate = 60.0f;
constexpr float kConsoleFrame = 1.0f / kConsoleRate;
constexpr uint32_t kAxes = 3;

// the shard, in esi
constexpr uint32_t kLife = 0x10;
constexpr uint32_t kWait = 0x14;
constexpr uint32_t kPosition = 0x18;
constexpr uint32_t kRotation = 0x24;
constexpr uint32_t kVelocity = 0x30;
constexpr uint32_t kSpin = 0x3C;

void AddScaled(uint32_t to, uint32_t from, float scale) {
    for (uint32_t i = 0; i < kAxes; i++) G<float>(to + i * 4) += G<float>(from + i * 4) * scale;
}

}  // namespace

// at 0x1FC70, in place of the life's store up to 0x1FC7F; the wait was made in frames of the rate
bool GlassShardTimes(X86Context& ctx, uint8_t*) {
    G<float>(ctx.esi + kWait) *= G<float>(kRate) * kConsoleFrame;
    G<float>(ctx.esi + kLife) = G<float>(kLifeFrames) * kConsoleFrame;
    return true;
}

// at 0x1FDD9, in place of the spin, the fall and the life's count up to 0x1FE3F
bool GlassShardStep(X86Context& ctx, uint8_t*) {
    float scale = G<float>(kStepScale);
    float gravity = kConsoleFrame * kConsoleFrame * G<float>(kGravityScale) * scale;
    AddScaled(ctx.esi + kRotation, ctx.esi + kSpin, scale);
    AddScaled(ctx.esi + kPosition, ctx.esi + kVelocity, scale);
    AddScaled(ctx.esi + kVelocity, kGravity, gravity);
    G<float>(ctx.esi + kLife) -= G<float>(kFrameTime);
    return true;
}

// at 0x1FE7A, in place of loading the rate the alpha is scaled by
bool GlassShardAlpha(X86Context& ctx, uint8_t*) {
    x87_push(ctx, kConsoleRate);
    return true;
}
