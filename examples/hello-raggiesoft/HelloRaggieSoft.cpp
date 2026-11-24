// ================================================================================
// FILE: examples/hello-raggiesoft/HelloRaggieSoft.cpp
// PROJECT: RaggieSoft Setup Wizard ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   This tiny zero-dependency native Win32 C++20 sample application (`HelloRaggieSoft.exe`)
//   serves as the live payload packaged into our `x64` and `ARM64` sample `.msi`,
//   `.msm`, `.mst`, `.msp`, and `-setup.exe` installers during automated builds!
// ================================================================================

#ifndef UNICODE
#define UNICODE
#endif
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    bool smokeTest = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (::lstrcmpW(argv[i], L"--smoke-test") == 0) smokeTest = true;
        }
        ::LocalFree(argv);
    }
    if (smokeTest) {
        return 0;
    }

    ::TaskDialog(
        nullptr,
        hInstance,
        L"Hello RaggieSoft",
        L"Installed with RaggieSoft Setup Wizard!",
        L"This native Windows application was packaged into a zero-dependency Enterprise .msi and accessible Setup.exe using RaggieSoft Setup Wizard (Install Without Limits).",
        TDCBF_OK_BUTTON,
        TD_INFORMATION_ICON,
        nullptr);
    return 0;
}
