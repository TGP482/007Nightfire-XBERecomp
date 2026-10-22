#include <cstdint>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "common/mouse_keyboard.h"
#include "kernel/kernel.h"

// Driving turns the pad into actions through one table (0x51250), filled from the controls files for the mode: the one
// driving layout, or in a turret one of the eight on-foot layouts. Each key presses the channel the table in use gives
// its action, so a layout never moves a key.
namespace {

constexpr uint32_t kInputMaps = 0x1E45E8;  // an InputToAction a port: 0x51580's manager 0x1E45A0, + 0x48
constexpr uint32_t kActivePort = 0x244520;
constexpr uint32_t kPorts = 4;
// InputToAction: +0 channel count, +4 channels of 0x3C (+0 trigger count, +8 triggers of 0xC: method, threshold, action)
constexpr uint32_t kMapChannels = 4;
constexpr uint32_t kChannelSize = 0x3C;
constexpr uint32_t kChannelTriggers = 8;
constexpr uint32_t kTriggerSize = 0xC;
constexpr uint32_t kTriggerAction = 8;
// XBoxPadDevice's channels (0x51B40): LX LY RX RY, the analog buttons in the pad's order, then its button bits
constexpr int kAnalogChannel = 4;
constexpr int kDigitalChannel = 12;
constexpr uint32_t kChannelCount = 20;
constexpr int16_t kFull = 32767;

// the controls files' action names (table 0x1B6BA0)
enum Action : uint32_t {
    kSteer = 0x01,
    kSteerVertical = 0x02,
    kGas = 0x16,
    kBrake = 0x1A,
    kHandbrake = 0x1B,
    kFire = 0x1F,  // FIRESECONDARY, the chosen weapon; FIREPRIMARY is in no layout
    kPrevWeapon = 0x22,  // TOGGLESECONDARYUP
    kNextWeapon = 0x23,
    kGadget = 0x24,
    kCamera = 0x26,
    kLookBack = 0x29,
    kAimX = 0x2F,  // CAMROTATEX2: a turret's right stick, the same look as its left (0xB7AE0); driving has none
    kAimY = 0x30,
    kSpin = 0x32,
    kZoomIn = 0x35,
    kZoomOut = 0x36,
    kManualAim = 0x38,  // AIMZOOMRIGHT
    kReload = 0x3F,  // POVRELOAD
    kStart = 0x47,  // FRONTENDACTION_START: pauses and resumes (0xE0140)
};

struct Key {
    const char* id;
    Action action;
    int sign;  // on a stick, up and right positive
};
constexpr Key kKeys[] = {
    {"car_accelerate", kGas, 1},
    {"car_brake", kBrake, 1},
    {"car_steer_left", kSteer, -1},
    {"car_steer_right", kSteer, 1},
    {"car_steer_up", kSteerVertical, 1},
    {"car_steer_down", kSteerVertical, -1},
    {"car_handbrake", kHandbrake, 1},
    {"car_fire", kFire, 1},
    {"car_gadget", kGadget, 1},
    {"car_prev_weapon", kPrevWeapon, 1},
    {"car_next_weapon", kNextWeapon, 1},
    {"car_camera", kCamera, 1},
    {"car_look_back", kLookBack, 1},
    {"car_aim_up", kAimY, 1},
    {"car_aim_down", kAimY, -1},
    {"car_aim_left", kAimX, -1},
    {"car_aim_right", kAimX, 1},
    {"car_manual_aim", kManualAim, 1},
    {"car_reload", kReload, 1},
    {"car_spin", kSpin, 1},
    {"car_zoom_in", kZoomIn, 1},
    {"car_zoom_out", kZoomOut, 1},
    {"car_pause", kStart, 1},
};

// not bindable, as on foot: the arrows are the dpad, Enter is A, Backspace is Back, the same in every layout
constexpr int kDpadKeys[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT};
constexpr int kAnalogA = 0;
constexpr uint16_t kBack = 0x20;

// the channel the table in use gives the action, or -1 when this mode has none
int Channel(Action action) {
    uint32_t port = kernel::R32(kActivePort);
    uint32_t map = port < kPorts ? kernel::R32(kInputMaps + port * 4) : 0;
    if (!map) return -1;
    uint32_t channels = kernel::R32(map + kMapChannels);
    uint32_t count = kernel::R32(map);
    for (uint32_t c = 0; c < count && c < kChannelCount; c++) {
        uint32_t channel = channels + c * kChannelSize;
        uint32_t triggers = kernel::R32(channel + kChannelTriggers);
        for (uint32_t t = 0, n = kernel::R32(channel); t < n; t++)
            if (kernel::R32(triggers + t * kTriggerSize + kTriggerAction) == action) return int(c);
    }
    return -1;
}

bool Foreground() {
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}

bool Down(int vk) { return GetAsyncKeyState(vk) < 0; }

}  // namespace

void input::ImageKeys(Pad& pad) {
    int sticks[kAnalogChannel] = {};
    for (const Key& k : kKeys) {
        if (!Held(k.id)) continue;
        int c = Channel(k.action);
        if (c < 0) continue;
        if (c < kAnalogChannel) sticks[c] += k.sign;
        else if (c < kDigitalChannel) pad.analog[c - kAnalogChannel] = 0xFF;
        else pad.buttons |= uint16_t(1u << (c - kDigitalChannel));
    }
    int16_t* axes[kAnalogChannel] = {&pad.lx, &pad.ly, &pad.rx, &pad.ry};
    for (int i = 0; i < kAnalogChannel; i++)
        if (sticks[i]) *axes[i] = sticks[i] > 0 ? kFull : int16_t(-kFull);
    if (!Foreground()) return;
    for (int i = 0; i < 4; i++)
        if (Down(kDpadKeys[i])) pad.buttons |= uint16_t(1u << i);
    if (Down(VK_RETURN)) pad.analog[kAnalogA] = 0xFF;
    if (Down(VK_BACK)) pad.buttons |= kBack;
}
