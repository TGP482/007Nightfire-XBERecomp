#include <algorithm>
#include <cstdint>
#include <iterator>

#include "common/game_options.h"
#include "common/options.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

// Player 1's options come in the launch data default.xbe hands over (0x19A10, kept at 0x243B90), which goes back with
// them when the mission ends (0x130740) for default.xbe to read (0x19E40). The engine reads vibration, the aims and the
// weapon switch there as it goes; the HUD (0xD7DA0), subtitles (0x5A254) and volumes (0x5B47E) copy theirs at the
// mission's start, so a change is copied on to them. Crouch and flashing objects only go back. The subtitle box
// (0xE39B0) takes its lines while subtitles are off too and only does not draw them, so a line showing comes back.
namespace {

using game::GameOption;
using game::OptionKind;
using game::OptionPlace;
using kernel::G;
using kernel::R32;

// the launch data
constexpr uint32_t kCrouch = 0x243C18;
constexpr uint32_t kFlashingObjects = 0x243C38;
constexpr uint32_t kMusicBack = 0x243C78;  // the volumes default.xbe reads back
constexpr uint32_t kEffectsBack = 0x243C7C;
constexpr uint32_t kEffects = 0x2444F4;
constexpr uint32_t kMusic = 0x2444F8;
constexpr uint32_t kVibration = 0x244524;
constexpr uint32_t kHudHidden = 0x244528;
constexpr uint32_t kCrosshairsHidden = 0x24452C;
constexpr uint32_t kSubtitles = 0x2445BC;
constexpr uint32_t kAutoAim = 0x2445D0;
constexpr uint32_t kManualAim = 0x2445D4;
constexpr uint32_t kWeaponAutoSwitch = 0x2445D8;

constexpr GameOption kOptions[] = {
    {"VIBRATION", OptionKind::OnOff, OptionPlace::Controller, kVibration},
    {"AUTO AIM", OptionKind::OnOff, OptionPlace::Controller, kAutoAim},
    {"CROSSHAIRS", OptionKind::OffWhenSet, OptionPlace::Gameplay, kCrosshairsHidden},
    {"CROUCH", OptionKind::HoldToggle, OptionPlace::Gameplay, kCrouch},
    {"MANUAL AIM", OptionKind::HoldToggle, OptionPlace::Gameplay, kManualAim},
    {"WEAPON AUTO SWITCH", OptionKind::OnOff, OptionPlace::Gameplay, kWeaponAutoSwitch},
    {"FLASHING OBJECTS", OptionKind::OnOff, OptionPlace::Gameplay, kFlashingObjects},
    {"HUD ALWAYS ON", OptionKind::OffWhenSet, OptionPlace::Gameplay, kHudHidden},
    {"SUBTITLES", OptionKind::OnOff, OptionPlace::Gameplay, kSubtitles},
    {"MUSIC VOLUME", OptionKind::Volume, OptionPlace::General, kMusic},
    {"EFFECTS VOLUME", OptionKind::Volume, OptionPlace::General, kEffects},
};

// the copies
constexpr uint32_t kHud = 0x23F44C;  // pointer
constexpr uint32_t kHudHiddenCopy = 0x431;  // bytes, of the HUD
constexpr uint32_t kCrosshairsHiddenCopy = 0x432;
constexpr uint32_t kSubtitlesCopy = 0x1E476D;
// the sound groups, by name, whose level is 0 to 1 (0x121150 sets them)
constexpr uint32_t kMusicGroup = 0x1A27CC;
constexpr uint32_t kEffectsGroup = 0x1A0028;
constexpr uint32_t kMusicLevel = 0x1D8064;
constexpr uint32_t kEffectsLevel = 0x1D8068;
constexpr uint32_t kSpeechGroup = 0x18BC48;  // "Speech" under 1:Effects, the lines in the mission's .spe (0x121470)
constexpr uint32_t kFindGroup = 0x11D720;  // (name)
constexpr uint32_t kGroupLevel = 0x4;
constexpr uint32_t kGroupLastLevel = 0x8;
constexpr float kPercent = 0.01f;

void SetGroupLevel(X86Context& ctx, uint32_t group_name, float share) {
    uint32_t group = kernel::CallGuest(ctx, kFindGroup, {group_name});
    G<float>(group + kGroupLastLevel) = G<float>(group + kGroupLevel);
    G<float>(group + kGroupLevel) = share;
}

void ApplyVolume(X86Context& ctx, uint32_t group_name, uint32_t level, uint32_t volume) {
    float share = float(R32(volume)) * kPercent;
    G<float>(level) = share;
    SetGroupLevel(ctx, group_name, share);
}

// the voice volume the Speech group has; the mission's sound start sets the group to 1
constexpr uint32_t kNoVoice = UINT32_MAX;
uint32_t g_voice = kNoVoice;
bool g_sound_up = false;

void ApplyVoice(X86Context& ctx) {
    uint32_t voice = std::min<uint32_t>(game::Opt().voice_volume, 100);
    if (voice == g_voice) return;
    g_voice = voice;
    SetGroupLevel(ctx, kSpeechGroup, float(voice) * kPercent);
}

}  // namespace

const GameOption* game::GameOptions(size_t* count) {
    *count = std::size(kOptions);
    return kOptions;
}

// the subtitle tests of a sound's line (0x44760) and its timed lines (0x44970)
bool DrivingQueueSubtitle(X86Context&, uint8_t*) { return true; }

// at 0xE3A45, the subtitle box's draw
bool DrivingSubtitleHidden(X86Context&, uint8_t*) { return R32(kSubtitles) == 0; }

void game::SaveChangedOptions(X86Context& ctx) {
    if (g_sound_up) ApplyVoice(ctx);
    if (!g_options_changed) return;
    g_options_changed = false;
    uint32_t hud = R32(kHud);
    if (hud) {
        G<uint8_t>(hud + kHudHiddenCopy) = R32(kHudHidden) != 0;
        G<uint8_t>(hud + kCrosshairsHiddenCopy) = R32(kCrosshairsHidden) != 0;
    }
    G<uint8_t>(kSubtitlesCopy) = R32(kSubtitles) != 0;
    kernel::W32(kMusicBack, R32(kMusic));
    kernel::W32(kEffectsBack, R32(kEffects));
    ApplyVolume(ctx, kMusicGroup, kMusicLevel, kMusic);
    ApplyVolume(ctx, kEffectsGroup, kEffectsLevel, kEffects);
}

// a mission's sound start, cdecl: its groups made and at their levels
X86_EXTERN_FUNC(__imp__sub_00121470);
X86_FUNC(sub_00121470) {
    __imp__sub_00121470(ctx, base);
    g_sound_up = true;
    g_voice = kNoVoice;
    ApplyVoice(ctx);
}
