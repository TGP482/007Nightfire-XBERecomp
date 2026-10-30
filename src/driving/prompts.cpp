#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>

#include "common/mouse_keyboard.h"
#include "kernel/memory.h"
#include "xdk/hle.h"

// PC prompts: while the keyboard and mouse drive Driving.xbe, its menus' button legend names the keys, key first, as
// "(ENTER) Select". The pause menu's legend draw lists a button picture and a label a slot; at 0xDECFA the lists are
// made and nothing is measured yet, so each label gets its key and its picture a blank copy that draws nothing.
namespace {

using kernel::G;
using kernel::R32;

// the legend's lists on the stack at 0xDECFA, esi entries long
constexpr uint32_t kPictures = 0x58;
constexpr uint32_t kLabels = 0x74;
constexpr uint32_t kMaxSlots = 7;

// the pause menu (ebp): its button pictures, by the names 0xDB5F7 looks them up with
constexpr uint32_t kSelectPicture = 0x1F8;      // A
constexpr uint32_t kBackPicture = 0x1F0;        // B
constexpr uint32_t kContinuePicture = 0x1EC;    // START
constexpr uint32_t kChangePagePicture = 0x1FC;  // dpad left and right
constexpr uint32_t kScrollPicture = 0x1E8;      // dpad up and down

// a picture: its sprite from +0x30, its width and height at +0x40 and +0x44
constexpr uint32_t kPictureSize = 0x50;
constexpr uint32_t kPictureWidth = 0x40;
constexpr uint32_t kPictureHeight = 0x44;
// a slot takes its picture's width, 5 to its label and 20 to the next slot; the blank's width takes back most of
// that, so the longer labels still fit the panel with 7 between them
constexpr float kBlankWidth = -18.0f;

constexpr uint32_t kLabelSize = 64;
constexpr uint32_t kSlotSize = kPictureSize + kLabelSize;

// the key doing a picture's button in the menus (src/driving/keyboard.cpp, FrontEnd.def); empty keeps the picture
std::string KeyFor(uint32_t menu, uint32_t picture) {
    if (picture == R32(menu + kSelectPicture)) return "ENTER";
    if (picture == R32(menu + kBackPicture)) return "BACKSPACE";
    if (picture == R32(menu + kContinuePicture)) return input::FirstKey("car_pause");
    if (picture == R32(menu + kChangePagePicture)) return "LEFT/RIGHT";
    if (picture == R32(menu + kScrollPicture)) return "UP/DOWN";
    return {};
}

uint32_t Slot(uint32_t i) {
    static const uint32_t slots = mem::AllocSystem(kSlotSize * kMaxSlots);
    return slots + i * kSlotSize;
}

// the label with its key in front, and a copy of the picture that draws nothing and pulls the slots closer
void SwapSlot(X86Context& ctx, uint32_t i, const std::string& key) {
    uint32_t picture_at = ctx.esp + kPictures + i * 4;
    uint32_t label_at = ctx.esp + kLabels + i * 4;
    uint32_t blank = Slot(i);
    uint32_t label = blank + kPictureSize;
    std::memcpy(kernel::HostPtr(blank), kernel::HostPtr(R32(picture_at)), kPictureSize);
    G<float>(blank + kPictureWidth) = kBlankWidth;
    G<float>(blank + kPictureHeight) = 0.0f;
    const char* stock = static_cast<const char*>(kernel::HostPtr(R32(label_at)));
    std::string text = "(" + key + ") " + std::string(stock, strnlen(stock, kLabelSize));
    size_t length = std::min<size_t>(text.size(), kLabelSize - 1);
    std::memcpy(kernel::HostPtr(label), text.data(), length);
    G<char>(label + uint32_t(length)) = 0;
    kernel::W32(picture_at, blank);
    kernel::W32(label_at, label);
}

}  // namespace

// The HUD's tutorial lines: 0xDA1D0 splits a line at its first *A*, *G*, *P* or *D* (gas, gadget, fire, weapon) and its
// callers draw that button's picture between the halves. The key goes into the line instead and the picture is blank.
namespace {

// 0xDA1D0's arguments, stdcall (line, before, after): the buffers for the text before and after the button
constexpr uint32_t kBeforeArg = 0x8;
constexpr uint32_t kAfterArg = 0xC;
constexpr uint32_t kBeforeSize = 0x40;  // on both callers' stacks, the after buffer follows it
constexpr uint32_t kMaxLine = 256;

constexpr const char* kButtonActions[] = {"car_accelerate", "car_gadget", "car_fire", "car_fire"};
constexpr uint32_t kButtons = 4;

bool g_line_keys = false;  // the line just split has its key in it

std::string GuestText(uint32_t at) {
    const char* text = static_cast<const char*>(kernel::HostPtr(at));
    return std::string(text, strnlen(text, kMaxLine));
}

uint32_t BlankPicture(uint32_t picture) {
    static const uint32_t blank = mem::AllocSystem(kPictureSize);
    std::memcpy(kernel::HostPtr(blank), kernel::HostPtr(picture), kPictureSize);
    G<float>(blank + kPictureWidth) = 0.0f;
    G<float>(blank + kPictureHeight) = 0.0f;
    return blank;
}

}  // namespace

X86_EXTERN_FUNC(__imp__sub_000DA1D0);
X86_FUNC(sub_000DA1D0) {
    uint32_t before = R32(ctx.esp + kBeforeArg);
    uint32_t after = R32(ctx.esp + kAfterArg);
    g_line_keys = false;
    __imp__sub_000DA1D0(ctx, base);
    if (!input::KeyboardActive() || ctx.eax >= kButtons) return;
    std::string key = input::FirstKey(kButtonActions[ctx.eax]);
    if (key.empty()) return;
    std::string line = GuestText(before) + "(" + key + ")" + GuestText(after);
    size_t length = std::min<size_t>(line.size(), kBeforeSize - 1);
    std::memcpy(kernel::HostPtr(before), line.data(), length);
    G<char>(before + uint32_t(length)) = 0;
    G<char>(after) = 0;
    g_line_keys = true;
}

// at 0xDDBC5 and 0xDF0F9, once the button's picture is chosen
void DrivingLinePictureEdi(X86Context& ctx, uint8_t*) {
    if (g_line_keys) ctx.edi = BlankPicture(ctx.edi);
}
void DrivingLinePictureEsi(X86Context& ctx, uint8_t*) {
    if (g_line_keys) ctx.esi = BlankPicture(ctx.esi);
}

void DrivingRecompLegend(X86Context& ctx);  // options_menu.cpp

void DrivingLegendKeys(X86Context& ctx, uint8_t*) {
    DrivingRecompLegend(ctx);
    if (!input::KeyboardActive()) return;
    uint32_t count = std::min(ctx.esi, kMaxSlots);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t picture = R32(ctx.esp + kPictures + i * 4);
        uint32_t label = R32(ctx.esp + kLabels + i * 4);
        std::string key = picture && label ? KeyFor(ctx.ebp, picture) : std::string();
        if (!key.empty()) SwapSlot(ctx, i, key);
    }
}
