#include <bit>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "common/options.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

// The frame rate, top left, in the pause menu's text font: Driving.xbe's HUD draw (0xE1970) runs every frame and
// flushes the menu's text renderer as it goes; the counter is drawn and flushed after its last flush.
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

constexpr uint32_t kTextScale = 0x19FF00;  // float 1
constexpr uint32_t kLineColour = 0x23F4D8;
constexpr uint32_t kDisplay = 0x1EBFF4;    // +0x4C set on a widescreen output
constexpr uint32_t kDisplayWide = 0x4C;
constexpr uint32_t kWideScale = 0x19FEA8;  // the widths' scale then

constexpr float kMargin = 8;  // as default.xbe's counter, on the frame 640x480
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

float MarginX() {
    uint32_t display = R32(kDisplay);
    bool wide = display && G<uint8_t>(display + kDisplayWide);
    return wide ? kMargin * G<float>(kWideScale) : kMargin;
}

}  // namespace

// at 0xE1A75, after the HUD draw's last flush; the pause menu in esi
void DrivingFpsCounter(X86Context& ctx, uint8_t*) {
    uint32_t renderer = R32(ctx.esi + kRenderer);
    if (!game::Opt().fps_counter || !renderer) return;
    CallRenderer(ctx, kSetFont, renderer, {R32(ctx.esi + kFontLine)});
    CallRenderer(ctx, kSetScale, renderer, {R32(kTextScale), R32(kTextScale)});
    kernel::W32(renderer + kRendererColour, R32(kLineColour));
    CallRenderer(ctx, kDrawText, renderer, {FpsText(), Bits(MarginX()), Bits(kMargin)});
    CallRenderer(ctx, kFlush, renderer, {});
}
