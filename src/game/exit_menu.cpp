// Quit on the main menu, as the other recomps have: a QUIT button under CODENAMES asks with the game's own popup
// whether to exit, and closes the game on yes. The stock buttons move up half a row so the four stay clear of the
// description text below them.
//
// The menu loader (0x93AF9) makes a widget from its definition, clears its event animation slots, sends it the
// file's property messages (style, label string, colours), then finishes it. QUIT is made the same way when
// CODENAMES is finished, with CODENAMES' properties and its own label.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstring>
#include <string>
#include <vector>

#include "app/app.h"
#include "common/translations.h"
#include "core/log.h"
#include "game/main_menu.h"
#include "game/options_menu.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

namespace {

using kernel::G;
using kernel::R32;

constexpr uint32_t kMainMenuId = 0x40000002;
constexpr uint32_t kFirstButtonId = 0x10000002;  // NightFire, Multiplayer, Codenames
constexpr uint32_t kLastButtonId = 0x10000004;
constexpr uint32_t kQuitId = 0x10000005;

// menu records, one a player: +0x1BC the page taking input, and the one widgets are made for
constexpr uint32_t kRecords = 0x25F1D0;
constexpr uint32_t kRecordSize = 0x1D8;
constexpr uint32_t kRecordMessage = 0x92C40;
constexpr uint32_t kRecordPage = 0x1BC;
constexpr uint32_t kMakeWidget = 0x42;    // (definition): make a widget on that page
constexpr uint32_t kFinishWidget = 0x51;  // (widget): the loader is done with it
// widget and page messages, (widget, message, a, b); a page is told of a finished widget too
constexpr uint32_t kWidgetMessage = 0x72630;
constexpr uint32_t kPageMessage = 0x95820;
constexpr uint32_t kSetLabel = 0x18;  // (text, string id): the text when given, else the string
constexpr uint32_t kPressed = 0x4B;   // page message (button, 0), as 0x70FE0 posts it
constexpr uint32_t kUpdate = 0x50;    // page handler message, each frame
// widgets and pages
constexpr uint32_t kId = 0x18;
constexpr uint32_t kParent = 0x0C;
constexpr uint32_t kPageFocus = 0xAC;
constexpr uint32_t kEventSlots = 0x7C;  // animations played on focus, press and the like
constexpr uint32_t kEventSlotCount = 6;

// the description box under the buttons, as 0x95990 finds it
constexpr uint32_t kDescriptionBox = 0x25FC18;

// the popup the pause menu asks with (player, text, value, with a choice, alternate choice): A answers 3, B 4
constexpr uint32_t kAsk = 0x75FB0;
constexpr uint32_t kTakeAnswer = 0x761A0;  // (value out or 0): the answer once, 0 while none
constexpr uint32_t kAnswerYes = 3;
constexpr uint32_t kAnswerNo = 4;

// widget definition, as 0x70D40 reads it
constexpr uint32_t kDefSize = 0x24;
constexpr uint32_t kDefId = 0x00;
constexpr uint32_t kDefY = 0x1A;  // s16, from the page's top
constexpr int16_t kRowPitch = 32;
constexpr int16_t kLift = kRowPitch / 2;

constexpr char kQuitLabel[] = "Quit";
constexpr char kQuitDescription[] = "Exit the game.";
constexpr char kQuitQuestion[] = "Are you sure you want to exit?";

// guest memory: a lifted definition, QUIT's definition, then the texts in the game's language
constexpr uint32_t kTextSize = 128;
constexpr uint32_t kLiftedAt = 0;
constexpr uint32_t kQuitDefAt = kDefSize;
constexpr uint32_t kLabelAt = kDefSize * 2;
constexpr uint32_t kDescriptionAt = kLabelAt + kTextSize;
constexpr uint32_t kQuestionAt = kDescriptionAt + kTextSize;
constexpr uint32_t kScratchSize = kQuestionAt + kTextSize;

struct Message {
    uint32_t id;
    uint32_t a;
    uint32_t b;
};

// CODENAMES while the loader works on it, and what it was sent
struct Codenames {
    uint32_t widget = 0;
    std::vector<Message> sent;
};
Codenames g_codenames;
bool g_asking = false;
bool g_quitting = false;

void PutText(uint32_t at, const char* english) {
    std::string text = game::GameText(english).substr(0, kTextSize - 1);
    std::memcpy(kernel::HostPtr(at), text.c_str(), text.size() + 1);
}

uint32_t Scratch() {
    static const uint32_t base = [] {
        uint32_t at = mem::AllocSystem(kScratchSize);
        PutText(at + kLabelAt, kQuitLabel);
        PutText(at + kDescriptionAt, kQuitDescription);
        PutText(at + kQuestionAt, kQuitQuestion);
        return at;
    }();
    return base;
}

bool StockButton(uint32_t id) { return id >= kFirstButtonId && id <= kLastButtonId; }

bool BuildingMainMenu(uint32_t record) {
    uint32_t page = R32(record + kRecordPage);
    return page && R32(page + kId) == kMainMenuId;
}

// the definition with its row moved up, in guest memory the widget is made from
uint32_t Lifted(uint32_t def) {
    uint32_t copy = Scratch() + kLiftedAt;
    std::memcpy(kernel::HostPtr(copy), kernel::HostPtr(def), kDefSize);
    G<int16_t>(copy + kDefY) = int16_t(G<int16_t>(copy + kDefY) - kLift);
    return copy;
}

// CODENAMES' definition a row lower, as QUIT; its label comes with the messages
void LatchQuitDefinition(uint32_t codenames_def) {
    uint32_t def = Scratch() + kQuitDefAt;
    std::memcpy(kernel::HostPtr(def), kernel::HostPtr(codenames_def), kDefSize);
    kernel::W32(def + kDefId, kQuitId);
    G<int16_t>(def + kDefY) = int16_t(G<int16_t>(def + kDefY) + kRowPitch);
}

// as the loader makes a widget: made, event slots cleared, the file's messages, finished
void MakeQuit(X86Context& ctx, uint32_t record, const std::vector<Message>& sent) {
    uint32_t def = Scratch() + kQuitDefAt;
    uint32_t quit = kernel::CallGuest(ctx, kRecordMessage, {record, kMakeWidget, def, 0});
    if (!quit) {
        LOGE("quit: the QUIT button was not made");
        return;
    }
    for (uint32_t i = 0; i < kEventSlotCount; i++) kernel::W32(quit + kEventSlots + i * 4, 0);
    for (const Message& m : sent) {
        if (m.id == kSetLabel) {
            kernel::CallGuest(ctx, kWidgetMessage, {quit, kSetLabel, Scratch() + kLabelAt, 0});
        } else {
            kernel::CallGuest(ctx, kWidgetMessage, {quit, m.id, m.a, m.b});
        }
    }
    kernel::CallGuest(ctx, kRecordMessage, {record, kFinishWidget, quit, 0});
    kernel::CallGuest(ctx, kPageMessage, {R32(record + kRecordPage), kFinishWidget, quit, 1});
}

void AskToQuit(X86Context& ctx, uint32_t page) {
    uint32_t player = (R32(page + kParent) - kRecords) / kRecordSize;
    g_asking = true;
    kernel::CallGuest(ctx, kAsk, {player, Scratch() + kQuestionAt, 0, 1, 0});
}

void Quit() {
    if (g_quitting) return;
    g_quitting = true;
    LOGR("quit: closing the window");
    PostMessageW(static_cast<HWND>(app::Window()), WM_CLOSE, 0, 0);
}

}  // namespace

bool game::QuitAsked() { return g_asking; }

// a menu record's message handler, cdecl (record, message, a, b)
X86_EXTERN_FUNC(__imp__sub_00092C40);
X86_FUNC(sub_00092C40) {
    uint32_t record = R32(ctx.esp + 4);
    uint32_t message = R32(ctx.esp + 8);
    uint32_t a = R32(ctx.esp + 12);
    bool finished = message == kFinishWidget && a && a == g_codenames.widget;
    if (finished) {
        __imp__sub_00092C40(ctx, base);
        Codenames codenames = std::move(g_codenames);
        g_codenames = {};
        MakeQuit(ctx, record, codenames.sent);
        return;
    }
    bool stock = message == kMakeWidget && a && StockButton(R32(a + kDefId)) && BuildingMainMenu(record);
    if (!stock) {
        __imp__sub_00092C40(ctx, base);
        // the pause menu's RECOMP tab is made the same way, from SCORE (options_menu.cpp)
        if (message == kMakeWidget && a) game::LatchScoreTab(record, a, ctx.eax);
        if (message == kFinishWidget && a) game::EnsureRecompTab(ctx, record, a);
        return;
    }
    uint32_t id = R32(a + kDefId);
    uint32_t lifted = Lifted(a);
    kernel::W32(ctx.esp + 12, lifted);
    __imp__sub_00092C40(ctx, base);
    if (id != kLastButtonId || !ctx.eax) return;
    LatchQuitDefinition(lifted);
    g_codenames = {ctx.eax, {}};
}

// a widget's message, cdecl (widget, message, a, b): CODENAMES' are kept for QUIT, and QUIT pressed on the main
// menu asks to exit
X86_EXTERN_FUNC(__imp__sub_00072630);
X86_FUNC(sub_00072630) {
    uint32_t widget = R32(ctx.esp + 4);
    uint32_t message = R32(ctx.esp + 8);
    uint32_t a = R32(ctx.esp + 12);
    uint32_t b = R32(ctx.esp + 16);
    if (widget && widget == g_codenames.widget) g_codenames.sent.push_back({message, a, b});
    game::LatchScoreMessage(widget, message, a, b);
    bool quit = message == kPressed && b == 0 && widget && a && R32(a + kId) == kQuitId &&
                R32(a + kParent) == widget && R32(widget + kId) == kMainMenuId;
    __imp__sub_00072630(ctx, base);
    if (quit && !g_asking) AskToQuit(ctx, widget);
}

// the description box follows the focus; the game's table has no line for QUIT
X86_EXTERN_FUNC(__imp__sub_000959E0);
X86_FUNC(sub_000959E0) {
    uint32_t page = R32(ctx.esp + 4);
    __imp__sub_000959E0(ctx, base);
    uint32_t focus = R32(page + kPageFocus);
    uint32_t box = R32(kDescriptionBox);
    if (!box || !focus || R32(focus + kId) != kQuitId) return;
    kernel::CallGuest(ctx, kWidgetMessage, {box, kSetLabel, Scratch() + kDescriptionAt, 0});
}

// the main menu's handler, cdecl (player, page, page id, message, a, b): the popup's answer
X86_EXTERN_FUNC(__imp__sub_00085780);
X86_FUNC(sub_00085780) {
    uint32_t message = R32(ctx.esp + 16);
    __imp__sub_00085780(ctx, base);
    if (message != kUpdate || !g_asking) return;
    uint32_t answer = kernel::CallGuest(ctx, kTakeAnswer, {0});
    if (answer == kAnswerYes) Quit();
    if (answer) g_asking = false;
}
