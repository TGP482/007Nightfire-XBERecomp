#pragma once
#include <string>
#include <string_view>

// Text the recomp adds to the menus, in the game's language (translations.cpp)
namespace game {

// each phrase with a translation in the language this run started with, the rest as it is; all capitals when the
// English is. As Latin-1 for the game's font, and as UTF-8 for the settings panel.
std::string GameText(std::string_view english);
std::string PanelText(std::string_view english);

}  // namespace game
