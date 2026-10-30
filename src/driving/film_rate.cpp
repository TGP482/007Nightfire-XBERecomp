#include "common/guest_call.h"

// A mission's film plays in its own loop (0x1308A0): it draws the picture and presents it, then waits for the film's
// next one (0x14CD00), running the timers (0x10AC40) and sleeping (0x10A7E0) between looks, so the whole frame rate
// held at the film's 30. Until the next picture is due the loop draws the one it has again instead, as default.xbe's
// films do, so only the film keeps its rate. The player's draw (vt +0xC) uploads the picture into its other texture
// first, so a picture drawn again only draws the current texture (vt +0x10).
namespace {

using kernel::R32;

constexpr uint32_t kFilm = 0x14;            // of the loop's player, esi
constexpr uint32_t kPictureDue = 0x14CD00;  // thiscall (film), al
constexpr uint32_t kRunTimers = 0x10AC40;   // (0)
constexpr uint32_t kByte = 0xFF;
constexpr uint32_t kDrawAgain = 0x10;  // the player's vt, thiscall

bool g_again = false;  // the loop draws the picture it has again

bool PictureDue(X86Context& ctx) { return (game::CallThis(ctx, kPictureDue, R32(ctx.esi + kFilm)) & kByte) != 0; }

}  // namespace

// at 0x130A5B, before the wait: true draws the picture again (0x130A18)
bool DrivingFilmRedraw(X86Context& ctx, uint8_t*) {
    g_again = !PictureDue(ctx);
    if (!g_again) return false;
    kernel::CallGuest(ctx, kRunTimers, {0});
    return true;
}

// at 0x130A30, the picture's draw, the player in edi: true has drawn it again (on to 0x130A38)
bool DrivingFilmPicture(X86Context& ctx, uint8_t*) {
    if (!g_again) return false;
    game::CallThis(ctx, R32(R32(ctx.edi) + kDrawAgain), ctx.edi);
    return true;
}
