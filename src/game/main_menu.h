#pragma once

// the main menu, from menu_mouse.cpp
namespace game {

// its button legend row in frame pixels (640x480), while the main menu or the title page takes input this frame
struct LegendRow {
    bool shown = false;
    float left = 0.0f;  // the page's own left edge, which moves when the page slides
    float top = 0.0f;
    float height = 0.0f;
};
// each frame: latched by the menu's update and kept between the menus' runs, which are 60 a second
LegendRow TakeMainMenuLegend();
void ClearMainMenuLegend();  // before each run of the menus

bool QuitAsked();  // exit_menu.cpp: QUIT's question is up
bool PauseMenuUp();  // menu_mouse.cpp: the pause menu takes input, as of the menus' last run

}  // namespace game
