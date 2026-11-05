#include <unordered_map>

#include "xdk/hle.h"

// A shot sets its owner's muzzle glow count (+0x8C8, 0xB9FC0) and the player's update steps it down once a frame
// (0xAE552), the glow showing until 0. Past 60 fps it ran out between the gadgets' shots and flickered.
namespace {

using kernel::G;

constexpr uint32_t kStepScale = 0x17C100;  // 60 over the rate
constexpr uint32_t kGlowCount = 0x8C8;     // in the character's data

struct Glow {
    double due = 0.0;  // 60 fps frames since the count last stepped
    int16_t last = 0;  // the count as it was left
};
std::unordered_map<uint32_t, Glow> g_glows;  // character data -> its glow

}  // namespace

// at 0xAE552, in place of the count's step up to 0xAE568 (character data in esi)
bool GlowCount(X86Context& ctx, uint8_t*) {
    int16_t& count = G<int16_t>(ctx.esi + kGlowCount);
    Glow& glow = g_glows[ctx.esi];
    if (count > glow.last) {
        glow.due = 1.0;  // a shot: the console steps it in the shot's frame
    } else {
        glow.due += G<float>(kStepScale);
    }
    while (glow.due >= 1.0 && count > 0) {
        count--;
        glow.due -= 1.0;
    }
    glow.last = count;
    return true;
}
