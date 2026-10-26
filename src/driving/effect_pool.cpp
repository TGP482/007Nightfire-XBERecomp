#include <initializer_list>

#include "kernel/memory.h"
#include "xdk/hle.h"

// A sprite's effects (0xD4F90) come from a pool (0x201730) keyed by the sprite's slot. A finished sprite hands its
// effect back (0xD3D90: countdown +0x2C and slot +0x44 zeroed) and the pool's next tick (0xA2930) deletes it. The
// console ticks on every frame, so a new sprite whose slot reuses the address never finds it; at high fps it does,
// takes the handed back effect, and keeps it after the pool deletes it. Such an effect is deleted at once instead.
namespace {

using kernel::R32;

// the pool, thiscall
constexpr uint32_t kPoolMap = 0x04;
constexpr uint32_t kErase = 0xA2660;  // (iterator out, node)
// a map node
constexpr uint32_t kNodeEffect = 0x10;
// an effect
constexpr uint32_t kEffectSlot = 0x44;  // 0 once handed back
constexpr uint32_t kDelete = 0x0;       // vt, (flags)

}  // namespace

// at 0xA2C4F, the lookup found the slot's effect: eax its node, edi the pool
bool DrivingEffectHandedBack(X86Context& ctx, uint8_t*) {
    uint32_t node = ctx.eax;
    uint32_t effect = R32(node + kNodeEffect);
    if (R32(effect + kEffectSlot) != 0) return false;
    static const uint32_t iterator = mem::AllocSystem(4);
    ctx.ecx = effect;
    kernel::CallGuest(ctx, R32(R32(effect) + kDelete), {1});
    ctx.ecx = R32(ctx.edi + kPoolMap);
    kernel::CallGuest(ctx, kErase, {iterator, node});
    return true;
}
