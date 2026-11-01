#include <algorithm>

#include "xdk/hle.h"

// A stunner shot (0x4A, 0x4C) touching a character steps its stun count (0x3E783) by 60 over the rate cut to a whole
// number, and the character falls stunned once the count passes twice its health. Past 60 fps that step is 0, so the
// count never moved. Each hit steps it by its 60 fps step instead.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate

}  // namespace

// at 0x3E78E, in place of the count's step up to its store at 0x3E79D; the count in eax
bool StunCount(X86Context& ctx, uint8_t*) {
    float step = std::max(G<float>(kStepScale), 1.0f);
    ctx.eax = uint32_t(ctx.eax + step);
    return true;
}
