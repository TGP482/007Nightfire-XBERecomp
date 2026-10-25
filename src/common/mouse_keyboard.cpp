// the keyboard and mouse as player 1's actions in the game
#include "common/mouse_keyboard.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iterator>
#include <mutex>
#include <string_view>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "app/app.h"
#include "common/menu.h"
#include "common/options.h"

namespace input {

namespace {

HWND g_hwnd = nullptr;
std::atomic<int> g_dx{0};
std::atomic<int> g_dy{0};
std::atomic<uint64_t> g_take_ms{0};     // TakeMouse, asked every frame in play
std::atomic<uint64_t> g_capture_ms{0};  // PressedKey
std::atomic<bool> g_settle{false};
std::atomic<bool> g_keyboard{true};
std::atomic<bool> g_menu{false};
std::atomic<uint8_t> g_reload_bits{0};
std::atomic<uint8_t> g_action_bits{0};
constexpr uint8_t kHeld = 1;
constexpr uint8_t kPressed = 4;
constexpr uint64_t kPlayMs = 250;
constexpr uint64_t kTapMs = 80;
constexpr uint64_t kCaptureMs = 250;  // PressedKey asked this recently: a key capture is up
// The wheel binds as two keys, codes Windows leaves unassigned. A notch holds what is bound to it for one tap.
constexpr int kWheelUp = 0x0E;
constexpr int kWheelDown = 0x0F;
std::atomic<uint64_t> g_wheel_ms[2] = {};  // the last notch up, down

uint64_t NowMs() {
    auto since = std::chrono::steady_clock::now().time_since_epoch();
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(since).count()) + 1;
}

// A player's action values as the game's controller layout leaves them (0xDE720), the same for every layout: buttons
// 0 or 1, sticks -1 to 1.
enum Value {
    kTurn = 0,  // right positive
    kStrafe = 1,  // left positive
    kForward = 2,
    kLookX = 3,  // the look stick again, which moves the sight in manual aim
    kLookY = 4,
    kPitch = 5,  // up positive, before the game's Y-axis setting
    kZoom = 6,  // the dpad pair the zoom shares, down positive
    kJump = 7,
    kCrouch = 8,
    kFire = 9,
    kPrevGadget = 10,
    kNextGadget = 11,
    kAltFire = 12,
    kVision = 13,
    kPrevWeapon = 15,
    kNextWeapon = 16,
    kAim = 19,  // manual aim
    kAny = 25,  // A, B or start
    kMenuUp = 26,  // then down, left and right
    kStart = 30,
    kAccept = 31,
    kMenuY = 32,
    kMenuX = 33,
    kBack = 34,
};
static_assert(size_t(kBack) < kActionValues);

struct Target {
    Action action;
    int value;     // or one of the keys below, which add to no value
    float amount;  // added to the value while held
};
constexpr int kHostKey = -1;
// reload and action share value 14, which the game's reads of it learn from ReloadKey and ActionKey
constexpr int kReloadKey = -2;
constexpr int kActionKey = -3;
constexpr int kImageKey = -4;  // another image's own, which it reads through Held
constexpr int kWalk = -5;      // the move keys at kWalkSpeed while held
constexpr float kWalkSpeed = 0.5f;  // the stick half pushed
constexpr Target kActions[] = {
    {{"move_forward", "MOVE FORWARD", "W"}, kForward, 1},
    {{"move_back", "MOVE BACK", "S"}, kForward, -1},
    {{"strafe_left", "STRAFE LEFT", "A"}, kStrafe, 1},
    {{"strafe_right", "STRAFE RIGHT", "D"}, kStrafe, -1},
    {{"jump", "JUMP", "Space"}, kJump, 1},
    {{"crouch", "CROUCH", "C,Ctrl"}, kCrouch, 1},
    {{"walk", "WALK", "CapsLock"}, kWalk, 0},
    {{"fire", "FIRE", "LMB"}, kFire, 1},
    {{"aim", "MANUAL AIM", "RMB"}, kAim, 1},
    {{"alt_fire", "ALT FIRE", "MMB"}, kAltFire, 1},
    {{"reload", "RELOAD", "R"}, kReloadKey, 0},
    {{"action", "ACTION", "E"}, kActionKey, 0},
    {{"vision", "VISION MODE", "F"}, kVision, 1},
    {{"next_weapon", "NEXT WEAPON", "Q,Wheel Down"}, kNextWeapon, 1},
    {{"prev_weapon", "PREVIOUS WEAPON", "Wheel Up"}, kPrevWeapon, 1},
    {{"zoom_in", "ZOOM IN", "Wheel Up"}, kZoom, -1},
    {{"zoom_out", "ZOOM OUT", "Wheel Down"}, kZoom, 1},
    {{"next_gadget", "NEXT GADGET", "G"}, kNextGadget, 1},
    {{"prev_gadget", "PREVIOUS GADGET", ""}, kPrevGadget, 1},
    {{"pause", "PAUSE MENU", "Escape"}, kStart, 1},
    {{"menu_x", "MENU BUTTON X", "X"}, kMenuX, 1},
    {{"menu_y", "MENU BUTTON Y", "Y"}, kMenuY, 1},
    {{"settings_panel", "SETTINGS PANEL", "F10"}, kHostKey, 0},
    {{"toggle_hud", "TOGGLE HUD", ""}, kHostKey, 0},
    {{"look_up", "LOOK UP", "Up"}, kPitch, 1},
    {{"look_down", "LOOK DOWN", "Down"}, kPitch, -1},
    {{"look_left", "LOOK LEFT", "Left"}, kTurn, -1},
    {{"look_right", "LOOK RIGHT", "Right"}, kTurn, 1},
    // Driving.xbe's, the same key for any mode whose controls have the action
    {{"car_accelerate", "ACCELERATE", "W", "DRIVING"}, kImageKey, 0},
    {{"car_brake", "BRAKE / REVERSE", "S", "DRIVING"}, kImageKey, 0},
    {{"car_steer_left", "STEER LEFT", "A", "DRIVING"}, kImageKey, 0},
    {{"car_steer_right", "STEER RIGHT", "D", "DRIVING"}, kImageKey, 0},
    {{"car_steer_up", "STEER UP", "Shift", "DRIVING"}, kImageKey, 0},
    {{"car_steer_down", "STEER DOWN", "Ctrl", "DRIVING"}, kImageKey, 0},
    {{"car_handbrake", "HANDBRAKE", "Space", "DRIVING"}, kImageKey, 0},
    {{"car_fire", "FIRE", "LMB", "DRIVING"}, kImageKey, 0},
    {{"car_gadget", "USE GADGET", "RMB", "DRIVING"}, kImageKey, 0},
    {{"car_prev_weapon", "PREVIOUS WEAPON", "Q", "DRIVING"}, kImageKey, 0},
    {{"car_next_weapon", "NEXT WEAPON", "E", "DRIVING"}, kImageKey, 0},
    {{"car_camera", "CAMERA VIEW", "C", "DRIVING"}, kImageKey, 0},
    {{"car_look_back", "LOOK BACK", "Tab", "DRIVING"}, kImageKey, 0},
    {{"car_pause", "PAUSE MENU", "Escape", "DRIVING"}, kImageKey, 0},
    {{"car_aim_up", "TURRET AIM UP", "W", "DRIVING"}, kImageKey, 0},
    {{"car_aim_down", "TURRET AIM DOWN", "S", "DRIVING"}, kImageKey, 0},
    {{"car_aim_left", "TURRET AIM LEFT", "A", "DRIVING"}, kImageKey, 0},
    {{"car_aim_right", "TURRET AIM RIGHT", "D", "DRIVING"}, kImageKey, 0},
    {{"car_manual_aim", "TURRET MANUAL AIM", "RMB", "DRIVING"}, kImageKey, 0},
    {{"car_reload", "TURRET RELOAD", "R", "DRIVING"}, kImageKey, 0},
    {{"car_spin", "TURRET SPIN", "C", "DRIVING"}, kImageKey, 0},
    {{"car_zoom_in", "TURRET ZOOM IN", "Wheel Up", "DRIVING"}, kImageKey, 0},
    {{"car_zoom_out", "TURRET ZOOM OUT", "Wheel Down", "DRIVING"}, kImageKey, 0},
};
constexpr size_t kCount = std::size(kActions);
constexpr size_t HostAction(std::string_view id) {
    size_t i = 0;
    while (kActions[i].action.id != id) i++;
    return i;
}
constexpr size_t kPanelAction = HostAction("settings_panel");
constexpr size_t kHudAction = HostAction("toggle_hud");
constexpr size_t kWalkAction = HostAction("walk");
struct Name {
    int vk;
    const char* name;
};
constexpr Name kNames[] = {
    {VK_LBUTTON, "LMB"}, {VK_RBUTTON, "RMB"}, {VK_MBUTTON, "MMB"}, {VK_XBUTTON1, "Mouse4"}, {VK_XBUTTON2, "Mouse5"},
    {kWheelUp, "Wheel Up"}, {kWheelDown, "Wheel Down"},
    {VK_SPACE, "Space"}, {VK_RETURN, "Enter"}, {VK_ESCAPE, "Escape"}, {VK_BACK, "Backspace"}, {VK_TAB, "Tab"},
    {VK_SHIFT, "Shift"}, {VK_CONTROL, "Ctrl"}, {VK_MENU, "Alt"}, {VK_CAPITAL, "CapsLock"},
    {VK_UP, "Up"}, {VK_DOWN, "Down"}, {VK_LEFT, "Left"}, {VK_RIGHT, "Right"},
    {VK_INSERT, "Insert"}, {VK_DELETE, "Delete"}, {VK_HOME, "Home"}, {VK_END, "End"},
    {VK_PRIOR, "PageUp"}, {VK_NEXT, "PageDown"},
    {'A', "A"}, {'B', "B"}, {'C', "C"}, {'D', "D"}, {'E', "E"}, {'F', "F"}, {'G', "G"}, {'H', "H"}, {'I', "I"},
    {'J', "J"}, {'K', "K"}, {'L', "L"}, {'M', "M"}, {'N', "N"}, {'O', "O"}, {'P', "P"}, {'Q', "Q"}, {'R', "R"},
    {'S', "S"}, {'T', "T"}, {'U', "U"}, {'V', "V"}, {'W', "W"}, {'X', "X"}, {'Y', "Y"}, {'Z', "Z"},
    {'0', "0"}, {'1', "1"}, {'2', "2"}, {'3', "3"}, {'4', "4"}, {'5', "5"}, {'6', "6"}, {'7', "7"}, {'8', "8"},
    {'9', "9"},
    {VK_F1, "F1"}, {VK_F2, "F2"}, {VK_F4, "F4"}, {VK_F5, "F5"}, {VK_F6, "F6"}, {VK_F7, "F7"},
    {VK_F8, "F8"}, {VK_F9, "F9"}, {VK_F10, "F10"}, {VK_F11, "F11"}, {VK_F12, "F12"},
    {VK_NUMPAD0, "Numpad0"}, {VK_NUMPAD1, "Numpad1"}, {VK_NUMPAD2, "Numpad2"}, {VK_NUMPAD3, "Numpad3"},
    {VK_NUMPAD4, "Numpad4"}, {VK_NUMPAD5, "Numpad5"}, {VK_NUMPAD6, "Numpad6"}, {VK_NUMPAD7, "Numpad7"},
    {VK_NUMPAD8, "Numpad8"}, {VK_NUMPAD9, "Numpad9"}, {VK_ADD, "NumpadPlus"}, {VK_SUBTRACT, "NumpadMinus"},
    {VK_MULTIPLY, "NumpadStar"}, {VK_DIVIDE, "NumpadSlash"}, {VK_DECIMAL, "NumpadPeriod"},
    {VK_OEM_3, "Backtick"}, {VK_OEM_MINUS, "Minus"}, {VK_OEM_PLUS, "Equals"}, {VK_OEM_4, "LBracket"},
    {VK_OEM_6, "RBracket"}, {VK_OEM_5, "Backslash"}, {VK_OEM_1, "Semicolon"}, {VK_OEM_7, "Quote"},
    {VK_OEM_COMMA, "Comma"}, {VK_OEM_PERIOD, "Period"}, {VK_OEM_2, "Slash"},
};

// not bindable: in the game's menus the arrows are the dpad, Enter accepts, Backspace backs out
constexpr int kFixed[] = {VK_UP, VK_DOWN, VK_LEFT, VK_RIGHT, VK_RETURN, VK_BACK};

// US layout scan codes of the keys that move with the layout: a bind names the US key at that position
struct Position {
    int vk;
    UINT scan;
};
constexpr Position kPositions[] = {
    {'1', 0x02}, {'2', 0x03}, {'3', 0x04}, {'4', 0x05}, {'5', 0x06}, {'6', 0x07}, {'7', 0x08}, {'8', 0x09},
    {'9', 0x0A}, {'0', 0x0B}, {VK_OEM_MINUS, 0x0C}, {VK_OEM_PLUS, 0x0D},
    {'Q', 0x10}, {'W', 0x11}, {'E', 0x12}, {'R', 0x13}, {'T', 0x14}, {'Y', 0x15}, {'U', 0x16}, {'I', 0x17},
    {'O', 0x18}, {'P', 0x19}, {VK_OEM_4, 0x1A}, {VK_OEM_6, 0x1B},
    {'A', 0x1E}, {'S', 0x1F}, {'D', 0x20}, {'F', 0x21}, {'G', 0x22}, {'H', 0x23}, {'J', 0x24}, {'K', 0x25},
    {'L', 0x26}, {VK_OEM_1, 0x27}, {VK_OEM_7, 0x28}, {VK_OEM_3, 0x29}, {VK_OEM_5, 0x2B},
    {'Z', 0x2C}, {'X', 0x2D}, {'C', 0x2E}, {'V', 0x2F}, {'B', 0x30}, {'N', 0x31}, {'M', 0x32},
    {VK_OEM_COMMA, 0x33}, {VK_OEM_PERIOD, 0x34}, {VK_OEM_2, 0x35},
};

using Binds = std::array<std::vector<int>, kCount>;
std::mutex g_bind_lock;
Binds g_binds;

int Vk(const std::string& name) {
    for (const Name& n : kNames)
        if (_stricmp(n.name, name.c_str()) == 0) return n.vk;
    return 0;
}

const char* NameOf(int vk) {
    for (const Name& n : kNames)
        if (n.vk == vk) return n.name;
    return nullptr;
}

UINT ScanOf(int vk) {
    for (const Position& p : kPositions)
        if (p.vk == vk) return p.scan;
    return 0;
}

// the keyboard layout the player types with, switched per window thread
HKL Layout() { return GetKeyboardLayout(GetWindowThreadProcessId(g_hwnd, nullptr)); }

// the key the current layout has at the bind's position
int LiveVk(int vk) {
    UINT scan = ScanOf(vk);
    if (!scan) return vk;
    UINT live = MapVirtualKeyExW(scan, MAPVK_VSC_TO_VK_EX, Layout());
    return live ? int(live) : vk;
}

// a key the window was sent, as the position bound to it
int PositionVk(int live) {
    if (NameOf(live) && !ScanOf(live)) return live;
    UINT scan = MapVirtualKeyExW(UINT(live), MAPVK_VK_TO_VSC, Layout());
    for (const Position& p : kPositions)
        if (p.scan == scan) return p.vk;
    return live;
}

// the key as the player's keyboard labels it
std::string Label(int vk) {
    int live = LiveVk(vk);
    if (live == vk) return NameOf(vk);
    bool alnum = (live >= 'A' && live <= 'Z') || (live >= '0' && live <= '9');
    if (alnum) return std::string(1, char(live));
    // the top bit flags a dead key
    UINT c = MapVirtualKeyExW(UINT(live), MAPVK_VK_TO_CHAR, Layout()) & 0x7FFF;
    // a comma would split the key list
    if (c == ',') return NameOf(VK_OEM_COMMA);
    if (c > ' ' && c < 0x7F) return std::string(1, char(c));
    return NameOf(vk);
}

std::vector<int> Parse(const std::string& list) {
    std::vector<int> out;
    size_t start = 0;
    while (start <= list.size()) {
        size_t comma = list.find(',', start);
        if (comma == std::string::npos) comma = list.size();
        if (int vk = Vk(list.substr(start, comma - start))) out.push_back(vk);
        start = comma + 1;
    }
    return out;
}

// the saved names, or the player's labels for the panel
std::string Join(const std::vector<int>& vks, bool labels = false) {
    std::string s;
    for (int vk : vks) {
        if (!s.empty()) s += ',';
        s += labels ? Label(vk) : NameOf(vk);
    }
    return s;
}

bool Foreground() { return g_hwnd && GetForegroundWindow() == g_hwnd; }
bool Active() { return Foreground() && !app::MenuOpen(); }
bool Wheel(int vk) { return vk == kWheelUp || vk == kWheelDown; }
bool Down(int vk) {
    if (Wheel(vk)) return NowMs() - g_wheel_ms[vk == kWheelDown] < kTapMs;
    return GetAsyncKeyState(LiveVk(vk)) < 0;
}
bool Mouse(int vk) {
    bool button = vk == VK_LBUTTON || vk == VK_RBUTTON || vk == VK_MBUTTON || vk == VK_XBUTTON1 || vk == VK_XBUTTON2;
    return button || Wheel(vk);
}
bool Play() { return NowMs() - g_take_ms < kPlayMs; }
bool KeyCaptureUp() { return NowMs() - g_capture_ms <= kCaptureMs; }

// the cursor shows in the game's menus only while the keyboard and mouse are in use
void SetKeyboard(bool on) {
    if (g_keyboard.exchange(on) != on) app::RefreshCursor();
}

void Clip() {
    RECT r;
    RECT cur;
    GetClientRect(g_hwnd, &r);
    MapWindowPoints(g_hwnd, nullptr, reinterpret_cast<POINT*>(&r), 2);
    if (!GetClipCursor(&cur) || !EqualRect(&r, &cur)) ClipCursor(&r);
}

// the action is bound to this key and no key capture wants it
bool HostKeyBound(size_t action, int vk) {
    if (KeyCaptureUp()) return false;
    std::lock_guard lock(g_bind_lock);
    const auto& b = g_binds[action];
    return std::find(b.begin(), b.end(), PositionVk(vk)) != b.end();
}

// keys held from before the game had the input wait for release; true once none is held
bool UpdateSettle(const Binds& binds) {
    if (!g_settle) return true;
    for (auto& b : binds)
        for (int vk : b)
            if (Down(vk)) return false;
    for (int vk : kFixed)
        if (Down(vk)) return false;
    g_settle = false;
    return true;
}

struct Use {
    bool reload = false;
    bool action = false;
    bool any = false;  // a bound key
};

bool Moves(int value) { return value == kForward || value == kStrafe; }

// default.xbe's walk (0xA9CF0): forward 0.005 + 0.1 f^2, back 0.005 + 0.075 f^2, strafe 0.075 s, each at most 0.1
constexpr float kMoveBase = 0.005f;
constexpr float kForwardCurve = 0.1f;
constexpr float kBackCurve = 0.075f;
constexpr float kStrafeRate = 0.075f;
constexpr float kMoveMost = 0.1f;

float ForwardSpeed(float forward) {
    float curve = forward > 0 ? kForwardCurve : kBackCurve;
    return std::min(kMoveBase + curve * forward * forward, kMoveMost);
}

// the forward amount the walk turns into this speed
float ForwardFor(float speed, bool back) {
    float curve = back ? kBackCurve : kForwardCurve;
    return std::sqrt(std::max(speed - kMoveBase, 0.0f) / curve);
}

// a move key and a strafe key together go as fast as the faster of the two alone, in the direction they gave
void LimitDiagonal(float* values) {
    float forward = values[kForward];
    float strafe = values[kStrafe];
    if (forward == 0.0f || strafe == 0.0f) return;
    float ahead = ForwardSpeed(forward);
    float across = std::min(std::abs(strafe) * kStrafeRate, kMoveMost);
    float scale = std::max(ahead, across) / std::hypot(ahead, across);
    values[kForward] = std::copysign(ForwardFor(ahead * scale, forward < 0.0f), forward);
    values[kStrafe] = std::copysign(across * scale / kStrafeRate, strafe);
}

Use PressActions(const Binds& binds, bool menu, float* values) {
    static bool pause_held;
    static bool pause_backs;
    bool pause = false;
    bool walk = std::any_of(binds[kWalkAction].begin(), binds[kWalkAction].end(), Down);
    Use use;
    for (size_t i = 0; i < kCount; i++) {
        const Target& t = kActions[i];
        if (t.value == kHostKey || t.value == kImageKey) continue;
        if (!std::any_of(binds[i].begin(), binds[i].end(), Down)) continue;
        use.any = true;
        if (t.value == kWalk) continue;
        if (t.value == kReloadKey || t.value == kActionKey) {
            (t.value == kReloadKey ? use.reload : use.action) = true;
            continue;
        }
        if (t.value == kStart) {
            // pressed in a menu it backs out of it, and does nothing more once the menu has gone
            if (!pause_held) pause_backs = menu;
            pause = true;
            if (pause_backs && !menu) continue;
            values[pause_backs ? kBack : kStart] += 1.0f;
            values[kAny] += 1.0f;
            continue;
        }
        values[t.value] += walk && Moves(t.value) ? t.amount * kWalkSpeed : t.amount;
        if (t.value == kTurn) values[kLookX] += t.amount;
        if (t.value == kPitch) values[kLookY] += t.amount;
    }
    pause_held = pause;
    LimitDiagonal(values);
    return use;
}

// the bound keys held this frame into the values, and the reload and action keys held
Use Press(float* values, bool menu) {
    // a bind started in the pause menu takes the next key, and the key that bound waits for release
    if (!Active() || app::Capturing()) {
        g_settle = true;
        return {};
    }
    Binds binds;
    {
        std::lock_guard lock(g_bind_lock);
        binds = g_binds;
    }
    if (!UpdateSettle(binds)) return {};
    Use use = PressActions(binds, menu, values);
    if (use.any || std::any_of(std::begin(kFixed), std::end(kFixed), Down)) SetKeyboard(true);
    // in the game's menus the wheel scrolls and the right button backs out, as on the siblings
    if (g_menu) {
        values[kMenuUp] += float(Down(kWheelUp));
        values[kMenuUp + 1] += float(Down(kWheelDown));
        if (Down(VK_RBUTTON)) {
            values[kBack] += 1.0f;
            values[kAny] += 1.0f;
        }
    }
    // the arrows, kFixed's first four, are the menus' directions in their order
    for (int i = 0; i < 4; i++) values[kMenuUp + i] += float(Down(kFixed[i]));
    if (Down(VK_RETURN)) {
        values[kAccept] += 1.0f;
        values[kAny] += 1.0f;
    }
    if (Down(VK_BACK)) {
        values[kBack] += 1.0f;
        values[kAny] += 1.0f;
    }
    return use;
}

void UpdateBits(std::atomic<uint8_t>& bits, bool held) {
    bool was = bits & kHeld;
    bits = uint8_t(held ? kHeld | (was ? 0 : kPressed) : 0);
}

}  // namespace

void StartMouse(void* hwnd) {
    g_hwnd = static_cast<HWND>(hwnd);
    RAWINPUTDEVICE mouse{1, 2, 0, g_hwnd};  // generic desktop page, mouse usage
    RegisterRawInputDevices(&mouse, 1, sizeof(mouse));
}

void OnRawInput(void* handle) {
    RAWINPUT ri;
    UINT size = sizeof(ri);
    HRAWINPUT input = static_cast<HRAWINPUT>(handle);
    if (GetRawInputData(input, RID_INPUT, &ri, &size, sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
    if (ri.header.dwType != RIM_TYPEMOUSE) return;
    const RAWMOUSE& m = ri.data.mouse;
    // noted while the settings panel has the input too, so its key capture sees the wheel
    if ((m.usButtonFlags & RI_MOUSE_WHEEL) && Foreground()) g_wheel_ms[SHORT(m.usButtonData) < 0] = NowMs();
    if (!Active()) return;
    SetKeyboard(true);
    if (!Play()) {
        ClipCursor(nullptr);
        return;
    }
    Clip();
    if (!(m.usFlags & MOUSE_MOVE_ABSOLUTE)) {
        g_dx += m.lLastX;
        g_dy += m.lLastY;
    }
}

void Settle() {
    ClipCursor(nullptr);
    g_settle = true;
}

void Keyboard(float* values, bool menu) {
    Use use = Press(values, menu);
    UpdateBits(g_reload_bits, use.reload);
    UpdateBits(g_action_bits, use.action);
}

uint8_t ReloadKey() { return g_reload_bits; }
uint8_t ActionKey() { return g_action_bits; }

bool TakeMouse(float& dx, float& dy) {
    uint64_t now = NowMs();
    bool stale = now - g_take_ms.exchange(now) >= kPlayMs;
    int x = g_dx.exchange(0);
    int y = g_dy.exchange(0);
    if ((!x && !y) || stale || !Active()) return false;
    const game::Options& o = game::Opt();
    dx = float(o.invert_mouse_x ? -x : x) * o.mouse_sensitivity;
    dy = float(o.invert_mouse ? -y : y) * o.mouse_vertical_sensitivity;
    return true;
}

void SeenPad(const Pad& p) {
    constexpr int16_t kStickPushed = 16000;
    constexpr uint8_t kAnalogPushed = 0x20;
    auto pushed = [](int16_t v) { return v > kStickPushed || v < -kStickPushed; };
    auto pressed = [](uint8_t a) { return a > kAnalogPushed; };
    bool analog = std::any_of(std::begin(p.analog), std::end(p.analog), pressed);
    bool sticks = pushed(p.lx) || pushed(p.ly) || pushed(p.rx) || pushed(p.ry);
    if (p.buttons || analog || sticks) SetKeyboard(false);
}

bool KeyboardActive() { return g_keyboard; }

void SetMenu(bool menu) {
    if (g_menu.exchange(menu) != menu) app::RefreshCursor();
}

bool CursorShown() { return g_menu && g_keyboard; }

namespace {

// the frame's place in the window's client area, in the middle as the renderer's BlitFrame draws it
struct Shown {
    float left;
    float top;
    float width;
    float height;
};

bool ShownFrame(Shown& s) {
    RECT r;
    if (!GetClientRect(g_hwnd, &r) || r.right <= 0 || r.bottom <= 0) return false;
    float aspect = app::Widescreen() ? 16.0f / 9.0f : 4.0f / 3.0f;
    float w = float(r.right);
    float h = float(r.bottom);
    s.width = std::min(w, h * aspect);
    s.height = s.width / aspect;
    s.left = (w - s.width) / 2;
    s.top = (h - s.height) / 2;
    return true;
}

}  // namespace

bool Cursor(float& x, float& y) {
    POINT p;
    Shown s;
    if (!Active() || !CursorShown()) return false;
    if (!GetCursorPos(&p) || !ScreenToClient(g_hwnd, &p) || !ShownFrame(s)) return false;
    x = (float(p.x) - s.left) / s.width;
    y = (float(p.y) - s.top) / s.height;
    return true;
}

bool LeftButton() { return Active() && !g_settle && Down(VK_LBUTTON); }

bool Held(std::string_view id) {
    if (!Active()) return false;
    std::lock_guard lock(g_bind_lock);
    for (size_t i = 0; i < kCount; i++) {
        if (kActions[i].action.id != id) continue;
        bool held = std::any_of(g_binds[i].begin(), g_binds[i].end(), Down);
        if (held) SetKeyboard(true);
        return held;
    }
    return false;
}

const Action* Actions(size_t* count) {
    static const auto list = [] {
        std::array<Action, kCount> a{};
        for (size_t i = 0; i < kCount; i++) a[i] = kActions[i].action;
        return a;
    }();
    *count = kCount;
    return list.data();
}

std::string Binding(size_t action) {
    std::lock_guard lock(g_bind_lock);
    return action < kCount ? Join(g_binds[action], true) : std::string();
}

void Bind(size_t action, int vk, int slot) {
    if (action >= kCount || slot < 0 || slot > 1 || (vk && !NameOf(vk))) return;
    // the host keys act on the window's key messages, which mouse buttons do not send
    if (kActions[action].value == kHostKey && Mouse(vk)) return;
    {
        std::lock_guard lock(g_bind_lock);
        std::vector<int>& keys = g_binds[action];
        int slots[2] = {0, 0};
        for (size_t i = 0; i < keys.size() && i < 2; i++) slots[i] = keys[i];
        if (vk && slots[1 - slot] == vk) slots[1 - slot] = 0;
        slots[slot] = vk;
        // the settings panel with no key would leave a keyboard player no way back into it
        if (!slots[0] && !slots[1] && action == kPanelAction) return;
        keys.clear();
        for (int k : slots) {
            if (k) keys.push_back(k);
        }
        game::Opt().keys[kActions[action].action.id] = Join(keys);
    }
    app::Save();
}

void ResetBindings() {
    game::Opt().keys.clear();
    LoadBindings();
    app::Save();
}

void LoadBindings() {
    std::lock_guard lock(g_bind_lock);
    const auto& keys = game::Opt().keys;
    for (size_t i = 0; i < kCount; i++) {
        auto it = keys.find(kActions[i].action.id);
        g_binds[i] = Parse(it != keys.end() ? it->second : kActions[i].action.defaults);
    }
}

int PressedKey() {
    g_capture_ms = NowMs();
    for (const Name& n : kNames)
        if (Down(n.vk)) return n.vk;
    return 0;
}

bool PanelKey(int vk) { return HostKeyBound(kPanelAction, vk); }

bool HudKey(int vk) { return HostKeyBound(kHudAction, vk); }

}  // namespace input
