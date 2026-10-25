// RECOMP in the pause menu: a tab right of SCORE, a copy of it made as the loader finishes SCORE. Its page is the two
// column list Objectives and Score fill: the categories, then a category's sections, then a section's rows, with the
// F10 panel's rows, values and wording (src/common/menu.cpp). The tab keeps the focus and the list takes the keys of
// its id: up and down move in the list, A opens, binds or steps a row, B goes back, left and right change a value.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "common/menu.h"
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
constexpr uint32_t kTitleId = 0x1000006F;  // the shown tab's name above its page
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
constexpr uint32_t kPageNav = 0xC0;  // the directions the dpad moves the menu's cursor in, 0 none
constexpr uint32_t kNoNav = 0;
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
constexpr char kRestoreDefaults[] = "RESTORE DEFAULTS";
const char* const kCategories[] = {"RECOMP SETTINGS", "MOUSE AND KEYBOARD", "CONTROLLER"};  // rows::Page order
constexpr int kCategoryCount = int(std::size(kCategories));

constexpr uint32_t kMaxText = 128;  // a list cell holds 0x84 bytes
// guest memory: RECOMP's definition, a measured size, its label, the title, then a text on its way to the list
constexpr uint32_t kDefAt = 0;
constexpr uint32_t kSizeAt = kDefSize;  // two floats
constexpr uint32_t kLabelAt = kSizeAt + 8;
constexpr uint32_t kTitleAt = kLabelAt + sizeof(kRecompLabel);
constexpr uint32_t kTextAt = kTitleAt + kMaxText;
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

enum class Level { Categories, Sections, Rows };

struct Tab {
    uint32_t page = 0;  // the pause page, as last opened
    uint32_t stock_nav = 0;
    // where the stock tabs' labels start and end, as the page first opened
    float strip_left = 0.0f;
    float strip_right = 0.0f;
    bool shown = false;
    Level level = Level::Categories;
    int category = 0;      // the open category's rows::Page
    size_t section = 0;    // the open section's heading, by its index in the page
    int category_row = 0;  // the list row the categories and the sections were left on
    int section_row = 0;
    uint32_t frame = 0;  // the page's updates
    uint32_t pressed_frame = UINT32_MAX;  // A comes from the tab and the list both: one press an update
    std::vector<std::string> values;  // the section's values as the list shows them
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
uint32_t GuestText(uint32_t slot, const std::string& text) {
    uint32_t at = Scratch() + slot;
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
        G<int16_t>(t.tab + kRect) = int16_t(std::lround(x));
        G<int16_t>(t.tab + kRect + 4) = int16_t(std::lround(t.width + gap));
        x += t.width + gap;
    }
    static bool logged = false;
    if (logged) return;
    logged = true;
    LOGR("pause: %zu tabs over %.0f..%.0f, labels %.0f wide, gaps %.1f", tabs.size(), double(g_tab.strip_left),
         double(g_tab.strip_right), double(labels), double(gap));
}

// The page's lines: the categories with rows, then a category's sections, then a section's rows.

struct PageRows {
    const Row* list = nullptr;
    size_t count = 0;
};

PageRows RowsOf(int page) {
    PageRows rows;
    rows.list = app::rows::Page(page, &rows.count);
    return rows;
}

std::vector<int> Categories() {
    std::vector<int> pages;
    for (int page = 0; page < kCategoryCount; page++) {
        if (RowsOf(page).count) pages.push_back(page);
    }
    return pages;
}

// the headings a row follows, by their index in the page
std::vector<size_t> Sections(int page) {
    PageRows rows = RowsOf(page);
    std::vector<size_t> headings;
    for (size_t i = 0; i + 1 < rows.count; i++) {
        bool filled = rows.list[i + 1].kind != Kind::Heading;
        if (rows.list[i].kind == Kind::Heading && filled) headings.push_back(i);
    }
    return headings;
}

// the rows after a heading, up to the next
std::vector<const Row*> SectionRows(int page, size_t heading) {
    PageRows rows = RowsOf(page);
    std::vector<const Row*> out;
    for (size_t i = heading + 1; i < rows.count; i++) {
        if (rows.list[i].kind == Kind::Heading) break;
        out.push_back(&rows.list[i]);
    }
    return out;
}

struct Line {
    std::string label;
    std::string value;
};

std::vector<Line> Lines() {
    std::vector<Line> lines;
    if (g_tab.level == Level::Categories) {
        for (int page : Categories()) lines.push_back({kCategories[page], {}});
        lines.push_back({kRestoreDefaults, {}});
    } else if (g_tab.level == Level::Sections) {
        PageRows rows = RowsOf(g_tab.category);
        for (size_t heading : Sections(g_tab.category)) lines.push_back({rows.list[heading].label, {}});
    } else {
        for (const Row* row : SectionRows(g_tab.category, g_tab.section)) {
            lines.push_back({row->label, app::rows::Value(*row)});
        }
    }
    return lines;
}

std::string Title() {
    if (g_tab.level == Level::Categories) return kRecompLabel;
    if (g_tab.level == Level::Sections) return kCategories[g_tab.category];
    return RowsOf(g_tab.category).list[g_tab.section].label;
}

// Showing the page.

// left and right change a row's value instead of moving to the next tab
void ApplyNav() {
    if (!g_tab.page) return;
    bool rows = g_tab.shown && g_tab.level == Level::Rows;
    kernel::W32(g_tab.page + kPageNav, rows ? kNoNav : g_tab.stock_nav);
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

// the level's title and lines, with that row selected
void ApplyLevel(X86Context& ctx, uint32_t player, int row) {
    kernel::CallGuest(ctx, kSendToId, {player, kTitleId, kSetLabel, GuestText(kTitleAt, Title()), 0});
    ResetList(ctx, player);
    std::vector<Line> lines = Lines();
    g_tab.values.clear();
    for (const Line& line : lines) {
        uint32_t index = SendToList(ctx, player, kAddRow, 0, 0);
        SetCell(ctx, player, index, kLabelColumn, line.label);
        if (!line.value.empty()) SetCell(ctx, player, index, kValueColumn, line.value);
        g_tab.values.push_back(line.value);
    }
    int last = std::max(int(lines.size()) - 1, 0);
    SendToList(ctx, player, kSelectRow, uint32_t(std::clamp(row, 0, last)));
    ApplyNav();
}

void ShowTab(X86Context& ctx, uint32_t player, bool shown) {
    g_tab.shown = shown;
    if (shown) {
        g_tab.level = Level::Categories;
        ApplyLevel(ctx, player, g_tab.category_row);
    }
    ApplyNav();
}

// the pause opened: the categories again, the stock directions back
void OpenTab(X86Context& ctx, uint32_t player, uint32_t page) {
    bool fresh = page != g_tab.page;
    if (fresh) {
        g_tab.page = page;
        g_tab.stock_nav = R32(page + kPageNav);
    }
    g_tab.level = Level::Categories;
    g_tab.category_row = 0;
    g_tab.section_row = 0;
    LayOutTabs(ctx, player, fresh);
    uint32_t focus = R32(page + kPageFocus);
    bool focused = focus && R32(focus + kId) == kPauseWidgets && R32(focus + kSub) == kRecompTab;
    ShowTab(ctx, player, focused);
}

// Input.

void StepRow(X86Context& ctx, const Row& row, int step) {
    app::rows::Value(row, step);
    kernel::CallGuest(ctx, kPlaySound, {step < 0 ? kSoundLower : kSoundHigher});
}

void PressCategory(X86Context& ctx, uint32_t player, int row) {
    std::vector<int> pages = Categories();
    if (row < 0 || row > int(pages.size())) return;
    g_tab.category_row = row;
    if (row == int(pages.size())) {
        for (int page : pages) app::rows::Defaults(page);
        return;
    }
    g_tab.category = pages[size_t(row)];
    g_tab.level = Level::Sections;
    ApplyLevel(ctx, player, 0);
}

void PressSection(X86Context& ctx, uint32_t player, int row) {
    std::vector<size_t> headings = Sections(g_tab.category);
    if (row < 0 || row >= int(headings.size())) return;
    g_tab.section_row = row;
    g_tab.section = headings[size_t(row)];
    g_tab.level = Level::Rows;
    ApplyLevel(ctx, player, 0);
}

// a bind row waits for a key, a value row steps on, as a click on it does
void PressRow(X86Context& ctx, int row) {
    std::vector<const Row*> rows = SectionRows(g_tab.category, g_tab.section);
    if (row < 0 || row >= int(rows.size())) return;
    const Row& pressed = *rows[size_t(row)];
    if (pressed.kind == Kind::Bind) {
        app::rows::Bind(pressed);
    } else {
        StepRow(ctx, pressed, 1);
    }
}

void Press(X86Context& ctx, uint32_t player) {
    if (g_tab.pressed_frame == g_tab.frame || app::Capturing()) return;
    g_tab.pressed_frame = g_tab.frame;
    int row = int(SendToList(ctx, player, kSelectedRow));
    if (g_tab.level == Level::Categories) {
        PressCategory(ctx, player, row);
    } else if (g_tab.level == Level::Sections) {
        PressSection(ctx, player, row);
    } else {
        PressRow(ctx, row);
    }
}

// B goes a level up; false on the categories, where it does what it always did
bool BackOut(X86Context& ctx, uint32_t player) {
    if (!g_tab.shown || g_tab.level == Level::Categories) return false;
    if (app::Capturing()) return true;
    if (g_tab.level == Level::Rows) {
        g_tab.level = Level::Sections;
        ApplyLevel(ctx, player, g_tab.section_row);
    } else {
        g_tab.level = Level::Categories;
        ApplyLevel(ctx, player, g_tab.category_row);
    }
    return true;
}

bool ActionDown(X86Context& ctx, uint32_t port, uint32_t action) {
    return (kernel::CallGuest(ctx, kReadAction, {port, action, kPressedOrRepeating}) & 0xFFFF) != 0;
}

// the values as they are now, a key capture's end and the F10 panel's changes among them
void UpdateValues(X86Context& ctx, uint32_t player, const std::vector<const Row*>& rows) {
    for (size_t i = 0; i < rows.size() && i < g_tab.values.size(); i++) {
        std::string value = app::rows::Value(*rows[i]);
        if (value == g_tab.values[i]) continue;
        g_tab.values[i] = value;
        SetCell(ctx, player, uint32_t(i), kValueColumn, value);
    }
}

void UpdateTab(X86Context& ctx, uint32_t player) {
    g_tab.frame++;
    if (!g_tab.shown || g_tab.level != Level::Rows) return;
    std::vector<const Row*> rows = SectionRows(g_tab.category, g_tab.section);
    uint32_t port = G<uint16_t>(kRecords + (player & 0xFF) * kRecordSize + kRecordPort);
    int step = int(ActionDown(ctx, port, kRight)) - int(ActionDown(ctx, port, kLeft));
    int row = int(SendToList(ctx, player, kSelectedRow));
    bool on_row = row >= 0 && row < int(rows.size());
    if (step && on_row && !app::Capturing()) StepRow(ctx, *rows[size_t(row)], step);
    UpdateValues(ctx, player, rows);
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
