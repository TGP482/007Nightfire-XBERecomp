#include <bit>
#include <initializer_list>

#include "common/aspect.h"
#include "common/options.h"
#include "xdk/hle.h"

// A mission's film plays in its own loop (0x1308A0): the player (edi) gets its place (vt +0x4) and scale (vt +0x8)
// once, the frame's size over the film's, and on a widescreen output the console's own fill: 4/3 taller and lifted a
// sixth of the frame. They are set again each frame by the FMV scale setting: FILL as the console, as much taller as
// the output is wider than 4:3, 4:3 the film's width narrowed to a 4:3 share in the middle of the frame. The loop
// reuses the x scale's slot, so the scale is kept beforehand.
namespace {

using kernel::G;
using kernel::R32;

// the loop's stack at 0x130A18, floats
constexpr uint32_t kScaleX = 0x10;  // the frame's width over the film's
constexpr uint32_t kScaleY = 0x14;
constexpr uint32_t kHeight = 0x18;  // the frame's
constexpr uint32_t kWidth = 0x1C;
constexpr uint32_t kWide = 0xD4;  // byte: the output is widescreen, as 0x5A6B0 passes it
// the player, thiscall
constexpr uint32_t kPlace = 0x4;  // (x, y, z)
constexpr uint32_t kScale = 0x8;  // (x, y)

constexpr uint32_t kFmv43 = 1;  // game::Options::fmv_scale

struct FilmScale {
    float x = 1.0f;
    float y = 1.0f;
};

FilmScale g_scale;

uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }

void CallPlayer(X86Context& ctx, uint32_t player, uint32_t slot, std::initializer_list<uint32_t> args) {
    uint32_t ecx = ctx.ecx;
    ctx.ecx = player;
    kernel::CallGuest(ctx, R32(R32(player) + slot), args);
    ctx.ecx = ecx;
}

}  // namespace

// at 0x1309BC, once the film's scale is worked out
void DrivingFilmScaleLatch(X86Context& ctx, uint8_t*) {
    g_scale.x = G<float>(ctx.esp + kScaleX);
    g_scale.y = G<float>(ctx.esp + kScaleY);
}

// at 0x130A18, the top of the film's loop; the player in edi
void DrivingFilmScale(X86Context& ctx, uint8_t*) {
    if (!G<uint8_t>(ctx.esp + kWide)) return;
    uint32_t player = ctx.edi;
    float share = game::NarrowShare();
    if (game::Opt().fmv_scale == kFmv43) {
        float x = G<float>(ctx.esp + kWidth) * (1 - share) / 2;
        CallPlayer(ctx, player, kPlace, {Bits(x), 0, 0});
        CallPlayer(ctx, player, kScale, {Bits(g_scale.x * share), Bits(g_scale.y)});
        return;
    }
    float fill = 1 / share;
    float y = G<float>(ctx.esp + kHeight) * (1 - fill) / 2;
    CallPlayer(ctx, player, kPlace, {0, Bits(y), 0});
    CallPlayer(ctx, player, kScale, {Bits(g_scale.x), Bits(g_scale.y * fill)});
}
