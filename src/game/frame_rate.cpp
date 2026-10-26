#include <algorithm>
#include <chrono>
#include <cmath>

#include "common/game_options.h"
#include "xdk/hle.h"

// The game steps a fixed 1/60 s each frame (1/50 on PAL): its main loop passes that rate to 0x6B040, which sets the
// step the whole game reads (the rate at 0x17C0F4, one over it at 0x17C104). It gets the rate the frames really come
// at instead, so the game keeps its speed at any frame rate. The second argument stays the console's rate (0x17C0F0):
// timers such as the mission end's count console frames from it.

namespace {

constexpr uint32_t kRate = 4;  // the first argument, from the stack pointer
constexpr double kMinRate = 30.0;  // a longer frame slows the game down, as on the console
// of each frame's own length, so its jitter does not reach the step; lengths, not rates, so the steps add up to real
// time when frames vary
constexpr double kSmoothing = 0.1;

}  // namespace

X86_EXTERN_FUNC(__imp__sub_0006B040);
X86_FUNC(sub_0006B040) {
    using clock = std::chrono::steady_clock;
    static clock::time_point last;
    static double frame = 0.0;  // seconds
    clock::time_point now = clock::now();
    if (frame == 0.0) {
        frame = 1.0 / double(kernel::R32(ctx.esp + kRate));
    } else {
        double seconds = std::chrono::duration<double>(now - last).count();
        frame += (std::min(seconds, 1.0 / kMinRate) - frame) * kSmoothing;
    }
    last = now;
    uint32_t whole = uint32_t(std::lround(1.0 / frame));
    kernel::W32(ctx.esp + kRate, whole);
    __imp__sub_0006B040(ctx, base);
    game::SaveChangedOptions(ctx);
}

// A character's animation change (0x33F20) blends the new layer in over the old one. A character holds at most 32
// layers (0x15C30) and each fades out over 8 frames of 60 fps, so past 240 fps a change on every frame fills them and
// the new layer is null; the game then writes through it (0x33FBB). The change runs without the blend instead.

// at 0x33F86, the blend; the new layer's slot is in ebp
bool SkipBlendWithoutLayer(X86Context& ctx, uint8_t*) {
    return kernel::G<uint32_t>(ctx.ebp) == 0;
}
