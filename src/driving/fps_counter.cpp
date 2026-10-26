#include <bit>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "common/options.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

// The frame rate, top left, in the pause menu's text font: Driving.xbe's HUD draw (0xE1970) runs every frame and
// flushes the menu's text renderer as it goes; the counter is drawn and flushed after its last flush. The text draw
// (0xD6520) puts text in the title safe area, or squeezes it to the middle 4:3 on a widescreen output; the counter
// is placed so it lands where default.xbe's and SCDA's do, at the frame's corner.
namespace {

using kernel::G;
using kernel::R32;

// the pause menu (esi at 0xE1A75): its text renderer and the line whose font the counter takes
constexpr uint32_t kRenderer = 0x18;
constexpr uint32_t kFontLine = 0x110;  // CONTINUE
// renderer, thiscall
constexpr uint32_t kSetFont = 0xD6EB0;   // (text object)
constexpr uint32_t kSetScale = 0xD61E0;  // (x scale, y scale)
constexpr uint32_t kDrawText = 0xD6520;  // (text, x, y)
constexpr uint32_t kFlush = 0xD6DA0;
constexpr uint32_t kRendererColour = 0x50;

constexpr uint32_t kConvertColour = 0x75C40;  // cdecl (0xAARRGGBB): the renderer's colour, as 0x15A5C0 makes its own
constexpr uint32_t kWhite = 0xFFFFFFFF;
constexpr uint32_t kBlack = 0xFF000000;
// the renderer's font: its loaded font file, ascent and descent bytes as 0x1073F0 steps a line
constexpr uint32_t kRendererFont = 0xA8;
constexpr uint32_t kFontAscent = 0x12;
constexpr uint32_t kFontDescent = 0x13;

constexpr uint32_t kDisplay = 0x1EBFF4;
constexpr uint32_t kDisplayLayout = 0x48;  // 2 squeezes x to the middle
constexpr uint32_t kDisplayWide = 0x4C;    // byte: a widescreen output
constexpr uint32_t kSqueezed = 2;

// 0xD6520 on the 640x480 frame: safe area x * 0.9 + 32, y * 0.9 + 34, glyphs 0.9; widescreen glyphs 0.75 wide
constexpr float kSafeScale = 0.9f;
constexpr float kSafeLeft = 32;
constexpr float kSafeTop = 34;
constexpr float kNarrow = 0.75f;
constexpr float kMiddle = 320;

// SCDA's counter: 12 high, 8 in, its baseline 8 + 12 down, a black copy 1 down and right
constexpr float kSize = 12;
constexpr float kMargin = 8;
constexpr float kShadow = 1;
constexpr uint32_t kScratchSize = 32;

uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }

void CallRenderer(X86Context& ctx, uint32_t fn, uint32_t renderer, std::initializer_list<uint32_t> args) {
    uint32_t ecx = ctx.ecx;
    ctx.ecx = renderer;
    kernel::CallGuest(ctx, fn, args);
    ctx.ecx = ecx;
}

uint32_t FpsText() {
    static const uint32_t scratch = mem::AllocSystem(kScratchSize);
    char line[kScratchSize];
    std::snprintf(line, sizeof(line), "FPS: %.1f", double(game::g_fps.load()));
    std::memcpy(kernel::HostPtr(scratch), line, std::strlen(line) + 1);
    return scratch;
}

struct Frame {
    bool wide;
    bool squeezed;
};

Frame FrameOf() {
    uint32_t display = R32(kDisplay);
    bool wide = display && G<uint8_t>(display + kDisplayWide);
    return {wide, wide && R32(display + kDisplayLayout) == kSqueezed};
}

struct Point {
    float x;
    float y;
};

// the point to ask 0xD6520 for so it draws at x, y on the 640x480 frame
Point Request(const Frame& frame, float x, float y) {
    if (!frame.wide) return {(x - kSafeLeft) / kSafeScale, (y - kSafeTop) / kSafeScale};
    if (frame.squeezed) x = kMiddle + (x - kMiddle) / kNarrow;
    return {x, y};
}

// the scale again each time: 0xD6520 leaves it scaled for the frame
void DrawAt(X86Context& ctx, uint32_t renderer, float scale, uint32_t colour, Point at) {
    CallRenderer(ctx, kSetScale, renderer, {Bits(scale), Bits(scale)});
    kernel::W32(renderer + kRendererColour, colour);
    CallRenderer(ctx, kDrawText, renderer, {FpsText(), Bits(at.x), Bits(at.y)});
}

uint32_t Colour(X86Context& ctx, uint32_t argb) { return kernel::CallGuest(ctx, kConvertColour, {argb}); }

}  // namespace

// at 0xE1A75, after the HUD draw's last flush; the pause menu in esi
void DrivingFpsCounter(X86Context& ctx, uint8_t*) {
    uint32_t renderer = R32(ctx.esi + kRenderer);
    if (!game::Opt().fps_counter || !renderer) return;
    CallRenderer(ctx, kSetFont, renderer, {R32(ctx.esi + kFontLine)});
    uint32_t font = R32(renderer + kRendererFont);
    if (!font) return;
    float ascent = G<uint8_t>(font + kFontAscent);
    float line = ascent + G<uint8_t>(font + kFontDescent);
    float scale = line > 0 ? kSize / line : 1.0f;
    float top = kMargin + kSize - ascent * scale;  // the glyphs hang from y, the baseline ascent below
    Frame frame = FrameOf();
    float left = frame.wide ? kMargin * kNarrow : kMargin;
    float asked = frame.wide ? scale : scale / kSafeScale;
    DrawAt(ctx, renderer, asked, Colour(ctx, kBlack), Request(frame, left + kShadow, top + kShadow));
    DrawAt(ctx, renderer, asked, Colour(ctx, kWhite), Request(frame, left, top));
    CallRenderer(ctx, kFlush, renderer, {});
}
