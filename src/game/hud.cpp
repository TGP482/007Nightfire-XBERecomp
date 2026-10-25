#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <mutex>
#include <unordered_map>

#include "app/app.h"
#include "common/options.h"
#include "game/animated_textures.h"
#include "xdk/hle.h"

// The 2D is laid out on the 640x480 frame, which a widescreen output shows 16:9. While a player's view, the HUD's or
// the menus' draws its 2D layer, each sprite keeps its 4:3 shape at its panel's side of the view:
// x' = left + (x - left) * 3/4 + anchor * width / 4. A side of a sprite on a side of the view stays there and its other
// side goes where a centered sprite's would, so a full-screen overlay built from pieces still covers the view.

namespace {

constexpr uint32_t kViews = 0x1F661C;  // one per player, then the other cameras
constexpr uint32_t kViewCount = 11;
constexpr uint32_t kPlayers = 4;
constexpr uint32_t kHudView = 5;   // the view id the HUD panels' items carry
constexpr uint32_t kMenuView = 7;  // the menus' items, built by 0x72D10
// view: its rectangle on the frame, floats
constexpr uint32_t kViewLeft = 0xDC;
constexpr uint32_t kViewRight = 0xE0;

// 2D layer, cdecl (view, items, first, end)
constexpr uint32_t kLayerArgs = 4;
constexpr uint32_t kArgView = 0;
constexpr uint32_t kArgItems = 1;
constexpr uint32_t kArgFirst = 2;
constexpr uint32_t kArgEnd = 3;

// a player's HUD, built by 0xB6AA0 (player, from) unless from's +0xDB is 2 or 0x11
constexpr uint32_t kPlayerHud = 0x770;
constexpr uint32_t kFromKind = 0xDB;
// HUD: panel slots from +8; slot +0 descriptor, +8 its items, then the items the panel adds
constexpr uint32_t kHudSlots = 0x08;
constexpr uint32_t kSlotCount = 22;
constexpr uint32_t kSlotSize = 0x20;
constexpr uint32_t kSlotItems = 0x08;
constexpr uint32_t kSlotItemCount = 0x18;    // ushort
constexpr uint32_t kDescriptorExtra = 0x16;  // ushort

// sprite list, cdecl (records, count): what the 2D layer queued
constexpr uint32_t kRecordSize = 0x30;
constexpr uint32_t kRecordX = 0x10;
constexpr uint32_t kRecordW = 0x18;

// sprite queue, cdecl (x, y, w, h, u, v, uw, vh, colour): frame pixels
constexpr uint32_t kSpriteX = 4;
constexpr uint32_t kSpriteY = 8;
constexpr uint32_t kSpriteW = 0xC;
constexpr uint32_t kSpriteH = 0x10;
constexpr uint32_t kSpriteU = 0x14;
constexpr uint32_t kSpriteUw = 0x1C;

constexpr float kNarrow = 0.75f;  // 4:3 of a 16:9 frame
constexpr float kFrameMiddleX = 320;
constexpr float kFrameMiddleY = 240;
constexpr uint32_t kFmvFill = 0;
constexpr uint32_t kFmv43 = 1;
// a menu page's background film (page +0xC4) loops (0x93115); the menus stay 4:3, so it does too
constexpr uint32_t kFilmLoops = 8;  // 0xDCCD0's second argument, from the stack pointer
constexpr uint32_t kReplayFilm = 0x4E504C59;  // 0xDCCD0's "NPLY": the film playing again
// byte: 0xE8CF0 decodes no picture on the call after one that decoded, the console's next 60 Hz frame
constexpr uint32_t kFilmSkipDecode = 0x2FF761;
constexpr std::chrono::duration<double> kConsoleFrame{1.0 / 60};
constexpr uint32_t kSprite = 0xE6790;
constexpr float kFrameWidth = 640;
constexpr float kFrameHeight = 480;
constexpr uint32_t kBlack = 0xFF000000;  // the film's 0xFF808080 is full colour
constexpr float kSlack = 0.5f;
constexpr float kLeft = 0;
constexpr float kCenter = 0.5f;
constexpr float kRight = 1;
constexpr float kAuto = -1;  // the third of the view the item draws in

// HUD panel descriptors placed at a side; the other panels stay in the middle. The multiplayer panels hold their
// pieces anywhere on the screen and place them every frame
struct Panel {
    uint32_t descriptor;
    float anchor;
};
constexpr Panel kPanels[] = {{0x17FDE8, kLeft},   // health ring
                             {0x17F930, kRight},  // ammo
                             {0x180FBC, kRight},  // ammo, multiplayer
                             {0x181138, kAuto},   // multiplayer health bar
                             {0x181180, kAuto},
                             {0x1812D4, kAuto},
                             {0x1806D8, kAuto}};  // multiplayer radar

uint32_t g_view = 0;  // while its 2D layer draws
bool g_film = false;  // the film player's frame draws
bool g_side_bars = false;  // DrawSideBars draws
bool g_menu_film = false;  // the film playing is a menu page's background
float g_anchor = kCenter;
std::mutex g_lock;
std::unordered_map<uint32_t, float> g_anchors;  // 2D item -> its panel's anchor

float AnchorOf(uint32_t item) {
    std::lock_guard<std::mutex> lock(g_lock);
    auto it = g_anchors.find(item);
    return it == g_anchors.end() ? kCenter : it->second;
}

float AnchorAt(uint32_t records, int32_t count) {
    if (count <= 0) return kAuto;
    float lo = kernel::G<float>(records + kRecordX);
    float hi = lo;
    for (int32_t i = 0; i < count; i++) {
        float x = kernel::G<float>(records + i * kRecordSize + kRecordX);
        lo = std::min(lo, x);
        hi = std::max(hi, x + kernel::G<float>(records + i * kRecordSize + kRecordW));
    }
    float left = kernel::G<float>(g_view + kViewLeft);
    float right = kernel::G<float>(g_view + kViewRight);
    float third = (right - left) / 3;
    float middle = (lo + hi) / 2;
    if (middle < left + third) return kLeft;
    return middle > right - third ? kRight : kCenter;
}

float Placed(float x, float anchor, float left, float right) {
    return left + (x - left) * kNarrow + anchor * (right - left) * (1 - kNarrow);
}

// a side on a side of the view stays there, and the other goes where a centered sprite's would
void Place(float& x, float& w, float anchor, float left, float right) {
    bool at_left = x <= left + kSlack;
    bool at_right = x + w >= right - kSlack;
    if (at_left || at_right) anchor = kCenter;
    float x1 = at_left ? x : Placed(x, anchor, left, right);
    float x2 = at_right ? x + w : Placed(x + w, anchor, left, right);
    x = x1;
    w = x2 - x1;
}

// the sides beside a picture narrowed to 4:3, where older frames would show: its texture times black, as sprites so
// they keep their place among its draws
void DrawSideBars(X86Context& ctx, float width) {
    g_side_bars = true;
    for (float left : {0.0f, kFrameWidth - width}) {
        std::array<uint32_t, 9> args = {std::bit_cast<uint32_t>(left), 0, std::bit_cast<uint32_t>(width),
                                        std::bit_cast<uint32_t>(kFrameHeight), 0, 0, 0, 0, kBlack};
        kernel::CallGuest(ctx, kSprite, args.data(), args.size());
    }
    g_side_bars = false;
}

void NarrowToMiddle(X86Context& ctx) {
    float& x = kernel::G<float>(ctx.esp + kSpriteX);
    float& w = kernel::G<float>(ctx.esp + kSpriteW);
    x = kFrameMiddleX + (x - kFrameMiddleX) * kNarrow;
    w *= kNarrow;
    DrawSideBars(ctx, (kFrameWidth - w) / 2);
}

// The film is one 640x480 sprite, which the 16:9 frame would stretch: FILL widens its height with it, so the film
// keeps its shape and fills the screen (the letterbox bars go past the edges); 4:3 narrows it into the middle.
void PlaceFilm(X86Context& ctx) {
    if (!app::Widescreen()) return;
    float& y = kernel::G<float>(ctx.esp + kSpriteY);
    float& h = kernel::G<float>(ctx.esp + kSpriteH);
    uint32_t scale = g_menu_film ? kFmv43 : game::Opt().fmv_scale;
    if (scale == kFmvFill) {
        y = kFrameMiddleY + (y - kFrameMiddleY) / kNarrow;
        h /= kNarrow;
        return;
    }
    NarrowToMiddle(ctx);
}

// a textured sprite over the whole frame in the menus' view (the title screen's picture)
bool MenuBackground(X86Context& ctx) {
    if (!g_view || g_view != kernel::R32(kViews + kMenuView * 4)) return false;
    float x = kernel::G<float>(ctx.esp + kSpriteX);
    float y = kernel::G<float>(ctx.esp + kSpriteY);
    float w = kernel::G<float>(ctx.esp + kSpriteW);
    float h = kernel::G<float>(ctx.esp + kSpriteH);
    bool covers = x <= kSlack && y <= kSlack && w >= kFrameWidth - kSlack && h >= kFrameHeight - kSlack;
    return covers && kernel::G<float>(ctx.esp + kSpriteUw) != 0.0f;
}

}  // namespace

// a film starts, cdecl (film, loops, volume)
X86_EXTERN_FUNC(__imp__sub_000DCCD0);
X86_FUNC(sub_000DCCD0) {
    if (kernel::R32(ctx.esp + 4) != kReplayFilm) g_menu_film = kernel::G<uint8_t>(ctx.esp + kFilmLoops) != 0;
    __imp__sub_000DCCD0(ctx, base);
}

// the film player's frame
X86_EXTERN_FUNC(__imp__sub_000E8CF0);
X86_FUNC(sub_000E8CF0) {
    using Clock = std::chrono::steady_clock;
    static Clock::time_point decoded;
    // the skip lasts that frame's time only, so a capped frame rate still shows every picture
    uint8_t& skip = kernel::G<uint8_t>(kFilmSkipDecode);
    if (skip && Clock::now() - decoded >= kConsoleFrame) skip = 0;
    g_film = true;
    __imp__sub_000E8CF0(ctx, base);
    g_film = false;
    if (skip) decoded = Clock::now();
}

X86_EXTERN_FUNC(__imp__sub_000B6AA0);
X86_FUNC(sub_000B6AA0) {
    uint32_t player = kernel::R32(ctx.esp + 4);
    uint32_t from = kernel::R32(ctx.esp + 8);
    __imp__sub_000B6AA0(ctx, base);
    uint8_t kind = from ? kernel::G<uint8_t>(from + kFromKind) : 0;
    uint32_t hud = kind == 0x02 || kind == 0x11 ? 0 : kernel::R32(player + kPlayerHud);
    if (!hud) return;
    std::lock_guard<std::mutex> lock(g_lock);
    for (uint32_t s = 0; s < kSlotCount; s++) {
        uint32_t slot = hud + kHudSlots + s * kSlotSize;
        uint32_t descriptor = kernel::R32(slot);
        for (const Panel& panel : kPanels) {
            if (panel.descriptor != descriptor) continue;
            uint32_t items = kernel::R32(slot + kSlotItems);
            uint32_t count = kernel::G<uint16_t>(slot + kSlotItemCount);
            count += kernel::G<uint16_t>(descriptor + kDescriptorExtra);
            for (uint32_t i = 0; i < count; i++)
                if (uint32_t item = kernel::R32(items + i * 4)) g_anchors[item] = panel.anchor;
        }
    }
}

// a new 2D item, eax
X86_EXTERN_FUNC(__imp__sub_000CEC90);
X86_FUNC(sub_000CEC90) {
    __imp__sub_000CEC90(ctx, base);
    std::lock_guard<std::mutex> lock(g_lock);
    g_anchors.erase(ctx.eax);
}

X86_EXTERN_FUNC(__imp__sub_000D9410);
X86_FUNC(sub_000D9410) {
    uint32_t args[kLayerArgs];
    for (uint32_t i = 0; i < kLayerArgs; i++) args[i] = kernel::R32(ctx.esp + 4 + i * 4);
    uint32_t index = 0;
    while (index < kViewCount && kernel::R32(kViews + index * 4) != args[kArgView]) index++;
    if (index == kHudView && game::g_hud_hidden) {
        kernel::Return(ctx, 0, 0);
        return;
    }
    bool narrowed = index < kPlayers || index == kHudView || index == kMenuView;
    uint32_t first = args[kArgFirst];
    uint32_t end = args[kArgEnd];
    if (!narrowed || !app::Widescreen() || first >= end) return __imp__sub_000D9410(ctx, base);
    // runs of items with one anchor: each call flushes its sprites before it returns
    uint32_t ret = kernel::R32(ctx.esp);
    g_view = args[kArgView];
    for (uint32_t i = first; i < end;) {
        float anchor = AnchorOf(kernel::R32(args[kArgItems] + i * 4));
        uint32_t next = i + 1;
        while (anchor != kAuto && next < end && AnchorOf(kernel::R32(args[kArgItems] + next * 4)) == anchor) next++;
        if (i != first) {
            ctx.esp -= 4;  // the previous call's ret took the return address
            kernel::W32(ctx.esp, ret);
        }
        args[kArgFirst] = i;
        args[kArgEnd] = next;
        for (uint32_t a = 0; a < kLayerArgs; a++) kernel::W32(ctx.esp + 4 + a * 4, args[a]);
        g_anchor = anchor;
        __imp__sub_000D9410(ctx, base);
        i = next;
    }
    g_view = 0;
}

// an auto item takes its side from the first sprites it flushes; the sprites' animated textures read their own clock
X86_EXTERN_FUNC(__imp__sub_000DE3D0);
X86_FUNC(sub_000DE3D0) {
    game::TextureClock clock;
    if (g_view && g_anchor == kAuto) g_anchor = AnchorAt(kernel::R32(ctx.esp + 4), kernel::G<int16_t>(ctx.esp + 8));
    __imp__sub_000DE3D0(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_000E6790);
X86_FUNC(sub_000E6790) {
    if (g_side_bars) return __imp__sub_000E6790(ctx, base);
    if (g_film) PlaceFilm(ctx);
    // the menus stay 4:3, so their full screen picture does too
    if (!g_film && app::Widescreen() && MenuBackground(ctx)) {
        NarrowToMiddle(ctx);
        return __imp__sub_000E6790(ctx, base);
    }
    if (!g_view) return __imp__sub_000E6790(ctx, base);
    float left = kernel::G<float>(g_view + kViewLeft);
    float right = kernel::G<float>(g_view + kViewRight);
    float middle = (left + right) / 2;
    float& x = kernel::G<float>(ctx.esp + kSpriteX);
    float& w = kernel::G<float>(ctx.esp + kSpriteW);
    float& u = kernel::G<float>(ctx.esp + kSpriteU);
    float& uw = kernel::G<float>(ctx.esp + kSpriteUw);
    bool at_left = x <= left + kSlack;
    bool at_right = x + w >= right - kSlack;
    // a picture from one side past the middle (a menu band whose ring frames another picture): split there, so the
    // far part narrows with what is drawn over it and the rest still reaches the side
    if (at_left != at_right && x < middle && x + w > middle) {
        uint32_t ret = kernel::R32(ctx.esp);
        float end = x + w;
        float u_end = u + uw;
        float u_middle = u + uw * (middle - x) / w;
        w = middle - x;
        uw = u_middle - u;
        Place(x, w, kCenter, left, right);
        __imp__sub_000E6790(ctx, base);
        ctx.esp -= 4;  // its ret took the return address
        kernel::W32(ctx.esp, ret);
        x = middle;
        w = end - middle;
        u = u_middle;
        uw = u_end - u_middle;
        Place(x, w, kCenter, left, right);
        return __imp__sub_000E6790(ctx, base);
    }
    Place(x, w, g_anchor == kAuto ? kCenter : g_anchor, left, right);
    __imp__sub_000E6790(ctx, base);
}
