#include <cstring>
#include <filesystem>
#include <string>

#include "kernel/disc.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

// The films play from eurocom\30_fps at 60 Hz and eurocom\25_fps at 50 Hz. The Europe disc has only 25_fps, so
// its films play from there while the game keeps the 60 Hz the recomp reports.
namespace {

constexpr uint32_t kFilmFolder = 0x1620FC;  // "30_fps\", which 0xE8A90 takes at 60 Hz
constexpr char kEuropeFolder[] = "25_fps\\";

bool HasFolder(const char* path) {
    if (const kernel::Disc* disc = kernel::MountedDisc()) return disc->Find(path) != nullptr;
    std::string host = kernel::ResolvePath(std::string("D:\\") + path, 0);
    std::error_code ec;
    return !host.empty() && std::filesystem::is_directory(host, ec);
}

}  // namespace

// plays a film, cdecl (name)
X86_EXTERN_FUNC(__imp__sub_000E8A90);
X86_FUNC(sub_000E8A90) {
    static const bool europe = !HasFolder("eurocom\\30_fps") && HasFolder("eurocom\\25_fps");
    if (europe) std::memcpy(kernel::HostPtr(kFilmFolder), kEuropeFolder, sizeof(kEuropeFolder));
    __imp__sub_000E8A90(ctx, base);
}
