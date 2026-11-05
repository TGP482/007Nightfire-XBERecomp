#pragma once
#include <cstddef>
#include <cstdint>
#include <string>

#include "core/input.h"

// the controller's buttons rebound, for the game's Custom controller layout
namespace input {

// RB and LB are the console's black and white; START stays the pause
enum PadButton : int {
    kPadA,
    kPadB,
    kPadX,
    kPadY,
    kPadRb,
    kPadLb,
    kPadLt,
    kPadRt,
    kPadUp,
    kPadDown,
    kPadLeft,
    kPadRight,
    kPadBack,
    kPadLs,
    kPadRs,
    kPadButtonCount
};

// an action of the NightFire layout, button its stock one; value and zoom are what it presses in default.xbe (0xDE720)
struct PadAction {
    const char* id;
    const char* label;
    int button;
    uint32_t value;
    int zoom;  // added to the zoom value while held, down positive
    uint32_t text;  // the game's name for it on the controls pages
};
const PadAction* PadActions(size_t* count);
uint32_t PadButtonBit(int button);  // in the game's button word: the pad's buttons, the analog ones from bit 16
int PadBinding(size_t action);      // its button, -1 for none
std::string PadBindingName(size_t action);  // empty for none
// the action that had the button takes this one's old one: buttons only change places
void BindPad(size_t action, int button);
void ResetPadBindings();
void LoadPadBindings();  // after the settings load
int PressedPadButton();  // a bindable button held on any controller, -1 for none
uint32_t PadBindsVersion();  // counts the changes, for what shows the binds

}  // namespace input

namespace game {

input::Pad ControllerPad(int slot);  // game_info.cpp: the slot as polled, before the player ports

}  // namespace game
