#include "common/options.h"
#include "xdk/hle.h"

// The boot logos (EA GAMES, film 0x7370048, then MGM INTERACTIVE, 0x7380048) are the steps of screen 0x40000032, which
// opens the title screen once both films have ended. Opening it past both steps goes to the title screen at once.
// The intro film after the title screen (0x73B0048) waits 180 menu runs before a press skips it.
namespace {

constexpr uint32_t kMessage = 0x10;       // the screen handler's fourth argument, from the stack pointer
constexpr uint32_t kOpened = 0x4C;        // the screen became current; the handler zeroes its step
constexpr uint32_t kUpdate = 0x50;
constexpr uint32_t kLogoStep = 0x25DDD8;  // 0 starts the EA film, then each film's end moves it on
constexpr uint32_t kLogosDone = 4;        // the next update opens the title screen without waiting on a film
constexpr uint32_t kIntroStep = 0x25DDDC;  // its update at 1 starts the intro film
constexpr uint32_t kIntroStarts = 1;
constexpr uint32_t kSkipWait = 0x25D7C0;  // menu runs left before the playing film can be skipped
constexpr uint32_t kSkipFilm = 0x7FEA0;   // cdecl (now): stops a skippable film, now regardless of the wait

}  // namespace

// the logo screen's handler, cdecl (player, screen, screen id, message, two message arguments)
X86_EXTERN_FUNC(__imp__sub_00085240);
X86_FUNC(sub_00085240) {
    bool opened = kernel::R32(ctx.esp + kMessage) == kOpened;
    __imp__sub_00085240(ctx, base);
    if (opened && game::Opt().skip_intros) kernel::W32(kLogoStep, kLogosDone);
}

// the intro film's screen handler, as the logos'; it opens the main menu once the film has ended
X86_EXTERN_FUNC(__imp__sub_000852F0);
X86_FUNC(sub_000852F0) {
    bool starts = kernel::R32(ctx.esp + kMessage) == kUpdate && kernel::R32(kIntroStep) == kIntroStarts;
    __imp__sub_000852F0(ctx, base);
    if (!starts || !game::Opt().skip_intros) return;
    uint32_t result = ctx.eax;
    kernel::CallGuest(ctx, kSkipFilm, {1});
    ctx.eax = result;
}

// a press skips the film at once
X86_EXTERN_FUNC(__imp__sub_0007FEA0);
X86_FUNC(sub_0007FEA0) {
    kernel::W32(kSkipWait, 0);
    __imp__sub_0007FEA0(ctx, base);
}
