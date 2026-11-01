#include <algorithm>
#include <iterator>
#include <unordered_map>

#include "xdk/hle.h"

// A laser or stunner shot ends the frame after it reaches its range (0x23670), so at 60 fps each of its 60 shots a
// second shows for one frame and the beam looks whole; past 60 fps most frames had none and the beam flickered. Such a
// shot stays drawn, still and testing nothing, until it has shown for a 60 fps frame.
namespace {

using kernel::G;

constexpr uint32_t kStep = 0x17C104;  // seconds the frame steps
constexpr double kConsoleFrame = 1.0 / 60;
constexpr double kSlack = 0.0001;  // the step is a float
constexpr uint32_t kRecord = 0x38;  // the shot's weapon record, in its data
constexpr uint16_t kBeams[] = {0x4A, 0x4C, 0x4E, 0x4F};  // the stunner and the laser, two strengths each

std::unordered_map<uint32_t, double> g_shown;  // shot -> seconds shown

bool Beam(uint32_t data) {
    uint16_t weapon = G<uint16_t>(kernel::R32(data + kRecord));
    return std::find(std::begin(kBeams), std::end(kBeams), weapon) != std::end(kBeams);
}

}  // namespace

// at 0x2372B, in place of the shot's end (shot in edi, its data in esi); true keeps it
bool BeamLingers(X86Context& ctx, uint8_t*) {
    if (!Beam(ctx.esi)) return false;
    double& shown = g_shown[ctx.edi];
    shown += G<float>(kStep);
    if (shown + kSlack < kConsoleFrame) return true;
    g_shown.erase(ctx.edi);
    return false;
}
