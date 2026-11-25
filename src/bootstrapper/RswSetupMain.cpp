// ================================================================================
// FILE: src/bootstrapper/RswSetupMain.cpp
// PROJECT: RaggieSoft Installer Studio — SetupWizard.exe ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `SetupWizard.exe`?
//     When `instbuild.exe` compiles a `.rsip` project with `BuildBootstrapper = true`,
//     it copies this zero-dependency static `/MT` Win32 C++20 binary (`x64` or `ARM64`)
//     and stamps the compiled `Product.msi`, `[UI]` branding colors, `.ico` icon,
//     RTF license, optional PNG cover/banner artwork, and author-owned `VERSIONINFO`
//     into Win32 PE resources (`UpdateResourceW`).
//
//   How do we render our signature **RaggieSoft Glass** and **16:9 CD-Autorun**
//   dialogs while remaining 100% WCAG 2.2 AA & Revised Section 508 compliant?
//     1. **Native GDI+ Vector Rendering (`RswGlassRenderer.hpp`)**:
//        - Draws our `28x28` Squircle Brand Emblem, `64x64` Step-Specific Product
//          Medallion (updating its circular vector badge on every wizard step!),
//          `4px` Multi-Stop Accent Bar, `Hero Brand Card`, and `48x48` double-rimmed
//          squircle Option Badges dynamically at any DPI (`100%`–`200%+`)!
//     2. **True Accessible `BS_AUTORADIOBUTTON` Subclassing (`SetWindowSubclass`)**:
//        - Legacy Delphi VCL installers used unlabeled `15px` radio dots next to
//          non-focusable `TGlowLabel` text, failing screen readers and target-size
//          accessibility rules.
//        - Also note a subtle Win32 trap: combining `BS_OWNERDRAW` (`0x0B`) with
//          `BS_AUTORADIOBUTTON` (`0x09`) overwrites the low 4 style bits (`BS_TYPEMASK`),
//          destroying `BM_GETCHECK`, Arrow-key cycling, and UI Automation's `RadioButton` role!
//        - Instead, each option card here is a **genuine `BS_AUTORADIOBUTTON`** spanning
//          the full `584 x 68` card rectangle, custom-painted via `SetWindowSubclass`!
//          Screen readers (`Narrator`, `NVDA`, `JAWS`) announce the native `RadioButton`
//          role, checked state, and full title + description, while sighted & low-vision
//          users get crisp `48x48` vector badges and a `2.5px` high-contrast focus ring!
//     3. **Official Windows Installer External UI Transaction (`msi.dll`)**:
//        - `MsiSetInternalUI(INSTALLUILEVEL_NONE)` + `MsiSetExternalUIRecord(...)`
//          streams live progress ticks (`INSTALLMESSAGE_PROGRESS`) and action text
//          (`INSTALLMESSAGE_ACTIONSTART`) onto our 60fps double-buffered Glass UI
//          while keeping MSI's transactional rollback and UAC broker 100% intact!
// ================================================================================

#include "../common/RswGlassRenderer.hpp"
#include "../common/RswLzmsCompression.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswMsiFeatureTree.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswPrerequisites.hpp"
#include "../common/RswVersion.hpp"

#include <msi.h>
#include <msiquery.h>
#include <richedit.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <atomic>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "msi.lib")
#pragma comment(lib, "shell32.lib")

namespace {

// Thread-safe `PostMessageW` / `SendMessageW` notifications from worker thread -> UI thread
constexpr UINT WM_RSW_PROGRESS_UPDATE = WM_APP + 10;
constexpr UINT WM_RSW_STATUS_TEXT     = WM_APP + 11;
constexpr UINT WM_RSW_INSTALL_DONE    = WM_APP + 12;
constexpr UINT WM_RSW_ASK_RETRY_DL    = WM_APP + 13;
constexpr UINT WM_RSW_REQUEST_UAC     = WM_APP + 14;

// Accessible Win32 Child Control IDs (supporting `Tab`, `Arrow`, & `Alt+Mnemonic` traversal)
enum ControlId : int {
    // Standard Glass `SetupType` 3-card radio group (`48x48` vector badges)
    IDC_MODE_EXPRESS     = 1001,
    IDC_MODE_COMPACT     = 1002,
    IDC_MODE_CUSTOM      = 1003,

    // Standard Glass `Maintenance` 3-card radio group (`48x48` vector badges)
    IDC_MAINT_MODIFY     = 1010,
    IDC_MAINT_REPAIR     = 1011,
    IDC_MAINT_REMOVE     = 1012,

    // Office 2000 `DATA1.MSI`-style `RepairMode` 2-card radio group
    IDC_REPAIR_QUICK     = 1015,
    IDC_REPAIR_FULL      = 1016,

    // `SafeUninstall` opt-in cleanup checkboxes (unchecked by default to protect user data!)
    IDC_UNINST_CACHE_CHK = 1018,
    IDC_UNINST_PREFS_CHK = 1019,

    // `License`, `AutorunReadMe`, & `CustomFeatures` (`SelectionTree`) controls
    IDC_LIC_RICHEDIT     = 1020,
    IDC_LIC_ACCEPT_CHK   = 1021,
    IDC_README_EDIT      = 1022,
    IDC_FEAT_TREE        = 1025,
    IDC_FEAT_RESET_BTN   = 1026,

    // `CustomFolder` controls (`STATIC` with `&` mnemonic immediately precedes `EDIT`!)
    IDC_DIR_LABEL        = 1030,
    IDC_DIR_EDIT         = 1031,
    IDC_DIR_BROWSE_BTN   = 1032,

    // `CustomShortcuts` controls (`startmenu` + `fileassociations`)
    IDC_SM_LABEL         = 1035,
    IDC_SM_EDIT          = 1036,
    IDC_DESK_SC_CHK      = 1037,
    IDC_FILE_ASSOC_CHK   = 1038,

    // `Installing` & `Finished` controls
    IDC_PROG_STATUS      = 1040,
    IDC_PROG_BAR         = 1041,
    IDC_FIN_LAUNCH_CHK   = 1051,
    IDC_FIN_VIEWLOG_BTN  = 1052,

    // `autorun16x9` 2x2 interactive CD-Autorun radio grid
    IDC_AR_CELL_1        = 1060,
    IDC_AR_CELL_2        = 1061,
    IDC_AR_CELL_3        = 1062,
    IDC_AR_CELL_4        = 1063,

    // Universal Glass Footer navigation buttons
    IDC_NAV_BACK         = 1090,
    IDC_NAV_PRIMARY      = 1091,
    IDC_NAV_CANCEL       = 1092
};

/// Every multi-step screen supported across Standard Glass, AeroWizard, & `16:9` CD-Autorun modes!
enum class WizardPage {
    Welcome,          // Glass Welcome (`Hero Brand Card` + Intro) OR `16:9` Autorun 2x2 Menu
    License,          // High-contrast RichEdit license + accept checkbox
    AutorunReadMe,    // `16:9` Autorun Edition Liner Notes with `144x74` mini-OG cover header
    SetupType,        // 3-card `48x48` vector picker: Express / Compact / Custom
    CustomFeatures,   // Authentic Windows Installer `SelectionTree` + live Feature Details & Disk Cost!
    CustomFolder,     // Destination path + `IFileOpenDialog` + live `GetDiskFreeSpaceExW` card
    CustomShortcuts,  // Start Menu folder + Desktop shortcut + File Associations checkboxes
    ReadyToInstall,   // `Hero Brand Card` `"READY TO INSTALL"` + summary card + UAC `&Install`
    Maintenance,      // 3-card `48x48` Maintenance Hub: Modify / Repair / Uninstall
    RepairMode,       // 2-card `48x48` Office-style Repair Picker: Quick Repair vs Full Reinstall
    SafeUninstall,    // `Hero Brand Card` `"SAFE UNINSTALL"` + safe unchecked cache/prefs boxes
    Installing,       // `Hero Brand Card` `"CONFIGURING SYSTEM"` + smooth progress + live `%`
    Finished          // `Hero Brand Card` `"READY TO LAUNCH"` + summary + `&Launch` checkbox
};

/// Concrete MSI transaction executed when the user clicks the final action button.
enum class SelectedAction {
    InstallExpress,
    InstallCompact,
    InstallCustom,
    MaintModify,
    MaintQuickRepair,
    MaintFullReinstall,
    MaintUninstall
};

///Parsed UTF-8 `[Branding]` resource (`#202`) stamped into `<Product>-setup.exe` by `instbuild.exe`.
struct BrandingConfig {
    std::wstring productName        = L"Application";
    std::wstring tagline            = L"Native Windows Desktop Software";
    std::wstring websiteLabel       = L"";
    std::wstring version            = L"1.0.0";
    std::wstring manufacturer       = L"RaggieSoft";
    std::wstring architecture       = L"arm64";
    std::wstring productCode;
    std::wstring upgradeCode;
    std::wstring defaultSubdir      = L"Application";
    std::wstring startMenuFolder    = L"RaggieSoft";
    std::wstring mainExecutable;
    bool         perMachine         = true;
    int          minWindowsBuild    = 22000;
    int          silentRebootCode   = 3010;
    std::uint64_t uncompressedMsiSz = 0;
    std::wstring osLaunchMessage    = L"This application requires Windows 11 (Build 22000) or later.";
    bool         isAutorun16x9      = false;
    bool         isAeroWizard       = false;
    COLORREF     accentPrimary      = RGB(37, 99, 235);
    COLORREF     accentSecondary    = RGB(251, 191, 36);
    std::wstring welcomeHeadline    = L"Welcome to Setup";
    std::wstring welcomeIntro;
    std::wstring readMeText;
    std::wstring bannerTitle        = L"Choose how you would like to install this application.";
    std::wstring installBtnText     = L"&Install";
    std::wstring finishLaunchText   = L"&Launch application when Setup finishes";
    bool         offerLaunch        = true;
    bool         hasLicenseRtf      = false;
    std::wstring expressDesc        = L"One-click setup into the default installation folder with recommended shortcuts. Skips extra prompts.";
    std::wstring compactDesc        = L"Installs essential application files with minimal disk footprint using recommended folder defaults.";
    std::wstring customDesc         = L"Choose a custom destination folder, Start Menu group, and shortcuts step by step before installing.";
    std::wstring authorOrArtist;
};

/// Unescapes `\n` sequences serialized into single-line `[Branding]` values back into `\r\n`.
std::wstring UnescapeIniNewlines(const std::wstring& raw) {
    std::wstring out;
    out.reserve(raw.size() + 8);
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == L'\\' && i + 1 < raw.size() && raw[i + 1] == L'n') {
            out += L"\r\n";
            ++i;
        } else {
            out.push_back(raw[i]);
        }
    }
    return out;
}

/// Loads a zero-copy byte view `(outPtr, outSize)` from an embedded `RT_RCDATA` resource.
bool GetEmbeddedRcDataView(int resourceId, const std::uint8_t*& outPtr, std::size_t& outSize) noexcept {
    outPtr  = nullptr;
    outSize = 0;
    HMODULE hSelf = ::GetModuleHandleW(nullptr);
    HRSRC hFound  = ::FindResourceW(hSelf, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
    if (!hFound) return false;
    const DWORD sz = ::SizeofResource(hSelf, hFound);
    HGLOBAL hGlob  = ::LoadResource(hSelf, hFound);
    if (!hGlob || sz == 0) return false;
    outPtr  = static_cast<const std::uint8_t*>(::LockResource(hGlob));
    outSize = static_cast<std::size_t>(sz);
    return outPtr != nullptr;
}

/// Parses `kResIdBrandingIni` (`#202`) into `BrandingConfig`.
BrandingConfig LoadBrandingConfigFromResource() {
    BrandingConfig cfg{};
    const std::uint8_t* ptr = nullptr;
    std::size_t sz = 0;
    if (!GetEmbeddedRcDataView(rsw::kResIdBrandingIni, ptr, sz)) {
        return cfg;
    }

    std::string text(reinterpret_cast<const char*>(ptr), sz);
    std::istringstream iss(text);
    std::string line;
    std::map<std::string, std::string> kv;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '[' || line.front() == ';') continue;
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }

    auto getW = [&](const char* k, const std::wstring& defVal) -> std::wstring {
        auto it = kv.find(k);
        return (it != kv.end() && !it->second.empty())
            ? UnescapeIniNewlines(rsw::Utf8ToWide(it->second))
            : defVal;
    };

    cfg.productName      = getW("ProductName", cfg.productName);
    cfg.tagline          = getW("Tagline", cfg.tagline);
    cfg.websiteLabel     = getW("WebsiteLabel", cfg.websiteLabel);
    cfg.version          = getW("Version", cfg.version);
    cfg.manufacturer     = getW("Manufacturer", cfg.manufacturer);
    cfg.architecture     = getW("Architecture", cfg.architecture);
    cfg.productCode      = getW("ProductCode", L"");
    cfg.upgradeCode      = getW("UpgradeCode", L"");
    cfg.defaultSubdir    = getW("DefaultSubdir", cfg.defaultSubdir);
    cfg.startMenuFolder  = getW("StartMenuFolder", cfg.manufacturer);
    cfg.mainExecutable   = getW("MainExecutable", L"");
    cfg.perMachine       = rsw::EqualsIgnoreCase(kv["PerMachine"], "true");
    if (!kv["MinWindowsBuild"].empty()) {
        cfg.minWindowsBuild = std::atoi(kv["MinWindowsBuild"].c_str());
    }
    if (!kv["SilentRebootExitCode"].empty()) {
        cfg.silentRebootCode = std::atoi(kv["SilentRebootExitCode"].c_str());
    }
    if (!kv["UncompressedMsiSize"].empty()) {
        cfg.uncompressedMsiSz = static_cast<std::uint64_t>(std::strtoull(kv["UncompressedMsiSize"].c_str(), nullptr, 10));
    }
    cfg.osLaunchMessage  = getW("OsLaunchMessage", cfg.osLaunchMessage);
    cfg.isAutorun16x9    = rsw::EqualsIgnoreCase(kv["WizardLayout"], "autorun16x9");
    cfg.isAeroWizard     = rsw::EqualsIgnoreCase(kv["WizardLayout"], "aerowizard");
    cfg.accentPrimary    = rsw::ui::ParseHexColor(kv["AccentPrimaryHex"], cfg.accentPrimary);
    cfg.accentSecondary  = rsw::ui::ParseHexColor(kv["AccentSecondaryHex"], cfg.accentSecondary);
    cfg.welcomeHeadline  = getW("WelcomeHeadline", L"Welcome to " + cfg.productName + L" Setup");
    cfg.welcomeIntro     = getW("WelcomeIntro", L"");
    cfg.readMeText       = getW("ReadMeText", L"");
    cfg.bannerTitle      = getW("BannerTitle", cfg.bannerTitle);
    cfg.installBtnText   = getW("InstallButtonText", cfg.installBtnText);
    cfg.finishLaunchText = getW("FinishLaunchCheckbox", cfg.finishLaunchText);
    cfg.offerLaunch      = !rsw::EqualsIgnoreCase(kv["OfferLaunchOnFinish"], "false");
    cfg.hasLicenseRtf    = rsw::EqualsIgnoreCase(kv["HasLicenseRtf"], "true");
    cfg.expressDesc      = getW("ExpressDescription", cfg.expressDesc);
    cfg.compactDesc      = getW("CompactDescription", cfg.compactDesc);
    cfg.customDesc       = getW("CustomDescription", cfg.customDesc);
    cfg.authorOrArtist   = getW("AuthorOrArtist", L"");
    return cfg;
}

/// Computes the default target path (`C:\Program Files\<Subdir>` or `%LOCALAPPDATA%\<Subdir>`).
std::wstring ComputeDefaultInstallDir(const BrandingConfig& cfg) {
    wchar_t baseBuf[MAX_PATH * 2]{};
    const wchar_t* envName = cfg.perMachine ? L"ProgramW6432" : L"LOCALAPPDATA";
    DWORD n = ::GetEnvironmentVariableW(envName, baseBuf, static_cast<DWORD>(std::size(baseBuf)));
    if (n == 0) {
        n = ::GetEnvironmentVariableW(L"ProgramFiles", baseBuf, static_cast<DWORD>(std::size(baseBuf)));
    }
    std::filesystem::path p = (n > 0) ? std::filesystem::path(baseBuf) : std::filesystem::path(L"C:\\Program Files");
    p /= cfg.defaultSubdir;
    return p.lexically_normal().wstring();
}

/// Formats a byte count into a clean consumer-friendly string (`"14.2 MB"`, `"842.6 GB"`).
std::wstring FormatByteSizeFriendly(std::uint64_t bytes) {
    wchar_t buf[64]{};
    constexpr double kKB = 1024.0;
    constexpr double kMB = 1024.0 * 1024.0;
    constexpr double kGB = 1024.0 * 1024.0 * 1024.0;
    if (bytes >= static_cast<std::uint64_t>(kGB)) {
        std::swprintf(buf, std::size(buf), L"%.1f GB", static_cast<double>(bytes) / kGB);
    } else if (bytes >= static_cast<std::uint64_t>(kMB)) {
        std::swprintf(buf, std::size(buf), L"%.1f MB", static_cast<double>(bytes) / kMB);
    } else {
        std::swprintf(buf, std::size(buf), L"%.0f KB", std::max(1.0, static_cast<double>(bytes) / kKB));
    }
    return buf;
}

/// Queries free disk space for `targetPath` by walking up to the nearest existing ancestor directory.
bool QueryVolumeFreeBytesForPath(const std::wstring& targetPath, std::wstring& outRootLabel, std::uint64_t& outFreeBytes) {
    outFreeBytes = 0;
    outRootLabel = L"System Drive";
    if (targetPath.empty()) return false;

    std::error_code ec;
    std::filesystem::path probe(targetPath);
    if (probe.has_root_name()) {
        outRootLabel = L"Drive " + probe.root_name().wstring();
    }
    while (!probe.empty() && !std::filesystem::exists(probe, ec)) {
        const auto parent = probe.parent_path();
        if (parent == probe) break;
        probe = parent;
    }
    ULARGE_INTEGER avail{}, total{}, freeTotal{};
    if (::GetDiskFreeSpaceExW(probe.empty() ? L"C:\\" : probe.wstring().c_str(), &avail, &total, &freeTotal)) {
        outFreeBytes = avail.QuadPart;
        return true;
    }
    return false;
}

/// Loads embedded `[Prerequisite.*]` specs (`#205`) stamped into `Setup.exe`.
std::vector<rsw::RuntimePrerequisiteSpec> LoadEmbeddedPrerequisitesFromResource() {
    const std::uint8_t* ptr = nullptr;
    std::size_t sz = 0;
    if (!GetEmbeddedRcDataView(rsw::kResIdPrereqManifest, ptr, sz) || sz == 0) {
        return {};
    }
    return rsw::ParsePrerequisiteTableIni(std::string_view(reinterpret_cast<const char*>(ptr), sz));
}

/// Writes an embedded `RT_RCDATA` resource (`#201` `Product.msi` or `#300+i` runtime) to disk,
/// transparently decompressing `COMPRESS_ALGORITHM_LZMS` (`"RSWLZMS1"`) streams in memory!
bool ExtractEmbeddedResourceToFile(int resourceId, const std::filesystem::path& destFile, std::wstring& outError) {
    const std::uint8_t* ptr = nullptr;
    std::size_t sz = 0;
    if (!GetEmbeddedRcDataView(resourceId, ptr, sz)) {
        outError = L"Required embedded setup payload (#" + std::to_wstring(resourceId) + L") is not present.";
        return false;
    }

    std::vector<std::uint8_t> rawBytes;
    std::string decErr;
    if (!rsw::DecompressLzmsOrCopyRaw(ptr, sz, rawBytes, decErr)) {
        outError = L"Failed decompressing embedded setup payload: " + rsw::Utf8ToWide(decErr);
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destFile.parent_path(), ec);
    std::ofstream out(destFile, std::ios::binary | std::ios::trunc);
    if (!out) {
        outError = L"Could not write temporary setup package to:\n" + destFile.wstring();
        return false;
    }
    if (!rawBytes.empty()) {
        out.write(reinterpret_cast<const char*>(rawBytes.data()), static_cast<std::streamsize>(rawBytes.size()));
    }
    return out.good();
}

bool ExtractEmbeddedMsiToFile(const std::filesystem::path& destFile, std::wstring& outError) {
    return ExtractEmbeddedResourceToFile(rsw::kResIdEmbeddedMsi, destFile, outError);
}

/// Eliminates the common external-UI `"Copying New File: [1]"` bug by stripping any unexpanded
/// Windows Installer `[1]`..`[99]` field placeholders and cleaning dangling `: ` separators!
std::wstring SanitizeUnexpandedMsiTokens(std::wstring s) {
    std::wstring out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'[') {
            std::size_t j = i + 1;
            while (j < s.size() && s[j] >= L'0' && s[j] <= L'9') ++j;
            if (j > i + 1 && j < s.size() && s[j] == L']') {
                i = j; // Skip unexpanded `[1]`, `[6]`, `[9]`, etc.!
                continue;
            }
        }
        out.push_back(s[i]);
    }
    while (!out.empty() && (out.back() == L' ' || out.back() == L':' || out.back() == L'-')) {
        out.pop_back();
    }
    return out;
}

/// Formats an `INSTALLMESSAGE_ACTIONDATA` record (`hRecord`) using the `Template` captured during
/// `INSTALLMESSAGE_ACTIONSTART` via `MsiFormatRecordW` so live filenames (`"Copying new files: App.exe"`)
/// display cleanly without EVER printing raw `[1]`!
std::wstring FormatMsiActionDataStatus(
    const std::wstring& actionDesc,
    const std::wstring& actionTemplate,
    MSIHANDLE hRecord)
{
    std::wstring detail;
    if (!actionTemplate.empty()) {
        ::MsiRecordSetStringW(hRecord, 0, actionTemplate.c_str());
        wchar_t fmtBuf[1024]{};
        DWORD cch = static_cast<DWORD>(std::size(fmtBuf));
        if (::MsiFormatRecordW(0, hRecord, fmtBuf, &cch) == ERROR_SUCCESS && fmtBuf[0] != L'\0') {
            detail = SanitizeUnexpandedMsiTokens(fmtBuf);
        }
    } else if (::MsiRecordGetFieldCount(hRecord) >= 1) {
        wchar_t f1Buf[512]{};
        DWORD cch = static_cast<DWORD>(std::size(f1Buf));
        if (::MsiRecordGetStringW(hRecord, 1, f1Buf, &cch) == ERROR_SUCCESS && f1Buf[0] != L'\0') {
            detail = SanitizeUnexpandedMsiTokens(f1Buf);
        }
    }

    std::wstring base = SanitizeUnexpandedMsiTokens(actionDesc);
    while (!base.empty() && base.back() == L'.') base.pop_back();
    if (detail.empty()) {
        return actionDesc.empty() ? L"Applying system changes..." : SanitizeUnexpandedMsiTokens(actionDesc);
    }
    if (base.empty()) return detail;
    return base + L": " + detail;
}

/// Streams RTF bytes into a Win32 RichEdit control (`EM_STREAMIN` + `SF_RTF`).
struct RtfStreamCookie {
    const char* data = nullptr;
    std::size_t size = 0;
    std::size_t pos  = 0;
};

DWORD CALLBACK RichEditStreamInCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) {
    auto* c = reinterpret_cast<RtfStreamCookie*>(dwCookie);
    if (!c || c->pos >= c->size) {
        *pcb = 0;
        return 0;
    }
    const std::size_t rem = c->size - c->pos;
    const std::size_t toCopy = std::min<std::size_t>(rem, static_cast<std::size_t>(cb));
    std::memcpy(pbBuff, c->data + c->pos, toCopy);
    c->pos += toCopy;
    *pcb = static_cast<LONG>(toCopy);
    return 0;
}

/// Removes Win32 `&` accelerator markers (`"&Express"` -> `"Express"`) for plain screen-reader/summary copy.
std::wstring StripMnemonics(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L'&') {
            if (i + 1 < s.size() && s[i + 1] == L'&') {
                out.push_back(L'&');
                ++i;
            }
            continue;
        }
        out.push_back(s[i]);
    }
    return out;
}

/// Checks whether the current process token already holds Administrator elevation (`TokenElevation`).
bool IsCurrentProcessElevated() noexcept {
    HANDLE hToken = nullptr;
    if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return false;
    }
    TOKEN_ELEVATION elev{};
    DWORD cb = sizeof(elev);
    const BOOL ok = ::GetTokenInformation(hToken, TokenElevation, &elev, sizeof(elev), &cb);
    ::CloseHandle(hToken);
    return ok && (elev.TokenIsElevated != 0);
}

/// Strips trailing path slashes (`C:\Apps\Foo\` -> `C:\Apps\Foo`) before wrapping in `"..."` on an MSI
/// command line so a trailing `\` never escapes the closing `"` (`\"`) inside `MsiInstallProductW`!
std::wstring SanitizeMsiDirValue(std::wstring raw) {
    while (raw.size() > 3 && (raw.back() == L'\\' || raw.back() == L'/')) {
        raw.pop_back();
    }
    if (raw.size() == 3 && raw[1] == L':' && (raw[2] == L'\\' || raw[2] == L'/')) {
        raw = raw.substr(0, 2) + L"\\\\"; // Escape drive root `C:\\` so `"C:\\"` doesn't escape `"`!
    }
    return raw;
}

class SetupWizardApp;

/// Per-button visual metadata attached to our WCAG 2.2 AA subclassed `BS_AUTORADIOBUTTON` option cards!
struct OptionCardVisual {
    SetupWizardApp*     app            = nullptr;
    HWND                hwnd           = nullptr;
    rsw::ui::GlassGlyph glyph          = rsw::ui::GlassGlyph::Bolt;
    COLORREF            rimRgb         = rsw::ui::brand::kSkyCyan;
    COLORREF            accentRgb      = rsw::ui::brand::kWarmGold;
    std::wstring        titleMnemonic;
    std::wstring        description;
    bool                isAutorunCell  = false;
    bool                isHovered      = false;
    std::vector<HWND>   groupSiblings;
};

/// Per-checkbox visual metadata attached to our WCAG 2.2 AAA subclassed `BS_AUTOCHECKBOX` controls!
struct CheckboxVisual {
    SetupWizardApp* app           = nullptr;
    HWND            hwnd          = nullptr;
    bool            onCardSurface = false;
    bool            isHovered     = false;
};

// ================================================================================
// MAIN INTERACTIVE SETUP WIZARD APPLICATION (`SetupWizardApp`)
// ================================================================================
class SetupWizardApp {
public:
    BrandingConfig          cfg{};
    rsw::ui::RswTheme       theme{};
    HWND                    hWnd              = nullptr;
    HMODULE                 hRichEditDll      = nullptr;
    HICON                   hAppIconBig       = nullptr;
    HICON                   hAppIconSmall     = nullptr;
    HBITMAP                 hArtworkBmp       = nullptr;
    int                     artworkW          = 0;
    int                     artworkH          = 0;

    HFONT                   hFontHeadline     = nullptr;
    HFONT                   hFontSection      = nullptr;
    HFONT                   hFontBody         = nullptr;
    HFONT                   hFontBold         = nullptr;
    HFONT                   hFontSmall        = nullptr;

    WizardPage              currentPage       = WizardPage::Welcome;
    SelectedAction          selectedAction    = SelectedAction::InstallExpress;
    int                     autorunChoiceIdx  = 0; // 0..3 in `autorun16x9` 2x2 grid
    bool                    alreadyInstalled  = false;
    std::wstring            installedProductCode;
    std::wstring            installDir;
    std::wstring            startMenuGroup;
    std::uint64_t           estimatedReqBytes = 12u * 1024u * 1024u;
    std::filesystem::path   tempWorkDir;
    std::filesystem::path   tempMsiPath;
    std::filesystem::path   msiLogPath;

    // Subclassed accessible radio cards & checkboxes (`unique_ptr` keeps pointer addresses stable for `SetWindowSubclass`!)
    std::vector<std::unique_ptr<OptionCardVisual>>  cardVisuals;
    std::vector<std::unique_ptr<CheckboxVisual>>    checkboxVisuals;

    // MSI `SelectionTree` state (`WizardPage::CustomFeatures`) & `[Prerequisite.*]` runtime specs!
    std::vector<rsw::MsiFeatureNode>                featureNodes;
    std::size_t                                     selectedFeatureIdx = 0;
    bool                                            featuresLoaded     = false;
    HIMAGELIST                                      hFeatStateIml      = nullptr;
    std::vector<rsw::RuntimePrerequisiteSpec>       prereqSpecs;
    std::vector<std::size_t>                        pendingPrereqIndices;

    // Standard Glass & Autorun child controls
    HWND hModeExpress = nullptr, hModeCompact = nullptr, hModeCustom = nullptr;
    HWND hMaintModify = nullptr, hMaintRepair = nullptr, hMaintRemove = nullptr;
    HWND hRepairQuick = nullptr, hRepairFull  = nullptr;
    HWND hUninstCache = nullptr, hUninstPrefs = nullptr;
    HWND hLicRich     = nullptr, hLicAccept   = nullptr, hReadMeEdit = nullptr;
    HWND hFeatTree    = nullptr, hFeatResetBtn = nullptr;
    HWND hDirLabel    = nullptr, hDirEdit     = nullptr, hDirBrowse  = nullptr;
    HWND hSmLabel     = nullptr, hSmEdit      = nullptr, hDeskScChk  = nullptr, hFileAssocChk = nullptr;
    HWND hProgStatus  = nullptr, hProgBar     = nullptr;
    HWND hFinLaunch   = nullptr, hFinViewLog  = nullptr;
    HWND hArCell[4]   = {nullptr, nullptr, nullptr, nullptr};
    HWND hBtnBack     = nullptr, hBtnPrimary  = nullptr, hBtnCancel  = nullptr;

    // Thread-safe MSI external UI progress state
    std::thread             workerThread;
    std::atomic<bool>       cancelRequested{false};
    std::atomic<bool>       installRunning{false};
    int                     currentPermille   = 0;
    int                     lastAnnouncedDecile = -1;
    UINT                    finalMsiResult    = ERROR_SUCCESS;
    std::mutex              msgMutex;
    std::wstring            latestStatusText  = L"Preparing Windows Installer transaction...";
    std::wstring            capturedErrorText;
    std::wstring            curMsiActionDesc;
    std::wstring            curMsiActionTmpl;

    // MSI 4-part progress state machine (`INSTALLMESSAGE_PROGRESS`)
    long long               msiTotalTicks     = 1000;
    long long               msiCurTicks       = 0;
    long long               msiStepTicks      = 1;
    bool                    msiForward        = true;

    ~SetupWizardApp() {
        if (workerThread.joinable()) workerThread.join();
        DestroyFonts();
        if (hFeatStateIml) ::ImageList_Destroy(hFeatStateIml);
        if (hArtworkBmp)   ::DeleteObject(hArtworkBmp);
        if (hAppIconBig)   ::DestroyIcon(hAppIconBig);
        if (hAppIconSmall) ::DestroyIcon(hAppIconSmall);
        if (hRichEditDll)  ::FreeLibrary(hRichEditDll);
        if (!tempWorkDir.empty()) {
            std::error_code ec;
            std::filesystem::remove_all(tempWorkDir, ec);
        }
    }

    void DestroyFonts() noexcept {
        for (HFONT* pf : {&hFontHeadline, &hFontSection, &hFontBody, &hFontBold, &hFontSmall}) {
            if (*pf) { ::DeleteObject(*pf); *pf = nullptr; }
        }
    }

    void RebuildFonts(UINT dpi) noexcept {
        DestroyFonts();
        const int ts  = theme.textScalePercent;
        hFontHeadline = rsw::ui::CreateScaledFont(L"Segoe UI", 15, FW_BOLD,     dpi, ts);
        hFontSection  = rsw::ui::CreateScaledFont(L"Segoe UI", 12, FW_SEMIBOLD, dpi, ts);
        hFontBody     = rsw::ui::CreateScaledFont(L"Segoe UI", 10, FW_NORMAL,   dpi, ts);
        hFontBold     = rsw::ui::CreateScaledFont(L"Segoe UI", 10, FW_SEMIBOLD, dpi, ts);
        hFontSmall    = rsw::ui::CreateScaledFont(L"Segoe UI",  9, FW_NORMAL,   dpi, ts);
    }

    /// Checks whether a product sharing `cfg.upgradeCode` is already installed on this PC
    /// AND evaluates which `[Prerequisite.*]` runtimes still need to be installed!
    void DetectExistingInstallation() {
        if (cfg.uncompressedMsiSz > 0) {
            estimatedReqBytes = std::max<std::uint64_t>(2u * 1024u * 1024u, cfg.uncompressedMsiSz * 5u / 2u);
        } else {
            const std::uint8_t* msiPtr = nullptr;
            std::size_t msiSz = 0;
            if (GetEmbeddedRcDataView(rsw::kResIdEmbeddedMsi, msiPtr, msiSz) && msiSz > 0) {
                estimatedReqBytes = std::max<std::uint64_t>(2u * 1024u * 1024u, static_cast<std::uint64_t>(msiSz) * 5u / 2u);
            }
        }

        RefreshPendingPrerequisites();

        if (cfg.upgradeCode.empty()) return;
        wchar_t prodBuf[39]{};
        if (::MsiEnumRelatedProductsW(cfg.upgradeCode.c_str(), 0, 0, prodBuf) == ERROR_SUCCESS) {
            alreadyInstalled     = true;
            installedProductCode = prodBuf;
            selectedAction       = SelectedAction::MaintQuickRepair;
            if (!cfg.isAutorun16x9) {
                currentPage = WizardPage::Maintenance;
            }
        }
    }

    void RefreshPendingPrerequisites() {
        pendingPrereqIndices.clear();
        const std::string arch = rsw::WideToUtf8(cfg.architecture);
        for (std::size_t i = 0; i < prereqSpecs.size(); ++i) {
            if (!rsw::IsPrerequisiteSatisfied(prereqSpecs[i], arch)) {
                pendingPrereqIndices.push_back(i);
            }
        }
    }

    /// Ensures `tempWorkDir` and extracted `Product.msi` exist so `SelectionTree` can read `Feature` rows & byte costs!
    bool EnsureTempMsiExtracted(std::wstring& outErr) {
        std::error_code ec;
        if (tempWorkDir.empty()) {
            tempWorkDir = std::filesystem::temp_directory_path(ec) /
                          (L"setupwizard-" + std::to_wstring(::GetCurrentProcessId()));
        }
        std::filesystem::create_directories(tempWorkDir, ec);
        if (tempMsiPath.empty()) {
            tempMsiPath = tempWorkDir / L"Product.msi";
        }
        if (std::filesystem::exists(tempMsiPath, ec) && std::filesystem::file_size(tempMsiPath, ec) > 0) {
            return true;
        }
        return ExtractEmbeddedMsiToFile(tempMsiPath, outErr);
    }

    /// Populates the authentic Windows Installer `SelectionTree` (`hFeatTree`) from embedded `Product.msi`!
    void EnsureFeatureTreeLoaded() {
        if (featuresLoaded || !hFeatTree) return;
        std::wstring err;
        if (!EnsureTempMsiExtracted(err)) return;

        if (!rsw::LoadFeatureTreeFromMsiDatabase(
                tempMsiPath,
                alreadyInstalled ? installedProductCode : std::wstring{},
                featureNodes)) {
            return;
        }
        featuresLoaded = true;
        const std::uint64_t selBytes = rsw::RefreshMsiFeatureRollupsAndTotalBytes(featureNodes);
        if (selBytes > 0) estimatedReqBytes = selBytes;

        RebuildFeatureTreeItems();
    }

    void RebuildFeatureStateImages(UINT dpi) {
        if (!hFeatTree) return;
        HIMAGELIST hNew = rsw::CreateMsiStateImageList(dpi, theme);
        if (hNew) {
            TreeView_SetImageList(hFeatTree, hNew, TVSIL_STATE);
            if (hFeatStateIml) ::ImageList_Destroy(hFeatStateIml);
            hFeatStateIml = hNew;
        }
        TreeView_SetBkColor(hFeatTree, theme.cardBg);
        TreeView_SetTextColor(hFeatTree, theme.textPrimary);
        TreeView_SetItemHeight(hFeatTree, static_cast<SHORT>(rsw::ui::ScalePx(26, dpi, theme.textScalePercent)));
    }

    void RebuildFeatureTreeItems() {
        if (!hFeatTree) return;
        TreeView_DeleteAllItems(hFeatTree);
        std::map<std::string, HTREEITEM> idToHandle;

        for (std::size_t i = 0; i < featureNodes.size(); ++i) {
            auto& fn = featureNodes[i];
            HTREEITEM hParent = TVI_ROOT;
            if (!fn.parentId.empty()) {
                auto it = idToHandle.find(rsw::AsciiLower(fn.parentId));
                if (it != idToHandle.end()) hParent = it->second;
            }

            TVINSERTSTRUCTW tvi{};
            tvi.hParent        = hParent;
            tvi.hInsertAfter   = TVI_LAST;
            tvi.item.mask      = TVIF_TEXT | TVIF_PARAM | TVIF_STATE;
            tvi.item.pszText   = fn.title.data();
            tvi.item.lParam    = static_cast<LPARAM>(i);
            tvi.item.stateMask = TVIS_STATEIMAGEMASK | TVIS_EXPANDED;
            tvi.item.state     = INDEXTOSTATEIMAGEMASK(static_cast<int>(fn.currentState)) | TVIS_EXPANDED;
            fn.hTreeItem       = TreeView_InsertItem(hFeatTree, &tvi);
            idToHandle[rsw::AsciiLower(fn.id)] = fn.hTreeItem;
        }

        if (!featureNodes.empty()) {
            selectedFeatureIdx = 0;
            TreeView_SelectItem(hFeatTree, featureNodes[0].hTreeItem);
        }
    }

    void SyncFeatureTreeStateIcons() {
        const std::uint64_t selBytes = rsw::RefreshMsiFeatureRollupsAndTotalBytes(featureNodes);
        if (selBytes > 0 || !featureNodes.empty()) {
            estimatedReqBytes = std::max<std::uint64_t>(64u * 1024u, selBytes);
        }
        for (const auto& fn : featureNodes) {
            if (!fn.hTreeItem) continue;
            TVITEMW item{};
            item.mask      = TVIF_HANDLE | TVIF_STATE;
            item.hItem     = fn.hTreeItem;
            item.stateMask = TVIS_STATEIMAGEMASK;
            item.state     = INDEXTOSTATEIMAGEMASK(static_cast<int>(fn.currentState));
            TreeView_SetItem(hFeatTree, &item);
        }
        ::InvalidateRect(hFeatTree, nullptr, FALSE);
        ::InvalidateRect(hWnd, nullptr, FALSE);
    }

    /// Displays the genuine Windows Installer `SelectionTree` popup menu for `featureNodes[idx]`!
    void ShowFeatureStatePopupMenu(std::size_t idx, POINT screenPt) {
        if (idx >= featureNodes.size()) return;
        const auto& fn = featureNodes[idx];

        HMENU hMenu = ::CreatePopupMenu();
        if (!hMenu) return;

        constexpr UINT ID_ACT_LOCAL      = 4001;
        constexpr UINT ID_ACT_SUBTREE    = 4002;
        constexpr UINT ID_ACT_ADVERTISED = 4003;
        constexpr UINT ID_ACT_ABSENT     = 4004;

        ::AppendMenuW(hMenu, MF_STRING | ((fn.currentState == rsw::MsiTreeState::Local) ? MF_CHECKED : 0),
                      ID_ACT_LOCAL, L"&Will be installed on local hard drive");
        ::AppendMenuW(hMenu, MF_STRING | ((fn.currentState == rsw::MsiTreeState::EntireSubtree) ? MF_CHECKED : 0),
                      ID_ACT_SUBTREE, L"&Entire feature will be installed on local hard drive");
        if (fn.AllowAdvertise()) {
            ::AppendMenuW(hMenu, MF_STRING | ((fn.currentState == rsw::MsiTreeState::Advertised) ? MF_CHECKED : 0),
                          ID_ACT_ADVERTISED, L"Feature will be installed when &required");
        }
        ::AppendMenuW(hMenu, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(hMenu, MF_STRING |
                      (fn.DisallowAbsent() ? MF_GRAYED : 0) |
                      ((fn.currentState == rsw::MsiTreeState::Absent) ? MF_CHECKED : 0),
                      ID_ACT_ABSENT,
                      fn.DisallowAbsent()
                          ? L"Entire feature will be &unavailable (Required Core Feature)"
                          : L"Entire feature will be &unavailable");

        const UINT picked = static_cast<UINT>(::TrackPopupMenu(
            hMenu, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN | TPM_RIGHTBUTTON,
            screenPt.x, screenPt.y, 0, hWnd, nullptr));
        ::DestroyMenu(hMenu);

        if (picked == ID_ACT_LOCAL)           rsw::ApplyMsiFeatureActionRecursive(featureNodes, idx, rsw::MsiTreeState::Local);
        else if (picked == ID_ACT_SUBTREE)    rsw::ApplyMsiFeatureActionRecursive(featureNodes, idx, rsw::MsiTreeState::EntireSubtree);
        else if (picked == ID_ACT_ADVERTISED) rsw::ApplyMsiFeatureActionRecursive(featureNodes, idx, rsw::MsiTreeState::Advertised);
        else if (picked == ID_ACT_ABSENT)     rsw::ApplyMsiFeatureActionRecursive(featureNodes, idx, rsw::MsiTreeState::Absent);
        else return;

        SyncFeatureTreeStateIcons();
        ::NotifyWinEvent(EVENT_OBJECT_STATECHANGE, hFeatTree, OBJID_CLIENT, CHILDID_SELF);
    }

    /// Synthesizes clean ReadMe liner notes when `[UI] ReadMeText` was not explicitly supplied.
    std::wstring EffectiveReadMeText() const {
        if (!cfg.readMeText.empty()) return cfg.readMeText;
        std::wostringstream ss;
        ss << cfg.productName;
        if (!cfg.authorOrArtist.empty()) ss << L" — " << cfg.authorOrArtist;
        ss << L"\r\nVersion " << cfg.version << L"  •  Published by " << cfg.manufacturer << L"\r\n"
           << L"------------------------------------------------------------------------\r\n\r\n"
           << cfg.tagline << L"\r\n\r\n";
        if (!cfg.welcomeIntro.empty()) {
            ss << cfg.welcomeIntro << L"\r\n\r\n";
        }
        ss << L"DEFAULT LIBRARY / INSTALLATION FOLDER:\r\n  " << installDir << L"\r\n\r\n"
           << L"ACCESSIBILITY & PRIVACY:\r\n"
           << L"  • Full keyboard navigation (Tab, Arrow keys, Space, Enter, Esc, and Alt+Key shortcuts)\r\n"
           << L"  • Automatic Dark Mode, Light Mode, and Windows High Contrast support\r\n"
           << L"  • Zero background telemetry and clean Windows Installer uninstallation\r\n";
        return ss.str();
    }

    // ----------------------------------------------------------------------------
    // WCAG 2.2 AA SUBCLASSED `BS_AUTORADIOBUTTON` OPTION CARD PAINTER
    // ----------------------------------------------------------------------------
    static LRESULT CALLBACK OptionCardSubclassProc(
        HWND hBtn,
        UINT msg,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR /*uIdSubclass*/,
        DWORD_PTR dwRefData)
    {
        auto* vis = reinterpret_cast<OptionCardVisual*>(dwRefData);
        if (!vis || !vis->app) {
            return ::DefSubclassProc(hBtn, msg, wParam, lParam);
        }

        switch (msg) {
            case WM_ERASEBKGND:
                return 1; // Double-buffered in WM_PAINT!

            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdcOut = ::BeginPaint(hBtn, &ps);
                RECT rc{};
                ::GetClientRect(hBtn, &rc);
                const int w = std::max<int>(1, rc.right - rc.left);
                const int h = std::max<int>(1, rc.bottom - rc.top);

                HDC memDc      = ::CreateCompatibleDC(hdcOut);
                HBITMAP memBmp = ::CreateCompatibleBitmap(hdcOut, w, h);
                HGDIOBJ oldBmp = ::SelectObject(memDc, memBmp);

                ::FillRect(memDc, &rc, vis->app->theme.hbrWindow);

                Gdiplus::Graphics g(memDc);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

                const UINT dpi      = ::GetDpiForWindow(hBtn);
                const bool selected = (::SendMessageW(hBtn, BM_GETCHECK, 0, 0) == BST_CHECKED);
                const bool focused  = (::GetFocus() == hBtn);

                if (vis->isAutorunCell) {
                    rsw::ui::DrawAutorunChoiceCell(
                        memDc, g, rc, dpi, vis->app->theme,
                        vis->titleMnemonic, vis->description,
                        selected, focused, vis->isHovered);
                } else {
                    rsw::ui::DrawAccessibleOptionCard(
                        memDc, g, rc, dpi, vis->app->theme,
                        vis->glyph, vis->rimRgb, vis->accentRgb,
                        vis->titleMnemonic, vis->description,
                        selected, focused, vis->isHovered);
                }

                ::BitBlt(hdcOut, 0, 0, w, h, memDc, 0, 0, SRCCOPY);
                ::SelectObject(memDc, oldBmp);
                ::DeleteObject(memBmp);
                ::DeleteDC(memDc);
                ::EndPaint(hBtn, &ps);
                return 0;
            }

            // Suppress legacy `user32.dll` direct-DC radio dot drawing during state transitions
            // while letting `DefSubclassProc` maintain 100% native `BM_GETCHECK` & UIA state!
            case BM_SETCHECK:
            case BM_SETSTATE:
            case WM_SETFOCUS:
            case WM_KILLFOCUS:
            case WM_ENABLE:
            case WM_UPDATEUISTATE: {
                ::SendMessageW(hBtn, WM_SETREDRAW, FALSE, 0);
                const LRESULT res = ::DefSubclassProc(hBtn, msg, wParam, lParam);
                ::SendMessageW(hBtn, WM_SETREDRAW, TRUE, 0);
                ::InvalidateRect(hBtn, nullptr, FALSE);
                if (msg == BM_SETCHECK || msg == WM_SETFOCUS) {
                    for (HWND sib : vis->groupSiblings) {
                        if (sib && sib != hBtn) ::InvalidateRect(sib, nullptr, FALSE);
                    }
                }
                return res;
            }

            case WM_MOUSEMOVE: {
                if (!vis->isHovered) {
                    vis->isHovered = true;
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hBtn, 0};
                    ::TrackMouseEvent(&tme);
                    ::InvalidateRect(hBtn, nullptr, FALSE);
                }
                break;
            }

            case WM_MOUSELEAVE:
                if (vis->isHovered) {
                    vis->isHovered = false;
                    ::InvalidateRect(hBtn, nullptr, FALSE);
                }
                break;

            case WM_NCDESTROY:
                ::RemoveWindowSubclass(hBtn, &SetupWizardApp::OptionCardSubclassProc, 1);
                break;
        }
        return ::DefSubclassProc(hBtn, msg, wParam, lParam);
    }

    // ----------------------------------------------------------------------------
    // WCAG 2.2 AAA SUBCLASSED `BS_AUTOCHECKBOX` PAINTER (`>= 15.8:1` Contrast!)
    // ----------------------------------------------------------------------------
    static LRESULT CALLBACK CheckboxSubclassProc(
        HWND hBtn,
        UINT msg,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR /*uIdSubclass*/,
        DWORD_PTR dwRefData)
    {
        auto* vis = reinterpret_cast<CheckboxVisual*>(dwRefData);
        if (!vis || !vis->app) {
            return ::DefSubclassProc(hBtn, msg, wParam, lParam);
        }

        switch (msg) {
            case WM_ERASEBKGND:
                return 1;

            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdcOut = ::BeginPaint(hBtn, &ps);
                RECT rc{};
                ::GetClientRect(hBtn, &rc);
                const int w = std::max<int>(1, rc.right - rc.left);
                const int h = std::max<int>(1, rc.bottom - rc.top);

                HDC memDc      = ::CreateCompatibleDC(hdcOut);
                HBITMAP memBmp = ::CreateCompatibleBitmap(hdcOut, w, h);
                HGDIOBJ oldBmp = ::SelectObject(memDc, memBmp);

                Gdiplus::Graphics g(memDc);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
                g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

                wchar_t labelBuf[512]{};
                ::GetWindowTextW(hBtn, labelBuf, static_cast<int>(std::size(labelBuf)));
                const UINT dpi     = ::GetDpiForWindow(hBtn);
                const bool checked = (::SendMessageW(hBtn, BM_GETCHECK, 0, 0) == BST_CHECKED);
                const bool focused = (::GetFocus() == hBtn);
                const bool onCard  = vis->onCardSurface && !vis->app->cfg.isAutorun16x9;

                rsw::ui::DrawAccessibleCheckbox(
                    memDc, g, rc, dpi, vis->app->theme,
                    labelBuf, checked, focused, vis->isHovered, onCard);

                ::BitBlt(hdcOut, 0, 0, w, h, memDc, 0, 0, SRCCOPY);
                ::SelectObject(memDc, oldBmp);
                ::DeleteObject(memBmp);
                ::DeleteDC(memDc);
                ::EndPaint(hBtn, &ps);
                return 0;
            }

            case BM_SETCHECK:
            case BM_SETSTATE:
            case WM_SETFOCUS:
            case WM_KILLFOCUS:
            case WM_ENABLE:
            case WM_UPDATEUISTATE: {
                ::SendMessageW(hBtn, WM_SETREDRAW, FALSE, 0);
                const LRESULT res = ::DefSubclassProc(hBtn, msg, wParam, lParam);
                ::SendMessageW(hBtn, WM_SETREDRAW, TRUE, 0);
                ::InvalidateRect(hBtn, nullptr, FALSE);
                return res;
            }

            case WM_LBUTTONUP:
            case WM_KEYUP: {
                const LRESULT res = ::DefSubclassProc(hBtn, msg, wParam, lParam);
                ::InvalidateRect(hBtn, nullptr, FALSE);
                return res;
            }

            case WM_MOUSEMOVE: {
                if (!vis->isHovered) {
                    vis->isHovered = true;
                    TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hBtn, 0};
                    ::TrackMouseEvent(&tme);
                    ::InvalidateRect(hBtn, nullptr, FALSE);
                }
                break;
            }

            case WM_MOUSELEAVE:
                if (vis->isHovered) {
                    vis->isHovered = false;
                    ::InvalidateRect(hBtn, nullptr, FALSE);
                }
                break;

            case WM_NCDESTROY:
                ::RemoveWindowSubclass(hBtn, &SetupWizardApp::CheckboxSubclassProc, 2);
                break;
        }
        return ::DefSubclassProc(hBtn, msg, wParam, lParam);
    }

    HWND CreateAccessibleCheckbox(const wchar_t* text, int id, bool onCardSurface = false) {
        HINSTANCE hInst = ::GetModuleHandleW(nullptr);
        HWND hBtn = ::CreateWindowExW(
            0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | BS_AUTOCHECKBOX,
            0, 0, 100, 26, hWnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);

        auto meta           = std::make_unique<CheckboxVisual>();
        meta->app           = this;
        meta->hwnd          = hBtn;
        meta->onCardSurface = onCardSurface;
        ::SetWindowSubclass(hBtn, &SetupWizardApp::CheckboxSubclassProc, 2, reinterpret_cast<DWORD_PTR>(meta.get()));
        checkboxVisuals.push_back(std::move(meta));
        return hBtn;
    }

    HWND CreateOptionRadioCard(
        int id,
        bool startOfGroup,
        bool isAutorunCell,
        rsw::ui::GlassGlyph glyph,
        COLORREF rimRgb,
        COLORREF accentRgb,
        const std::wstring& titleWithMnemonic,
        const std::wstring& description)
    {
        HINSTANCE hInst = ::GetModuleHandleW(nullptr);
        // Combine title + description in the underlying Win32 window text so UI Automation
        // screen readers (`Narrator`, `NVDA`, `JAWS`) read BOTH the title and helper text!
        const std::wstring uiaAccessibleName = titleWithMnemonic + L" — " + description;
        const DWORD style = WS_CHILD | WS_TABSTOP | BS_AUTORADIOBUTTON | (startOfGroup ? WS_GROUP : 0);

        HWND hBtn = ::CreateWindowExW(
            0, L"BUTTON", uiaAccessibleName.c_str(), style,
            0, 0, 100, 64, hWnd,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);

        auto meta           = std::make_unique<OptionCardVisual>();
        meta->app           = this;
        meta->hwnd          = hBtn;
        meta->glyph         = glyph;
        meta->rimRgb        = rimRgb;
        meta->accentRgb     = accentRgb;
        meta->titleMnemonic = titleWithMnemonic;
        meta->description   = description;
        meta->isAutorunCell = isAutorunCell;

        ::SetWindowSubclass(hBtn, &SetupWizardApp::OptionCardSubclassProc, 1, reinterpret_cast<DWORD_PTR>(meta.get()));
        cardVisuals.push_back(std::move(meta));
        return hBtn;
    }

    void LinkSiblingGroup(std::initializer_list<HWND> groupButtons) {
        std::vector<HWND> vec(groupButtons.begin(), groupButtons.end());
        for (auto& cv : cardVisuals) {
            for (HWND g : vec) {
                if (cv->hwnd == g) {
                    cv->groupSiblings = vec;
                    break;
                }
            }
        }
    }

    void UpdateAutorunCardText(HWND hCell, const std::wstring& titleWithMnemonic, const std::wstring& subtitle) {
        for (auto& cv : cardVisuals) {
            if (cv->hwnd == hCell) {
                cv->titleMnemonic = titleWithMnemonic;
                cv->description   = subtitle;
                const std::wstring uiaText = titleWithMnemonic + L" — " + subtitle;
                ::SendMessageW(hCell, WM_SETREDRAW, FALSE, 0);
                ::SetWindowTextW(hCell, uiaText.c_str());
                ::SendMessageW(hCell, WM_SETREDRAW, TRUE, 0);
                ::InvalidateRect(hCell, nullptr, FALSE);
                break;
            }
        }
    }

    void CreateControls() {
        HINSTANCE hInst = ::GetModuleHandleW(nullptr);
        hRichEditDll = ::LoadLibraryW(L"Msftedit.dll");

        auto mkBtn = [&](const wchar_t* text, int id, DWORD extraStyle) -> HWND {
            return ::CreateWindowExW(
                0, L"BUTTON", text, WS_CHILD | WS_TABSTOP | extraStyle,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
        };
        auto mkStatic = [&](const wchar_t* text, int id) -> HWND {
            return ::CreateWindowExW(
                0, L"STATIC", text, WS_CHILD,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
        };

        // 1. Standard Glass `SetupType` 3-Card Radio Group (`Bolt`, `Cube`, `Sliders`!)
        hModeExpress = CreateOptionRadioCard(
            IDC_MODE_EXPRESS, true, false,
            rsw::ui::GlassGlyph::Bolt, rsw::ui::brand::kSkyCyan, rsw::ui::brand::kWarmGold,
            L"&Express Setup (Recommended)", cfg.expressDesc);
        hModeCompact = CreateOptionRadioCard(
            IDC_MODE_COMPACT, false, false,
            rsw::ui::GlassGlyph::Cube, rsw::ui::brand::kRaggieBlue, rsw::ui::brand::kSkyCyan,
            L"Co&mpact Core Setup", cfg.compactDesc);
        hModeCustom = CreateOptionRadioCard(
            IDC_MODE_CUSTOM, false, false,
            rsw::ui::GlassGlyph::Sliders, rsw::ui::brand::kVioletTune, rsw::ui::brand::kWarmGold,
            L"C&ustom Setup (Choose Features, Folder && Shortcuts)", cfg.customDesc);
        LinkSiblingGroup({hModeExpress, hModeCompact, hModeCustom});
        ::SendMessageW(hModeExpress, BM_SETCHECK, BST_CHECKED, 0);

        // 2. Standard Glass `Maintenance` 3-Card Radio Group (`Blocks`, `ShieldCheck`, `Uninstall`!)
        hMaintModify = CreateOptionRadioCard(
            IDC_MAINT_MODIFY, true, false,
            rsw::ui::GlassGlyph::Blocks, rsw::ui::brand::kVioletTune, rsw::ui::brand::kSkyCyan,
            L"&Modify Installed Features && Shortcuts",
            L"Add or remove individual features in the MSI SelectionTree and update shortcuts.");
        hMaintRepair = CreateOptionRadioCard(
            IDC_MAINT_REPAIR, false, false,
            rsw::ui::GlassGlyph::ShieldCheck, rsw::ui::brand::kEmeraldOk, rsw::ui::brand::kWarmGold,
            L"&Repair && Verify Installation (Recommended)",
            L"Choose Quick Check & Repair or a Full Reinstall to restore missing or damaged files.");
        hMaintRemove = CreateOptionRadioCard(
            IDC_MAINT_REMOVE, false, false,
            rsw::ui::GlassGlyph::Uninstall, rsw::ui::brand::kCoralRose, rsw::ui::brand::kWarmGold,
            L"&Uninstall " + cfg.productName,
            L"Cleanly remove program files while keeping your personal documents safe.");
        LinkSiblingGroup({hMaintModify, hMaintRepair, hMaintRemove});
        ::SendMessageW(hMaintRepair, BM_SETCHECK, BST_CHECKED, 0);

        // 3. Office 2000 `DATA1.MSI`-Style `RepairMode` 2-Card Radio Group!
        hRepairQuick = CreateOptionRadioCard(
            IDC_REPAIR_QUICK, true, false,
            rsw::ui::GlassGlyph::ShieldCheck, rsw::ui::brand::kEmeraldOk, rsw::ui::brand::kWarmGold,
            L"&Quick Check && Repair (Recommended)",
            L"Inspects your installation and restores only missing, older, or corrupted program files and shortcuts.");
        hRepairFull = CreateOptionRadioCard(
            IDC_REPAIR_FULL, false, false,
            rsw::ui::GlassGlyph::Reinstall, rsw::ui::brand::kRaggieBlue, rsw::ui::brand::kSkyCyan,
            L"&Full Reinstall (Refresh All Program Files)",
            L"Replaces every installed program file, shortcut, and registry entry from the original setup package.");
        LinkSiblingGroup({hRepairQuick, hRepairFull});
        ::SendMessageW(hRepairQuick, BM_SETCHECK, BST_CHECKED, 0);

        // 4. `SafeUninstall` Opt-In Checkboxes (Unchecked by default to protect user work!)
        hUninstCache = CreateAccessibleCheckbox(L"&Also remove temporary cache and diagnostic log files", IDC_UNINST_CACHE_CHK, true);
        hUninstPrefs = CreateAccessibleCheckbox(L"A&lso remove saved window layout and user preferences", IDC_UNINST_PREFS_CHK, true);

        // 5. `16:9` CD-Autorun 2x2 Choice Grid (`640x480` Book & Album Micro-Installers!)
        hArCell[0] = CreateOptionRadioCard(IDC_AR_CELL_1, true,  true, rsw::ui::GlassGlyph::Bolt, rsw::ui::brand::kSkyCyan, rsw::ui::brand::kWarmGold, L"&Install Edition",   L"Add to local library");
        hArCell[1] = CreateOptionRadioCard(IDC_AR_CELL_2, false, true, rsw::ui::GlassGlyph::ReadMeBook, rsw::ui::brand::kSkyCyan, rsw::ui::brand::kWarmGold, L"View &ReadMe",       L"Edition notes & credits");
        hArCell[2] = CreateOptionRadioCard(IDC_AR_CELL_3, false, true, rsw::ui::GlassGlyph::Folder, rsw::ui::brand::kRaggieBlue, rsw::ui::brand::kWarmGold, L"C&hange Folder...",  L"Pick custom library path");
        hArCell[3] = CreateOptionRadioCard(IDC_AR_CELL_4, false, true, rsw::ui::GlassGlyph::Uninstall, rsw::ui::brand::kCoralRose, rsw::ui::brand::kWarmGold, L"E&xit Installer",    L"Close without changes");
        LinkSiblingGroup({hArCell[0], hArCell[1], hArCell[2], hArCell[3]});
        RefreshAutorunMenuLabels();
        ::SendMessageW(hArCell[0], BM_SETCHECK, BST_CHECKED, 0);

        // 6. License Agreement & Autorun ReadMe Viewers
        hLicRich = ::CreateWindowExW(
            WS_EX_CLIENTEDGE,
            hRichEditDll ? MSFTEDIT_CLASS : L"EDIT",
            L"",
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIC_RICHEDIT)), hInst, nullptr);
        LoadEmbeddedRtfIntoControl();
        hLicAccept = CreateAccessibleCheckbox(L"I have &read and accept the terms in the License Agreement", IDC_LIC_ACCEPT_CHK, false);

        hReadMeEdit = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", EffectiveReadMeText().c_str(),
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_README_EDIT)), hInst, nullptr);

        // 6B. Authentic Windows Installer `SelectionTree` (`SysTreeView32` + `TVSIL_STATE` split icons!)
        hFeatTree = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"Feature Selection Tree",
            WS_CHILD | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FEAT_TREE)), hInst, nullptr);
        hFeatResetBtn = mkBtn(L"&Reset Defaults", IDC_FEAT_RESET_BTN, BS_PUSHBUTTON);

        // 7. Custom Destination Folder (`STATIC` with `&` mnemonic immediately precedes `EDIT`!)
        hDirLabel  = mkStatic(L"&Destination folder path:", IDC_DIR_LABEL);
        hDirEdit   = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", installDir.c_str(),
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DIR_EDIT)), hInst, nullptr);
        hDirBrowse = mkBtn(L"&Browse...", IDC_DIR_BROWSE_BTN, BS_PUSHBUTTON);

        // 8. Custom Shortcuts & File Associations (`STATIC` with `&` mnemonic immediately precedes `EDIT`!)
        hSmLabel      = mkStatic(L"&Start Menu folder name:", IDC_SM_LABEL);
        hSmEdit       = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", startMenuGroup.c_str(),
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SM_EDIT)), hInst, nullptr);
        hDeskScChk    = CreateAccessibleCheckbox(L"&Create a Desktop shortcut for quick access", IDC_DESK_SC_CHK, false);
        hFileAssocChk = CreateAccessibleCheckbox(L"&Register supported file types with Windows Explorer", IDC_FILE_ASSOC_CHK, false);
        ::SendMessageW(hDeskScChk,    BM_SETCHECK, BST_CHECKED, 0);
        ::SendMessageW(hFileAssocChk, BM_SETCHECK, BST_CHECKED, 0);

        // 9. Progress & Completion Controls
        hProgStatus = mkStatic(L"Preparing Windows Installer transaction...", IDC_PROG_STATUS);
        hProgBar    = ::CreateWindowExW(
            0, PROGRESS_CLASSW, nullptr, WS_CHILD | PBS_SMOOTH,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROG_BAR)), hInst, nullptr);
        ::SendMessageW(hProgBar, PBM_SETRANGE32, 0, 1000);

        hFinLaunch  = CreateAccessibleCheckbox(cfg.finishLaunchText.c_str(), IDC_FIN_LAUNCH_CHK, false);
        ::SendMessageW(hFinLaunch, BM_SETCHECK, (cfg.offerLaunch && !cfg.mainExecutable.empty()) ? BST_CHECKED : BST_UNCHECKED, 0);
        hFinViewLog = mkBtn(L"&View Setup Log", IDC_FIN_VIEWLOG_BTN, BS_PUSHBUTTON);

        // 10. Universal Glass Footer Navigation Bar
        hBtnBack    = mkBtn(L"< &Back",                IDC_NAV_BACK,    BS_PUSHBUTTON);
        hBtnPrimary = mkBtn(cfg.installBtnText.c_str(), IDC_NAV_PRIMARY, BS_DEFPUSHBUTTON);
        hBtnCancel  = mkBtn(L"Cancel",                 IDC_NAV_CANCEL,  BS_PUSHBUTTON);

        // Subclass all push buttons so Dark Mode, Light Mode, & High Contrast render seamlessly
        // without `ComCtl32`'s hardcoded `#FFFFFF` white button background!
        rsw::ui::AttachAccessiblePushButtonSubclass(hFeatResetBtn, &theme, false, false);
        rsw::ui::AttachAccessiblePushButtonSubclass(hDirBrowse,    &theme, false, false);
        rsw::ui::AttachAccessiblePushButtonSubclass(hFinViewLog,   &theme, false, false);
        rsw::ui::AttachAccessiblePushButtonSubclass(hBtnBack,      &theme, false, true);
        rsw::ui::AttachAccessiblePushButtonSubclass(hBtnPrimary,   &theme, true,  true);
        rsw::ui::AttachAccessiblePushButtonSubclass(hBtnCancel,    &theme, false, true);

        rsw::ui::ApplyDarkScrollbarTheme(hLicRich,    theme);
        rsw::ui::ApplyDarkScrollbarTheme(hReadMeEdit, theme);
        rsw::ui::ApplyDarkScrollbarTheme(hFeatTree,   theme);
    }

    void RefreshAutorunMenuLabels() {
        if (!hArCell[0]) return;
        if (alreadyInstalled) {
            UpdateAutorunCardText(hArCell[0], L"&Repair / Reinstall", L"Verify all edition files");
            UpdateAutorunCardText(hArCell[1], L"View &ReadMe",        L"Edition notes & credits");
            UpdateAutorunCardText(hArCell[2], L"&Uninstall",          L"Remove from computer");
            UpdateAutorunCardText(hArCell[3], L"E&xit Installer",     L"Close without changes");
        } else {
            UpdateAutorunCardText(hArCell[0], L"&Install Edition",    L"Add to local library");
            UpdateAutorunCardText(hArCell[1], L"View &ReadMe",        L"Edition notes & credits");
            UpdateAutorunCardText(hArCell[2], L"C&hange Folder...",   L"Pick custom library path");
            UpdateAutorunCardText(hArCell[3], L"E&xit Installer",     L"Close without changes");
        }
    }

    void LoadEmbeddedRtfIntoControl() {
        if (!hLicRich) return;
        const std::uint8_t* ptr = nullptr;
        std::size_t sz = 0;
        if (!GetEmbeddedRcDataView(rsw::kResIdLicenseRtf, ptr, sz)) return;

        if (hRichEditDll) {
            RtfStreamCookie cookie{reinterpret_cast<const char*>(ptr), sz, 0};
            EDITSTREAM es{};
            es.dwCookie    = reinterpret_cast<DWORD_PTR>(&cookie);
            es.pfnCallback = &RichEditStreamInCallback;
            ::SendMessageW(hLicRich, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));
        } else {
            std::wstring plain = rsw::Utf8ToWide(std::string_view(reinterpret_cast<const char*>(ptr), sz));
            ::SetWindowTextW(hLicRich, plain.c_str());
        }
        ApplyRichEditReadableColors();
    }

    /// Ensures RichEdit RTF text ALWAYS maintains >= 15:1 WCAG AAA contrast in Dark, Light, & High Contrast!
    void ApplyRichEditReadableColors() {
        if (!hLicRich || !hRichEditDll) return;
        ::SendMessageW(hLicRich, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(theme.cardBg));
        CHARFORMAT2W cf{};
        cf.cbSize      = sizeof(cf);
        cf.dwMask      = CFM_COLOR;
        cf.crTextColor = theme.textPrimary;
        ::SendMessageW(hLicRich, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&cf));
    }

    void ApplyFontsAndLayout() {
        const UINT dpi = ::GetDpiForWindow(hWnd);
        RebuildFonts(dpi);

        for (HWND h : {hBtnPrimary, hDirLabel, hSmLabel}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);
        }
        for (HWND h : {hLicRich, hLicAccept, hReadMeEdit, hFeatTree, hFeatResetBtn, hDirEdit, hDirBrowse,
                       hSmEdit, hDeskScChk, hFileAssocChk, hUninstCache, hUninstPrefs,
                       hProgStatus, hFinLaunch, hFinViewLog, hBtnBack, hBtnCancel}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBody), TRUE);
        }
        ApplyRichEditReadableColors();
        RebuildFeatureStateImages(dpi);

        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = rc.right - rc.left;
        const int H = rc.bottom - rc.top;
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        const int footerH = S(58);
        const int footerY = H - footerH;
        const int btnY    = footerY + (footerH - S(32)) / 2;

        // Universal Footer Buttons (`[< &Back]  [&Next >]  [Cancel]` positioned consistently in the Wizard97 footer!)
        ::SetWindowTextW(hBtnBack, L"< &Back");
        ::MoveWindow(hBtnBack,    W - S(320), btnY, S(94),  S(32), TRUE);
        ::MoveWindow(hBtnPrimary, W - S(218), btnY, S(112), S(32), TRUE);
        ::MoveWindow(hBtnCancel,  W - S(98),  btnY, S(80),  S(32), TRUE);

        if (cfg.isAutorun16x9) {
            // `640 x 480` 16:9 CD-Autorun Geometry:
            //   - `y = 0..324`   : 16:9 OpenGraph cover banner + `3px` accent bar
            //   - `y = 334..414` : 2x2 interactive Autorun radio grid (`296 x 37` each!)
            const int cellW = (W - S(46)) / 2;
            const int cellH = S(37);
            const int row1Y = S(334);
            const int row2Y = row1Y + cellH + S(6);
            ::MoveWindow(hArCell[0], S(18),                row1Y, cellW, cellH, TRUE);
            ::MoveWindow(hArCell[1], S(28) + cellW,        row1Y, cellW, cellH, TRUE);
            ::MoveWindow(hArCell[2], S(18),                row2Y, cellW, cellH, TRUE);
            ::MoveWindow(hArCell[3], S(28) + cellW,        row2Y, cellW, cellH, TRUE);

            // Interior pages (`AutorunReadMe`, `License`, `CustomFolder`, `SafeUninstall`, `Installing`, `Finished`)
            // use `DrawAutorunCompactInteriorHeader` (`0..91px`) on top!
            const int intTop = S(104);
            ::MoveWindow(hReadMeEdit, S(18), intTop, W - S(36), footerY - intTop - S(14), TRUE);
            ::MoveWindow(hLicRich,    S(18), intTop, W - S(36), footerY - intTop - S(46), TRUE);
            ::MoveWindow(hLicAccept,  S(18), footerY - S(36), W - S(36), S(24), TRUE);

            ::MoveWindow(hDirLabel,   S(18), intTop + S(32), W - S(36),  S(22), TRUE);
            ::MoveWindow(hDirEdit,    S(18), intTop + S(58), W - S(132), S(30), TRUE);
            ::MoveWindow(hDirBrowse,  W - S(106), intTop + S(57), S(88), S(32), TRUE);

            ::MoveWindow(hUninstCache, S(24), intTop + S(128), W - S(48), S(26), TRUE);
            ::MoveWindow(hUninstPrefs, S(24), intTop + S(160), W - S(48), S(26), TRUE);

            ::MoveWindow(hProgStatus, S(30), intTop + S(124), W - S(60), S(24), TRUE);
            ::MoveWindow(hProgBar,    S(30), intTop + S(156), W - S(60), S(24), TRUE);

            ::MoveWindow(hFinLaunch,  S(26), footerY - S(52), W - S(190), S(26), TRUE);
            ::MoveWindow(hFinViewLog, W - S(158), footerY - S(54), S(140), S(30), TRUE);
        } else {
            // Standard RaggieSoft Glass Wizard (`620 x 490`):
            //   - `y = 0..92`    : `DrawGlassTopHeader` (`64x64` Step Medallion + `4px` Accent Bar)
            //   - `y = 104..424` : Step content canvas
            const int bodyTop = S(104);
            const int cardX   = S(18);
            const int cardW   = W - S(36);

            // `SetupType`: 3 Full-Row `48x48` Vector Option Cards (`72px` tall each!)
            const int optStart = bodyTop + S(38);
            const int optH     = S(72);
            const int optGap   = S(10);
            ::MoveWindow(hModeExpress, cardX, optStart,                      cardW, optH, TRUE);
            ::MoveWindow(hModeCompact, cardX, optStart + (optH + optGap),    cardW, optH, TRUE);
            ::MoveWindow(hModeCustom,  cardX, optStart + (optH + optGap) * 2,cardW, optH, TRUE);

            // `Maintenance`: Hero Brand Card (`104..166`) + 3 Option Cards (`66px` tall each!)
            const int mntStart = bodyTop + S(74);
            const int mntH     = S(68);
            const int mntGap   = S(8);
            ::MoveWindow(hMaintModify, cardX, mntStart,                      cardW, mntH, TRUE);
            ::MoveWindow(hMaintRepair, cardX, mntStart + (mntH + mntGap),    cardW, mntH, TRUE);
            ::MoveWindow(hMaintRemove, cardX, mntStart + (mntH + mntGap) * 2,cardW, mntH, TRUE);

            // `RepairMode`: Office 2000 `DATA1.MSI`-style 2 Option Cards (`78px` tall each!)
            const int repStart = bodyTop + S(46);
            const int repH     = S(78);
            ::MoveWindow(hRepairQuick, cardX, repStart,               cardW, repH, TRUE);
            ::MoveWindow(hRepairFull,  cardX, repStart + repH + S(12),cardW, repH, TRUE);

            // `SafeUninstall`: Hero Brand Card + 2 safe unchecked checkboxes
            ::MoveWindow(hUninstCache, S(28), bodyTop + S(162), W - S(56), S(26), TRUE);
            ::MoveWindow(hUninstPrefs, S(28), bodyTop + S(196), W - S(56), S(26), TRUE);

            // `License`: Step intro + High-Contrast RichEdit + Accept checkbox
            ::MoveWindow(hLicRich,   cardX, bodyTop + S(30), cardW, footerY - bodyTop - S(76), TRUE);
            ::MoveWindow(hLicAccept, cardX, footerY - S(36), cardW, S(26), TRUE);

            // `CustomFeatures` (`SelectionTree` on left + `Feature Details` Glass Card painted on right!)
            const int treeW = (cardW * 58) / 100;
            const int treeH = footerY - bodyTop - S(78);
            ::MoveWindow(hFeatTree,     cardX, bodyTop + S(32), treeW, treeH, TRUE);
            ::MoveWindow(hFeatResetBtn, cardX, bodyTop + S(38) + treeH, S(132), S(28), TRUE);

            // `CustomFolder`: Path `EDIT` + `&Browse...` (Disk Space Glass Card painted below!)
            ::MoveWindow(hDirLabel,  cardX, bodyTop + S(38), cardW,      S(22), TRUE);
            ::MoveWindow(hDirEdit,   cardX, bodyTop + S(64), cardW - S(98), S(30), TRUE);
            ::MoveWindow(hDirBrowse, cardX + cardW - S(90), bodyTop + S(63), S(90), S(32), TRUE);

            // `CustomShortcuts`: Start Menu group + Desktop & File Association checkboxes
            ::MoveWindow(hSmLabel,      cardX, bodyTop + S(42),  cardW, S(22), TRUE);
            ::MoveWindow(hSmEdit,       cardX, bodyTop + S(68),  cardW, S(30), TRUE);
            ::MoveWindow(hDeskScChk,    cardX, bodyTop + S(124), cardW, S(28), TRUE);
            ::MoveWindow(hFileAssocChk, cardX, bodyTop + S(160), cardW, S(28), TRUE);

            // `Installing`: Hero Brand Card + Glass Progress Card (`hProgStatus` + `hProgBar`)
            ::MoveWindow(hProgStatus, S(34), bodyTop + S(136), W - S(68), S(24), TRUE);
            ::MoveWindow(hProgBar,    S(34), bodyTop + S(168), W - S(68), S(24), TRUE);

            // `Finished`: Hero Brand Card + Summary Card + `&Launch` checkbox / `&View Setup Log`
            ::MoveWindow(hFinLaunch,  S(28), footerY - S(52), W - S(196), S(26), TRUE);
            ::MoveWindow(hFinViewLog, W - S(162), footerY - S(54), S(144), S(30), TRUE);
        }

        UpdateControlVisibilityForPage();
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    void UpdateControlVisibilityForPage() {
        auto show = [](HWND h, bool vis) {
            if (h) ::ShowWindow(h, vis ? SW_SHOW : SW_HIDE);
        };

        const bool pWelcome   = (currentPage == WizardPage::Welcome);
        const bool pLic       = (currentPage == WizardPage::License);
        const bool pReadMe    = (currentPage == WizardPage::AutorunReadMe);
        const bool pSetupType = (currentPage == WizardPage::SetupType);
        const bool pFeatures  = (currentPage == WizardPage::CustomFeatures);
        const bool pDir       = (currentPage == WizardPage::CustomFolder);
        const bool pShort     = (currentPage == WizardPage::CustomShortcuts);
        const bool pReady     = (currentPage == WizardPage::ReadyToInstall);
        const bool pMaint     = (currentPage == WizardPage::Maintenance);
        const bool pRepair    = (currentPage == WizardPage::RepairMode);
        const bool pSafeUn    = (currentPage == WizardPage::SafeUninstall);
        const bool pProg      = (currentPage == WizardPage::Installing);
        const bool pFin       = (currentPage == WizardPage::Finished);

        for (HWND c : hArCell) show(c, cfg.isAutorun16x9 && pWelcome);

        show(hModeExpress, pSetupType);
        show(hModeCompact, pSetupType);
        show(hModeCustom,  pSetupType);

        show(hMaintModify, pMaint);
        show(hMaintRepair, pMaint);
        show(hMaintRemove, pMaint);

        show(hRepairQuick, pRepair);
        show(hRepairFull,  pRepair);

        show(hUninstCache, pSafeUn);
        show(hUninstPrefs, pSafeUn);

        show(hLicRich,      pLic);
        show(hLicAccept,    pLic);
        show(hReadMeEdit,   pReadMe);
        show(hFeatTree,     pFeatures);
        show(hFeatResetBtn, pFeatures);

        show(hDirLabel,  pDir);
        show(hDirEdit,   pDir);
        show(hDirBrowse, pDir);

        show(hSmLabel,      pShort);
        show(hSmEdit,       pShort);
        show(hDeskScChk,    pShort);
        show(hFileAssocChk, pShort);

        show(hProgStatus, pProg);
        show(hProgBar,    pProg);

        const bool canLaunch = pFin &&
                               (finalMsiResult == ERROR_SUCCESS || finalMsiResult == ERROR_SUCCESS_REBOOT_REQUIRED) &&
                               (selectedAction != SelectedAction::MaintUninstall) &&
                               cfg.offerLaunch && !cfg.mainExecutable.empty();
        show(hFinLaunch,  canLaunch);
        show(hFinViewLog, pFin && (finalMsiResult != ERROR_SUCCESS && finalMsiResult != ERROR_INSTALL_USEREXIT));

        const bool canGoBack = pLic || pReadMe || pSetupType || pFeatures || pDir || pShort || pReady || pRepair || pSafeUn;
        show(hBtnBack,    canGoBack);
        show(hBtnPrimary, !pProg);
        show(hBtnCancel,  !pFin);

        // Configure Primary Action Button text, enabled gate, & Windows UAC Shield icon!
        bool showShield = false;
        if (pWelcome) {
            if (cfg.isAutorun16x9) {
                if (autorunChoiceIdx == 0) {
                    const bool direct = !cfg.hasLicenseRtf || alreadyInstalled;
                    ::SetWindowTextW(hBtnPrimary, alreadyInstalled ? L"&Repair" : (direct ? cfg.installBtnText.c_str() : L"&Next >"));
                    showShield = direct && cfg.perMachine;
                } else if (autorunChoiceIdx == 1) {
                    ::SetWindowTextW(hBtnPrimary, L"&Read Notes");
                } else if (autorunChoiceIdx == 2) {
                    ::SetWindowTextW(hBtnPrimary, alreadyInstalled ? L"&Continue >" : L"C&hange...");
                } else {
                    ::SetWindowTextW(hBtnPrimary, L"E&xit");
                }
            } else {
                ::SetWindowTextW(hBtnPrimary, L"&Next >");
            }
            ::EnableWindow(hBtnPrimary, TRUE);
        } else if (pReadMe) {
            ::SetWindowTextW(hBtnPrimary, alreadyInstalled ? L"&Done" : cfg.installBtnText.c_str());
            ::EnableWindow(hBtnPrimary, TRUE);
            showShield = !alreadyInstalled && cfg.perMachine && !cfg.hasLicenseRtf;
        } else if (pLic) {
            const bool accepted = (::SendMessageW(hLicAccept, BM_GETCHECK, 0, 0) == BST_CHECKED);
            ::SetWindowTextW(hBtnPrimary, cfg.isAutorun16x9 ? cfg.installBtnText.c_str() : L"&Next >");
            ::EnableWindow(hBtnPrimary, accepted ? TRUE : FALSE);
            showShield = cfg.isAutorun16x9 && cfg.perMachine;
        } else if (pSetupType || pFeatures || pShort || pMaint) {
            ::SetWindowTextW(hBtnPrimary, L"&Next >");
            ::EnableWindow(hBtnPrimary, TRUE);
        } else if (pDir) {
            ::SetWindowTextW(hBtnPrimary, cfg.isAutorun16x9 ? cfg.installBtnText.c_str() : L"&Next >");
            ::EnableWindow(hBtnPrimary, TRUE);
            showShield = cfg.isAutorun16x9 && cfg.perMachine;
        } else if (pReady) {
            ::SetWindowTextW(hBtnPrimary, cfg.installBtnText.c_str());
            ::EnableWindow(hBtnPrimary, TRUE);
            showShield = cfg.perMachine;
        } else if (pRepair) {
            ::SetWindowTextW(hBtnPrimary, L"&Repair Now");
            ::EnableWindow(hBtnPrimary, TRUE);
            showShield = cfg.perMachine;
        } else if (pSafeUn) {
            ::SetWindowTextW(hBtnPrimary, L"&Uninstall");
            ::EnableWindow(hBtnPrimary, TRUE);
            showShield = cfg.perMachine;
        } else if (pFin) {
            ::SetWindowTextW(hBtnPrimary, L"&Finish");
            ::EnableWindow(hBtnPrimary, TRUE);
        }
        const bool needsUac = cfg.perMachine && !IsCurrentProcessElevated();
        ::SendMessageW(hBtnPrimary, BCM_SETSHIELD, 0, (showShield && needsUac) ? TRUE : FALSE);

        // Announce wizard stage transition to assistive technologies (`Narrator` / `NVDA` / `JAWS`)
        ::NotifyWinEvent(EVENT_OBJECT_NAMECHANGE, hWnd, OBJID_CLIENT, CHILDID_SELF);
    }

    void BrowseForDestinationFolder() {
        IFileOpenDialog* pDlg = nullptr;
        if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))) && pDlg) {
            DWORD opts = 0;
            pDlg->GetOptions(&opts);
            pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
            pDlg->SetTitle(L"Select Destination Folder");

            if (SUCCEEDED(pDlg->Show(hWnd))) {
                IShellItem* pItem = nullptr;
                if (SUCCEEDED(pDlg->GetResult(&pItem)) && pItem) {
                    PWSTR pszPath = nullptr;
                    if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &pszPath)) && pszPath) {
                        installDir = pszPath;
                        ::SetWindowTextW(hDirEdit, installDir.c_str());
                        ::InvalidateRect(hWnd, nullptr, FALSE);
                        ::CoTaskMemFree(pszPath);
                    }
                    pItem->Release();
                }
            }
            pDlg->Release();
        }
    }

    // ----------------------------------------------------------------------------
    // WINDOWS INSTALLER (`MSI.DLL`) EXTERNAL UI RECORD HANDLER & WORKER THREAD
    // ----------------------------------------------------------------------------
    static INT CALLBACK OnMsiExternalRecord(void* pvContext, UINT iMessageType, MSIHANDLE hRecord) {
        auto* self = static_cast<SetupWizardApp*>(pvContext);
        if (!self) return 0;
        if (self->cancelRequested.load()) {
            return IDCANCEL; // Triggers clean Windows Installer transactional rollback!
        }

        const UINT msgKind = iMessageType & 0xFF000000u;
        switch (msgKind) {
            case INSTALLMESSAGE_ACTIONSTART: {
                wchar_t descBuf[512]{};
                wchar_t tmplBuf[512]{};
                DWORD cDesc = static_cast<DWORD>(std::size(descBuf));
                DWORD cTmpl = static_cast<DWORD>(std::size(tmplBuf));
                ::MsiRecordGetStringW(hRecord, 2, descBuf, &cDesc);
                ::MsiRecordGetStringW(hRecord, 3, tmplBuf, &cTmpl);
                if (descBuf[0] != L'\0') {
                    const std::wstring cleanDesc = SanitizeUnexpandedMsiTokens(descBuf);
                    std::lock_guard<std::mutex> lk(self->msgMutex);
                    self->curMsiActionDesc = cleanDesc;
                    self->curMsiActionTmpl = tmplBuf;
                    if (!cleanDesc.empty()) {
                        self->latestStatusText = cleanDesc;
                        ::PostMessageW(self->hWnd, WM_RSW_STATUS_TEXT, 0, 0);
                    }
                }
                return IDOK;
            }
            case INSTALLMESSAGE_ACTIONDATA: {
                std::wstring aDesc, aTmpl;
                {
                    std::lock_guard<std::mutex> lk(self->msgMutex);
                    aDesc = self->curMsiActionDesc;
                    aTmpl = self->curMsiActionTmpl;
                }
                const std::wstring line = FormatMsiActionDataStatus(aDesc, aTmpl, hRecord);
                if (!line.empty()) {
                    std::lock_guard<std::mutex> lk(self->msgMutex);
                    self->latestStatusText = line;
                    ::PostMessageW(self->hWnd, WM_RSW_STATUS_TEXT, 0, 0);
                }
                return IDOK;
            }
            case INSTALLMESSAGE_ERROR:
            case INSTALLMESSAGE_FATALEXIT: {
                wchar_t errBuf[1024]{};
                DWORD cch = static_cast<DWORD>(std::size(errBuf));
                ::MsiFormatRecordW(0, hRecord, errBuf, &cch);
                if (errBuf[0] != L'\0') {
                    std::lock_guard<std::mutex> lk(self->msgMutex);
                    self->capturedErrorText = errBuf;
                }
                return IDOK;
            }
            case INSTALLMESSAGE_PROGRESS: {
                if (::MsiRecordGetFieldCount(hRecord) == 0) return IDOK;
                const int subType = ::MsiRecordGetInteger(hRecord, 1);
                const int f2      = ::MsiRecordGetInteger(hRecord, 2);
                const int f3      = ::MsiRecordGetInteger(hRecord, 3);
                if (subType == 0) {
                    self->msiTotalTicks = std::max<long long>(1, f2);
                    self->msiForward    = (f3 == 0);
                    self->msiCurTicks   = self->msiForward ? 0 : self->msiTotalTicks;
                } else if (subType == 1) {
                    if (f3 != 0) self->msiStepTicks = std::max<int>(1, f2);
                } else if (subType == 2) {
                    const long long delta = (f2 != MSI_NULL_INTEGER && f2 > 0) ? f2 : self->msiStepTicks;
                    if (self->msiForward) self->msiCurTicks += delta;
                    else self->msiCurTicks = std::max<long long>(0, self->msiCurTicks - delta);
                } else if (subType == 3) {
                    self->msiTotalTicks += std::max<int>(0, f2);
                }
                const int permille = static_cast<int>(
                    std::clamp<long long>((self->msiCurTicks * 1000LL) / std::max<long long>(1, self->msiTotalTicks), 0, 1000));
                ::PostMessageW(self->hWnd, WM_RSW_PROGRESS_UPDATE, static_cast<WPARAM>(permille), 0);
                return IDOK;
            }
            default:
                break;
        }
        return 0;
    }

    void SyncEditableFieldsFromControls() {
        wchar_t buf[MAX_PATH * 4]{};
        if (::GetWindowTextW(hDirEdit, buf, static_cast<int>(std::size(buf))) > 0) {
            installDir = buf;
        }
        if (::GetWindowTextW(hSmEdit, buf, static_cast<int>(std::size(buf))) > 0) {
            startMenuGroup = buf;
        }
    }

    struct RetryDownloadPromptCtx {
        std::wstring runtimeName;
        std::wstring url;
        std::wstring errorDetail;
        bool         retryChosen = false;
    };

    struct UacLaunchPromptCtx {
        std::wstring jobFilePath;
        HANDLE       hChildProcess  = nullptr;
        bool         canceledByUser = false;
        DWORD        win32Error     = 0;
    };

    WizardPage pageBeforeInstall = WizardPage::ReadyToInstall;
    static constexpr UINT kSentinelUacDeclined = 0xFFFFFFFEu;

    void StartMsiWorkerTransaction() {
        SyncEditableFieldsFromControls();
        pageBeforeInstall = currentPage;

        std::error_code ec;
        if (tempWorkDir.empty()) {
            tempWorkDir = std::filesystem::temp_directory_path(ec) /
                          (L"setupwizard-" + std::to_wstring(::GetCurrentProcessId()));
        }
        std::filesystem::create_directories(tempWorkDir, ec);
        tempMsiPath = tempWorkDir / L"Product.msi";
        msiLogPath  = std::filesystem::temp_directory_path(ec) /
                      (L"Setup-" + std::to_wstring(::GetCurrentProcessId()) + L".log");

        std::wstring extractErr;
        if (selectedAction != SelectedAction::MaintUninstall &&
            selectedAction != SelectedAction::MaintQuickRepair &&
            !EnsureTempMsiExtracted(extractErr)) {
            capturedErrorText = extractErr;
            finalMsiResult    = ERROR_INSTALL_PACKAGE_OPEN_FAILED;
            OnTransactionCompleted();
            return;
        }

        const bool wantDesktopSc = (::SendMessageW(hDeskScChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
        const bool wantFileAssoc = (::SendMessageW(hFileAssocChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
        const bool wipeCache     = (::SendMessageW(hUninstCache, BM_GETCHECK, 0, 0) == BST_CHECKED);
        const bool wipePrefs     = (::SendMessageW(hUninstPrefs, BM_GETCHECK, 0, 0) == BST_CHECKED);

        // Build clean MSI property string with trailing-backslash-safe quoting + `SelectionTree` lists!
        std::wstring msiProps = L"INSTALLDIR=\"" + SanitizeMsiDirValue(installDir) + L"\"";
        if (!startMenuGroup.empty()) {
            msiProps += L" STARTMENUFOLDER=\"" + SanitizeMsiDirValue(startMenuGroup) + L"\"";
        }
        msiProps += wantDesktopSc ? L" DESKTOP_SHORTCUT=1" : L" DESKTOP_SHORTCUT=0";
        msiProps += wantFileAssoc ? L" REGISTER_ASSOC=1"   : L" REGISTER_ASSOC=0";
        if (selectedAction == SelectedAction::InstallCompact) {
            // Minimal / Compact Setup installs all features with `Inclusion = minimal` (`Level <= 20`)
            // while skipping `typical` (`Level = 50`) and `complete` (`Level = 150`) extras!
            msiProps += L" INSTALLLEVEL=20";
        } else if ((selectedAction == SelectedAction::InstallCustom ||
                    selectedAction == SelectedAction::MaintModify) &&
                   !featureNodes.empty()) {
            msiProps += rsw::BuildMsiFeatureSelectionProps(featureNodes);
        }

        // Re-evaluate pending `[Prerequisite.*]` runtimes right before execution
        // (Prerequisites run on Install / Modify / Full Reinstall, never on Uninstall!)
        if (selectedAction != SelectedAction::MaintUninstall &&
            selectedAction != SelectedAction::MaintQuickRepair) {
            RefreshPendingPrerequisites();
        } else {
            pendingPrereqIndices.clear();
        }

        currentPermille     = 0;
        lastAnnouncedDecile = -1;
        capturedErrorText.clear();
        ::SendMessageW(hProgBar, PBM_SETPOS, 0, 0);

        currentPage = WizardPage::Installing;
        UpdateControlVisibilityForPage();
        ::InvalidateRect(hWnd, nullptr, TRUE);

        cancelRequested.store(false);
        installRunning.store(true);

        if (workerThread.joinable()) workerThread.join();

        const auto pendingIndicesCopy = pendingPrereqIndices;
        workerThread = std::thread([this, msiProps, wipeCache, wipePrefs, pendingIndicesCopy]() {
            auto postStatus = [&](const std::wstring& s) {
                std::lock_guard<std::mutex> lk(msgMutex);
                latestStatusText = s;
                ::PostMessageW(hWnd, WM_RSW_STATUS_TEXT, 0, 0);
            };

            std::error_code thEc;
            wchar_t selfExeBuf[MAX_PATH * 4]{};
            ::GetModuleFileNameW(nullptr, selfExeBuf, static_cast<DWORD>(std::size(selfExeBuf)));
            const std::filesystem::path selfExeDir = std::filesystem::path(selfExeBuf).parent_path();

            // -----------------------------------------------------------------
            // STEP 1: Stage all needed `[Prerequisite.*]` runtimes (`download`,
            //         `embedded` LZMS, or `side-by-side` `Redist\`) BEFORE UAC!
            // -----------------------------------------------------------------
            struct StagedPrereqItem {
                rsw::RuntimePrerequisiteSpec spec;
                std::filesystem::path        localInstallerPath;
            };
            std::vector<StagedPrereqItem> stagedPrereqs;
            bool anyPrereqNeedsElevation = false;

            const std::filesystem::path prereqDir = tempWorkDir / L"prereqs";
            if (!pendingIndicesCopy.empty()) {
                std::filesystem::create_directories(prereqDir, thEc);
            }

            for (std::size_t k = 0; k < pendingIndicesCopy.size(); ++k) {
                if (cancelRequested.load()) {
                    finalMsiResult = ERROR_INSTALL_USEREXIT;
                    installRunning.store(false);
                    ::PostMessageW(hWnd, WM_RSW_INSTALL_DONE, static_cast<WPARAM>(finalMsiResult), 0);
                    return;
                }

                const auto& pr = prereqSpecs[pendingIndicesCopy[k]];
                const std::wstring wName = rsw::Utf8ToWide(pr.displayName);
                const std::filesystem::path targetBin = prereqDir / rsw::Utf8ToWide(pr.fileName);
                bool acquired = false;

                if (pr.distribution == "embedded" && pr.embeddedResId > 0) {
                    postStatus(L"Extracting prerequisite: " + wName + L"...");
                    std::wstring extErr;
                    acquired = ExtractEmbeddedResourceToFile(pr.embeddedResId, targetBin, extErr);
                    if (!acquired && !pr.optional) {
                        std::lock_guard<std::mutex> lk(msgMutex);
                        capturedErrorText = extErr;
                    }
                } else {
                    if (pr.distribution == "side-by-side") {
                        const auto sideFile = selfExeDir / L"Redist" / rsw::Utf8ToWide(pr.fileName);
                        if (std::filesystem::exists(sideFile, thEc)) {
                            std::filesystem::copy_file(sideFile, targetBin, std::filesystem::copy_options::overwrite_existing, thEc);
                            acquired = std::filesystem::exists(targetBin, thEc);
                        }
                    }

                    // Stream via `WinHTTP` for `download` mode (or `side-by-side` HTTPS fallback!)
                    while (!acquired && !pr.downloadUrl.empty()) {
                        postStatus(L"Downloading " + wName + L"...");
                        std::string dlErr;
                        acquired = rsw::DownloadFileViaWinHttp(
                            pr.downloadUrl,
                            targetBin,
                            &cancelRequested,
                            [&](std::uint64_t doneBytes, std::uint64_t totalBytes) {
                                std::wstring progMsg = L"Downloading " + wName + L" (" + FormatByteSizeFriendly(doneBytes);
                                if (totalBytes > 0) {
                                    progMsg += L" / " + FormatByteSizeFriendly(totalBytes);
                                    const int dlPerm = static_cast<int>(std::clamp<std::uint64_t>((doneBytes * 250ULL) / totalBytes, 0ULL, 250ULL));
                                    ::PostMessageW(hWnd, WM_RSW_PROGRESS_UPDATE, static_cast<WPARAM>(dlPerm), 0);
                                }
                                progMsg += L")...";
                                postStatus(progMsg);
                            },
                            dlErr);

                        if (acquired && !pr.sha256Hex.empty()) {
                            std::string actualHex;
                            if (!rsw::ComputeFileSha256Hex(targetBin, actualHex) ||
                                !rsw::EqualsIgnoreCase(actualHex, pr.sha256Hex)) {
                                acquired = false;
                                dlErr = "SHA-256 integrity mismatch for downloaded prerequisite.";
                                std::filesystem::remove(targetBin, thEc);
                            }
                        }

                        if (!acquired) {
                            if (cancelRequested.load()) {
                                finalMsiResult = ERROR_INSTALL_USEREXIT;
                                installRunning.store(false);
                                ::PostMessageW(hWnd, WM_RSW_INSTALL_DONE, static_cast<WPARAM>(finalMsiResult), 0);
                                return;
                            }
                            RetryDownloadPromptCtx prompt{wName, rsw::Utf8ToWide(pr.downloadUrl), rsw::Utf8ToWide(dlErr), false};
                            ::SendMessageW(hWnd, WM_RSW_ASK_RETRY_DL, reinterpret_cast<WPARAM>(&prompt), 0);
                            if (!prompt.retryChosen) {
                                break;
                            }
                        }
                    }
                }

                if (!acquired) {
                    if (pr.optional) continue;
                    {
                        std::lock_guard<std::mutex> lk(msgMutex);
                        if (capturedErrorText.empty()) {
                            capturedErrorText = L"Required prerequisite '" + wName + L"' could not be acquired.";
                        }
                    }
                    finalMsiResult = 9010u;
                    installRunning.store(false);
                    ::PostMessageW(hWnd, WM_RSW_INSTALL_DONE, static_cast<WPARAM>(finalMsiResult), 0);
                    return;
                }

                if (pr.requiresElevation) anyPrereqNeedsElevation = true;
                stagedPrereqs.push_back({pr, targetBin});
            }

            // -----------------------------------------------------------------
            // STEP 2: Unified Single-UAC Elevation Broker (`Prerequisites -> MSI`)
            // -----------------------------------------------------------------
            const bool needsElevation = (cfg.perMachine || anyPrereqNeedsElevation) && !IsCurrentProcessElevated();
            const auto statusFile = tempWorkDir / L"msi-status.txt";
            const auto cancelFile = tempWorkDir / L"msi-cancel.flag";
            std::filesystem::remove(statusFile, thEc);
            std::filesystem::remove(cancelFile, thEc);

            if (needsElevation) {
                const auto jobFile = tempWorkDir / L"msi-elevated-job.ini";
                std::string modeStr = "install";
                if (selectedAction == SelectedAction::MaintUninstall)          modeStr = "uninstall";
                else if (selectedAction == SelectedAction::MaintQuickRepair)   modeStr = "quickrepair";
                else if (selectedAction == SelectedAction::MaintFullReinstall) modeStr = "fullreinstall";

                std::wstring unProps;
                if (wipeCache) unProps += L"REMOVE_CACHE=1 ";
                if (wipePrefs) unProps += L"REMOVE_PREFS=1 ";

                {
                    std::ofstream jf(jobFile, std::ios::binary | std::ios::trunc);
                    jf << "Mode="        << modeStr << "\n"
                       << "MsiPath="     << rsw::WideToUtf8(tempMsiPath.wstring()) << "\n"
                       << "ProductCode=" << rsw::WideToUtf8(installedProductCode) << "\n"
                       << "LogPath="     << rsw::WideToUtf8(msiLogPath.wstring()) << "\n"
                       << "Props="       << rsw::WideToUtf8(selectedAction == SelectedAction::MaintUninstall ? unProps : msiProps) << "\n"
                       << "StatusFile="  << rsw::WideToUtf8(statusFile.wstring()) << "\n"
                       << "CancelFile="  << rsw::WideToUtf8(cancelFile.wstring()) << "\n"
                       << "PrereqCount=" << stagedPrereqs.size() << "\n";
                    for (std::size_t j = 0; j < stagedPrereqs.size(); ++j) {
                        const auto& sp = stagedPrereqs[j];
                        const std::string pfx = "Prereq_" + std::to_string(j) + "_";
                        jf << pfx << "Name="         << sp.spec.displayName << "\n"
                           << pfx << "Path="         << rsw::WideToUtf8(sp.localInstallerPath.wstring()) << "\n"
                           << pfx << "Args="         << sp.spec.silentArgs << "\n"
                           << pfx << "SuccessCodes=" << rsw::FormatExitCodeList(sp.spec.successCodes) << "\n"
                           << pfx << "RebootCodes="  << rsw::FormatExitCodeList(sp.spec.rebootCodes) << "\n"
                           << pfx << "Optional="     << (sp.spec.optional ? "true" : "false") << "\n";
                    }
                }

                UacLaunchPromptCtx uacCtx{jobFile.wstring(), nullptr, false, 0};
                ::SendMessageW(hWnd, WM_RSW_REQUEST_UAC, reinterpret_cast<WPARAM>(&uacCtx), 0);
                if (uacCtx.canceledByUser) {
                    installRunning.store(false);
                    ::PostMessageW(hWnd, WM_RSW_INSTALL_DONE, static_cast<WPARAM>(kSentinelUacDeclined), 0);
                    return;
                }
                if (!uacCtx.hChildProcess) {
                    {
                        std::lock_guard<std::mutex> lk(msgMutex);
                        capturedErrorText = L"Could not start elevated Windows Installer helper (Win32 error " +
                                            std::to_wstring(uacCtx.win32Error) + L").";
                    }
                    finalMsiResult = ERROR_INSTALL_FAILURE;
                    installRunning.store(false);
                    ::PostMessageW(hWnd, WM_RSW_INSTALL_DONE, static_cast<WPARAM>(finalMsiResult), 0);
                    return;
                }

                HANDLE hElevatedChild = uacCtx.hChildProcess;
                auto syncStatusFromDisk = [&]() {
                    std::ifstream in(statusFile, std::ios::binary);
                    if (!in) return;
                    std::string linePerm, lineStat, lineErr;
                    std::getline(in, linePerm);
                    std::getline(in, lineStat);
                    std::getline(in, lineErr);
                    if (!linePerm.empty() && linePerm.back() == '\r') linePerm.pop_back();
                    if (!lineStat.empty() && lineStat.back() == '\r') lineStat.pop_back();
                    if (!lineErr.empty()  && lineErr.back()  == '\r') lineErr.pop_back();

                    if (!linePerm.empty()) {
                        const int perm = std::clamp(std::atoi(linePerm.c_str()), 0, 1000);
                        ::PostMessageW(hWnd, WM_RSW_PROGRESS_UPDATE, static_cast<WPARAM>(perm), 0);
                    }
                    if (!lineStat.empty() || !lineErr.empty()) {
                        std::lock_guard<std::mutex> lk(msgMutex);
                        if (!lineStat.empty()) {
                            latestStatusText = rsw::Utf8ToWide(lineStat);
                            ::PostMessageW(hWnd, WM_RSW_STATUS_TEXT, 0, 0);
                        }
                        if (!lineErr.empty()) {
                            capturedErrorText = rsw::Utf8ToWide(lineErr);
                        }
                    }
                };

                while (::WaitForSingleObject(hElevatedChild, 45) == WAIT_TIMEOUT) {
                    if (cancelRequested.load()) {
                        std::ofstream cf(cancelFile, std::ios::binary);
                        cf << "cancel\n";
                    }
                    syncStatusFromDisk();
                }
                syncStatusFromDisk();

                DWORD exitCode = ERROR_INSTALL_FAILURE;
                ::GetExitCodeProcess(hElevatedChild, &exitCode);
                ::CloseHandle(hElevatedChild);

                finalMsiResult = static_cast<UINT>(exitCode);
                installRunning.store(false);
                ::PostMessageW(hWnd, WM_RSW_INSTALL_DONE, static_cast<WPARAM>(finalMsiResult), 0);
                return;
            }

            // -----------------------------------------------------------------
            // STEP 3: Direct In-Process Execution (Already Elevated or Per-User)
            //         Runs all staged `[Prerequisite.*]` installers BEFORE MSI!
            // -----------------------------------------------------------------
            bool anyPrereqRequestedReboot = false;
            for (std::size_t j = 0; j < stagedPrereqs.size(); ++j) {
                if (cancelRequested.load()) {
                    finalMsiResult = ERROR_INSTALL_USEREXIT;
                    installRunning.store(false);
                    ::PostMessageW(hWnd, WM_RSW_INSTALL_DONE, static_cast<WPARAM>(finalMsiResult), 0);
                    return;
                }
                const auto& sp = stagedPrereqs[j];
                postStatus(L"Installing prerequisite: " + rsw::Utf8ToWide(sp.spec.displayName) + L"...");
                std::uint32_t rawCode = 0;
                std::string runErr;
                const auto outcome = rsw::ExecuteStagedPrerequisiteInstaller(sp.spec, sp.localInstallerPath, rawCode, runErr);
                if (outcome == rsw::PrereqExitOutcome::SucceededRebootRequested) {
                    anyPrereqRequestedReboot = true;
                } else if (outcome == rsw::PrereqExitOutcome::Failed && !sp.spec.optional) {
                    {
                        std::lock_guard<std::mutex> lk(msgMutex);
                        capturedErrorText = rsw::Utf8ToWide(runErr);
                    }
                    finalMsiResult = rawCode ? rawCode : ERROR_INSTALL_FAILURE;
                    installRunning.store(false);
                    ::PostMessageW(hWnd, WM_RSW_INSTALL_DONE, static_cast<WPARAM>(finalMsiResult), 0);
                    return;
                }
            }

            ::MsiSetInternalUI(INSTALLUILEVEL_NONE, nullptr);
            constexpr DWORD kFilter =
                INSTALLLOGMODE_FATALEXIT | INSTALLLOGMODE_ERROR | INSTALLLOGMODE_WARNING |
                INSTALLLOGMODE_ACTIONSTART | INSTALLLOGMODE_ACTIONDATA |
                INSTALLLOGMODE_COMMONDATA | INSTALLLOGMODE_PROGRESS;

            INSTALLUI_HANDLER_RECORD prevHandler = nullptr;
            ::MsiSetExternalUIRecord(&SetupWizardApp::OnMsiExternalRecord, kFilter, this, &prevHandler);
            ::MsiEnableLogW(INSTALLLOGMODE_VERBOSE, msiLogPath.wstring().c_str(), INSTALLLOGATTRIBUTES_FLUSHEACHLINE);

            UINT rc = ERROR_SUCCESS;
            if (selectedAction == SelectedAction::MaintUninstall && !installedProductCode.empty()) {
                std::wstring unProps;
                if (wipeCache) unProps += L"REMOVE_CACHE=1 ";
                if (wipePrefs) unProps += L"REMOVE_PREFS=1 ";
                rc = ::MsiConfigureProductExW(installedProductCode.c_str(), INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT, unProps.c_str());
            } else if (selectedAction == SelectedAction::MaintQuickRepair && !installedProductCode.empty()) {
                rc = ::MsiReinstallProductW(
                    installedProductCode.c_str(),
                    REINSTALLMODE_FILEMISSING | REINSTALLMODE_FILEOLDERVERSION | REINSTALLMODE_FILEVERIFY |
                    REINSTALLMODE_MACHINEDATA | REINSTALLMODE_USERDATA | REINSTALLMODE_SHORTCUT);
            } else if (selectedAction == SelectedAction::MaintFullReinstall && !installedProductCode.empty()) {
                std::wstring err;
                if (EnsureTempMsiExtracted(err)) {
                    rc = ::MsiInstallProductW(tempMsiPath.wstring().c_str(), L"REINSTALL=ALL REINSTALLMODE=vomus");
                } else {
                    rc = ::MsiReinstallProductW(
                        installedProductCode.c_str(),
                        REINSTALLMODE_FILEREPLACE | REINSTALLMODE_SHORTCUT | REINSTALLMODE_MACHINEDATA | REINSTALLMODE_USERDATA);
                }
            } else {
                rc = ::MsiInstallProductW(tempMsiPath.wstring().c_str(), msiProps.c_str());
            }

            ::MsiSetExternalUIRecord(prevHandler, 0, nullptr, nullptr);
            if (rc == ERROR_SUCCESS && anyPrereqRequestedReboot) {
                rc = ERROR_SUCCESS_REBOOT_REQUIRED; // Latch prerequisite reboot request!
            }
            finalMsiResult = rc;
            installRunning.store(false);
            ::PostMessageW(hWnd, WM_RSW_INSTALL_DONE, static_cast<WPARAM>(rc), 0);
        });
    }

    void OnTransactionCompleted() {
        currentPermille = (finalMsiResult == ERROR_SUCCESS || finalMsiResult == ERROR_SUCCESS_REBOOT_REQUIRED) ? 1000 : currentPermille;
        currentPage     = WizardPage::Finished;
        UpdateControlVisibilityForPage();
        ::SetFocus(hBtnPrimary);
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    // ----------------------------------------------------------------------------
    // MULTI-STEP WIZARD FORWARD & BACKWARD NAVIGATION
    // ----------------------------------------------------------------------------
    void NavigateTo(WizardPage target, HWND initialFocus = nullptr) {
        SyncEditableFieldsFromControls();
        currentPage = target;
        UpdateControlVisibilityForPage();
        if (initialFocus && ::IsWindowVisible(initialFocus) && ::IsWindowEnabled(initialFocus)) {
            ::SetFocus(initialFocus);
        }
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    void OnPrimaryButtonClicked() {
        switch (currentPage) {
            case WizardPage::Welcome:
                if (cfg.isAutorun16x9) {
                    if (autorunChoiceIdx == 0) {
                        if (alreadyInstalled) {
                            selectedAction = SelectedAction::MaintQuickRepair;
                            NavigateTo(WizardPage::RepairMode, hRepairQuick);
                        } else if (cfg.hasLicenseRtf) {
                            selectedAction = SelectedAction::InstallExpress;
                            NavigateTo(WizardPage::License, hLicAccept);
                        } else {
                            selectedAction = SelectedAction::InstallExpress;
                            StartMsiWorkerTransaction();
                        }
                    } else if (autorunChoiceIdx == 1) {
                        NavigateTo(WizardPage::AutorunReadMe, hReadMeEdit);
                    } else if (autorunChoiceIdx == 2) {
                        if (alreadyInstalled) {
                            selectedAction = SelectedAction::MaintUninstall;
                            NavigateTo(WizardPage::SafeUninstall, hUninstCache);
                        } else {
                            selectedAction = SelectedAction::InstallCustom;
                            NavigateTo(WizardPage::CustomFolder, hDirEdit);
                        }
                    } else {
                        ::DestroyWindow(hWnd);
                    }
                } else {
                    if (cfg.hasLicenseRtf) {
                        NavigateTo(WizardPage::License, hLicAccept);
                    } else {
                        NavigateTo(WizardPage::SetupType, hModeExpress);
                    }
                }
                break;

            case WizardPage::AutorunReadMe:
                if (alreadyInstalled) {
                    NavigateTo(WizardPage::Welcome, hArCell[0]);
                } else if (cfg.hasLicenseRtf) {
                    NavigateTo(WizardPage::License, hLicAccept);
                } else {
                    selectedAction = SelectedAction::InstallExpress;
                    StartMsiWorkerTransaction();
                }
                break;

            case WizardPage::License:
                if (cfg.isAutorun16x9) {
                    StartMsiWorkerTransaction();
                } else {
                    NavigateTo(WizardPage::SetupType, hModeExpress);
                }
                break;

            case WizardPage::SetupType:
                if (selectedAction == SelectedAction::InstallCustom) {
                    EnsureFeatureTreeLoaded();
                    NavigateTo(WizardPage::CustomFeatures, hFeatTree);
                } else {
                    RefreshPendingPrerequisites();
                    NavigateTo(WizardPage::ReadyToInstall, hBtnPrimary);
                }
                break;

            case WizardPage::CustomFeatures:
                if (alreadyInstalled || selectedAction == SelectedAction::MaintModify) {
                    NavigateTo(WizardPage::CustomShortcuts, hSmEdit);
                } else {
                    NavigateTo(WizardPage::CustomFolder, hDirEdit);
                }
                break;

            case WizardPage::CustomFolder:
                if (cfg.isAutorun16x9) {
                    if (cfg.hasLicenseRtf && ::SendMessageW(hLicAccept, BM_GETCHECK, 0, 0) != BST_CHECKED) {
                        NavigateTo(WizardPage::License, hLicAccept);
                    } else {
                        StartMsiWorkerTransaction();
                    }
                } else {
                    NavigateTo(WizardPage::CustomShortcuts, hSmEdit);
                }
                break;

            case WizardPage::CustomShortcuts:
                RefreshPendingPrerequisites();
                NavigateTo(WizardPage::ReadyToInstall, hBtnPrimary);
                break;

            case WizardPage::ReadyToInstall:
            case WizardPage::RepairMode:
            case WizardPage::SafeUninstall:
                StartMsiWorkerTransaction();
                break;

            case WizardPage::Maintenance:
                if (::SendMessageW(hMaintModify, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    selectedAction = SelectedAction::MaintModify;
                    EnsureFeatureTreeLoaded();
                    NavigateTo(WizardPage::CustomFeatures, hFeatTree);
                } else if (::SendMessageW(hMaintRemove, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    selectedAction = SelectedAction::MaintUninstall;
                    NavigateTo(WizardPage::SafeUninstall, hUninstCache);
                } else {
                    selectedAction = SelectedAction::MaintQuickRepair;
                    NavigateTo(WizardPage::RepairMode, hRepairQuick);
                }
                break;

            case WizardPage::Finished:
                if ((finalMsiResult == ERROR_SUCCESS || finalMsiResult == ERROR_SUCCESS_REBOOT_REQUIRED) &&
                    selectedAction != SelectedAction::MaintUninstall &&
                    cfg.offerLaunch && !cfg.mainExecutable.empty() &&
                    ::SendMessageW(hFinLaunch, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    const std::filesystem::path exePath = std::filesystem::path(installDir) / cfg.mainExecutable;
                    // Safely launched from our `asInvoker` process so user apps never run elevated!
                    ::ShellExecuteW(nullptr, L"open", exePath.wstring().c_str(), nullptr, installDir.c_str(), SW_SHOWNORMAL);
                }
                ::DestroyWindow(hWnd);
                break;

            default:
                break;
        }
    }

    void OnBackButtonClicked() {
        switch (currentPage) {
            case WizardPage::AutorunReadMe:
                NavigateTo(WizardPage::Welcome, hArCell[1]);
                break;
            case WizardPage::License:
                NavigateTo(WizardPage::Welcome, cfg.isAutorun16x9 ? hArCell[0] : hBtnPrimary);
                break;
            case WizardPage::SetupType:
                NavigateTo(cfg.hasLicenseRtf ? WizardPage::License : WizardPage::Welcome);
                break;
            case WizardPage::CustomFeatures:
                NavigateTo(alreadyInstalled ? WizardPage::Maintenance : WizardPage::SetupType);
                break;
            case WizardPage::CustomFolder:
                NavigateTo(cfg.isAutorun16x9 ? WizardPage::Welcome : WizardPage::CustomFeatures);
                break;
            case WizardPage::CustomShortcuts:
                NavigateTo(alreadyInstalled ? WizardPage::CustomFeatures : WizardPage::CustomFolder);
                break;
            case WizardPage::ReadyToInstall:
                NavigateTo((selectedAction == SelectedAction::InstallCustom || selectedAction == SelectedAction::MaintModify)
                           ? WizardPage::CustomShortcuts
                           : WizardPage::SetupType);
                break;
            case WizardPage::RepairMode:
            case WizardPage::SafeUninstall:
                NavigateTo(cfg.isAutorun16x9 ? WizardPage::Welcome : WizardPage::Maintenance);
                break;
            default:
                break;
        }
    }

    // ----------------------------------------------------------------------------
    // STEP METADATA & DOUBLE-BUFFERED GDI+ GLASS SURFACE RENDERING
    // ----------------------------------------------------------------------------
    struct StepHeaderInfo {
        const wchar_t*      kickerSuffix;
        const wchar_t*      aeroInstruction;
        rsw::ui::GlassGlyph glyph;
        COLORREF            badgeColor;
    };

    StepHeaderInfo CurrentStepHeaderInfo() const noexcept {
        switch (currentPage) {
            case WizardPage::Welcome:         return {L"OFFICIAL SETUP WIZARD",    L"Welcome to Setup",                 rsw::ui::GlassGlyph::Sparkle,     rsw::ui::brand::kRaggieBlue};
            case WizardPage::License:         return {L"LICENSE AGREEMENT",        L"Review Software License Agreement",rsw::ui::GlassGlyph::ShieldCheck, rsw::ui::brand::kEmeraldOk};
            case WizardPage::AutorunReadMe:   return {L"EDITION LINER NOTES",      L"Edition Liner Notes",              rsw::ui::GlassGlyph::ReadMeBook,  rsw::ui::brand::kSkyCyan};
            case WizardPage::SetupType:       return {L"CHOOSE SETUP MODE",        L"Choose Setup Mode",                rsw::ui::GlassGlyph::Sliders,     rsw::ui::brand::kVioletTune};
            case WizardPage::CustomFeatures:  return {L"SELECT FEATURES",          L"Select Program Features",          rsw::ui::GlassGlyph::Blocks,      rsw::ui::brand::kVioletTune};
            case WizardPage::CustomFolder:    return {L"DESTINATION FOLDER",       L"Select Destination Folder",        rsw::ui::GlassGlyph::Folder,      rsw::ui::brand::kRaggieBlue};
            case WizardPage::CustomShortcuts: return {L"SHORTCUTS & INTEGRATION",  L"Shortcuts & Explorer Integration", rsw::ui::GlassGlyph::PinGrid,     rsw::ui::brand::kSkyCyan};
            case WizardPage::ReadyToInstall:  return {L"READY TO APPLY CHANGES",   L"Ready to Install",                 rsw::ui::GlassGlyph::Play,        rsw::ui::brand::kEmeraldOk};
            case WizardPage::Maintenance:     return {L"MAINTENANCE WIZARD",       L"Modify, Repair, or Uninstall",     rsw::ui::GlassGlyph::Gear,        rsw::ui::brand::kVioletTune};
            case WizardPage::RepairMode:      return {L"REPAIR & VERIFY OPTIONS",  L"Repair & Verify Installation",     rsw::ui::GlassGlyph::Reinstall,   rsw::ui::brand::kRaggieBlue};
            case WizardPage::SafeUninstall:   return {L"SAFE UNINSTALL",           L"Safe Uninstall",                   rsw::ui::GlassGlyph::Uninstall,   rsw::ui::brand::kCoralRose};
            case WizardPage::Installing:      return {L"APPLYING SYSTEM CHANGES",  L"Applying System Changes...",       rsw::ui::GlassGlyph::Gear,        rsw::ui::brand::kRaggieBlue};
            case WizardPage::Finished:
                return (finalMsiResult == ERROR_SUCCESS || finalMsiResult == ERROR_SUCCESS_REBOOT_REQUIRED)
                    ? StepHeaderInfo{L"SETUP COMPLETE",    L"Setup Completed Successfully", rsw::ui::GlassGlyph::ShieldCheck, rsw::ui::brand::kEmeraldOk}
                    : StepHeaderInfo{L"SETUP INTERRUPTED", L"Setup Did Not Complete",       rsw::ui::GlassGlyph::Uninstall,   rsw::ui::brand::kCoralRose};
        }
        return {L"OFFICIAL SETUP WIZARD", L"Welcome to Setup", rsw::ui::GlassGlyph::Sparkle, rsw::ui::brand::kRaggieBlue};
    }

    void DrawGlassPanelBox(Gdiplus::Graphics& g, const RECT& rc, int radiusPx) const {
        const Gdiplus::SolidBrush fillBr(rsw::ui::ToGpColor(theme.cardBg));
        const Gdiplus::Pen borderPen(rsw::ui::ToGpColor(theme.borderSubtle), 1.0f);
        rsw::ui::FillAndStrokeRoundRect(
            g,
            static_cast<float>(rc.left), static_cast<float>(rc.top),
            static_cast<float>(rc.right - rc.left), static_cast<float>(rc.bottom - rc.top),
            static_cast<float>(radiusPx), &fillBr, &borderPen);
    }

    void PaintWindow(HDC hdcOut) {
        RECT clientRc{};
        ::GetClientRect(hWnd, &clientRc);
        const int W = std::max<int>(1, clientRc.right - clientRc.left);
        const int H = std::max<int>(1, clientRc.bottom - clientRc.top);
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        HDC memDc      = ::CreateCompatibleDC(hdcOut);
        HBITMAP memBmp = ::CreateCompatibleBitmap(hdcOut, W, H);
        HGDIOBJ oldBmp = ::SelectObject(memDc, memBmp);

        ::FillRect(memDc, &clientRc, theme.hbrWindow);
        ::SetBkMode(memDc, TRANSPARENT);

        Gdiplus::Graphics g(memDc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        const int footerH = S(58);
        const int footerY = H - footerH;
        const int btnsLeftX = ::IsWindowVisible(hBtnBack) ? (W - S(326)) : (W - S(224));

        // 1. Universal Branded Glass Footer Bar (`22x22` Squircle Emblem + `"Publisher  |  website"`)
        rsw::ui::DrawGlassFooterBar(
            memDc, g, W, H, footerH, btnsLeftX, dpi, theme, cfg.manufacturer, cfg.websiteLabel);

        const StepHeaderInfo hdrInfo = CurrentStepHeaderInfo();

        // 2. Top Header Stage (Autorun `16:9` Cover Stage, Autorun Compact Interior Header, or Standard Glass Header!)
        if (cfg.isAutorun16x9 && currentPage == WizardPage::Welcome) {
            RECT stageRc{0, 0, W, S(324)};
            rsw::ui::FillHorizontalGradient(memDc, stageRc, theme.bannerNavyLeft, theme.bannerNavyRight);
            if (hArtworkBmp) {
                rsw::ui::DrawAlphaBitmapFit(memDc, hArtworkBmp, artworkW, artworkH, stageRc);
            } else {
                // Sleek vector fallback stage when an `autorun16x9` project has no external `.png` cover yet
                rsw::ui::DrawBrandSquircleBadge(g, static_cast<float>(S(28)), static_cast<float>(S(34)), static_cast<float>(S(52)), theme, cfg.manufacturer);
                HGDIOBJ oldF = ::SelectObject(memDc, hFontHeadline);
                ::SetTextColor(memDc, theme.bannerTitleText);
                RECT tRc{S(28), S(106), W - S(28), S(156)};
                ::DrawTextW(memDc, cfg.productName.c_str(), -1, &tRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.bannerSubText);
                const std::wstring credit = cfg.authorOrArtist.empty() ? cfg.tagline : (cfg.authorOrArtist + L"  •  " + cfg.tagline);
                RECT sRc{S(28), S(156), W - S(28), S(240)};
                ::DrawTextW(memDc, credit.c_str(), -1, &sRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                ::SelectObject(memDc, oldF);
            }
            rsw::ui::DrawMultiStopAccentBar(g, 0.0f, static_cast<float>(S(324)), static_cast<float>(W), static_cast<float>(S(3)), theme);
            ::BitBlt(hdcOut, 0, 0, W, H, memDc, 0, 0, SRCCOPY);
            ::SelectObject(memDc, oldBmp);
            ::DeleteObject(memBmp);
            ::DeleteDC(memDc);
            return;
        }

        if (cfg.isAutorun16x9) {
            const std::wstring pubUpper = rsw::ui::UpperWide(cfg.manufacturer.empty() ? L"RaggieSoft" : cfg.manufacturer);
            const std::wstring kicker   = pubUpper + L"  |  " + hdrInfo.kickerSuffix;
            rsw::ui::DrawAutorunCompactInteriorHeader(
                memDc, g, W, dpi, theme, cfg.manufacturer, kicker, cfg.productName,
                hArtworkBmp, artworkW, artworkH);
        } else if (cfg.isAeroWizard) {
            const std::wstring winCap = cfg.productName + L" Setup";
            rsw::ui::DrawAeroWizardTopHeader(
                memDc, g, W, dpi, theme, winCap, hdrInfo.aeroInstruction,
                hAppIconBig, hdrInfo.glyph, hdrInfo.badgeColor, cfg.manufacturer);
        } else {
            rsw::ui::DrawGlassTopHeader(
                memDc, g, W, dpi, theme,
                cfg.manufacturer, hdrInfo.kickerSuffix, cfg.productName,
                hAppIconBig, hdrInfo.glyph, hdrInfo.badgeColor);
        }

        // 3. Page-Specific Glass Content Canvas (`y = 104 .. footerY - 10`)
        const int bodyTop = S(104);
        const int cardX   = S(18);
        const int cardW   = W - S(36);
        HGDIOBJ oldF      = ::SelectObject(memDc, hFontBody);

        switch (currentPage) {
            case WizardPage::Welcome: {
                // Hero Brand Card (`"OFFICIAL INSTALLER"`)
                RECT heroRc{cardX, bodyTop, cardX + cardW, bodyTop + S(70)};
                rsw::ui::DrawHeroBrandCard(
                    memDc, g, heroRc, dpi, theme,
                    cfg.manufacturer, cfg.tagline, cfg.websiteLabel,
                    L"OFFICIAL INSTALLER", hAppIconBig);

                int textLeft = S(22);
                if (hArtworkBmp) {
                    RECT artThumb{cardX, bodyTop + S(82), cardX + S(138), bodyTop + S(246)};
                    DrawGlassPanelBox(g, artThumb, S(10));
                    RECT artInner{artThumb.left + S(4), artThumb.top + S(4), artThumb.right - S(4), artThumb.bottom - S(4)};
                    rsw::ui::DrawAlphaBitmapFit(memDc, hArtworkBmp, artworkW, artworkH, artInner);
                    textLeft = cardX + S(152);
                }

                ::SelectObject(memDc, hFontHeadline);
                ::SetTextColor(memDc, theme.textPrimary);
                RECT headRc{textLeft, bodyTop + S(84), W - S(22), bodyTop + S(120)};
                ::DrawTextW(memDc, cfg.welcomeHeadline.c_str(), -1, &headRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                ::SelectObject(memDc, hFontBody);
                ::SetTextColor(memDc, theme.textSecondary);
                const std::wstring introCopy = !cfg.welcomeIntro.empty()
                    ? cfg.welcomeIntro
                    : (L"This wizard installs " + cfg.productName + L" (v" + cfg.version +
                       L") on your computer using native Windows Installer transaction safety.\r\n\r\n"
                       L"Click Next to choose Express Setup for one-click installation, or Custom Setup to choose your destination folder and shortcuts.");
                RECT introRc{textLeft, bodyTop + S(124), W - S(22), bodyTop + S(246)};
                ::DrawTextW(memDc, introCopy.c_str(), -1, &introRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                // Bottom Trust & Fictitious Disclaimer Strip
                RECT tipCard{cardX, footerY - S(62), cardX + cardW, footerY - S(12)};
                DrawGlassPanelBox(g, tipCard, S(8));
                ::SelectObject(memDc, hFontSmall);
                ::SetTextColor(memDc, theme.textSecondary);
                const bool isRaggie = rsw::ui::UpperWide(cfg.manufacturer).find(L"RAGGIESOFT") != std::wstring::npos;
                const std::wstring footNote = isRaggie
                    ? L"Zero Telemetry  •  Full Keyboard & Screen-Reader Support.\r\nAll narrative characters, bands, universities, and corporations in RaggieSoft works are fictitious."
                    : (L"Version " + cfg.version + L"  •  Zero Telemetry  •  Full Keyboard & Screen-Reader Accessibility.");
                RECT fnRc{tipCard.left + S(12), tipCard.top + S(6), tipCard.right - S(12), tipCard.bottom - S(4)};
                ::DrawTextW(memDc, footNote.c_str(), -1, &fnRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            }

            case WizardPage::License: {
                if (!cfg.isAutorun16x9) {
                    ::SelectObject(memDc, hFontBold);
                    ::SetTextColor(memDc, theme.textPrimary);
                    RECT pRc{cardX, bodyTop, cardX + cardW, bodyTop + S(26)};
                    ::DrawTextW(memDc, L"Please review the software license agreement below before continuing:", -1, &pRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                }
                break;
            }

            case WizardPage::SetupType: {
                ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.textPrimary);
                const std::wstring prompt = L"Choose how you would like to install " + cfg.productName + L":";
                RECT pRc{cardX, bodyTop + S(2), cardX + cardW, bodyTop + S(32)};
                ::DrawTextW(memDc, prompt.c_str(), -1, &pRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                break;
            }

            case WizardPage::CustomFeatures: {
                ::SelectObject(memDc, hFontBold);
                ::SetTextColor(memDc, theme.textPrimary);
                RECT pRc{cardX, bodyTop + S(2), cardX + cardW, bodyTop + S(28)};
                ::DrawTextW(memDc, L"Click any feature's drive icon (or press Space) to choose how it is installed:", -1, &pRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                // Right-Hand Glass Card (`Feature Details` + Live Byte Cost!)
                const int treeW  = (cardW * 58) / 100;
                const int detX   = cardX + treeW + S(12);
                RECT detCard{detX, bodyTop + S(32), cardX + cardW, footerY - S(14)};
                DrawGlassPanelBox(g, detCard, S(10));

                if (!featureNodes.empty() && selectedFeatureIdx < featureNodes.size()) {
                    const auto& fn = featureNodes[selectedFeatureIdx];
                    ::SelectObject(memDc, hFontSection);
                    ::SetTextColor(memDc, theme.textPrimary);
                    RECT tRc{detCard.left + S(14), detCard.top + S(12), detCard.right - S(14), detCard.top + S(38)};
                    ::DrawTextW(memDc, fn.title.c_str(), -1, &tRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                    rsw::PaintSingleMsiStateCell(
                        g,
                        static_cast<float>(detCard.left + S(14)),
                        static_cast<float>(detCard.top + S(42)),
                        static_cast<float>(S(30)),
                        static_cast<float>(S(18)),
                        fn.currentState,
                        theme);

                    ::SelectObject(memDc, hFontSmall);
                    const COLORREF stateTextCol = theme.isHighContrast
                        ? theme.textPrimary
                        : (theme.isDarkMode ? RGB(125, 211, 252) : RGB(30, 64, 175));
                    ::SetTextColor(memDc, stateTextCol);
                    const std::wstring stTxt = rsw::DescribeMsiTreeState(fn.currentState);
                    RECT stRc{detCard.left + S(50), detCard.top + S(39), detCard.right - S(12), detCard.top + S(88)};
                    ::DrawTextW(memDc, stTxt.c_str(), -1, &stRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                    ::SelectObject(memDc, hFontBody);
                    ::SetTextColor(memDc, theme.textSecondary);
                    RECT dRc{detCard.left + S(14), detCard.top + S(92), detCard.right - S(14), detCard.bottom - S(72)};
                    ::DrawTextW(memDc, fn.description.c_str(), -1, &dRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                    std::wstring volLbl;
                    std::uint64_t freeB = 0;
                    const bool hasVol = QueryVolumeFreeBytesForPath(installDir, volLbl, freeB);

                    ::SelectObject(memDc, hFontSmall);
                    ::SetTextColor(memDc, theme.textPrimary);
                    std::wstring costSummary =
                        L"Feature size:  " + FormatByteSizeFriendly(fn.ownBytes) +
                        L"\r\nTotal selected:  " + FormatByteSizeFriendly(estimatedReqBytes);
                    if (hasVol) {
                        costSummary += L"\r\nAvailable on drive:  " + FormatByteSizeFriendly(freeB);
                    }
                    RECT cRc{detCard.left + S(14), detCard.bottom - S(66), detCard.right - S(14), detCard.bottom - S(10)};
                    ::DrawTextW(memDc, costSummary.c_str(), -1, &cRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                }
                break;
            }

            case WizardPage::CustomFolder: {
                ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.textPrimary);
                RECT hRc{cardX, bodyTop + S(2), cardX + cardW, bodyTop + S(30)};
                ::DrawTextW(memDc, L"Select Destination Folder", -1, &hRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

                // Live `GetDiskFreeSpaceExW` Glass Capacity Card!
                wchar_t editBuf[MAX_PATH * 4]{};
                std::wstring livePath = installDir;
                if (hDirEdit && ::GetWindowTextW(hDirEdit, editBuf, static_cast<int>(std::size(editBuf))) > 0) {
                    livePath = editBuf;
                }
                std::wstring volLabel;
                std::uint64_t freeBytes = 0;
                const bool hasDisk = QueryVolumeFreeBytesForPath(livePath, volLabel, freeBytes);

                RECT diskCard{cardX, bodyTop + S(114), cardX + cardW, bodyTop + S(214)};
                DrawGlassPanelBox(g, diskCard, S(10));
                rsw::ui::DrawOptionBadge48(
                    g, static_cast<float>(diskCard.left + S(14)), static_cast<float>(diskCard.top + S(26)),
                    static_cast<float>(S(46)), rsw::ui::GlassGlyph::Folder,
                    rsw::ui::brand::kRaggieBlue, rsw::ui::brand::kWarmGold, theme);

                ::SelectObject(memDc, hFontBold);
                ::SetTextColor(memDc, theme.textPrimary);
                const std::wstring dHead = volLabel + L" — Storage Summary";
                RECT dhRc{diskCard.left + S(74), diskCard.top + S(16), diskCard.right - S(16), diskCard.top + S(40)};
                ::DrawTextW(memDc, dHead.c_str(), -1, &dhRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

                ::SelectObject(memDc, hFontBody);
                ::SetTextColor(memDc, theme.textSecondary);
                std::wstring dSub = L"Estimated space required:  " + FormatByteSizeFriendly(estimatedReqBytes);
                if (hasDisk) {
                    dSub += L"\r\nAvailable space on drive:  " + FormatByteSizeFriendly(freeBytes);
                }
                RECT dsRc{diskCard.left + S(74), diskCard.top + S(42), diskCard.right - S(16), diskCard.bottom - S(12)};
                ::DrawTextW(memDc, dSub.c_str(), -1, &dsRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            }

            case WizardPage::CustomShortcuts: {
                ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.textPrimary);
                RECT hRc{cardX, bodyTop + S(2), cardX + cardW, bodyTop + S(32)};
                ::DrawTextW(memDc, L"Configure Start Menu & Shortcuts", -1, &hRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                break;
            }

            case WizardPage::ReadyToInstall: {
                RECT heroRc{cardX, bodyTop, cardX + cardW, bodyTop + S(66)};
                rsw::ui::DrawHeroBrandCard(
                    memDc, g, heroRc, dpi, theme,
                    cfg.manufacturer, cfg.tagline, cfg.websiteLabel,
                    L"READY TO INSTALL", hAppIconBig);

                RECT sumCard{cardX, bodyTop + S(78), cardX + cardW, footerY - S(14)};
                DrawGlassPanelBox(g, sumCard, S(10));

                ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.textPrimary);
                RECT shRc{sumCard.left + S(16), sumCard.top + S(12), sumCard.right - S(16), sumCard.top + S(38)};
                ::DrawTextW(memDc, L"Review Your Setup Summary", -1, &shRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

                const wchar_t* modeName = L"Express Setup (Recommended)";
                if (selectedAction == SelectedAction::InstallCompact) modeName = L"Compact Core Setup";
                if (selectedAction == SelectedAction::InstallCustom)  modeName = L"Custom Setup";
                if (selectedAction == SelectedAction::MaintModify)    modeName = L"Modify Installed Features";

                std::wstring prereqSummary = L"None required";
                if (!prereqSpecs.empty()) {
                    if (pendingPrereqIndices.empty()) {
                        prereqSummary = L"All required runtimes already installed";
                    } else {
                        prereqSummary = std::to_wstring(pendingPrereqIndices.size()) + L" to install (" +
                                        rsw::Utf8ToWide(prereqSpecs[pendingPrereqIndices.front()].displayName) +
                                        (pendingPrereqIndices.size() > 1 ? L", ...)" : L")");
                    }
                }

                const bool deskOn = (::SendMessageW(hDeskScChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
                const int rowCount = prereqSpecs.empty() ? 4 : 5;
                std::wstring rows[5][2] = {
                    {L"Setup Mode:",         modeName},
                    {L"Destination Folder:", installDir},
                    {L"Install Scope:",      cfg.perMachine ? L"All Users (Administrator UAC)" : L"Current User (No UAC Needed)"},
                    {L"Shortcuts:",          L"Start Menu (" + startMenuGroup + L")" + (deskOn ? L" + Desktop" : L"")},
                    {L"Runtimes:",           prereqSummary}
                };

                int ry = sumCard.top + S(44);
                const int rowStep = prereqSpecs.empty() ? S(34) : S(28);
                for (int i = 0; i < rowCount; ++i) {
                    ::SelectObject(memDc, hFontBold);
                    ::SetTextColor(memDc, theme.textSecondary);
                    RECT kRc{sumCard.left + S(16), ry, sumCard.left + S(172), ry + S(26)};
                    ::DrawTextW(memDc, rows[i][0].c_str(), -1, &kRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

                    ::SelectObject(memDc, hFontBody);
                    ::SetTextColor(memDc, theme.textPrimary);
                    RECT vRc{sumCard.left + S(176), ry, sumCard.right - S(16), ry + S(26)};
                    ::DrawTextW(memDc, rows[i][1].c_str(), -1, &vRc, DT_LEFT | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
                    ry += rowStep;
                }
                break;
            }

            case WizardPage::Maintenance: {
                RECT heroRc{cardX, bodyTop, cardX + cardW, bodyTop + S(62)};
                rsw::ui::DrawHeroBrandCard(
                    memDc, g, heroRc, dpi, theme,
                    cfg.manufacturer, cfg.tagline, cfg.websiteLabel,
                    L"MAINTENANCE WIZARD", hAppIconBig);
                break;
            }

            case WizardPage::RepairMode: {
                ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.textPrimary);
                RECT hRc{cardX, bodyTop + S(4), cardX + cardW, bodyTop + S(34)};
                ::DrawTextW(memDc, L"Choose how you would like Setup to repair your installation:", -1, &hRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                break;
            }

            case WizardPage::SafeUninstall: {
                RECT heroRc{cardX, bodyTop, cardX + cardW, bodyTop + S(66)};
                rsw::ui::DrawHeroBrandCard(
                    memDc, g, heroRc, dpi, theme,
                    cfg.manufacturer, cfg.tagline, cfg.websiteLabel,
                    L"SAFE UNINSTALL", hAppIconBig);

                RECT boxRc{cardX, bodyTop + S(78), cardX + cardW, footerY - S(16)};
                DrawGlassPanelBox(g, boxRc, S(10));

                ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.textPrimary);
                const std::wstring uHead = L"Remove " + cfg.productName + L" from Your Computer?";
                RECT uhRc{boxRc.left + S(16), boxRc.top + S(14), boxRc.right - S(16), boxRc.top + S(42)};
                ::DrawTextW(memDc, uHead.c_str(), -1, &uhRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                ::SelectObject(memDc, hFontBody);
                ::SetTextColor(memDc, theme.textSecondary);
                const std::wstring uSub = L"Your personal documents and projects are NEVER deleted. Optional cleanup items below are left unchecked by default for safety:";
                RECT usRc{boxRc.left + S(16), boxRc.top + S(42), boxRc.right - S(16), boxRc.top + S(80)};
                ::DrawTextW(memDc, uSub.c_str(), -1, &usRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            }

            case WizardPage::Installing: {
                RECT heroRc{cardX, bodyTop, cardX + cardW, bodyTop + S(66)};
                rsw::ui::DrawHeroBrandCard(
                    memDc, g, heroRc, dpi, theme,
                    cfg.manufacturer, cfg.tagline, cfg.websiteLabel,
                    L"CONFIGURING SYSTEM", hAppIconBig);

                RECT progCard{cardX, bodyTop + S(82), cardX + cardW, bodyTop + S(218)};
                DrawGlassPanelBox(g, progCard, S(10));

                ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.textPrimary);
                std::wstring phaseTitle = L"Installing " + cfg.productName + L"...";
                if (selectedAction == SelectedAction::MaintUninstall)     phaseTitle = L"Removing " + cfg.productName + L"...";
                if (selectedAction == SelectedAction::MaintQuickRepair ||
                    selectedAction == SelectedAction::MaintFullReinstall) phaseTitle = L"Repairing " + cfg.productName + L"...";

                RECT ptRc{progCard.left + S(16), progCard.top + S(16), progCard.right - S(84), progCard.top + S(46)};
                ::DrawTextW(memDc, phaseTitle.c_str(), -1, &ptRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                const int pct = std::clamp(currentPermille / 10, 0, 100);
                const std::wstring pctStr = std::to_wstring(pct) + L"%";
                RECT pctRc{progCard.right - S(80), progCard.top + S(16), progCard.right - S(16), progCard.top + S(46)};
                ::DrawTextW(memDc, pctStr.c_str(), -1, &pctRc, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
                break;
            }

            case WizardPage::Finished: {
                const bool okDone = (finalMsiResult == ERROR_SUCCESS || finalMsiResult == ERROR_SUCCESS_REBOOT_REQUIRED);
                const wchar_t* pill = okDone
                    ? ((selectedAction == SelectedAction::MaintUninstall) ? L"REMOVAL COMPLETE" : L"READY TO LAUNCH")
                    : L"SETUP INTERRUPTED";

                RECT heroRc{cardX, bodyTop, cardX + cardW, bodyTop + S(66)};
                rsw::ui::DrawHeroBrandCard(
                    memDc, g, heroRc, dpi, theme,
                    cfg.manufacturer, cfg.tagline, cfg.websiteLabel,
                    pill, hAppIconBig);

                RECT finCard{cardX, bodyTop + S(80), cardX + cardW, footerY - S(64)};
                DrawGlassPanelBox(g, finCard, S(10));

                ::SelectObject(memDc, hFontHeadline);
                ::SetTextColor(memDc, theme.textPrimary);
                std::wstring finHead = okDone ? (cfg.productName + L" Is Ready!") : L"Setup Did Not Complete";
                if (okDone && selectedAction == SelectedAction::MaintUninstall) {
                    finHead = cfg.productName + L" Was Removed";
                }
                RECT fhRc{finCard.left + S(18), finCard.top + S(16), finCard.right - S(18), finCard.top + S(52)};
                ::DrawTextW(memDc, finHead.c_str(), -1, &fhRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                ::SelectObject(memDc, hFontBody);
                ::SetTextColor(memDc, theme.textSecondary);
                std::wstring finBody;
                if (okDone) {
                    if (selectedAction == SelectedAction::MaintUninstall) {
                        finBody = cfg.productName + L" program files were cleanly uninstalled from your computer while preserving your personal work.\r\n\r\nClick Finish to close Setup.";
                    } else if (selectedAction == SelectedAction::MaintQuickRepair || selectedAction == SelectedAction::MaintFullReinstall) {
                        finBody = L"All application files, shortcuts, and settings for " + cfg.productName + L" have been verified and restored.\r\n\r\nClick Finish to close Setup.";
                    } else {
                        finBody = cfg.productName + L" (v" + cfg.version + L") has been installed to:\r\n" + installDir +
                                  ((finalMsiResult == ERROR_SUCCESS_REBOOT_REQUIRED) ? L"\r\n\r\nNote: Restart Windows when convenient to finalize system updates." : L"");
                    }
                } else if (finalMsiResult == ERROR_INSTALL_USEREXIT) {
                    finBody = L"Setup was canceled at your request. Windows Installer automatically rolled back any partial changes.";
                } else {
                    finBody = L"Windows Installer returned status code " + std::to_wstring(finalMsiResult) + L".\r\n" +
                              (capturedErrorText.empty() ? L"Click View Setup Log to inspect diagnostic details." : capturedErrorText);
                }
                RECT fbRc{finCard.left + S(18), finCard.top + S(54), finCard.right - S(18), finCard.bottom - S(14)};
                ::DrawTextW(memDc, finBody.c_str(), -1, &fbRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            }

            default:
                break;
        }

        ::SelectObject(memDc, oldF);
        ::BitBlt(hdcOut, 0, 0, W, H, memDc, 0, 0, SRCCOPY);
        ::SelectObject(memDc, oldBmp);
        ::DeleteObject(memBmp);
        ::DeleteDC(memDc);
    }

    // ----------------------------------------------------------------------------
    // MAIN TOP-LEVEL WIN32 WINDOW PROCEDURE
    // ----------------------------------------------------------------------------
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        SetupWizardApp* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<SetupWizardApp*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hWnd = hWnd;
        } else {
            self = reinterpret_cast<SetupWizardApp*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        }

        if (!self) return ::DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
            case WM_CREATE:
                self->CreateControls();
                rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                self->ApplyFontsAndLayout();
                return 0;

            case WM_ERASEBKGND:
                return 1; // Flicker-free double-buffered GDI+ painting in `WM_PAINT`

            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdc = ::BeginPaint(hWnd, &ps);
                self->PaintWindow(hdc);
                ::EndPaint(hWnd, &ps);
                return 0;
            }

            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN: {
                HDC hdcChild = reinterpret_cast<HDC>(wParam);
                HWND hChild  = reinterpret_cast<HWND>(lParam);
                const bool onCard = (hChild == self->hProgStatus || hChild == self->hUninstCache || hChild == self->hUninstPrefs);
                ::SetTextColor(hdcChild, self->theme.textPrimary);
                ::SetBkColor(hdcChild, onCard ? self->theme.cardBg : self->theme.windowBg);
                return reinterpret_cast<LRESULT>(onCard ? self->theme.hbrCard : self->theme.hbrWindow);
            }

            case WM_CTLCOLOREDIT: {
                HDC hdcEdit = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(hdcEdit, self->theme.textPrimary);
                ::SetBkColor(hdcEdit, self->theme.cardBg);
                return reinterpret_cast<LRESULT>(self->theme.hbrCard);
            }

            case WM_DPICHANGED: {
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                if (suggested) {
                    ::SetWindowPos(
                        hWnd, nullptr,
                        suggested->left, suggested->top,
                        suggested->right - suggested->left,
                        suggested->bottom - suggested->top,
                        SWP_NOZORDER | SWP_NOACTIVATE);
                }
                self->ApplyFontsAndLayout();
                return 0;
            }

            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
            case WM_THEMECHANGED:
                if (rsw::ui::IsLiveThemeBroadcast(msg, wParam, lParam)) {
                    self->theme.Refresh(self->cfg.accentPrimary, self->cfg.accentSecondary);
                    rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                    self->LoadEmbeddedRtfIntoControl();
                    for (HWND sc : {self->hLicRich, self->hReadMeEdit, self->hFeatTree}) {
                        rsw::ui::ApplyDarkScrollbarTheme(sc, self->theme);
                    }
                    self->ApplyFontsAndLayout();
                    ::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
                    return 0;
                }
                break;

            case WM_NOTIFY: {
                const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
                if (nm && nm->idFrom == IDC_FEAT_TREE) {
                    if (nm->code == TVN_SELCHANGEDW) {
                        const auto* nmtv = reinterpret_cast<const NMTREEVIEWW*>(lParam);
                        const std::size_t idx = static_cast<std::size_t>(nmtv->itemNew.lParam);
                        if (idx < self->featureNodes.size()) {
                            self->selectedFeatureIdx = idx;
                            ::InvalidateRect(hWnd, nullptr, FALSE);
                        }
                        return 0;
                    }
                    if (nm->code == NM_CLICK || nm->code == NM_RCLICK) {
                        POINT ptScreen{};
                        ::GetCursorPos(&ptScreen);
                        POINT ptClient = ptScreen;
                        ::ScreenToClient(self->hFeatTree, &ptClient);
                        TVHITTESTINFO hti{};
                        hti.pt = ptClient;
                        HTREEITEM hHit = TreeView_HitTest(self->hFeatTree, &hti);
                        const bool hitIcon = (hti.flags & (TVHT_ONITEMSTATEICON | TVHT_ONITEMICON)) != 0;
                        if (hHit && (nm->code == NM_RCLICK || hitIcon)) {
                            TreeView_SelectItem(self->hFeatTree, hHit);
                            TVITEMW tvi{};
                            tvi.mask  = TVIF_HANDLE | TVIF_PARAM;
                            tvi.hItem = hHit;
                            if (TreeView_GetItem(self->hFeatTree, &tvi)) {
                                RECT itemRc{};
                                if (TreeView_GetItemRect(self->hFeatTree, hHit, &itemRc, FALSE)) {
                                    POINT popPt{ptClient.x, itemRc.bottom};
                                    ::ClientToScreen(self->hFeatTree, &popPt);
                                    self->ShowFeatureStatePopupMenu(static_cast<std::size_t>(tvi.lParam), popPt);
                                }
                            }
                            return 1; // Consume click so TreeView doesn't double-toggle
                        }
                    }
                    if (nm->code == TVN_KEYDOWN) {
                        const auto* kd = reinterpret_cast<const NMTVKEYDOWN*>(lParam);
                        if (kd->wVKey == VK_SPACE && self->selectedFeatureIdx < self->featureNodes.size()) {
                            HTREEITEM hSel = self->featureNodes[self->selectedFeatureIdx].hTreeItem;
                            RECT itemRc{};
                            POINT popPt{16, 16};
                            if (hSel && TreeView_GetItemRect(self->hFeatTree, hSel, &itemRc, TRUE)) {
                                popPt = {itemRc.left, itemRc.bottom};
                            }
                            ::ClientToScreen(self->hFeatTree, &popPt);
                            self->ShowFeatureStatePopupMenu(self->selectedFeatureIdx, popPt);
                            return 1;
                        }
                    }
                }
                break;
            }

            case WM_CONTEXTMENU: {
                if (reinterpret_cast<HWND>(wParam) == self->hFeatTree &&
                    self->selectedFeatureIdx < self->featureNodes.size()) {
                    POINT pt{
                        static_cast<int>(static_cast<short>(LOWORD(lParam))),
                        static_cast<int>(static_cast<short>(HIWORD(lParam)))
                    };
                    if (pt.x == -1 && pt.y == -1) {
                        RECT itemRc{};
                        HTREEITEM hSel = self->featureNodes[self->selectedFeatureIdx].hTreeItem;
                        if (hSel && TreeView_GetItemRect(self->hFeatTree, hSel, &itemRc, TRUE)) {
                            pt = {itemRc.left, itemRc.bottom};
                            ::ClientToScreen(self->hFeatTree, &pt);
                        }
                    }
                    self->ShowFeatureStatePopupMenu(self->selectedFeatureIdx, pt);
                    return 0;
                }
                break;
            }

            case WM_COMMAND: {
                const int id   = LOWORD(wParam);
                const int code = HIWORD(wParam);
                if (id == IDC_DIR_EDIT && code == EN_CHANGE) {
                    ::InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
                switch (id) {
                    case IDC_MODE_EXPRESS:
                        self->selectedAction = SelectedAction::InstallExpress;
                        self->UpdateControlVisibilityForPage();
                        return 0;
                    case IDC_MODE_COMPACT:
                        self->selectedAction = SelectedAction::InstallCompact;
                        self->UpdateControlVisibilityForPage();
                        return 0;
                    case IDC_MODE_CUSTOM:
                        self->selectedAction = SelectedAction::InstallCustom;
                        self->UpdateControlVisibilityForPage();
                        return 0;

                    case IDC_REPAIR_QUICK:
                        self->selectedAction = SelectedAction::MaintQuickRepair;
                        return 0;
                    case IDC_REPAIR_FULL:
                        self->selectedAction = SelectedAction::MaintFullReinstall;
                        return 0;

                    case IDC_AR_CELL_1:
                    case IDC_AR_CELL_2:
                    case IDC_AR_CELL_3:
                    case IDC_AR_CELL_4:
                        self->autorunChoiceIdx = id - IDC_AR_CELL_1;
                        self->UpdateControlVisibilityForPage();
                        return 0;

                    case IDC_LIC_ACCEPT_CHK:
                        self->UpdateControlVisibilityForPage();
                        return 0;

                    case IDC_DIR_BROWSE_BTN:
                        self->BrowseForDestinationFolder();
                        return 0;

                    case IDC_FEAT_RESET_BTN:
                        for (auto& fn : self->featureNodes) {
                            fn.currentState = fn.defaultState;
                        }
                        self->SyncFeatureTreeStateIcons();
                        return 0;

                    case IDC_FIN_VIEWLOG_BTN:
                        if (!self->msiLogPath.empty()) {
                            ::ShellExecuteW(hWnd, L"open", L"notepad.exe", self->msiLogPath.wstring().c_str(), nullptr, SW_SHOWNORMAL);
                        }
                        return 0;

                    case IDC_NAV_PRIMARY:
                    case IDOK:
                        if (::IsWindowEnabled(self->hBtnPrimary)) {
                            self->OnPrimaryButtonClicked();
                        }
                        return 0;

                    case IDC_NAV_BACK:
                        self->OnBackButtonClicked();
                        return 0;

                    case IDC_NAV_CANCEL:
                    case IDCANCEL:
                        if (self->installRunning.load()) {
                            self->cancelRequested.store(true);
                            ::EnableWindow(self->hBtnCancel, FALSE);
                            ::SetWindowTextW(self->hProgStatus, L"Canceling installation and rolling back changes...");
                        } else {
                            ::DestroyWindow(hWnd);
                        }
                        return 0;
                }
                break;
            }

            case WM_RSW_PROGRESS_UPDATE: {
                const int perm = static_cast<int>(wParam);
                self->currentPermille = perm;
                ::SendMessageW(self->hProgBar, PBM_SETPOS, wParam, 0);
                const int decile = perm / 100;
                if (decile != self->lastAnnouncedDecile) {
                    self->lastAnnouncedDecile = decile;
                    ::NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, self->hProgBar, OBJID_CLIENT, CHILDID_SELF);
                }
                ::InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }

            case WM_RSW_STATUS_TEXT: {
                std::wstring txt;
                {
                    std::lock_guard<std::mutex> lk(self->msgMutex);
                    txt = self->latestStatusText;
                }
                ::SetWindowTextW(self->hProgStatus, txt.c_str());
                return 0;
            }

            case WM_RSW_ASK_RETRY_DL: {
                auto* p = reinterpret_cast<RetryDownloadPromptCtx*>(wParam);
                if (!p) return 0;
                const TASKDIALOG_BUTTON btns[2] = {
                    {101, L"Retry Download\nCheck your Internet connection and try downloading again"},
                    {102, L"Exit Setup\nClose Setup without making changes"}
                };
                const std::wstring title = self->cfg.productName + L" Setup";
                const std::wstring instr = L"Could not download required Windows component";
                const std::wstring body  = L"Setup needs " + p->runtimeName + L" before continuing.\n\nPlease verify your Internet connection and try again.";
                const std::wstring details = L"Source: " + p->url + L"\nDiagnostic: " + p->errorDetail;
                TASKDIALOGCONFIG tdc{};
                tdc.cbSize                 = sizeof(tdc);
                tdc.hwndParent             = hWnd;
                tdc.dwFlags                = TDF_USE_COMMAND_LINKS | TDF_ALLOW_DIALOG_CANCELLATION;
                tdc.pszWindowTitle         = title.c_str();
                tdc.pszMainIcon            = TD_WARNING_ICON;
                tdc.pszMainInstruction     = instr.c_str();
                tdc.pszContent             = body.c_str();
                tdc.cButtons               = 2;
                tdc.pButtons               = btns;
                tdc.nDefaultButton         = 101;
                tdc.pszExpandedControlText = L"Show technical details";
                tdc.pszExpandedInformation = details.c_str();
                int chosenBtn = 102;
                ::TaskDialogIndirect(&tdc, &chosenBtn, nullptr, nullptr);
                p->retryChosen = (chosenBtn == 101);
                return 0;
            }

            case WM_RSW_REQUEST_UAC: {
                auto* u = reinterpret_cast<UacLaunchPromptCtx*>(wParam);
                if (!u) return 0;
                wchar_t selfExe[MAX_PATH * 4]{};
                ::GetModuleFileNameW(nullptr, selfExe, static_cast<DWORD>(std::size(selfExe)));
                const std::wstring workerParams = L"--elevated-msi-worker \"" + u->jobFilePath + L"\"";

                SHELLEXECUTEINFOW sei{};
                sei.cbSize       = sizeof(sei);
                sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
                sei.hwnd         = hWnd;
                sei.lpVerb       = L"runas";
                sei.lpFile       = selfExe;
                sei.lpParameters = workerParams.c_str();
                sei.nShow        = SW_HIDE;
                if (!::ShellExecuteExW(&sei) || !sei.hProcess) {
                    u->win32Error     = ::GetLastError();
                    u->canceledByUser = (u->win32Error == ERROR_CANCELLED);
                } else {
                    u->hChildProcess = sei.hProcess;
                }
                return 0;
            }

            case WM_RSW_INSTALL_DONE:
                if (static_cast<UINT>(wParam) == kSentinelUacDeclined) {
                    // User clicked "No" on UAC — return calmly to `pageBeforeInstall`!
                    self->NavigateTo(self->pageBeforeInstall, self->hBtnPrimary);
                    return 0;
                }
                self->OnTransactionCompleted();
                return 0;

            case WM_CLOSE:
                if (self->installRunning.load()) {
                    self->cancelRequested.store(true);
                    return 0;
                }
                ::DestroyWindow(hWnd);
                return 0;

            case WM_DESTROY:
                ::PostQuitMessage(0);
                return 0;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

} // namespace

namespace {

struct ElevatedWorkerProgressState {
    std::filesystem::path statusFile;
    std::filesystem::path cancelFile;
    long long             totalTicks = 1000;
    long long             curTicks   = 0;
    long long             stepTicks  = 1;
    bool                  forward    = true;
    int                   permille   = 0;
    std::string           statusUtf8 = "Applying Windows Installer changes...";
    std::string           errorUtf8;
    std::wstring          curActionDesc;
    std::wstring          curActionTmpl;

    void FlushToDisk() const {
        if (statusFile.empty()) return;
        std::ofstream out(statusFile, std::ios::binary | std::ios::trunc);
        if (!out) return;
        auto oneLine = [](std::string s) {
            for (char& c : s) {
                if (c == '\r' || c == '\n') c = ' ';
            }
            return s;
        };
        out << permille << "\n"
            << oneLine(statusUtf8) << "\n"
            << oneLine(errorUtf8)  << "\n";
    }
};

INT CALLBACK OnElevatedMsiRecord(void* pvContext, UINT iMessageType, MSIHANDLE hRecord) {
    auto* st = static_cast<ElevatedWorkerProgressState*>(pvContext);
    if (!st) return 0;
    std::error_code ec;
    if (!st->cancelFile.empty() && std::filesystem::exists(st->cancelFile, ec)) {
        return IDCANCEL;
    }

    const UINT msgKind = iMessageType & 0xFF000000u;
    switch (msgKind) {
        case INSTALLMESSAGE_ACTIONSTART: {
            wchar_t descBuf[512]{};
            wchar_t tmplBuf[512]{};
            DWORD cDesc = static_cast<DWORD>(std::size(descBuf));
            DWORD cTmpl = static_cast<DWORD>(std::size(tmplBuf));
            ::MsiRecordGetStringW(hRecord, 2, descBuf, &cDesc);
            ::MsiRecordGetStringW(hRecord, 3, tmplBuf, &cTmpl);
            if (descBuf[0] != L'\0') {
                st->curActionDesc = SanitizeUnexpandedMsiTokens(descBuf);
                st->curActionTmpl = tmplBuf;
                if (!st->curActionDesc.empty()) {
                    st->statusUtf8 = rsw::WideToUtf8(st->curActionDesc);
                    st->FlushToDisk();
                }
            }
            return IDOK;
        }
        case INSTALLMESSAGE_ACTIONDATA: {
            const std::wstring formatted = FormatMsiActionDataStatus(st->curActionDesc, st->curActionTmpl, hRecord);
            if (!formatted.empty()) {
                st->statusUtf8 = rsw::WideToUtf8(formatted);
                st->FlushToDisk();
            }
            return IDOK;
        }
        case INSTALLMESSAGE_ERROR:
        case INSTALLMESSAGE_FATALEXIT: {
            wchar_t errBuf[1024]{};
            DWORD cch = static_cast<DWORD>(std::size(errBuf));
            ::MsiFormatRecordW(0, hRecord, errBuf, &cch);
            if (errBuf[0] != L'\0') {
                st->errorUtf8 = rsw::WideToUtf8(errBuf);
                st->FlushToDisk();
            }
            return IDOK;
        }
        case INSTALLMESSAGE_PROGRESS: {
            if (::MsiRecordGetFieldCount(hRecord) == 0) return IDOK;
            const int subType = ::MsiRecordGetInteger(hRecord, 1);
            const int f2      = ::MsiRecordGetInteger(hRecord, 2);
            const int f3      = ::MsiRecordGetInteger(hRecord, 3);
            if (subType == 0) {
                st->totalTicks = std::max<long long>(1, f2);
                st->forward    = (f3 == 0);
                st->curTicks   = st->forward ? 0 : st->totalTicks;
            } else if (subType == 1) {
                if (f3 != 0) st->stepTicks = std::max<int>(1, f2);
            } else if (subType == 2) {
                const long long delta = (f2 != MSI_NULL_INTEGER && f2 > 0) ? f2 : st->stepTicks;
                if (st->forward) st->curTicks += delta;
                else st->curTicks = std::max<long long>(0, st->curTicks - delta);
            } else if (subType == 3) {
                st->totalTicks += std::max<int>(0, f2);
            }
            st->permille = static_cast<int>(
                std::clamp<long long>((st->curTicks * 1000LL) / std::max<long long>(1, st->totalTicks), 0, 1000));
            st->FlushToDisk();
            return IDOK;
        }
        default:
            break;
    }
    return 0;
}

int RunElevatedMsiWorkerFromJobFile(const std::filesystem::path& jobFile) {
    std::ifstream in(jobFile, std::ios::binary);
    if (!in) return ERROR_OPEN_FAILED;

    std::map<std::string, std::string> kv;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }

    const std::string mode        = kv["Mode"];
    const std::wstring msiPath    = rsw::Utf8ToWide(kv["MsiPath"]);
    const std::wstring prodCode   = rsw::Utf8ToWide(kv["ProductCode"]);
    const std::wstring logPath    = rsw::Utf8ToWide(kv["LogPath"]);
    const std::wstring props      = rsw::Utf8ToWide(kv["Props"]);

    ElevatedWorkerProgressState st{};
    st.statusFile = rsw::Utf8ToWide(kv["StatusFile"]);
    st.cancelFile = rsw::Utf8ToWide(kv["CancelFile"]);
    st.FlushToDisk();

    // Execute all staged `[Prerequisite.*]` runtimes BEFORE `msiexec` (`MsiInstallProductW`)!
    bool anyPrereqReboot = false;
    const int prereqCount = std::clamp(std::atoi(kv["PrereqCount"].c_str()), 0, 64);
    for (int j = 0; j < prereqCount; ++j) {
        std::error_code cEc;
        if (!st.cancelFile.empty() && std::filesystem::exists(st.cancelFile, cEc)) {
            return ERROR_INSTALL_USEREXIT;
        }
        const std::string pfx = "Prereq_" + std::to_string(j) + "_";
        rsw::RuntimePrerequisiteSpec spec{};
        spec.displayName  = kv[pfx + "Name"];
        spec.silentArgs   = kv[pfx + "Args"];
        spec.successCodes = rsw::ParseExitCodeList(kv[pfx + "SuccessCodes"]);
        spec.rebootCodes  = rsw::ParseExitCodeList(kv[pfx + "RebootCodes"]);
        spec.optional     = rsw::EqualsIgnoreCase(kv[pfx + "Optional"], "true");
        const std::filesystem::path binPath = rsw::Utf8ToWide(kv[pfx + "Path"]);

        st.statusUtf8 = "Installing prerequisite: " + spec.displayName + "...";
        st.permille   = std::min(300, ((j + 1) * 250) / std::max(1, prereqCount));
        st.FlushToDisk();

        std::uint32_t rawCode = 0;
        std::string runErr;
        const auto outcome = rsw::ExecuteStagedPrerequisiteInstaller(spec, binPath, rawCode, runErr);
        if (outcome == rsw::PrereqExitOutcome::SucceededRebootRequested) {
            anyPrereqReboot = true;
        } else if (outcome == rsw::PrereqExitOutcome::Failed && !spec.optional) {
            st.errorUtf8 = runErr;
            st.FlushToDisk();
            return static_cast<int>(rawCode ? rawCode : ERROR_INSTALL_FAILURE);
        }
    }

    ::MsiSetInternalUI(INSTALLUILEVEL_NONE, nullptr);
    constexpr DWORD kFilter =
        INSTALLLOGMODE_FATALEXIT | INSTALLLOGMODE_ERROR | INSTALLLOGMODE_WARNING |
        INSTALLLOGMODE_ACTIONSTART | INSTALLLOGMODE_ACTIONDATA |
        INSTALLLOGMODE_COMMONDATA | INSTALLLOGMODE_PROGRESS;

    INSTALLUI_HANDLER_RECORD prevHandler = nullptr;
    ::MsiSetExternalUIRecord(&OnElevatedMsiRecord, kFilter, &st, &prevHandler);
    if (!logPath.empty()) {
        ::MsiEnableLogW(INSTALLLOGMODE_VERBOSE, logPath.c_str(), INSTALLLOGATTRIBUTES_FLUSHEACHLINE);
    }

    UINT rc = ERROR_SUCCESS;
    if (mode == "uninstall" && !prodCode.empty()) {
        rc = ::MsiConfigureProductExW(prodCode.c_str(), INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT, props.c_str());
    } else if (mode == "quickrepair" && !prodCode.empty()) {
        rc = ::MsiReinstallProductW(
            prodCode.c_str(),
            REINSTALLMODE_FILEMISSING | REINSTALLMODE_FILEOLDERVERSION | REINSTALLMODE_FILEVERIFY |
            REINSTALLMODE_MACHINEDATA | REINSTALLMODE_USERDATA | REINSTALLMODE_SHORTCUT);
    } else if (mode == "fullreinstall") {
        if (!msiPath.empty() && std::filesystem::exists(msiPath)) {
            rc = ::MsiInstallProductW(msiPath.c_str(), L"REINSTALL=ALL REINSTALLMODE=vomus");
        } else if (!prodCode.empty()) {
            rc = ::MsiReinstallProductW(
                prodCode.c_str(),
                REINSTALLMODE_FILEREPLACE | REINSTALLMODE_SHORTCUT | REINSTALLMODE_MACHINEDATA | REINSTALLMODE_USERDATA);
        }
    } else {
        rc = ::MsiInstallProductW(msiPath.c_str(), props.c_str());
    }

    ::MsiSetExternalUIRecord(prevHandler, 0, nullptr, nullptr);
    if (rc == ERROR_SUCCESS && anyPrereqReboot) {
        rc = ERROR_SUCCESS_REBOOT_REQUIRED;
    }
    if (rc == ERROR_SUCCESS || rc == ERROR_SUCCESS_REBOOT_REQUIRED) {
        st.permille = 1000;
    }
    st.FlushToDisk();
    return static_cast<int>(rc);
}

// ================================================================================
// UNIVERSAL SINGLE-DOWNLOAD DISPATCHER (`kResIdUniversalManifest` `#210`):
//   1. `Mode=offline` (`<Product>-offline-setup.exe`):
//      Embeds BOTH `x64` (`#211`) and `ARM64` (`#212`) LZMS-compressed native
//      installers in one portable `.exe`, detects true host silicon via
//      `IsWow64Process2` (piercing Windows 11 Prism emulation!), extracts ONLY
//      the native child installer, verifies SHA-256, and forwards all CLI args.
//   2. `Mode=web` (`<Product>-setup.exe` Smart CPU-Aware Web Stub):
//      Compact ~400 KB `.exe` that detects true host silicon (`ARM64` vs `x64`),
//      streams ONLY the matching native installer over HTTPS (`WinHTTP`) inside
//      an accessible Dark/Light/High-Contrast progress dialog (or silently in `/s`),
//      verifies SHA-256 (`bcrypt.dll`), and launches the genuine native installer!
// ================================================================================
struct UniversalLauncherConfig {
    std::string   mode              = "offline";
    std::wstring  productName       = L"Application";
    std::wstring  tagline           = L"Universal Windows 11 Setup";
    std::wstring  websiteLabel;
    std::wstring  version           = L"1.0.0";
    std::wstring  manufacturer      = L"RaggieSoft";
    int           minWindowsBuild   = 22000;
    std::wstring  osLaunchMessage   = L"This application requires Windows 11 (Build 22000) or later.";
    COLORREF      accentPrimary     = RGB(37, 99, 235);
    COLORREF      accentSecondary   = RGB(251, 191, 36);
    std::wstring  x64FileName       = L"setup-x64.exe";
    std::wstring  arm64FileName     = L"setup-arm64.exe";
    std::string   sha256X64;
    std::string   sha256Arm64;
    std::uint64_t sizeX64           = 0;
    std::uint64_t sizeArm64         = 0;
    std::string   urlX64;
    std::string   urlArm64;
};

UniversalLauncherConfig ParseUniversalLauncherConfig(std::string_view text) {
    UniversalLauncherConfig cfg{};
    std::istringstream iss{std::string(text)};
    std::string line;
    std::map<std::string, std::string> kv;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '[' || line.front() == ';') continue;
        const auto eq = line.find('=');
        if (eq != std::string::npos) {
            kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    auto getW = [&](const char* k, const std::wstring& defVal) -> std::wstring {
        auto it = kv.find(k);
        return (it != kv.end() && !it->second.empty())
            ? UnescapeIniNewlines(rsw::Utf8ToWide(it->second))
            : defVal;
    };
    if (!kv["Mode"].empty()) cfg.mode = rsw::AsciiLower(rsw::TrimAscii(kv["Mode"]));
    cfg.productName     = getW("ProductName", cfg.productName);
    cfg.tagline         = getW("Tagline", cfg.tagline);
    cfg.websiteLabel    = getW("WebsiteLabel", L"");
    cfg.version         = getW("Version", cfg.version);
    cfg.manufacturer    = getW("Manufacturer", cfg.manufacturer);
    if (!kv["MinWindowsBuild"].empty()) cfg.minWindowsBuild = std::atoi(kv["MinWindowsBuild"].c_str());
    cfg.osLaunchMessage = getW("OsLaunchMessage", cfg.osLaunchMessage);
    cfg.accentPrimary   = rsw::ui::ParseHexColor(kv["AccentPrimaryHex"], cfg.accentPrimary);
    cfg.accentSecondary = rsw::ui::ParseHexColor(kv["AccentSecondaryHex"], cfg.accentSecondary);
    cfg.x64FileName     = getW("X64FileName", cfg.x64FileName);
    cfg.arm64FileName   = getW("Arm64FileName", cfg.arm64FileName);
    cfg.sha256X64       = rsw::AsciiLower(rsw::TrimAscii(kv["Sha256X64"]));
    cfg.sha256Arm64     = rsw::AsciiLower(rsw::TrimAscii(kv["Sha256Arm64"]));
    if (!kv["SizeX64"].empty())   cfg.sizeX64   = static_cast<std::uint64_t>(std::strtoull(kv["SizeX64"].c_str(), nullptr, 10));
    if (!kv["SizeArm64"].empty()) cfg.sizeArm64 = static_cast<std::uint64_t>(std::strtoull(kv["SizeArm64"].c_str(), nullptr, 10));
    cfg.urlX64          = rsw::TrimAscii(kv["UrlX64"]);
    cfg.urlArm64        = rsw::TrimAscii(kv["UrlArm64"]);
    return cfg;
}

std::wstring QuoteWin32Argument(const std::wstring& arg) {
    if (!arg.empty() && arg.find_first_of(L" \t\"") == std::wstring::npos) {
        return arg;
    }
    std::wstring out = L"\"";
    for (std::size_t i = 0; i < arg.size(); ++i) {
        std::size_t bs = 0;
        while (i < arg.size() && arg[i] == L'\\') {
            ++bs;
            ++i;
        }
        if (i == arg.size()) {
            out.append(bs * 2, L'\\');
            break;
        } else if (arg[i] == L'"') {
            out.append(bs * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(bs, L'\\');
            out.push_back(arg[i]);
        }
    }
    out.push_back(L'"');
    return out;
}

class UniversalWebDownloaderUi {
public:
    static constexpr UINT WM_UNIV_PROGRESS = WM_APP + 40;
    static constexpr UINT WM_UNIV_FINISHED = WM_APP + 41;
    static constexpr int  IDC_UNIV_PROG    = 1401;
    static constexpr int  IDC_UNIV_CANCEL  = 1402;

    UniversalLauncherConfig cfg{};
    rsw::ui::RswTheme       theme{};
    bool                    targetArm64    = false;
    std::string             targetUrl;
    std::string             expectedSha256;
    std::uint64_t           expectedBytes  = 0;
    std::filesystem::path   destPath;

    HWND                    hWnd           = nullptr;
    HWND                    hProgBar       = nullptr;
    HWND                    hBtnCancel     = nullptr;
    HICON                   hIconBig       = nullptr;
    HICON                   hIconSmall     = nullptr;
    HFONT                   hFontTitle     = nullptr;
    HFONT                   hFontBold      = nullptr;
    HFONT                   hFontBody      = nullptr;

    std::atomic<bool>       cancelFlag{false};
    std::mutex              mtx;
    std::wstring            statusLine     = L"Connecting to download server...";
    std::string             errorUtf8;
    bool                    downloadSucceeded = false;
    std::thread             dlThread;

    ~UniversalWebDownloaderUi() {
        cancelFlag.store(true);
        if (dlThread.joinable()) dlThread.join();
        for (HFONT* f : {&hFontTitle, &hFontBold, &hFontBody}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
    }

    void RebuildFonts(UINT dpi) {
        for (HFONT* f : {&hFontTitle, &hFontBold, &hFontBody}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
        const int ts = theme.textScalePercent;
        hFontTitle = rsw::ui::CreateScaledFont(L"Segoe UI", 13, FW_BOLD,     dpi, ts);
        hFontBold  = rsw::ui::CreateScaledFont(L"Segoe UI", 10, FW_SEMIBOLD, dpi, ts);
        hFontBody  = rsw::ui::CreateScaledFont(L"Segoe UI",  9, FW_NORMAL,   dpi, ts);
        if (hBtnCancel) ::SendMessageW(hBtnCancel, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBody), TRUE);
    }

    void LayoutControls() {
        if (!hWnd) return;
        const UINT dpi = ::GetDpiForWindow(hWnd);
        RebuildFonts(dpi);
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = rc.right - rc.left;
        const int H = rc.bottom - rc.top;
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        if (hProgBar) {
            ::MoveWindow(hProgBar, S(28), S(162), std::max(S(160), W - S(56)), S(20), TRUE);
        }
        if (hBtnCancel) {
            ::MoveWindow(hBtnCancel, W - S(124), H - S(46), S(96), S(32), TRUE);
        }
    }

    void StartDownloadAttempt() {
        cancelFlag.store(false);
        if (dlThread.joinable()) dlThread.join();
        {
            std::lock_guard<std::mutex> lk(mtx);
            errorUtf8.clear();
            statusLine = std::wstring(L"Downloading native ") + (targetArm64 ? L"ARM64" : L"x64") + L" installer...";
        }
        if (hProgBar) ::SendMessageW(hProgBar, PBM_SETPOS, 0, 0);
        ::InvalidateRect(hWnd, nullptr, FALSE);

        dlThread = std::thread([this]() {
            std::string err;
            const bool okDl = rsw::DownloadFileViaWinHttp(
                targetUrl,
                destPath,
                &cancelFlag,
                [this](std::uint64_t got, std::uint64_t total) {
                    const std::uint64_t denom = (total > 0) ? total : expectedBytes;
                    int permille = 0;
                    std::wstring txt;
                    if (denom > 0) {
                        permille = static_cast<int>(std::min<std::uint64_t>(980, (got * 980u) / denom));
                        const int pct = static_cast<int>(std::min<std::uint64_t>(99, (got * 100u) / denom));
                        txt = std::wstring(L"Downloading native ") + (targetArm64 ? L"ARM64" : L"x64") +
                              L" installer (" + FormatByteSizeFriendly(got) + L" of " +
                              FormatByteSizeFriendly(denom) + L" — " + std::to_wstring(pct) + L"%)...";
                    } else {
                        txt = std::wstring(L"Downloading native ") + (targetArm64 ? L"ARM64" : L"x64") +
                              L" installer (" + FormatByteSizeFriendly(got) + L")...";
                    }
                    {
                        std::lock_guard<std::mutex> lk(mtx);
                        statusLine = std::move(txt);
                    }
                    ::PostMessageW(hWnd, WM_UNIV_PROGRESS, static_cast<WPARAM>(permille), 0);
                },
                err);

            bool okAll = okDl;
            if (okAll && !expectedSha256.empty()) {
                {
                    std::lock_guard<std::mutex> lk(mtx);
                    statusLine = L"Verifying SHA-256 cryptographic checksum...";
                }
                ::PostMessageW(hWnd, WM_UNIV_PROGRESS, 990, 0);
                std::string actualHex;
                if (!rsw::ComputeFileSha256Hex(destPath, actualHex) ||
                    !rsw::EqualsIgnoreCase(actualHex, expectedSha256)) {
                    okAll = false;
                    err = "Downloaded installer SHA-256 digest did not match expected publisher signature.";
                    std::error_code rmEc;
                    std::filesystem::remove(destPath, rmEc);
                }
            }
            {
                std::lock_guard<std::mutex> lk(mtx);
                errorUtf8 = err;
            }
            ::PostMessageW(hWnd, WM_UNIV_FINISHED, okAll ? 1 : 0, 0);
        });
    }

    void PaintWindow(HDC hdcOut) {
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(1, rc.right - rc.left);
        const int H = std::max<int>(1, rc.bottom - rc.top);
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        HDC memDc = ::CreateCompatibleDC(hdcOut);
        HBITMAP memBmp = ::CreateCompatibleBitmap(hdcOut, W, H);
        HGDIOBJ oldBmp = ::SelectObject(memDc, memBmp);
        ::SetBkMode(memDc, TRANSPARENT);

        HBRUSH bgBr = ::CreateSolidBrush(theme.windowBg);
        ::FillRect(memDc, &rc, bgBr);
        ::DeleteObject(bgBr);

        const int hdrH = S(68);
        RECT hdrRc{0, 0, W, hdrH};
        HBRUSH hdrBr = ::CreateSolidBrush(theme.isHighContrast ? theme.windowBg : (theme.isDarkMode ? RGB(11, 19, 43) : RGB(248, 250, 252)));
        ::FillRect(memDc, &hdrRc, hdrBr);
        ::DeleteObject(hdrBr);

        // 3px Accent Divider Bar (uses semantic highlight color in Windows Contrast Themes)
        RECT barRc{0, hdrH - S(3), W, hdrH};
        HBRUSH barBr = ::CreateSolidBrush(theme.isHighContrast ? theme.accentPrimary : cfg.accentPrimary);
        ::FillRect(memDc, &barRc, barBr);
        ::DeleteObject(barBr);

        if (hIconBig) {
            ::DrawIconEx(memDc, S(20), S(16), hIconBig, S(36), S(36), 0, nullptr, DI_NORMAL);
        }

        ::SelectObject(memDc, hFontTitle);
        ::SetTextColor(memDc, theme.textPrimary);
        const std::wstring headTitle = cfg.productName + L" Setup (v" + cfg.version + L")";
        RECT tRc{S(68), S(12), W - S(20), S(38)};
        ::DrawTextW(memDc, headTitle.c_str(), -1, &tRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        ::SelectObject(memDc, hFontBody);
        ::SetTextColor(memDc, theme.textSecondary);
        RECT subRc{S(68), S(38), W - S(20), S(60)};
        ::DrawTextW(memDc, L"Smart Universal Installer — Automatically matching your Windows processor", -1, &subRc,
                    DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        // Card Box around CPU detection & live status
        RECT cardRc{S(18), S(82), W - S(18), H - S(58)};
        HBRUSH cardBr = ::CreateSolidBrush(theme.cardBg);
        HPEN cardPen  = ::CreatePen(PS_SOLID, theme.isHighContrast ? 2 : 1, theme.borderSubtle);
        HGDIOBJ oldBr = ::SelectObject(memDc, cardBr);
        HGDIOBJ oldPn = ::SelectObject(memDc, cardPen);
        ::RoundRect(memDc, cardRc.left, cardRc.top, cardRc.right, cardRc.bottom, S(10), S(10));
        ::SelectObject(memDc, oldPn);
        ::SelectObject(memDc, oldBr);
        ::DeleteObject(cardPen);
        ::DeleteObject(cardBr);

        ::SelectObject(memDc, hFontBold);
        ::SetTextColor(memDc, theme.textPrimary);
        const std::wstring cpuLine = targetArm64
            ? L"Detected Processor: Windows 11 ARM64 (Native Silicon)"
            : L"Detected Processor: Windows 11 x64 (64-Bit Intel / AMD)";
        RECT cpuRc{S(30), S(94), W - S(30), S(120)};
        ::DrawTextW(memDc, cpuLine.c_str(), -1, &cpuRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        std::wstring curStatus;
        {
            std::lock_guard<std::mutex> lk(mtx);
            curStatus = statusLine;
        }
        ::SelectObject(memDc, hFontBody);
        ::SetTextColor(memDc, theme.textSecondary);
        RECT stRc{S(30), S(124), W - S(30), S(154)};
        ::DrawTextW(memDc, curStatus.c_str(), -1, &stRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        if (!cfg.websiteLabel.empty()) {
            RECT footRc{S(20), H - S(38), W - S(140), H - S(14)};
            ::DrawTextW(memDc, cfg.websiteLabel.c_str(), -1, &footRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
        }

        ::BitBlt(hdcOut, 0, 0, W, H, memDc, 0, 0, SRCCOPY);
        ::SelectObject(memDc, oldBmp);
        ::DeleteObject(memBmp);
        ::DeleteDC(memDc);
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<UniversalWebDownloaderUi*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<UniversalWebDownloaderUi*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hWnd = hWnd;
            rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
        }
        if (!self) return ::DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
            case WM_CREATE: {
                HINSTANCE hInst = ::GetModuleHandleW(nullptr);
                self->hProgBar = ::CreateWindowExW(
                    0, PROGRESS_CLASSW, L"Installer Download Progress",
                    WS_CHILD | WS_VISIBLE | PBS_SMOOTH,
                    0, 0, 100, 20, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_UNIV_PROG)), hInst, nullptr);
                ::SendMessageW(self->hProgBar, PBM_SETRANGE32, 0, 1000);
                self->hBtnCancel = ::CreateWindowExW(
                    0, L"BUTTON", L"&Cancel",
                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 96, 32, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_UNIV_CANCEL)), hInst, nullptr);
                rsw::ui::AttachAccessiblePushButtonSubclass(self->hBtnCancel, &self->theme, false, false);
                self->LayoutControls();
                self->StartDownloadAttempt();
                return 0;
            }
            case WM_DPICHANGED: {
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                ::SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                               suggested->right - suggested->left, suggested->bottom - suggested->top,
                               SWP_NOZORDER | SWP_NOACTIVATE);
                self->LayoutControls();
                ::InvalidateRect(hWnd, nullptr, TRUE);
                return 0;
            }
            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
            case WM_THEMECHANGED: {
                if (rsw::ui::IsLiveThemeBroadcast(msg, wParam, lParam)) {
                    self->theme.Refresh(self->cfg.accentPrimary, self->cfg.accentSecondary);
                    rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                    self->LayoutControls();
                    if (self->hBtnCancel) ::InvalidateRect(self->hBtnCancel, nullptr, TRUE);
                    ::InvalidateRect(hWnd, nullptr, TRUE);
                    return 0;
                }
                break;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdc = ::BeginPaint(hWnd, &ps);
                self->PaintWindow(hdc);
                ::EndPaint(hWnd, &ps);
                return 0;
            }
            case WM_UNIV_PROGRESS: {
                if (self->hProgBar) {
                    ::SendMessageW(self->hProgBar, PBM_SETPOS, wParam, 0);
                }
                ::InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            case WM_UNIV_FINISHED: {
                if (wParam == 1) {
                    if (self->hProgBar) ::SendMessageW(self->hProgBar, PBM_SETPOS, 1000, 0);
                    self->downloadSucceeded = true;
                    ::DestroyWindow(hWnd);
                    return 0;
                }
                if (self->cancelFlag.load()) {
                    self->downloadSucceeded = false;
                    ::DestroyWindow(hWnd);
                    return 0;
                }
                std::wstring errW;
                {
                    std::lock_guard<std::mutex> lk(self->mtx);
                    errW = rsw::Utf8ToWide(self->errorUtf8);
                }
                const std::wstring msgBody =
                    L"Could not download the native installer package:\n" + errW +
                    L"\n\nCheck your internet connection and choose Retry, or use the Full Offline Universal Installer for air-gapped PCs.";
                int chosenBtn = IDCANCEL;
                ::TaskDialog(
                    hWnd, ::GetModuleHandleW(nullptr),
                    (self->cfg.productName + L" Setup").c_str(),
                    L"Download Interrupted",
                    msgBody.c_str(),
                    TDCBF_RETRY_BUTTON | TDCBF_CANCEL_BUTTON,
                    TD_WARNING_ICON,
                    &chosenBtn);
                if (chosenBtn == IDRETRY) {
                    self->StartDownloadAttempt();
                } else {
                    self->downloadSucceeded = false;
                    ::DestroyWindow(hWnd);
                }
                return 0;
            }
            case WM_COMMAND: {
                if (LOWORD(wParam) == IDC_UNIV_CANCEL || LOWORD(wParam) == IDCANCEL) {
                    self->cancelFlag.store(true);
                    if (self->hBtnCancel) ::EnableWindow(self->hBtnCancel, FALSE);
                    return 0;
                }
                break;
            }
            case WM_CLOSE: {
                self->cancelFlag.store(true);
                ::DestroyWindow(hWnd);
                return 0;
            }
            case WM_DESTROY: {
                ::PostQuitMessage(0);
                return 0;
            }
            default:
                break;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

int RunUniversalInstallerDispatcher(
    HINSTANCE hInstance,
    int nCmdShow,
    const std::vector<std::wstring>& rawArgs,
    std::string_view manifestText)
{
    const UniversalLauncherConfig ucfg = ParseUniversalLauncherConfig(manifestText);
    const auto hostOs = rsw::QueryHostPlatform();
    bool targetArm64  = (hostOs.nativeMachine == IMAGE_FILE_MACHINE_ARM64);
    bool headlessMode = false;

    std::wstring forwardedArgs;
    for (std::size_t i = 0; i < rawArgs.size(); ++i) {
        const auto& a = rawArgs[i];
        if (a == L"--force-arch=arm64" || a == L"--force-arch=ARM64") {
            targetArm64 = true;
            continue;
        }
        if (a == L"--force-arch=x64" || a == L"--force-arch=amd64") {
            targetArm64 = false;
            continue;
        }
        if (a == L"/s" || a == L"/S" || a == L"/qn" || a == L"/quiet" || a == L"--silent" ||
            a == L"--extract-msi" || a == L"/extract" || a == L"--export-screenshots") {
            headlessMode = true;
        }
        if (!forwardedArgs.empty()) forwardedArgs.push_back(L' ');
        forwardedArgs += QuoteWin32Argument(a);
    }

    if (!rsw::MeetsWindowsBuildFloor(ucfg.minWindowsBuild)) {
        if (!headlessMode) {
            const std::wstring title  = ucfg.productName + L" Setup";
            const std::wstring detail = ucfg.osLaunchMessage + L"\n\nDetected OS Build: " + std::to_wstring(hostOs.buildNumber);
            ::TaskDialog(nullptr, hInstance, title.c_str(), L"Unsupported Windows Version",
                         detail.c_str(), TDCBF_CLOSE_BUTTON, TD_WARNING_ICON, nullptr);
        }
        return 1603;
    }

    std::error_code ec;
    const std::filesystem::path tmpDir = std::filesystem::temp_directory_path(ec) /
                                         (L"rsw-univ-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(tmpDir, ec);

    const std::wstring childName = targetArm64 ? ucfg.arm64FileName : ucfg.x64FileName;
    const std::filesystem::path childPath = tmpDir / childName;
    const std::string& expectedSha = targetArm64 ? ucfg.sha256Arm64 : ucfg.sha256X64;

    if (ucfg.mode == "offline") {
        const int resId = targetArm64 ? rsw::kResIdUniversalSetupArm64 : rsw::kResIdUniversalSetupX64;
        std::wstring extErr;
        if (!ExtractEmbeddedResourceToFile(resId, childPath, extErr)) {
            if (!headlessMode) {
                ::TaskDialog(nullptr, hInstance, (ucfg.productName + L" Setup").c_str(),
                             L"Package Extraction Error", extErr.c_str(), TDCBF_CLOSE_BUTTON, TD_ERROR_ICON, nullptr);
            }
            std::filesystem::remove_all(tmpDir, ec);
            return 1619;
        }
        if (!expectedSha.empty()) {
            std::string actualHex;
            if (!rsw::ComputeFileSha256Hex(childPath, actualHex) || !rsw::EqualsIgnoreCase(actualHex, expectedSha)) {
                std::filesystem::remove_all(tmpDir, ec);
                return 1620;
            }
        }
    } else {
        const std::string& targetUrl = targetArm64 ? ucfg.urlArm64 : ucfg.urlX64;
        if (targetUrl.empty()) {
            std::filesystem::remove_all(tmpDir, ec);
            return 1619;
        }
        if (headlessMode) {
            std::string dlErr;
            if (!rsw::DownloadFileViaWinHttp(targetUrl, childPath, nullptr, nullptr, dlErr)) {
                std::filesystem::remove_all(tmpDir, ec);
                return 9011;
            }
            if (!expectedSha.empty()) {
                std::string actualHex;
                if (!rsw::ComputeFileSha256Hex(childPath, actualHex) || !rsw::EqualsIgnoreCase(actualHex, expectedSha)) {
                    std::filesystem::remove_all(tmpDir, ec);
                    return 9012;
                }
            }
        } else {
            ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            {
                rsw::ui::ScopedGdiPlusSession gpSession;
                INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
                ::InitCommonControlsEx(&icc);

                UniversalWebDownloaderUi ui{};
                ui.cfg            = ucfg;
                ui.targetArm64    = targetArm64;
                ui.targetUrl      = targetUrl;
                ui.expectedSha256 = expectedSha;
                ui.expectedBytes  = targetArm64 ? ucfg.sizeArm64 : ucfg.sizeX64;
                ui.destPath       = childPath;
                ui.theme.Refresh(ucfg.accentPrimary, ucfg.accentSecondary);
                ui.hIconBig = static_cast<HICON>(::LoadImageW(
                    hInstance, MAKEINTRESOURCEW(IDI_RAGGIESOFT_APP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
                ui.hIconSmall = static_cast<HICON>(::LoadImageW(
                    hInstance, MAKEINTRESOURCEW(IDI_RAGGIESOFT_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
                if (!ui.hIconBig) ui.hIconBig = ::LoadIconW(nullptr, IDI_APPLICATION);

                constexpr const wchar_t* kUnivWndClass = L"RaggieSoftUniversalWebSetupWindow";
                WNDCLASSEXW wc{};
                wc.cbSize        = sizeof(wc);
                wc.lpfnWndProc   = &UniversalWebDownloaderUi::WndProc;
                wc.hInstance     = hInstance;
                wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
                wc.hIcon         = ui.hIconBig;
                wc.hIconSm       = ui.hIconSmall ? ui.hIconSmall : ui.hIconBig;
                wc.lpszClassName = kUnivWndClass;
                ::RegisterClassExW(&wc);

                const UINT dpi = ::GetDpiForSystem();
                RECT wRc{0, 0,
                         rsw::ui::ScalePx(540, dpi, ui.theme.textScalePercent),
                         rsw::ui::ScalePx(256, dpi, ui.theme.textScalePercent)};
                constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
                ::AdjustWindowRectExForDpi(&wRc, kStyle, FALSE, 0, dpi);
                const int wW = wRc.right - wRc.left;
                const int wH = wRc.bottom - wRc.top;
                const int wX = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - wW) / 2);
                const int wY = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - wH) / 2);

                const std::wstring wTitle = ucfg.productName + L" Setup";
                HWND hUnivWnd = ::CreateWindowExW(
                    0, kUnivWndClass, wTitle.c_str(), kStyle, wX, wY, wW, wH, nullptr, nullptr, hInstance, &ui);
                if (hUnivWnd) {
                    ::ShowWindow(hUnivWnd, nCmdShow);
                    ::UpdateWindow(hUnivWnd);
                    MSG msg{};
                    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
                        if (!::IsDialogMessageW(hUnivWnd, &msg)) {
                            ::TranslateMessage(&msg);
                            ::DispatchMessageW(&msg);
                        }
                    }
                }
                if (ui.dlThread.joinable()) ui.dlThread.join();
                if (!ui.downloadSucceeded) {
                    ::CoUninitialize();
                    std::filesystem::remove_all(tmpDir, ec);
                    return ERROR_INSTALL_USEREXIT;
                }
            }
            ::CoUninitialize();
        }
    }

    // Launch the verified 100% native (`ARM64` or `x64`) installer process and return its exact exit code!
    std::wstring childCmd = L"\"" + childPath.wstring() + L"\"";
    if (!forwardedArgs.empty()) {
        childCmd += L" " + forwardedArgs;
    }
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    const std::wstring workDir = tmpDir.wstring();
    if (!::CreateProcessW(nullptr, childCmd.data(), nullptr, nullptr, FALSE, 0, nullptr, workDir.c_str(), &si, &pi)) {
        std::filesystem::remove_all(tmpDir, ec);
        return ERROR_INSTALL_FAILURE;
    }
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD childExitCode = 0;
    ::GetExitCodeProcess(pi.hProcess, &childExitCode);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    std::filesystem::remove_all(tmpDir, ec);
    return static_cast<int>(childExitCode);
}

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    // 1. Parse command-line arguments via `CommandLineToArgvW`
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (argv) {
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        ::LocalFree(argv);
    }

    // Fast automated self-check & `--elevated-msi-worker` flags
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--elevated-msi-worker") {
            if (i + 1 >= args.size()) return ERROR_BAD_ARGUMENTS;
            return RunElevatedMsiWorkerFromJobFile(std::filesystem::path(args[i + 1]));
        }
        if (args[i] == L"--self-check") {
            const auto hp = rsw::QueryHostPlatform();
            return (hp.majorVersion >= 10) ? 0 : 1;
        }
    }

    // Universal Single-Download Mode (`Mode=offline` x64+ARM64 combined EXE or `Mode=web` Smart CPU Web Stub!)
    {
        const std::uint8_t* univPtr = nullptr;
        std::size_t univSz = 0;
        if (GetEmbeddedRcDataView(rsw::kResIdUniversalManifest, univPtr, univSz) && univSz > 0) {
            return RunUniversalInstallerDispatcher(
                hInstance, nCmdShow, args,
                std::string_view(reinterpret_cast<const char*>(univPtr), univSz));
        }
    }

    // Per-architecture `--extract-msi` flag
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--extract-msi" || args[i] == L"/extract") {
            if (i + 1 >= args.size()) return 2;
            const std::filesystem::path targetDir(args[i + 1]);
            const auto cfg = LoadBrandingConfigFromResource();
            const std::string slug = rsw::WideToUtf8(cfg.productName);
            std::string clean;
            for (char c : slug) {
                if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) clean += c;
                else if (!clean.empty() && clean.back() != '-') clean += '-';
            }
            const auto outMsi = targetDir / (rsw::Utf8ToWide(clean.empty() ? "Product" : clean) + L".msi");
            std::wstring err;
            return ExtractEmbeddedMsiToFile(outMsi, err) ? 0 : 1;
        }
    }

    // Silent CLI install mode (`/s`, `/S`, `/qn`, `--silent`, `/quiet`) & `--export-screenshots`
    bool silentInstall = false;
    std::wstring silentExtraProps;
    std::filesystem::path exportShotDir;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == L"--export-screenshots" && i + 1 < args.size()) {
            exportShotDir = args[i + 1];
            rsw::ui::ForcedThemeOverride() = 1; // Export Dark Mode first, then live-swap via `WM_SETTINGCHANGE`!
            ++i;
        } else if (a == L"/s" || a == L"/S" || a == L"/qn" || a == L"/quiet" || a == L"--silent") {
            silentInstall = true;
        } else if (a.find(L'=') != std::wstring::npos && a.front() != L'-' && a.front() != L'/') {
            if (!silentExtraProps.empty()) silentExtraProps += L" ";
            silentExtraProps += a;
        }
    }

    if (silentInstall) {
        std::error_code ec;
        const auto silentCfg     = LoadBrandingConfigFromResource();
        const auto silentPrereqs = LoadEmbeddedPrerequisitesFromResource();
        const auto tmpDir = std::filesystem::temp_directory_path(ec) /
                            (L"rsw-silent-" + std::to_wstring(::GetCurrentProcessId()));
        std::filesystem::create_directories(tmpDir, ec);

        wchar_t selfExeBuf[MAX_PATH * 4]{};
        ::GetModuleFileNameW(nullptr, selfExeBuf, static_cast<DWORD>(std::size(selfExeBuf)));
        const std::filesystem::path selfDir = std::filesystem::path(selfExeBuf).parent_path();
        const std::string arch = rsw::WideToUtf8(silentCfg.architecture);

        bool anyReboot = false;
        for (const auto& pr : silentPrereqs) {
            if (rsw::IsPrerequisiteSatisfied(pr, arch)) continue;
            const auto prBin = tmpDir / rsw::Utf8ToWide(pr.fileName);
            bool okBin = false;
            std::wstring extErr;
            if (pr.distribution == "embedded" && pr.embeddedResId > 0) {
                okBin = ExtractEmbeddedResourceToFile(pr.embeddedResId, prBin, extErr);
            } else {
                if (pr.distribution == "side-by-side") {
                    const auto sideP = selfDir / L"Redist" / rsw::Utf8ToWide(pr.fileName);
                    if (std::filesystem::exists(sideP, ec)) {
                        std::filesystem::copy_file(sideP, prBin, std::filesystem::copy_options::overwrite_existing, ec);
                        okBin = std::filesystem::exists(prBin, ec);
                    }
                }
                if (!okBin && !pr.downloadUrl.empty()) {
                    std::string dlErr;
                    okBin = rsw::DownloadFileViaWinHttp(pr.downloadUrl, prBin, nullptr, nullptr, dlErr);
                    if (okBin && !pr.sha256Hex.empty()) {
                        std::string hex;
                        if (!rsw::ComputeFileSha256Hex(prBin, hex) || !rsw::EqualsIgnoreCase(hex, pr.sha256Hex)) okBin = false;
                    }
                }
            }
            if (!okBin) {
                if (pr.optional) continue;
                std::filesystem::remove_all(tmpDir, ec);
                return 9010;
            }
            std::uint32_t rawCode = 0;
            std::string runErr;
            const auto outc = rsw::ExecuteStagedPrerequisiteInstaller(pr, prBin, rawCode, runErr);
            if (outc == rsw::PrereqExitOutcome::SucceededRebootRequested) anyReboot = true;
            else if (outc == rsw::PrereqExitOutcome::Failed && !pr.optional) {
                std::filesystem::remove_all(tmpDir, ec);
                return static_cast<int>(rawCode ? rawCode : ERROR_INSTALL_FAILURE);
            }
        }

        const auto tmpMsi = tmpDir / L"Product.msi";
        std::wstring err;
        if (!ExtractEmbeddedMsiToFile(tmpMsi, err)) {
            std::filesystem::remove_all(tmpDir, ec);
            return 1619;
        }
        ::MsiSetInternalUI(INSTALLUILEVEL_NONE, nullptr);
        UINT rc = ::MsiInstallProductW(tmpMsi.wstring().c_str(), silentExtraProps.c_str());
        std::filesystem::remove_all(tmpDir, ec);
        if ((rc == ERROR_SUCCESS && anyReboot) || rc == ERROR_SUCCESS_REBOOT_REQUIRED) {
            return silentCfg.silentRebootCode;
        }
        return static_cast<int>(rc);
    }

    // 2. Initialize COM, Common Controls v6, and built-in OS GDI+ (`System32\gdiplus.dll`)!
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    rsw::ui::ScopedGdiPlusSession gdiPlusSession;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS | ICC_TREEVIEW_CLASSES};
    ::InitCommonControlsEx(&icc);

    SetupWizardApp app{};
    app.cfg         = LoadBrandingConfigFromResource();
    app.prereqSpecs = LoadEmbeddedPrerequisitesFromResource();

    // 3. True Windows 11 Build Gate (`RtlGetVersion` pierces compatibility shims!)
    const auto hostOs = rsw::QueryHostPlatform();
    if (!rsw::MeetsWindowsBuildFloor(app.cfg.minWindowsBuild)) {
        const std::wstring title = app.cfg.productName + L" Setup";
        const std::wstring detail = app.cfg.osLaunchMessage + L"\n\nDetected OS Build: " + std::to_wstring(hostOs.buildNumber);
        ::TaskDialog(nullptr, hInstance, title.c_str(), L"Unsupported Windows Version", detail.c_str(), TDCBF_CLOSE_BUTTON, TD_WARNING_ICON, nullptr);
        ::CoUninitialize();
        return 1603;
    }

    app.installDir     = ComputeDefaultInstallDir(app.cfg);
    app.startMenuGroup = app.cfg.startMenuFolder;
    app.DetectExistingInstallation();
    app.theme.Refresh(app.cfg.accentPrimary, app.cfg.accentSecondary);

    // Load product `.ico` (`#101` stamped by `instbuild.exe`, falling back to `IDI_APPLICATION`)
    app.hAppIconBig = static_cast<HICON>(::LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_RAGGIESOFT_APP_ICON), IMAGE_ICON, 64, 64, LR_DEFAULTCOLOR));
    app.hAppIconSmall = static_cast<HICON>(::LoadImageW(
        hInstance, MAKEINTRESOURCEW(IDI_RAGGIESOFT_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (!app.hAppIconBig) {
        app.hAppIconBig = ::LoadIconW(nullptr, IDI_APPLICATION);
    }

    // Decode optional embedded PNG cover / 16:9 OpenGraph banner (`#204`) via WIC
    {
        const std::uint8_t* artPtr = nullptr;
        std::size_t artSz = 0;
        if (GetEmbeddedRcDataView(rsw::kResIdArtworkPng, artPtr, artSz)) {
            app.hArtworkBmp = rsw::ui::DecodeImageBytesToHBitmap(artPtr, artSz, app.artworkW, app.artworkH);
        }
    }

    // 4. Register top-level window class and center on active monitor!
    constexpr const wchar_t* kWndClass = L"RaggieSoftSetupWizardWindow";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &SetupWizardApp::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = app.hAppIconBig;
    wc.hIconSm       = app.hAppIconSmall ? app.hAppIconSmall : app.hAppIconBig;
    wc.lpszClassName = kWndClass;
    ::RegisterClassExW(&wc);

    const UINT screenDpi = ::GetDpiForSystem();
    const int logW = app.cfg.isAutorun16x9 ? 640 : 620;
    const int logH = app.cfg.isAutorun16x9 ? 480 : 490;
    RECT winRc{0, 0,
               rsw::ui::ScalePx(logW, screenDpi, app.theme.textScalePercent),
               rsw::ui::ScalePx(logH, screenDpi, app.theme.textScalePercent)};
    constexpr DWORD kWinStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    ::AdjustWindowRectExForDpi(&winRc, kWinStyle, FALSE, 0, screenDpi);
    const int totalW = winRc.right - winRc.left;
    const int totalH = winRc.bottom - winRc.top;
    const int posX   = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - totalW) / 2);
    const int posY   = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - totalH) / 2);

    const std::wstring winTitle = app.cfg.productName + L" Setup";
    HWND hWnd = ::CreateWindowExW(
        0, kWndClass, winTitle.c_str(), kWinStyle,
        posX, posY, totalW, totalH,
        nullptr, nullptr, hInstance, &app);

    if (!hWnd) {
        ::CoUninitialize();
        return 1;
    }

    ::ShowWindow(hWnd, nCmdShow);
    ::UpdateWindow(hWnd);

    if (!exportShotDir.empty()) {
        app.alreadyInstalled = false;
        app.selectedAction   = SelectedAction::InstallCustom;

        if (app.cfg.hasLicenseRtf) {
            app.currentPage = WizardPage::License;
            ::SendMessageW(app.hLicAccept, BM_SETCHECK, BST_CHECKED, 0);
            app.UpdateControlVisibilityForPage();
            rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-license.png");
        }

        app.currentPage = WizardPage::SetupType;
        app.UpdateControlVisibilityForPage();
        const bool okType = rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-setup-type.png");

        app.EnsureFeatureTreeLoaded();
        app.currentPage = WizardPage::CustomFeatures;
        app.UpdateControlVisibilityForPage();
        const bool okTree = rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-selection-tree.png");

        // Torture-test `BCM_SETSHIELD` 32-bit ARGB UAC elevation shield on Primary Royal Cobalt button (Dark Mode)!
        app.currentPage = WizardPage::ReadyToInstall;
        app.UpdateControlVisibilityForPage();
        ::SendMessageW(app.hBtnPrimary, BCM_SETSHIELD, 0, TRUE);
        const bool okUacDark = rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-ready-uac.png");

        // Live-test `WM_SETTINGCHANGE(L"ImmersiveColorSet")` hot-swapping from Dark -> Light without restarting!
        rsw::ui::ForcedThemeOverride() = 0;
        ::SendMessageW(hWnd, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
        ::SendMessageW(app.hBtnPrimary, BCM_SETSHIELD, 0, TRUE);
        const bool okUacLight = rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-ready-uac-light.png");

        app.currentPage = WizardPage::SetupType;
        app.UpdateControlVisibilityForPage();
        const bool okLight = rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-setup-type-light.png");

        ::DestroyWindow(hWnd);
        ::CoUninitialize();
        return (okType && okTree && okUacDark && okUacLight && okLight) ? 0 : 2;
    }

    // 5. Accessible keyboard-aware Win32 message loop (`IsDialogMessageW` powers `Tab`, `Shift+Tab`,
    //    `Arrow` keys across subclassed `BS_AUTORADIOBUTTON` cards, `Enter`, `Esc`, and `Alt+Key`!)
    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!::IsDialogMessageW(hWnd, &msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    ::CoUninitialize();
    return 0;
}
