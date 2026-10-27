#include <cctype>
#include <string>

#include "common/mouse_keyboard.h"
#include "game/name_entry.h"
#include "xdk/hle.h"

// The codename keyboard: its keys (widgets of id 0x10000074, handler 0x8CC50) type into the name at 0x245200, 8
// letters at most. A key's sub tells it apart: 1000 deletes, 0x3E9 is a space, 0x3EA is done, any other is its label's
// letter. A typed character presses the key it labels as a click does (0x70FE0: the press sound, then 0x4B to the
// page), Backspace presses delete and Enter done.
namespace {

using kernel::G;
using kernel::R32;

// a page and its widgets, as menu_mouse.cpp walks them: +0 the next
constexpr uint32_t kPageWidgets = 0x94;
constexpr uint32_t kPageLink = 0xA0;  // dwords from a widget's start to its link
constexpr uint32_t kParent = 0x0C;
constexpr uint32_t kId = 0x18;
constexpr uint32_t kSub = 0x20;
constexpr int kMaxWidgets = 256;

constexpr uint32_t kName = 0x245200;
constexpr uint32_t kKeyId = 0x10000074;
constexpr uint32_t kDeleteKey = 1000;
constexpr uint32_t kSpaceKey = 0x3E9;
constexpr uint32_t kDoneKey = 0x3EA;

constexpr uint32_t kPostMessage = 0x72630;  // (widget, message, a, b)
constexpr uint32_t kLabel = 0x2F;           // widget message: its label's text
constexpr uint32_t kPressed = 0x4B;         // page message (key, 0)
constexpr uint32_t kPlaySound = 0x730F0;    // (sound)
constexpr uint32_t kPressSound = 0;

constexpr char kBackspace = '\b';
constexpr char kEnter = '\r';

uint32_t FirstWidget(uint32_t page) {
    uint32_t w = R32(page + kPageWidgets);
    return w ? w - R32(page + kPageLink) * 4 : 0;
}

bool HasKeys(uint32_t page) {
    int n = 0;
    for (uint32_t w = FirstWidget(page); w && n < kMaxWidgets; w = R32(w), n++)
        if (R32(w + kId) == kKeyId) return true;
    return false;
}

// the sub of the key a control character presses, 0 for a letter's
uint32_t SpecialKey(char c) {
    if (c == kBackspace) return kDeleteKey;
    if (c == kEnter) return kDoneKey;
    if (c == ' ') return kSpaceKey;
    return 0;
}

char LabelOf(X86Context& ctx, uint32_t key) {
    uint32_t sub = R32(key + kSub);
    if (sub == kDeleteKey || sub == kSpaceKey || sub == kDoneKey) return 0;
    uint32_t label = kernel::CallGuest(ctx, kPostMessage, {key, kLabel, 0, 0});
    return label ? G<char>(label) : 0;
}

bool SameLetter(char a, char b) { return std::toupper(uint8_t(a)) == std::toupper(uint8_t(b)); }

// the key the character presses: a label of its case first, else of the other
uint32_t FindKey(X86Context& ctx, uint32_t page, char c) {
    uint32_t special = SpecialKey(c);
    uint32_t other_case = 0;
    int n = 0;
    for (uint32_t w = FirstWidget(page); w && n < kMaxWidgets; w = R32(w), n++) {
        if (R32(w + kId) != kKeyId) continue;
        if (special) {
            if (R32(w + kSub) == special) return w;
            continue;
        }
        char label = LabelOf(ctx, w);
        if (label == c) return w;
        if (!other_case && label && SameLetter(label, c)) other_case = w;
    }
    return other_case;
}

void Press(X86Context& ctx, uint32_t key) {
    kernel::CallGuest(ctx, kPlaySound, {kPressSound});
    kernel::CallGuest(ctx, kPostMessage, {R32(key + kParent), kPressed, key, 0});
}

}  // namespace

void game::UpdateNameEntry(X86Context& ctx, uint32_t page) {
    if (!HasKeys(page)) return;
    input::SetTyping();
    for (char c : input::TakeTyped()) {
        // delete writes before an empty name (0x8CE4B)
        if (c == kBackspace && !G<char>(kName)) continue;
        if (uint32_t key = FindKey(ctx, page, c)) Press(ctx, key);
    }
}
