#include <array>
#include <unordered_map>

#include "kernel/memory.h"
#include "xdk/hle.h"

// The weapon update (0xBB300) hides a character's beam every frame and a shot draws it again (laser 0xB8260, stunner
// chain 0xB8400). The gadgets fire 60 shots a second, so past 60 fps the beam flickered. For a 60 fps frame after a
// shot the laser is drawn again from where the weapon is now and the stunner's chain is shown again as drawn.
namespace {

using kernel::G;
using kernel::R32;

constexpr uint32_t kStep = 0x17C104;        // seconds the frame steps
constexpr uint32_t kFrameCount = 0x1F65B4;  // one a frame
constexpr double kConsoleFrame = 1.0 / 60;
constexpr double kSlack = 0.0001;  // the step is a float

constexpr uint32_t kEffects = 0x77C;  // the character's effect objects
constexpr uint32_t kLinks = 32;       // the most a lightning chain uses
constexpr uint32_t kViewMask = 0xD4;  // of an object, 0 while hidden
constexpr uint32_t kDrawLaser = 0xB8260;  // cdecl (character data, hit point)
constexpr uint32_t kShow = 0xDB3C0;       // cdecl (object)

using Vector = std::array<float, 3>;

struct Shot {
    double seconds = 0.0;
    uint32_t frame = 0;  // the frame count it was last counted on
    bool lightning = false;
    Vector hit{};
    uint32_t links = 0;  // a lightning chain's shown effect objects, a bit each
};
std::unordered_map<uint32_t, Shot> g_shots;  // character data -> its last beam shot
bool g_redrawing = false;

uint32_t HitScratch() {
    static const uint32_t at = mem::AllocSystem(sizeof(Vector));
    return at;
}

uint32_t Effect(uint32_t character, uint32_t i) { return R32(character + kEffects + i * 4); }

uint32_t ShownLinks(uint32_t character) {
    uint32_t links = 0;
    for (uint32_t i = 0; i < kLinks; i++) {
        uint32_t object = Effect(character, i);
        if (object && G<uint16_t>(object + kViewMask)) links |= 1u << i;
    }
    return links;
}

// true while the shot is less than a 60 fps frame old
bool UpdateHeld(Shot& shot) {
    uint32_t frame = R32(kFrameCount);
    if (frame == shot.frame + 1) shot.seconds += G<float>(kStep);
    else if (frame != shot.frame) shot.seconds = kConsoleFrame;  // a level started
    shot.frame = frame;
    return shot.seconds + kSlack < kConsoleFrame;
}

void RedrawLaser(X86Context& ctx, uint32_t character, const Shot& shot) {
    uint32_t hit = HitScratch();
    for (uint32_t i = 0; i < shot.hit.size(); i++) G<float>(hit + i * 4) = shot.hit[i];
    g_redrawing = true;
    kernel::CallGuest(ctx, kDrawLaser, {character, hit});
    g_redrawing = false;
}

void ShowLightning(X86Context& ctx, uint32_t character, const Shot& shot) {
    for (uint32_t i = 0; i < kLinks; i++) {
        if (shot.links & 1u << i) kernel::CallGuest(ctx, kShow, {Effect(character, i)});
    }
}

}  // namespace

X86_EXTERN_FUNC(__imp__sub_000B8260);
X86_FUNC(sub_000B8260) {
    uint32_t character = R32(ctx.esp + 4);
    uint32_t hit = R32(ctx.esp + 8);
    __imp__sub_000B8260(ctx, base);
    if (g_redrawing) return;
    Shot shot{.frame = R32(kFrameCount)};
    shot.hit = {G<float>(hit), G<float>(hit + 4), G<float>(hit + 8)};
    g_shots[character] = shot;
}

// a stunner shot's chain, cdecl (character data, hit point)
X86_EXTERN_FUNC(__imp__sub_000B8400);
X86_FUNC(sub_000B8400) {
    uint32_t character = R32(ctx.esp + 4);
    __imp__sub_000B8400(ctx, base);
    g_shots[character] = {.frame = R32(kFrameCount), .lightning = true, .links = ShownLinks(character)};
}

// at 0xBB35D, after the weapon update hid its effects (character data in esi)
void BeamHolds(X86Context& ctx, uint8_t*) {
    uint32_t character = ctx.esi;
    auto found = g_shots.find(character);
    if (found == g_shots.end()) return;
    Shot& shot = found->second;
    if (!UpdateHeld(shot)) {
        g_shots.erase(found);
        return;
    }
    if (shot.lightning) ShowLightning(ctx, character, shot);
    else RedrawLaser(ctx, character, shot);
}
