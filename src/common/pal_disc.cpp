#include <filesystem>
#include <string>

#include "common/options.h"
#include "kernel/disc.h"
#include "kernel/kernel.h"

// The Europe disc holds only the films for 50 Hz: eurocom\25_fps and Driving.xbe's PAL folders, no 30_fps or NTSC.
namespace {

bool HasFolder(const char* path) {
    if (const kernel::Disc* disc = kernel::MountedDisc()) return disc->Find(path) != nullptr;
    std::string host = kernel::ResolvePath(std::string("D:\\") + path, 0);
    std::error_code ec;
    return !host.empty() && std::filesystem::is_directory(host, ec);
}

}  // namespace

bool game::PalDisc() {
    static const bool pal = !HasFolder("eurocom\\30_fps") && HasFolder("eurocom\\25_fps");
    return pal;
}
