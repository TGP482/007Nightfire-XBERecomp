#include "d3d/visibility.h"
#include "xdk/hle.h"

// The sun flare (0x9E540) spins on last frame's visibility test until its report is written (0xE7CA0). The host
// writes reports at the present, which the spin never reaches, so an incomplete one finishes the GPU's work first.
namespace {

constexpr uint32_t kTestIncomplete = 0x88760828;  // D3DERR_TESTINCOMPLETE

}  // namespace

// D3DDevice_GetVisibilityTestResult(index, result, time), stdcall
X86_EXTERN_FUNC(__imp__sub_00165FD0);
X86_FUNC(sub_00165FD0) {
    uint32_t esp = ctx.esp;
    __imp__sub_00165FD0(ctx, base);
    if (ctx.eax != kTestIncomplete) return;
    d3d::FinishVisibilityTests();
    ctx.esp = esp;
    __imp__sub_00165FD0(ctx, base);
}
