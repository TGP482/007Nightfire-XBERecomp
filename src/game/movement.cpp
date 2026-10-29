#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "xdk/hle.h"

// Bond's walk (0xA9CF0) eases last frame's move toward the stick's by 0.2 times 60 over the rate, then scales it by
// the state's speed. The scaled move starts the next frame, so a speed under 1 settles lower at a smaller step. The
// speed is changed so the walk settles where it does at 60 fps.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr float kWalkEase = 0.2f;          // of the way to the stick's move, at 60 fps

// level 0x7000007 walks at 0.9 unless 0x1DF180 is set
constexpr uint32_t kLevel = 0x1F658C;
constexpr uint32_t kSlowLevel = 0x7000007;
constexpr uint32_t kSlowLevelOff = 0x1DF180;
constexpr float kSlowLevelSpeed = 0.9f;

constexpr uint32_t kWalkSpeed = 0x8C;  // the speed argument (1, crouched 0.75) from the stack pointer at 0xAA024

// share of the stick's move a walk at this speed and ease settles at
float SettledShare(float speed, float ease) { return speed * ease / (1.0f - speed + speed * ease); }

float SpeedForShare(float share, float ease) { return share / (ease + share - share * ease); }

}  // namespace

// at 0xAA024, in place of the level's slow walk up to 0xAA048, which loads the speed
bool WalkSpeed(X86Context& ctx, uint8_t*) {
    float& speed = G<float>(ctx.esp + kWalkSpeed);
    if (kernel::R32(kLevel) == kSlowLevel && !G<uint8_t>(kSlowLevelOff)) speed *= kSlowLevelSpeed;
    float ease = G<float>(kStepScale) * kWalkEase;
    speed = SpeedForShare(SettledShare(speed, kWalkEase), ease);
    return true;
}

// The grapple's pull (0xAA340) speeds up 0.05 a frame to 1.5 (0.5 in multiplayer) and moves that far each frame,
// and Bond lets go if he is on the ground once 15 frames have passed. Both run as at 60 fps. It ends on the first frame
// he is within 2 of the point, up to 1.5 further on at 60 fps, which carries him up into vents; it ends on a 60 fps
// frame too.
namespace {

// the grapple, Bond's state +0xF4: its point, then
constexpr uint32_t kPullStep = 0xC;   // the pull's move this frame
constexpr uint32_t kPullTime = 0x1A;  // u16 frames left before the ground ends the pull
constexpr float kPullGain = 0.05f;
constexpr float kPullTop = 1.5f;
constexpr float kPullTopMultiplayer = 0.5f;
constexpr float kPullFrames = 15.0f;
constexpr uint32_t kMultiplayer = 0x260018;
constexpr uint32_t kBondPosition = 0x24;
constexpr uint32_t kPullDirection = 0xC;  // the direction vector 0xAA4EB points eax at, from the stack pointer

std::unordered_map<uint32_t, double> g_pull_frames;  // grapple -> 60 fps frames pulled

float Distance(uint32_t a, uint32_t b) {
    float x = G<float>(a) - G<float>(b);
    float y = G<float>(a + 4) - G<float>(b + 4);
    float z = G<float>(a + 8) - G<float>(b + 8);
    return std::sqrt(x * x + y * y + z * z);
}

}  // namespace

// at 0xAA4A9, in place of the pull's speed up to 0xAA4F2; grapple in esi, Bond in edi
bool GrapplePull(X86Context& ctx, uint8_t*) {
    float frames = G<float>(kStepScale);
    float top = kernel::R32(kMultiplayer) ? kPullTopMultiplayer : kPullTop;
    float& step = G<float>(ctx.esi + kPullStep);
    float speed = std::min(step / frames + kPullGain * frames, top);  // per 60 fps frame
    step = std::min(speed * frames, Distance(ctx.edi + kBondPosition, ctx.esi));
    g_pull_frames[ctx.esi] += frames;
    ctx.eax = ctx.esp + kPullDirection;
    return true;
}

// at 0xAA4A1, Bond is within 2 of the point; true pulls on until a 60 fps frame ends (grapple in esi)
bool GrappleArrival(X86Context& ctx, uint8_t*) {
    double before = g_pull_frames[ctx.esi];
    return std::floor(before + G<float>(kStepScale)) == std::floor(before);
}

// at 0xAA334, once a grapple's start set its time; grapple in edi
void GrappleStartTime(X86Context& ctx, uint8_t*) {
    G<uint16_t>(ctx.edi + kPullTime) = uint16_t(std::lround(kPullFrames / G<float>(kStepScale)));
    g_pull_frames[ctx.edi] = 0.0;
}
