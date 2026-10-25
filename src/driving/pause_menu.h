#pragma once

// Driving.xbe's pause menu, from options_menu.cpp, for its keys in keyboard.cpp
namespace driving {

bool PauseShown();    // it drew lately
bool ClickAccepts();  // the left button went down on a line and is still held, so it presses A

}  // namespace driving
