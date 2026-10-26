#include "common/options.h"
#include "xdk/hle.h"

// Pause on focus loss: the engine's timer (set up by 0x10AE50) counts its ticks (0x242428) and runs its callbacks on
// its own thread, so a pause went on counting; the films time themselves by that count (0x10A6C0) and played on
// ahead of their pictures after one. The ticks stop while a pause holds the game.

// the timer's callback, stdcall (id, message, user, 0, 0)
X86_EXTERN_FUNC(__imp__sub_0010AE10);
X86_FUNC(sub_0010AE10) {
    if (!game::FocusPaused()) return __imp__sub_0010AE10(ctx, base);
    kernel::Return(ctx, 0, 5);
}
