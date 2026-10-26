#pragma once
#include <array>
#include <cstdint>

// State::simple pieces the title wrote since the render thread was last sent them, a bit per 16 states (64 bytes), so
// the 8 KB of them is not compared every draw
namespace gpu {

constexpr uint32_t kSimplePiece = 16;
inline std::array<uint64_t, 2> g_simple_dirty = {~0ull, ~0ull};

inline void MarkSimple(uint32_t index) {
    uint32_t piece = index / kSimplePiece;
    g_simple_dirty[piece / 64] |= 1ull << (piece % 64);
}

inline void MarkAllSimple() { g_simple_dirty = {~0ull, ~0ull}; }

}  // namespace gpu
