#pragma once

#include <cstdint>

// the main menu, from menu_mouse.cpp
namespace game {

// its button legend row in frame pixels (640x480), while the main menu or the title page takes input this frame
struct LegendRow {
    bool shown = false;
    float left = 0.0f;  // the page's own left edge, which moves when the page slides
    float top = 0.0f;
    float height = 0.0f;
    uint32_t text = 0;  // the legend's label and style strings in guest memory, 0 when it has none to measure
    uint32_t style = 0;
    float box_left = 0.0f;  // the legend widget's box in the page's units
    float box_width = 0.0f;
};
// each frame: latched by the menu's update and kept between the menus' runs, which are 60 a second
LegendRow TakeMainMenuLegend();
void ClearMainMenuLegend();  // before each run of the menus

bool QuitAsked();  // exit_menu.cpp: QUIT's question is up
bool PauseMenuUp();  // menu_mouse.cpp: the pause menu takes input, as of the menus' last run

}  // namespace game
