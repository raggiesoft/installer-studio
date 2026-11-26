// ================================================================================
// FILE: src/common/RswNativeAndSuiteEngine.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file, and why did `v0.6.0` introduce a second packaging engine
//   alongside Microsoft Windows Installer (`msi.dll`)?
//     Windows Installer (`msi.dll`) is unbeatable for enterprise Group Policy
//     (`.mst` transforms, `.msp` patches, `msiexec /a` network images), and
//     Installer Studio continues to treat `Engine = msi` as a first-class citizen.
//
//     However, three everyday scenarios shine when powered by a **zero-`msi.dll`
//     native C++20 engine** built directly on Windows `LZMS` (`RSWPAK1`):
//       1. **`PackageKind = sfx` (Lightweight Self-Extracting `.EXE` Builder)**:
//          - Turns any folder or `.zip` archive into a portable, accessible `.exe`
//            with selectable UAC levels (`asInvoker`, `highestAvailable`,
//            `requireAdministrator`), zero Windows *Installed apps* clutter, and
//            optional `PostExtractRun` / `PostExtractOpen` document launching!
//       2. **`Engine = native` (Native Direct Engine & "Simple Setup Project")**:
//          - Installs thousands of book/audio/tool files in milliseconds with
//            ZERO `Global\_MSIExecute` mutex contention (`Error 1618` impossible!).
//          - Maintains a real **Atomic Rollback Journal** (`.rsw-rollback.jnl` +
//            `.rswbak` file backups): if canceled or interrupted mid-write, every
//            partial file, `.lnk` shortcut, and registry key rolls back cleanly!
//          - Copies the Authenticode-signed setup stub to `<InstallDir>\Uninstall.exe`
//            and registers cleanly in `HKLM`/`HKCU\...\Uninstall\<UpgradeCode>`.
//       3. **`PackageKind = suite` (Unified Suite Setup & Dynamic `ARPSYSTEMCOMPONENT`)**:
//          - Chains multiple `.msi` and `Engine = native` tools under **one** UAC
//            prompt and **one** unified Suite Maintenance entry in *Installed apps*.
//          - Dynamically adopts already-installed standalone sibling `.msi` or
//            `native` apps (`RS_SUITE_MANAGED=1` / `SystemComponent=1`) so Windows
//            *Installed apps* never gets cluttered with 8 separate entries—and
//            cleanly restores standalone visibility (`RS_SUITE_MANAGED=0`) if a
//            user detaches/keeps individual apps when removing the Suite!
// ================================================================================

#pragma once

#include "RswCryptoAndPe.hpp"
#include "RswManifestAst.hpp"
#include "RswPlatform.hpp"
#include "RswPrerequisites.hpp"
#include "RswVersion.hpp"
#include "RswZipAndPak.hpp"

#include <msi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "msi.lib")

namespace rsw {

// ================================================================================
// 1. NATIVE & SFX RUNTIME MANIFEST (`kResIdNativeManifest` `#221`)
// ================================================================================
struct NativeFolderRule {
    std::string targetRoot = "INSTALLDIR";
    std::string targetSubdir;
    std::string featureId  = "Main";
    bool        removeOnUninstall = true;
};

struct NativeShortcutRule {
    std::string id;
    std::string name;
    std::string targetFile; // Relative to `INSTALLDIR`
    std::string location   = "StartMenu"; // `StartMenu` | `Desktop`
    std::string arguments;
    std::string description;
    std::string featureId  = "Main";
};

struct NativeRegistryRule {
    std::string root       = "HKLM"; // `HKLM` | `HKCU` | `HKCR` | `HKMU`
    std::string key;
    std::string name;
    std::string value;
    std::string type       = "string"; // `string` | `expand_string` | `dword`
    std::string featureId  = "Main";
};

struct NativeFileAssocRule {
    std::string extension;
    std::string progId;
    std::string description;
    std::string targetFile;
    std::string verbs      = "open:&Open:\"%1\"";
    std::string contentType;
    std::string perceivedType;
    int         iconIndex  = 0;
    std::string featureId  = "Main";
};

struct NativeOrSfxManifest {
    std::string   mode                  = "native"; // `"sfx"` or `"native"`
    std::string   productName           = "Application";
    std::string   tagline;
    std::string   websiteLabel;
    std::string   version               = "1.0.0";
    std::string   manufacturer          = "RaggieSoft";
    std::string   upgradeCode;
    std::string   productCode;
    std::string   architecture          = "arm64";
    bool          perMachine            = false;
    std::string   requestedExecLevel    = "asInvoker";
    bool          hideFromInstalledApps = false;
    std::string   aboutUrl;
    int           minWindowsBuild       = 22000;
    std::string   osLaunchMessage       = "This application requires Windows 11 (Build 22000) or later.";
    int           silentRebootCode      = 3010;
    std::string   defaultSubdir;
    std::string   startMenuFolder;
    std::string   mainExecutable;
    std::string   wizardLayout          = "simple";
    std::string   accentPrimaryHex      = "#2563EB";
    std::string   accentSecondaryHex    = "#FBBF24";
    std::string   installBtnText        = "&Install";
    std::string   finishLaunchText      = "&Launch application when finished";
    bool          offerLaunchOnFinish   = true;
    std::uint64_t uncompressedBytes     = 0;

    // `[SFX]` post-extraction & Software Installation `%TEMP%` wrapper options
    std::string   postExtractRun;
    std::string   postExtractArgs;
    std::string   postExtractOpen;
    bool          extractToTemp         = false;
    bool          allowChangeFolder     = true;
    bool          silentOverwrite       = true;
    bool          cleanupTempAfterRun   = false;
    std::string   waitMode              = "auto"; // `auto` | `none` | `direct` | `job_tree` | `process_name`
    std::string   waitForProcess;                 // e.g. `_ISDEL.EXE; _INS*._MP; setup-worker.exe`
    int           waitSpawnGraceMs      = 1500;
    bool          autoStartExtract      = false;

    std::vector<NativeFolderRule>    folders;
    std::vector<NativeShortcutRule>  shortcuts;
    std::vector<NativeRegistryRule>  registries;
    std::vector<NativeFileAssocRule> fileAssocs;
};

namespace native_detail {

/// Optional dependency-injected virtual registry backing folder (`RSW_TEST_REG_STORE_DIR`) used
/// exclusively by `insttests.exe` when running inside zero-registry CI sandboxes (`ERROR_ACCESS_DENIED`).
/// Returns an empty path in 100% of normal desktop runs so real Win32 `HKLM`/`HKCU` APIs are used directly.
inline std::filesystem::path GetVirtualRegistryStoreRoot() {
    wchar_t buf[MAX_PATH * 4]{};
    const DWORD n = ::GetEnvironmentVariableW(L"RSW_TEST_REG_STORE_DIR", buf, static_cast<DWORD>(std::size(buf)));
    if (n == 0 || n >= std::size(buf)) return {};
    return std::filesystem::path(buf);
}

inline std::filesystem::path ResolveVirtualRegKeyDir(
    const std::filesystem::path& storeRoot,
    std::string_view rootCode,
    std::wstring_view subKey)
{
    std::wstring normSub(subKey);
    for (wchar_t& ch : normSub) {
        if (ch == L'/') ch = L'\\';
    }
    while (!normSub.empty() && normSub.front() == L'\\') normSub.erase(normSub.begin());
    return (storeRoot / Utf8ToWide(AsciiLower(rootCode)) / normSub).lexically_normal();
}

inline std::wstring VirtualRegValueFileName(std::wstring_view valName) {
    if (valName.empty()) return L"_default_.regval";
    std::wstring clean(valName);
    for (wchar_t& ch : clean) {
        if (ch == L'\\' || ch == L'/' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'"' || ch == L'<' || ch == L'>' || ch == L'|') {
            ch = L'_';
        }
    }
    return clean + L".regval";
}

inline bool VirtualRegWriteValue(
    const std::filesystem::path& storeRoot,
    std::string_view rootCode,
    std::wstring_view subKey,
    std::wstring_view valName,
    std::string_view utf8Val,
    bool& outCreatedNewKey)
{
    std::error_code ec;
    const auto keyDir = ResolveVirtualRegKeyDir(storeRoot, rootCode, subKey);
    outCreatedNewKey  = !std::filesystem::exists(keyDir, ec);
    std::filesystem::create_directories(keyDir, ec);
    std::ofstream out(keyDir / VirtualRegValueFileName(valName), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(utf8Val.data(), static_cast<std::streamsize>(utf8Val.size()));
    return out.good();
}

inline bool VirtualRegReadValue(
    const std::filesystem::path& storeRoot,
    std::string_view rootCode,
    std::wstring_view subKey,
    std::wstring_view valName,
    std::string& outVal)
{
    std::error_code ec;
    const auto vf = ResolveVirtualRegKeyDir(storeRoot, rootCode, subKey) / VirtualRegValueFileName(valName);
    if (!std::filesystem::is_regular_file(vf, ec)) return false;
    std::ifstream in(vf, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    outVal = ss.str();
    return true;
}

inline void VirtualRegDeleteValue(
    const std::filesystem::path& storeRoot,
    std::string_view rootCode,
    std::wstring_view subKey,
    std::wstring_view valName) noexcept
{
    std::error_code ec;
    std::filesystem::remove(ResolveVirtualRegKeyDir(storeRoot, rootCode, subKey) / VirtualRegValueFileName(valName), ec);
}

inline void VirtualRegDeleteTree(
    const std::filesystem::path& storeRoot,
    std::string_view rootCode,
    std::wstring_view subKey) noexcept
{
    std::error_code ec;
    std::filesystem::remove_all(ResolveVirtualRegKeyDir(storeRoot, rootCode, subKey), ec);
}

inline std::string EscapeSingleLine(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (c == '\r') continue;
        if (c == '\\') out += "\\\\";
        else if (c == '\n') out += "\\n";
        else out.push_back(c);
    }
    return out;
}

inline std::string UnescapeSingleLine(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) {
            if (s[i + 1] == 'n') { out += "\r\n"; ++i; continue; }
            if (s[i + 1] == '\\') { out.push_back('\\'); ++i; continue; }
        }
        out.push_back(s[i]);
    }
    return out;
}

inline std::map<std::string, std::string> ParseFlatIniMap(std::string_view text) {
    std::map<std::string, std::string> kv;
    std::istringstream iss{std::string(text)};
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '[' || line.front() == ';') continue;
        const auto eq = line.find('=');
        if (eq != std::string::npos) {
            kv[line.substr(0, eq)] = UnescapeSingleLine(line.substr(eq + 1));
        }
    }
    return kv;
}

inline std::string ExpandInstallTokens(
    std::string raw,
    const std::filesystem::path& installDir,
    std::string_view productVersion)
{
    std::string dirWithSlash = WideToUtf8(installDir.wstring());
    if (!dirWithSlash.empty() && dirWithSlash.back() != '\\') {
        dirWithSlash.push_back('\\');
    }
    auto replaceAll = [](std::string& s, std::string_view needle, std::string_view repl) {
        std::size_t pos = 0;
        while ((pos = s.find(needle, pos)) != std::string::npos) {
            s.replace(pos, needle.size(), repl);
            pos += repl.size();
        }
    };
    replaceAll(raw, "[INSTALLDIR]", dirWithSlash);
    replaceAll(raw, "[ProductVersion]", productVersion);
    return raw;
}

/// Resolves standard Windows Installer root folder tokens (`INSTALLDIR`, `AppDataFolder`, etc.)
/// to concrete filesystem paths on the target Windows 11 machine!
inline std::filesystem::path ResolveSystemRootFolder(
    std::string_view rootToken,
    const std::filesystem::path& installDir,
    std::string_view startMenuSubfolder,
    bool perMachine)
{
    if (rootToken.empty() || EqualsIgnoreCase(rootToken, "INSTALLDIR")) {
        return installDir;
    }

    auto knownFolder = [](REFKNOWNFOLDERID rfid) -> std::filesystem::path {
        PWSTR psz = nullptr;
        std::filesystem::path res;
        if (SUCCEEDED(::SHGetKnownFolderPath(rfid, 0, nullptr, &psz)) && psz) {
            res = psz;
            ::CoTaskMemFree(psz);
        }
        return res;
    };

    if (EqualsIgnoreCase(rootToken, "CommonFiles64Folder"))     return knownFolder(FOLDERID_ProgramFilesCommonX64);
    if (EqualsIgnoreCase(rootToken, "CommonAppDataFolder"))     return knownFolder(FOLDERID_ProgramData);
    if (EqualsIgnoreCase(rootToken, "AppDataFolder"))           return knownFolder(FOLDERID_RoamingAppData);
    if (EqualsIgnoreCase(rootToken, "LocalAppDataFolder"))      return knownFolder(FOLDERID_LocalAppData);
    if (EqualsIgnoreCase(rootToken, "System64Folder"))          return knownFolder(FOLDERID_System);
    if (EqualsIgnoreCase(rootToken, "FontsFolder"))             return knownFolder(FOLDERID_Fonts);
    if (EqualsIgnoreCase(rootToken, "DesktopFolder")) {
        return knownFolder(perMachine ? FOLDERID_PublicDesktop : FOLDERID_Desktop);
    }
    if (EqualsIgnoreCase(rootToken, "ProgramMenuFolder")) {
        return knownFolder(perMachine ? FOLDERID_CommonPrograms : FOLDERID_Programs);
    }
    if (EqualsIgnoreCase(rootToken, "ApplicationProgramsFolder")) {
        auto base = knownFolder(perMachine ? FOLDERID_CommonPrograms : FOLDERID_Programs);
        if (!startMenuSubfolder.empty()) base /= Utf8ToWide(startMenuSubfolder);
        return base;
    }
    return installDir;
}

/// Creates a standard Windows Explorer `.lnk` Shell Link shortcut via `IShellLinkW` + `IPersistFile`.
inline bool CreateWindowsShellShortcut(
    const std::filesystem::path& lnkFilePath,
    const std::filesystem::path& targetExePath,
    std::string_view argumentsUtf8,
    std::string_view descriptionUtf8,
    const std::filesystem::path& iconPath,
    int iconIndex,
    std::string& outError)
{
    std::error_code ec;
    std::filesystem::create_directories(lnkFilePath.parent_path(), ec);

    const HRESULT hrCo = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const bool needUninit = (hrCo == S_OK || hrCo == S_FALSE);

    IShellLinkW* pLink = nullptr;
    HRESULT hr = ::CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pLink));
    if (FAILED(hr) || !pLink) {
        if (needUninit) ::CoUninitialize();
        outError = "CoCreateInstance(CLSID_ShellLink) failed creating shortcut '" + WideToUtf8(lnkFilePath.wstring()) + "'.";
        return false;
    }

    pLink->SetPath(targetExePath.wstring().c_str());
    pLink->SetWorkingDirectory(targetExePath.parent_path().wstring().c_str());
    if (!argumentsUtf8.empty()) {
        pLink->SetArguments(Utf8ToWide(argumentsUtf8).c_str());
    }
    if (!descriptionUtf8.empty()) {
        pLink->SetDescription(Utf8ToWide(descriptionUtf8).c_str());
    }
    if (!iconPath.empty() && std::filesystem::exists(iconPath, ec)) {
        pLink->SetIconLocation(iconPath.wstring().c_str(), iconIndex);
    } else {
        pLink->SetIconLocation(targetExePath.wstring().c_str(), 0);
    }

    IPersistFile* pPersist = nullptr;
    hr = pLink->QueryInterface(IID_PPV_ARGS(&pPersist));
    if (SUCCEEDED(hr) && pPersist) {
        hr = pPersist->Save(lnkFilePath.wstring().c_str(), TRUE);
        pPersist->Release();
    }
    pLink->Release();
    if (needUninit) ::CoUninitialize();

    if (FAILED(hr)) {
        outError = "IPersistFile::Save failed writing shortcut '" + WideToUtf8(lnkFilePath.wstring()) + "'.";
        return false;
    }
    return true;
}

inline HKEY ResolveRegistryRootHandle(std::string_view rootStr, bool perMachine) noexcept {
    if (EqualsIgnoreCase(rootStr, "HKCU")) return HKEY_CURRENT_USER;
    if (EqualsIgnoreCase(rootStr, "HKCR")) return HKEY_CLASSES_ROOT;
    if (EqualsIgnoreCase(rootStr, "HKU"))  return HKEY_USERS;
    if (EqualsIgnoreCase(rootStr, "HKMU")) return perMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    return HKEY_LOCAL_MACHINE;
}

} // namespace native_detail

inline std::string SerializeNativeOrSfxManifestIni(const NativeOrSfxManifest& m) {
    using native_detail::EscapeSingleLine;
    std::ostringstream out;
    out << "[NativeManifest]\n"
        << "Mode="                  << EscapeSingleLine(m.mode) << "\n"
        << "ProductName="           << EscapeSingleLine(m.productName) << "\n"
        << "Tagline="               << EscapeSingleLine(m.tagline) << "\n"
        << "WebsiteLabel="          << EscapeSingleLine(m.websiteLabel) << "\n"
        << "Version="               << EscapeSingleLine(m.version) << "\n"
        << "Manufacturer="          << EscapeSingleLine(m.manufacturer) << "\n"
        << "UpgradeCode="           << EscapeSingleLine(m.upgradeCode) << "\n"
        << "ProductCode="           << EscapeSingleLine(m.productCode) << "\n"
        << "Architecture="          << EscapeSingleLine(m.architecture) << "\n"
        << "PerMachine="            << (m.perMachine ? "true" : "false") << "\n"
        << "RequestedExecutionLevel=" << EscapeSingleLine(m.requestedExecLevel) << "\n"
        << "HideFromInstalledApps=" << (m.hideFromInstalledApps ? "true" : "false") << "\n"
        << "AboutUrl="              << EscapeSingleLine(m.aboutUrl) << "\n"
        << "MinWindowsBuild="       << m.minWindowsBuild << "\n"
        << "OsLaunchMessage="       << EscapeSingleLine(m.osLaunchMessage) << "\n"
        << "SilentRebootExitCode="  << m.silentRebootCode << "\n"
        << "DefaultSubdir="         << EscapeSingleLine(m.defaultSubdir) << "\n"
        << "StartMenuFolder="       << EscapeSingleLine(m.startMenuFolder) << "\n"
        << "MainExecutable="        << EscapeSingleLine(m.mainExecutable) << "\n"
        << "WizardLayout="          << EscapeSingleLine(m.wizardLayout) << "\n"
        << "AccentPrimaryHex="      << EscapeSingleLine(m.accentPrimaryHex) << "\n"
        << "AccentSecondaryHex="    << EscapeSingleLine(m.accentSecondaryHex) << "\n"
        << "InstallButtonText="     << EscapeSingleLine(m.installBtnText) << "\n"
        << "FinishLaunchCheckbox="  << EscapeSingleLine(m.finishLaunchText) << "\n"
        << "OfferLaunchOnFinish="   << (m.offerLaunchOnFinish ? "true" : "false") << "\n"
        << "UncompressedBytes="     << m.uncompressedBytes << "\n"
        << "PostExtractRun="        << EscapeSingleLine(m.postExtractRun) << "\n"
        << "PostExtractArgs="       << EscapeSingleLine(m.postExtractArgs) << "\n"
        << "PostExtractOpen="       << EscapeSingleLine(m.postExtractOpen) << "\n"
        << "ExtractToTemp="         << (m.extractToTemp ? "true" : "false") << "\n"
        << "AllowChangeFolder="     << (m.allowChangeFolder ? "true" : "false") << "\n"
        << "SilentOverwrite="       << (m.silentOverwrite ? "true" : "false") << "\n"
        << "CleanupTempAfterRun="   << (m.cleanupTempAfterRun ? "true" : "false") << "\n"
        << "WaitMode="              << EscapeSingleLine(m.waitMode) << "\n"
        << "WaitForProcess="        << EscapeSingleLine(m.waitForProcess) << "\n"
        << "WaitSpawnGraceMs="      << m.waitSpawnGraceMs << "\n"
        << "AutoStartExtract="      << (m.autoStartExtract ? "true" : "false") << "\n";

    out << "FolderCount=" << m.folders.size() << "\n";
    for (std::size_t i = 0; i < m.folders.size(); ++i) {
        const std::string p = "Folder_" + std::to_string(i) + "_";
        out << p << "Root="   << EscapeSingleLine(m.folders[i].targetRoot) << "\n"
            << p << "Subdir=" << EscapeSingleLine(m.folders[i].targetSubdir) << "\n"
            << p << "Feat="   << EscapeSingleLine(m.folders[i].featureId) << "\n"
            << p << "Rem="    << (m.folders[i].removeOnUninstall ? "true" : "false") << "\n";
    }

    out << "ShortcutCount=" << m.shortcuts.size() << "\n";
    for (std::size_t i = 0; i < m.shortcuts.size(); ++i) {
        const std::string p = "Shortcut_" + std::to_string(i) + "_";
        out << p << "Id="   << EscapeSingleLine(m.shortcuts[i].id) << "\n"
            << p << "Name=" << EscapeSingleLine(m.shortcuts[i].name) << "\n"
            << p << "Targ=" << EscapeSingleLine(m.shortcuts[i].targetFile) << "\n"
            << p << "Loc="  << EscapeSingleLine(m.shortcuts[i].location) << "\n"
            << p << "Args=" << EscapeSingleLine(m.shortcuts[i].arguments) << "\n"
            << p << "Desc=" << EscapeSingleLine(m.shortcuts[i].description) << "\n"
            << p << "Feat=" << EscapeSingleLine(m.shortcuts[i].featureId) << "\n";
    }

    out << "RegistryCount=" << m.registries.size() << "\n";
    for (std::size_t i = 0; i < m.registries.size(); ++i) {
        const std::string p = "Reg_" + std::to_string(i) + "_";
        out << p << "Root=" << EscapeSingleLine(m.registries[i].root) << "\n"
            << p << "Key="  << EscapeSingleLine(m.registries[i].key) << "\n"
            << p << "Name=" << EscapeSingleLine(m.registries[i].name) << "\n"
            << p << "Val="  << EscapeSingleLine(m.registries[i].value) << "\n"
            << p << "Type=" << EscapeSingleLine(m.registries[i].type) << "\n"
            << p << "Feat=" << EscapeSingleLine(m.registries[i].featureId) << "\n";
    }

    out << "AssocCount=" << m.fileAssocs.size() << "\n";
    for (std::size_t i = 0; i < m.fileAssocs.size(); ++i) {
        const std::string p = "Assoc_" + std::to_string(i) + "_";
        out << p << "Ext="   << EscapeSingleLine(m.fileAssocs[i].extension) << "\n"
            << p << "Prog="  << EscapeSingleLine(m.fileAssocs[i].progId) << "\n"
            << p << "Desc="  << EscapeSingleLine(m.fileAssocs[i].description) << "\n"
            << p << "Targ="  << EscapeSingleLine(m.fileAssocs[i].targetFile) << "\n"
            << p << "Verbs=" << EscapeSingleLine(m.fileAssocs[i].verbs) << "\n"
            << p << "Mime="  << EscapeSingleLine(m.fileAssocs[i].contentType) << "\n"
            << p << "Perc="  << EscapeSingleLine(m.fileAssocs[i].perceivedType) << "\n"
            << p << "Icon="  << m.fileAssocs[i].iconIndex << "\n"
            << p << "Feat="  << EscapeSingleLine(m.fileAssocs[i].featureId) << "\n";
    }
    return out.str();
}

inline NativeOrSfxManifest ParseNativeOrSfxManifestIni(std::string_view text) {
    NativeOrSfxManifest m{};
    auto kv = native_detail::ParseFlatIniMap(text);
    auto getS = [&](const std::string& k, const std::string& defV = "") {
        auto it = kv.find(k);
        return (it != kv.end()) ? it->second : defV;
    };

    m.mode                  = AsciiLower(getS("Mode", "native"));
    m.productName           = getS("ProductName", m.productName);
    m.tagline               = getS("Tagline");
    m.websiteLabel          = getS("WebsiteLabel");
    m.version               = getS("Version", m.version);
    m.manufacturer          = getS("Manufacturer", m.manufacturer);
    m.upgradeCode           = getS("UpgradeCode");
    m.productCode           = getS("ProductCode");
    m.architecture          = getS("Architecture", m.architecture);
    m.perMachine            = EqualsIgnoreCase(getS("PerMachine", "false"), "true");
    m.requestedExecLevel    = std::string(NormalizeRequestedExecutionLevel(getS("RequestedExecutionLevel", "asInvoker")));
    m.hideFromInstalledApps = EqualsIgnoreCase(getS("HideFromInstalledApps", "false"), "true");
    m.aboutUrl              = getS("AboutUrl");
    if (!kv["MinWindowsBuild"].empty())      m.minWindowsBuild   = std::atoi(kv["MinWindowsBuild"].c_str());
    m.osLaunchMessage       = getS("OsLaunchMessage", m.osLaunchMessage);
    if (!kv["SilentRebootExitCode"].empty()) m.silentRebootCode  = std::atoi(kv["SilentRebootExitCode"].c_str());
    m.defaultSubdir         = getS("DefaultSubdir");
    m.startMenuFolder       = getS("StartMenuFolder", m.manufacturer);
    m.mainExecutable        = getS("MainExecutable");
    m.wizardLayout          = getS("WizardLayout", (m.mode == "sfx") ? "simple" : "standard");
    m.accentPrimaryHex      = getS("AccentPrimaryHex", m.accentPrimaryHex);
    m.accentSecondaryHex    = getS("AccentSecondaryHex", m.accentSecondaryHex);
    m.installBtnText        = getS("InstallButtonText", (m.mode == "sfx") ? "&Extract" : "&Install");
    m.finishLaunchText      = getS("FinishLaunchCheckbox", m.finishLaunchText);
    m.offerLaunchOnFinish   = !EqualsIgnoreCase(getS("OfferLaunchOnFinish", "true"), "false");
    if (!kv["UncompressedBytes"].empty()) {
        m.uncompressedBytes = static_cast<std::uint64_t>(std::strtoull(kv["UncompressedBytes"].c_str(), nullptr, 10));
    }

    m.postExtractRun    = getS("PostExtractRun");
    m.postExtractArgs   = getS("PostExtractArgs");
    m.postExtractOpen   = getS("PostExtractOpen");
    m.extractToTemp       = EqualsIgnoreCase(getS("ExtractToTemp", "false"), "true");
    m.allowChangeFolder   = !EqualsIgnoreCase(getS("AllowChangeFolder", "true"), "false");
    m.silentOverwrite     = !EqualsIgnoreCase(getS("SilentOverwrite", "true"), "false");
    m.cleanupTempAfterRun = EqualsIgnoreCase(getS("CleanupTempAfterRun", "false"), "true");
    m.waitMode            = AsciiLower(TrimAscii(getS("WaitMode", "auto")));
    m.waitForProcess      = TrimAscii(getS("WaitForProcess"));
    if (!kv["WaitSpawnGraceMs"].empty()) {
        m.waitSpawnGraceMs = std::clamp(std::atoi(kv["WaitSpawnGraceMs"].c_str()), 0, 60000);
    }
    m.autoStartExtract    = EqualsIgnoreCase(getS("AutoStartExtract", "false"), "true");

    const int nFold = std::clamp(std::atoi(kv["FolderCount"].c_str()), 0, 512);
    for (int i = 0; i < nFold; ++i) {
        const std::string p = "Folder_" + std::to_string(i) + "_";
        m.folders.push_back({getS(p + "Root", "INSTALLDIR"), getS(p + "Subdir"), getS(p + "Feat", "Main"),
                             !EqualsIgnoreCase(getS(p + "Rem", "true"), "false")});
    }

    const int nSc = std::clamp(std::atoi(kv["ShortcutCount"].c_str()), 0, 256);
    for (int i = 0; i < nSc; ++i) {
        const std::string p = "Shortcut_" + std::to_string(i) + "_";
        m.shortcuts.push_back({getS(p + "Id"), getS(p + "Name"), getS(p + "Targ"),
                               getS(p + "Loc", "StartMenu"), getS(p + "Args"), getS(p + "Desc"), getS(p + "Feat", "Main")});
    }

    const int nReg = std::clamp(std::atoi(kv["RegistryCount"].c_str()), 0, 2048);
    for (int i = 0; i < nReg; ++i) {
        const std::string p = "Reg_" + std::to_string(i) + "_";
        m.registries.push_back({getS(p + "Root", "HKLM"), getS(p + "Key"), getS(p + "Name"),
                                getS(p + "Val"), getS(p + "Type", "string"), getS(p + "Feat", "Main")});
    }

    const int nFa = std::clamp(std::atoi(kv["AssocCount"].c_str()), 0, 128);
    for (int i = 0; i < nFa; ++i) {
        const std::string p = "Assoc_" + std::to_string(i) + "_";
        m.fileAssocs.push_back({getS(p + "Ext"), getS(p + "Prog"), getS(p + "Desc"), getS(p + "Targ"),
                                getS(p + "Verbs", "open:&Open:\"%1\""), getS(p + "Mime"), getS(p + "Perc"),
                                std::atoi(getS(p + "Icon", "0").c_str()), getS(p + "Feat", "Main")});
    }
    return m;
}

// ================================================================================
// 2. ATOMIC TRANSACTIONAL NATIVE INSTALLER, ROLLBACK JOURNAL, & UNINSTALLER
// ================================================================================
struct NativeExecutionOptions {
    std::filesystem::path targetDir;
    std::string           startMenuFolderOverride;
    bool                  createDesktopShortcuts = true;
    bool                  registerFileAssocs     = true;
    int                   suiteManagedFlag       = -1; // `-1` = unchanged, `1` = `RS_SUITE_MANAGED=1`, `0` = `RS_SUITE_MANAGED=0`
    std::filesystem::path runningSetupExePath;         // Copied byte-for-byte to `<InstallDir>\Uninstall.exe`!
    std::filesystem::path externalPackSearchDir;       // Side-by-side directory searched before WinHTTP for `.rswpak`
    int                   simulateFaultAfterFiles = 0; // Used by `insttests.exe` to prove atomic LIFO rollback!
};

/// Executes either a pure `PackageKind = sfx` extraction OR a full transactional `Engine = native`
/// installation with atomic LIFO rollback journaling!
inline bool ExecuteNativeOrSfxTransaction(
    const NativeOrSfxManifest& manifest,
    const std::uint8_t* pakBytes,
    std::size_t pakSize,
    const std::vector<FeaturePackRuntimeEntry>& featurePacks,
    const std::set<std::string>& excludedFeaturesLower,
    const NativeExecutionOptions& opts,
    const std::atomic<bool>* cancelFlag,
    const std::function<void(int permille, const std::wstring& statusText)>& onProgress,
    std::string& outError)
{
    std::vector<RswPakFileMetadata> metas;
    std::vector<std::uint8_t> payload;
    if (!UnpackRswPakBytesInMemory(pakBytes, pakSize, metas, payload, outError)) {
        return false;
    }

    std::error_code ec;
    const bool isSfx = (manifest.mode == "sfx");
    const std::string effStartMenu = opts.startMenuFolderOverride.empty()
        ? manifest.startMenuFolder
        : opts.startMenuFolderOverride;

    // Rollback journal state (reversed in LIFO order on any error or user cancellation!)
    std::vector<std::filesystem::path> createdDirs;
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> backedUpFiles; // `(originalPath, backupPath)`
    std::vector<std::filesystem::path> newlyWrittenFiles;
    std::vector<std::filesystem::path> createdShortcuts;
    struct RegUndoItem {
        HKEY         hRoot = HKEY_CURRENT_USER;
        std::wstring subKey;
        std::wstring valueName;
        bool         createdKey = false;
    };
    std::vector<RegUndoItem> regUndoStack;

    auto ensureTrackedDirectories = [&](const std::filesystem::path& dirPath) -> bool {
        std::vector<std::filesystem::path> missingChain;
        std::filesystem::path cur = dirPath.lexically_normal();
        while (!cur.empty() && !std::filesystem::exists(cur, ec)) {
            missingChain.push_back(cur);
            const auto par = cur.parent_path();
            if (par == cur) break;
            cur = par;
        }
        std::reverse(missingChain.begin(), missingChain.end());
        for (const auto& d : missingChain) {
            if (!std::filesystem::create_directory(d, ec) && !std::filesystem::exists(d, ec)) {
                return false;
            }
            createdDirs.push_back(d);
        }
        return true;
    };

    const std::filesystem::path virtRegRoot = native_detail::GetVirtualRegistryStoreRoot();
    auto performLifoRollback = [&]() {
        if (onProgress) onProgress(0, L"Rolling back changes to restore previous system state...");
        for (auto it = regUndoStack.rbegin(); it != regUndoStack.rend(); ++it) {
            if (!virtRegRoot.empty()) {
                const std::string rc = (it->hRoot == HKEY_LOCAL_MACHINE) ? "HKLM" : "HKCU";
                if (it->createdKey) native_detail::VirtualRegDeleteTree(virtRegRoot, rc, it->subKey);
                else native_detail::VirtualRegDeleteValue(virtRegRoot, rc, it->subKey, it->valueName);
                continue;
            }
            if (it->createdKey) {
                ::RegDeleteTreeW(it->hRoot, it->subKey.c_str());
            } else {
                HKEY hk = nullptr;
                if (::RegOpenKeyExW(it->hRoot, it->subKey.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
                    ::RegDeleteValueW(hk, it->valueName.empty() ? nullptr : it->valueName.c_str());
                    ::RegCloseKey(hk);
                }
            }
        }
        for (auto it = createdShortcuts.rbegin(); it != createdShortcuts.rend(); ++it) {
            std::filesystem::remove(*it, ec);
        }
        for (auto it = newlyWrittenFiles.rbegin(); it != newlyWrittenFiles.rend(); ++it) {
            std::filesystem::remove(*it, ec);
        }
        for (auto it = backedUpFiles.rbegin(); it != backedUpFiles.rend(); ++it) {
            std::filesystem::remove(it->first, ec);
            std::filesystem::rename(it->second, it->first, ec);
        }
        for (auto it = createdDirs.rbegin(); it != createdDirs.rend(); ++it) {
            std::filesystem::remove(*it, ec);
        }
    };

    if (!ensureTrackedDirectories(opts.targetDir)) {
        outError = "Could not create destination folder: " + WideToUtf8(opts.targetDir.wstring());
        return false;
    }

    // Write live rollback journal header for crash diagnostics if `Engine = native`
    const std::filesystem::path journalFile = opts.targetDir / L".rsw-rollback.jnl";
    if (!isSfx) {
        std::ofstream jnl(journalFile, std::ios::binary | std::ios::trunc);
        if (jnl) {
            jnl << "RSW_NATIVE_ROLLBACK_JOURNAL_V1\nTargetDir=" << WideToUtf8(opts.targetDir.wstring()) << "\n";
        }
    }

    auto resolveArchiveDestPath = [&](const std::string& archiveRelPath) -> std::filesystem::path {
        if (isSfx) {
            return (opts.targetDir / Utf8ToWide(archiveRelPath)).lexically_normal();
        }
        const auto slash = archiveRelPath.find('/');
        if (slash == std::string::npos) {
            return (opts.targetDir / Utf8ToWide(archiveRelPath)).lexically_normal();
        }
        const std::string rootTok = archiveRelPath.substr(0, slash);
        const std::string remRel  = archiveRelPath.substr(slash + 1);
        const auto rootDir = native_detail::ResolveSystemRootFolder(
            rootTok, opts.targetDir, effStartMenu, manifest.perMachine);
        return (rootDir / Utf8ToWide(remRel)).lexically_normal();
    };

    auto writeTrackedFile = [&](const std::filesystem::path& dstPath,
                                const std::uint8_t* dataPtr,
                                std::size_t dataLen,
                                std::uint64_t utcTicks) -> bool {
        if (!ensureTrackedDirectories(dstPath.parent_path())) {
            outError = "Could not create parent directory for '" + WideToUtf8(dstPath.wstring()) + "'.";
            return false;
        }
        if (!isSfx && std::filesystem::exists(dstPath, ec)) {
            const auto bakPath = std::filesystem::path(dstPath.wstring() + L".rswbak." + std::to_wstring(::GetCurrentProcessId()));
            std::filesystem::remove(bakPath, ec);
            ec.clear();
            std::filesystem::rename(dstPath, bakPath, ec);
            if (!ec) {
                backedUpFiles.push_back({dstPath, bakPath});
            }
        }
        std::ofstream out(dstPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            outError = "Could not write file '" + WideToUtf8(dstPath.wstring()) + "' (file may be in use).";
            return false;
        }
        if (dataLen > 0) {
            out.write(reinterpret_cast<const char*>(dataPtr), static_cast<std::streamsize>(dataLen));
        }
        out.close();
        if (!out.good()) {
            outError = "Disk write failed on '" + WideToUtf8(dstPath.wstring()) + "'.";
            return false;
        }
        newlyWrittenFiles.push_back(dstPath);

        if (utcTicks != 0) {
            FILETIME ft{};
            ft.dwLowDateTime  = static_cast<DWORD>(utcTicks & 0xFFFFFFFFull);
            ft.dwHighDateTime = static_cast<DWORD>((utcTicks >> 32) & 0xFFFFFFFFull);
            HANDLE hSet = ::CreateFileW(dstPath.wstring().c_str(), FILE_WRITE_ATTRIBUTES,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hSet != INVALID_HANDLE_VALUE) {
                ::SetFileTime(hSet, nullptr, nullptr, &ft);
                ::CloseHandle(hSet);
            }
        }
        return true;
    };

    // 1. Extract all selected files from embedded `RSWPAK1` stream!
    std::size_t filesWrittenCount = 0;
    const std::size_t totalItems  = std::max<std::size_t>(1, metas.size());
    for (std::size_t i = 0; i < metas.size(); ++i) {
        if (cancelFlag && cancelFlag->load()) {
            outError = "Installation canceled by user.";
            performLifoRollback();
            std::filesystem::remove(journalFile, ec);
            return false;
        }
        const auto& m = metas[i];
        if (!m.featureId.empty() && excludedFeaturesLower.count(AsciiLower(m.featureId)) > 0) {
            continue;
        }
        const auto dstFile = resolveArchiveDestPath(m.archiveRelPath);
        if (onProgress) {
            const int perm = static_cast<int>(((i + 1) * 800u) / totalItems);
            onProgress(perm, (isSfx ? L"Extracting: " : L"Installing: ") + dstFile.filename().wstring());
        }
        if (!writeTrackedFile(dstFile, payload.data() + m.payloadOffset, static_cast<std::size_t>(m.payloadSize), m.lastWriteTicksUtc)) {
            performLifoRollback();
            std::filesystem::remove(journalFile, ec);
            return false;
        }
        ++filesWrittenCount;
        if (opts.simulateFaultAfterFiles > 0 && static_cast<int>(filesWrittenCount) >= opts.simulateFaultAfterFiles) {
            outError = "Simulated disk write fault after " + std::to_string(filesWrittenCount) + " file(s).";
            performLifoRollback();
            std::filesystem::remove(journalFile, ec);
            return false;
        }
    }

    // 2. Acquire & extract any selected external `.rswpak` On-Demand Feature Packs!
    for (const auto& fp : featurePacks) {
        if (excludedFeaturesLower.count(AsciiLower(fp.featureId)) > 0) continue;
        std::filesystem::path localPak;
        bool tempDownloaded = false;
        if (!opts.externalPackSearchDir.empty()) {
            const auto candidate = opts.externalPackSearchDir / Utf8ToWide(fp.fileName);
            if (std::filesystem::exists(candidate, ec)) {
                localPak = candidate;
            }
        }
        if (localPak.empty() && !fp.downloadUrl.empty()) {
            localPak = std::filesystem::temp_directory_path(ec) /
                       (L"rswpak-" + std::to_wstring(::GetCurrentProcessId()) + L"-" + Utf8ToWide(fp.fileName));
            if (onProgress) onProgress(820, L"Downloading feature pack: " + Utf8ToWide(fp.fileName));
            if (!DownloadFileViaWinHttp(fp.downloadUrl, localPak, cancelFlag, nullptr, outError)) {
                performLifoRollback();
                std::filesystem::remove(journalFile, ec);
                return false;
            }
            tempDownloaded = true;
        }
        if (localPak.empty()) {
            outError = "Required on-demand feature pack '" + fp.fileName + "' was not found locally and has no PackUrl.";
            performLifoRollback();
            std::filesystem::remove(journalFile, ec);
            return false;
        }
        if (!VerifyFeaturePackIntegrity(localPak, fp.sha256Hex, false, outError)) {
            if (tempDownloaded) std::filesystem::remove(localPak, ec);
            performLifoRollback();
            std::filesystem::remove(journalFile, ec);
            return false;
        }

        std::vector<std::uint8_t> extRaw;
        if (!ReadBinaryFileBytes(localPak, extRaw, outError)) {
            if (tempDownloaded) std::filesystem::remove(localPak, ec);
            performLifoRollback();
            std::filesystem::remove(journalFile, ec);
            return false;
        }
        if (tempDownloaded) std::filesystem::remove(localPak, ec);

        std::vector<RswPakFileMetadata> extMetas;
        std::vector<std::uint8_t> extPayload;
        if (!UnpackRswPakBytesInMemory(extRaw.data(), extRaw.size(), extMetas, extPayload, outError)) {
            performLifoRollback();
            std::filesystem::remove(journalFile, ec);
            return false;
        }
        for (const auto& em : extMetas) {
            const auto dstFile = resolveArchiveDestPath(em.archiveRelPath);
            if (!writeTrackedFile(dstFile, extPayload.data() + em.payloadOffset, static_cast<std::size_t>(em.payloadSize), em.lastWriteTicksUtc)) {
                performLifoRollback();
                std::filesystem::remove(journalFile, ec);
                return false;
            }
        }
    }

    // Pure `PackageKind = sfx` finishes cleanly right here with ZERO registry/Installed Apps footprint!
    if (isSfx) {
        if (onProgress) onProgress(1000, L"Extraction complete.");
        return true;
    }

    // 3. Create explicit `[Folder.*]` directories
    for (const auto& fl : manifest.folders) {
        if (!fl.featureId.empty() && excludedFeaturesLower.count(AsciiLower(fl.featureId)) > 0) continue;
        const auto base = native_detail::ResolveSystemRootFolder(fl.targetRoot, opts.targetDir, effStartMenu, manifest.perMachine);
        const auto full = fl.targetSubdir.empty() ? base : (base / Utf8ToWide(fl.targetSubdir));
        ensureTrackedDirectories(full);
    }

    // 4. Create `.lnk` Start Menu & Desktop Shortcuts via `IShellLinkW`
    if (onProgress) onProgress(880, L"Creating Start Menu & Desktop shortcuts...");
    for (const auto& sc : manifest.shortcuts) {
        if (!sc.featureId.empty() && excludedFeaturesLower.count(AsciiLower(sc.featureId)) > 0) continue;
        const bool isDesktop = EqualsIgnoreCase(sc.location, "Desktop");
        if (isDesktop && !opts.createDesktopShortcuts) continue;

        const auto scFolder = native_detail::ResolveSystemRootFolder(
            isDesktop ? "DesktopFolder" : "ApplicationProgramsFolder",
            opts.targetDir, effStartMenu, manifest.perMachine);
        ensureTrackedDirectories(scFolder);

        const auto lnkPath    = scFolder / Utf8ToWide(sc.name + ".lnk");
        const auto targetPath = (opts.targetDir / Utf8ToWide(sc.targetFile)).lexically_normal();
        if (!native_detail::CreateWindowsShellShortcut(lnkPath, targetPath, sc.arguments, sc.description, targetPath, 0, outError)) {
            performLifoRollback();
            std::filesystem::remove(journalFile, ec);
            return false;
        }
        createdShortcuts.push_back(lnkPath);
    }

    // 5. Write `[Registry.*]` & `[FileAssociation.*]` keys
    if (onProgress) onProgress(930, L"Registering application settings...");
    std::vector<std::pair<std::string, std::wstring>> recordedRegKeysForUninstall;

    auto setRegValueTracked = [&](HKEY hRoot, const std::string& rootCode, const std::wstring& subKey,
                                  const std::wstring& valName, const std::string& typeStr, const std::string& rawVal) -> bool {
        const std::string expanded = native_detail::ExpandInstallTokens(rawVal, opts.targetDir, manifest.version);
        if (!virtRegRoot.empty()) {
            bool createdNew = false;
            if (!native_detail::VirtualRegWriteValue(virtRegRoot, rootCode, subKey, valName, expanded, createdNew)) {
                outError = "Virtual registry write failed under '" + rootCode + "\\" + WideToUtf8(subKey) + "'.";
                return false;
            }
            regUndoStack.push_back({hRoot, subKey, valName, createdNew});
            recordedRegKeysForUninstall.push_back({rootCode, subKey});
            return true;
        }
        HKEY hk = nullptr;
        DWORD disp = 0;
        if (::RegCreateKeyExW(hRoot, subKey.c_str(), 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &hk, &disp) != ERROR_SUCCESS) {
            outError = "Registry write denied under '" + rootCode + "\\" + WideToUtf8(subKey) + "'.";
            return false;
        }
        regUndoStack.push_back({hRoot, subKey, valName, disp == REG_CREATED_NEW_KEY});
        recordedRegKeysForUninstall.push_back({rootCode, subKey});

        LSTATUS st = ERROR_SUCCESS;
        if (EqualsIgnoreCase(typeStr, "dword")) {
            const DWORD dw = static_cast<DWORD>(std::strtoul(expanded.c_str(), nullptr, 10));
            st = ::RegSetValueExW(hk, valName.empty() ? nullptr : valName.c_str(), 0, REG_DWORD,
                                  reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
        } else {
            const std::wstring wVal = Utf8ToWide(expanded);
            const DWORD regType = EqualsIgnoreCase(typeStr, "expand_string") ? REG_EXPAND_SZ : REG_SZ;
            st = ::RegSetValueExW(hk, valName.empty() ? nullptr : valName.c_str(), 0, regType,
                                  reinterpret_cast<const BYTE*>(wVal.c_str()),
                                  static_cast<DWORD>((wVal.size() + 1) * sizeof(wchar_t)));
        }
        ::RegCloseKey(hk);
        return st == ERROR_SUCCESS;
    };

    for (const auto& r : manifest.registries) {
        if (!r.featureId.empty() && excludedFeaturesLower.count(AsciiLower(r.featureId)) > 0) continue;
        HKEY hRoot = native_detail::ResolveRegistryRootHandle(r.root, manifest.perMachine);
        const std::string effRootCode = EqualsIgnoreCase(r.root, "HKMU") ? (manifest.perMachine ? "HKLM" : "HKCU") : r.root;
        const std::wstring wName = (r.name == "@") ? L"" : Utf8ToWide(r.name);
        if (!setRegValueTracked(hRoot, effRootCode, Utf8ToWide(r.key), wName, r.type, r.value)) {
            performLifoRollback();
            std::filesystem::remove(journalFile, ec);
            return false;
        }
    }

    if (opts.registerFileAssocs) {
        const HKEY hClassesHive = manifest.perMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
        const std::string rootCode = manifest.perMachine ? "HKLM" : "HKCU";
        for (const auto& fa : manifest.fileAssocs) {
            if (!fa.featureId.empty() && excludedFeaturesLower.count(AsciiLower(fa.featureId)) > 0) continue;
            std::string ext = TrimAscii(fa.extension);
            if (!ext.empty() && ext.front() != '.') ext.insert(ext.begin(), '.');
            const std::string targetFull = WideToUtf8((opts.targetDir / Utf8ToWide(fa.targetFile)).lexically_normal().wstring());

            setRegValueTracked(hClassesHive, rootCode, Utf8ToWide("Software\\Classes\\" + ext), L"", "string", fa.progId);
            setRegValueTracked(hClassesHive, rootCode, Utf8ToWide("Software\\Classes\\" + fa.progId), L"", "string", fa.description);
            setRegValueTracked(hClassesHive, rootCode, Utf8ToWide("Software\\Classes\\" + fa.progId + "\\DefaultIcon"), L"", "string",
                               targetFull + "," + std::to_string(fa.iconIndex));
            setRegValueTracked(hClassesHive, rootCode, Utf8ToWide("Software\\Classes\\" + fa.progId + "\\shell\\open\\command"), L"", "string",
                               "\"" + targetFull + "\" \"%1\"");
        }
    }

    // 6. Emit signed `Uninstall.exe` + `Uninstall.rswm` & register in Windows *Installed apps*!
    if (onProgress) onProgress(970, L"Registering uninstaller...");
    std::filesystem::path srcStub = opts.runningSetupExePath;
    if (srcStub.empty()) {
        wchar_t selfBuf[MAX_PATH * 4]{};
        ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
        srcStub = selfBuf;
    }
    const std::filesystem::path uninstallExePath = opts.targetDir / L"Uninstall.exe";
    if (std::filesystem::exists(srcStub, ec) && srcStub != uninstallExePath) {
        std::filesystem::copy_file(srcStub, uninstallExePath, std::filesystem::copy_options::overwrite_existing, ec);
        newlyWrittenFiles.push_back(uninstallExePath);
    }

    const std::string uninstKeyId = manifest.upgradeCode.empty()
        ? ("RaggieSoft." + manifest.productName)
        : manifest.upgradeCode;
    const std::wstring uninstSubKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + Utf8ToWide(uninstKeyId);
    const HKEY hUninstHive = manifest.perMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER;
    const std::string uninstRootCode = manifest.perMachine ? "HKLM" : "HKCU";

    // Persist `Uninstall.rswm` manifest so `Uninstall.exe --native-uninstall` removes 100% of installed items cleanly!
    const std::filesystem::path uninstMetaPath = opts.targetDir / L"Uninstall.rswm";
    {
        std::ofstream um(uninstMetaPath, std::ios::binary | std::ios::trunc);
        um << "[NativeUninstall]\n"
           << "ProductName=" << native_detail::EscapeSingleLine(manifest.productName) << "\n"
           << "Version=" << native_detail::EscapeSingleLine(manifest.version) << "\n"
           << "Manufacturer=" << native_detail::EscapeSingleLine(manifest.manufacturer) << "\n"
           << "PerMachine=" << (manifest.perMachine ? "true" : "false") << "\n"
           << "UninstallRegKey=" << WideToUtf8(uninstSubKey) << "\n"
           << "InstallDir=" << WideToUtf8(opts.targetDir.wstring()) << "\n";
        um << "FileCount=" << newlyWrittenFiles.size() << "\n";
        for (std::size_t i = 0; i < newlyWrittenFiles.size(); ++i) {
            um << "File_" << i << "=" << WideToUtf8(newlyWrittenFiles[i].wstring()) << "\n";
        }
        um << "ShortcutCount=" << createdShortcuts.size() << "\n";
        for (std::size_t i = 0; i < createdShortcuts.size(); ++i) {
            um << "Shortcut_" << i << "=" << WideToUtf8(createdShortcuts[i].wstring()) << "\n";
        }
        um << "DirCount=" << createdDirs.size() << "\n";
        for (std::size_t i = 0; i < createdDirs.size(); ++i) {
            um << "Dir_" << i << "=" << WideToUtf8(createdDirs[i].wstring()) << "\n";
        }
        um << "RegKeyCount=" << recordedRegKeysForUninstall.size() << "\n";
        for (std::size_t i = 0; i < recordedRegKeysForUninstall.size(); ++i) {
            um << "RegRoot_" << i << "=" << recordedRegKeysForUninstall[i].first << "\n"
               << "RegKey_"  << i << "=" << WideToUtf8(recordedRegKeysForUninstall[i].second) << "\n";
        }
    }

    const std::string uninstCmd      = "\"" + WideToUtf8(uninstallExePath.wstring()) + "\" --native-uninstall";
    const std::string quietUninstCmd = "\"" + WideToUtf8(uninstallExePath.wstring()) + "\" --native-uninstall /s";
    const std::string iconExe        = !manifest.mainExecutable.empty()
        ? WideToUtf8((opts.targetDir / Utf8ToWide(manifest.mainExecutable)).wstring())
        : WideToUtf8(uninstallExePath.wstring());
    const std::uint64_t estKb = std::max<std::uint64_t>(64u, manifest.uncompressedBytes / 1024u);

    setRegValueTracked(hUninstHive, uninstRootCode, uninstSubKey, L"DisplayName",          "string", manifest.productName);
    setRegValueTracked(hUninstHive, uninstRootCode, uninstSubKey, L"DisplayVersion",       "string", manifest.version);
    setRegValueTracked(hUninstHive, uninstRootCode, uninstSubKey, L"Publisher",            "string", manifest.manufacturer);
    setRegValueTracked(hUninstHive, uninstRootCode, uninstSubKey, L"InstallLocation",      "string", WideToUtf8(opts.targetDir.wstring()));
    setRegValueTracked(hUninstHive, uninstRootCode, uninstSubKey, L"DisplayIcon",          "string", iconExe);
    setRegValueTracked(hUninstHive, uninstRootCode, uninstSubKey, L"UninstallString",      "string", uninstCmd);
    setRegValueTracked(hUninstHive, uninstRootCode, uninstSubKey, L"QuietUninstallString", "string", quietUninstCmd);
    setRegValueTracked(hUninstHive, uninstRootCode, uninstSubKey, L"EstimatedSize",        "dword",  std::to_string(estKb));
    setRegValueTracked(hUninstHive, uninstRootCode, uninstSubKey, L"NoModify",             "dword",  "1");
    setRegValueTracked(hUninstHive, uninstRootCode, uninstSubKey, L"NoRepair",             "dword",  "1");
    if (!manifest.aboutUrl.empty()) {
        setRegValueTracked(hUninstHive, uninstRootCode, uninstSubKey, L"URLInfoAbout", "string", manifest.aboutUrl);
    }

    // Dynamic `ARPSYSTEMCOMPONENT` (`SystemComponent`) Governance!
    const bool hideEntry = (opts.suiteManagedFlag == 1) ||
                           (opts.suiteManagedFlag < 0 && manifest.hideFromInstalledApps);
    if (hideEntry) {
        setRegValueTracked(hUninstHive, uninstRootCode, uninstSubKey, L"SystemComponent", "dword", "1");
    } else if (opts.suiteManagedFlag == 0) {
        if (!virtRegRoot.empty()) {
            native_detail::VirtualRegDeleteValue(virtRegRoot, uninstRootCode, uninstSubKey, L"SystemComponent");
        } else {
            HKEY hk = nullptr;
            if (::RegOpenKeyExW(hUninstHive, uninstSubKey.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
                ::RegDeleteValueW(hk, L"SystemComponent");
                ::RegCloseKey(hk);
            }
        }
    }

    // 7. Commit transaction: clean up `.rswbak` backups and `.rsw-rollback.jnl`!
    for (const auto& [origP, bakP] : backedUpFiles) {
        (void)origP;
        std::filesystem::remove(bakP, ec);
    }
    std::filesystem::remove(journalFile, ec);
    if (onProgress) onProgress(1000, L"Installation complete.");
    return true;
}

/// Executes clean `Engine = native` uninstallation from `<InstallDir>\Uninstall.rswm`!
inline bool ExecuteNativeUninstallFromMeta(
    const std::filesystem::path& metaRswmPath,
    bool scheduleSelfDelete,
    std::string& outError)
{
    std::vector<std::uint8_t> raw;
    if (!ReadBinaryFileBytes(metaRswmPath, raw, outError)) {
        return false;
    }
    auto kv = native_detail::ParseFlatIniMap(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
    const bool perMachine = EqualsIgnoreCase(kv["PerMachine"], "true");

    std::error_code ec;
    const int nSc = std::clamp(std::atoi(kv["ShortcutCount"].c_str()), 0, 512);
    for (int i = 0; i < nSc; ++i) {
        const std::string p = kv["Shortcut_" + std::to_string(i)];
        if (!p.empty()) {
            const std::filesystem::path sp(Utf8ToWide(p));
            std::filesystem::remove(sp, ec);
            std::filesystem::remove(sp.parent_path(), ec); // Removes Start Menu folder if now empty!
        }
    }

    const int nFiles = std::clamp(std::atoi(kv["FileCount"].c_str()), 0, 65536);
    wchar_t selfBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
    const std::filesystem::path selfPath = std::filesystem::path(selfBuf).lexically_normal();

    for (int i = nFiles - 1; i >= 0; --i) {
        const std::string p = kv["File_" + std::to_string(i)];
        if (p.empty()) continue;
        const std::filesystem::path fp = std::filesystem::path(Utf8ToWide(p)).lexically_normal();
        if (_wcsicmp(fp.wstring().c_str(), selfPath.wstring().c_str()) == 0) {
            continue; // Deferred self-cleanup below if running straight from `Uninstall.exe`!
        }
        std::filesystem::remove(fp, ec);
    }

    std::filesystem::remove(metaRswmPath, ec);
    const std::filesystem::path installDir(Utf8ToWide(kv["InstallDir"]));
    const auto uninstExeInDir = installDir / L"Uninstall.exe";
    if (_wcsicmp(uninstExeInDir.wstring().c_str(), selfPath.wstring().c_str()) != 0) {
        std::filesystem::remove(uninstExeInDir, ec);
    }

    const std::filesystem::path virtRegRoot = native_detail::GetVirtualRegistryStoreRoot();
    const int nReg = std::clamp(std::atoi(kv["RegKeyCount"].c_str()), 0, 4096);
    for (int i = nReg - 1; i >= 0; --i) {
        const std::string rc = kv["RegRoot_" + std::to_string(i)];
        const std::string rk = kv["RegKey_"  + std::to_string(i)];
        if (!rk.empty()) {
            if (!virtRegRoot.empty()) {
                native_detail::VirtualRegDeleteTree(virtRegRoot, rc, Utf8ToWide(rk));
            } else {
                HKEY hRoot = native_detail::ResolveRegistryRootHandle(rc, perMachine);
                ::RegDeleteTreeW(hRoot, Utf8ToWide(rk).c_str());
            }
        }
    }

    const std::string uninstKey = kv["UninstallRegKey"];
    if (!uninstKey.empty()) {
        if (!virtRegRoot.empty()) {
            native_detail::VirtualRegDeleteTree(virtRegRoot, perMachine ? "HKLM" : "HKCU", Utf8ToWide(uninstKey));
        } else {
            ::RegDeleteTreeW(perMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER, Utf8ToWide(uninstKey).c_str());
        }
    }

    const int nDirs = std::clamp(std::atoi(kv["DirCount"].c_str()), 0, 4096);
    for (int i = nDirs - 1; i >= 0; --i) {
        const std::string d = kv["Dir_" + std::to_string(i)];
        if (!d.empty()) {
            std::filesystem::remove(std::filesystem::path(Utf8ToWide(d)), ec);
        }
    }
    if (!installDir.empty()) {
        std::filesystem::remove(installDir, ec);
    }

    if (scheduleSelfDelete && _wcsicmp(uninstExeInDir.wstring().c_str(), selfPath.wstring().c_str()) == 0) {
        // Spawn a brief background `cmd.exe` cleanup after `Uninstall.exe` exits so `<InstallDir>` vanishes cleanly!
        std::wstring cmd = L"cmd.exe /c ping -n 2 127.0.0.1 >nul & del /f /q \"" + selfPath.wstring() +
                           L"\" >nul 2>&1 & rmdir \"" + installDir.wstring() + L"\" >nul 2>&1";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, L"C:\\", &si, &pi)) {
            ::CloseHandle(pi.hThread);
            ::CloseHandle(pi.hProcess);
        }
    }
    return true;
}

// ================================================================================
// 3. UNIFIED SUITE CHAINER & DYNAMIC `ARPSYSTEMCOMPONENT` GOVERNANCE (`#230`)
// ================================================================================
struct SuiteChildSpec {
    std::string   id;
    std::string   name;
    std::string   description;
    std::string   fileName;
    std::string   upgradeCode;
    std::string   engine            = "msi"; // `"msi"` | `"native"` | `"exe"`
    std::string   installArgs;
    std::string   sha256Hex;
    std::uint64_t uncompressedBytes = 0;
    int           embeddedResId     = 0;     // `kResIdSuiteChildBase + idx`
    bool          required          = false;
    bool          selectedByDefault = true;
    bool          adoptStandalone   = true;
};

struct SuiteChainerManifest {
    std::string   suiteName          = "Application Suite";
    std::string   tagline;
    std::string   websiteLabel;
    std::string   version            = "1.0.0";
    std::string   manufacturer       = "RaggieSoft";
    std::string   upgradeCode;
    std::string   architecture       = "arm64";
    bool          perMachine         = true;
    std::string   requestedExecLevel = "asInvoker";
    int           minWindowsBuild    = 22000;
    std::string   osLaunchMessage    = "This suite requires Windows 11 (Build 22000) or later.";
    std::string   defaultSubdir      = "RaggieSoft\\Suite";
    std::string   accentPrimaryHex   = "#2563EB";
    std::string   accentSecondaryHex = "#FBBF24";
    std::vector<SuiteChildSpec> children;
};

inline std::string SerializeSuiteChainerManifestIni(const SuiteChainerManifest& s) {
    using native_detail::EscapeSingleLine;
    std::ostringstream out;
    out << "[SuiteChainer]\n"
        << "SuiteName="             << EscapeSingleLine(s.suiteName) << "\n"
        << "Tagline="               << EscapeSingleLine(s.tagline) << "\n"
        << "WebsiteLabel="          << EscapeSingleLine(s.websiteLabel) << "\n"
        << "Version="               << EscapeSingleLine(s.version) << "\n"
        << "Manufacturer="          << EscapeSingleLine(s.manufacturer) << "\n"
        << "UpgradeCode="           << EscapeSingleLine(s.upgradeCode) << "\n"
        << "Architecture="          << EscapeSingleLine(s.architecture) << "\n"
        << "PerMachine="            << (s.perMachine ? "true" : "false") << "\n"
        << "RequestedExecutionLevel=" << EscapeSingleLine(s.requestedExecLevel) << "\n"
        << "MinWindowsBuild="       << s.minWindowsBuild << "\n"
        << "OsLaunchMessage="       << EscapeSingleLine(s.osLaunchMessage) << "\n"
        << "DefaultSubdir="         << EscapeSingleLine(s.defaultSubdir) << "\n"
        << "AccentPrimaryHex="      << EscapeSingleLine(s.accentPrimaryHex) << "\n"
        << "AccentSecondaryHex="    << EscapeSingleLine(s.accentSecondaryHex) << "\n"
        << "ChildCount="            << s.children.size() << "\n";

    for (std::size_t i = 0; i < s.children.size(); ++i) {
        const auto& c = s.children[i];
        const std::string p = "Child_" + std::to_string(i) + "_";
        out << p << "Id="        << EscapeSingleLine(c.id) << "\n"
            << p << "Name="      << EscapeSingleLine(c.name) << "\n"
            << p << "Desc="      << EscapeSingleLine(c.description) << "\n"
            << p << "FileName="  << EscapeSingleLine(c.fileName) << "\n"
            << p << "Upgrade="   << EscapeSingleLine(c.upgradeCode) << "\n"
            << p << "Engine="    << EscapeSingleLine(c.engine) << "\n"
            << p << "Args="      << EscapeSingleLine(c.installArgs) << "\n"
            << p << "Sha256="    << EscapeSingleLine(c.sha256Hex) << "\n"
            << p << "Bytes="     << c.uncompressedBytes << "\n"
            << p << "ResId="     << c.embeddedResId << "\n"
            << p << "Required="  << (c.required ? "true" : "false") << "\n"
            << p << "Default="   << (c.selectedByDefault ? "true" : "false") << "\n"
            << p << "Adopt="     << (c.adoptStandalone ? "true" : "false") << "\n";
    }
    return out.str();
}

inline SuiteChainerManifest ParseSuiteChainerManifestIni(std::string_view text) {
    SuiteChainerManifest s{};
    auto kv = native_detail::ParseFlatIniMap(text);
    auto getS = [&](const std::string& k, const std::string& defV = "") {
        auto it = kv.find(k);
        return (it != kv.end()) ? it->second : defV;
    };

    s.suiteName          = getS("SuiteName", s.suiteName);
    s.tagline            = getS("Tagline");
    s.websiteLabel       = getS("WebsiteLabel");
    s.version            = getS("Version", s.version);
    s.manufacturer       = getS("Manufacturer", s.manufacturer);
    s.upgradeCode        = getS("UpgradeCode");
    s.architecture       = getS("Architecture", s.architecture);
    s.perMachine         = !EqualsIgnoreCase(getS("PerMachine", "true"), "false");
    s.requestedExecLevel = std::string(NormalizeRequestedExecutionLevel(getS("RequestedExecutionLevel", "asInvoker")));
    if (!kv["MinWindowsBuild"].empty()) s.minWindowsBuild = std::atoi(kv["MinWindowsBuild"].c_str());
    s.osLaunchMessage    = getS("OsLaunchMessage", s.osLaunchMessage);
    s.defaultSubdir      = getS("DefaultSubdir", s.defaultSubdir);
    s.accentPrimaryHex   = getS("AccentPrimaryHex", s.accentPrimaryHex);
    s.accentSecondaryHex = getS("AccentSecondaryHex", s.accentSecondaryHex);

    const int count = std::clamp(std::atoi(kv["ChildCount"].c_str()), 0, 128);
    for (int i = 0; i < count; ++i) {
        const std::string p = "Child_" + std::to_string(i) + "_";
        SuiteChildSpec c{};
        c.id                = getS(p + "Id");
        c.name              = getS(p + "Name");
        c.description       = getS(p + "Desc");
        c.fileName          = getS(p + "FileName");
        c.upgradeCode       = getS(p + "Upgrade");
        c.engine            = AsciiLower(getS(p + "Engine", "msi"));
        c.installArgs       = getS(p + "Args");
        c.sha256Hex         = AsciiLower(getS(p + "Sha256"));
        if (!kv[p + "Bytes"].empty()) {
            c.uncompressedBytes = static_cast<std::uint64_t>(std::strtoull(kv[p + "Bytes"].c_str(), nullptr, 10));
        }
        c.embeddedResId     = std::atoi(getS(p + "ResId", "0").c_str());
        c.required          = EqualsIgnoreCase(getS(p + "Required", "false"), "true");
        c.selectedByDefault = !EqualsIgnoreCase(getS(p + "Default", "true"), "false");
        c.adoptStandalone   = !EqualsIgnoreCase(getS(p + "Adopt", "true"), "false");
        s.children.push_back(std::move(c));
    }
    return s;
}

/// Checks whether a Suite child package (`msi` or `native`) is currently installed on this PC,
/// returning its active MSI `ProductCode` (`outMsiProductCode`) when applicable!
inline bool IsSuiteChildCurrentlyInstalled(
    const SuiteChildSpec& child,
    std::wstring& outMsiProductCode,
    std::wstring& outNativeQuietUninstall)
{
    outMsiProductCode.clear();
    outNativeQuietUninstall.clear();
    if (child.upgradeCode.empty()) return false;

    const std::wstring wUpg = Utf8ToWide(child.upgradeCode);
    if (child.engine == "msi") {
        wchar_t prodBuf[39]{};
        if (::MsiEnumRelatedProductsW(wUpg.c_str(), 0, 0, prodBuf) == ERROR_SUCCESS) {
            outMsiProductCode = prodBuf;
            return true;
        }
    }

    // Check `HKLM` and `HKCU` `Uninstall\<UpgradeCode>` for `Engine = native` packages!
    const std::wstring subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + wUpg;
    const auto virtRegRoot = native_detail::GetVirtualRegistryStoreRoot();
    if (!virtRegRoot.empty()) {
        for (const char* rc : {"HKLM", "HKCU"}) {
            std::string quietCmd;
            if (native_detail::VirtualRegReadValue(virtRegRoot, rc, subKey, L"QuietUninstallString", quietCmd) ||
                native_detail::VirtualRegReadValue(virtRegRoot, rc, subKey, L"UninstallString", quietCmd)) {
                outNativeQuietUninstall = Utf8ToWide(quietCmd);
                return true;
            }
        }
        return false;
    }
    for (HKEY hRoot : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        std::string quietCmd;
        if (ReadRegValueStringView(hRoot, subKey, L"QuietUninstallString", KEY_WOW64_64KEY, quietCmd) ||
            ReadRegValueStringView(hRoot, subKey, L"UninstallString", KEY_WOW64_64KEY, quietCmd)) {
            outNativeQuietUninstall = Utf8ToWide(quietCmd);
            return true;
        }
    }
    return false;
}

/// Updates `SystemComponent` (`1` = hidden under Suite umbrella, `0` = standalone visible in *Installed apps*)
/// on an installed MSI `ProductCode` OR Native `UpgradeCode` in both `HKLM` and `HKCU` 64-bit Uninstall keys!
inline void SetInstalledAppsSystemComponentFlag(std::wstring_view uninstallKeyLeaf, bool hideUnderSuite) noexcept {
    if (uninstallKeyLeaf.empty()) return;
    const std::wstring subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + std::wstring(uninstallKeyLeaf);
    const auto virtRegRoot = native_detail::GetVirtualRegistryStoreRoot();
    if (!virtRegRoot.empty()) {
        std::error_code ec;
        for (const char* rc : {"HKLM", "HKCU"}) {
            const auto keyDir = native_detail::ResolveVirtualRegKeyDir(virtRegRoot, rc, subKey);
            if (!std::filesystem::is_directory(keyDir, ec)) continue;
            if (hideUnderSuite) {
                bool dummy = false;
                native_detail::VirtualRegWriteValue(virtRegRoot, rc, subKey, L"SystemComponent", "1", dummy);
            } else {
                native_detail::VirtualRegDeleteValue(virtRegRoot, rc, subKey, L"SystemComponent");
            }
        }
        return;
    }
    for (HKEY hRoot : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        HKEY hk = nullptr;
        if (::RegOpenKeyExW(hRoot, subKey.c_str(), 0, KEY_QUERY_VALUE | KEY_SET_VALUE | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
            if (hideUnderSuite) {
                const DWORD one = 1;
                ::RegSetValueExW(hk, L"SystemComponent", 0, REG_DWORD, reinterpret_cast<const BYTE*>(&one), sizeof(one));
            } else {
                ::RegDeleteValueW(hk, L"SystemComponent");
            }
            ::RegCloseKey(hk);
        }
    }
}

/// Queries whether an installed `ProductCode` or `UpgradeCode` currently has `SystemComponent == 1` set!
inline bool IsInstalledAppHiddenAsSuiteComponent(std::wstring_view uninstallKeyLeaf) noexcept {
    if (uninstallKeyLeaf.empty()) return false;
    const std::wstring subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + std::wstring(uninstallKeyLeaf);
    const auto virtRegRoot = native_detail::GetVirtualRegistryStoreRoot();
    if (!virtRegRoot.empty()) {
        for (const char* rc : {"HKLM", "HKCU"}) {
            std::string val;
            if (native_detail::VirtualRegReadValue(virtRegRoot, rc, subKey, L"SystemComponent", val) && val == "1") {
                return true;
            }
        }
        return false;
    }
    for (HKEY hRoot : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        std::string val;
        if (ReadRegValueStringView(hRoot, subKey, L"SystemComponent", KEY_WOW64_64KEY, val) && val == "1") {
            return true;
        }
    }
    return false;
}

// ================================================================================
// 4. `[SFX]` SOFTWARE INSTALLATION `%TEMP%` WRAPPER, `_ISDEL` WATCHER & AUTO-CLEANUP
// ================================================================================
namespace sfx_wait_detail {

/// Case-insensitive ASCII/UTF-16 wildcard matcher (`*` and `?`) for `WaitForProcess`
/// entries like `_ISDEL.EXE`, `_INS*._MP`, or `setup-*.exe`.
inline bool WildcardMatchWideInsensitive(std::wstring_view pattern, std::wstring_view text) noexcept {
    std::size_t p = 0, t = 0;
    std::size_t starIdx = std::wstring_view::npos, matchIdx = 0;
    auto low = [](wchar_t c) noexcept -> wchar_t {
        return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c;
    };
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == L'?' || low(pattern[p]) == low(text[t]))) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == L'*') {
            starIdx  = p++;
            matchIdx = t;
        } else if (starIdx != std::wstring_view::npos) {
            p = starIdx + 1;
            t = ++matchIdx;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == L'*') ++p;
    return p == pattern.size();
}

/// Parses a comma- or semicolon-separated `WaitForProcess` string (`"_ISDEL.EXE; _INS*._MP"`)
/// into normalized filename patterns.
inline std::vector<std::wstring> ParseWaitProcessPatterns(std::string_view rawList) {
    std::vector<std::wstring> out;
    std::string cur;
    auto flushOne = [&]() {
        std::string trimmed = TrimAscii(cur);
        cur.clear();
        if (trimmed.empty()) return;
        const auto slash = trimmed.find_last_of("\\/");
        if (slash != std::string::npos) trimmed.erase(0, slash + 1);
        if (!trimmed.empty()) out.push_back(Utf8ToWide(trimmed));
    };
    for (char ch : rawList) {
        if (ch == ';' || ch == ',') flushOne();
        else cur.push_back(ch);
    }
    flushOne();
    return out;
}

/// Scans running processes (`TH32CS_SNAPPROCESS`) for:
///   1. Any process whose executable filename matches `waitPatterns` (e.g. `_ISDEL.EXE`, `_INS*._MP`), OR
///   2. When `checkTempDirImages == true`, any process whose full image path still lives inside `tempDir`!
inline bool FindActiveCompanionOrTempProcess(
    const std::filesystem::path& tempDir,
    const std::vector<std::wstring>& waitPatterns,
    bool checkTempDirImages,
    std::wstring& outMatchedProcessName)
{
    outMatchedProcessName.clear();
    if (waitPatterns.empty() && !checkTempDirImages) return false;

    std::error_code ec;
    std::wstring normTempPrefix;
    if (checkTempDirImages && !tempDir.empty()) {
        normTempPrefix = tempDir.lexically_normal().wstring();
        if (!normTempPrefix.empty() && normTempPrefix.back() != L'\\') {
            normTempPrefix.push_back(L'\\');
        }
    }

    const DWORD selfPid = ::GetCurrentProcessId();
    HANDLE hSnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnap == INVALID_HANDLE_VALUE) return false;

    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (::Process32FirstW(hSnap, &pe)) {
        do {
            if (pe.th32ProcessID == 0 || pe.th32ProcessID == selfPid) continue;
            const std::wstring_view exeLeaf(pe.szExeFile);

            for (const auto& pat : waitPatterns) {
                if (WildcardMatchWideInsensitive(pat, exeLeaf)) {
                    outMatchedProcessName = pe.szExeFile;
                    found = true;
                    break;
                }
            }
            if (found) break;

            if (!normTempPrefix.empty()) {
                HANDLE hProc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
                if (hProc) {
                    wchar_t imgBuf[MAX_PATH * 4]{};
                    DWORD cch = static_cast<DWORD>(std::size(imgBuf));
                    if (::QueryFullProcessImageNameW(hProc, 0, imgBuf, &cch) && cch > normTempPrefix.size()) {
                        const std::wstring normImg = std::filesystem::path(imgBuf).lexically_normal().wstring();
                        if (normImg.size() > normTempPrefix.size() &&
                            _wcsnicmp(normImg.c_str(), normTempPrefix.c_str(), normTempPrefix.size()) == 0) {
                            outMatchedProcessName = pe.szExeFile;
                            found = true;
                        }
                    }
                    ::CloseHandle(hProc);
                    if (found) break;
                }
            }
        } while (::Process32NextW(hSnap, &pe));
    }
    ::CloseHandle(hSnap);
    return found;
}

/// Safely deletes an isolated `%TEMP%` staging directory (`CleanupTempAfterRun = true`)
/// ONLY after verifying `tempDir` is strictly a child folder inside the OS `%TEMP%` root!
inline bool CleanupSfxTempStagingDirectory(const std::filesystem::path& tempDir) noexcept {
    std::error_code ec;
    if (tempDir.empty() || !std::filesystem::exists(tempDir, ec)) return true;

    const auto osTempRoot = std::filesystem::temp_directory_path(ec).lexically_normal();
    if (osTempRoot.empty()) return false;

    std::wstring rootPfx = osTempRoot.wstring();
    if (!rootPfx.empty() && rootPfx.back() != L'\\') rootPfx.push_back(L'\\');
    const std::wstring cand = tempDir.lexically_normal().wstring();

    // Belt-and-suspenders safety guard: NEVER auto-delete `%TEMP%` itself or any path outside `%TEMP%\...`!
    if (cand.size() <= rootPfx.size() || _wcsnicmp(cand.c_str(), rootPfx.c_str(), rootPfx.size()) != 0) {
        return false;
    }

    auto stripReadOnlyAttributes = [&]() {
        for (const auto& de : std::filesystem::recursive_directory_iterator(tempDir, ec)) {
            const std::wstring p = de.path().wstring();
            const DWORD attrs = ::GetFileAttributesW(p.c_str());
            if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) != 0) {
                ::SetFileAttributesW(p.c_str(), attrs & ~FILE_ATTRIBUTE_READONLY);
            }
        }
    };

    for (int attempt = 0; attempt < 10; ++attempt) {
        stripReadOnlyAttributes();
        ec.clear();
        std::filesystem::remove_all(tempDir, ec);
        if (!std::filesystem::exists(tempDir, ec)) {
            return true;
        }
        ::Sleep(120);
    }
    return !std::filesystem::exists(tempDir, ec);
}

} // namespace sfx_wait_detail

/// Launches `[SFX] PostExtractOpen` / `PostExtractRun`, optionally tracks the entire child process
/// tree (Win32 Job Object + `%TEMP%` image scanner) AND explicit companion cleanup processes
/// (`WaitForProcess`, such as `_ISDEL.EXE` / `_INS*._MP`), and deletes `%TEMP%` staging afterward!
inline bool ExecuteSfxLaunchWaitAndCleanup(
    const NativeOrSfxManifest& m,
    const std::filesystem::path& targetDir,
    std::atomic<bool>* cancelFlag,
    const std::function<void(const std::wstring& status)>& onStatus,
    DWORD& outChildExitCode,
    std::string& outError)
{
    outChildExitCode = 0;
    std::error_code ec;

    if (!m.postExtractOpen.empty()) {
        const auto docPath = (targetDir / Utf8ToWide(m.postExtractOpen)).lexically_normal();
        if (std::filesystem::exists(docPath, ec)) {
            SHELLEXECUTEINFOW sei{sizeof(sei)};
            sei.fMask        = SEE_MASK_FLAG_NO_UI;
            sei.lpVerb       = L"open";
            const std::wstring wDoc = docPath.wstring();
            const std::wstring wDir = docPath.parent_path().wstring();
            sei.lpFile       = wDoc.c_str();
            sei.lpDirectory  = wDir.c_str();
            sei.nShow        = SW_SHOWNORMAL;
            ::ShellExecuteExW(&sei);
        }
    }

    const std::string exeRel = !m.postExtractRun.empty() ? m.postExtractRun : m.mainExecutable;
    if (exeRel.empty()) {
        if (m.mode == "sfx" && m.extractToTemp && m.cleanupTempAfterRun) {
            sfx_wait_detail::CleanupSfxTempStagingDirectory(targetDir);
        }
        return true;
    }

    const auto exePath = (targetDir / Utf8ToWide(exeRel)).lexically_normal();
    if (!std::filesystem::exists(exePath, ec)) {
        outError = "Post-extract executable not found: " + WideToUtf8(exePath.wstring());
        return false;
    }

    std::string effWait = AsciiLower(TrimAscii(m.waitMode));
    if (effWait.empty() || effWait == "auto") {
        effWait = (m.cleanupTempAfterRun || !m.waitForProcess.empty()) ? "job_tree" : "none";
    }

    // Fire-and-forget mode for normal portable extractors (`CleanupTempAfterRun = false` & `WaitMode = none`)
    if (effWait == "none" && !m.cleanupTempAfterRun) {
        const std::wstring wExe  = exePath.wstring();
        const std::wstring wArgs = Utf8ToWide(m.postExtractArgs);
        const std::wstring wDir  = exePath.parent_path().wstring();
        ::ShellExecuteW(nullptr, L"open", wExe.c_str(), wArgs.empty() ? nullptr : wArgs.c_str(), wDir.c_str(), SW_SHOWNORMAL);
        return true;
    }

    if (onStatus) {
        onStatus(L"Running " + exePath.filename().wstring() + L"...");
    }

    // Create a Win32 Job Object so multi-stage setup bootstrappers that spawn child workers and exit
    // early keep `ActiveProcesses > 0` until the entire child tree completes!
    HANDLE hJob = ::CreateJobObjectW(nullptr, nullptr);
    if (hJob) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jeli{};
        jeli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_BREAKAWAY_OK;
        ::SetInformationJobObject(hJob, JobObjectExtendedLimitInformation, &jeli, sizeof(jeli));
    }

    const std::string extLow = AsciiLower(WideToUtf8(exePath.extension().wstring()));
    const std::wstring workDir = exePath.parent_path().wstring();
    const std::wstring wArgs   = Utf8ToWide(m.postExtractArgs);
    HANDLE hChildProc = nullptr;

    if (extLow == ".msi") {
        std::wstring cmd = L"msiexec.exe /i \"" + exePath.wstring() + L"\"" + (wArgs.empty() ? L"" : (L" " + wArgs));
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &si, &pi)) {
            ::CloseHandle(pi.hThread);
            hChildProc = pi.hProcess;
        }
    } else if (extLow == ".cmd" || extLow == ".bat") {
        std::wstring cmd = L"cmd.exe /c \"\"" + exePath.wstring() + L"\"" + (wArgs.empty() ? L"" : (L" " + wArgs)) + L"\"";
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, workDir.c_str(), &si, &pi)) {
            ::CloseHandle(pi.hThread);
            hChildProc = pi.hProcess;
        }
    } else {
        std::wstring cmd = L"\"" + exePath.wstring() + L"\"" + (wArgs.empty() ? L"" : (L" " + wArgs));
        STARTUPINFOW si{sizeof(si)};
        PROCESS_INFORMATION pi{};
        if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &si, &pi)) {
            ::CloseHandle(pi.hThread);
            hChildProc = pi.hProcess;
        } else {
            SHELLEXECUTEINFOW sei{sizeof(sei)};
            sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
            sei.lpVerb       = L"open";
            const std::wstring wExe = exePath.wstring();
            sei.lpFile       = wExe.c_str();
            sei.lpParameters = wArgs.empty() ? nullptr : wArgs.c_str();
            sei.lpDirectory  = workDir.c_str();
            sei.nShow        = SW_SHOWNORMAL;
            if (::ShellExecuteExW(&sei)) {
                hChildProc = sei.hProcess;
            }
        }
    }

    if (!hChildProc) {
        if (hJob) ::CloseHandle(hJob);
        outError = "Failed launching post-extract installer '" + WideToUtf8(exePath.filename().wstring()) + "'.";
        return false;
    }

    if (hJob) {
        (void)::AssignProcessToJobObject(hJob, hChildProc);
    }

    // Phase 1: Wait for the primary `PostExtractRun` process handle
    while (::WaitForSingleObject(hChildProc, 100) == WAIT_TIMEOUT) {
        if (cancelFlag && cancelFlag->load()) break;
    }
    ::GetExitCodeProcess(hChildProc, &outChildExitCode);
    ::CloseHandle(hChildProc);

    // Phase 2: Wait for Job Object descendants, `%TEMP%`-resident child processes, AND explicit
    //          companion processes (`WaitForProcess`, e.g. `_ISDEL.EXE` / `_INS*._MP`)!
    const auto waitPatterns    = sfx_wait_detail::ParseWaitProcessPatterns(m.waitForProcess);
    const bool watchJobOrTemp  = (effWait == "job_tree");
    const bool watchCompanions = (effWait == "job_tree" || effWait == "process_name" || !waitPatterns.empty());

    if ((watchJobOrTemp || watchCompanions) && !(cancelFlag && cancelFlag->load())) {
        const ULONGLONG graceMs = static_cast<ULONGLONG>(std::clamp(m.waitSpawnGraceMs, 0, 60000));
        ULONGLONG idleSinceTick = 0;

        for (;;) {
            if (cancelFlag && cancelFlag->load()) break;

            DWORD jobActiveCount = 0;
            if (watchJobOrTemp && hJob) {
                JOBOBJECT_BASIC_ACCOUNTING_INFORMATION acct{};
                if (::QueryInformationJobObject(hJob, JobObjectBasicAccountingInformation, &acct, sizeof(acct), nullptr)) {
                    jobActiveCount = acct.ActiveProcesses;
                }
            }

            std::wstring matchedProcName;
            const bool procStillAlive = sfx_wait_detail::FindActiveCompanionOrTempProcess(
                targetDir,
                waitPatterns,
                watchJobOrTemp && m.extractToTemp,
                matchedProcName);

            if (jobActiveCount > 0 || procStillAlive) {
                idleSinceTick = 0;
                if (onStatus) {
                    const std::wstring label = !matchedProcName.empty()
                        ? (L"Waiting for " + matchedProcName + L" to finish...")
                        : L"Waiting for setup child processes to finish...";
                    onStatus(label);
                }
            } else {
                const ULONGLONG now = ::GetTickCount64();
                if (idleSinceTick == 0) {
                    idleSinceTick = now;
                }
                if (now - idleSinceTick >= graceMs) {
                    break;
                }
            }
            ::Sleep(100);
        }
    }

    if (hJob) {
        ::CloseHandle(hJob);
    }

    // Phase 3: Remove isolated `%TEMP%` staging directory once all setup & `_ISDEL` processes exit!
    if (m.mode == "sfx" && m.extractToTemp && m.cleanupTempAfterRun && !(cancelFlag && cancelFlag->load())) {
        if (onStatus) {
            onStatus(L"Cleaning up temporary setup files...");
        }
        sfx_wait_detail::CleanupSfxTempStagingDirectory(targetDir);
    }
    return true;
}

} // namespace rsw
