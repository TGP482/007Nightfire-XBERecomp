#include <algorithm>
#include <cctype>
#include <cstring>
#include <string>
#include <string_view>

#include "common/mouse_keyboard.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

// PC prompts: while the keyboard and mouse drive the menus, the button legends name the keys, key first, as
// "(ENTER) Select". The game draws a button as "~" and a code inside its text (0x69C50 lays out and measures text),
// so the codes are swapped for key names there. The controller layout in the pause menu keeps its buttons: its
// lines hold one code each, legends several apart or "Press ~".
namespace {

using kernel::G;

// 0x69C50's stack: the text, then the number of characters to lay out, below 0 for all
constexpr uint32_t kTextArg = 0x14;
constexpr uint32_t kCountArg = 0x20;
constexpr uint32_t kMaxText = 512;
constexpr uint32_t kScratchSize = kMaxText * 2;

constexpr char kGlyph = '~';

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

bool Legend(std::string_view text) { return text.rfind("Press ~", 0) == 0 || text.find("  ~") != text.npos; }

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

}  // namespace

X86_EXTERN_FUNC(__imp__sub_00069C50);
X86_FUNC(sub_00069C50) {
    std::string_view text = GuestText(kernel::R32(ctx.esp + kTextArg));
    bool keys_shown = input::CursorShown() && text.find(kGlyph) != text.npos && Legend(text);
    if (!keys_shown) return __imp__sub_00069C50(ctx, base);
    static const uint32_t scratch = mem::AllocSystem(kScratchSize);
    Rewritten keys = Rewrite(text, int(kernel::R32(ctx.esp + kCountArg)));
    size_t length = std::min<size_t>(keys.text.size(), kScratchSize - 1);
    std::memcpy(kernel::HostPtr(scratch), keys.text.data(), length);
    G<char>(scratch + uint32_t(length)) = 0;
    kernel::W32(ctx.esp + kTextArg, scratch);
    if (keys.count >= 0) kernel::W32(ctx.esp + kCountArg, uint32_t(keys.count));
    __imp__sub_00069C50(ctx, base);
}
