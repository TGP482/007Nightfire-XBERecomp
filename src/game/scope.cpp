#include "xdk/hle.h"

// The sniper scope is a HUD panel: a 480x480 circle between two black bars on the 640x480 view. On a view 240 high
// (two players, top and bottom) its init 0xB5E80 halves the items' height only, so the circle came out twice as wide
// as tall. The circle's width is halved around its middle too, and the bars widen to meet it.
namespace {

using kernel::G;
using kernel::R32;

constexpr uint32_t kScope = 0x17FF4C;  // the panel's descriptor
constexpr uint32_t kViews = 0x1F661C;
constexpr uint32_t kPlayerView = 0x8DE;  // byte
// view: its size, floats
constexpr uint32_t kViewWidth = 0xF8;
constexpr uint32_t kViewHeight = 0xFC;
constexpr float kFullWidth = 640;
constexpr float kFullHeight = 480;
// HUD slot: its items, in the descriptor's order (circle, left bar, right bar)
constexpr uint32_t kSlotItems = 0x08;
constexpr uint32_t kCircle = 0;
constexpr uint32_t kLeftBar = 1;
constexpr uint32_t kRightBar = 2;
// 2D item: frame pixels, shorts
constexpr uint32_t kItemX = 0x24;
constexpr uint32_t kItemW = 0x2C;

bool HalvedHeightOnly(uint32_t player) {
    uint32_t view = R32(kViews + G<int8_t>(player + kPlayerView) * 4);
    return view && G<float>(view + kViewHeight) < kFullHeight && G<float>(view + kViewWidth) >= kFullWidth;
}

}  // namespace

// a HUD panel's init (player, slot, descriptor, from), cdecl
X86_EXTERN_FUNC(__imp__sub_000B5E80);
X86_FUNC(sub_000B5E80) {
    uint32_t player = R32(ctx.esp + 4);
    uint32_t slot = R32(ctx.esp + 8);
    uint32_t descriptor = R32(ctx.esp + 12);
    __imp__sub_000B5E80(ctx, base);
    if (descriptor != kScope || !HalvedHeightOnly(player)) return;
    uint32_t items = R32(slot + kSlotItems);
    uint32_t circle = R32(items + kCircle * 4);
    uint32_t left = R32(items + kLeftBar * 4);
    uint32_t right = R32(items + kRightBar * 4);
    if (!circle || !left || !right) return;
    int16_t shift = int16_t(G<int16_t>(circle + kItemW) / 4);
    G<int16_t>(circle + kItemX) += shift;
    G<int16_t>(circle + kItemW) -= 2 * shift;
    G<int16_t>(left + kItemW) += shift;
    G<int16_t>(right + kItemX) -= shift;
    G<int16_t>(right + kItemW) += shift;
}
