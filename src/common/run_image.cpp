// what nightfire.exe calls in the image runtime it loaded (src/launcher/main.cpp)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE prev, PWSTR line, int show);  // the SDK's app/main.cpp

extern "C" __declspec(dllexport) int RunImage(HINSTANCE inst, int show) {
    return wWinMain(inst, nullptr, GetCommandLineW(), show);
}
