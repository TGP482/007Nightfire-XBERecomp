#include "common/aspect.h"
#include "common/game_options.h"
#include "d3d/wide_box.h"
#include "xdk/hle.h"

// The display (0x7CFB0) is widescreen when the launch data's AV Options row or the console's video flags were set;
// the output decides instead, and the 3D view (0x96960) takes the output's own shape, so a 4:3 output plays the
// console's 4:3. The HUD and the pause menu are laid out for a 16:9 screen, so they keep that shape in the middle.
namespace {

using kernel::G;
using kernel::R32;

constexpr uint32_t kDisplay = 0x1EBFF4;
constexpr uint32_t kDisplayMode = 0x48;  // 2 widescreen, else 0
constexpr uint32_t kDisplayWide = 0x4C;  // byte
constexpr uint32_t kNarrowMode = 0;
constexpr uint32_t kWideMode = 2;
constexpr uint32_t kWideViewAspect = 0x19245C;  // 16/9, read only on a widescreen output

}  // namespace

void game::FollowOutputAspect() {
    uint32_t display = R32(kDisplay);
    if (!display) return;
    bool wide = Wide();
    G<uint8_t>(display + kDisplayWide) = wide;
    uint32_t mode = R32(display + kDisplayMode);
    if (mode == kNarrowMode || mode == kWideMode) kernel::W32(display + kDisplayMode, wide ? kWideMode : kNarrowMode);
    G<float>(kWideViewAspect) = OutputAspect();
}

// the HUD's frame, thiscall: its pieces, its 3D icons and the pause menu (0xE1970)
X86_EXTERN_FUNC(__imp__sub_000E1CE0);
X86_FUNC(sub_000E1CE0) {
    bool box = game::Wide();
    if (box) d3d::BoxToWide(true);
    __imp__sub_000E1CE0(ctx, base);
    if (box) d3d::BoxToWide(false);
}
