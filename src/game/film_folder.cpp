#include <cstring>

#include "common/options.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

// The films play from eurocom\30_fps at 60 Hz and eurocom\25_fps at 50 Hz. The Europe disc has only 25_fps, so
// its films play from there while the game keeps the 60 Hz the recomp reports.
namespace {

constexpr uint32_t kFilmFolder = 0x1620FC;  // "30_fps\", which 0xE8A90 takes at 60 Hz
constexpr char kEuropeFolder[] = "25_fps\\";

}  // namespace

// plays a film, cdecl (name)
X86_EXTERN_FUNC(__imp__sub_000E8A90);
X86_FUNC(sub_000E8A90) {
    if (game::PalDisc()) std::memcpy(kernel::HostPtr(kFilmFolder), kEuropeFolder, sizeof(kEuropeFolder));
    __imp__sub_000E8A90(ctx, base);
}
