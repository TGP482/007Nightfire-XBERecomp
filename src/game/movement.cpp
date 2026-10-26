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
