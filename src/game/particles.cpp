#include "xdk/hle.h"

// A particle (kind 0x0E, made by 0xCD140, its update 0xCD560) moves, grows and ages on the step, but its colour and
// alpha step toward their end by a 60 fps share each frame (0xCD8BB), so past 60 fps it faded before its life ended,
// as the glass of a breaking window does. The share is scaled by 60 over the rate.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
// the particle data, in edi: red, green, blue and alpha steps, then the colour
constexpr uint32_t kColourStep = 0x0C;
constexpr uint32_t kColour = 0x1C;
constexpr uint32_t kChannels = 4;

}  // namespace

// at 0xCD8BB, in place of the colour's steps up to 0xCD8DF, which clamps the red left in st0
bool ParticleColourStep(X86Context& ctx, uint8_t*) {
    float scale = G<float>(kStepScale);
    for (uint32_t i = 0; i < kChannels; i++) {
        float step = G<float>(ctx.edi + kColourStep + i * 4);
        G<float>(ctx.edi + kColour + i * 4) += step * scale;
    }
    x87_push(ctx, G<float>(ctx.edi + kColour));
    return true;
}
