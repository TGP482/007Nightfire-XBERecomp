// RECOMP in the pause menu: a tab right of SCORE, a copy of it made as the loader finishes SCORE. Its page is the two
// column list Objectives and Score fill: the categories, then a category's sections, then a section's rows, with the
// F10 panel's rows, values and wording (src/common/menu.cpp). The tab keeps the focus and the list takes the keys of
// its id: up and down move in the list, A opens, binds or steps a row, B goes back, left and right change a value.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "common/menu.h"
#include "common/recomp_pages.h"
#include "common/translations.h"
#include "core/log.h"
#include "game/options_menu.h"
#include "kernel/memory.h"

namespace {

using app::rows::Kind;
using app::rows::Row;
using kernel::G;
using kernel::R32;

// the pause page; its tabs and their contents are one id, told apart by sub
constexpr uint32_t kPauseId = 0x4000004B;
constexpr uint32_t kPauseWidgets = 0x10000028;
constexpr uint32_t kScoreTab = 4;
constexpr uint32_t kList = 8;          // the two column list
constexpr uint32_t kRecompTab = 0x11;  // past the stock subs, 0 to 0x10
constexpr uint32_t kLegendId = 0x1000006F;  // the bar under the page, its buttons' legend
// the legends OBJECTIVES and SCORE put in the bar as they take the focus (0x80590)
constexpr uint32_t kStockLegends[] = {0x1000218, 0x100021B};
constexpr int16_t kTabGap = 10;  // as 0x80030 lays out the multiplayer tabs
constexpr uint32_t kTabs[] = {0, 1, 3, kScoreTab, kRecompTab};  // left to right; sub 2 is no tab

// menu records, one a player
constexpr uint32_t kRecords = 0x25F1D0;
constexpr uint32_t kRecordSize = 0x1D8;
constexpr uint32_t kRecordPage = 0x1BC;
constexpr uint32_t kRecordPort = 0x1CC;  // u16: the controller port its keys are read from

// widgets and pages
constexpr uint32_t kId = 0x18;
constexpr uint32_t kRect = 0x70;  // s16 x, y, w, h from the page's origin
constexpr uint32_t kEventSlots = 0x7C;  // animations played on focus, press and the like
constexpr uint32_t kEventSlotCount = 6;
constexpr uint32_t kPageFocus = 0xAC;
constexpr uint32_t kPageNav = 0xC0;  // the directions the dpad moves the menu's cursor in (0x94400)
constexpr uint32_t kNoNav = 0;
constexpr uint32_t kSidewaysNav = 3;  // left and right only; 1 is all four, 2 up and down
constexpr uint32_t kSub = 0x20;
constexpr uint32_t kFlags = 0x7A;  // 1 hidden
constexpr uint32_t kHidden = 1;
// a tab (type 1) and the text widget in it that draws its label left aligned at the tab's x
constexpr uint32_t kTabLabel = 0x94;
constexpr uint32_t kLabelFont = 0xC0;
constexpr uint32_t kLabelStringId = 0xF0;  // -1 when the text is given
constexpr uint32_t kLabelText = 0xF4;
constexpr uint32_t kNoStringId = 0xFFFFFFFF;
constexpr uint32_t kListWraps = 0x569;  // byte: a long text goes on in rows of its own

// widget definition, as 0x70D40 reads it
constexpr uint32_t kDefSize = 0x24;
constexpr uint32_t kDefId = 0x00;
constexpr uint32_t kDefSub = 0x0C;
constexpr uint32_t kDefX = 0x18;  // s16
constexpr uint32_t kDefW = 0x1C;  // s16

// guest functions
constexpr uint32_t kRecordMessage = 0x92C40;  // (record, message, a, b)
constexpr uint32_t kWidgetMessage = 0x72630;  // (widget, message, a, b)
constexpr uint32_t kPageMessage = 0x95820;    // (page, message, a, b)
constexpr uint32_t kSendToId = 0x72820;       // (player, id, message, a, b)
constexpr uint32_t kSendToSub = 0x728F0;      // (player, id, sub, message, a, b)
constexpr uint32_t kReadAction = 0x72E20;     // (port, action, state bits): ax nonzero while it has any of them
constexpr uint32_t kPlaySound = 0x730F0;      // (sound)
constexpr uint32_t kStringText = 0x6D460;     // (string id, 0, 0): its text
constexpr uint32_t kMeasureText = 0x6A5D0;    // (text, font, size out, scale or 0 for 1, characters or -1)

// widget messages
constexpr uint32_t kMakeWidget = 0x42;    // record (definition): a widget on the page being built
constexpr uint32_t kFinishWidget = 0x51;  // record and page (widget): the loader is done with it
constexpr uint32_t kSetLabel = 0x18;      // (text, string id)
constexpr uint32_t kSetFlags = 0x2B;      // 0 shown and selectable
constexpr uint32_t kPointer = 0x39;
// list messages, as 0x91B10 takes them
constexpr uint32_t kClearRows = 0x17;     // its columns too, back to one
constexpr uint32_t kAddRow = 0x10;        // (first column's text, data): the row
constexpr uint32_t kSetCell = 0x19;       // (row << 16 | column, text), copied
constexpr uint32_t kAddColumn = 0x0E;     // (width in percent, alignment)
constexpr uint32_t kColumnWidth = 0x28;   // (column, width in percent)
constexpr uint32_t kColumnAlign = 0x59;   // (column, alignment)
constexpr uint32_t kRowHeight = 0x75;     // (percent of the font's line)
constexpr uint32_t kSelectRow = 0x1D;
constexpr uint32_t kSelectedRow = 0x34;
// the pause handlers' messages, (player, widget, id, message, a, b)
constexpr uint32_t kOpened = 0x4C;
constexpr uint32_t kUpdate = 0x50;     // each frame the page takes input
constexpr uint32_t kPressed = 0x4B;    // A
constexpr uint32_t kFocused = 0x4E;
constexpr uint32_t kBackOut = 0x6B;    // B; b points at the answer
constexpr uint32_t kKeepPage = 0xFFFFFFFE;  // the answer that keeps the record from going back a page

// the list: the selection arrow, the label, then the value
constexpr uint32_t kLineHeight = 100;
constexpr uint32_t kArrowWidth = 6;  // the selection arrow's column, as Objectives keeps it
constexpr uint32_t kLabelWidth = 54;
constexpr uint32_t kValueWidth = 40;
constexpr uint32_t kAlignLeft = 0;
constexpr uint32_t kAlignValue = 2;
constexpr uint32_t kArrowColumn = 0;
constexpr uint32_t kLabelColumn = 1;
constexpr uint32_t kValueColumn = 2;

// the menus' actions, 0x72E20 reads them with state bits 1 held, 4 pressed, 8 repeating
constexpr uint32_t kLeft = 28;
constexpr uint32_t kRight = 29;
constexpr uint32_t kPressedOrRepeating = 4 | 8;
constexpr uint32_t kSoundLower = 3;
constexpr uint32_t kSoundHigher = 4;

constexpr char kRecompLabel[] = "RECOMP";

constexpr uint32_t kMaxText = 128;  // a list cell holds 0x84 bytes
// guest memory: RECOMP's definition, a measured size, its label, the legend, then a text on its way to the list
constexpr uint32_t kDefAt = 0;
constexpr uint32_t kSizeAt = kDefSize;  // two floats
constexpr uint32_t kLabelAt = kSizeAt + 8;
constexpr uint32_t kLegendAt = kLabelAt + sizeof(kRecompLabel);  // the bar keeps the pointer
constexpr uint32_t kTextAt = kLegendAt + kMaxText;
constexpr uint32_t kScratchSize = kTextAt + kMaxText;

struct Message {
    uint32_t id;
    uint32_t a;
    uint32_t b;
};

// SCORE while the loader works on it, and what it was sent
struct Score {
    uint32_t widget = 0;
    std::vector<Message> sent;
};
Score g_score;

using Level = app::RecompPages::Level;
using Pressed = app::RecompPages::Pressed;

struct Tab {
    uint32_t page = 0;  // the pause page, as last opened
    // where the stock tabs' labels start and end, as the page first opened
    float strip_left = 0.0f;
    float strip_right = 0.0f;
    bool shown = false;
    app::RecompPages pages;
    uint32_t frame = 0;  // the page's updates
    uint32_t pressed_frame = UINT32_MAX;  // A comes from the tab and the list both: one press an update
    std::vector<std::string> values;  // the lines' values as the list shows them
    std::string legend;  // as the bar shows it
    std::vector<std::string> stock_parts;  // the stock legends' "~V Scroll" like parts for other buttons
};
Tab g_tab;

uint32_t Scratch() {
    static const uint32_t base = [] {
        uint32_t at = mem::AllocSystem(kScratchSize);
        std::memcpy(kernel::HostPtr(at + kLabelAt), kRecompLabel, sizeof(kRecompLabel));
        return at;
    }();
    return base;
}

// the text in guest memory at that scratch slot, cut to what a list cell holds
uint32_t GuestText(uint32_t slot, const std::string& english) {
    uint32_t at = Scratch() + slot;
    std::string text = game::GameText(english);
    size_t length = std::min<size_t>(text.size(), kMaxText - 1);
    std::memcpy(kernel::HostPtr(at), text.data(), length);
    G<char>(at + uint32_t(length)) = 0;
    return at;
}

struct Rect {
    int16_t x;
    int16_t y;
    int16_t w;
    int16_t h;
};

Rect RectOf(uint32_t widget) {
    return {G<int16_t>(widget + kRect), G<int16_t>(widget + kRect + 2), G<int16_t>(widget + kRect + 4),
            G<int16_t>(widget + kRect + 6)};
}

uint32_t PauseWidget(X86Context& ctx, uint32_t player, uint32_t sub) {
    return kernel::CallGuest(ctx, kSendToSub, {player, kPauseWidgets, sub, kPointer, 0, 0});
}

uint32_t SendToList(X86Context& ctx, uint32_t player, uint32_t message, uint32_t a = 0, uint32_t b = 0) {
    return kernel::CallGuest(ctx, kSendToSub, {player, kPauseWidgets, kList, message, a, b});
}

// The tab, made as the loader makes a widget: made, event slots cleared, SCORE's messages, finished.

uint32_t MakeTab(X86Context& ctx, uint32_t record, const std::vector<Message>& sent) {
    uint32_t tab = kernel::CallGuest(ctx, kRecordMessage, {record, kMakeWidget, Scratch() + kDefAt, 0});
    if (!tab) {
        LOGE("pause: the RECOMP tab was not made");
        return 0;
    }
    for (uint32_t i = 0; i < kEventSlotCount; i++) kernel::W32(tab + kEventSlots + i * 4, 0);
    for (const Message& m : sent) {
        if (m.id == kSetLabel) {
            kernel::CallGuest(ctx, kWidgetMessage, {tab, kSetLabel, Scratch() + kLabelAt, 0});
        } else {
            kernel::CallGuest(ctx, kWidgetMessage, {tab, m.id, m.a, m.b});
        }
    }
    kernel::CallGuest(ctx, kRecordMessage, {record, kFinishWidget, tab, 0});
    kernel::CallGuest(ctx, kPageMessage, {R32(record + kRecordPage), kFinishWidget, tab, 1});
    return tab;
}

struct TabLabel {
    uint32_t tab;
    float width;
};

float LabelWidth(X86Context& ctx, uint32_t tab) {
    uint32_t label = tab + kTabLabel;
    uint32_t text = R32(label + kLabelText);
    uint32_t id = R32(label + kLabelStringId);
    if (id != kNoStringId) text = kernel::CallGuest(ctx, kStringText, {id, 0, 0});
    uint32_t size = Scratch() + kSizeAt;
    G<float>(size) = 0.0f;
    if (text) kernel::CallGuest(ctx, kMeasureText, {text, R32(label + kLabelFont), size, 0, 0xFFFFFFFF});
    return G<float>(size);
}

// the shown tabs left to right, RECOMP last
std::vector<TabLabel> ShownTabs(X86Context& ctx, uint32_t player) {
    std::vector<TabLabel> tabs;
    for (uint32_t sub : kTabs) {
        uint32_t tab = PauseWidget(ctx, player, sub);
        if (tab && !(G<uint8_t>(tab + kFlags) & kHidden)) tabs.push_back({tab, LabelWidth(ctx, tab)});
    }
    return tabs;
}

void LatchStrip(const std::vector<TabLabel>& tabs) {
    const TabLabel& last_stock = tabs[tabs.size() - 2];
    g_tab.strip_left = RectOf(tabs.front().tab).x;
    g_tab.strip_right = float(RectOf(last_stock.tab).x) + last_stock.width;
}

// The labels spread evenly over the stock labels' span, the multiplayer pause's too (it moves the tabs as it opens),
// each tab reaching to the next label.
void LayOutTabs(X86Context& ctx, uint32_t player, bool latch) {
    std::vector<TabLabel> tabs = ShownTabs(ctx, player);
    bool recomp_last = tabs.size() >= 2 && R32(tabs.back().tab + kSub) == kRecompTab;
    if (!recomp_last) return;
    if (latch) LatchStrip(tabs);
    float labels = 0.0f;
    for (const TabLabel& t : tabs) labels += t.width;
    float gap = (g_tab.strip_right - g_tab.strip_left - labels) / float(tabs.size() - 1);
    float x = g_tab.strip_left;
    for (const TabLabel& t : tabs) {
        int16_t left = int16_t(std::lround(x));
        x += t.width + gap;
        // a pixel short of the next: 0x734D0 moves left only onto a tab ending before the one it leaves
        G<int16_t>(t.tab + kRect) = left;
        G<int16_t>(t.tab + kRect + 4) = int16_t(std::lround(x) - left - 1);
    }
    static bool logged = false;
    if (logged) return;
    logged = true;
    LOGR("pause: %zu tabs over %.0f..%.0f, labels %.0f wide, gaps %.1f", tabs.size(), double(g_tab.strip_left),
         double(g_tab.strip_right), double(labels), double(gap));
}

// A legend is parts two spaces apart, each "~" and a button then its label; prompts.cpp names the keys.

constexpr char kPartGap[] = "  ";

bool OwnButton(char code) { return code == 'A' || code == 'B' || code == 'S'; }

void AddStockParts(std::vector<std::string>& parts, std::string_view text) {
    for (size_t at = 0; at < text.size();) {
        size_t end = std::min(text.find(kPartGap, at), text.size());
        std::string_view part = text.substr(at, end - at);
        at = end + 2;
        if (part.size() < 2 || part[0] != '~' || OwnButton(part[1])) continue;
        auto same = [&](const std::string& p) { return p[1] == part[1]; };
        if (std::none_of(parts.begin(), parts.end(), same)) parts.emplace_back(part);
    }
}

void LatchStockParts(X86Context& ctx) {
    g_tab.stock_parts.clear();
    for (uint32_t id : kStockLegends) {
        uint32_t text = kernel::CallGuest(ctx, kStringText, {id, 0, 0});
        if (!text) continue;
        const char* host = static_cast<const char*>(kernel::HostPtr(text));
        std::string_view legend(host, strnlen(host, kMaxText));
        AddStockParts(g_tab.stock_parts, legend);
        static int logged = 0;
        if (logged++ < 2) LOGR("pause: stock legend %08X \"%.*s\"", id, int(legend.size()), legend.data());
    }
}

// first, the stock parts, then last; on the rows left and right change a value, so no part of theirs
std::string JoinLegend(const char* first, const char* last, bool rows) {
    std::string legend = first;
    for (const std::string& part : g_tab.stock_parts) {
        if (rows && part[1] == 'H') continue;
        legend += kPartGap + part;
    }
    return legend + kPartGap + last;
}

std::string Legend(const Row* selected) {
    if (g_tab.pages.level() == Level::Categories) return JoinLegend("~A Select", "~S Continue", false);
    if (g_tab.pages.level() == Level::Sections) return JoinLegend("~A Select", "~B Back", false);
    if (app::Capturing()) return "Press a key or mouse button to bind, Escape cancels";
    if (selected && selected->kind == Kind::Bind) return JoinLegend("~A Change key", "~B Back", true);
    return JoinLegend("~H Change", "~B Back", true);
}

// Showing the page.

// up and down stay with the tab's list, else they moved the cursor off the row; on RECOMP's rows left and right
// change a value instead of moving to the next tab
void ApplyNav() {
    if (!g_tab.page) return;
    bool rows = g_tab.shown && g_tab.pages.level() == Level::Rows;
    kernel::W32(g_tab.page + kPageNav, rows ? kNoNav : kSidewaysNav);
}

void SetCell(X86Context& ctx, uint32_t player, uint32_t row, uint32_t column, const std::string& text) {
    SendToList(ctx, player, kSetCell, row << 16 | column, GuestText(kTextAt, text));
}

// shown, emptied and laid out in columns
void ResetList(X86Context& ctx, uint32_t player) {
    uint32_t list = PauseWidget(ctx, player, kList);
    if (list) G<uint8_t>(list + kListWraps) = 0;
    SendToList(ctx, player, kRowHeight, kLineHeight);
    SendToList(ctx, player, kSetFlags, 0);
    SendToList(ctx, player, kClearRows);
    SendToList(ctx, player, kColumnWidth, kArrowColumn, kArrowWidth);
    SendToList(ctx, player, kAddColumn, kLabelWidth, kAlignLeft);
    SendToList(ctx, player, kAddColumn, kValueWidth, kAlignLeft);
    SendToList(ctx, player, kColumnAlign, kValueColumn, kAlignValue);
}

void UpdateLegend(X86Context& ctx, uint32_t player, const Row* selected) {
    std::string legend = Legend(selected);
    if (legend == g_tab.legend) return;
    g_tab.legend = legend;
    kernel::CallGuest(ctx, kSendToId, {player, kLegendId, kSetLabel, GuestText(kLegendAt, legend), 0});
}

// the level's lines, at the line it remembers
void ApplyLevel(X86Context& ctx, uint32_t player) {
    ResetList(ctx, player);
    std::vector<app::RecompPages::Line> lines = g_tab.pages.Lines();
    g_tab.values.clear();
    for (const app::RecompPages::Line& line : lines) {
        uint32_t index = SendToList(ctx, player, kAddRow, 0, 0);
        SetCell(ctx, player, index, kLabelColumn, line.label);
        if (!line.value.empty()) SetCell(ctx, player, index, kValueColumn, line.value);
        g_tab.values.push_back(line.value);
    }
    int last = std::max(int(lines.size()) - 1, 0);
    SendToList(ctx, player, kSelectRow, uint32_t(std::clamp(g_tab.pages.line(), 0, last)));
    UpdateLegend(ctx, player, nullptr);
    ApplyNav();
}

void ShowTab(X86Context& ctx, uint32_t player, bool shown) {
    g_tab.shown = shown;
    if (shown) {
        g_tab.legend.clear();  // the stock tabs wrote the bar
        g_tab.pages.Top();
        ApplyLevel(ctx, player);
    }
    ApplyNav();
}

// the pause opened: the categories again, the stock directions back
void OpenTab(X86Context& ctx, uint32_t player, uint32_t page) {
    bool fresh = page != g_tab.page;
    g_tab.page = page;
    g_tab.pages.Open();
    LatchStockParts(ctx);
    LayOutTabs(ctx, player, fresh);
    uint32_t focus = R32(page + kPageFocus);
    bool focused = focus && R32(focus + kId) == kPauseWidgets && R32(focus + kSub) == kRecompTab;
    ShowTab(ctx, player, focused);
}

// Input.

void PlayStep(X86Context& ctx, int step) { kernel::CallGuest(ctx, kPlaySound, {step < 0 ? kSoundLower : kSoundHigher}); }

void Press(X86Context& ctx, uint32_t player) {
    if (g_tab.pressed_frame == g_tab.frame || app::Capturing()) return;
    g_tab.pressed_frame = g_tab.frame;
    int line = int(SendToList(ctx, player, kSelectedRow));
    Pressed pressed = g_tab.pages.Press(line);
    if (pressed == Pressed::Opened) ApplyLevel(ctx, player);
    if (pressed == Pressed::Stepped) PlayStep(ctx, 1);
}

// B goes a level up; false on the categories, where it does what it always did
bool BackOut(X86Context& ctx, uint32_t player) {
    if (!g_tab.shown || g_tab.pages.level() == Level::Categories) return false;
    if (!app::Capturing() && g_tab.pages.Back()) ApplyLevel(ctx, player);
    return true;
}

bool ActionDown(X86Context& ctx, uint32_t port, uint32_t action) {
    return (kernel::CallGuest(ctx, kReadAction, {port, action, kPressedOrRepeating}) & 0xFFFF) != 0;
}

// the values as they are now, a key capture's end and the F10 panel's changes among them
void UpdateValues(X86Context& ctx, uint32_t player) {
    std::vector<app::RecompPages::Line> lines = g_tab.pages.Lines();
    for (size_t i = 0; i < lines.size() && i < g_tab.values.size(); i++) {
        if (lines[i].value == g_tab.values[i]) continue;
        g_tab.values[i] = lines[i].value;
        SetCell(ctx, player, uint32_t(i), kValueColumn, lines[i].value);
    }
}

void UpdateTab(X86Context& ctx, uint32_t player) {
    g_tab.frame++;
    if (!g_tab.shown || g_tab.pages.level() != Level::Rows) return;
    uint32_t port = G<uint16_t>(kRecords + (player & 0xFF) * kRecordSize + kRecordPort);
    int step = int(ActionDown(ctx, port, kRight)) - int(ActionDown(ctx, port, kLeft));
    int line = int(SendToList(ctx, player, kSelectedRow));
    if (step && !app::Capturing() && g_tab.pages.Step(line, step)) PlayStep(ctx, step);
    UpdateValues(ctx, player);
    UpdateLegend(ctx, player, g_tab.pages.RowAt(line));
}

}  // namespace

void game::LatchScoreTab(uint32_t record, uint32_t def, uint32_t widget) {
    uint32_t page = R32(record + kRecordPage);
    bool pause = page && R32(page + kId) == kPauseId;
    bool score = widget && R32(def + kDefId) == kPauseWidgets && R32(def + kDefSub) == kScoreTab;
    if (!pause || !score) return;
    std::memcpy(kernel::HostPtr(Scratch() + kDefAt), kernel::HostPtr(def), kDefSize);
    g_score = {widget, {}};
}

void game::LatchScoreMessage(uint32_t widget, uint32_t message, uint32_t a, uint32_t b) {
    if (widget && widget == g_score.widget) g_score.sent.push_back({message, a, b});
}

// SCORE's definition at the next sub, a gap to its right; the multiplayer layout moves it again on open
void game::EnsureRecompTab(X86Context& ctx, uint32_t record, uint32_t finished) {
    if (!finished || finished != g_score.widget) return;
    Score score = std::move(g_score);
    g_score = {};
    uint32_t def = Scratch() + kDefAt;
    kernel::W32(def + kDefSub, kRecompTab);
    G<int16_t>(def + kDefX) = int16_t(G<int16_t>(def + kDefX) + G<int16_t>(def + kDefW) + kTabGap);
    g_tab = {};
    MakeTab(ctx, record, score.sent);
}

// the pause page's handler, cdecl (player, page, page id, message, a, b)
X86_EXTERN_FUNC(__imp__sub_00080030);
X86_FUNC(sub_00080030) {
    uint32_t player = R32(ctx.esp + 4);
    uint32_t page = R32(ctx.esp + 8);
    uint32_t message = R32(ctx.esp + 16);
    uint32_t answer = R32(ctx.esp + 24);
    if (message == kBackOut && answer && BackOut(ctx, player)) {
        kernel::W32(answer, kKeepPage);
        kernel::Return(ctx, 1, 0);
        return;
    }
    __imp__sub_00080030(ctx, base);
    uint32_t result = ctx.eax;
    if (message == kOpened) OpenTab(ctx, player, page);
    if (message == kUpdate) UpdateTab(ctx, player);
    ctx.eax = result;
}

// the pause widgets' handler, cdecl (player, widget, id, message, a, b)
X86_EXTERN_FUNC(__imp__sub_00080590);
X86_FUNC(sub_00080590) {
    uint32_t player = R32(ctx.esp + 4);
    uint32_t widget = R32(ctx.esp + 8);
    uint32_t message = R32(ctx.esp + 16);
    uint32_t sub = R32(widget + kSub);
    // the list focused would hide with the tabs' contents
    if (message == kFocused && g_tab.shown && sub == kList) {
        kernel::Return(ctx, 1, 0);
        return;
    }
    __imp__sub_00080590(ctx, base);
    uint32_t result = ctx.eax;
    if (message == kFocused) ShowTab(ctx, player, sub == kRecompTab);
    bool ours = sub == kRecompTab || sub == kList;
    if (message == kPressed && g_tab.shown && ours) Press(ctx, player);
    ctx.eax = result;
}
