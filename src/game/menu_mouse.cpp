#include <algorithm>
#include <climits>
#include <cmath>

#include "app/app.h"
#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "core/log.h"
#include "game/main_menu.h"
#include "xdk/hle.h"

// The game's menus run on a pointer of their own: each page update (0x95A80) first focuses the widget under the
// menu's cursor (0x73370), which the dpad moves from widget to widget and the update puts back on the focus after.
// The mouse moves that cursor, so the game changes the focus with its own sounds and messages.
namespace {

using kernel::G;
using kernel::R32;

// menu record
constexpr uint32_t kRecordCursor = 0x94;  // its 2D item, centered at +0x24/+0x26 (s16, frame pixels)
constexpr uint32_t kCursorX = 0x24;
constexpr uint32_t kCursorY = 0x26;
constexpr uint32_t kRecordPage = 0x1BC;  // the page taking input
// a page, and the widgets in its list: +0 the next, rect x, y, w, h (s16; a widget's from its page's origin)
constexpr uint32_t kPageWidgets = 0x94;
constexpr uint32_t kPageLink = 0xA0;  // dwords from a widget's start to its link
constexpr uint32_t kPageFocus = 0xAC;
constexpr uint32_t kPageNav = 0xC0;
constexpr uint32_t kParent = 0x0C;
constexpr uint32_t kId = 0x18;
constexpr uint32_t kSub = 0x20;
constexpr uint32_t kRect = 0x70;
constexpr uint32_t kZ = 0x78;
constexpr uint32_t kFlags = 0x7A;  // 1 hidden, 2 not selectable
constexpr uint32_t kType = 0x7B;
enum Type : uint8_t {
    kButton = 1,
    kCheckbox = 2,
    kDropdown = 3,
    kText = 5,
    kList = 6,
    kChoiceRow = 9,
    kValue = 10,
    kRegion = 13,
    kTextBox = 200,
};
// a list (type 6), its rows one row height apart from the top of its box
constexpr uint32_t kListRows = 0x278;
constexpr uint32_t kListVisible = 0x500;
constexpr uint32_t kListRowHeight = 0x530;  // float
constexpr uint32_t kListTop = 0x534;        // the first row shown
constexpr uint32_t kSelectRow = 0x1D;  // message: select the nth row, as its scrollbar does
constexpr uint32_t kPostMessage = 0x72630;  // (widget, message, a, b)
constexpr uint32_t kStyleOf = 0x71DB0;      // (widget): its style, +8 the top padding
constexpr uint32_t kStylePadTop = 8;
// a value (type 10): a step is the value, a sound and 0x49 to its parent, as 0x97BE0 steps it
constexpr uint32_t kValueMin = 0x128;
constexpr uint32_t kValueMax = 0x12C;
constexpr uint32_t kValueNow = 0x130;
constexpr uint32_t kValueFlags = 0x13C;
constexpr uint32_t kValueWraps = 0x144;  // byte
constexpr uint32_t kPlaySound = 0x730F0;  // (sound)
constexpr uint32_t kSoundLower = 3;
constexpr uint32_t kSoundHigher = 4;
constexpr uint32_t kValueChanged = 0x49;
// a choice row (type 9), for the log
constexpr uint32_t kChoiceItems = 0x2F8;
constexpr uint32_t kChoiceIndex = 0x2D8;
// A carousel shows its value as five texts of one id, subs 0 to 4 the index -2 to +2, sub 2 in the band (0x7F680).
constexpr uint32_t kCarouselTexts = 5;
constexpr uint32_t kBand = 2;
constexpr int kRowSlack = 7;  // the rows are 17 high and 29 apart: the gaps go to the nearer row
// player 1's action values and their state bits, as the menus read them (0x72D70); 0x6CF50 makes both anew each frame
constexpr uint32_t kActions = 0x1FE6E4;
constexpr uint32_t kStates = 0x1FE7D4;
constexpr uint32_t kPlayerBlock = 0x158;  // the other players' follow
constexpr uint32_t kAccept = 31;
constexpr uint8_t kHeldPressed = 1 | 4;
// view 7 draws the menus; on a widescreen output hud.cpp draws its sprites at 4:3 around its middle
constexpr uint32_t kMenuView = 0x1F661C + 7 * 4;
constexpr uint32_t kViewLeft = 0xDC;
constexpr uint32_t kViewRight = 0xE0;
constexpr uint32_t kViewTop = 0xE4;
constexpr float kNarrow = 0.75f;
constexpr float kFrameWidth = 640.0f;
constexpr float kFrameHeight = 480.0f;
constexpr int kMaxWidgets = 256;
// the main menu page and its button legend text
constexpr uint32_t kMainMenuId = 0x40000002;
constexpr uint32_t kLegendId = 0x10000001;

uint32_t g_main_menu = 0;  // the main menu page while it takes input, until the menus' next run

struct Rect {
    int x;
    int y;
    int w;
    int h;
    bool Around(int px, int py) const { return px > x && px < x + w && py > y && py < y + h; }
};

Rect RectOf(uint32_t o) {
    return {G<int16_t>(o + kRect), G<int16_t>(o + kRect + 2), G<int16_t>(o + kRect + 4), G<int16_t>(o + kRect + 6)};
}

uint32_t FirstWidget(uint32_t page) {
    uint32_t w = R32(page + kPageWidgets);
    return w ? w - R32(page + kPageLink) * 4 : 0;
}

uint8_t TypeOf(uint32_t w) { return G<uint8_t>(w + kType); }

bool CarouselRow(uint32_t page, uint32_t text) {
    if (TypeOf(text) != kText || R32(text + kSub) >= kCarouselTexts) return false;
    uint32_t count = 0;
    int n = 0;
    for (uint32_t w = FirstWidget(page); w && n < kMaxWidgets; w = R32(w), n++)
        count += TypeOf(w) == kText && R32(w + kId) == R32(text + kId);
    return count == kCarouselTexts;
}

// the value a carousel shows: the focus, else the page's first selectable value
uint32_t CarouselValue(uint32_t page) {
    uint32_t focus = R32(page + kPageFocus);
    if (focus && TypeOf(focus) == kValue) return focus;
    int n = 0;
    for (uint32_t w = FirstWidget(page); w && n < kMaxWidgets; w = R32(w), n++)
        if (TypeOf(w) == kValue && !G<uint8_t>(w + kFlags)) return w;
    return 0;
}

// false when the value is already at its end that way
bool Step(X86Context& ctx, uint32_t value, int delta) {
    int now = int(R32(value + kValueNow));
    int lo = int(R32(value + kValueMin));
    int hi = int(R32(value + kValueMax));
    int to = now + delta;
    if (G<uint8_t>(value + kValueWraps) && hi >= lo)
        to = lo + ((to - lo) % (hi - lo + 1) + (hi - lo + 1)) % (hi - lo + 1);
    to = std::clamp(to, lo, hi);
    if (to == now) return false;
    kernel::W32(value + kValueNow, uint32_t(to));
    kernel::CallGuest(ctx, kPlaySound, {to < now ? kSoundLower : kSoundHigher});
    kernel::CallGuest(ctx, kPostMessage, {R32(value + kParent), kValueChanged, value, 0});
    return true;
}

// 0x73370's pick: the smallest selectable widget strictly around the point (page pixels), the lower z on a tie
uint32_t WidgetAt(uint32_t page, int px, int py) {
    uint32_t best = 0;
    int best_score = INT_MIN;
    uint16_t best_z = 0;
    int n = 0;
    for (uint32_t w = FirstWidget(page); w && n < kMaxWidgets; w = R32(w), n++) {
        if (G<uint8_t>(w + kFlags) || TypeOf(w) == kRegion) continue;
        Rect r = RectOf(w);
        if (!r.Around(px, py)) continue;
        int score = (int(kFrameHeight) - r.h) + (int(kFrameWidth) - r.w);
        uint16_t z = G<uint16_t>(w + kZ);
        if (score < best_score || (score == best_score && z >= best_z)) continue;
        best = w;
        best_score = score;
        best_z = z;
    }
    return best;
}

// the carousel text whose middle is nearest the point down, across it; they are not selectable
uint32_t CarouselRowAt(uint32_t page, int px, int py) {
    uint32_t best = 0;
    int best_off = INT_MAX;
    int n = 0;
    for (uint32_t w = FirstWidget(page); w && n < kMaxWidgets; w = R32(w), n++) {
        Rect r = RectOf(w);
        if ((G<uint8_t>(w + kFlags) & 1) || px <= r.x || px >= r.x + r.w) continue;
        int off = std::abs(2 * py - (2 * r.y + r.h));  // twice the distance from its middle
        if (off > r.h + 2 * kRowSlack || off >= best_off || !CarouselRow(page, w)) continue;
        best = w;
        best_off = off;
    }
    return best;
}

// one frame of an action, as if the keyboard's player pressed it; the page's widgets read it next
void Tap(uint32_t value) {
    int player = input::KeyboardPlayer();
    if (player < 0) return;
    uint32_t block = uint32_t(player) * kPlayerBlock;
    G<float>(kActions + block + value * 4) = 1.0f;
    G<uint8_t>(kStates + block + value) |= kHeldPressed;
}

bool Accepts(uint8_t type) {
    return type == kButton || type == kCheckbox || type == kDropdown || type == kText || type == kList ||
           type == kTextBox;
}

// the list's row under the point, selected the way its scrollbar does when it differs from the last one picked here
void SelectRow(X86Context& ctx, uint32_t list, int py, bool click) {
    static uint32_t last_list = 0;
    static int last_row = -1;
    Rect r = RectOf(list);
    float height = G<float>(list + kListRowHeight);
    if (height <= 0.0f) return;
    uint32_t style = kernel::CallGuest(ctx, kStyleOf, {list});
    int pad = style ? G<int16_t>(style + kStylePadTop) : 0;
    int shown = int(std::floor(float(py - r.y - pad) / height));
    int row = int(R32(list + kListTop)) + shown;
    if (shown < 0 || shown >= int(R32(list + kListVisible)) || row >= int(R32(list + kListRows))) return;
    if (!click && list == last_list && row == last_row) return;
    last_list = list;
    last_row = row;
    kernel::CallGuest(ctx, kPostMessage, {list, kSelectRow, uint32_t(row), 0});
}

// The pages' widgets, once a page: what the next stage of the mouse needs (types, rects, value ranges).
void LogPage(uint32_t page) {
    static uint32_t last = 0;
    if (page == last) return;
    last = page;
    Rect p = RectOf(page);
    LOGR("menu: page %08X id %08X nav %u at %d,%d", page, R32(page + kId), R32(page + kPageNav), p.x, p.y);
    int n = 0;
    for (uint32_t w = FirstWidget(page); w && n < kMaxWidgets; w = R32(w), n++) {
        Rect r = RectOf(w);
        uint8_t type = TypeOf(w);
        LOGR("menu:   type %u id %08X sub %u flags %u z %u rect %d,%d %dx%d", type, R32(w + kId), R32(w + kSub),
             G<uint8_t>(w + kFlags), G<uint16_t>(w + kZ), r.x, r.y, r.w, r.h);
        if (type == kValue)
            LOGR("menu:     value %d of %d..%d flags %u", int(R32(w + kValueNow)), int(R32(w + kValueMin)),
                 int(R32(w + kValueMax)), G<uint8_t>(w + kValueFlags));
        if (type == kList)
            LOGR("menu:     rows %u visible %u height %.1f top %u", R32(w + kListRows), R32(w + kListVisible),
                 double(G<float>(w + kListRowHeight)), R32(w + kListTop));
        if (type == kChoiceRow) LOGR("menu:     item %u of %u", R32(w + kChoiceIndex), R32(w + kChoiceItems));
    }
}

float ViewTop() {
    uint32_t view = R32(kMenuView);
    return view ? G<float>(view + kViewTop) : 0.0f;
}

// the mouse in frame pixels as the menus' cursor counts them
bool MousePoint(int& x, int& y) {
    float fx;
    float fy;
    if (!input::Cursor(fx, fy)) return false;
    uint32_t view = R32(kMenuView);
    float left = view ? G<float>(view + kViewLeft) : 0.0f;
    float right = view ? G<float>(view + kViewRight) : kFrameWidth;
    float shown = fx * kFrameWidth;
    // where a sprite drawn here was laid out: the inverse of hud.cpp's centered placement
    if (app::Widescreen()) shown = left + (shown - left - (right - left) * (1 - kNarrow) / 2) / kNarrow;
    x = int(std::floor(shown - left));
    y = int(std::floor(fy * kFrameHeight - ViewTop()));
    return true;
}

void Point(X86Context& ctx, uint32_t record, uint32_t page) {
    static int last_x = INT_MIN;
    static int last_y = INT_MIN;
    static bool was_down = false;
    static int last_sub = -1;  // the carousel row the pointer came onto, -1 off the carousel
    static uint32_t last_page = 0;
    if (page != last_page) last_sub = -1;
    last_page = page;
    LogPage(page);
    int x;
    int y;
    bool shown = MousePoint(x, y);
    bool down = shown && input::LeftButton();
    // the button that binds in a key capture clicks nothing, held past it too
    bool click = down && !was_down && !app::Capturing();
    was_down = down;
    if (!shown) {
        last_x = INT_MIN;
        last_sub = -1;
        return;
    }
    bool moved = x != last_x || y != last_y;
    last_x = x;
    last_y = y;
    if (!moved && !click) return;
    Rect p = RectOf(page);
    int px = x - p.x;
    int py = y - p.y;
    // Coming onto a carousel row brings the item it shows then into the band; the pointer stays. A click accepts the
    // band's.
    uint32_t row = CarouselRowAt(page, px, py);
    if (row) {
        int sub = int(R32(row + kSub));
        uint32_t value = CarouselValue(page);
        if (sub != last_sub && value) Step(ctx, value, sub - int(kBand));
        last_sub = sub;
        if (click) Tap(kAccept);
        return;
    }
    last_sub = -1;
    uint32_t w = WidgetAt(page, px, py);
    if (!w) return;
    uint32_t focus = R32(page + kPageFocus);
    uint8_t type = TypeOf(w);
    // widgets of one id all take input while any of them is focused; focusing a list of the focus's id would tell the
    // page to show that list alone (the pause hides its tabs' content then)
    bool shared = focus && R32(w + kId) == R32(focus + kId);
    if (type == kList && shared) {
        SelectRow(ctx, w, py, click);
    } else if (w != focus && !(shared && type != kButton && type != kText)) {
        uint32_t cursor = R32(record + kRecordCursor);
        G<int16_t>(cursor + kCursorX) = int16_t(x);
        G<int16_t>(cursor + kCursorY) = int16_t(y);
    }
    if (click && Accepts(type)) Tap(kAccept);
}

}  // namespace

// a menu page's update (record, page, input): the current page with input first, its focus from the cursor
X86_EXTERN_FUNC(__imp__sub_00095A80);
X86_FUNC(sub_00095A80) {
    uint32_t record = R32(ctx.esp + 4);
    uint32_t page = R32(ctx.esp + 8);
    bool input = G<uint8_t>(ctx.esp + 12) != 0;
    bool current = page == R32(record + kRecordPage);
    if (current && R32(page + kId) == kMainMenuId) g_main_menu = page;
    if (input && current) Point(ctx, record, page);
    __imp__sub_00095A80(ctx, base);
}

void game::ClearMainMenuLegend() { g_main_menu = 0; }

game::LegendRow game::TakeMainMenuLegend() {
    uint32_t page = g_main_menu;
    if (!page) return {};
    int n = 0;
    for (uint32_t w = FirstWidget(page); w && n < kMaxWidgets; w = R32(w), n++) {
        if (TypeOf(w) != kText || R32(w + kId) != kLegendId) continue;
        Rect p = RectOf(page);
        Rect r = RectOf(w);
        return {true, float(p.x), ViewTop() + float(p.y + r.y), float(r.h)};
    }
    static bool logged = false;
    if (!logged) LOGR("watermark: no button legend on the main menu page %08X", page);
    logged = true;
    return {};
}
