#include <array>
#include <chrono>

#include "common/mouse_keyboard.h"
#include "game/main_menu.h"
#include "xdk/hle.h"

// The menus count frames, not time: 0x94370 runs them once a frame (their input, pages and focus pulse). It runs 60
// times a second instead, as on the console, at any frame rate; the widgets' 2D items stay drawn between runs. A press
// is a state bit for one frame, so the presses of the frames between runs are kept for the next run. A held action
// repeats after a count of its frames, which also goes up 60 times a second while a menu is up.
namespace {

using kernel::G;

// menu records, 0x1D8 apart: their page, 0 while closed
constexpr uint32_t kRecordPages = 0x25F384;
constexpr uint32_t kRecordSize = 0x1D8;
constexpr uint32_t kRecordCount = 5;
// player 1's action state bytes, the other players' 0x158 apart; 0x6CF50 makes them anew each frame
constexpr uint32_t kStates = 0x1FE7D4;
constexpr uint32_t kPlayerBlock = 0x158;
constexpr uint32_t kPlayers = 4;
constexpr uint8_t kEdges = 4 | 8;  // pressed, repeating
constexpr uint8_t kRepeating = 8;
// u16 frames each action value has been held, beside the states; it repeats from 46 on, every 8th
constexpr uint32_t kHeldFrames = 0x1FE784;
constexpr double kConsoleRate = 60.0;

using Clock = std::chrono::steady_clock;

std::array<uint8_t, kPlayers * input::kActionValues> g_edges{};
std::array<uint16_t, kPlayers * input::kActionValues> g_held{};
Clock::time_point g_next;
Clock::time_point g_next_held;

bool MenuUp() {
    for (uint32_t k = 0; k < kRecordCount; k++)
        if (kernel::R32(kRecordPages + k * kRecordSize)) return true;
    return false;
}

uint32_t PlayerOffset(size_t index) { return uint32_t(index / input::kActionValues) * kPlayerBlock; }

uint8_t& State(size_t index) {
    return G<uint8_t>(kStates + PlayerOffset(index) + uint32_t(index % input::kActionValues));
}

uint16_t& Held(size_t index) {
    return G<uint16_t>(kHeldFrames + PlayerOffset(index) + uint32_t(index % input::kActionValues) * 2);
}

void LatchHeld() {
    for (size_t i = 0; i < g_held.size(); i++) g_held[i] = Held(i);
}

// a value still held keeps its count from before this frame, and so does not repeat on it
void RestoreHeld() {
    for (size_t i = 0; i < g_held.size(); i++) {
        if (!g_held[i] || !Held(i)) continue;
        Held(i) = g_held[i];
        State(i) &= ~kRepeating;
    }
}

void KeepEdges() {
    for (size_t i = 0; i < g_edges.size(); i++) g_edges[i] |= State(i) & kEdges;
}

void ApplyEdges() {
    for (size_t i = 0; i < g_edges.size(); i++) State(i) |= g_edges[i];
    g_edges.fill(0);
}

// true when a 60 Hz step is due, the next then due a period later
bool UpdateDue(Clock::time_point& next) {
    constexpr std::chrono::duration<double> kSeconds{1.0 / kConsoleRate};
    constexpr auto kPeriod = std::chrono::duration_cast<Clock::duration>(kSeconds);
    Clock::time_point now = Clock::now();
    if (now < next) return false;
    next += kPeriod;
    if (next <= now) next = now + kPeriod;
    return true;
}

}  // namespace

// the menus' frame
X86_EXTERN_FUNC(__imp__sub_00094370);
X86_FUNC(sub_00094370) {
    if (!MenuUp()) {
        g_edges.fill(0);
        game::ClearMainMenuLegend();
        return __imp__sub_00094370(ctx, base);
    }
    KeepEdges();
    if (!UpdateDue(g_next)) {
        kernel::Return(ctx, 0, 0);
        return;
    }
    ApplyEdges();
    game::ClearMainMenuLegend();
    __imp__sub_00094370(ctx, base);
}

// the input's frame
X86_EXTERN_FUNC(__imp__sub_0006CF50);
X86_FUNC(sub_0006CF50) {
    bool between = MenuUp() && !UpdateDue(g_next_held);
    if (between) LatchHeld();
    __imp__sub_0006CF50(ctx, base);
    if (between) RestoreHeld();
}
