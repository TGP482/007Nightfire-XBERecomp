#include <cmath>

#include "common/options.h"
#include "xdk/hle.h"

// Field of view: the view's draw (0x96EB0) takes the camera's angle (+0xB4, 33 unzoomed) twice as its horizontal
// degrees on a 4:3 frame, a quarter wider on a widescreen output, and hands it to the projection (0xE46D0). The car,
// turret and sniper cameras all draw through it; the setting widens it as default.xbe's own 75 degrees widen to it,
// so a zoom keeps its share.
namespace {

constexpr uint32_t kViewAngle = 0x10;  // degrees, on the draw's stack at 0x96F76
constexpr float kDegrees = 57.29578f;

float HalfTan(float degrees) { return std::tan(degrees / 2 / kDegrees); }

}  // namespace

// at 0x96F76, after the camera's sway is added and before the projection takes the angle
void DrivingFov(X86Context& ctx, uint8_t*) {
    uint32_t fov = game::Opt().fov;
    if (fov == game::kGameFov) return;
    float& angle = kernel::G<float>(ctx.esp + kViewAngle);
    float scale = HalfTan(float(fov)) / HalfTan(float(game::kGameFov));
    angle = 2 * std::atan(HalfTan(angle) * scale) * kDegrees;
}
