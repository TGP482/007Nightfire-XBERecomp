#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>

struct X86Context;

// The game's own options from its main menu, shown in the panel and the pause menu where they fit. Each image gives
// its own: default.xbe's in src/game/game_options.cpp, Driving.xbe's in src/driving/game_options.cpp.
namespace game {

enum class OptionKind { OnOff, OffWhenSet, HoldToggle, Volume };  // OffWhenSet: the image keeps it as hidden
enum class OptionPlace { Gameplay, General, Controller };  // the section of the panel it joins

struct GameOption {
    const char* label;
    OptionKind kind;
    OptionPlace place;
    uint32_t address;  // a byte for the switches, a dword 0 to 100 for a volume
};

const GameOption* GameOptions(size_t* count);
inline std::atomic<bool> g_options_changed{false};  // a row changed one of them
// game thread, each frame: once one changed, default.xbe saves the profile with the panel shut, Driving.xbe passes it on
void SaveChangedOptions(X86Context& ctx);
// game thread, each frame: the game's widescreen follows the output's shape
void FollowOutputAspect();

}  // namespace game
