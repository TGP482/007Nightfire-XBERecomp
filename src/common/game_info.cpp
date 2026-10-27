#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <string>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "audio/output.h"
#include "common/menu.h"
#include "common/monitor.h"
#include "common/mouse_keyboard.h"
#include "common/options.h"
#include "core/input.h"

namespace game {

namespace {

constexpr intptr_t kKeyWasDown = 1 << 30;  // bit 30 of a key message's lparam: a repeat
constexpr uintptr_t kSysCommandMask = 0xFFF0;  // the low bits of a WM_SYSCOMMAND code are the system's
constexpr uint32_t kHostApplyDisplay = WM_APP + 1;  // main.cpp's kApplyDisplay

bool Take(intptr_t* result) {
    *result = 0;
    return true;
}

// the settings panel key opens and closes the panel, Escape closes it unless a key binding waits for it
bool PanelKeyDown(uint32_t msg, uintptr_t key, intptr_t lp, intptr_t* result) {
    bool first_press = !(lp & kKeyWasDown);
    if (first_press && input::PanelKey(int(key))) {
        app::ToggleMenu();
        return Take(result);
    }
    if (first_press && !app::MenuOpen() && input::HudKey(int(key))) {
        g_hud_hidden = !g_hud_hidden;
        return Take(result);
    }
    if (first_press && !app::MenuOpen() && input::TexturesKey(int(key))) {
        app::ToggleTextureReplacement();
        return Take(result);
    }
    if (msg == WM_KEYDOWN && key == VK_ESCAPE && app::MenuOpen() && !app::Capturing()) {
        app::ToggleMenu();
        return Take(result);
    }
    return false;
}

// the arrow over the panel, the menu cursor over the game's menus, none in play
void* MenuCursor() {
    if (app::MenuOpen()) return LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));  // IDC_ARROW
    return input::CursorShown() ? app::CursorImage() : nullptr;
}

// F10 is a system key: the window menu it would open stalls the game window's messages
bool SwallowKeyMenu(uintptr_t command, intptr_t* result) {
    if ((command & kSysCommandMask) != SC_KEYMENU) return false;
    return Take(result);
}

bool Message(void* hwnd, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    switch (msg) {
        case WM_CREATE:
            UpdateSplashMonitor();
            UpdateWindowMonitor(hwnd);
            return false;
        case kHostApplyDisplay:
            UpdateWindowMonitor(hwnd);
            return false;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
            return PanelKeyDown(msg, wp, lp, result);
        case WM_SYSCOMMAND:
            return SwallowKeyMenu(wp, result);
        case WM_INPUT:
            input::OnRawInput(reinterpret_cast<void*>(lp));
            return false;
        case WM_CHAR:
            input::OnChar(uint32_t(wp));
            return false;
        case WM_ACTIVATE:
            if (LOWORD(wp) == WA_INACTIVE) input::Settle();
            return false;
        case WM_SETCURSOR:
            // the game has no pointer of its own; the panel shows the arrow, the game's menus the menu cursor
            if (LOWORD(lp) != HTCLIENT) return false;
            SetCursor(static_cast<HCURSOR>(MenuCursor()));
            *result = TRUE;
            return true;
    }
    return false;
}

// The SDK reads and sets the controller slot numbered as the port. A player port takes the slot it is set to, so a
// slot read or set for another port goes past these hooks.
thread_local bool g_raw = false;
std::array<std::atomic<uint32_t>, kPlayers> g_motors{};  // by slot: left << 16 | right as its player last set

input::Pad RawPad(int slot) {
    g_raw = true;
    input::Pad pad = input::Poll(slot);
    g_raw = false;
    return pad;
}

void RawRumble(int slot, uint16_t left, uint16_t right) {
    g_raw = true;
    input::Rumble(slot, left, right);
    g_raw = false;
}

// not plain negation: -32768 has no positive
int16_t Flip(int16_t v) { return int16_t(-1 - v); }

// The game drops a stick axis inside 30 of its 100 (0xE76DD): the axis is moved so that zone starts where the chosen
// one does, the identity at the game's own
int16_t MoveDeadZone(int16_t value, float zone) {
    constexpr float kStickMax = 32767.0f;
    float v = std::min(std::abs(float(value)) / kStickMax, 1.0f);
    if (v <= zone) return 0;
    float out = (kGameDeadZone + (v - zone) / (1.0f - zone) * (1.0f - kGameDeadZone)) * kStickMax;
    return int16_t(std::lround(value < 0 ? -out : out));
}

void ApplyDeadZone(input::Pad& pad) {
    float zone = Opt().pad_dead_zone;
    if (zone <= 0.0f || zone == kGameDeadZone) return;
    for (int16_t* axis : {&pad.lx, &pad.ly, &pad.rx, &pad.ry}) *axis = MoveDeadZone(*axis, zone);
}

uint16_t ScaleMotor(uint16_t speed) {
    return uint16_t(std::min<uint32_t>(speed * Opt().vibration_strength / 100, 0xFFFF));
}

void InvertLook(input::Pad& pad) {
    if (Opt().invert_pad_x) pad.rx = Flip(pad.rx);
}

input::Pad SlotPad(int port, int slot, const input::Pad& own) {
    if (slot < 0) return input::Pad{};
    return slot == port ? own : RawPad(slot);
}

bool SlotOwned(int slot) {
    const auto& players = Opt().players;
    return std::any_of(players.begin(), players.end(), [slot](const PlayerPort& p) { return p.pad == slot; });
}

// the port's controller slot and, when set to, the keyboard, which is there without a controller; the game sees no
// input while the panel is open
void Pad(int port, input::Pad& pad) {
    if (g_raw || port < 0 || port >= kPlayers) return;
    PlayerPort player = Opt().players[size_t(port)];
    pad = SlotPad(port, player.pad, pad);
    ApplyDeadZone(pad);
    InvertLook(pad);
    if (player.keyboard) input::SeenPad(pad);
    if (player.keyboard) pad.connected = true;
    if (app::MenuOpen()) pad = input::Pad{.connected = pad.connected};
    else if (player.keyboard) input::ImageKeys(pad);
}

// to the port's own slot; what the SDK then sets on the slot numbered as the port is that slot's player's.
// The controller keeps still while the keyboard and mouse were used last.
void Rumble(int port, uint16_t& left, uint16_t& right) {
    if (g_raw || port < 0 || port >= kPlayers) return;
    PlayerPort player = Opt().players[size_t(port)];
    if (player.keyboard && input::KeyboardActive()) {
        left = 0;
        right = 0;
    }
    left = ScaleMotor(left);
    right = ScaleMotor(right);
    if (player.pad >= 0) g_motors[size_t(player.pad)] = uint32_t(left) << 16 | right;
    if (player.pad >= 0 && player.pad != port) RawRumble(player.pad, left, right);
    uint32_t motors = SlotOwned(port) ? g_motors[size_t(port)].load() : 0;
    left = uint16_t(motors >> 16);
    right = uint16_t(motors);
}

std::string PortName(const PlayerPort& p) {
    std::string name = p.keyboard ? "keyboard" : "";
    if (p.pad >= 0) name += (name.empty() ? "controller" : "+controller") + std::to_string(p.pad + 1);
    return name.empty() ? "none" : name;
}

PlayerPort ParsePort(const std::string& name, PlayerPort fallback) {
    PlayerPort p;
    p.keyboard = name.find("keyboard") != std::string::npos;
    if (size_t at = name.find("controller"); at != std::string::npos && at + 10 < name.size()) {
        int n = name[at + 10] - '0';  // the digit after "controller"
        if (n >= 1 && n <= kPlayers) p.pad = n - 1;
    }
    return p.keyboard || p.pad >= 0 || name == "none" ? p : fallback;
}

std::string PlayerKey(int player) { return "player" + std::to_string(player + 1); }

// a device sits on one port only, and player 1 never has none
void LoadPlayers(const toml::table& settings, std::array<PlayerPort, kPlayers>& players) {
    for (int i = 0; i < kPlayers; i++) {
        PlayerPort& p = players[size_t(i)];
        p = ParsePort(settings[PlayerKey(i)].value_or(PortName(p)), p);
        for (int j = 0; j < i; j++) {
            if (players[size_t(j)].keyboard) p.keyboard = false;
            if (p.pad >= 0 && players[size_t(j)].pad == p.pad) p.pad = -1;
        }
    }
    auto used = [](const PlayerPort& p) { return p.keyboard || p.pad >= 0; };
    auto first = std::find_if(players.begin(), players.end(), used);
    if (first == players.end()) players[0] = PlayerPort{true, 0};
    else std::swap(players[0], *first);
}

struct FloatOption {
    const char* key;
    float Options::*value;
};
// the max frame rate is the display's refresh rate until the player sets one, not the runtime's 60
bool g_fps_cap_saved = false;

constexpr FloatOption kFloats[] = {
    {"mouse_sensitivity", &Options::mouse_sensitivity},
    {"mouse_vertical_sensitivity", &Options::mouse_vertical_sensitivity},
    {"aim_horizontal_sensitivity", &Options::aim_horizontal_sensitivity},
    {"aim_vertical_sensitivity", &Options::aim_vertical_sensitivity},
    {"pad_horizontal_sensitivity", &Options::pad_horizontal_sensitivity},
    {"pad_vertical_sensitivity", &Options::pad_vertical_sensitivity},
    {"pad_aim_horizontal_sensitivity", &Options::pad_aim_horizontal_sensitivity},
    {"pad_aim_vertical_sensitivity", &Options::pad_aim_vertical_sensitivity},
};

// what the options list cannot hold
void Load(const void* table) {
    const toml::table& t = *static_cast<const toml::table*>(table);
    Options& o = Opt();
    for (const FloatOption& f : kFloats) o.*f.value = float(t[f.key].value_or(double(o.*f.value)));
    // files from before the split have the one sensitivity
    if (!t["mouse_vertical_sensitivity"]) o.mouse_vertical_sensitivity = o.mouse_sensitivity;
    o.gyro.sensitivity = float(t["gyro_sensitivity"].value_or(double(o.gyro.sensitivity)));
    float zone = float(t["pad_dead_zone"].value_or(double(o.pad_dead_zone)));
    o.pad_dead_zone = zone > 0.0f ? std::clamp(zone, kMinDeadZone, 1.0f) : 0.0f;
    o.vibration_strength = std::clamp(o.vibration_strength, kMinVibration, kMaxVibration);
    o.monitor = t["monitor"].value_or(o.monitor);
    if (const toml::table* keys = t["keys"].as_table())
        for (auto&& [id, value] : *keys)
            if (auto v = value.value<std::string>()) o.keys[std::string(id.str())] = *v;
    LoadPlayers(t, o.players);
    audio::SetOutputMode(o.audio_output);
    g_fps_cap_saved = t.contains("fps_cap");
}

// after the settings load, which leaves the runtime's own when there is no settings file
void Init() {
    if (!g_fps_cap_saved) app::Get().fps_cap = app::kFpsCapRefresh;
    input::LoadBindings();
}

void Save(void* table) {
    toml::table& t = *static_cast<toml::table*>(table);
    for (const FloatOption& f : kFloats) t.insert(f.key, double(Opt().*f.value));
    t.insert("gyro_sensitivity", double(Opt().gyro.sensitivity));
    t.insert("pad_dead_zone", double(Opt().pad_dead_zone));
    t.insert("monitor", Opt().monitor);
    toml::table keys;
    for (const auto& [id, value] : Opt().keys) keys.insert(id, value);
    t.insert("keys", keys);
    for (int i = 0; i < kPlayers; i++) t.insert(PlayerKey(i), PortName(Opt().players[size_t(i)]));
}

}  // namespace

Options& Opt() {
    static Options o;
    return o;
}

bool ControllerPlugged(int slot) { return slot >= 0 && slot < kPlayers && RawPad(slot).connected; }

int KeyboardPort() {
    for (int i = 0; i < kPlayers; i++)
        if (Opt().players[size_t(i)].keyboard) return i;
    return -1;
}

}  // namespace game

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"007: Nightfire - XBE Recomp", // window, splash and compiler
    .short_title = L"007 Nightfire",        // message boxes
    .name = L"007Nightfire",                // window classes, crash dumps, handover event
    .log = "007nightfire",                  // log file prefix
    .settings = "settings.toml",            // settings file beside the exe
    .splash = L"splash",                    // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "nightfire"},  // each recompiled xbe and its runtime, which nightfire.exe loads
               {"Driving.xbe", "driving"}},
    .options = {{"skip_intros", &game::Opt().skip_intros, nullptr},
                {"fps_counter", &game::Opt().fps_counter, nullptr},
                {"pause_on_focus_loss", &game::Opt().pause_on_focus_loss, nullptr},
                {"invert_mouse", &game::Opt().invert_mouse, nullptr},
                {"invert_mouse_x", &game::Opt().invert_mouse_x, nullptr},
                {"invert_pad_x", &game::Opt().invert_pad_x, nullptr},
                {"fov", nullptr, &game::Opt().fov},
                {"viewmodel_fov", nullptr, &game::Opt().viewmodel_fov},
                {"fmv_scale", nullptr, &game::Opt().fmv_scale},
                {"hdr_output", &game::Opt().hdr_output, nullptr},
                {"colour_blind", nullptr, &game::Opt().colour_blind},
                {"audio_output", nullptr, &game::Opt().audio_output},
                {"vibration_strength", nullptr, &game::Opt().vibration_strength},
                {"gyro_aiming", &game::Opt().gyro.enabled, nullptr},
                {"gyro_axis", nullptr, &game::Opt().gyro.axis},
                {"gyro_button", nullptr, &game::Opt().gyro.button},
                {"gyro_button_mode", nullptr, &game::Opt().gyro.button_mode},
                {"gyro_recenter_button", nullptr, &game::Opt().gyro.recenter_button},
                {"msaa", nullptr, &game::Opt().msaa},
                {"smaa", &game::Opt().smaa, nullptr},
                {"ambient_occlusion", nullptr, &game::Opt().ambient_occlusion}},  // game settings kept in the settings file
    .load = game::Load,                     // reads more from the settings file's toml::table
    .save = game::Save,                     // writes more into it
    .init = game::Init,                     // after the settings load, before the window and boot
    .loaded = nullptr,                      // image in memory, before its entry point runs
    .window = input::StartMouse,            // the game window is up
    .message = game::Message,               // window messages first; true takes one
    .ui = app::DrawUi,                      // ImGui drawing each frame
    .pad = game::Pad,                       // changes a polled pad
    .rumble = game::Rumble,                 // changes a rumble before it is set
};
