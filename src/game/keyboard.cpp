#include <atomic>
#include <chrono>

#include "common/mouse_keyboard.h"
#include "xdk/hle.h"

// The keys join the game's actions after its controller layout has made them from the pad (0xDE720, a switch on the
// layout), so the layout picked in the game's controls menu never moves a key.
namespace {

constexpr uint32_t kPlayer1Actions = 0x1FE6E4;  // floats; 0x6CF50 clamps them to -1..1 and finds the presses next
constexpr uint32_t kPlayer1States = 0x1FE7D4;   // a byte of state bits each: 1 held, 4 pressed, 8 repeating
constexpr uint8_t kPressed = 4;
constexpr int64_t kMenuMs = 250;  // a menu reads its keys each frame it is up
std::atomic<int64_t> g_menu_ms{0};  // the last read
// menu records, 0x1D8 apart; the pause is the first, and START closes it (0x94720), not back
constexpr uint32_t kRecords = 0x25F1D0;
constexpr uint32_t kRecordSize = 0x1D8;
constexpr uint32_t kRecordCount = 5;
constexpr uint32_t kRecordKind = 0x1B4;   // 0 closed
constexpr uint32_t kRecordClose = 0x1C8;  // the action value that closes it
constexpr uint32_t kRecordActive = 0x1D1;
constexpr uint32_t kStart = 30;

// Reload and action share one value. Its read in 0xBB300 uses what is in front of the player (0xB86B0), then reloads
// unless a door took the press; every other read acts (leaves a turret, uses an object).
constexpr uint32_t kReloadAction = 14;
constexpr uint32_t kReloadRead = 0xBBB8C;  // the return address of 0xBB300's read
constexpr uint32_t kControllerPlayer = 0x8DE;  // in the controller 0xB86B0 gets in eax

int64_t NowMs() {
    auto since = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(since).count();
}

bool MenuUp() {
    for (uint32_t k = 0; k < kRecordCount; k++)
        if (kernel::R32(kRecords + k * kRecordSize + kRecordKind)) return true;
    return false;
}

bool StartCloses() {
    return kernel::G<uint8_t>(kRecords + kRecordActive) && kernel::R32(kRecords + kRecordClose) == kStart;
}

}  // namespace

void input::ImageKeys(Pad&) {}

// the menus' key reader: (player, action, state bits 1 held, 4 pressed, 8 repeating)
X86_EXTERN_FUNC(__imp__sub_00072E20);
X86_FUNC(sub_00072E20) {
    g_menu_ms = NowMs();
    __imp__sub_00072E20(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_000DE720);
X86_FUNC(sub_000DE720) {
    __imp__sub_000DE720(ctx, base);
    input::SetMenu(MenuUp());
    float keys[input::kActionValues] = {};
    input::Keyboard(keys, NowMs() - g_menu_ms < kMenuMs && !StartCloses());
    for (uint32_t i = 0; i < input::kActionValues; i++) kernel::G<float>(kPlayer1Actions + i * 4) += keys[i];
}

// (player, value, state bits): nonzero in ax when the value has any of the bits
X86_EXTERN_FUNC(__imp__sub_0006C9A0);
X86_FUNC(sub_0006C9A0) {
    uint32_t read = kernel::R32(ctx.esp);
    int16_t player = int16_t(kernel::R16(ctx.esp + 4));
    uint32_t value = kernel::R32(ctx.esp + 8);
    uint8_t bits = kernel::G<uint8_t>(ctx.esp + 12);
    __imp__sub_0006C9A0(ctx, base);
    if (player != 0 || value != kReloadAction) return;
    uint8_t keys = input::ActionKey();
    if (read == kReloadRead) keys |= input::ReloadKey();
    if (keys & bits) ctx.eax = (ctx.eax & 0xFFFF0000u) | 1;
}

// The reload key reloads without using anything, the action key uses without reloading; the controller's button does
// both, as on the console.
X86_EXTERN_FUNC(__imp__sub_000B86B0);
X86_FUNC(sub_000B86B0) {
    bool player1 = kernel::G<int8_t>(ctx.eax + kControllerPlayer) == 0;
    bool button = kernel::G<uint8_t>(kPlayer1States + kReloadAction) & kPressed;
    bool reload = input::ReloadKey() & kPressed;
    bool action = input::ActionKey() & kPressed;
    if (!player1 || button || reload == action) {
        __imp__sub_000B86B0(ctx, base);
    } else if (reload) {
        kernel::Return(ctx, 0, 0);
    } else {
        __imp__sub_000B86B0(ctx, base);
        ctx.eax = (ctx.eax & 0xFFFFFF00u) | 1;
    }
}
