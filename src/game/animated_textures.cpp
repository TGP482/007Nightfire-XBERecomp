#include <chrono>
#include <cstdint>

#include "game/animated_textures.h"
#include "xdk/hle.h"

// Animated textures (screens, water, fire) pick their picture from the game's frame count: the count over the frames
// a picture lasts (+0x28 of the texture, table 0x2ABE80), modulo its pictures (+0x24). The count goes up once a
// frame, so they played as fast as the frame rate. Around the draws that read it (0xDD692, 0xDD7E8, 0xDE457,
// 0xDFA18, 0xE00AD, 0xE012F, 0xE0696) the count is a clock of their own, 60 a second while the game's count moves.
namespace {

constexpr uint32_t kFrameCount = 0x1F65B4;  // 0x6ACA0 adds one a frame unless paused; a level start sets 1
constexpr double kConsoleRate = 60.0;

using Clock = std::chrono::steady_clock;

struct TextureTime {
    uint32_t game_count = 0;  // the game's count as the last frame left it
    double seconds = 0.0;     // played while the game's count moved
    Clock::time_point last;
    uint32_t saved = 0;  // the game's count while a draw reads the clock
    int depth = 0;
};
TextureTime g_time;

// game thread, once a frame
void UpdateTextureTime() {
    Clock::time_point now = Clock::now();
    uint32_t count = kernel::R32(kFrameCount);
    if (count < g_time.game_count) {
        g_time.seconds = 0.0;
    } else if (count != g_time.game_count) {
        g_time.seconds += std::chrono::duration<double>(now - g_time.last).count();
    }
    g_time.game_count = count;
    g_time.last = now;
}

}  // namespace

game::TextureClock::TextureClock() {
    if (g_time.depth++) return;
    g_time.saved = kernel::R32(kFrameCount);
    kernel::W32(kFrameCount, uint32_t(g_time.seconds * kConsoleRate));
}

game::TextureClock::~TextureClock() {
    if (--g_time.depth) return;
    kernel::W32(kFrameCount, g_time.saved);
}

// the frame
X86_EXTERN_FUNC(__imp__sub_0006ACA0);
X86_FUNC(sub_0006ACA0) {
    __imp__sub_0006ACA0(ctx, base);
    UpdateTextureTime();
}

// the draws that pick an animated texture's picture; 0xDE3D0 is in hud.cpp
X86_EXTERN_FUNC(__imp__sub_000DD4C0);
X86_FUNC(sub_000DD4C0) {
    game::TextureClock clock;
    __imp__sub_000DD4C0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_000DF9F0);
X86_FUNC(sub_000DF9F0) {
    game::TextureClock clock;
    __imp__sub_000DF9F0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_000E0020);
X86_FUNC(sub_000E0020) {
    game::TextureClock clock;
    __imp__sub_000E0020(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_000E04E0);
X86_FUNC(sub_000E04E0) {
    game::TextureClock clock;
    __imp__sub_000E04E0(ctx, base);
}
