#include "common/pc_wording.h"
#include "game/custom_layout.h"
#include "xdk/hle.h"

// a text by its id, cdecl (id, ...): see src/common/pc_wording.cpp
X86_EXTERN_FUNC(__imp__sub_0006D460);
X86_FUNC(sub_0006D460) {
    uint32_t id = kernel::R32(ctx.esp + 4);
    __imp__sub_0006D460(ctx, base);
    ctx.eax = game::PcWording(ctx.eax);
    if (uint32_t custom = game::CustomLayoutText(id)) ctx.eax = custom;
}
