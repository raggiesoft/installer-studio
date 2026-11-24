// ================================================================================
// FILE: src/common/RswPlatform.hpp
// PROJECT: RaggieSoft Setup Wizard ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file, and what two subtle Windows traps does it solve?
//     1. **True Windows 11 Build Detection (`RtlGetVersion` in `ntdll.dll`)**:
//        - Legacy Win32 APIs like `GetVersionExW` and `VersionHelpers.h` lie on
//          modern Windows unless specific compatibility GUIDs exist in the `.exe`
//          manifest, AND Microsoft never added an `IsWindows11OrGreater()` helper!
//        - Even worse, reading `ProductName` from `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion`
//          still returns `"Windows 10 Pro"` on Windows 11 24H2 / 26H2!
//        - Calling kernel-backed `RtlGetVersion` directly from `ntdll.dll` always
//          returns the true, un-spoofed `dwMajorVersion` and `dwBuildNumber`.
//          Checking `Major > 10 || (Major == 10 && Build >= 22000)` cleanly accepts
//          all Windows 11 releases (including non-linear Insider/26H1/26H2 builds).
//
//     2. **Piercing Windows 11 ARM64 Prism Emulation (`IsWow64Process2`)**:
//        - On an Apple Silicon Parallels VM or Snapdragon Elite PC running Windows 11
//          `ARM64`, Microsoft's Prism emulator spoofs `GetNativeSystemInfo` for `x64`
//          binaries so old installers think they are on an Intel `x64` CPU!
//        - `IsWow64Process2(::GetCurrentProcess(), &processMachine, &nativeMachine)`
//          (`wow64apiset.h`) always reveals the TRUE physical silicon in `nativeMachine`
//          (`IMAGE_FILE_MACHINE_ARM64` = `0xAA64`, `IMAGE_FILE_MACHINE_AMD64` = `0x8664`)
//          AND tells us whether the current process is emulated (`processMachine != IMAGE_FILE_MACHINE_UNKNOWN`)!
// ================================================================================

#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <wow64apiset.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace rsw {

// Standard UTF-8 <-> UTF-16 string conversion helpers using Win32's built-in
// `MultiByteToWideChar` and `WideCharToMultiByte` (zero deprecated `<codecvt>`!).
inline std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int needed = ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring out(static_cast<std::size_t>(needed), L'\0');
    ::MultiByteToWideChar(
        CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), needed);
    return out;
}

inline std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int needed = ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string out(static_cast<std::size_t>(needed), '\0');
    ::WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()), out.data(), needed, nullptr, nullptr);
    return out;
}

/// Structured snapshot of the host Windows kernel version and CPU silicon.
struct HostPlatformInfo {
    std::uint32_t majorVersion   = 0;
    std::uint32_t minorVersion   = 0;
    std::uint32_t buildNumber    = 0;
    USHORT        processMachine = IMAGE_FILE_MACHINE_UNKNOWN; // Non-zero when running under WOW64 / Prism emulation!
    USHORT        nativeMachine  = IMAGE_FILE_MACHINE_UNKNOWN; // True physical host CPU (`0xAA64` ARM64, `0x8664` AMD64)
    USHORT        compiledArch   =
#if defined(_M_ARM64)
        IMAGE_FILE_MACHINE_ARM64;
#elif defined(_M_X64)
        IMAGE_FILE_MACHINE_AMD64;
#elif defined(_M_IX86)
        IMAGE_FILE_MACHINE_I386;
#else
        IMAGE_FILE_MACHINE_UNKNOWN;
#endif

    /// Returns `true` when this process is running under WOW64 or ARM64 Prism x64 emulation.
    [[nodiscard]] bool IsEmulatedProcess() const noexcept {
        return processMachine != IMAGE_FILE_MACHINE_UNKNOWN;
    }

    /// Evaluates our Windows 11+ OS gate (`Major > 10 || (Major == 10 && Build >= minBuild)`).
    [[nodiscard]] bool MeetsMinimumWindowsBuild(std::uint32_t minBuild = 22000) const noexcept {
        return (majorVersion > 10) || (majorVersion == 10 && buildNumber >= minBuild);
    }

    /// Converts any `IMAGE_FILE_MACHINE_*` constant into a clean human/manifest architecture slug.
    static std::string_view MachineToSlug(USHORT machine) noexcept {
        switch (machine) {
            case IMAGE_FILE_MACHINE_ARM64: return "arm64";
            case IMAGE_FILE_MACHINE_AMD64: return "x64";
            case IMAGE_FILE_MACHINE_I386:  return "x86";
            default:                       return "unknown";
        }
    }

    [[nodiscard]] std::string_view NativeArchSlug() const noexcept {
        return MachineToSlug(nativeMachine);
    }

    [[nodiscard]] std::string_view CompiledArchSlug() const noexcept {
        return MachineToSlug(compiledArch);
    }
};

/// Queries `RtlGetVersion` (`ntdll.dll`) and `IsWow64Process2` (`kernel32.dll`) directly.
inline HostPlatformInfo QueryHostPlatform() noexcept {
    HostPlatformInfo info{};

    // 1. Query un-spoofed kernel version from `ntdll.dll!RtlGetVersion`.
    //    `RTL_OSVERSIONINFOW` has the exact same memory layout as `OSVERSIONINFOW`.
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    if (HMODULE hNtdll = ::GetModuleHandleW(L"ntdll.dll")) {
        auto fn = reinterpret_cast<RtlGetVersionFn>(::GetProcAddress(hNtdll, "RtlGetVersion"));
        if (fn != nullptr) {
            RTL_OSVERSIONINFOW osvi{};
            osvi.dwOSVersionInfoSize = sizeof(osvi);
            if (fn(&osvi) == 0) {
                info.majorVersion = osvi.dwMajorVersion;
                info.minorVersion = osvi.dwMinorVersion;
                info.buildNumber  = osvi.dwBuildNumber;
            }
        }
    }

    // 2. Pierce WOW64 and ARM64 Prism emulation via `IsWow64Process2`.
    USHORT procMach = IMAGE_FILE_MACHINE_UNKNOWN;
    USHORT natMach  = IMAGE_FILE_MACHINE_UNKNOWN;
    if (::IsWow64Process2(::GetCurrentProcess(), &procMach, &natMach)) {
        info.processMachine = procMach;
        info.nativeMachine  = natMach;
    } else {
        info.nativeMachine = info.compiledArch;
    }

    return info;
}

/// Convenience helper checking whether the true Windows kernel build meets `minBuild` (`22000` = Win 11+).
inline bool MeetsWindowsBuildFloor(int minBuild = 22000) noexcept {
    const auto hp = QueryHostPlatform();
    return hp.MeetsMinimumWindowsBuild(static_cast<std::uint32_t>(minBuild > 0 ? minBuild : 0));
}

} // namespace rsw
