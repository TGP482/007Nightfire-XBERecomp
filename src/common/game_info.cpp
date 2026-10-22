#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/app.h"
#include "common/menu.h"
#include "core/input.h"

namespace game {

namespace {

constexpr intptr_t kKeyWasDown = 1 << 30;  // bit 30 of a key message's lparam: a repeat
constexpr uintptr_t kSysCommandMask = 0xFFF0;  // the low bits of a WM_SYSCOMMAND code are the system's

bool Take(intptr_t* result) {
    *result = 0;
    return true;
}

// F10 opens and closes the panel, Escape closes it
bool PanelKeyDown(uint32_t msg, uintptr_t key, intptr_t lp, intptr_t* result) {
    bool first_press = !(lp & kKeyWasDown);
    if (first_press && key == VK_F10) {
        app::ToggleMenu();
        return Take(result);
    }
    if (msg == WM_KEYDOWN && key == VK_ESCAPE && app::MenuOpen()) {
        app::ToggleMenu();
        return Take(result);
    }
    return false;
}

// F10 is a system key: the window menu it would open stalls the game window's messages
bool SwallowKeyMenu(uintptr_t command, intptr_t* result) {
    if ((command & kSysCommandMask) != SC_KEYMENU) return false;
    return Take(result);
}

bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            return PanelKeyDown(msg, wp, lp, result);
        case WM_SYSCOMMAND:
            return SwallowKeyMenu(wp, result);
    }
    return false;
}

// the game sees no input while the panel is open
void Pad(int, input::Pad& pad) {
    if (app::MenuOpen()) pad = input::Pad{.connected = pad.connected};
}

}  // namespace

}  // namespace game

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"007: NightFire - XBE Recomp", // window, splash and compiler
    .short_title = L"007 NightFire",        // message boxes
    .name = L"007Nightfire",                // window classes, crash dumps, handover event
    .log = "007nightfire",                  // log file prefix
    .settings = "settings.toml",            // settings file beside the exe
    .splash = L"splash",                    // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "007Nightfire"},  // each recompiled xbe and its exe, for title launches
               {"Driving.xbe", "007NightfireDriving"}},
    .options = {},                          // game settings kept in the settings file
    .load = nullptr,                        // reads more from the settings file's toml::table
    .save = nullptr,                        // writes more into it
    .init = nullptr,                        // after the settings load, before the window and boot
    .loaded = nullptr,                      // image in memory, before its entry point runs
    .window = nullptr,                      // the game window is up
    .message = game::Message,               // window messages first; true takes one
    .ui = app::DrawUi,                      // ImGui drawing each frame
    .pad = game::Pad,                       // changes a polled pad
    .rumble = nullptr,                      // changes a rumble before it is set
};
