#include "common/options.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

// The mission films play from misc.viv's NTSC\eng at 60 Hz and PAL\<language> at 50 Hz. The Europe disc has only the
// PAL ones, so its films play from there while the game keeps the 60 Hz the recomp reports.
namespace {

constexpr uint32_t kLanguageColumn = 0xE30D0;  // cdecl (): the language's place in the folder lists
constexpr uint32_t kPalFolders = 0x1CAEEC;     // "PAL\eng", "PAL\fre", "PAL\ger", "PAL\spa", then "PAL\eng"

}  // namespace

// the films' folder, cdecl ()
X86_EXTERN_FUNC(__imp__sub_000E3140);
X86_FUNC(sub_000E3140) {
    if (!game::PalDisc()) return __imp__sub_000E3140(ctx, base);
    uint32_t column = kernel::CallGuest(ctx, kLanguageColumn, {});
    ctx.eax = kernel::R32(kPalFolders + column * 4);
}
