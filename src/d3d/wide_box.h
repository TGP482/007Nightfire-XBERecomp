#pragma once
#include <cstdint>

// 2D a title lays out for a 16:9 screen goes to the renderer between Clear marks, so they keep their place among the
// draws on the render thread
namespace gpu {

constexpr uint32_t kClearWideBox = 0x20000000;  // no rects, stencil 1 or 0: the draws that follow fit the 16:9 box
constexpr float kBoxMiddle = 0.5f;  // the box's side, a float in the colour: 0 left, 1 right of the spare width

}  // namespace gpu

namespace d3d {

// the screen's draws between on and off show in the largest 16:9 box, at side of the screen's spare width
void BoxToWide(bool on, float side = gpu::kBoxMiddle);

}  // namespace d3d
