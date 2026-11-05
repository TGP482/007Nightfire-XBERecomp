#include "common/pad_binds.h"

#include <array>
#include <atomic>
#include <iterator>
#include <string_view>
#include <vector>

#include "app/app.h"
#include "common/options.h"

namespace input {

namespace {

// in PadButton order, each with its bit in the game's button word
struct Button {
    const char* name;
    uint32_t bit;
};
constexpr int kAnalogBit = 16;  // the analog buttons follow the pad's buttons word
constexpr Button kButtons[kPadButtonCount] = {
    {"A", 1u << 16},  {"B", 1u << 17},  {"X", 1u << 18},  {"Y", 1u << 19},
    {"RB", 1u << 20}, {"LB", 1u << 21}, {"LT", 1u << 22}, {"RT", 1u << 23},
    {"D-PAD UP", 0x01}, {"D-PAD DOWN", 0x02}, {"D-PAD LEFT", 0x04}, {"D-PAD RIGHT", 0x08},
    {"BACK", 0x20}, {"LS", 0x40}, {"RS", 0x80},
};
constexpr uint8_t kAnalogDown = 0x40;

// as the NightFire layout's controls page lists them
constexpr PadAction kActions[] = {
    {"crouch", "CROUCH", kPadA, 8, 0, 0xEB},
    {"next_weapon", "NEXT WEAPON", kPadB, 16, 0, 0xE9},
    {"reload_action", "RELOAD / ACTION", kPadX, 14, 0, 0xEA},
    {"jump", "JUMP", kPadY, 7, 0, 0xE8},
    {"alt_fire", "ALT FIRE", kPadLb, 12, 0, 0xE6},
    {"next_gadget", "NEXT GADGET", kPadRb, 11, 0, 0xE7},
    {"manual_aim", "MANUAL AIM", kPadLt, 19, 0, 0xDF},
    {"fire", "FIRE", kPadRt, 9, 0, 0xE5},
    {"cycle_weapon_back", "CYCLE WEAPON BACK", kPadLeft, 15, 0, 0xE2},
    {"cycle_weapon_forward", "CYCLE WEAPON FORWARD", kPadRight, 16, 0, 0xE2},
    {"cycle_gadget_zoom_in", "CYCLE GADGET / ZOOM IN", kPadUp, 11, -1, 0xE4},
    {"cycle_gadget_zoom_out", "CYCLE GADGET / ZOOM OUT", kPadDown, 10, 1, 0xE4},
    {"vision_mode", "VISION MODE", kPadBack, 13, 0, 0xDE},
};
constexpr size_t kCount = std::size(kActions);
std::array<std::atomic<int>, kCount> g_binds{};
std::atomic<uint32_t> g_version{0};

int ButtonNamed(std::string_view name) {
    for (int b = 0; b < kPadButtonCount; b++)
        if (name == kButtons[b].name) return b;
    return -1;
}

// as saved, the stock one when none is
int SavedButton(size_t i) {
    const auto& saved = game::Opt().pad_buttons;
    auto it = saved.find(kActions[i].id);
    return it == saved.end() ? kActions[i].button : ButtonNamed(it->second);
}

void SaveBinds() {
    auto& saved = game::Opt().pad_buttons;
    for (size_t i = 0; i < kCount; i++) {
        int b = g_binds[i];
        if (b == kActions[i].button) saved.erase(kActions[i].id);
        else saved[kActions[i].id] = b >= 0 ? kButtons[b].name : "";
    }
}

// the pad's buttons held, in the game's button word
uint32_t PadWord(const Pad& pad) {
    uint32_t word = pad.buttons;
    for (int i = 0; i <= kPadRt; i++)
        if (pad.analog[i] > kAnalogDown) word |= 1u << (kAnalogBit + i);
    return word;
}

}  // namespace

const PadAction* PadActions(size_t* count) {
    *count = kCount;
    return kActions;
}

uint32_t PadButtonBit(int button) { return button >= 0 && button < kPadButtonCount ? kButtons[button].bit : 0; }

int PadBinding(size_t action) { return action < kCount ? g_binds[action].load() : -1; }

std::string PadBindingName(size_t action) {
    int b = PadBinding(action);
    return b >= 0 ? kButtons[b].name : "";
}

void BindPad(size_t action, int button) {
    if (action >= kCount || button < 0 || button >= kPadButtonCount) return;
    int old = g_binds[action];
    for (auto& b : g_binds)
        if (b == button) b = old;
    g_binds[action] = button;
    g_version++;
    SaveBinds();
    app::Save();
}

void ResetPadBindings() {
    game::Opt().pad_buttons.clear();
    LoadPadBindings();
    app::Save();
}

// a hand edited file may give two actions one button: the later one takes its stock button if that is free, else none
void LoadPadBindings() {
    std::array<bool, kPadButtonCount> taken{};
    std::vector<size_t> later;
    for (size_t i = 0; i < kCount; i++) {
        int b = SavedButton(i);
        g_binds[i] = -1;
        if (b < 0) continue;
        if (taken[size_t(b)]) {
            later.push_back(i);
            continue;
        }
        g_binds[i] = b;
        taken[size_t(b)] = true;
    }
    for (size_t i : later) {
        int b = kActions[i].button;
        if (taken[size_t(b)]) continue;
        g_binds[i] = b;
        taken[size_t(b)] = true;
    }
    g_version++;
}

int PressedPadButton() {
    for (int slot = 0; slot < game::kPlayers; slot++) {
        Pad pad = game::ControllerPad(slot);
        if (!pad.connected) continue;
        uint32_t word = PadWord(pad);
        for (int b = 0; b < kPadButtonCount; b++)
            if (word & PadButtonBit(b)) return b;
    }
    return -1;
}

uint32_t PadBindsVersion() { return g_version; }

}  // namespace input
