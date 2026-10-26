#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "xdk/hle.h"

// Debris (kind 0x10, made by 0xCCCF0 where a shot hits glass and other materials, its update 0xCCFF0) moves and falls
// on the step, but its life and fade count frames and its spin adds a 60 fps share each frame, so past 60 fps the
// shards spun faster and were gone sooner. They count 60 fps frames and spin by 60 over the rate.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint32_t kAlpha = 0xE2;          // byte, of the object in esi
constexpr uint32_t kRotation = 0x3C;       // of the object
// the debris data, in edi
constexpr uint32_t kSpin = 0x10;
constexpr uint32_t kLife = 0x1C;  // s16 frames
constexpr uint32_t kFade = 0x1E;  // alpha lost a frame
constexpr uint32_t kAxes = 3;

std::unordered_map<uint32_t, double> g_due;  // object -> 60 fps frames not counted yet

int TakeFrames(uint32_t debris) {
    double& due = g_due[debris];
    due += G<float>(kStepScale);
    double frames = std::floor(due);
    due -= frames;
    return int(frames);
}

}  // namespace

// at 0xCD010, in place of the fade and the life's count up to 0xCD03D; false goes to 0xCD093, where it ends
bool DebrisFade(X86Context& ctx, uint8_t*) {
    int frames = TakeFrames(ctx.esi);
    int alpha = G<uint8_t>(ctx.esi + kAlpha) - frames * G<uint8_t>(ctx.edi + kFade);
    if (alpha <= 0) {
        g_due.erase(ctx.esi);
        return false;
    }
    G<uint8_t>(ctx.esi + kAlpha) = uint8_t(alpha);
    int16_t& life = G<int16_t>(ctx.edi + kLife);
    life = int16_t(std::max(life - frames, 0));
    return true;
}

// at 0xCD077, in place of the spin's add up to 0xCD088
bool DebrisSpin(X86Context& ctx, uint8_t*) {
    float scale = G<float>(kStepScale);
    for (uint32_t i = 0; i < kAxes; i++) {
        float spin = G<float>(ctx.edi + kSpin + i * 4);
        G<float>(ctx.esi + kRotation + i * 4) += spin * scale;
    }
    return true;
}
