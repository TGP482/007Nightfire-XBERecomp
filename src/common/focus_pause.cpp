// pause on focus loss: the game thread waits while the window is in the background or minimized
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <mutex>

#include "app/app.h"
#include "audio/output.h"
#include "common/options.h"
#include "core/log.h"
#include "cpu/x86_ops.h"

namespace game {

namespace {

constexpr DWORD kPollMs = 20;

std::mutex g_clock_lock;
uint64_t g_paused_ticks = 0;  // spent in focus pauses so far
uint64_t g_pause_start = 0;   // the count a pause started at, 0 while running

// a hidden window, behind the splash or for a handover, never pauses: it opens from the game thread
bool Unfocused() {
    HWND hwnd = static_cast<HWND>(app::Window());
    if (!hwnd || !IsWindowVisible(hwnd)) return false;
    return GetForegroundWindow() != hwnd || IsIconic(hwnd);
}

void HoldClock(bool held) {
    std::lock_guard<std::mutex> lock(g_clock_lock);
    uint64_t now = x86_rdtsc();
    if (held) {
        g_pause_start = now;
        return;
    }
    g_paused_ticks += now - g_pause_start;
    g_pause_start = 0;
}

}  // namespace

void WaitWhileUnfocused() {
    if (!Opt().pause_on_focus_loss || !Unfocused()) return;
    LOGR("focus lost: paused");
    HoldClock(true);
    audio::SetPaused(true);
    while (Opt().pause_on_focus_loss && Unfocused()) Sleep(kPollMs);
    audio::SetPaused(false);
    HoldClock(false);
    LOGR("focus back: resumed");
}

uint64_t GuestTicks() {
    std::lock_guard<std::mutex> lock(g_clock_lock);
    uint64_t now = g_pause_start ? g_pause_start : x86_rdtsc();
    return now - g_paused_ticks;
}

}  // namespace game
