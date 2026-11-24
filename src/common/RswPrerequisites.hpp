// ================================================================================
// FILE: src/common/RswPrerequisites.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file?
//     This header powers RaggieSoft Installer Studio's **Pre-MSI Runtime
//     Prerequisites Engine** (`[Prerequisite.<id>]` in `.rsip` manifests)!
//
//   Why MUST runtime installers (`.NET Desktop Runtime`, `VC++ Redistributable`,
//   `Edge WebView2`, or custom `.msi`/`.exe` runtimes) execute BEFORE our own
//   `Product.msi` starts?
//     Windows Installer (`msiexec.exe`) enforces a system-wide single-transaction
//     mutex (`_MSIExecute`) during `InstallExecuteSequence`. Because Microsoft's
//     own `.NET Desktop Runtime` and `VC++ Redistributable` bundles launch nested
//     `.msi` packages internally, running them from inside an MSI CustomAction
//     fails immediately with Error `1618` (`ERROR_INSTALL_ALREADY_RUNNING`)!
//
//     Instead, `SetupWizard.exe` evaluates every `[Prerequisite.<id>]` BEFORE
//     starting `Product.msi`:
//       1. **Fast Non-Invasive Detection**: Checks 64-bit `HKLM`/`HKCU` registry
//          keys, `.NET` `shared\` framework folders, PE file versions, or MSI
//          `UpgradeCode`s so machines that already have the runtime skip it in <1ms!
//       2. **Three Flexible Delivery Modes**:
//          - `download`     : Streams directly from official HTTPS (`aka.ms` or
//                             custom CDN) via stock Windows `WinHTTP` with TLS 1.2/1.3,
//                             live byte progress, and optional `SHA-256` verification!
//          - `embedded`     : Extracts the runtime directly from `Setup.exe` `RT_RCDATA`
//                             (`#300+i`) for 100% offline / air-gapped environments!
//          - `side-by-side` : Reads `Redist\<FileName>` next to `Setup.exe` (falling
//                             back to HTTPS `Url` if absent!).
//       3. **Standardized + Custom Exit Code Classification**:
//          - `SuccessExitCodes` (default `0, 1638` — where `1638` means a newer
//            patch of VC++/.NET is already installed!).
//          - `RebootExitCodes`  (default `3010, 1641, 23` — or ANY custom vendor
//            exit codes configured by the author!). Whenever a prerequisite returns
//            a reboot code, Setup latches `rebootRequested = true`, finishes
//            installing `Product.msi`, and reports the pending restart cleanly at
//            the end (returning `3010` or custom `SilentRebootExitCode` in `/s`)!
// ================================================================================

#pragma once

#include "RswCryptoAndPe.hpp"
#include "RswManifestAst.hpp"
#include "RswPlatform.hpp"
#include "RswVersion.hpp"

#include <bcrypt.h>
#include <msi.h>
#include <winhttp.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "msi.lib")

namespace rsw {

/// Built-in curated runtime preset descriptor so authors only need `Preset = DotNet10Desktop`!
struct CuratedRuntimePreset {
    std::string_view presetId;
    std::string_view displayName;
    std::string_view fileNamePattern; // Supports `$(ARCH)`
    std::string_view urlX64;
    std::string_view urlArm64;
    std::string_view silentArgs;
    std::string_view successCodes;
    std::string_view rebootCodes;
};

inline constexpr std::array<CuratedRuntimePreset, 6> kCuratedRuntimePresets{{
    {"DotNet10Desktop",
     "Microsoft .NET 10.0 Desktop Runtime",
     "windowsdesktop-runtime-10.0-$(ARCH).exe",
     "https://aka.ms/dotnet/10.0/windowsdesktop-runtime-win-x64.exe",
     "https://aka.ms/dotnet/10.0/windowsdesktop-runtime-win-arm64.exe",
     "/install /quiet /norestart",
     "0, 1638",
     "3010, 1641, 23"},

    {"DotNet10AspNet",
     "Microsoft ASP.NET Core 10.0 Runtime",
     "aspnetcore-runtime-10.0-$(ARCH).exe",
     "https://aka.ms/dotnet/10.0/aspnetcore-runtime-win-x64.exe",
     "https://aka.ms/dotnet/10.0/aspnetcore-runtime-win-arm64.exe",
     "/install /quiet /norestart",
     "0, 1638",
     "3010, 1641, 23"},

    {"DotNet9Desktop",
     "Microsoft .NET 9.0 Desktop Runtime",
     "windowsdesktop-runtime-9.0-$(ARCH).exe",
     "https://aka.ms/dotnet/9.0/windowsdesktop-runtime-win-x64.exe",
     "https://aka.ms/dotnet/9.0/windowsdesktop-runtime-win-arm64.exe",
     "/install /quiet /norestart",
     "0, 1638",
     "3010, 1641, 23"},

    {"DotNet8Desktop",
     "Microsoft .NET 8.0 LTS Desktop Runtime",
     "windowsdesktop-runtime-8.0-$(ARCH).exe",
     "https://aka.ms/dotnet/8.0/windowsdesktop-runtime-win-x64.exe",
     "https://aka.ms/dotnet/8.0/windowsdesktop-runtime-win-arm64.exe",
     "/install /quiet /norestart",
     "0, 1638",
     "3010, 1641, 23"},

    {"EdgeWebView2",
     "Microsoft Edge WebView2 Evergreen Runtime",
     "MicrosoftEdgeWebview2Setup.exe",
     "https://go.microsoft.com/fwlink/p/?LinkId=2124703",
     "https://go.microsoft.com/fwlink/p/?LinkId=2124703",
     "/silent /install",
     "0, 1638",
     "3010, 1641"},

    {"VcRedist2022",
     "Microsoft Visual C++ 2015-2022 Redistributable",
     "vc_redist.$(ARCH).exe",
     "https://aka.ms/vs/17/release/vc_redist.x64.exe",
     "https://aka.ms/vs/17/release/vc_redist.arm64.exe",
     "/install /quiet /norestart",
     "0, 1638",
     "3010, 1641"}
}};

inline const CuratedRuntimePreset* FindCuratedRuntimePreset(std::string_view presetId) noexcept {
    for (const auto& p : kCuratedRuntimePresets) {
        if (EqualsIgnoreCase(p.presetId, presetId)) return &p;
    }
    return nullptr;
}

/// Concrete runtime prerequisite definition resolved for a specific target CPU (`x64` or `arm64`).
struct RuntimePrerequisiteSpec {
    std::string            id;
    std::string            preset            = "Custom";
    std::string            displayName       = "Required Runtime";
    std::string            distribution      = "download"; // `download` | `embedded` | `side-by-side`
    std::filesystem::path  buildSourcePath;                // Used at build time by `instbuild.exe`
    std::string            fileName          = "runtime-setup.exe";
    std::string            downloadUrl;
    std::string            sha256Hex;
    std::string            silentArgs;
    std::vector<std::uint32_t> successCodes  = {0u, 1638u};
    std::vector<std::uint32_t> rebootCodes   = {3010u, 1641u};
    std::string            detectRegHive     = "HKLM";
    std::string            detectRegKey;
    std::string            detectRegValue;
    std::string            detectMinVersion;
    std::string            detectFilePath;
    std::string            detectUpgradeCode;
    bool                   requiresElevation = true;
    bool                   optional          = false;
    int                    embeddedResId     = 0; // `kResIdPrereqPayloadBase + idx` when `embedded`
};

/// Parses a comma- or space-separated list of unsigned exit codes (`"3010, 1641, 23"`).
inline std::vector<std::uint32_t> ParseExitCodeList(std::string_view csv) {
    std::vector<std::uint32_t> out;
    std::string tok;
    auto flushTok = [&]() {
        std::string t = TrimAscii(tok);
        tok.clear();
        if (t.empty()) return;
        char* endPtr = nullptr;
        const unsigned long v = std::strtoul(t.c_str(), &endPtr, 10);
        if (endPtr && *endPtr == '\0') {
            out.push_back(static_cast<std::uint32_t>(v));
        }
    };
    for (char c : csv) {
        if (c == ',' || c == ';' || c == ' ' || c == '\t') flushTok();
        else tok.push_back(c);
    }
    flushTok();
    return out;
}

inline std::string FormatExitCodeList(const std::vector<std::uint32_t>& codes) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < codes.size(); ++i) {
        if (i > 0) oss << ",";
        oss << codes[i];
    }
    return oss.str();
}

/// Resolves all `[Prerequisite.<id>]` sections from a `.rsip` document for `arch` (`"arm64"` or `"x64"`).
inline std::vector<RuntimePrerequisiteSpec> ResolveManifestPrerequisites(
    const RswManifestDocument& doc,
    std::string_view arch)
{
    std::vector<RuntimePrerequisiteSpec> list;
    const std::filesystem::path baseDir = doc.sourcePath.empty()
        ? std::filesystem::current_path()
        : doc.sourcePath.parent_path();

    int idx = 0;
    for (const auto& pid : doc.GetNamedSectionIds("Prerequisite")) {
        RuntimePrerequisiteSpec sp{};
        sp.id     = pid;
        sp.preset = doc.GetString("Prerequisite", pid, "Preset", "Custom");
        const auto* cur = FindCuratedRuntimePreset(sp.preset);

        const std::string defName = cur ? std::string(cur->displayName) : pid;
        const std::string defFile = cur ? ExpandArchToken(cur->fileNamePattern, arch) : (pid + "-setup.exe");
        const std::string defUrl  = cur
            ? std::string(EqualsIgnoreCase(arch, "arm64") ? cur->urlArm64 : cur->urlX64)
            : "";
        const std::string defArgs = cur ? std::string(cur->silentArgs)   : "/quiet /norestart";
        const std::string defSucc = cur ? std::string(cur->successCodes) : "0, 1638";
        const std::string defReb  = cur ? std::string(cur->rebootCodes)  : "3010, 1641";

        sp.displayName  = ExpandArchToken(doc.GetString("Prerequisite", pid, "Name", defName), arch);
        sp.distribution = AsciiLower(doc.GetString("Prerequisite", pid, "Distribution", "download"));
        sp.fileName     = ExpandArchToken(doc.GetString("Prerequisite", pid, "FileName", defFile), arch);

        const std::string rawSrc = doc.GetString("Prerequisite", pid, "SourceFile");
        if (!rawSrc.empty()) {
            std::filesystem::path p(Utf8ToWide(ExpandArchToken(rawSrc, arch)));
            sp.buildSourcePath = (p.is_absolute() ? p : (baseDir / p)).lexically_normal();
            if (doc.GetString("Prerequisite", pid, "FileName").empty()) {
                sp.fileName = WideToUtf8(sp.buildSourcePath.filename().wstring());
            }
        }

        const std::string urlArchKey = EqualsIgnoreCase(arch, "arm64") ? "UrlArm64" : "UrlX64";
        std::string chosenUrl = doc.GetString("Prerequisite", pid, urlArchKey);
        if (chosenUrl.empty()) chosenUrl = doc.GetString("Prerequisite", pid, "Url", defUrl);
        sp.downloadUrl = ExpandArchToken(chosenUrl, arch);

        sp.sha256Hex         = AsciiLower(TrimAscii(doc.GetString("Prerequisite", pid, "Sha256")));
        sp.silentArgs        = ExpandArchToken(doc.GetString("Prerequisite", pid, "SilentArgs", defArgs), arch);
        sp.successCodes      = ParseExitCodeList(doc.GetString("Prerequisite", pid, "SuccessExitCodes", defSucc));
        sp.rebootCodes       = ParseExitCodeList(doc.GetString("Prerequisite", pid, "RebootExitCodes", defReb));
        if (sp.successCodes.empty()) sp.successCodes.push_back(0u);

        sp.detectRegHive     = doc.GetString("Prerequisite", pid, "DetectRegistryHive", "HKLM");
        sp.detectRegKey      = ExpandArchToken(doc.GetString("Prerequisite", pid, "DetectRegistryKey"), arch);
        sp.detectRegValue    = doc.GetString("Prerequisite", pid, "DetectRegistryValue");
        sp.detectMinVersion  = doc.GetString("Prerequisite", pid, "DetectMinVersion");
        sp.detectFilePath    = ExpandArchToken(doc.GetString("Prerequisite", pid, "DetectFilePath"), arch);
        sp.detectUpgradeCode = doc.GetString("Prerequisite", pid, "DetectUpgradeCode");
        sp.requiresElevation = doc.GetBool("Prerequisite", pid, "RequiresElevation", true);
        sp.optional          = doc.GetBool("Prerequisite", pid, "Optional", false);
        sp.embeddedResId     = (sp.distribution == "embedded") ? (kResIdPrereqPayloadBase + idx) : 0;

        list.push_back(std::move(sp));
        ++idx;
    }
    return list;
}

/// Serializes resolved prerequisites into `kResIdPrereqManifest` (`#205`) inside `Setup.exe`.
inline std::string SerializePrerequisiteTableIni(const std::vector<RuntimePrerequisiteSpec>& list) {
    auto cleanVal = [](std::string s) {
        for (char& c : s) {
            if (c == '\r' || c == '\n') c = ' ';
        }
        return s;
    };
    std::ostringstream out;
    out << "Count=" << list.size() << "\n";
    for (std::size_t i = 0; i < list.size(); ++i) {
        const auto& p = list[i];
        const std::string pfx = "P" + std::to_string(i) + "_";
        out << pfx << "Id="                << cleanVal(p.id) << "\n"
            << pfx << "Preset="            << cleanVal(p.preset) << "\n"
            << pfx << "Name="              << cleanVal(p.displayName) << "\n"
            << pfx << "Distribution="      << cleanVal(p.distribution) << "\n"
            << pfx << "FileName="          << cleanVal(p.fileName) << "\n"
            << pfx << "Url="               << cleanVal(p.downloadUrl) << "\n"
            << pfx << "Sha256="            << cleanVal(p.sha256Hex) << "\n"
            << pfx << "SilentArgs="        << cleanVal(p.silentArgs) << "\n"
            << pfx << "SuccessCodes="      << FormatExitCodeList(p.successCodes) << "\n"
            << pfx << "RebootCodes="       << FormatExitCodeList(p.rebootCodes) << "\n"
            << pfx << "DetectRegHive="     << cleanVal(p.detectRegHive) << "\n"
            << pfx << "DetectRegKey="      << cleanVal(p.detectRegKey) << "\n"
            << pfx << "DetectRegValue="    << cleanVal(p.detectRegValue) << "\n"
            << pfx << "DetectMinVersion="  << cleanVal(p.detectMinVersion) << "\n"
            << pfx << "DetectFilePath="    << cleanVal(p.detectFilePath) << "\n"
            << pfx << "DetectUpgradeCode=" << cleanVal(p.detectUpgradeCode) << "\n"
            << pfx << "RequiresElevation=" << (p.requiresElevation ? "true" : "false") << "\n"
            << pfx << "Optional="          << (p.optional ? "true" : "false") << "\n"
            << pfx << "EmbeddedResId="     << p.embeddedResId << "\n";
    }
    return out.str();
}

/// Deserializes `kResIdPrereqManifest` (`#205`) inside `SetupWizard.exe` at runtime.
inline std::vector<RuntimePrerequisiteSpec> ParsePrerequisiteTableIni(std::string_view text) {
    std::map<std::string, std::string> kv;
    std::istringstream iss{std::string(text)};
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == ';') continue;
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    const int count = std::clamp(std::atoi(kv["Count"].c_str()), 0, 64);
    std::vector<RuntimePrerequisiteSpec> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        const std::string pfx = "P" + std::to_string(i) + "_";
        RuntimePrerequisiteSpec p{};
        p.id                = kv[pfx + "Id"];
        p.preset            = kv[pfx + "Preset"];
        p.displayName       = kv[pfx + "Name"];
        p.distribution      = kv[pfx + "Distribution"];
        p.fileName          = kv[pfx + "FileName"];
        p.downloadUrl       = kv[pfx + "Url"];
        p.sha256Hex         = kv[pfx + "Sha256"];
        p.silentArgs        = kv[pfx + "SilentArgs"];
        p.successCodes      = ParseExitCodeList(kv[pfx + "SuccessCodes"]);
        p.rebootCodes       = ParseExitCodeList(kv[pfx + "RebootCodes"]);
        if (p.successCodes.empty()) p.successCodes.push_back(0u);
        p.detectRegHive     = kv[pfx + "DetectRegHive"];
        p.detectRegKey      = kv[pfx + "DetectRegKey"];
        p.detectRegValue    = kv[pfx + "DetectRegValue"];
        p.detectMinVersion  = kv[pfx + "DetectMinVersion"];
        p.detectFilePath    = kv[pfx + "DetectFilePath"];
        p.detectUpgradeCode = kv[pfx + "DetectUpgradeCode"];
        p.requiresElevation = !EqualsIgnoreCase(kv[pfx + "RequiresElevation"], "false");
        p.optional          = EqualsIgnoreCase(kv[pfx + "Optional"], "true");
        p.embeddedResId     = std::atoi(kv[pfx + "EmbeddedResId"].c_str());
        out.push_back(std::move(p));
    }
    return out;
}

/// Compares two dotted numeric version strings (`"10.0.2"` >= `"10.0.0"`).
inline int CompareDottedVersions(std::string_view a, std::string_view b) noexcept {
    auto parseParts = [](std::string_view s) {
        std::array<int, 4> p{0, 0, 0, 0};
        std::size_t idx = 0;
        for (char c : s) {
            if (c == '.') {
                if (++idx >= 4) break;
            } else if (c >= '0' && c <= '9') {
                p[idx] = p[idx] * 10 + (c - '0');
            } else {
                break;
            }
        }
        return p;
    };
    const auto pa = parseParts(a);
    const auto pb = parseParts(b);
    for (std::size_t i = 0; i < 4; ++i) {
        if (pa[i] < pb[i]) return -1;
        if (pa[i] > pb[i]) return  1;
    }
    return 0;
}

/// Reads a registry value (`REG_SZ` or `REG_DWORD`) as a UTF-8 string using `KEY_WOW64_64KEY` (or `32KEY`).
inline bool ReadRegValueStringView(HKEY hRoot, const std::wstring& subKey, const std::wstring& valName, REGSAM wowFlag, std::string& outVal) {
    outVal.clear();
    HKEY hKey = nullptr;
    if (::RegOpenKeyExW(hRoot, subKey.c_str(), 0, KEY_READ | wowFlag, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    if (valName.empty()) {
        ::RegCloseKey(hKey);
        return true; // Key existence alone satisfied!
    }
    DWORD dwType = 0;
    BYTE buf[1024]{};
    DWORD cb = sizeof(buf) - sizeof(wchar_t);
    const LSTATUS st = ::RegQueryValueExW(hKey, valName.c_str(), nullptr, &dwType, buf, &cb);
    ::RegCloseKey(hKey);
    if (st != ERROR_SUCCESS) return false;

    if (dwType == REG_DWORD && cb >= sizeof(DWORD)) {
        DWORD v = 0;
        std::memcpy(&v, buf, sizeof(DWORD));
        outVal = std::to_string(v);
        return v != 0;
    }
    if (dwType == REG_SZ || dwType == REG_EXPAND_SZ) {
        outVal = TrimAscii(WideToUtf8(reinterpret_cast<const wchar_t*>(buf)));
        return !outVal.empty();
    }
    return true;
}

/// Checks whether a `.NET` shared framework major version (`8`, `9`, or `10`) is installed for `arch` (`"arm64"` or `"x64"`).
inline bool IsDotNetSharedFxInstalled(std::string_view fxFolderName, int majorVer, std::string_view arch) {
    // 1. Check `HKLM\SOFTWARE\dotnet\Setup\InstalledVersions\<arch>\sharedfx\<fxFolderName>`
    const std::string archFolder = EqualsIgnoreCase(arch, "arm64") ? "arm64" : "x64";
    const std::wstring regPath = L"SOFTWARE\\dotnet\\Setup\\InstalledVersions\\" +
                                 Utf8ToWide(archFolder) + L"\\sharedfx\\" + Utf8ToWide(fxFolderName);
    HKEY hKey = nullptr;
    if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, regPath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        const std::wstring prefix = std::to_wstring(majorVer) + L".";
        DWORD idx = 0;
        wchar_t valName[256]{};
        for (;;) {
            DWORD cch = static_cast<DWORD>(std::size(valName));
            DWORD dwType = 0;
            DWORD dwData = 0;
            DWORD cbData = sizeof(dwData);
            if (::RegEnumValueW(hKey, idx++, valName, &cch, nullptr, &dwType, reinterpret_cast<LPBYTE>(&dwData), &cbData) != ERROR_SUCCESS) {
                break;
            }
            if ( std::wstring_view(valName, cch).rfind(prefix, 0) == 0 && dwData != 0) {
                ::RegCloseKey(hKey);
                return true;
            }
        }
        ::RegCloseKey(hKey);
    }

    // 2. Fallback: Check `%ProgramW6432%\dotnet\shared\<fxFolderName>\<major>.*`
    //    (Note: on Windows 11 ARM64, native `arm64` dotnet lives in `C:\Program Files\dotnet`,
    //     while `x64` dotnet lives in `C:\Program Files\dotnet\x64`!)
    wchar_t pfBuf[MAX_PATH * 2]{};
    ::GetEnvironmentVariableW(L"ProgramW6432", pfBuf, static_cast<DWORD>(std::size(pfBuf)));
    std::filesystem::path dotnetRoot = pfBuf[0] ? std::filesystem::path(pfBuf) / L"dotnet" : std::filesystem::path(L"C:\\Program Files\\dotnet");
    const auto host = QueryHostPlatform();
    if (host.nativeMachine == IMAGE_FILE_MACHINE_ARM64 && EqualsIgnoreCase(arch, "x64")) {
        if (std::filesystem::exists(dotnetRoot / L"x64" / L"shared")) {
            dotnetRoot /= L"x64";
        }
    }
    std::error_code ec;
    const auto fxDir = dotnetRoot / L"shared" / Utf8ToWide(fxFolderName);
    if (std::filesystem::exists(fxDir, ec)) {
        const std::wstring prefix = std::to_wstring(majorVer) + L".";
        for (const auto& de : std::filesystem::directory_iterator(fxDir, ec)) {
            if (de.is_directory(ec) && de.path().filename().wstring().rfind(prefix, 0) == 0) {
                return true;
            }
        }
    }
    return false;
}

/// Fast (<1ms) non-invasive check returning `true` if `spec` is ALREADY installed on this machine!
inline bool IsPrerequisiteSatisfied(const RuntimePrerequisiteSpec& spec, std::string_view arch) {
    if (EqualsIgnoreCase(spec.preset, "DotNet10Desktop")) return IsDotNetSharedFxInstalled("Microsoft.WindowsDesktop.App", 10, arch);
    if (EqualsIgnoreCase(spec.preset, "DotNet10AspNet"))  return IsDotNetSharedFxInstalled("Microsoft.AspNetCore.App",     10, arch);
    if (EqualsIgnoreCase(spec.preset, "DotNet9Desktop"))  return IsDotNetSharedFxInstalled("Microsoft.WindowsDesktop.App",  9, arch);
    if (EqualsIgnoreCase(spec.preset, "DotNet8Desktop"))  return IsDotNetSharedFxInstalled("Microsoft.WindowsDesktop.App",  8, arch);

    if (EqualsIgnoreCase(spec.preset, "EdgeWebView2")) {
        const std::wstring wvKey = L"SOFTWARE\\Microsoft\\EdgeUpdate\\Clients\\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}";
        std::string pv;
        if ((ReadRegValueStringView(HKEY_LOCAL_MACHINE, wvKey, L"pv", KEY_WOW64_32KEY, pv) ||
             ReadRegValueStringView(HKEY_LOCAL_MACHINE, wvKey, L"pv", KEY_WOW64_64KEY, pv) ||
             ReadRegValueStringView(HKEY_CURRENT_USER,  wvKey, L"pv", 0, pv)) &&
            !pv.empty() && pv != "0.0.0.0") {
            return true;
        }
        return false;
    }

    if (EqualsIgnoreCase(spec.preset, "VcRedist2022")) {
        const std::wstring vcKey = L"SOFTWARE\\Microsoft\\VisualStudio\\14.0\\VC\\Runtimes\\" +
                                   Utf8ToWide(EqualsIgnoreCase(arch, "arm64") ? "arm64" : "x64");
        std::string inst;
        return ReadRegValueStringView(HKEY_LOCAL_MACHINE, vcKey, L"Installed", KEY_WOW64_64KEY, inst) && inst == "1";
    }

    // Custom prerequisite detection rules (`DetectUpgradeCode`, `DetectRegistryKey`, `DetectFilePath`)
    if (!spec.detectUpgradeCode.empty()) {
        wchar_t prodBuf[39]{};
        if (::MsiEnumRelatedProductsW(Utf8ToWide(spec.detectUpgradeCode).c_str(), 0, 0, prodBuf) == ERROR_SUCCESS) {
            return true;
        }
    }
    if (!spec.detectRegKey.empty()) {
        HKEY hive = EqualsIgnoreCase(spec.detectRegHive, "HKCU") ? HKEY_CURRENT_USER : HKEY_LOCAL_MACHINE;
        std::string val;
        if (ReadRegValueStringView(hive, Utf8ToWide(spec.detectRegKey), Utf8ToWide(spec.detectRegValue), KEY_WOW64_64KEY, val)) {
            if (spec.detectMinVersion.empty()) return true;
            return CompareDottedVersions(val, spec.detectMinVersion) >= 0;
        }
    }
    if (!spec.detectFilePath.empty()) {
        wchar_t expBuf[MAX_PATH * 4]{};
        ::ExpandEnvironmentStringsW(Utf8ToWide(spec.detectFilePath).c_str(), expBuf, static_cast<DWORD>(std::size(expBuf)));
        std::error_code ec;
        const std::filesystem::path fp(expBuf);
        if (std::filesystem::exists(fp, ec)) {
            if (spec.detectMinVersion.empty()) return true;
            const auto pe = HarvestPeVersionAndLanguage(fp);
            return !pe.version.empty() && CompareDottedVersions(pe.version, spec.detectMinVersion) >= 0;
        }
    }
    return false;
}

/// Computes lowercase SHA-256 hex digest of a file on disk via Windows CNG (`bcrypt.dll`).
inline bool ComputeFileSha256Hex(const std::filesystem::path& filePath, std::string& outHex) {
    outHex.clear();
    std::ifstream in(filePath, std::ios::binary);
    if (!in) return false;

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;

    DWORD objLen = 0, cbRes = 0;
    ::BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cbRes, 0);
    std::vector<UCHAR> hashObj(objLen);
    bool ok = (::BCryptCreateHash(hAlg, &hHash, hashObj.data(), objLen, nullptr, 0, 0) == 0);

    std::array<char, 32768> buf{};
    while (ok && in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const auto n = in.gcount();
        if (n > 0) {
            ok = (::BCryptHashData(hHash, reinterpret_cast<PUCHAR>(buf.data()), static_cast<ULONG>(n), 0) == 0);
        }
    }
    std::array<UCHAR, 32> digest{};
    if (ok) {
        ok = (::BCryptFinishHash(hHash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0);
    }
    if (hHash) ::BCryptDestroyHash(hHash);
    if (hAlg)  ::BCryptCloseAlgorithmProvider(hAlg, 0);
    if (!ok) return false;

    static constexpr char kHex[] = "0123456789abcdef";
    outHex.reserve(64);
    for (UCHAR b : digest) {
        outHex.push_back(kHex[(b >> 4) & 0x0F]);
        outHex.push_back(kHex[b & 0x0F]);
    }
    return true;
}

/// Streams a runtime installer from HTTPS via stock Windows `WinHTTP` (`aka.ms` 301/302 redirect aware!).
inline bool DownloadFileViaWinHttp(
    const std::string& httpsUrl,
    const std::filesystem::path& destFile,
    const std::atomic<bool>* cancelFlag,
    const std::function<void(std::uint64_t downloadedBytes, std::uint64_t totalBytes)>& onProgress,
    std::string& outError)
{
    const std::wstring wUrl = Utf8ToWide(httpsUrl);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t hostBuf[256]{};
    wchar_t pathBuf[2048]{};
    uc.lpszHostName     = hostBuf;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(hostBuf));
    uc.lpszUrlPath      = pathBuf;
    uc.dwUrlPathLength  = static_cast<DWORD>(std::size(pathBuf));
    if (!::WinHttpCrackUrl(wUrl.c_str(), static_cast<DWORD>(wUrl.size()), 0, &uc)) {
        outError = "Invalid prerequisite URL: " + httpsUrl;
        return false;
    }

    HINTERNET hSession = ::WinHttpOpen(
        L"RaggieSoftSetupWizard/0.3 (Windows NT 10.0; Win64)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        outError = "WinHttpOpen failed (Win32 " + std::to_string(::GetLastError()) + ").";
        return false;
    }

    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    ::WinHttpSetOption(hSession, WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    ::WinHttpSetTimeouts(hSession, 15000, 15000, 30000, 60000);

    HINTERNET hConnect = ::WinHttpConnect(hSession, hostBuf, uc.nPort, 0);
    const DWORD reqFlags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = hConnect
        ? ::WinHttpOpenRequest(hConnect, L"GET", pathBuf, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, reqFlags)
        : nullptr;

    bool ok = false;
    if (hRequest &&
        ::WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        ::WinHttpReceiveResponse(hRequest, nullptr)) {

        DWORD statusCode = 0;
        DWORD cbStatus   = sizeof(statusCode);
        ::WinHttpQueryHeaders(
            hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &cbStatus, WINHTTP_NO_HEADER_INDEX);

        if (statusCode >= 200 && statusCode < 300) {
            wchar_t lenBuf[64]{};
            DWORD cbLen = sizeof(lenBuf);
            std::uint64_t totalBytes = 0;
            if (::WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX, lenBuf, &cbLen, WINHTTP_NO_HEADER_INDEX)) {
                totalBytes = static_cast<std::uint64_t>(std::wcstoull(lenBuf, nullptr, 10));
            }

            std::error_code ec;
            std::filesystem::create_directories(destFile.parent_path(), ec);
            std::ofstream out(destFile, std::ios::binary | std::ios::trunc);
            if (!out) {
                outError = "Could not open destination file for download: " + WideToUtf8(destFile.wstring());
            } else {
                std::vector<char> chunk(65536);
                std::uint64_t downloaded = 0;
                ok = true;
                for (;;) {
                    if (cancelFlag && cancelFlag->load()) {
                        outError = "Canceled by user.";
                        ok = false;
                        break;
                    }
                    DWORD avail = 0;
                    if (!::WinHttpQueryDataAvailable(hRequest, &avail)) {
                        outError = "Network read interrupted (Win32 " + std::to_string(::GetLastError()) + ").";
                        ok = false;
                        break;
                    }
                    if (avail == 0) break;
                    const DWORD toRead = std::min<DWORD>(avail, static_cast<DWORD>(chunk.size()));
                    DWORD got = 0;
                    if (!::WinHttpReadData(hRequest, chunk.data(), toRead, &got)) {
                        outError = "WinHttpReadData failed (Win32 " + std::to_string(::GetLastError()) + ").";
                        ok = false;
                        break;
                    }
                    if (got == 0) break;
                    out.write(chunk.data(), static_cast<std::streamsize>(got));
                    downloaded += got;
                    if (onProgress) onProgress(downloaded, totalBytes);
                }
                out.close();
                if (!ok) std::filesystem::remove(destFile, ec);
            }
        } else {
            outError = "Server returned HTTP status " + std::to_string(statusCode) + " for " + httpsUrl;
        }
    } else {
        outError = "Could not connect to download server (Win32 " + std::to_string(::GetLastError()) + ").";
    }

    if (hRequest) ::WinHttpCloseHandle(hRequest);
    if (hConnect) ::WinHttpCloseHandle(hConnect);
    ::WinHttpCloseHandle(hSession);
    return ok;
}

/// Classifies the exit code returned by a runtime prerequisite installer!
enum class PrereqExitOutcome {
    Succeeded,
    SucceededRebootRequested,
    Failed
};

inline PrereqExitOutcome ClassifyPrerequisiteExitCode(
    const RuntimePrerequisiteSpec& spec,
    std::uint32_t exitCode) noexcept
{
    for (std::uint32_t rc : spec.rebootCodes) {
        if (rc == exitCode) return PrereqExitOutcome::SucceededRebootRequested;
    }
    for (std::uint32_t sc : spec.successCodes) {
        if (sc == exitCode) return PrereqExitOutcome::Succeeded;
    }
    return PrereqExitOutcome::Failed;
}

/// Executes a staged prerequisite `.exe` or `.msi` binary BEFORE `Product.msi` runs!
inline PrereqExitOutcome ExecuteStagedPrerequisiteInstaller(
    const RuntimePrerequisiteSpec& spec,
    const std::filesystem::path& installerPath,
    std::uint32_t& outRawExitCode,
    std::string& outError)
{
    outRawExitCode = 1;
    std::wstring ext = installerPath.extension().wstring();
    for (wchar_t& c : ext) if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c - L'A' + L'a');

    std::wstring cmdLine;
    if (ext == L".msi") {
        wchar_t sysDir[MAX_PATH]{};
        ::GetSystemDirectoryW(sysDir, MAX_PATH);
        const std::filesystem::path msiExec = std::filesystem::path(sysDir) / L"msiexec.exe";
        cmdLine = L"\"" + msiExec.wstring() + L"\" /i \"" + installerPath.wstring() + L"\" /qn /norestart";
        if (!spec.silentArgs.empty()) {
            cmdLine += L" " + Utf8ToWide(spec.silentArgs);
        }
    } else {
        cmdLine = L"\"" + installerPath.wstring() + L"\"";
        if (!spec.silentArgs.empty()) {
            cmdLine += L" " + Utf8ToWide(spec.silentArgs);
        }
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const std::wstring workDir = installerPath.parent_path().wstring();
    if (!::CreateProcessW(
            nullptr, cmdLine.data(), nullptr, nullptr, FALSE,
            CREATE_NO_WINDOW, nullptr, workDir.empty() ? nullptr : workDir.c_str(), &si, &pi)) {
        outError = "Failed launching prerequisite '" + spec.displayName + "' (Win32 " + std::to_string(::GetLastError()) + ").";
        return PrereqExitOutcome::Failed;
    }

    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    ::GetExitCodeProcess(pi.hProcess, &code);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    outRawExitCode = static_cast<std::uint32_t>(code);

    const auto outcome = ClassifyPrerequisiteExitCode(spec, outRawExitCode);
    if (outcome == PrereqExitOutcome::Failed) {
        outError = "Prerequisite '" + spec.displayName + "' exited with code " + std::to_string(outRawExitCode) + ".";
    }
    return outcome;
}

} // namespace rsw
