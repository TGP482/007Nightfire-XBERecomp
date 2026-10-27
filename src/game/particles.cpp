#include "core/log.h"
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
constexpr uint32_t kType = 0x38;  // byte
// the particle object, in esi
constexpr uint32_t kParent = 0x1C;
constexpr uint32_t kRoom = 0x20;
constexpr uint32_t kKind = 0xDB;      // byte
constexpr uint32_t kCallArgs = 0x1C;  // 0xD7430's and 0xD7340's arguments, which the code pops next

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

// A particle stuck to a parent takes the parent's room each frame (0x2E2C1); a parent out of every room leaves it with
// none, and the falling types then read their floor through the null room ([room+0x50]). Without a room they fall
// with no floor.

// at 0xCD5BA, 0xCD632 and 0xCD68A, in place of the floor test; true goes to the no floor branch
bool ParticleWithoutRoom(X86Context& ctx, uint8_t*) {
    if (kernel::R32(ctx.esi + kRoom)) return false;
    static bool logged = false;
    if (!logged) {
        uint32_t parent = kernel::R32(ctx.esi + kParent);
        uint8_t kind = parent ? G<uint8_t>(parent + kKind) : 0;
        LOGR("particle: type %u without a room, parent %08X kind %02X", G<uint8_t>(ctx.edi + kType), parent, kind);
        logged = true;
    }
    ctx.esp += kCallArgs;
    return true;
}
