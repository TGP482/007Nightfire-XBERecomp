// Custom, a ninth controller layout after the game's eight: the game keeps NightFire as its own (Driving.xbe and the
// saved profile only know eight) and the controller binds (src/common/pad_binds.cpp) press its buttons' actions.
#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>

#include "app/app.h"
#include "common/options.h"
#include "common/pad_binds.h"
#include "common/translations.h"
#include "game/custom_layout.h"
#include "kernel/memory.h"

namespace {

using kernel::G;
using kernel::R32;

constexpr uint32_t kNightFire = 0;
constexpr uint32_t kCustom = 8;
constexpr uint32_t kPlayers = 4;

// the players' blocks (0xDE720)
constexpr uint32_t kPlayer1Layout = 0x1FE6DE;   // s16
constexpr uint32_t kPlayer1Actions = 0x1FE6E4;  // floats
constexpr uint32_t kPlayerBlock = 0x158;
constexpr uint32_t kBlockConnected = 0x141;  // byte, past the actions
constexpr uint32_t kBlockPort = 0x142;
constexpr uint32_t kZoom = 6;
// each port's buttons held, as 0xE76A0 polls them
constexpr uint32_t kPadButtons = 0x2FF4C8;
constexpr uint32_t kPadSize = 0xA8;

// menu records, one a player
constexpr uint32_t kRecords = 0x25F1D0;
constexpr uint32_t kRecordSize = 0x1D8;
constexpr uint32_t kRecordPlayer = 0x1CE;  // byte: the block its menus set

// guest functions
constexpr uint32_t kSendToId = 0x72820;   // (player, id, message, a, b)
constexpr uint32_t kSendToSub = 0x728F0;  // (player, id, sub, message, a, b)
constexpr uint32_t kStringText = 0x6D460;  // (string id, 0, 0): its text
constexpr uint32_t kPauseDiagram = 0x749C0;  // (player, layout): the pause CONTROLS lines
constexpr uint32_t kMainDiagram = 0x73D10;   // (player, layout): the main menu's controls page labels

// messages
constexpr uint32_t kAddRow = 0x10;  // (text, data): a picker's data is its layout, which a pick reads and selects by
constexpr uint32_t kSelectRow = 0x1E;
constexpr uint32_t kSetLabel = 0x18;  // (text, string id)
constexpr uint32_t kPointer = 0x39;
constexpr uint32_t kSelectedData = 0x35;
constexpr uint32_t kOpened = 0x4C;
constexpr uint32_t kUpdate = 0x50;  // each frame the page takes input
// widget
constexpr uint32_t kFlags = 0x7A;
constexpr uint32_t kHidden = 1;

// the layout pickers: the pause CONTROLS tab's (0x80030) and the main menu controls page's (0x8CE70)
constexpr uint32_t kNoSub = UINT32_MAX;
struct Picker {
    uint32_t id;
    uint32_t sub;
};
constexpr Picker kPausePicker = {0x10000028, 9};
constexpr Picker kMainPicker = {0x10000025, kNoSub};

// a NightFire line: its text on both controls pages, the main menu's label, the buttons it names
struct Line {
    uint32_t text;
    uint32_t label;
    int first;
    int second;
    bool stick;  // the stick's own text, then its click's action
};
constexpr Line kLines[] = {
    {0xEB, 0x10000045, input::kPadA, -1, false},
    {0xE9, 0x10000043, input::kPadB, -1, false},
    {0xEA, 0x10000044, input::kPadX, -1, false},
    {0xE8, 0x10000042, input::kPadY, -1, false},
    {0xE6, 0x10000040, input::kPadLb, -1, false},
    {0xE7, 0x10000041, input::kPadRb, -1, false},
    {0xDF, 0x10000047, input::kPadLt, -1, false},
    {0xE5, 0x1000003F, input::kPadRt, -1, false},
    {0xE0, 0x10000048, input::kPadLs, -1, true},
    {0xEC, 0x10000046, input::kPadRs, -1, true},
    {0xE2, 0x10000049, input::kPadLeft, input::kPadRight, false},
    {0xE4, 0x1000004A, input::kPadUp, input::kPadDown, false},
    {0xDE, 0x10000095, input::kPadBack, -1, false},
};
constexpr uint32_t kLineCount = uint32_t(std::size(kLines));
constexpr uint32_t kMaxText = 28;  // 0x74910 prints a line into 0x20 bytes, "~A " first

// guest memory: each line's text, then the layout's name
constexpr uint32_t kTextSize = 64;
constexpr uint32_t kNameAt = kLineCount * kTextSize;
bool g_lines_shown = false;  // 0x749C0 draws Custom
uint32_t g_lines_version = UINT32_MAX;  // the binds the lines last showed

uint32_t Scratch() {
    static const uint32_t base = mem::AllocSystem(kNameAt + kTextSize);
    return base;
}

uint32_t PutText(uint32_t at, const std::string& text) {
    size_t length = std::min<size_t>(text.size(), kMaxText);
    std::memcpy(kernel::HostPtr(at), text.data(), length);
    G<char>(at + uint32_t(length)) = 0;
    return at;
}

void ChooseCustom(uint32_t player, bool custom) {
    uint32_t& layouts = game::Opt().custom_layouts;
    uint32_t bit = 1u << player;
    uint32_t chosen = custom ? layouts | bit : layouts & ~bit;
    if (chosen == layouts) return;
    layouts = chosen;
    app::Save();
}

bool Custom(uint32_t player) {
    bool chosen = game::Opt().custom_layouts >> player & 1;
    return chosen && G<int16_t>(kPlayer1Layout + player * kPlayerBlock) == int16_t(kNightFire);
}

uint32_t PickerPlayer(uint32_t player) { return G<uint8_t>(kRecords + (player & 0xFF) * kRecordSize + kRecordPlayer); }

uint32_t SendToPicker(X86Context& ctx, uint32_t player, const Picker& p, uint32_t message, uint32_t a, uint32_t b) {
    if (p.sub == kNoSub) return kernel::CallGuest(ctx, kSendToId, {player, p.id, message, a, b});
    return kernel::CallGuest(ctx, kSendToSub, {player, p.id, p.sub, message, a, b});
}

// Custom after the game's eight, picked when it is the player's
void AddLine(X86Context& ctx, uint32_t player, const Picker& p, bool chosen) {
    uint32_t name = PutText(Scratch() + kNameAt, game::GameText("Custom"));
    SendToPicker(ctx, player, p, kAddRow, name, kCustom);
    if (chosen) SendToPicker(ctx, player, p, kSelectRow, kCustom, 0);
}

// Custom picked and its lines from older binds
bool Stale(X86Context& ctx, uint32_t player, const Picker& p) {
    if (g_lines_version == input::PadBindsVersion()) return false;
    return SendToPicker(ctx, player, p, kSelectedData, 0, 0) == kCustom;
}

// The binds pressed: each action's value, the zoom summed.

float& Value(uint32_t player, uint32_t value) { return G<float>(kPlayer1Actions + player * kPlayerBlock + value * 4); }

void PressBinds(uint32_t player, uint32_t held) {
    size_t count = 0;
    const input::PadAction* actions = input::PadActions(&count);
    for (size_t i = 0; i < count; i++) Value(player, actions[i].value) = 0.0f;
    float zoom = 0.0f;
    for (size_t i = 0; i < count; i++) {
        if (!(held & input::PadButtonBit(input::PadBinding(i)))) continue;
        Value(player, actions[i].value) = 1.0f;
        zoom += float(actions[i].zoom);
    }
    Value(player, kZoom) = zoom;
}

// The controls lines name what the binds put on their buttons, in the game's own words.

std::string GameString(X86Context& ctx, uint32_t id) {
    uint32_t text = kernel::CallGuest(ctx, kStringText, {id, 0, 0});
    if (!text) return {};
    const char* host = static_cast<const char*>(kernel::HostPtr(text));
    return std::string(host, strnlen(host, kTextSize));
}

std::string BoundText(X86Context& ctx, int button) {
    if (button < 0) return {};
    size_t count = 0;
    const input::PadAction* actions = input::PadActions(&count);
    for (size_t i = 0; i < count; i++)
        if (input::PadBinding(i) == button) return GameString(ctx, actions[i].text);
    return {};
}

void Join(std::string& text, const std::string& part) {
    if (part.empty() || text.find(part) != std::string::npos) return;
    text += text.empty() ? part : " / " + part;
}

std::string LineText(X86Context& ctx, const Line& line) {
    std::string text = line.stick ? GameString(ctx, line.text) : std::string();
    Join(text, BoundText(ctx, line.first));
    Join(text, BoundText(ctx, line.second));
    return text;
}

// before the lines show: the texts are read with the game's own still in place
void LatchLines(X86Context& ctx) {
    g_lines_version = input::PadBindsVersion();
    for (uint32_t i = 0; i < kLineCount; i++) PutText(Scratch() + i * kTextSize, LineText(ctx, kLines[i]));
}

}  // namespace

void game::ApplyCustomLayouts(uint32_t players) {
    for (uint32_t p = 0; p < players && p < kPlayers; p++) {
        uint32_t actions = kPlayer1Actions + p * kPlayerBlock;
        if (!Custom(p) || !G<uint8_t>(actions + kBlockConnected)) continue;
        uint32_t port = G<uint8_t>(actions + kBlockPort) & 3;
        PressBinds(p, R32(kPadButtons + port * kPadSize));
    }
}

bool game::CustomLayoutChosen(uint32_t player) { return Custom(PickerPlayer(player)); }

void game::AddCustomLayout(X86Context& ctx, uint32_t player, bool chosen) {
    AddLine(ctx, player, kPausePicker, chosen);
}

// the pick sets Custom again through 0x80590's 0x49
void game::ShowCustomLayout(X86Context& ctx, uint32_t player, bool chosen) {
    if (!chosen) return;
    SendToPicker(ctx, player, kPausePicker, kSelectRow, kCustom, 0);
    kernel::CallGuest(ctx, kPauseDiagram, {player, kCustom});
}

void game::UpdateCustomLayout(X86Context& ctx, uint32_t player) {
    uint32_t picker = SendToPicker(ctx, player, kPausePicker, kPointer, 0, 0);
    if (!picker || (G<uint8_t>(picker + kFlags) & kHidden)) return;
    if (Stale(ctx, player, kPausePicker)) kernel::CallGuest(ctx, kPauseDiagram, {player, kCustom});
}

uint32_t game::CustomLayoutText(uint32_t id) {
    if (!g_lines_shown) return 0;
    for (uint32_t i = 0; i < kLineCount; i++)
        if (kLines[i].text == id) return Scratch() + i * kTextSize;
    return 0;
}

// a player's layout set, cdecl (player, layout): Custom is NightFire to the game
X86_EXTERN_FUNC(__imp__sub_0006CBC0);
X86_FUNC(sub_0006CBC0) {
    uint32_t player = kernel::R16(ctx.esp + 4);
    bool custom = R32(ctx.esp + 8) == kCustom;
    if (player < kPlayers) ChooseCustom(player, custom);
    if (custom) kernel::W32(ctx.esp + 8, kNightFire);
    __imp__sub_0006CBC0(ctx, base);
}

// the pause CONTROLS lines, cdecl (player, layout): Custom's are NightFire's with the binds' texts
X86_EXTERN_FUNC(__imp__sub_000749C0);
X86_FUNC(sub_000749C0) {
    if (R32(ctx.esp + 8) != kCustom) {
        __imp__sub_000749C0(ctx, base);
        return;
    }
    LatchLines(ctx);
    kernel::W32(ctx.esp + 8, kNightFire);
    g_lines_shown = true;
    __imp__sub_000749C0(ctx, base);
    g_lines_shown = false;
}

// the main menu controls page's labels, cdecl (player, layout); they keep a string id, so Custom's are given as text
X86_EXTERN_FUNC(__imp__sub_00073D10);
X86_FUNC(sub_00073D10) {
    uint32_t player = R32(ctx.esp + 4);
    bool custom = R32(ctx.esp + 8) == kCustom;
    if (custom) kernel::W32(ctx.esp + 8, kNightFire);
    __imp__sub_00073D10(ctx, base);
    if (!custom) return;
    LatchLines(ctx);
    for (uint32_t i = 0; i < kLineCount; i++)
        kernel::CallGuest(ctx, kSendToId, {player, kLines[i].label, kSetLabel, Scratch() + i * kTextSize, 0});
}

// the main menu controls page's handler, cdecl (player, page, page id, message, a, b)
X86_EXTERN_FUNC(__imp__sub_0008CE70);
X86_FUNC(sub_0008CE70) {
    uint32_t player = R32(ctx.esp + 4);
    uint32_t message = R32(ctx.esp + 16);
    __imp__sub_0008CE70(ctx, base);
    uint32_t result = ctx.eax;
    if (message == kOpened) AddLine(ctx, player, kMainPicker, Custom(0));
    if (message == kUpdate && Stale(ctx, player, kMainPicker)) {
        kernel::CallGuest(ctx, kMainDiagram, {player, kCustom});
    }
    ctx.eax = result;
}
