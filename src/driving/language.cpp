#include "common/options.h"
#include "kernel/kernel.h"

namespace {

constexpr uint32_t kLanguage = 0x244518;  // the game's language id, as default.xbe launched this image with

}  // namespace

uint32_t game::GameLanguage() {
    uint32_t language = kernel::G<uint32_t>(kLanguage);
    return language == kAmerican ? kEnglish : language;
}
