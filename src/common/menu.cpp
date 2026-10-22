// the options panel on F10
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <imgui.h>

#include "app/app.h"
#include "common/menu.h"

namespace app {

namespace {

std::atomic<bool> g_open{false};

// The display mode and resolution wait for Apply, as Windows' display settings do; closing drops them.
bool g_display_held = false;
Settings g_display;

// the game's own pause menu: cream text, orange bars on a dark red panel
constexpr ImVec4 kPanelBg{0.13f, 0.03f, 0.04f, 0.94f};
constexpr ImVec4 kBorder{0.74f, 0.32f, 0.11f, 0.85f};
constexpr ImVec4 kText{1.00f, 0.85f, 0.69f, 1.0f};
constexpr ImVec4 kHighlight{0.27f, 0.12f, 0.08f, 1.0f};  // the title bar's orange at a quarter over the panel
constexpr ImVec4 kHeading{0.78f, 0.52f, 0.23f, 1.0f};
constexpr ImVec4 kField{0.22f, 0.06f, 0.05f, 1.0f};
constexpr ImVec4 kFieldHovered{0.30f, 0.07f, 0.06f, 1.0f};
constexpr ImVec4 kButton{0.30f, 0.07f, 0.06f, 1.0f};
// lighter than the highlight, so it shows on a selected row
constexpr ImVec4 kButtonHovered{0.41f, 0.20f, 0.11f, 1.0f};
constexpr ImVec4 kLine{0.78f, 0.52f, 0.23f, 0.35f};

constexpr std::pair<ImGuiCol, ImVec4> kPanelColors[] = {
    {ImGuiCol_WindowBg, kPanelBg},
    {ImGuiCol_Border, kBorder},
    {ImGuiCol_Text, kText},
    {ImGuiCol_Header, kHighlight},
    {ImGuiCol_HeaderHovered, kHighlight},
    {ImGuiCol_HeaderActive, kHighlight},
    {ImGuiCol_Button, kButton},
    {ImGuiCol_ButtonHovered, kButtonHovered},
    {ImGuiCol_ButtonActive, kButtonHovered},
    {ImGuiCol_FrameBg, kField},
    {ImGuiCol_FrameBgHovered, kFieldHovered},
    {ImGuiCol_FrameBgActive, kHighlight},
    {ImGuiCol_ScrollbarGrab, kHighlight},
    {ImGuiCol_ScrollbarGrabHovered, kButtonHovered},
    {ImGuiCol_ScrollbarGrabActive, kHeading},
    {ImGuiCol_Separator, kLine},
    {ImGuiCol_NavHighlight, kHeading},
};

constexpr uint32_t kAnisoValues[] = {0, 2, 4, 8, 16};
const char* const kAnisoNames[] = {"GAME DEFAULT", "2x", "4x", "8x", "16x"};
constexpr uint32_t kFpsCaps[] = {30, 60, 75, 90, 120, 144, 165, 240, 360, 0};
const char* const kDisplayModeNames[] = {"FULLSCREEN", "BORDERLESS", "WINDOWED"};
constexpr int kDisplayModeCount = int(std::size(kDisplayModeNames));

constexpr int kRenderMin = 20;
constexpr int kRenderMax = 200;
constexpr int kRenderStep = 5;
constexpr int kRenderSlots = (kRenderMax - kRenderMin) / kRenderStep + 1;

constexpr DWORD kMinModeWidth = 640;
constexpr DWORD kMinModeBits = 32;

constexpr float kValueEms = 12.0f;  // width of the value between the arrows
constexpr float kButtonEms = 8.0f;
constexpr int kFooterButtons = 3;
constexpr int kArrowCount = 2;
constexpr float kHeadingGapEms = 0.5f;
constexpr float kHeadingAfterEms = 0.2f;
constexpr float kPaddingXEms = 1.6f;
constexpr float kPaddingYEms = 1.2f;
constexpr float kBorderSize = 2.0f;
constexpr float kWindowWidthEms = 50.0f;
constexpr float kWindowHeightEms = 32.0f;
constexpr float kRepeatSeconds = 0.1f;  // held arrows step ten times a second
constexpr ImGuiWindowFlags kWindowFlags = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                                         ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoTitleBar;
constexpr float kMinFontScale = 1.0f;
constexpr float kFontScaleLines = 720.0f;  // the font grows with the output height past this many lines

int StepIndex(int index, int step, int count) {
    if (count <= 0) return 0;
    index = (index + step) % count;
    return index < 0 ? index + count : index;
}

template <size_t N>
int NearestIndex(const uint32_t (&values)[N], uint32_t value) {
    int best = 0;
    uint32_t best_distance = UINT32_MAX;
    for (int i = 0; i < int(N); i++) {
        uint32_t distance = values[i] > value ? values[i] - value : value - values[i];
        if (distance < best_distance) {
            best_distance = distance;
            best = i;
        }
    }
    return best;
}

// the table index nearest to value, after stepping value to the next entry when step is not zero
template <size_t N>
int StepTable(const uint32_t (&values)[N], uint32_t& value, int step) {
    int index = NearestIndex(values, value);
    if (step == 0) return index;
    index = StepIndex(index, step, int(N));
    value = values[index];
    return index;
}

bool SameMode(Mode a, Mode b) { return a.width == b.width && a.height == b.height; }

// every mode the monitor reports, smallest first so Left steps down
const std::vector<Mode>& Modes(const std::string& device) {
    static std::map<std::string, std::vector<Mode>> cache;
    auto [it, fresh] = cache.try_emplace(device);
    std::vector<Mode>& list = it->second;
    if (!fresh) return list;
    std::wstring wide(device.begin(), device.end());
    const wchar_t* name = wide.empty() ? nullptr : wide.c_str();
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    for (DWORD i = 0; EnumDisplaySettingsW(name, i, &dm); i++) {
        if (dm.dmBitsPerPel < kMinModeBits || dm.dmPelsWidth < kMinModeWidth) continue;
        Mode mode{dm.dmPelsWidth, dm.dmPelsHeight};
        bool known = std::any_of(list.begin(), list.end(), [&](Mode other) { return SameMode(other, mode); });
        if (!known) list.push_back(mode);
    }
    auto smaller = [](Mode a, Mode b) {
        if (a.width != b.width) return a.width < b.width;
        return a.height < b.height;
    };
    std::sort(list.begin(), list.end(), smaller);
    return list;
}

// the monitor the resolution row lists: the game window's, as the host's display apply uses
struct ModeMonitor {
    std::string device;
    Mode desktop;
};

ModeMonitor ShownMonitor() {
    MONITORINFOEXW mi{};
    mi.cbSize = sizeof(mi);
    std::string device;
    HMONITOR monitor = MonitorFromWindow(static_cast<HWND>(Window()), MONITOR_DEFAULTTOPRIMARY);
    if (GetMonitorInfoW(monitor, &mi))
        for (const wchar_t* c = mi.szDevice; *c; c++) device += char(*c);
    return {device, Desktop()};
}

Mode ShownMode(const Settings& s, Mode desktop) {
    bool custom = s.width != 0 && s.height != 0;
    return custom ? Mode{s.width, s.height} : desktop;
}

int ModeIndex(const std::vector<Mode>& modes, Mode mode) {
    int index = 0;
    for (size_t i = 0; i < modes.size(); i++) {
        if (SameMode(modes[i], mode)) index = int(i);
    }
    return index;
}

// Row steps. Each function steps its setting when step is not zero, then returns the text the row shows.

std::string OnOff(bool on) { return on ? "ON" : "OFF"; }

std::string StepDisplayMode(Settings& s, int step) {
    if (step) s.display_mode = DisplayMode(StepIndex(int(s.display_mode), step, kDisplayModeCount));
    return kDisplayModeNames[std::min(int(s.display_mode), kDisplayModeCount - 1)];
}

void StepResolutionMode(Settings& s, int step) {
    ModeMonitor monitor = ShownMonitor();
    const std::vector<Mode>& modes = Modes(monitor.device);
    if (modes.empty()) return;
    int next = StepIndex(ModeIndex(modes, ShownMode(s, monitor.desktop)), step, int(modes.size()));
    // the desktop's own mode is kept as 0, which follows the desktop
    Mode picked = modes[size_t(next)];
    bool native = SameMode(picked, monitor.desktop);
    s.width = native ? 0 : picked.width;
    s.height = native ? 0 : picked.height;
}

std::string StepResolution(Settings& s, int step) {
    if (step) StepResolutionMode(s, step);
    Mode mode = ShownMode(s, ShownMonitor().desktop);
    return std::to_string(mode.width) + "x" + std::to_string(mode.height);
}

std::string StepRenderResolution(Settings& s, int step) {
    if (step) {
        int percent = std::clamp(int(s.render_percent), kRenderMin, kRenderMax);
        int slot = StepIndex((percent - kRenderMin) / kRenderStep, step, kRenderSlots);
        s.render_percent = uint32_t(kRenderMin + kRenderStep * slot);
    }
    Mode out = Output();
    std::string size = std::to_string(out.width * s.render_percent / 100) + "x";
    size += std::to_string(out.height * s.render_percent / 100);
    return std::to_string(s.render_percent) + "% (" + size + ")";
}

std::string StepVsync(Settings& s, int step) {
    if (step) s.vsync = !s.vsync;
    return OnOff(s.vsync);
}

std::string StepMaxFps(Settings& s, int step) {
    uint32_t cap = kFpsCaps[StepTable(kFpsCaps, s.fps_cap, step)];
    return cap ? std::to_string(cap) : "UNLIMITED";
}

std::string StepAniso(Settings& s, int step) { return kAnisoNames[StepTable(kAnisoValues, s.aniso, step)]; }

// The rows. A value row's id picks its step, held rows wait for Apply.
using StepFn = std::string (*)(Settings& s, int step);

enum RowId {
    kDisplayModeRow,
    kResolutionRow,
    kRenderResolutionRow,
    kVsyncRow,
    kMaxFpsRow,
    kAnisoRow,
};

// in RowId order
constexpr StepFn kSteps[] = {
    StepDisplayMode,
    StepResolution,
    StepRenderResolution,
    StepVsync,
    StepMaxFps,
    StepAniso,
};

bool Held(int id) { return id == kDisplayModeRow || id == kResolutionRow; }

enum class Kind { Heading, Value };
struct Row {
    Kind kind;
    int id;
    const char* label;
};

constexpr Row kSettingsRows[] = {
    {Kind::Heading, -1, "DISPLAY"},
    {Kind::Value, kDisplayModeRow, "DISPLAY MODE"},
    {Kind::Value, kResolutionRow, "RESOLUTION"},
    {Kind::Value, kRenderResolutionRow, "RENDER RESOLUTION"},
    {Kind::Value, kVsyncRow, "VERTICAL SYNC"},
    {Kind::Value, kMaxFpsRow, "MAX FRAME RATE"},
    {Kind::Heading, -1, "GRAPHICS"},
    {Kind::Value, kAnisoRow, "ANISOTROPIC FILTERING"},
};

bool SameDisplay(const Settings& a, const Settings& b) {
    return a.display_mode == b.display_mode && a.width == b.width && a.height == b.height;
}

void CopyDisplay(Settings& to, const Settings& from) {
    to.display_mode = from.display_mode;
    to.width = from.width;
    to.height = from.height;
}

void ApplyLiveStep(const Row& row, int step) {
    kSteps[row.id](Get(), step);
    Save();
}

void ApplyHeldStep(const Row& row, int step) {
    const Settings& live = Get();
    if (!g_display_held) CopyDisplay(g_display, live);
    kSteps[row.id](g_display, step);
    g_display_held = !SameDisplay(g_display, live);
}

void Apply() {
    CopyDisplay(Get(), g_display);
    g_display_held = false;
    ApplyDisplay();
    Save();
}

void RestoreDefaults() {
    Settings& s = Get();
    std::string join_ip = s.join_ip;
    s = Settings{};
    s.join_ip = join_ip;
    g_display_held = false;
    ApplyDisplay();
    Save();
}

// Drawing. A row has the label left, then an arrow either side of the value on the right.

struct RowLayout {
    ImVec2 origin;
    float width;
    float height;
    float value_width;
    float block_x;  // left edge of the first arrow
    float value_x;
    float inc_x;    // the second arrow
};

RowLayout MeasureRow() {
    const ImGuiStyle& style = ImGui::GetStyle();
    RowLayout row;
    row.origin = ImGui::GetCursorPos();
    row.width = ImGui::GetContentRegionAvail().x;
    row.height = ImGui::GetFrameHeight();
    row.value_width = ImGui::GetFontSize() * kValueEms;
    float block_width = row.height * kArrowCount + row.value_width + style.ItemSpacing.x * kArrowCount;
    row.block_x = row.origin.x + row.width - block_width;
    row.value_x = row.block_x + row.height + style.ItemSpacing.x;
    row.inc_x = row.value_x + row.value_width + style.ItemSpacing.x;
    return row;
}

// the row is the only nav stop, filled while focused
void DrawNavStop(const RowLayout& row) {
    ImGui::Selectable("##row", false, ImGuiSelectableFlags_AllowOverlap, ImVec2(row.width, row.height));
    if (ImGui::IsItemFocused()) {
        ImU32 fill = ImGui::GetColorU32(ImGuiCol_Header);
        ImGui::GetWindowDrawList()->AddRectFilled(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), fill);
    }
}

// the focused row steps on the left and right keys
int FocusedStep(const RowLayout& row) {
    DrawNavStop(row);
    if (!ImGui::IsItemFocused()) return 0;
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadLeft)) return -1;
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) || ImGui::IsKeyPressed(ImGuiKey_GamepadDpadRight)) return 1;
    return 0;
}

void DrawLabel(const RowLayout& row, const char* label) {
    ImGui::SetCursorPos(ImVec2(row.origin.x + ImGui::GetStyle().ItemInnerSpacing.x, row.origin.y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
}

void DrawValueText(const RowLayout& row, const char* value) {
    float centred = (row.value_width - ImGui::CalcTextSize(value).x) * 0.5f;
    ImGui::SetCursorPos(ImVec2(row.value_x + std::max(centred, 0.0f), row.origin.y));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(value);
}

// the arrows keep stepping while held
int DrawValueBlock(const RowLayout& row, const char* value) {
    int step = 0;
    ImGui::PushItemFlag(ImGuiItemFlags_NoNav | ImGuiItemFlags_ButtonRepeat, true);
    ImGui::SetCursorPos(ImVec2(row.block_x, row.origin.y));
    if (ImGui::ArrowButton("##dec", ImGuiDir_Left)) step = -1;
    DrawValueText(row, value);
    ImGui::SetCursorPos(ImVec2(row.inc_x, row.origin.y));
    if (ImGui::ArrowButton("##inc", ImGuiDir_Right)) step = 1;
    ImGui::PopItemFlag();
    return step;
}

// closes the PushID a row opened and moves past the row
void EndRow(const RowLayout& row) {
    ImGui::PopID();
    ImGui::SetCursorPos(row.origin);
    ImGui::Dummy(ImVec2(row.width, row.height));
}

// the step asked for this frame, 0 for none
int OptionRow(const char* label, const char* value) {
    RowLayout row = MeasureRow();
    ImGui::PushID(label);
    int key_step = FocusedStep(row);
    DrawLabel(row, label);
    int arrow_step = DrawValueBlock(row, value);
    EndRow(row);
    return arrow_step ? arrow_step : key_step;
}

void DrawHeading(const char* title) {
    float em = ImGui::GetFontSize();
    ImGui::Dummy(ImVec2(0, em * kHeadingGapEms));
    ImGui::PushStyleColor(ImGuiCol_Text, kHeading);
    ImGui::TextUnformatted(title);
    ImGui::PopStyleColor();
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, em * kHeadingAfterEms));
}

void DrawValueRow(const Row& row) {
    bool held = Held(row.id);
    Settings& shown = held && g_display_held ? g_display : Get();
    std::string value = kSteps[row.id](shown, 0);
    int step = OptionRow(row.label, value.c_str());
    if (step == 0) return;
    if (held) {
        ApplyHeldStep(row, step);
    } else {
        ApplyLiveStep(row, step);
    }
}

void DrawRow(const Row& row) {
    if (row.kind == Kind::Heading) {
        DrawHeading(row.label);
    } else {
        DrawValueRow(row);
    }
}

void DrawTitle() {
    const char* title = "RECOMP SETTINGS";
    float spare = ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(title).x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(spare * 0.5f, 0.0f));
    ImGui::TextUnformatted(title);
}

void DrawFooter() {
    float em = ImGui::GetFontSize();
    float button_width = em * kButtonEms;
    float spacing = ImGui::GetStyle().ItemSpacing.x;
    float spare = ImGui::GetContentRegionAvail().x - button_width * kFooterButtons - spacing * (kFooterButtons - 1);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(spare, 0.0f));
    if (ImGui::Button("Back", ImVec2(button_width, 0))) ToggleMenu();
    ImGui::SameLine();
    if (ImGui::Button("Defaults", ImVec2(button_width, 0))) RestoreDefaults();
    ImGui::SameLine();
    ImGui::BeginDisabled(!g_display_held);
    if (ImGui::Button("Apply", ImVec2(button_width, 0))) Apply();
    ImGui::EndDisabled();
}

// colours and square corners for the panel's window, undone when it goes out of scope
struct PanelStyle {
    PanelStyle() {
        float em = ImGui::GetFontSize();
        for (const auto& [slot, color] : kPanelColors) ImGui::PushStyleColor(slot, color);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, kBorderSize);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(em * kPaddingXEms, em * kPaddingYEms));
    }
    ~PanelStyle() {
        ImGui::PopStyleVar(4);
        ImGui::PopStyleColor(int(std::size(kPanelColors)));
    }
};

void DrawPanel() {
    float em = ImGui::GetFontSize();
    ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(em * kWindowWidthEms, em * kWindowHeightEms), ImGuiCond_Always);
    PanelStyle style;
    if (ImGui::Begin("##settings", nullptr, kWindowFlags)) {
        DrawTitle();
        float footer = ImGui::GetFrameHeightWithSpacing() + em;
        ImGui::BeginChild("##rows", ImVec2(0, -footer), ImGuiChildFlags_NavFlattened);
        for (const Row& row : kSettingsRows) DrawRow(row);
        ImGui::EndChild();
        ImGui::Separator();
        DrawFooter();
    }
    ImGui::End();
}

}  // namespace

bool MenuOpen() { return g_open.load(std::memory_order_relaxed); }

void ToggleMenu() {
    g_open.store(!MenuOpen(), std::memory_order_relaxed);
    g_display_held = false;
}

void DrawUi(double) {
    ImGuiIO& io = ImGui::GetIO();
    io.KeyRepeatRate = kRepeatSeconds;
    io.FontGlobalScale = std::max(kMinFontScale, io.DisplaySize.y / kFontScaleLines);
    if (MenuOpen()) DrawPanel();
}

}  // namespace app
