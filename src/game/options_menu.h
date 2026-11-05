#pragma once
#include <cstdint>

#include "xdk/hle.h"

// the pause menu's RECOMP tab, from options_menu.cpp; the menu loader's hooks in exit_menu.cpp call these
namespace game {

void LatchScoreTab(uint32_t record, uint32_t def, uint32_t widget);  // a widget was made from its definition
void LatchScoreMessage(uint32_t widget, uint32_t message, uint32_t a, uint32_t b);  // a widget was sent a message
void EnsureRecompTab(X86Context& ctx, uint32_t record, uint32_t finished);  // the loader is done with a widget

}  // namespace game
