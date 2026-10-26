#include "xdk/hle.h"

// The pause page's silhouette film decodes into two 128x128 textures (0xD7770 builds each), which the console drew
// with point filtering, so it turned blocky once drawn above 640x480. They are filtered instead.
namespace {

using kernel::G;
using kernel::R32;

constexpr uint32_t kSampler = 0x20;  // the texture's sampler record
constexpr uint32_t kFilter = 0x14;   // D3DTEXF for both magnifying and minifying
constexpr uint32_t kLinear = 2;

}  // namespace

// a film texture's constructor, thiscall
X86_EXTERN_FUNC(__imp__sub_000D7770);
X86_FUNC(sub_000D7770) {
    uint32_t texture = ctx.ecx;
    __imp__sub_000D7770(ctx, base);
    G<uint32_t>(R32(texture + kSampler) + kFilter) = kLinear;
}
