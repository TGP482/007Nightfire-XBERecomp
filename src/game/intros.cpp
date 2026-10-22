#include "common/options.h"
#include "xdk/hle.h"

// The boot logos (EA GAMES, film 0x7370048, then MGM INTERACTIVE, 0x7380048) are the steps of screen 0x40000032, which
// opens the title screen once both films have ended. Opening it past both steps goes to the title screen at once.
namespace {

constexpr uint32_t kMessage = 0x10;       // the screen handler's fourth argument, from the stack pointer
constexpr uint32_t kOpened = 0x4C;        // the screen became current; the handler zeroes its step
constexpr uint32_t kLogoStep = 0x25DDD8;  // 0 starts the EA film, then each film's end moves it on
constexpr uint32_t kLogosDone = 4;        // the next update opens the title screen without waiting on a film

}  // namespace

// the logo screen's handler, cdecl (player, screen, screen id, message, two message arguments)
X86_EXTERN_FUNC(__imp__sub_00085240);
X86_FUNC(sub_00085240) {
    bool opened = kernel::R32(ctx.esp + kMessage) == kOpened;
    __imp__sub_00085240(ctx, base);
    if (opened && game::Opt().skip_intros) kernel::W32(kLogoStep, kLogosDone);
}
