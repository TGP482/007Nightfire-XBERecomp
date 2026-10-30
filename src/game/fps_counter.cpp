#include <cstdio>

#include "app/app.h"
#include "common/aspect.h"
#include "common/guest_call.h"
#include "common/options.h"
#include "core/log.h"
#include "game/main_menu.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

// The frame rate, top left, in the menus' font and the main menu watermark in the game's text font: drawn as the HUD's
// text items are (0xD9410), just before the game presents the frame.
namespace {

using game::Bits;
using kernel::G;

constexpr uint32_t kFrameOpen = 0x2C5754;  // byte: a frame 0xE43A0 presents
// byte: a level is loading, 0xBFA60 to 0xDFF90; each file read (0xDC990) presents spinner frames (0xDC8A0), and
// the counter's text there read through a null pointer
constexpr uint32_t kLoading = 0x2ADF50;
// (x, y, colour, shadow colour, -1, text, style, scale pair or 0, flags, length, width): 640x480 units, y the
// baseline, colours 0xRRGGBBAA with 0x80 full; the glyphs go into the records below. It returns the text's size.
constexpr uint32_t kText = 0x6A340;
constexpr uint32_t kMeasureOnly = 1;  // no glyphs, only the size
constexpr uint32_t kDropShadow = 0x1000;  // the HUD text's shadow colour copy a line down and right
constexpr uint32_t kRecords = 0x29DC00;
constexpr uint32_t kRecordCount = 0x29E810;
constexpr uint32_t kFlushRecords = 0xDE3D0;  // (records, count)
// the 2D layer's lighting before its items: white ambient, lighting mode 2, no lights
constexpr uint32_t kAmbient = 0xDDF00;
constexpr uint32_t kLightMode = 0xDD930;
constexpr uint32_t kLights = 0xDE610;

// 0xFF then a font from the table at 0x17BD90: 1 is the default text font, 13 lines high; 2 the menus', the style
// a menu widget is made with (0x712E5)
constexpr char kTextStyle[] = "\xFF\x01";
constexpr char kMenuStyle[] = "\xFF\x02";
static_assert(sizeof(kTextStyle) == sizeof(kMenuStyle));
constexpr float kLineHeight = 13;
constexpr float kFrameWidth = 640;
constexpr float kMargin = 8;  // as the other recomps, on the frame 480 high
constexpr float kFpsSize = 12;  // SCDA's counter: 12 high, its baseline 8 + 12 down
constexpr uint32_t kWhite = 0xFFFFFFFF;
constexpr uint32_t kBlack = 0x000000FF;
constexpr uint32_t kGrey = 0x5D5D5DFF;  // the other recomps' watermark grey, 186 of 255
constexpr uint32_t kUnused = 0xFFFFFFFF;  // as 0xD9410 passes it

// the watermark ends this far in from the frame's right edge, as on the other recomps
constexpr char kWatermark[] = "XBE Recomp V1";
constexpr float kWatermarkRight = 80;
constexpr float kLegendGap = 8;  // the least room between the legend's text and the watermark
constexpr float kLowerLine = 16;  // where the legend leaves no room, the watermark goes a line below it
constexpr uint32_t kStyleAlign = 0x6AA10;  // (style): 1 left, 2 right, 3 centered
constexpr uint32_t kAlignRight = 2;
constexpr uint32_t kAlignCentered = 3;

// guest memory 0x6A340 reads: the style, the scale pair, the text
constexpr uint32_t kScaleAt = 4;
constexpr uint32_t kTextAt = 12;
constexpr uint32_t kScratchSize = 64;

float Wide() { return game::NarrowShare(); }  // a 4:3 glyph on the frame

struct Line {
    float x;
    float y;
    uint32_t colour;
    uint32_t shadow;
    uint32_t flags;
};

struct Size {
    float width;
    float height;
};

void SetScale(uint32_t scratch, float scale) {
    G<float>(scratch + kScaleAt) = scale * Wide();
    G<float>(scratch + kScaleAt + 4) = scale;
}

// the text into guest memory with the style and scale 1; its length
uint32_t Stage(uint32_t scratch, const char* style, const char* text) {
    for (uint32_t i = 0; i < sizeof(kMenuStyle); i++) G<char>(scratch + i) = style[i];
    SetScale(scratch, 1.0f);
    uint32_t length = 0;
    while (text[length] && kTextAt + length + 1 < kScratchSize) {
        G<char>(scratch + kTextAt + length) = text[length];
        length++;
    }
    G<char>(scratch + kTextAt + length) = 0;
    return length;
}

// the size when flags is kMeasureOnly
Size Text(X86Context& ctx, uint32_t scratch, uint32_t length, const Line& line) {
    uint32_t text = scratch + kTextAt;
    uint32_t scale = scratch + kScaleAt;
    uint32_t size = kernel::CallGuest(ctx, kText,
                                      {Bits(line.x), Bits(line.y), line.colour, line.shadow, kUnused, text, scratch,
                                       scale, line.flags, length, Bits(0)});
    if (!size) return {};
    return {G<float>(size), G<float>(size + 4)};
}

void BeginText(X86Context& ctx) {
    kernel::CallGuest(ctx, kAmbient, {0xFF, 0xFF, 0xFF, 0xFF});
    kernel::CallGuest(ctx, kLightMode, {2});
    kernel::CallGuest(ctx, kLights, {0, 0, 0, 0, 0});
}

void EndText(X86Context& ctx) {
    kernel::CallGuest(ctx, kFlushRecords, {kRecords, kernel::R32(kRecordCount)});
    kernel::W32(kRecordCount, 0);
}

void DrawFps(X86Context& ctx, uint32_t scratch) {
    char line[kScratchSize - kTextAt];
    std::snprintf(line, sizeof(line), "FPS: %.1f", double(game::g_fps.load()));
    uint32_t length = Stage(scratch, kMenuStyle, line);
    float height = Text(ctx, scratch, length, {0, 0, kWhite, kBlack, kMeasureOnly}).height;
    if (height > 0) SetScale(scratch, kFpsSize / height);
    Text(ctx, scratch, length, {kMargin * Wide(), kMargin + kFpsSize, kWhite, kBlack, kDropShadow});
}

// where the legend's text ends on the frame, 0 when it has no label to measure; uses the scratch's scale pair
float LegendEnd(X86Context& ctx, uint32_t scratch, const game::LegendRow& legend) {
    if (!legend.text || !legend.style) return 0;
    uint32_t length = 0;
    while (G<char>(legend.text + length)) length++;
    uint32_t scale = scratch + kScaleAt;
    G<float>(scale) = 1.0f;
    G<float>(scale + 4) = 1.0f;
    uint32_t size = kernel::CallGuest(ctx, kText,
                                      {Bits(0), Bits(0), kGrey, kGrey, kUnused, legend.text, legend.style, scale,
                                       kMeasureOnly, length, Bits(0)});
    if (!size) return 0;
    float width = G<float>(size);
    uint32_t align = kernel::CallGuest(ctx, kStyleAlign, {legend.style});
    float end = legend.box_left + width;
    if (align == kAlignRight) end = legend.box_left + legend.box_width;
    if (align == kAlignCentered) end = legend.box_left + (legend.box_width + width) / 2;
    return kFrameWidth * (1 - Wide()) / 2 + end * Wide();
}

// right aligned on the legend's row, grey without a shadow; it moves with the page
void DrawWatermark(X86Context& ctx, uint32_t scratch, const game::LegendRow& legend) {
    float legend_end = LegendEnd(ctx, scratch, legend);
    uint32_t length = Stage(scratch, kTextStyle, kWatermark);
    float right = kFrameWidth - kWatermarkRight * Wide() + legend.left * Wide();
    float baseline = legend.top + (legend.height + kLineHeight) / 2;
    float width = Text(ctx, scratch, length, {0, 0, kGrey, kGrey, kMeasureOnly}).width;
    if (right - width < legend_end + kLegendGap) baseline += kLowerLine;
    static bool logged = false;
    if (!logged) {
        LOGR("watermark: x %.1f baseline %.1f width %.1f legend end %.1f", double(right - width), double(baseline),
             double(width), double(legend_end));
    }
    logged = true;
    Text(ctx, scratch, length, {right - width, baseline, kGrey, kGrey, 0});
}

}  // namespace

// the game's end of a frame
X86_EXTERN_FUNC(__imp__sub_000E43A0);
X86_FUNC(sub_000E43A0) {
    static const uint32_t scratch = mem::AllocSystem(kScratchSize);
    // also called with no frame open, by the loading loop: the legend waits for the frame that presents
    if (!G<uint8_t>(kFrameOpen)) {
        __imp__sub_000E43A0(ctx, base);
        return;
    }
    game::LegendRow legend = game::TakeMainMenuLegend();
    bool fps = game::Opt().fps_counter && !G<uint8_t>(kLoading);
    if (fps || legend.shown) {
        BeginText(ctx);
        if (fps) DrawFps(ctx, scratch);
        if (legend.shown) DrawWatermark(ctx, scratch, legend);
        EndText(ctx);
    }
    __imp__sub_000E43A0(ctx, base);
}
