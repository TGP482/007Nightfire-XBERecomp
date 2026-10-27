#include "common/pc_wording.h"
#include "xdk/hle.h"

// a text by its id, cdecl (id, ...): see src/common/pc_wording.cpp
X86_EXTERN_FUNC(__imp__sub_0006D460);
X86_FUNC(sub_0006D460) {
    __imp__sub_0006D460(ctx, base);
    ctx.eax = game::PcWording(ctx.eax);
}
