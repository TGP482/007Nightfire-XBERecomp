#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "core/input.h"

// the keyboard and mouse as player 1's actions in the game
namespace input {

constexpr size_t kActionValues = 40;  // the game's actions a player, see Keyboard

void StartMouse(void* hwnd);    // window thread, the game window is up
void OnRawInput(void* handle);  // window thread, WM_INPUT
void Settle();                  // the game lost the input: the cursor goes free, held keys wait for release
// the bound keys held now, as amounts to add to player 1's kActionValues action values once the game's controller
// layout has made them from the pad; in a menu the pause key backs out instead
void Keyboard(float* values, bool menu);
// the reload and action keys as of the last Keyboard, in the game's state bits (1 held, 4 pressed): the game has one
// action value for both, so the keys answer its reads apart instead
uint8_t ReloadKey();
uint8_t ActionKey();
// raw mouse counts since the last call, scaled by the settings; false when there are none. Asked every frame in play,
// which keeps the cursor in the window.
bool TakeMouse(float& dx, float& dy);
void SeenPad(const Pad& pad);  // a controller's report before the panel or keys change it
bool KeyboardActive();         // keyboard or mouse used last, not a controller

// the game's own menus
void SetMenu(bool menu);  // game thread, each frame: a menu is up
bool CursorShown();       // in a menu with the keyboard or mouse used last
// the cursor in the shown frame, 0 to 1 across and down; false while it is not shown
bool Cursor(float& x, float& y);
void MoveCursorDown(float y);  // to this height of the shown frame, 0 to 1, where it is across
bool LeftButton();  // held in the game window
// the keys bound to the action with this id are held and the game has the input
bool Held(std::string_view id);
// each image's own: its keys onto port 0's pad, while the settings panel is closed. default.xbe's keys join its
// actions instead (src\game\keyboard.cpp), Driving.xbe's are src\driving\keyboard.cpp.
void ImageKeys(Pad& pad);

// actions the keyboard and mouse can be bound to, key names comma separated
// letter, digit and symbol keys bind by position as the US key there; Binding shows the player's labels
struct Action {
    const char* id;
    const char* label;
    const char* defaults;
    const char* section = nullptr;  // a heading of its own on the panel page, after the rest
};
const Action* Actions(size_t* count);
std::string Binding(size_t action);
// slot 0 the key, 1 its alternate (the F10 panel's second box); vk 0 clears the slot. A key the other slot has moves.
void Bind(size_t action, int vk, int slot = 0);
void ResetBindings();
void LoadBindings();     // after the settings load
int PressedKey();        // a bindable key or mouse button held now, 0 for none
bool PanelKey(int vk);   // the settings panel is bound to this key and no key capture wants it

}  // namespace input
