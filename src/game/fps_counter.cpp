#include <bit>
#include <cstdio>

#include "app/app.h"
#include "common/options.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

// The frame rate, top left, in the game's own text font: drawn as the HUD's text items are (0xD9410), just before
// the game presents the frame.
namespace {

using kernel::G;

constexpr uint32_t kFrameOpen = 0x2C5754;  // byte: a frame 0xE43A0 presents
// (x, y, unused, colour 0xRRGGBBAA, -1, text, style, scale pair or 0, flags, length, width): 640x480 units, y the
// baseline; the glyphs go into the records below
constexpr uint32_t kText = 0x6A340;
constexpr uint32_t kDropShadow = 0x1000;  // the HUD text's black copy a line down and right
constexpr uint32_t kRecords = 0x29DC00;
constexpr uint32_t kRecordCount = 0x29E810;
constexpr uint32_t kFlushRecords = 0xDE3D0;  // (records, count)
// the 2D layer's lighting before its items: white ambient, lighting mode 2, no lights
constexpr uint32_t kAmbient = 0xDDF00;
constexpr uint32_t kLightMode = 0xDD930;
constexpr uint32_t kLights = 0xDE610;

// 0xFF then a font from the table at 0x17BD90: 1 is the default text font, 13 lines high
constexpr char kStyle[] = "\xFF\x01";
constexpr float kLineHeight = 13;
constexpr float kMargin = 8;  // as the other recomps, on the frame 480 high
constexpr float kNarrow = 0.75f;  // a 4:3 glyph on the frame shown 16:9
constexpr uint32_t kWhite = 0xFFFFFFFF;
constexpr uint32_t kUnused = 0xFFFFFFFF;  // as 0xD9410 passes it

// guest memory 0x6A340 reads: the style, the scale pair, the text
constexpr uint32_t kScaleAt = 4;
constexpr uint32_t kTextAt = 12;
constexpr uint32_t kScratchSize = 64;

uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }

void DrawFps(X86Context& ctx) {
    static const uint32_t scratch = mem::AllocSystem(kScratchSize);
    char line[kScratchSize - kTextAt];
    int length = std::snprintf(line, sizeof(line), "FPS: %.1f", double(game::g_fps.load()));
    for (uint32_t i = 0; i < sizeof(kStyle); i++) G<char>(scratch + i) = kStyle[i];
    for (int i = 0; i <= length; i++) G<char>(scratch + kTextAt + uint32_t(i)) = line[i];
    float wide = app::Widescreen() ? kNarrow : 1.0f;
    G<float>(scratch + kScaleAt) = wide;
    G<float>(scratch + kScaleAt + 4) = 1.0f;
    kernel::CallGuest(ctx, kAmbient, {0xFF, 0xFF, 0xFF, 0xFF});
    kernel::CallGuest(ctx, kLightMode, {2});
    kernel::CallGuest(ctx, kLights, {0, 0, 0, 0, 0});
    kernel::CallGuest(ctx, kText,
                      {Bits(kMargin * wide), Bits(kMargin + kLineHeight), 0, kWhite, kUnused, scratch + kTextAt, scratch,
                       scratch + kScaleAt, kDropShadow, uint32_t(length), Bits(0)});
    kernel::CallGuest(ctx, kFlushRecords, {kRecords, kernel::R32(kRecordCount)});
    kernel::W32(kRecordCount, 0);
}

}  // namespace

// the game's end of a frame
X86_EXTERN_FUNC(__imp__sub_000E43A0);
X86_FUNC(sub_000E43A0) {
    if (game::Opt().fps_counter && G<uint8_t>(kFrameOpen)) DrawFps(ctx);
    __imp__sub_000E43A0(ctx, base);
}
