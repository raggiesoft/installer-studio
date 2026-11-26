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
#include <cwchar>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    bool smokeTest = false;
    DWORD sleepMs  = 0;
    const wchar_t* copySrc = nullptr;
    const wchar_t* copyDst = nullptr;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (::lstrcmpW(argv[i], L"--smoke-test") == 0) {
                smokeTest = true;
            } else if (::lstrcmpW(argv[i], L"--sleep-ms") == 0 && i + 1 < argc) {
                sleepMs = static_cast<DWORD>(std::wcstoul(argv[++i], nullptr, 10));
            } else if (::lstrcmpW(argv[i], L"--copy-check") == 0 && i + 2 < argc) {
                copySrc = argv[i + 1];
                copyDst = argv[i + 2];
                i += 2;
            }
        }
    }
    if (sleepMs > 0) {
        ::Sleep(sleepMs);
    }
    if (copySrc && copyDst) {
        const BOOL copied = ::CopyFileW(copySrc, copyDst, FALSE);
        if (argv) ::LocalFree(argv);
        return copied ? 0 : 2;
    }
    if (argv) {
        ::LocalFree(argv);
    }
    if (smokeTest || sleepMs > 0) {
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
