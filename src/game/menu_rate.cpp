#include <array>
#include <chrono>

#include "common/mouse_keyboard.h"
#include "game/main_menu.h"
#include "xdk/hle.h"

// The menus count frames, not time: 0x94370 runs them once a frame (their input, pages and focus pulse). It runs 30
// times a second instead, as on the console, at any frame rate; the widgets' 2D items stay drawn between runs. A press
// is a state bit for one frame, so the presses of the frames between runs are kept for the next run.
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
constexpr double kConsoleRate = 30.0;

using Clock = std::chrono::steady_clock;

std::array<uint8_t, kPlayers * input::kActionValues> g_edges{};
Clock::time_point g_next;

bool MenuUp() {
    for (uint32_t k = 0; k < kRecordCount; k++)
        if (kernel::R32(kRecordPages + k * kRecordSize)) return true;
    return false;
}

uint8_t& State(size_t index) {
    uint32_t player = uint32_t(index / input::kActionValues);
    uint32_t value = uint32_t(index % input::kActionValues);
    return G<uint8_t>(kStates + player * kPlayerBlock + value);
}

void KeepEdges() {
    for (size_t i = 0; i < g_edges.size(); i++) g_edges[i] |= State(i) & kEdges;
}

void ApplyEdges() {
    for (size_t i = 0; i < g_edges.size(); i++) State(i) |= g_edges[i];
    g_edges.fill(0);
}

// true when a run is due, the next then due a period later
bool UpdateRunDue() {
    constexpr std::chrono::duration<double> kSeconds{1.0 / kConsoleRate};
    constexpr auto kPeriod = std::chrono::duration_cast<Clock::duration>(kSeconds);
    Clock::time_point now = Clock::now();
    if (now < g_next) return false;
    g_next += kPeriod;
    if (g_next <= now) g_next = now + kPeriod;
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
    if (!UpdateRunDue()) {
        kernel::Return(ctx, 0, 0);
        return;
    }
    ApplyEdges();
    game::ClearMainMenuLegend();
    __imp__sub_00094370(ctx, base);
}
