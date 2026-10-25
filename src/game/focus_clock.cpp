// Pause on focus loss: the image's QueryPerformanceCounter leaves out the pauses, so films do not race on return
#include "common/options.h"
#include "xdk/hle.h"

// QueryPerformanceCounter (stdcall, the count out), read by the films and Direct3D
X86_FUNC(sub_000F53D0) {
    kernel::W64(kernel::R32(ctx.esp + 4), game::GuestTicks());
    kernel::Return(ctx, 1, 1);
}
