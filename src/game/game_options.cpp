#include <array>
#include <iterator>

#include "common/game_options.h"
#include "common/menu.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

// Player 1's options, as the main menu's Advanced Options (0x8D0F0) and AV Options (0x8D7A0) pages set them. The
// music update applies its volume each frame (0xC9030), the effects read theirs as they play. Subtitles only stop new
// lines (0xD1D70), so the lines queue while they are off too and are hidden instead, to show again when turned on.
namespace {

using game::GameOption;
using game::OptionKind;
using game::OptionPlace;

constexpr GameOption kOptions[] = {
    {"VIBRATION", OptionKind::OnOff, OptionPlace::Controller, 0x1FE6D9},
    {"AUTO AIM", OptionKind::OnOff, OptionPlace::Controller, 0x1FE6D1},
    {"CROSSHAIRS", OptionKind::OnOff, OptionPlace::Gameplay, 0x1FE6D8},
    {"CROUCH", OptionKind::HoldToggle, OptionPlace::Gameplay, 0x1FE6D4},
    {"MANUAL AIM", OptionKind::HoldToggle, OptionPlace::Gameplay, 0x1FE6D3},
    {"WEAPON AUTO SWITCH", OptionKind::OnOff, OptionPlace::Gameplay, 0x1FE6DA},
    {"FLASHING OBJECTS", OptionKind::OnOff, OptionPlace::Gameplay, 0x1FE6DC},
    {"HUD ALWAYS ON", OptionKind::OnOff, OptionPlace::Gameplay, 0x1FE6DB},
    {"SUBTITLES", OptionKind::OnOff, OptionPlace::Gameplay, 0x1F6614},
    {"MUSIC VOLUME", OptionKind::Volume, OptionPlace::General, 0x182A88},
    {"EFFECTS VOLUME", OptionKind::Volume, OptionPlace::General, 0x182A98},
};

}  // namespace

const GameOption* game::GameOptions(size_t* count) {
    *count = std::size(kOptions);
    return kOptions;
}

namespace {

// The profile save as 0x757D0 runs it for the menus: 0x6F880 (name, buffer, chunk mask) packs every chunk, the options
// among them, and writes the save at once; 0x6EB20 then clears the save and load states. The loaded profile's entry
// in the profile table takes the options first, as the main menu's own save does (0x8C7F0).
constexpr uint32_t kProfileName = 0x17D558;
constexpr uint32_t kProfileSlot = 0x17D550;  // 0 or 1 while a profile is loaded
constexpr uint32_t kProfileSlots = 2;
constexpr uint32_t kSaveAllowed = 0x17D57D;  // 0 once the player goes on without saving
constexpr uint32_t kMenuSaveTask = 0x17D540;  // the menus' own save or load
constexpr uint32_t kMenuSaveRunning = 0x224F74;
constexpr uint32_t kLoadState = 0x21D790;
constexpr uint32_t kSaveState = 0x21D794;
constexpr uint32_t kCopyToProfile = 0x7F100;  // (player, profile slot)
constexpr uint32_t kSaveProfile = 0x6F880;
constexpr uint32_t kClearSaveStates = 0x6EB20;
constexpr uint32_t kPlayer1 = 0;
constexpr uint32_t kAllChunks = 0xFF;
constexpr uint32_t kFirstBuffer = 0;

bool CanSave() {
    using kernel::G;
    bool loaded = kernel::R32(kProfileSlot) < kProfileSlots && G<char>(kProfileName) != 0;
    bool idle = !kernel::R32(kMenuSaveTask) && !G<uint8_t>(kMenuSaveRunning) && !G<uint8_t>(kLoadState) &&
                !G<uint8_t>(kSaveState);
    return loaded && idle && G<uint8_t>(kSaveAllowed);
}

// the subtitle lines' text items, 0xD2360 the first and 0xD2400 the other two; +0x38 0xFF hides one
constexpr uint32_t kSubtitles = 0x1F6614;
constexpr uint32_t kSubtitleItems[] = {0x29AE24, 0x29AE28, 0x29AE2C};
constexpr uint32_t kItemShown = 0x38;
constexpr uint8_t kHidden = 0xFF;

std::array<uint8_t, std::size(kSubtitleItems)> g_shown{};  // as the game left them, while hidden
bool g_subtitles_hidden = false;

// null before the items are made
uint8_t* ShownByte(size_t line) {
    uint32_t item = kernel::R32(kSubtitleItems[line]);
    return item ? &kernel::G<uint8_t>(item + kItemShown) : nullptr;
}

void HideSubtitles() {
    for (size_t i = 0; i < g_shown.size(); i++) {
        uint8_t* shown = ShownByte(i);
        if (!shown) continue;
        g_shown[i] = *shown;
        *shown = kHidden;
    }
}

void RestoreSubtitles() {
    for (size_t i = 0; i < g_shown.size(); i++)
        if (uint8_t* shown = ShownByte(i)) *shown = g_shown[i];
}

// the lines show and time out as the game runs them, and stay hidden while subtitles are off
void ShowSubtitles(X86Context& ctx, uint8_t* base, void (*show)(X86Context&, uint8_t*)) {
    bool on = kernel::R32(kSubtitles) != 0;
    if (on && g_subtitles_hidden) RestoreSubtitles();
    show(ctx, base);
    if (!on) HideSubtitles();
    g_subtitles_hidden = !on;
}

}  // namespace

// the subtitle tests of 0xD1D70 and of a sound's line (0xCBD10)
bool QueueSubtitle(X86Context&, uint8_t*) { return true; }

X86_EXTERN_FUNC(__imp__sub_000D2360);
X86_FUNC(sub_000D2360) { ShowSubtitles(ctx, base, __imp__sub_000D2360); }

X86_EXTERN_FUNC(__imp__sub_000D2400);
X86_FUNC(sub_000D2400) { ShowSubtitles(ctx, base, __imp__sub_000D2400); }

void game::SaveChangedOptions(X86Context& ctx) {
    if (!g_options_changed || app::MenuOpen() || !CanSave()) return;
    g_options_changed = false;
    kernel::CallGuest(ctx, kCopyToProfile, {kPlayer1, kernel::R32(kProfileSlot)});
    kernel::CallGuest(ctx, kSaveProfile, {kProfileName, kFirstBuffer, kAllChunks});
    kernel::CallGuest(ctx, kClearSaveStates, {});
}
