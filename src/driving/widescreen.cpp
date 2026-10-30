#include "common/aspect.h"
#include "common/game_options.h"
#include "common/guest_call.h"
#include "d3d/wide_box.h"
#include "xdk/hle.h"

// The display (0x7CFB0) is widescreen when the launch data's AV Options row or the console's video flags were set;
// the output decides instead, and the 3D view (0x96960) takes the output's own shape, so a 4:3 output plays the
// console's 4:3. The HUD and the pause menu are laid out for a 16:9 screen, so they keep that shape in the middle.
// On a wider screen the full screen overlays drawn before it reach the edges, and the health ring and the ammo
// panel sit at the screen's sides as they do on a 16:9 one.
namespace {

using kernel::G;
using kernel::R32;

constexpr uint32_t kDisplay = 0x1EBFF4;
constexpr uint32_t kDisplayMode = 0x48;  // 2 widescreen, else 0
constexpr uint32_t kDisplayWide = 0x4C;  // byte
constexpr uint32_t kNarrowMode = 0;
constexpr uint32_t kWideMode = 2;
constexpr uint32_t kWideViewAspect = 0x19245C;  // 16/9, read only on a widescreen output
// text draw (0x1073A0), cdecl (font, x, y, text); the font's x scale
constexpr uint32_t kDrawFont = 4;
constexpr uint32_t kDrawX = 8;
constexpr uint32_t kFontScaleX = 0x38;
// 0xD6520 squeezes the menu's text to the middle 4:3 of a 16:9 frame as these
constexpr float kNarrow = 0.75f;
constexpr float kMiddle = 320;

bool g_subtitle = false;  // the subtitle box's text draws

// HUD: its text renderer, which draws what it holds at a flush (0xD6DA0)
constexpr uint32_t kRenderer = 0x18;
constexpr uint32_t kFlushText = 0xD6DA0;
constexpr float kLeftSide = 0.0f;
constexpr float kRightSide = 1.0f;

using GuestFn = void (*)(X86Context&, uint8_t*);

// the game draws these past the 640 wide frame's sides, so out of the box they cover the screen
void DrawUnboxed(X86Context& ctx, uint8_t* base, GuestFn draw) {
    bool box = game::Wide();
    if (box) d3d::BoxToWide(false);
    draw(ctx, base);
    if (box) d3d::BoxToWide(true);
}

// a HUD panel, thiscall on the HUD, with the box at a side of the screen; its text flushed there too
void DrawAtSide(X86Context& ctx, uint8_t* base, GuestFn draw, float side) {
    if (!game::Wide()) return draw(ctx, base);
    uint32_t renderer = R32(ctx.ecx + kRenderer);
    game::CallThis(ctx, kFlushText, renderer);
    d3d::BoxToWide(true, side);
    draw(ctx, base);
    game::CallThis(ctx, kFlushText, renderer);
    d3d::BoxToWide(true);
}

}  // namespace

void game::FollowOutputAspect() {
    uint32_t display = R32(kDisplay);
    if (!display) return;
    bool wide = Wide();
    G<uint8_t>(display + kDisplayWide) = wide;
    uint32_t mode = R32(display + kDisplayMode);
    if (mode == kNarrowMode || mode == kWideMode) kernel::W32(display + kDisplayMode, wide ? kWideMode : kNarrowMode);
    G<float>(kWideViewAspect) = OutputAspect();
}

// the HUD's frame, thiscall: its pieces, its 3D icons and the pause menu (0xE1970)
X86_EXTERN_FUNC(__imp__sub_000E1CE0);
X86_FUNC(sub_000E1CE0) {
    bool box = game::Wide();
    if (box) d3d::BoxToWide(true);
    __imp__sub_000E1CE0(ctx, base);
    if (box) d3d::BoxToWide(false);
}

// the fade to black, the mission end's shutters and the cutscene bars
X86_EXTERN_FUNC(__imp__sub_000D8970);
X86_FUNC(sub_000D8970) { DrawUnboxed(ctx, base, __imp__sub_000D8970); }

X86_EXTERN_FUNC(__imp__sub_000DC590);
X86_FUNC(sub_000DC590) { DrawUnboxed(ctx, base, __imp__sub_000DC590); }

X86_EXTERN_FUNC(__imp__sub_000D9E30);
X86_FUNC(sub_000D9E30) { DrawUnboxed(ctx, base, __imp__sub_000D9E30); }

// the health ring, bottom left
X86_EXTERN_FUNC(__imp__sub_000D81D0);
X86_FUNC(sub_000D81D0) { DrawAtSide(ctx, base, __imp__sub_000D81D0, kLeftSide); }

// the weapon's ammo and name, bottom right
X86_EXTERN_FUNC(__imp__sub_000DC990);
X86_FUNC(sub_000DC990) { DrawAtSide(ctx, base, __imp__sub_000DC990, kRightSide); }

// the subtitle box's draw (0xE3670) skips that squeeze, so its text would stretch in the HUD's 16:9 frame
X86_EXTERN_FUNC(__imp__sub_000E3670);
X86_FUNC(sub_000E3670) {
    g_subtitle = true;
    __imp__sub_000E3670(ctx, base);
    g_subtitle = false;
}

X86_EXTERN_FUNC(__imp__sub_001073A0);
X86_FUNC(sub_001073A0) {
    if (g_subtitle && game::Wide()) {
        float& x = G<float>(ctx.esp + kDrawX);
        G<float>(R32(ctx.esp + kDrawFont) + kFontScaleX) *= kNarrow;
        x = kMiddle + (x - kMiddle) * kNarrow;
    }
    __imp__sub_001073A0(ctx, base);
}
