#include <algorithm>
#include <cctype>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

#include "common/mouse_keyboard.h"
#include "core/log.h"
#include "game/main_menu.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

// PC prompts: while the keyboard and mouse drive the menus, the button legends name the keys, key first, as
// "(ENTER) Select". The game draws a button as "~" and a code inside its text (0x69C50 lays out and measures text),
// so the codes are swapped for key names there. The controller layout in the pause menu keeps its buttons: its
// lines hold one code each, legends several apart or "Press ~". Elsewhere in the menus a legend can be one button
// first, as the stats screen's "~B Back". The title screen's line is a legend too.
namespace {

using kernel::G;

// 0x69C50's stack: the text, then the number of characters to lay out, below 0 for all
constexpr uint32_t kTextArg = 0x14;
constexpr uint32_t kCountArg = 0x20;
constexpr uint32_t kMaxText = 512;
constexpr uint32_t kScratchSize = kMaxText * 2;

constexpr char kGlyph = '~';

constexpr uint32_t kStringText = 0x6D460;  // (string id, 0, 0): its text
constexpr uint32_t kTitleLine = 0x2FB;     // the title screen's line, set by its page handler 0x854C0
constexpr char kStartWord[] = "START";
constexpr char kStartButton[] = "~A";  // the page's full screen text 0x10000196 takes A: ENTER

// the popup QUIT asks with takes B as no, which its legend leaves out
constexpr char kQuitLegend[] = "Press ~A";
constexpr char kQuitCancel[] = " or ~B to cancel";

std::string Upper(std::string name) {
    for (char& c : name) c = char(std::toupper(uint8_t(c)));
    return name == "ESCAPE" ? "ESC" : name;
}

// the first key bound to the action, as the F10 panel names it; empty when none
std::string Bound(std::string_view id) {
    size_t count = 0;
    const input::Action* actions = input::Actions(&count);
    for (size_t i = 0; i < count; i++) {
        if (actions[i].id != id) continue;
        std::string keys = input::Binding(i);
        return Upper(keys.substr(0, keys.find(',')));
    }
    return {};
}

// the key doing a button's work in the menus (src/common/mouse_keyboard.cpp); empty keeps the button
std::string KeyFor(char code) {
    switch (code) {
        case 'A':
            return "ENTER";
        case 'B':
            return "ESC";
        case 'V':
            return "UP/DOWN";
        case 'H':
            return "LEFT/RIGHT";
        case 'W':
            return "ARROWS";
        case 'X':
            return Bound("menu_x");
        case 'Y':
            return Bound("menu_y");
        case 'S':
            return Bound("pause");
        default:
            return {};
    }
}

bool Legend(std::string_view text) {
    if (text.rfind("Press ~", 0) == 0 || text.find("  ~") != text.npos) return true;
    bool one_button = text.size() > 3 && text[0] == kGlyph && text[2] == ' ' && text.find(kGlyph, 1) == text.npos;
    return one_button && !game::PauseMenuUp();
}

struct Rewritten {
    std::string text;
    int count;  // the characters laid out of it, as many as the original count covered
};

Rewritten Rewrite(std::string_view text, int count) {
    Rewritten out{{}, -1};
    for (size_t i = 0; i < text.size(); i++) {
        if (count >= 0 && int(i) == count) out.count = int(out.text.size());
        std::string key = text[i] == kGlyph && i + 1 < text.size() ? KeyFor(text[i + 1]) : std::string();
        if (key.empty()) {
            out.text += text[i];
            continue;
        }
        out.text += "(" + key + ")";
        i++;
    }
    if (count >= 0 && out.count < 0) out.count = int(out.text.size());
    return out;
}

std::string_view GuestText(uint32_t at) {
    if (!at) return {};
    const char* text = static_cast<const char*>(kernel::HostPtr(at));
    return {text, strnlen(text, kMaxText)};
}

size_t FindStartWord(std::string_view text) {
    auto same = [](char a, char b) { return std::toupper(uint8_t(a)) == b; };
    auto at = std::search(text.begin(), text.end(), std::begin(kStartWord), std::end(kStartWord) - 1, same);
    return at == text.end() ? text.npos : size_t(at - text.begin());
}

// the title line, or the part of it shown so far, with START as the A button
bool TitleLine(X86Context& ctx, std::string_view text, std::string& line, int& count) {
    if (text.empty()) return false;
    std::string_view stock = GuestText(kernel::CallGuest(ctx, kStringText, {kTitleLine, 0, 0}));
    if (!stock.starts_with(text)) return false;
    static bool logged = false;
    if (!logged) LOGR("prompts: title line \"%.*s\"", int(stock.size()), stock.data());
    logged = true;
    line = std::string(text);
    size_t word = FindStartWord(line);
    if (word == line.npos || line.find(kGlyph) != line.npos) return true;
    line.replace(word, sizeof(kStartWord) - 1, kStartButton);
    int shrink = int(sizeof(kStartWord) - sizeof(kStartButton));
    if (count > int(word)) count = std::max(int(word), count - shrink);
    return true;
}

// QUIT's legend with B to cancel, or empty
std::string QuitLegend(std::string_view text, int& count) {
    bool legend = text.starts_with(kQuitLegend) && text.find("~B") == text.npos;
    if (!game::QuitAsked() || !legend) return {};
    if (count >= int(text.size())) count = int(text.size() + sizeof(kQuitCancel) - 1);
    return std::string(text) + kQuitCancel;
}

void SetText(X86Context& ctx, uint32_t scratch, const Rewritten& keys) {
    size_t length = std::min<size_t>(keys.text.size(), kScratchSize - 1);
    std::memcpy(kernel::HostPtr(scratch), keys.text.data(), length);
    G<char>(scratch + uint32_t(length)) = 0;
    kernel::W32(ctx.esp + kTextArg, scratch);
    if (keys.count >= 0) kernel::W32(ctx.esp + kCountArg, uint32_t(keys.count));
}

}  // namespace

X86_EXTERN_FUNC(__imp__sub_00069C50);
X86_FUNC(sub_00069C50) {
    std::string_view text = GuestText(kernel::R32(ctx.esp + kTextArg));
    int count = int(kernel::R32(ctx.esp + kCountArg));
    std::string title;
    bool title_keys = input::KeyboardActive() && TitleLine(ctx, text, title, count);
    if (title_keys) text = title;
    std::string quit = QuitLegend(text, count);
    if (!quit.empty()) text = quit;
    bool legend = title_keys || (input::CursorShown() && Legend(text));
    bool keys_shown = legend && text.find(kGlyph) != text.npos;
    if (!keys_shown && quit.empty()) return __imp__sub_00069C50(ctx, base);
    static const uint32_t scratch = mem::AllocSystem(kScratchSize);
    SetText(ctx, scratch, keys_shown ? Rewrite(text, count) : Rewritten{quit, count});
    __imp__sub_00069C50(ctx, base);
}
