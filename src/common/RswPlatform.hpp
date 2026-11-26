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
#include <sddl.h>
#include <wow64apiset.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>

#pragma comment(lib, "advapi32.lib")

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

// ============================================================================
// v0.5.1: MANIFEST EXECUTION LEVEL, UAC-DISABLED (`EnableLUA = 0`) ADMIN GATE,
//         PER-PRODUCT SINGLE-INSTANCE ACTIVATION & `Global\_MSIExecute` QUEUE
// ============================================================================
// PLAIN-ENGLISH TEACHING OVERVIEW:
//   1. `NormalizeRequestedExecutionLevel`:
//        Canonicalizes `[Package] RequestedExecutionLevel` to exact Win32 Side-by-Side
//        XML casing (`"asInvoker"`, `"highestAvailable"`, or `"requireAdministrator"`).
//   2. `QueryProcessPrivilegeState`:
//        When a user disables UAC (`EnableLUA = 0`), `TOKEN_ELEVATION.TokenIsElevated`
//        can report `0` even on a genuine Administrator account, AND Windows cannot
//        display an Over-the-Shoulder UAC elevation prompt for a Standard User!
//        Calling Win32 `CheckTokenMembership(nullptr, BUILTIN\Administrators)` tests
//        whether the `Administrators` SID is *actively enabled* on the current token:
//          - Split-Token Admin (un-elevated, UAC ON) : `isAdminGroupActive == false` (deny-only!)
//          - Elevated Admin (UAC ON)                 : `isAdminGroupActive == true`
//          - Admin with UAC OFF (`EnableLUA = 0`)    : `isAdminGroupActive == true`!
//          - Standard User (UAC ON or OFF)           : `isAdminGroupActive == false`
//   3. `DeriveProductInstanceKey` + `CreatePermissiveLocalMutex` + `ActivateExistingProductSetupWindow`:
//        Prevents launching duplicate `Setup.exe` windows for the SAME `UpgradeCode`
//        (bringing the existing window back to the foreground even across UIPI!), while
//        allowing DIFFERENT products (e.g. *StoreTrainer* + *Accessibility Studio*) to
//        open side-by-side and politely auto-queue on `Global\_MSIExecute` instead of
//        crashing with Windows Installer `Error 1618`!

/// Canonicalizes a case-insensitive execution level token into exact Win32 `RT_MANIFEST` XML casing.
inline std::string_view NormalizeRequestedExecutionLevel(std::string_view raw) noexcept {
    auto eqCi = [](std::string_view a, std::string_view b) noexcept {
        if (a.size() != b.size()) return false;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const char ca = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
            const char cb = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
            if (ca != cb) return false;
        }
        return true;
    };
    while (!raw.empty() && (raw.front() == ' ' || raw.front() == '\t')) raw.remove_prefix(1);
    while (!raw.empty() && (raw.back()  == ' ' || raw.back()  == '\t')) raw.remove_suffix(1);
    if (eqCi(raw, "requireadministrator")) return "requireAdministrator";
    if (eqCi(raw, "highestavailable"))     return "highestAvailable";
    return "asInvoker";
}

/// Snapshot of UAC policy (`EnableLUA`), split-token elevation, and active `BUILTIN\Administrators` membership.
struct ProcessPrivilegeState {
    bool isUacEnabled       = true;  // `HKLM\...\Policies\System\EnableLUA != 0`
    bool isTokenElevated    = false; // `GetTokenInformation(TokenElevation)`
    bool isAdminGroupActive = false; // `CheckTokenMembership(BUILTIN\Administrators)`

    /// `true` if this process already possesses active Administrator privileges (whether via UAC elevation OR `EnableLUA = 0` Admin login!).
    [[nodiscard]] bool HasEffectiveAdminRights() const noexcept {
        return isTokenElevated || isAdminGroupActive;
    }

    /// `true` when this process is un-elevated AND Windows UAC is enabled so `ShellExecuteExW(L"runas")` can prompt for consent/credentials.
    [[nodiscard]] bool CanPromptForUacElevation() const noexcept {
        return !HasEffectiveAdminRights() && isUacEnabled;
    }

    /// `true` when UAC is disabled (`EnableLUA = 0`) AND the user is a Standard User without Admin rights (cannot elevate via UAC).
    [[nodiscard]] bool IsStandardUserWithUacDisabled() const noexcept {
        return !HasEffectiveAdminRights() && !isUacEnabled;
    }
};

/// Queries `EnableLUA`, `TokenElevation`, and `CheckTokenMembership(DOMAIN_ALIAS_RID_ADMINS)`.
inline ProcessPrivilegeState QueryProcessPrivilegeState() noexcept {
    ProcessPrivilegeState st{};

    // 1. Check `HKLM\SOFTWARE\Microsoft\Windows\CurrentVersion\Policies\System\EnableLUA`
    HKEY hPolKey = nullptr;
    if (::RegOpenKeyExW(
            HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
            0, KEY_READ | KEY_WOW64_64KEY, &hPolKey) == ERROR_SUCCESS && hPolKey) {
        DWORD enableLua = 1;
        DWORD cbData    = sizeof(enableLua);
        DWORD regType   = 0;
        if (::RegQueryValueExW(hPolKey, L"EnableLUA", nullptr, &regType,
                               reinterpret_cast<LPBYTE>(&enableLua), &cbData) == ERROR_SUCCESS &&
            regType == REG_DWORD) {
            st.isUacEnabled = (enableLua != 0);
        }
        ::RegCloseKey(hPolKey);
    }

    // 2. Check `TOKEN_ELEVATION` on current process token
    HANDLE hToken = nullptr;
    if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken) && hToken) {
        TOKEN_ELEVATION elev{};
        DWORD cb = sizeof(elev);
        if (::GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &cb)) {
            st.isTokenElevated = (elev.TokenIsElevated != 0);
        }
        ::CloseHandle(hToken);
    }

    // 3. Verify active `BUILTIN\Administrators` (`S-1-5-32-544`) membership via `CheckTokenMembership`.
    //    `CheckTokenMembership(nullptr, ...)` inspects the effective impersonation/process token and
    //    returns `TRUE` ONLY if the `Administrators` SID is present AND enabled (NOT `SE_GROUP_USE_FOR_DENY_ONLY`).
    SID_IDENTIFIER_AUTHORITY ntAuth = SECURITY_NT_AUTHORITY;
    PSID pAdminSid = nullptr;
    if (::AllocateAndInitializeSid(
            &ntAuth, 2,
            SECURITY_BUILTIN_DOMAIN_RID,
            DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &pAdminSid) && pAdminSid) {
        BOOL isMember = FALSE;
        if (::CheckTokenMembership(nullptr, pAdminSid, &isMember) && isMember) {
            st.isAdminGroupActive = true;
        }
        ::FreeSid(pAdminSid);
    }

    return st;
}

/// Derives a sanitized uppercase alphanumeric per-product instance key (`A-Z0-9`, max 48 chars)
/// from `upgradeCode` (falling back to `manufacturer + productName` if `upgradeCode` is empty).
inline std::wstring DeriveProductInstanceKey(
    std::wstring_view upgradeCode,
    std::wstring_view manufacturer,
    std::wstring_view productName)
{
    auto sanitizeAlphaNum = [](std::wstring_view src) {
        std::wstring out;
        out.reserve(48);
        for (wchar_t ch : src) {
            if ((ch >= L'0' && ch <= L'9') || (ch >= L'A' && ch <= L'Z')) {
                out.push_back(ch);
            } else if (ch >= L'a' && ch <= L'z') {
                out.push_back(static_cast<wchar_t>(ch - L'a' + L'A'));
            }
            if (out.size() >= 48) break;
        }
        return out;
    };

    std::wstring key = sanitizeAlphaNum(upgradeCode);
    if (key.empty()) {
        key = sanitizeAlphaNum(manufacturer) + L"_" + sanitizeAlphaNum(productName);
    }
    return key.empty() ? L"DEFAULTPRODUCT" : key;
}

/// Creates or opens a named per-session mutex (`Local\...`) with a permissive DACL so an un-elevated
/// second launch can reliably detect an already-running `requireAdministrator` first launch!
inline HANDLE CreatePermissiveLocalMutex(const std::wstring& fullMutexName, bool& outAlreadyRunning) noexcept {
    outAlreadyRunning = false;
    PSECURITY_DESCRIPTOR pSd = nullptr;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, FALSE};
    // Grant `GA` to `WD` (Everyone) and `AC` (All Application Packages) on this synchronization mutex
    if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;WD)(A;;GA;;;AC)", SDDL_REVISION_1, &pSd, nullptr)) {
        sa.lpSecurityDescriptor = pSd;
    }

    ::SetLastError(ERROR_SUCCESS);
    HANDLE hMut = ::CreateMutexW(pSd ? &sa : nullptr, FALSE, fullMutexName.c_str());
    const DWORD gle = ::GetLastError();
    if (pSd) {
        ::LocalFree(pSd);
    }

    if (!hMut && gle == ERROR_ACCESS_DENIED) {
        // An elevated instance already created the mutex in this session!
        outAlreadyRunning = true;
        return nullptr;
    }
    if (hMut && gle == ERROR_ALREADY_EXISTS) {
        outAlreadyRunning = true;
    }
    return hMut;
}

/// Checks whether a named mutex currently exists in the session without keeping a handle open.
inline bool DoesLocalMutexExist(const std::wstring& fullMutexName) noexcept {
    HANDLE h = ::OpenMutexW(SYNCHRONIZE, FALSE, fullMutexName.c_str());
    if (h) {
        ::CloseHandle(h);
        return true;
    }
    return ::GetLastError() == ERROR_ACCESS_DENIED;
}

/// Tags a top-level Setup window (`SetupWizard.exe` or `UniversalWebDownloaderUi`) with its
/// per-product `instanceKey` and whitelists its activation message across UIPI!
inline UINT RegisterSingleInstanceWindowTag(HWND hWnd, const std::wstring& instanceKey) noexcept {
    if (!hWnd || instanceKey.empty()) return 0;
    const std::wstring propName = L"RaggieSoft.SetupKey." + instanceKey;
    const std::wstring msgName  = L"RaggieSoft.ActivateSetup." + instanceKey;
    ::SetPropW(hWnd, propName.c_str(), reinterpret_cast<HANDLE>(1));
    const UINT wmActivate = ::RegisterWindowMessageW(msgName.c_str());
    if (wmActivate != 0) {
        CHANGEFILTERSTRUCT cfs{sizeof(cfs), 0};
        ::ChangeWindowMessageFilterEx(hWnd, wmActivate, MSGFLT_ALLOW, &cfs);
    }
    return wmActivate;
}

inline void UnregisterSingleInstanceWindowTag(HWND hWnd, const std::wstring& instanceKey) noexcept {
    if (!hWnd || instanceKey.empty()) return;
    const std::wstring propName = L"RaggieSoft.SetupKey." + instanceKey;
    ::RemovePropW(hWnd, propName.c_str());
}

/// Locates an already-running Setup window for `instanceKey`, hands over Explorer's foreground
/// activation permission (`AllowSetForegroundWindow`), and brings it back to the front!
inline bool ActivateExistingProductSetupWindow(const std::wstring& instanceKey) noexcept {
    if (instanceKey.empty()) return false;
    const std::wstring propName  = L"RaggieSoft.SetupKey." + instanceKey;
    const UINT wmActivate        = ::RegisterWindowMessageW((L"RaggieSoft.ActivateSetup." + instanceKey).c_str());

    struct SearchCtx {
        const wchar_t* prop = nullptr;
        HWND           found = nullptr;
    };

    for (int attempt = 0; attempt < 14; ++attempt) {
        SearchCtx ctx{propName.c_str(), nullptr};
        ::EnumWindows([](HWND hTop, LPARAM lp) -> BOOL {
            auto* c = reinterpret_cast<SearchCtx*>(lp);
            if (::GetPropW(hTop, c->prop) != nullptr) {
                c->found = hTop;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));

        if (ctx.found && ::IsWindow(ctx.found)) {
            DWORD targetPid = 0;
            ::GetWindowThreadProcessId(ctx.found, &targetPid);
            if (targetPid != 0) {
                ::AllowSetForegroundWindow(targetPid);
            }
            if (wmActivate != 0) {
                ::PostMessageW(ctx.found, wmActivate, 0, 0);
            }
            if (::IsIconic(ctx.found)) {
                ::ShowWindow(ctx.found, SW_RESTORE);
            }
            ::SetForegroundWindow(ctx.found);
            return true;
        }
        ::Sleep(25);
    }
    return false;
}

/// Publishes the active RaggieSoft product name in shared memory (`Local\RaggieSoft.ActiveMsiTxProduct`)
/// while an MSI transaction is running so a second RaggieSoft installer waiting in the polite queue can
/// display `"Waiting for <Other Product> Setup to finish installing..."`!
class ScopedActiveMsiBeacon {
public:
    explicit ScopedActiveMsiBeacon(std::wstring_view productName) noexcept {
        PSECURITY_DESCRIPTOR pSd = nullptr;
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, FALSE};
        if (::ConvertStringSecurityDescriptorToSecurityDescriptorW(
                L"D:(A;;GA;;;WD)(A;;GA;;;AC)", SDDL_REVISION_1, &pSd, nullptr)) {
            sa.lpSecurityDescriptor = pSd;
        }
        hMap_ = ::CreateFileMappingW(
            INVALID_HANDLE_VALUE, pSd ? &sa : nullptr, PAGE_READWRITE,
            0, kBufferBytes, kMappingName);
        if (pSd) ::LocalFree(pSd);
        if (hMap_) {
            pView_ = static_cast<wchar_t*>(::MapViewOfFile(hMap_, FILE_MAP_WRITE, 0, 0, kBufferBytes));
            if (pView_) {
                std::memset(pView_, 0, kBufferBytes);
                const std::size_t maxChars = (kBufferBytes / sizeof(wchar_t)) - 1;
                const std::size_t nCopy    = std::min(productName.size(), maxChars);
                if (nCopy > 0) {
                    std::memcpy(pView_, productName.data(), nCopy * sizeof(wchar_t));
                }
            }
        }
    }

    ~ScopedActiveMsiBeacon() {
        if (pView_) {
            std::memset(pView_, 0, kBufferBytes);
            ::UnmapViewOfFile(pView_);
        }
        if (hMap_) {
            ::CloseHandle(hMap_);
        }
    }

    ScopedActiveMsiBeacon(const ScopedActiveMsiBeacon&)            = delete;
    ScopedActiveMsiBeacon& operator=(const ScopedActiveMsiBeacon&) = delete;

    static std::wstring ReadActiveProductName() {
        HANDLE hOpen = ::OpenFileMappingW(FILE_MAP_READ, FALSE, kMappingName);
        if (!hOpen) return {};
        const auto* view = static_cast<const wchar_t*>(::MapViewOfFile(hOpen, FILE_MAP_READ, 0, 0, kBufferBytes));
        std::wstring out;
        if (view) {
            const std::size_t maxChars = (kBufferBytes / sizeof(wchar_t)) - 1;
            std::size_t len = 0;
            while (len < maxChars && view[len] != L'\0') ++len;
            out.assign(view, len);
            ::UnmapViewOfFile(view);
        }
        ::CloseHandle(hOpen);
        return out;
    }

private:
    static constexpr const wchar_t* kMappingName = L"Local\\RaggieSoft.ActiveMsiTxProduct";
    static constexpr DWORD          kBufferBytes = 512;
    HANDLE   hMap_  = nullptr;
    wchar_t* pView_ = nullptr;
};

/// Non-destructively probes whether Windows Installer's system-wide `Global\_MSIExecute` mutex
/// is currently held by another installation transaction.
/// CRITICAL WIN32 DETAIL: If `WaitForSingleObject(..., 0)` succeeds (`WAIT_OBJECT_0` or `WAIT_ABANDONED`),
/// we MUST immediately call `ReleaseMutex` so `msiserver` can acquire it inside `MsiInstallProductW`!
inline bool IsSystemMsiExecuteMutexBusy() noexcept {
    HANDLE hMsiMut = ::OpenMutexW(SYNCHRONIZE, FALSE, L"Global\\_MSIExecute");
    if (!hMsiMut) {
        return false;
    }
    const DWORD waitRc = ::WaitForSingleObject(hMsiMut, 0);
    if (waitRc == WAIT_OBJECT_0 || waitRc == WAIT_ABANDONED) {
        ::ReleaseMutex(hMsiMut);
        ::CloseHandle(hMsiMut);
        return false;
    }
    ::CloseHandle(hMsiMut);
    return (waitRc == WAIT_TIMEOUT);
}

} // namespace rsw
