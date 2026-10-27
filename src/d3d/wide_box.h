#pragma once
#include <cstdint>

// 2D a title lays out for a 16:9 screen goes to the renderer between Clear marks, so they keep their place among the
// draws on the render thread
namespace gpu {

constexpr uint32_t kClearWideBox = 0x20000000;  // no rects, stencil 1 or 0: the draws that follow fit the 16:9 box

}  // namespace gpu

namespace d3d {

void BoxToWide(bool on);  // the screen's draws between on and off show in the largest 16:9 box in its middle

}  // namespace d3d
