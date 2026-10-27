#include "common/pc_wording.h"
#include "xdk/hle.h"

// a text by its id, cdecl (id): see src/common/pc_wording.cpp
X86_EXTERN_FUNC(__imp__sub_000E3AF0);
X86_FUNC(sub_000E3AF0) {
    __imp__sub_000E3AF0(ctx, base);
    ctx.eax = game::PcWording(ctx.eax);
}
