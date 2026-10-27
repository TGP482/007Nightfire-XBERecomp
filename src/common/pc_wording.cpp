#include "common/pc_wording.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "kernel/kernel.h"
#include "kernel/memory.h"

// The games' texts name the console and its parts: they read as on PC instead. A text that names it gets a copy in
// guest memory once, which its lookup returns while the text stays the same.
namespace {

struct Swap {
    const char* from;
    const char* to;
};
// the longer names first
constexpr Swap kSwaps[] = {
    {"Xbox Hard Disk", "hard drive"},  {"Xbox hard disk", "hard drive"},  {"XBOX HARD DISK", "HARD DRIVE"},
    {"Xbox Controller", "controller"}, {"Xbox controller", "controller"}, {"XBOX CONTROLLER", "CONTROLLER"},
    {"Xbox Console", "PC"},            {"Xbox console", "PC"},            {"XBOX CONSOLE", "PC"},
    {"Xbox Dashboard", "desktop"},     {"Xbox dashboard", "desktop"},     {"XBOX DASHBOARD", "DESKTOP"},
    {"Xbox", "PC"},                    {"XBox", "PC"},                    {"XBOX", "PC"},
};
constexpr size_t kMaxText = 1024;

struct Copy {
    std::string text;  // the text it was made from
    uint32_t at;
};
std::mutex g_lock;
std::unordered_map<uint32_t, Copy> g_copies;  // by the text's address

bool NamesConsole(std::string_view text) {
    constexpr std::string_view kName = "xbox";
    auto same = [](char a, char b) { return std::tolower(uint8_t(a)) == b; };
    return std::search(text.begin(), text.end(), kName.begin(), kName.end(), same) != text.end();
}

bool StartsSentence(const std::string& text, size_t at) {
    if (at == 0) return true;
    size_t before = text.find_last_not_of(' ', at - 1);
    if (before == text.npos) return true;
    char c = text[before];
    return c == '.' || c == '!' || c == '?' || c == '\n';
}

std::string Swapped(std::string text) {
    for (const Swap& swap : kSwaps) {
        size_t from = std::strlen(swap.from);
        size_t to = std::strlen(swap.to);
        for (size_t at = text.find(swap.from); at != text.npos; at = text.find(swap.from, at + to)) {
            text.replace(at, from, swap.to);
            if (StartsSentence(text, at)) text[at] = char(std::toupper(uint8_t(text[at])));
        }
    }
    return text;
}

}  // namespace

uint32_t game::PcWording(uint32_t text) {
    if (!text) return text;
    const char* host = static_cast<const char*>(kernel::HostPtr(text));
    std::string_view view(host, strnlen(host, kMaxText));
    if (!NamesConsole(view)) return text;
    std::lock_guard lock(g_lock);
    auto it = g_copies.find(text);
    if (it != g_copies.end() && it->second.text == view) return it->second.at;
    std::string swapped = Swapped(std::string(view));
    uint32_t at = mem::AllocSystem(uint32_t(swapped.size() + 1));
    std::memcpy(kernel::HostPtr(at), swapped.c_str(), swapped.size() + 1);
    g_copies[text] = {std::string(view), at};
    return at;
}
