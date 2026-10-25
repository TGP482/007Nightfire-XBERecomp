#include "app/app.h"
#include "xdk/hle.h"

// The loading screen (0xE28A0, a timer callback while a mission loads) draws its picture's pieces and its dots as
// sprites on the 640x480 frame (0x75F60), which a widescreen output stretches. They keep their 4:3 shape in the
// middle of the frame instead.
namespace {

using kernel::G;

constexpr float kNarrow = 0.75f;  // 4:3 of a 16:9 frame
constexpr float kFrameMiddle = 320.0f;
// the sprite's arguments, cdecl (x, y, width, height, ...)
constexpr uint32_t kSpriteX = 4;
constexpr uint32_t kSpriteWidth = 0xC;

bool g_loading = false;

}  // namespace

// the loading screen's frame
X86_EXTERN_FUNC(__imp__sub_000E28A0);
X86_FUNC(sub_000E28A0) {
    g_loading = true;
    __imp__sub_000E28A0(ctx, base);
    g_loading = false;
}

// a sprite
X86_EXTERN_FUNC(__imp__sub_00075F60);
X86_FUNC(sub_00075F60) {
    if (g_loading && app::Widescreen()) {
        float& x = G<float>(ctx.esp + kSpriteX);
        x = kFrameMiddle + (x - kFrameMiddle) * kNarrow;
        G<float>(ctx.esp + kSpriteWidth) *= kNarrow;
    }
    __imp__sub_00075F60(ctx, base);
}
