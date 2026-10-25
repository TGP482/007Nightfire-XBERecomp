#pragma once

// the main menu, from menu_mouse.cpp
namespace game {

// its button legend row in frame pixels (640x480), while the main menu takes input this frame
struct LegendRow {
    bool shown = false;
    float left = 0.0f;  // the page's own left edge, which moves when the page slides
    float top = 0.0f;
    float height = 0.0f;
};
LegendRow TakeMainMenuLegend();  // once a frame: latched by the menu's update, cleared by the take

}  // namespace game
