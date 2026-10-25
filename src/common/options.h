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
inline constexpr uint32_t kGameFov = 75;  // the game's 60 degrees vertical as horizontal degrees on a 4:3 frame

struct Options {
    bool skip_intros = false;  // the EA GAMES and MGM INTERACTIVE logos at boot
    uint32_t fov = kGameFov;  // first person play, horizontal degrees on a 4:3 frame
    uint32_t viewmodel_fov = kGameFov;  // the gun and arms
    bool fps_counter = false;
    uint32_t msaa = 0;  // samples a pixel of the screen, 0 off
    bool smaa = false;  // over the finished frame, HUD included; combines with msaa
    uint32_t ambient_occlusion = 0;  // from the scene's depth: 0 off, 1 SSAO, 2 GTAO
    bool hdr_output = true;  // HDR10 on a display with Windows HDR on
    uint32_t fmv_scale = 0;  // the full screen films on a wider screen: 0 fill it, 1 in a 4:3 frame
    bool pause_on_focus_loss = true;  // the game and its sound stop while the window is in the background
    float mouse_sensitivity = 1.0f;  // horizontal
    float mouse_vertical_sensitivity = 1.0f;
    // in manual aim or zoomed in, times the sensitivity above
    float aim_horizontal_sensitivity = 1.0f;
    float aim_vertical_sensitivity = 1.0f;
    bool invert_mouse = false;  // the Y axis
    bool invert_mouse_x = false;
    // the right stick's look, then in manual aim or zoomed in times it
    float pad_horizontal_sensitivity = 1.0f;
    float pad_vertical_sensitivity = 1.0f;
    float pad_aim_horizontal_sensitivity = 1.0f;
    float pad_aim_vertical_sensitivity = 1.0f;
    bool invert_pad_x = false;  // the game's own controls have the Y axis
    std::string monitor;  // its device name, \\.\DISPLAY2; empty the primary
    std::map<std::string, std::string> keys;  // action id to key names, absent is the default
    std::array<PlayerPort, kPlayers> players = {{{true, 0}, {false, 1}, {false, 2}, {false, 3}}};  // by port
};
Options& Opt();
bool ControllerPlugged(int slot);  // game_info.cpp
int KeyboardPort();  // game_info.cpp: the port with the keyboard, -1 for none
inline std::atomic<float> g_fps{0};  // from the host, for the game's own counter
inline std::atomic<bool> g_hdr_display{false};  // the renderer's: the window's display has Windows HDR on
inline std::atomic<bool> g_hud_hidden{false};  // the toggle HUD key's, not saved
void WaitWhileUnfocused();  // focus_pause.cpp: game thread, before the present
uint64_t GuestTicks();  // focus_pause.cpp: rdtsc ticks less the focus pauses, held while one lasts

}  // namespace game
