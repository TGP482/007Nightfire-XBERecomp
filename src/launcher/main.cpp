// nightfire.exe, the one executable for both of the game's images: it loads the runtime of the image the command line
// names (--image, which a title launch adds, src/kernel/boot.cpp) and runs it, default.xbe's otherwise
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cwchar>
#include <string>

namespace {

constexpr wchar_t kFirstImage[] = L"007Nightfire";  // GameInfo::images' first exe name
constexpr wchar_t kImageArgument[] = L"--image ";
constexpr wchar_t kRuntimeSuffix[] = L"Runtime.dll";
constexpr char kRunImage[] = "RunImage";  // src/common/run_image.cpp

std::wstring ImageName() {
    const wchar_t* at = std::wcsstr(GetCommandLineW(), kImageArgument);
    if (!at) return kFirstImage;
    at += std::wcslen(kImageArgument);
    const wchar_t* end = at;
    while (*end && *end != L' ') end++;
    return std::wstring(at, end);
}

std::wstring ExeFolder() {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring folder = path;
    return folder.substr(0, folder.find_last_of(L'\\') + 1);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, PWSTR, int show) {
    using RunImage = int (*)(HINSTANCE, int);
    std::wstring dll = ExeFolder() + ImageName() + kRuntimeSuffix;
    HMODULE runtime = LoadLibraryExW(dll.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    FARPROC entry = runtime ? GetProcAddress(runtime, kRunImage) : nullptr;
    if (!entry) {
        std::wstring message = L"Cannot load " + dll + L". Extract the whole release again.";
        MessageBoxW(nullptr, message.c_str(), L"007 Nightfire", MB_ICONERROR);
        return 1;
    }
    return reinterpret_cast<RunImage>(reinterpret_cast<void*>(entry))(inst, show);
}
