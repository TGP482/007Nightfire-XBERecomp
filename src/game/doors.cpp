#include "xdk/hle.h"

// A door (kind 0x1C, its update 0x2F930) opens by a share of the way per 60 fps frame (+0x50, 0x2EE70 scales it to
// the frame). At its creation (0x2F6BD) the share is one over its open time in frames (+0x4 of its record), or one
// over the frame rate when it has none: one second on the console, but at a high frame rate the doors without an
// open time, such as Double Cross's sliding doors, opened slower by the rate over 60. They take the console's rate.
namespace {

constexpr uint32_t kOpenFrames = 0x4;      // u16 of the door's record, in esi
constexpr uint32_t kConsoleRate = 0x17C0F0;

}  // namespace

// at 0x2F6D9, st0 holds the open time in frames
void DoorDefaultOpenTime(X86Context& ctx, uint8_t*) {
    uint32_t rate = kernel::R32(kConsoleRate);
    if (kernel::G<uint16_t>(ctx.esi + kOpenFrames) == 0 && rate) X87_ST(0) = double(rate);
}
