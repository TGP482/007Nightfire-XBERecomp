#include <unordered_map>

#include "driving/frame_rate.h"
#include "xdk/hle.h"

// Body (0xB0510) and world (0xB1420) collisions run each update and pass the impact to the owner's damage (vt +8)
// and its hits from the player (vt +0xD0). The console took one hit a tick, so an owner held in a collision, such as
// a path car scripted through a wreck, blew up at high fps. It now takes collision damage from one update a tick.
namespace {

using kernel::G;

constexpr uint32_t kObjects = 0x2342A0;  // a body's owner, by its index
// body
constexpr uint32_t kIndex = 0x6F;  // signed byte

std::unordered_map<uint32_t, uint32_t> g_hit_update;  // owner, update it last took collision damage

uint32_t Owner(uint32_t body) { return G<uint32_t>(kObjects + int8_t(G<uint8_t>(body + kIndex)) * 4); }

// already hit by an earlier update of this console tick
bool HitThisTick(uint32_t body) {
    uint32_t now = driving::UpdateCount();
    auto [it, added] = g_hit_update.try_emplace(Owner(body), now);
    if (added || it->second == now) return false;
    if (it->second >= driving::TickUpdate()) return true;
    it->second = now;
    return false;
}

}  // namespace

// in place of a damage call, for the body in a register
bool DrivingHitAgainEbx(X86Context& ctx, uint8_t*) { return HitThisTick(ctx.ebx); }
bool DrivingHitAgainEsi(X86Context& ctx, uint8_t*) { return HitThisTick(ctx.esi); }
