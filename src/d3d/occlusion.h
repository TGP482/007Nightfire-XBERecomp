#pragma once
#include <cstdint>

// Ambient occlusion goes to the renderer as a Clear, so it keeps its place among the draws on the render thread
namespace gpu {

constexpr uint32_t kClearOcclusion = 0x80000000;  // no D3DCLEAR flag: the rects hold the parameters below
constexpr uint32_t kOcclusionRects = 3;
// 1 / the projection's x and y scale, its x and y centre, view z = [4] / (depth - [5]), then the fog
// (slope, offset and amount over view z), 1 for GTAO, else SSAO, and view z per clip w unit of a w buffer
constexpr uint32_t kOcclusionParams = kOcclusionRects * 4;

}  // namespace gpu

namespace d3d {

void Occlusion(const float (&p)[gpu::kOcclusionParams]);  // over the scene drawn so far

}  // namespace d3d
