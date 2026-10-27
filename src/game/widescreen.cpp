#include "common/aspect.h"
#include "common/game_options.h"
#include "xdk/hle.h"

// The game's widescreen comes from the console's video flags at boot (0xDC0D0), the AV Options row (0x81DD0) and the
// loaded profile (0x19DB0), and 0x25190 shapes each view 4:3 or 16:9 by it. The output decides instead: each view
// takes the output's own shape, so it plays wider as the screen does and a 4:3 one plays the console's 4:3, and a
// change of resolution reshapes the views already made.
namespace {

using kernel::G;
using kernel::R32;

constexpr uint32_t kWideShown = 0x1DF9E8;  // the AV Options row's value
constexpr uint32_t kWide = 0x1F6610;       // what 0x25190 shapes the views by
constexpr uint32_t kViews = 0x1F661C;
constexpr uint32_t kViewCount = 9;  // as 0x81DD0 reshapes them
// view: 0x25190 sets the aspect and divides the world to screen scale across by it
constexpr uint32_t kViewScreenX = 0xEC;
constexpr uint32_t kViewAspect = 0x100;
// 0x25190's arguments, cdecl (view index, angle)
constexpr uint32_t kShapeIndex = 4;

float g_shaped = 0.0f;  // the output shape the views have, 0 before the first

void Reshape(uint32_t view, float scale) {
    G<float>(view + kViewAspect) *= scale;
    G<float>(view + kViewScreenX) /= scale;
}

}  // namespace

void game::FollowOutputAspect() {
    float aspect = OutputAspect();
    bool wide = Wide();
    kernel::W32(kWide, wide);
    kernel::W32(kWideShown, wide);
    if (g_shaped == 0.0f) g_shaped = aspect;  // 0x25190 has made no view before the first call
    if (aspect == g_shaped) return;
    for (uint32_t i = 0; i < kViewCount; i++) {
        uint32_t view = R32(kViews + i * 4);
        if (view) Reshape(view, aspect / g_shaped);
    }
    g_shaped = aspect;
}

namespace game {

// 0x25190 shapes the view 4:3 or 16:9, and the output's shape follows; a view it leaves alone keeps its own
void ShapeView(X86Context& ctx, uint8_t* base, void (*shape)(X86Context&, uint8_t*)) {
    FollowOutputAspect();
    uint32_t index = kernel::R16(ctx.esp + kShapeIndex);
    uint32_t view = index < kViewCount ? R32(kViews + index * 4) : 0;
    float kept = view ? G<float>(view + kViewAspect) : 0.0f;
    if (view) G<float>(view + kViewAspect) = 0.0f;
    shape(ctx, base);
    if (!view) return;
    if (G<float>(view + kViewAspect) == 0.0f) {
        G<float>(view + kViewAspect) = kept;
        return;
    }
    float flag_shape = R32(kWide) ? kWideAspect : kConsoleAspect;
    Reshape(view, g_shaped / flag_shape);
}

}  // namespace game
