#include <array>
#include <unordered_map>

#include "xdk/hle.h"

// A player's update (0xAD520) recharges the gadgets every second, fourth or sixth frame (frame count 0x1F65B4) by
// 60 over the rate (0x17C100), cut to a whole number: past 60 fps that is 0 and they never recharge. They recharge as
// at 60 fps instead, by 1 on those frames of a 60 fps clock that runs on the game's own step.
namespace {

using kernel::G;

constexpr uint32_t kStep = 0x17C104;  // seconds the frame steps
constexpr double kConsoleRate = 60.0;
constexpr int16_t kRecharge = 1;  // 60 over 60

// a gadget's charge on the player and the full charge in its weapon record (0x18CFA0 on, 0x10C apart, +0x92)
struct Gadget {
    uint32_t charge;
    uint32_t full;
    uint32_t frames;  // between recharges
};
constexpr std::array<Gadget, 6> kGadgets = {{
    {0x4D4, 0x191DAA, 4},
    {0x504, 0x1921DA, 4},
    {0x4EC, 0x191FC2, 2},
    {0x510, 0x1922E6, 2},
    {0x498, 0x19186E, 6},
    {0x3C0, 0x190596, 4},  // the laser watch
}};

struct Clock {
    double due = 0.0;  // 60 fps frames not run yet
    uint32_t frame = 0;
};

void Recharge(uint32_t player, uint32_t frame) {
    for (const Gadget& gadget : kGadgets) {
        int16_t& charge = G<int16_t>(player + gadget.charge);
        if (frame % gadget.frames == 0 && charge < G<int16_t>(gadget.full)) charge += kRecharge;
    }
}

}  // namespace

// at 0xADA5C, in place of the recharges up to 0xADB97 (player in ebp)
bool GadgetRecharge(X86Context& ctx, uint8_t*) {
    static std::unordered_map<uint32_t, Clock> clocks;
    Clock& clock = clocks[ctx.ebp];
    clock.due += G<float>(kStep) * kConsoleRate;
    for (; clock.due >= 1.0; clock.due -= 1.0) Recharge(ctx.ebp, clock.frame++);
    return true;
}
