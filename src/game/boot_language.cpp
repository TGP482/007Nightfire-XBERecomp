#include "common/options.h"
#include "kernel/kernel.h"
#include "xdk/hle.h"

// 0xDBE10 picks the language once at boot: from eurocom\config.txt (the USA disc names American English), else from
// the console's language, which the boot screen 0x84130 then offers to change. The language setting replaces both.
// Driving.xbe takes the language default.xbe launches it with.
namespace {

constexpr uint32_t kLanguagePicked = 0x2AE3E8;
constexpr uint32_t kFromConfig = 0x2ADF5C;
constexpr uint32_t kAskOnBoot = 0x17D5F9;  // 0 makes 0x84130 apply the language and go on to the logos
constexpr uint32_t kLanguage = 0x25D7A0;

}  // namespace

// cdecl (): the film audio track for the language, which the boot ignores
X86_EXTERN_FUNC(__imp__sub_000DBE10);
X86_FUNC(sub_000DBE10) {
    bool picks = kernel::G<uint8_t>(kLanguagePicked) == 0;
    __imp__sub_000DBE10(ctx, base);
    if (!picks) return;
    uint32_t language = game::BootLanguage();
    bool usa_english = language == game::kEnglish && kernel::G<uint8_t>(kFromConfig);
    if (!usa_english) kernel::G<uint32_t>(kLanguage) = language;
    kernel::G<uint8_t>(kAskOnBoot) = 0;
}

uint32_t game::GameLanguage() {
    uint32_t language = kernel::G<uint32_t>(kLanguage);
    return language == kAmerican ? kEnglish : language;
}
