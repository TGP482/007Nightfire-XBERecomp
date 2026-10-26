#include <algorithm>

#include "driving/frame_rate.h"
#include "xdk/hle.h"

// Path players (0x7FBC0, list 0x1EC300) move AI path cars and AIPathAnim cameras along their paths by the ticks since
// their last move. Given the whole tick count, they only moved on updates that crossed a tick. They get the game clock
// with its fraction now, and what they did once per move scales to the ticks it covers (config/driving.toml).
namespace {

using kernel::G;

// path player
constexpr uint32_t kSpeed = 0xBC;
constexpr uint32_t kRamp = 0xC0;  // speed change per tick
constexpr uint32_t kTargetSpeed = 0xC4;
constexpr uint32_t kMoving = 0xCC;

constexpr uint32_t kElapsed = 0xC;  // a local of 0x7FBC0: ticks since the player last moved

float Elapsed(X86Context& ctx) { return G<float>(ctx.esp + kElapsed); }

}  // namespace

// at 0x3E665: st0 holds the clock's whole ticks, about to be handed to the players
void DrivingPathClock(X86Context& ctx, uint8_t*) { X87_ST(0) += driving::TickCarry(); }

// at 0x7FC32, in place of the ramp up to 0x7FD02: the speed eases to its target, a player at rest stops moving
bool DrivingPathRamp(X86Context& ctx, uint8_t*) {
    float& speed = G<float>(ctx.esi + kSpeed);
    float target = G<float>(ctx.esi + kTargetSpeed);
    float step = G<float>(ctx.esi + kRamp) * Elapsed(ctx);
    if (speed < target) speed = std::min(speed + step, target);
    else if (speed > target) speed = std::max(speed - step, target);
    else if (speed == 0.0f) G<uint8_t>(ctx.esi + kMoving) = 0;
    return true;
}

// before each axis of the velocity (+0x88) is stored: the move times the rate was sized for one tick
void DrivingPathRate(X86Context& ctx, uint8_t*) { X87_ST(0) /= Elapsed(ctx); }
