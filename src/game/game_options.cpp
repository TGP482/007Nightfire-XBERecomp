#include <iterator>

#include "common/game_options.h"
#include "common/menu.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

// Player 1's options, as the main menu's Advanced Options (0x8D0F0) and AV Options (0x8D7A0) pages set them. The
// music update applies its volume each frame (0xC9030), the effects read theirs as they play. Subtitles only stop new
// lines (0xD1D70), so the ones showing are ended when they go off.
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

// the messages 0xD1D70 queues, +4 the next; a subtitle's kind is 4 (or 5, which 0xD1D70 makes of some 4s), and it
// ends once its time is spent, as the game ends messages itself (0xD1E4E)
constexpr uint32_t kSubtitles = 0x1F6614;
constexpr uint32_t kMessages = 0x29AC04;
constexpr uint32_t kNextMessage = 0x4;
constexpr uint32_t kMessageTime = 0x18;  // float, 60 fps frames left
constexpr uint32_t kMessageKind = 0x1C;  // byte
constexpr uint8_t kSubtitleKinds[] = {4, 5};
constexpr float kSpent = -1.0f;

bool Subtitle(uint32_t message) {
    uint8_t kind = kernel::G<uint8_t>(message + kMessageKind);
    return kind == kSubtitleKinds[0] || kind == kSubtitleKinds[1];
}

void EndSubtitles() {
    for (uint32_t m = kernel::R32(kMessages); m; m = kernel::R32(m + kNextMessage))
        if (Subtitle(m)) kernel::G<float>(m + kMessageTime) = kSpent;
}

void UpdateSubtitles() {
    static bool shown = true;
    bool on = kernel::R32(kSubtitles) != 0;
    if (shown && !on) EndSubtitles();
    shown = on;
}

}  // namespace

void game::SaveChangedOptions(X86Context& ctx) {
    UpdateSubtitles();
    if (!g_options_changed || app::MenuOpen() || !CanSave()) return;
    g_options_changed = false;
    kernel::CallGuest(ctx, kCopyToProfile, {kPlayer1, kernel::R32(kProfileSlot)});
    kernel::CallGuest(ctx, kSaveProfile, {kProfileName, kFirstBuffer, kAllChunks});
    kernel::CallGuest(ctx, kClearSaveStates, {});
}
