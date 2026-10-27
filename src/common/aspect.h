#pragma once
#include <algorithm>

// The output's shape. One narrower than the console's 4:3 shows the 4:3 frame in its middle, as a 4:3 TV would.
namespace game {

constexpr float kConsoleAspect = 4.0f / 3.0f;
constexpr float kWideAspect = 16.0f / 9.0f;  // the console's widescreen TV

float OutputAspect();  // width over height, kConsoleAspect at the least
inline bool Wide() { return OutputAspect() > kConsoleAspect; }
// a 4:3 picture's share of the output's width: 1 on a 4:3 output, 0.75 on a 16:9 one
inline float NarrowShare() { return kConsoleAspect / OutputAspect(); }
// the largest 16:9 box in the middle of the output, as shares of its width and height
inline float WideBoxWidth() { return std::min(1.0f, kWideAspect / OutputAspect()); }
inline float WideBoxHeight() { return std::min(1.0f, OutputAspect() / kWideAspect); }

}  // namespace game
