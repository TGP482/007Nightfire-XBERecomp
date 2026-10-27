#include <cmath>

#include "common/options.h"
#include "xdk/hle.h"

// Field of view: the view's draw (0x96EB0) takes the camera's angle (+0xB4, 33 unzoomed) twice as its horizontal
// degrees on a 4:3 frame, a quarter wider on a widescreen output, and hands it to the projection (0xE46D0). The car,
// turret and sniper cameras all draw through it; the setting widens it as default.xbe's own 75 degrees widen to it,
// so a zoom keeps its share. Lock on targets (0xCE2B0) are placed with the same angle, the setting's included, and
// the scene is culled (0x8A420) to it, so nothing at the sides of a wide view goes missing.
namespace {

using kernel::G;
using kernel::R32;

constexpr uint32_t kViewAngle = 0x10;  // degrees, on the draw's stack at 0x96F76
constexpr float kDegrees = 57.29578f;

// the display (0x1EBFF4)
constexpr uint32_t kDisplay = 0x1EBFF4;
constexpr uint32_t kWide = 0x4C;      // byte
constexpr uint32_t kFovScale = 0x58;  // of the camera's angle
constexpr uint32_t kWideFov = 0x18A834;      // 1.25
constexpr uint32_t kHudSqueeze = 0x189ED0;  // 0.75, the HUD's 4:3 width on a widescreen output

// the target projection's stack at 0xCE371
constexpr uint32_t kCameraAngle = 0xF4;
constexpr uint32_t kTargetHalfTan = 0x2C;

// the cull's stack at 0x8A490: the camera's angle, half the view's degrees as the draw takes it twice
constexpr uint32_t kCullHalfAngle = 0xC;

float HalfTan(float degrees) { return std::tan(degrees / 2 / kDegrees); }

float SettingDegrees(float degrees) {
    uint32_t fov = game::Opt().fov;
    if (fov == game::kGameFov) return degrees;
    float scale = HalfTan(float(fov)) / HalfTan(float(game::kGameFov));
    return 2 * std::atan(HalfTan(degrees) * scale) * kDegrees;
}

}  // namespace

// at 0x96F76, after the camera's sway is added and before the projection takes the angle
void DrivingFov(X86Context& ctx, uint8_t*) {
    float& angle = G<float>(ctx.esp + kViewAngle);
    angle = SettingDegrees(angle);
}

// at 0xCE371: a target took the 4:3 view's angle, close to the wide view's once the HUD squeezes it; it takes the
// view's own angle now, squeezed as the HUD is
void DrivingTargetProjection(X86Context& ctx, uint8_t*) {
    uint32_t display = R32(kDisplay);
    float degrees = 2 * G<float>(ctx.esp + kCameraAngle) * G<float>(display + kFovScale);
    float squeeze = 1.0f;
    if (G<uint8_t>(display + kWide)) {
        degrees *= G<float>(kWideFov);
        squeeze = G<float>(kHudSqueeze);
    }
    G<float>(ctx.esp + kTargetHalfTan) = HalfTan(SettingDegrees(degrees)) * squeeze;
}

// at 0x8A490, before the cull (0x8D440) takes the camera's angle
void DrivingCullFov(X86Context& ctx, uint8_t*) {
    float& half = G<float>(ctx.esp + kCullHalfAngle);
    half = SettingDegrees(2 * half) / 2;
}
