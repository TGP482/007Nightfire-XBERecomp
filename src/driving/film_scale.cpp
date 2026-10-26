#include <bit>
#include <initializer_list>

#include "common/options.h"
#include "xdk/hle.h"

// A mission's film plays in its own loop (0x1308A0): the player (edi) gets its place (vt +0x4) and scale (vt +0x8)
// once, the frame's size over the film's, and on a widescreen output the console's own fill: 4/3 taller and lifted a
// sixth of the frame. They are set again each frame by the FMV scale setting: FILL as the console, 4:3 the film's
// width 3/4 narrower in the middle of the frame.
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
constexpr uint32_t kFillScale = 0x1A3190;  // 4/3
constexpr uint32_t kFillLift = 0x1A3194;   // -1/6

constexpr uint32_t kFmv43 = 1;     // game::Options::fmv_scale
constexpr float kNarrow = 0.75f;  // 4:3 of a 16:9 frame
constexpr float kCentre = (1 - kNarrow) / 2;

uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }

void CallPlayer(X86Context& ctx, uint32_t player, uint32_t slot, std::initializer_list<uint32_t> args) {
    uint32_t ecx = ctx.ecx;
    ctx.ecx = player;
    kernel::CallGuest(ctx, R32(R32(player) + slot), args);
    ctx.ecx = ecx;
}

}  // namespace

// at 0x130A18, the top of the film's loop; the player in edi
void DrivingFilmScale(X86Context& ctx, uint8_t*) {
    if (!G<uint8_t>(ctx.esp + kWide)) return;
    uint32_t player = ctx.edi;
    float scale_x = G<float>(ctx.esp + kScaleX);
    float scale_y = G<float>(ctx.esp + kScaleY);
    if (game::Opt().fmv_scale == kFmv43) {
        float x = G<float>(ctx.esp + kWidth) * kCentre;
        CallPlayer(ctx, player, kPlace, {Bits(x), 0, 0});
        CallPlayer(ctx, player, kScale, {Bits(scale_x * kNarrow), Bits(scale_y)});
        return;
    }
    float y = G<float>(ctx.esp + kHeight) * G<float>(kFillLift);
    CallPlayer(ctx, player, kPlace, {0, Bits(y), 0});
    CallPlayer(ctx, player, kScale, {Bits(scale_x), Bits(scale_y * G<float>(kFillScale))});
}
