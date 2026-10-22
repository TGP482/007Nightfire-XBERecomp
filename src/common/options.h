#pragma once
#include <map>
#include <string>

// the game's own settings, in the settings file through app::kGame.load and save
namespace game {

struct Options {
    bool skip_intros = false;  // the EA GAMES and MGM INTERACTIVE logos at boot
    float mouse_sensitivity = 1.0f;
    bool invert_mouse = false;
    std::map<std::string, std::string> keys;  // action id to key names, absent is the default
};
Options& Opt();

}  // namespace game
