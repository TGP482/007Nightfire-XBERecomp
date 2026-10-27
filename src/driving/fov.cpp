#include <algorithm>
#include <cmath>
#include <iterator>

#include "common/aspect.h"
#include "common/options.h"
#include "xdk/hle.h"

// Field of view: the view's draw (0x96EB0) takes the camera's angle (+0xB4, 33 unzoomed) twice as its horizontal
// degrees on a 4:3 frame, a quarter wider on a 16:9 output and wider again as the output is, and hands it to the
// projection (0xE46D0). The car, turret and sniper cameras all draw through it; the setting widens it as
// default.xbe's own 75 degrees widen to it.
// Lock on targets (0xCE2B0) are placed with the same angle, the setting's included, and the scene is culled (0x8A420)
// to it, so nothing at the sides of a wide view goes missing. The in-game cutscenes' cameras keep the game's angle.
// A zoom (the turret's and the sniper's aim) eases the angle down (0x97710) and the widening fades out with it, so
// the zoomed view is the game's own.
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
constexpr uint32_t kTargetCameraAngle = 0xF4;
constexpr uint32_t kTargetHalfTan = 0x2C;

// the cull's stack at 0x8A490: the camera's angle, half the view's degrees as the draw takes it twice
constexpr uint32_t kCullHalfAngle = 0xC;

// The player camera (0x877E0, in esi) runs the update of its type, read from the level's camera file (0x786AD): a
// table of 0x18 byte types, the update at +0x14
constexpr uint32_t kCameraTypes = 0x1EC368;
constexpr uint32_t kCameraTypeSize = 0x18;
constexpr uint32_t kTypeUpdate = 0x14;
constexpr uint32_t kCameraType = 0x130;
// the cutscenes' types, by the camera file's names
constexpr uint32_t kCutsceneCameras[] = {
    0x82400,  // WorldAnim
    0x82840,  // RelativeAnim
    0x80CF0,  // AIPathAnim
    0x84A60,  // Spline
    0x886F0,  // Ellipse
    0x85160,  // Fixed
};

// the player, and its turret's camera while it is in one
constexpr uint32_t kPlayer = 0x239220;
constexpr uint32_t kPlayerCamera = 0x47C;
constexpr uint32_t kCameraAngle = 0xB4;  // of a camera and of the player's eyes
constexpr uint32_t kViewCamera = 0x4;    // of the draw's view, in esi
constexpr float kUnzoomed = 33.0f;

bool g_cutscene = false;  // the player camera's type is a cutscene's
float g_zoom = kUnzoomed;  // the angle the player's zoom eases to, kUnzoomed while it does not zoom

float HalfTan(float degrees) { return std::tan(degrees / 2 / kDegrees); }

// how much of the setting a camera at this angle takes: all of it unzoomed, none at the zoom's angle
float Widening(float camera_angle) {
    if (g_zoom >= kUnzoomed) return 1.0f;
    return std::clamp((camera_angle - g_zoom) / (kUnzoomed - g_zoom), 0.0f, 1.0f);
}

// a 16:9 output's degrees on the output's own shape
float OutputDegrees(float degrees) {
    if (!game::Wide()) return degrees;
    return 2 * std::atan(HalfTan(degrees) * game::OutputAspect() / game::kWideAspect) * kDegrees;
}

float SettingDegrees(float degrees, float camera_angle) {
    uint32_t fov = game::Opt().fov;
    if (fov == game::kGameFov || g_cutscene) return degrees;
    float scale = HalfTan(float(fov)) / HalfTan(float(game::kGameFov));
    scale = 1.0f + (scale - 1.0f) * Widening(camera_angle);
    return 2 * std::atan(HalfTan(degrees) * scale) * kDegrees;
}

}  // namespace

// at 0x96F76, after the camera's sway is added and before the projection takes the angle
void DrivingFov(X86Context& ctx, uint8_t*) {
    float& angle = G<float>(ctx.esp + kViewAngle);
    float camera_angle = G<float>(R32(ctx.esi + kViewCamera) + kCameraAngle);
    angle = SettingDegrees(OutputDegrees(angle), camera_angle);
}

// at 0xCE371: a target took the 4:3 view's angle, close to the wide view's once the HUD squeezes it; it takes the
// view's own angle now, squeezed as the HUD is in its 16:9 box
void DrivingTargetProjection(X86Context& ctx, uint8_t*) {
    uint32_t display = R32(kDisplay);
    float camera_angle = G<float>(ctx.esp + kTargetCameraAngle);
    float degrees = 2 * camera_angle * G<float>(display + kFovScale);
    float squeeze = 1.0f;
    if (G<uint8_t>(display + kWide)) {
        degrees *= G<float>(kWideFov);
        squeeze = G<float>(kHudSqueeze) / game::WideBoxWidth();
    }
    float shown = SettingDegrees(OutputDegrees(degrees), camera_angle);
    G<float>(ctx.esp + kTargetHalfTan) = HalfTan(shown) * squeeze;
}

// at 0x8782E, before the player camera runs its type's update
void DrivingCameraType(X86Context& ctx, uint8_t*) {
    uint32_t type = R32(kCameraTypes) + R32(ctx.esi + kCameraType) * kCameraTypeSize;
    uint32_t update = R32(type + kTypeUpdate);
    const uint32_t* end = std::end(kCutsceneCameras);
    g_cutscene = std::find(std::begin(kCutsceneCameras), end, update) != end;
}

// at 0x8A490, before the cull (0x8D440) takes the camera's angle
void DrivingCullFov(X86Context& ctx, uint8_t*) {
    float& half = G<float>(ctx.esp + kCullHalfAngle);
    half = SettingDegrees(OutputDegrees(2 * half), half) / 2;
}

// a camera's angle eased towards a target (target, step), thiscall: the player's zoom in and out
X86_EXTERN_FUNC(__imp__sub_00097710);
X86_FUNC(sub_00097710) {
    uint32_t camera = ctx.ecx;
    float target = G<float>(ctx.esp + 4);
    uint32_t player = R32(kPlayer);
    bool own = player && (camera == player || camera == R32(player + kPlayerCamera));
    if (own && target < kUnzoomed) g_zoom = target;
    __imp__sub_00097710(ctx, base);
    if (own && target >= kUnzoomed && G<float>(camera + kCameraAngle) >= kUnzoomed) g_zoom = kUnzoomed;
}
