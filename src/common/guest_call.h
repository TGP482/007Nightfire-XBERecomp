#pragma once
#include <bit>
#include <cstdint>
#include <initializer_list>

#include "xdk/hle.h"

namespace game {

// a float as the guest stack word it is passed in
inline uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }

// a thiscall: self in ecx for the call, ecx kept
inline uint32_t CallThis(X86Context& ctx, uint32_t fn, uint32_t self, std::initializer_list<uint32_t> args = {}) {
    uint32_t ecx = ctx.ecx;
    ctx.ecx = self;
    uint32_t result = kernel::CallGuest(ctx, fn, args);
    ctx.ecx = ecx;
    return result;
}

}  // namespace game
