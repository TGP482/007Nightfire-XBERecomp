#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <map>
#include <string>

// the game's own settings, in the settings file through app::kGame.load and save
namespace game {

// what a player's controller port has plugged in: the keyboard and mouse, a controller, both or neither
struct PlayerPort {
    bool keyboard = false;
    int pad = -1;  // controller slot 0 to 3, -1 for none
};
inline constexpr int kPlayers = 4;  // the console's controller ports

struct Options {
    bool skip_intros = false;  // the EA GAMES and MGM INTERACTIVE logos at boot
    bool fps_counter = false;
    bool pause_on_focus_loss = true;  // the game and its sound stop while the window is in the background
    float mouse_sensitivity = 1.0f;
    bool invert_mouse = false;
    std::string monitor;  // its device name, \\.\DISPLAY2; empty the primary
    std::map<std::string, std::string> keys;  // action id to key names, absent is the default
    std::array<PlayerPort, kPlayers> players = {{{true, 0}, {false, 1}, {false, 2}, {false, 3}}};  // by port
};
Options& Opt();
bool ControllerPlugged(int slot);  // game_info.cpp
int KeyboardPort();  // game_info.cpp: the port with the keyboard, -1 for none
inline std::atomic<float> g_fps{0};  // from the host, for the game's own counter
void WaitWhileUnfocused();  // focus_pause.cpp: game thread, before the present
uint64_t GuestTicks();  // focus_pause.cpp: rdtsc ticks less the focus pauses, held while one lasts

}  // namespace game
