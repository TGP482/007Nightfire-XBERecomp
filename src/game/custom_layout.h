#pragma once
#include <cstdint>

#include "xdk/hle.h"

// the Custom controller layout, from custom_layout.cpp
namespace game {

void ApplyCustomLayouts(uint32_t players);  // keyboard.cpp: 0xDE720 made the first players' action values
// options_menu.cpp: the pause's picker picks the player's layout as it opens and as CONTROLS takes the focus, which
// sets Custom's NightFire; chosen is Custom as it was before
bool CustomLayoutChosen(uint32_t player);
void AddCustomLayout(X86Context& ctx, uint32_t player, bool chosen);   // the pause opened
void ShowCustomLayout(X86Context& ctx, uint32_t player, bool chosen);  // CONTROLS took the focus
void UpdateCustomLayout(X86Context& ctx, uint32_t player);  // the pause's update: Custom's lines follow the binds
uint32_t CustomLayoutText(uint32_t id);  // pc_wording.cpp: a controls line's text, 0 for the game's own

}  // namespace game
