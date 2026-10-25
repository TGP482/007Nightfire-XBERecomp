// RECOMP in Driving.xbe's pause menu: a tab after SCORE whose page lists the categories, then a category's sections,
// then a section's rows, with the F10 panel's rows, values and wording (src/common/recomp_pages.cpp). The pause draws
// its tabs and pages itself each frame (0xDD340) and takes its keys as messages (0xDCC10): the tab strip is drawn
// here with RECOMP added, the page is drawn where the stock pages are, and the menu's messages drive it while it is
// shown. Up and down move in the list, A opens, binds or steps a row, B goes back, left and right change a value.
#include <algorithm>
#include <atomic>
#include <bit>
#include <chrono>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include "common/menu.h"
#include "common/mouse_keyboard.h"
#include "common/recomp_pages.h"
#include "core/log.h"
#include "driving/pause_menu.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {

using kernel::G;
using kernel::R32;
using Level = app::RecompPages::Level;
using Pressed = app::RecompPages::Pressed;

// the pause menu, as 0xDAB50 builds it: its text objects and state
constexpr uint32_t kRenderer = 0x18;
constexpr uint32_t kFirstMissionLine = 0x110;   // CONTINUE, RESTART then QUIT, one line apart
constexpr uint32_t kSecondMissionLine = 0x114;
constexpr uint32_t kStockTabs[] = {0x11C, 0x120, 0x128, 0x12C};  // MISSION, OBJECTIVES, CONTROLS, SCORE
constexpr uint32_t kChangePagePicture = 0x1FC;  // the dpad left and right in the legend
constexpr uint32_t kTab = 0x470;
constexpr uint32_t kMissionLine = 0x474;  // the selected one
constexpr uint32_t kConfirmingQuit = 0x484;
constexpr uint32_t kObjectivesHeld = 0x488;  // OBJECTIVES keeps the dpad while 1
constexpr uint32_t kHidesTabs = 0x454;       // byte
constexpr uint32_t kMode = 0x310;
constexpr uint32_t kTablessMode = 0x18577;  // its page 5, no tabs
constexpr uint32_t kMissionTab = 0;
constexpr uint32_t kObjectivesTab = 1;
constexpr uint32_t kScoreTab = 3;
constexpr uint32_t kRecompTab = 4;  // the page switch draws nothing for it
constexpr uint32_t kTablessTab = 5;  // the tabless mode's page, RESTART and QUIT alone
constexpr int kTabCount = 5;
constexpr int kMissionLines = 3;
// a text object: x, y at +0, +4 and its text's string id
constexpr uint32_t kObjectY = 0x04;
constexpr uint32_t kObjectString = 0x34;

// the menu's messages (FrontEnd.def's dpad, ACCEPT, CANCEL)
constexpr uint32_t kUp = 0x41;
constexpr uint32_t kDown = 0x42;
constexpr uint32_t kLeft = 0x43;
constexpr uint32_t kRight = 0x44;
constexpr uint32_t kAccept = 0x45;
constexpr uint32_t kCancel = 0x46;

// guest functions; the renderer's are thiscall
constexpr uint32_t kStringText = 0xE3AF0;  // cdecl (string id): its text
constexpr uint32_t kSetFont = 0xD6EB0;     // (text object): its font, colour and place
constexpr uint32_t kSetScale = 0xD61E0;    // (x scale, y scale)
constexpr uint32_t kMeasure = 0xD5AD0;     // (text): its width in st(0)
constexpr uint32_t kDrawText = 0xD6520;    // (text, x, y)
constexpr uint32_t kPlaySound = 0x11E010;  // cdecl (8, sound, bank, 2), as the menu's dpad plays it
constexpr uint32_t kSoundToggle = 0x1A000C;  // "SFX_Toggle"
constexpr uint32_t kSoundBank = 0x19372C;    // "Menu Sounds"
constexpr uint32_t kSoundChannel = 8;
constexpr uint32_t kSoundPlay = 2;
constexpr uint32_t kRendererColour = 0x50;

// the pause draw's values (0xDEA1C on)
constexpr uint32_t kTextScale = 0x19FF00;     // float 1
constexpr uint32_t kSelectedColour = 0x23F4CC;
constexpr uint32_t kTabColour = 0x23F4E0;
constexpr uint32_t kLineColour = 0x23F4D8;    // a mission line not selected
constexpr uint32_t kDisplay = 0x1EBFF4;       // +0x4C set on a widescreen output
constexpr uint32_t kDisplayWide = 0x4C;
constexpr uint32_t kWideScale = 0x19FEA8;     // the widths' scale then
constexpr float kTabsLeft = 80.0f;
constexpr float kTabsSpan = 485.0f;  // the room the tabs are spread over, past their labels
constexpr float kTabRaise = 4.0f;    // below the first tab object's y
// the objectives' text box (0xDDAE7) holds the page; the lines keep to the tabs' span
constexpr float kLinesLeft = kTabsLeft;
constexpr float kLinesRight = kTabsLeft + kTabsSpan;
constexpr float kBoxBottom = 310.0f;
constexpr float kFrameWidth = 640.0f;  // the pause lays out its text on the 640x480 frame
constexpr float kFrameHeight = 480.0f;

// the legend's flags on the pause draw's stack, by the button and label they add (0xDEBB8 on)
constexpr uint32_t kLegendChangePage = 0x19;  // dpad left and right
constexpr uint32_t kLegendContinue = 0x1A;    // START
constexpr uint32_t kLegendScroll = 0x1B;      // dpad up and down
constexpr uint32_t kLegendBack = 0x2A;        // B
constexpr uint32_t kLegendSelect = 0x2B;      // A
// the legend's lists at 0xDECFA, esi entries long
constexpr uint32_t kLegendPictures = 0x58;
constexpr uint32_t kLegendLabels = 0x74;
constexpr uint32_t kMaxLegend = 7;

constexpr char kRecompLabel[] = "RECOMP";
constexpr char kChangeLabel[] = "Change";
constexpr uint32_t kMaxText = 128;
constexpr uint32_t kMaxLines = 16;  // shown at once; the text draw may keep the pointers to the frame's end
// guest memory: the two labels, then a label and a value a shown line
constexpr uint32_t kRecompAt = 0;
constexpr uint32_t kChangeAt = kMaxText;
constexpr uint32_t kLinesAt = kChangeAt + kMaxText;
constexpr uint32_t kScratchSize = kLinesAt + kMaxLines * 2 * kMaxText;

struct Page {
    app::RecompPages pages;
    int line = 0;
    int top = 0;  // the first line shown
};
Page g_page;

uint32_t Scratch() {
    static const uint32_t base = [] {
        uint32_t at = mem::AllocSystem(kScratchSize);
        std::memcpy(kernel::HostPtr(at + kRecompAt), kRecompLabel, sizeof(kRecompLabel));
        std::memcpy(kernel::HostPtr(at + kChangeAt), kChangeLabel, sizeof(kChangeLabel));
        return at;
    }();
    return base;
}

uint32_t GuestText(uint32_t slot, const std::string& text) {
    uint32_t at = Scratch() + slot;
    size_t length = std::min<size_t>(text.size(), kMaxText - 1);
    std::memcpy(kernel::HostPtr(at), text.data(), length);
    G<char>(at + uint32_t(length)) = 0;
    return at;
}

uint32_t Bits(float value) { return std::bit_cast<uint32_t>(value); }

uint32_t CallThis(X86Context& ctx, uint32_t fn, uint32_t self, std::initializer_list<uint32_t> args) {
    uint32_t ecx = ctx.ecx;
    ctx.ecx = self;
    uint32_t result = kernel::CallGuest(ctx, fn, args);
    ctx.ecx = ecx;
    return result;
}

bool TabsShown(uint32_t menu) { return !G<uint8_t>(menu + kHidesTabs) && R32(menu + kMode) != kTablessMode; }

bool RecompShown(uint32_t menu) { return TabsShown(menu) && R32(menu + kTab) == kRecompTab; }

float ObjectY(uint32_t menu, uint32_t object) { return G<float>(R32(menu + object) + kObjectY); }

// The renderer's text, as the pause draw lays it out.

struct Pen {
    uint32_t renderer;
    uint32_t font;  // the text object whose font and place the text takes
    float widths;   // the widths' scale on a widescreen output
};

Pen PenFor(uint32_t menu, uint32_t font_object) {
    uint32_t display = R32(kDisplay);
    bool wide = display && G<uint8_t>(display + kDisplayWide);
    float widths = wide ? G<float>(kWideScale) : 1.0f;
    return {R32(menu + kRenderer), R32(menu + font_object), widths};
}

float Measure(X86Context& ctx, const Pen& pen, uint32_t text) {
    CallThis(ctx, kSetFont, pen.renderer, {pen.font});
    CallThis(ctx, kSetScale, pen.renderer, {R32(kTextScale), R32(kTextScale)});
    CallThis(ctx, kMeasure, pen.renderer, {text});
    double width = X87_ST(0);
    x87_pop(ctx);
    return float(width) * pen.widths;
}

void Draw(X86Context& ctx, const Pen& pen, uint32_t text, float x, float y, uint32_t colour) {
    CallThis(ctx, kSetFont, pen.renderer, {pen.font});
    CallThis(ctx, kSetScale, pen.renderer, {R32(kTextScale), R32(kTextScale)});
    kernel::W32(pen.renderer + kRendererColour, R32(colour));
    CallThis(ctx, kDrawText, pen.renderer, {text, Bits(x), Bits(y)});
}

// The tab strip: the stock labels and RECOMP spread over the stock span, the selected one lit.

struct TabLabel {
    uint32_t text;
    float x;
    float width;
};

struct TabStrip {
    TabLabel labels[kTabCount];
    float y;
};

uint32_t TabText(X86Context& ctx, uint32_t menu, int tab) {
    if (tab == int(kRecompTab)) return Scratch() + kRecompAt;
    uint32_t object = R32(menu + kStockTabs[tab]);
    return kernel::CallGuest(ctx, kStringText, {R32(object + kObjectString)});
}

TabStrip LayTabs(X86Context& ctx, uint32_t menu, const Pen& pen) {
    TabStrip strip{};
    float total = 0.0f;
    for (int i = 0; i < kTabCount; i++) {
        uint32_t text = TabText(ctx, menu, i);
        strip.labels[i] = {text, 0.0f, Measure(ctx, pen, text)};
        total += strip.labels[i].width;
    }
    float gap = (kTabsSpan - total) / float(kTabCount - 1);
    float x = kTabsLeft;
    for (TabLabel& label : strip.labels) {
        label.x = x;
        x += label.width + gap;
    }
    strip.y = ObjectY(menu, kStockTabs[0]) + kTabRaise;
    return strip;
}

void DrawTabs(X86Context& ctx, uint32_t menu) {
    Pen pen = PenFor(menu, kStockTabs[0]);
    TabStrip strip = LayTabs(ctx, menu, pen);
    uint32_t selected = R32(menu + kTab);
    for (int i = 0; i < kTabCount; i++) {
        uint32_t colour = uint32_t(i) == selected ? kSelectedColour : kTabColour;
        Draw(ctx, pen, strip.labels[i].text, strip.labels[i].x, strip.y, colour);
    }
}

// The page: as many lines as the objectives' box holds, scrolled to the selected one.

struct Lines {
    float first_y;
    float pitch;
    int shown;
};

Lines LinesFor(uint32_t menu) {
    float first = ObjectY(menu, kFirstMissionLine);
    float pitch = ObjectY(menu, kSecondMissionLine) - first;
    int shown = pitch > 0.0f ? int((kBoxBottom - first) / pitch) : 1;
    shown = std::clamp(shown, 1, int(kMaxLines));
    static bool logged = false;
    if (!logged) LOGR("pause: RECOMP lines from y %.1f, %.1f apart, %d shown", double(first), double(pitch), shown);
    logged = true;
    return {first, pitch, shown};
}

void UpdateScroll(int shown, int count) {
    g_page.line = std::clamp(g_page.line, 0, std::max(count - 1, 0));
    if (g_page.line < g_page.top) g_page.top = g_page.line;
    if (g_page.line >= g_page.top + shown) g_page.top = g_page.line - shown + 1;
    g_page.top = std::clamp(g_page.top, 0, std::max(count - shown, 0));
}

void DrawPage(X86Context& ctx, uint32_t menu) {
    std::vector<app::RecompPages::Line> lines = g_page.pages.Lines();
    Lines layout = LinesFor(menu);
    UpdateScroll(layout.shown, int(lines.size()));
    Pen pen = PenFor(menu, kFirstMissionLine);
    int end = std::min(int(lines.size()), g_page.top + layout.shown);
    for (int i = g_page.top; i < end; i++) {
        uint32_t slot = kLinesAt + uint32_t(i - g_page.top) * 2 * kMaxText;
        const app::RecompPages::Line& line = lines[size_t(i)];
        float y = layout.first_y + layout.pitch * float(i - g_page.top);
        uint32_t colour = i == g_page.line ? kSelectedColour : kLineColour;
        Draw(ctx, pen, GuestText(slot, line.label), kLinesLeft, y, colour);
        if (line.value.empty()) continue;
        uint32_t value = GuestText(slot + kMaxText, line.value);
        Draw(ctx, pen, value, kLinesRight - Measure(ctx, pen, value), y, colour);
    }
}

void SetLegend(uint32_t esp, Level level) {
    G<uint8_t>(esp + kLegendSelect) = 1;
    G<uint8_t>(esp + kLegendScroll) = 1;
    G<uint8_t>(esp + kLegendChangePage) = 1;
    if (level == Level::Categories) {
        G<uint8_t>(esp + kLegendContinue) = 1;
    } else {
        G<uint8_t>(esp + kLegendBack) = 1;
    }
}

// Input.

void PlayToggle(X86Context& ctx) {
    kernel::CallGuest(ctx, kPlaySound, {kSoundChannel, kSoundToggle, kSoundBank, kSoundPlay});
}

// the stock tabs' own rule: no tab change while QUIT asks or OBJECTIVES holds the dpad
bool TabsMove(uint32_t menu) {
    uint32_t tab = R32(menu + kTab);
    bool held = tab == kObjectivesTab && R32(menu + kObjectivesHeld) == 1;
    return !held && R32(menu + kConfirmingQuit) == 0;
}

void SelectTab(X86Context& ctx, uint32_t menu, uint32_t tab) {
    kernel::W32(menu + kTab, tab);
    if (tab == kRecompTab) {
        g_page.pages.Top();
        g_page.line = g_page.pages.line();
    }
    PlayToggle(ctx);
}

void MoveLine(X86Context& ctx, int step) {
    int count = int(g_page.pages.Lines().size());
    if (count == 0) return;
    g_page.line = (g_page.line + step + count) % count;
    PlayToggle(ctx);
}

void ShowLevel() { g_page.line = g_page.pages.line(); }

// left and right step a row's value; elsewhere they go to the next tab
void Sideways(X86Context& ctx, uint32_t menu, int step) {
    if (g_page.pages.level() == Level::Rows) {
        if (g_page.pages.Step(g_page.line, step)) PlayToggle(ctx);
        return;
    }
    SelectTab(ctx, menu, step < 0 ? kScoreTab : kMissionTab);
}

void Accept(X86Context& ctx) {
    Pressed pressed = g_page.pages.Press(g_page.line);
    if (pressed == Pressed::Opened) ShowLevel();
    if (pressed == Pressed::Stepped) PlayToggle(ctx);
}

void Cancel(X86Context& ctx) {
    if (!g_page.pages.Back()) return;
    ShowLevel();
    PlayToggle(ctx);
}

// a message on the RECOMP page; every one stops there, the stock pages' handling has nothing for it
void TakeRecompMessage(X86Context& ctx, uint32_t menu, uint32_t message) {
    if (app::Capturing()) return;
    if (message == kUp || message == kDown) MoveLine(ctx, message == kUp ? -1 : 1);
    if (message == kLeft || message == kRight) Sideways(ctx, menu, message == kLeft ? -1 : 1);
    if (message == kAccept) Accept(ctx);
    if (message == kCancel) Cancel(ctx);
}

// true when the message was taken: on RECOMP, or moving onto it from MISSION or SCORE
bool TakeMessage(X86Context& ctx, uint32_t menu, uint32_t message) {
    if (!TabsShown(menu)) return false;
    uint32_t tab = R32(menu + kTab);
    if (tab == kRecompTab) {
        TakeRecompMessage(ctx, menu, message);
        return true;
    }
    bool onto = (tab == kMissionTab && message == kLeft) || (tab == kScoreTab && message == kRight);
    if (!onto || !TabsMove(menu)) return false;
    SelectTab(ctx, menu, kRecompTab);
    return true;
}

// The mouse: the pointer picks the line under it (a mission line or a RECOMP row) as the dpad would and a click on a
// tab shows its page. A click on a line presses A and the right button presses B (keyboard.cpp).

constexpr int64_t kShownMs = 100;  // drawn this recently, the pause is up
std::atomic<int64_t> g_drawn_ms{0};
std::atomic<bool> g_accepting{false};

int64_t NowMs() {
    auto since = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(since).count();
}

struct Pointer {
    float x;
    float y;
    bool moved;
    bool click;
};

// false while the pointer is not shown
bool ReadPointer(Pointer& p) {
    static float last_x = -1.0f;
    static float last_y = -1.0f;
    static bool was_down = false;
    float fx;
    float fy;
    bool shown = input::Cursor(fx, fy);
    bool down = shown && input::LeftButton();
    // the button that binds in a key capture clicks nothing
    p.click = down && !was_down && !app::Capturing();
    was_down = down;
    if (!down) g_accepting = false;
    if (!shown) {
        last_x = -1.0f;
        return false;
    }
    p.x = fx * kFrameWidth;
    p.y = fy * kFrameHeight;
    p.moved = p.x != last_x || p.y != last_y;
    last_x = p.x;
    last_y = p.y;
    return true;
}

// the line under the pointer counted from the first, -1 when none of the count is
int LineAt(float y, float first_y, float pitch, int count) {
    if (pitch <= 0.0f || y < first_y) return -1;
    int line = int((y - first_y) / pitch);
    return line < count ? line : -1;
}

void ClickTab(X86Context& ctx, uint32_t menu, const Pointer& p) {
    if (!p.click || !TabsShown(menu) || !TabsMove(menu)) return;
    TabStrip strip = LayTabs(ctx, menu, PenFor(menu, kStockTabs[0]));
    float pitch = ObjectY(menu, kSecondMissionLine) - ObjectY(menu, kFirstMissionLine);
    if (p.y < strip.y || p.y >= strip.y + pitch) return;
    for (int i = 0; i < kTabCount; i++) {
        const TabLabel& label = strip.labels[i];
        if (p.x < label.x || p.x >= label.x + label.width) continue;
        if (R32(menu + kTab) != uint32_t(i)) SelectTab(ctx, menu, uint32_t(i));
        return;
    }
}

void PointMissionLine(X86Context& ctx, uint32_t menu, const Pointer& p) {
    uint32_t tab = R32(menu + kTab);
    if ((tab != kMissionTab && tab != kTablessTab) || R32(menu + kConfirmingQuit) != 0) return;
    float first = ObjectY(menu, kFirstMissionLine);
    float pitch = ObjectY(menu, kSecondMissionLine) - first;
    int line = LineAt(p.y, first, pitch, kMissionLines);
    int lowest = R32(menu + kMode) == kTablessMode ? 1 : 0;  // the tabless page has no CONTINUE
    if (line < lowest) return;
    if (p.click) g_accepting = true;
    if ((!p.moved && !p.click) || R32(menu + kMissionLine) == uint32_t(line)) return;
    kernel::W32(menu + kMissionLine, uint32_t(line));
    PlayToggle(ctx);
}

void PointRecompLine(X86Context& ctx, uint32_t menu, const Pointer& p) {
    if (!RecompShown(menu)) return;
    Lines layout = LinesFor(menu);
    int count = int(g_page.pages.Lines().size());
    int shown = LineAt(p.y, layout.first_y, layout.pitch, std::min(layout.shown, count - g_page.top));
    if (shown < 0) return;
    if (p.click) g_accepting = true;
    int line = g_page.top + shown;
    if ((!p.moved && !p.click) || g_page.line == line) return;
    g_page.line = line;
    PlayToggle(ctx);
}

void UpdatePauseMouse(X86Context& ctx, uint32_t menu) {
    g_drawn_ms = NowMs();
    Pointer p{};
    if (!ReadPointer(p)) return;
    ClickTab(ctx, menu, p);
    PointMissionLine(ctx, menu, p);
    PointRecompLine(ctx, menu, p);
}

}  // namespace

bool driving::PauseShown() { return NowMs() - g_drawn_ms < kShownMs; }

bool driving::ClickAccepts() { return g_accepting; }

// at 0xDE9E0, after the pages are drawn: RECOMP's page and its legend
void DrivingRecompPage(X86Context& ctx, uint8_t*) {
    uint32_t menu = ctx.ebp;
    UpdatePauseMouse(ctx, menu);
    if (!RecompShown(menu)) return;
    DrawPage(ctx, menu);
    SetLegend(ctx.esp, g_page.pages.level());
}

// at 0xDEA1C, in place of the stock strip up to 0xDEBB8
bool DrivingTabStrip(X86Context& ctx, uint8_t*) {
    DrawTabs(ctx, ctx.ebp);
    return true;
}

// from DrivingLegendKeys at 0xDECFA: on a section's rows, the dpad left and right change a value
void DrivingRecompLegend(X86Context& ctx) {
    uint32_t menu = ctx.ebp;
    if (!RecompShown(menu) || g_page.pages.level() != Level::Rows) return;
    uint32_t count = std::min(ctx.esi, kMaxLegend);
    for (uint32_t i = 0; i < count; i++) {
        if (R32(ctx.esp + kLegendPictures + i * 4) != R32(menu + kChangePagePicture)) continue;
        kernel::W32(ctx.esp + kLegendLabels + i * 4, Scratch() + kChangeAt);
    }
}

// the pause opened, thiscall (menu)
X86_EXTERN_FUNC(__imp__sub_000DCAE0);
X86_FUNC(sub_000DCAE0) {
    __imp__sub_000DCAE0(ctx, base);
    g_page = {};
    g_page.pages.Open();
}

// the pause menu's messages, thiscall (menu, message)
X86_EXTERN_FUNC(__imp__sub_000DCC10);
X86_FUNC(sub_000DCC10) {
    uint32_t menu = ctx.ecx;
    uint32_t message = R32(ctx.esp + 4);
    if (!TakeMessage(ctx, menu, message)) return __imp__sub_000DCC10(ctx, base);
    kernel::Return(ctx, 0, 1);
}
