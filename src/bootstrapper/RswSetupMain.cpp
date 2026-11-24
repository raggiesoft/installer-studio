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
#include "../common/RswManifestAst.hpp"
#include "../common/RswPlatform.hpp"
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

// Thread-safe `PostMessageW` notifications from the MSI worker thread -> UI thread
constexpr UINT WM_RSW_PROGRESS_UPDATE = WM_APP + 10;
constexpr UINT WM_RSW_STATUS_TEXT     = WM_APP + 11;
constexpr UINT WM_RSW_INSTALL_DONE    = WM_APP + 12;

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

    // `License` & `AutorunReadMe` controls
    IDC_LIC_RICHEDIT     = 1020,
    IDC_LIC_ACCEPT_CHK   = 1021,
    IDC_README_EDIT      = 1022,

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

/// Every multi-step screen supported across Standard Glass & `16:9` CD-Autorun modes!
enum class WizardPage {
    Welcome,          // Glass Welcome (`Hero Brand Card` + Intro) OR `16:9` Autorun 2x2 Menu
    License,          // High-contrast RichEdit license + accept checkbox
    AutorunReadMe,    // `16:9` Autorun Edition Liner Notes with `144x74` mini-OG cover header
    SetupType,        // 3-card `48x48` vector picker: Express / Compact / Custom
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

/// Writes the embedded `Product.msi` resource (`#201`) to a file on disk.
bool ExtractEmbeddedMsiToFile(const std::filesystem::path& destFile, std::wstring& outError) {
    const std::uint8_t* ptr = nullptr;
    std::size_t sz = 0;
    if (!GetEmbeddedRcDataView(rsw::kResIdEmbeddedMsi, ptr, sz)) {
        outError = L"This setup stub does not contain an embedded MSI package yet.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destFile.parent_path(), ec);
    std::ofstream out(destFile, std::ios::binary);
    if (!out) {
        outError = L"Could not write temporary setup package to:\n" + destFile.wstring();
        return false;
    }
    out.write(reinterpret_cast<const char*>(ptr), static_cast<std::streamsize>(sz));
    return out.good();
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

    // Subclassed accessible radio cards (`unique_ptr` keeps pointer addresses stable for `SetWindowSubclass`!)
    std::vector<std::unique_ptr<OptionCardVisual>> cardVisuals;

    // Standard Glass & Autorun child controls
    HWND hModeExpress = nullptr, hModeCompact = nullptr, hModeCustom = nullptr;
    HWND hMaintModify = nullptr, hMaintRepair = nullptr, hMaintRemove = nullptr;
    HWND hRepairQuick = nullptr, hRepairFull  = nullptr;
    HWND hUninstCache = nullptr, hUninstPrefs = nullptr;
    HWND hLicRich     = nullptr, hLicAccept   = nullptr, hReadMeEdit = nullptr;
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

    // MSI 4-part progress state machine (`INSTALLMESSAGE_PROGRESS`)
    long long               msiTotalTicks     = 1000;
    long long               msiCurTicks       = 0;
    long long               msiStepTicks      = 1;
    bool                    msiForward        = true;

    ~SetupWizardApp() {
        if (workerThread.joinable()) workerThread.join();
        DestroyFonts();
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

    /// Checks whether a product sharing `cfg.upgradeCode` is already installed on this PC!
    void DetectExistingInstallation() {
        const std::uint8_t* msiPtr = nullptr;
        std::size_t msiSz = 0;
        if (GetEmbeddedRcDataView(rsw::kResIdEmbeddedMsi, msiPtr, msiSz) && msiSz > 0) {
            estimatedReqBytes = std::max<std::uint64_t>(2u * 1024u * 1024u, static_cast<std::uint64_t>(msiSz) * 5u / 2u);
        }

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
            L"C&ustom Setup (Choose Folder && Shortcuts)", cfg.customDesc);
        LinkSiblingGroup({hModeExpress, hModeCompact, hModeCustom});
        ::SendMessageW(hModeExpress, BM_SETCHECK, BST_CHECKED, 0);

        // 2. Standard Glass `Maintenance` 3-Card Radio Group (`Blocks`, `ShieldCheck`, `Uninstall`!)
        hMaintModify = CreateOptionRadioCard(
            IDC_MAINT_MODIFY, true, false,
            rsw::ui::GlassGlyph::Blocks, rsw::ui::brand::kVioletTune, rsw::ui::brand::kSkyCyan,
            L"&Modify or Reconfigure Setup",
            L"Review Start Menu shortcuts, file associations, and installation settings.");
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
        hUninstCache = mkBtn(L"&Also remove temporary cache and diagnostic log files", IDC_UNINST_CACHE_CHK, BS_AUTOCHECKBOX);
        hUninstPrefs = mkBtn(L"A&lso remove saved window layout and user preferences", IDC_UNINST_PREFS_CHK, BS_AUTOCHECKBOX);

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
        hLicAccept = mkBtn(L"I have &read and accept the terms in the License Agreement", IDC_LIC_ACCEPT_CHK, BS_AUTOCHECKBOX);

        hReadMeEdit = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", EffectiveReadMeText().c_str(),
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_README_EDIT)), hInst, nullptr);

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
        hDeskScChk    = mkBtn(L"&Create a Desktop shortcut for quick access", IDC_DESK_SC_CHK, BS_AUTOCHECKBOX);
        hFileAssocChk = mkBtn(L"&Register supported file types with Windows Explorer", IDC_FILE_ASSOC_CHK, BS_AUTOCHECKBOX);
        ::SendMessageW(hDeskScChk,    BM_SETCHECK, BST_CHECKED, 0);
        ::SendMessageW(hFileAssocChk, BM_SETCHECK, BST_CHECKED, 0);

        // 9. Progress & Completion Controls
        hProgStatus = mkStatic(L"Preparing Windows Installer transaction...", IDC_PROG_STATUS);
        hProgBar    = ::CreateWindowExW(
            0, PROGRESS_CLASSW, nullptr, WS_CHILD | PBS_SMOOTH,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROG_BAR)), hInst, nullptr);
        ::SendMessageW(hProgBar, PBM_SETRANGE32, 0, 1000);

        hFinLaunch  = mkBtn(cfg.finishLaunchText.c_str(), IDC_FIN_LAUNCH_CHK, BS_AUTOCHECKBOX);
        ::SendMessageW(hFinLaunch, BM_SETCHECK, (cfg.offerLaunch && !cfg.mainExecutable.empty()) ? BST_CHECKED : BST_UNCHECKED, 0);
        hFinViewLog = mkBtn(L"&View Setup Log", IDC_FIN_VIEWLOG_BTN, BS_PUSHBUTTON);

        // 10. Universal Glass Footer Navigation Bar
        hBtnBack    = mkBtn(L"< &Back",                IDC_NAV_BACK,    BS_PUSHBUTTON);
        hBtnPrimary = mkBtn(cfg.installBtnText.c_str(), IDC_NAV_PRIMARY, BS_DEFPUSHBUTTON);
        hBtnCancel  = mkBtn(L"Cancel",                 IDC_NAV_CANCEL,  BS_PUSHBUTTON);
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
        for (HWND h : {hLicRich, hLicAccept, hReadMeEdit, hDirEdit, hDirBrowse,
                       hSmEdit, hDeskScChk, hFileAssocChk, hUninstCache, hUninstPrefs,
                       hProgStatus, hFinLaunch, hFinViewLog, hBtnBack, hBtnCancel}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBody), TRUE);
        }
        ApplyRichEditReadableColors();

        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = rc.right - rc.left;
        const int H = rc.bottom - rc.top;
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        const int footerH = S(58);
        const int footerY = H - footerH;
        const int btnY    = footerY + (footerH - S(32)) / 2;

        // Universal Footer Buttons (`< &Back` moves to top-left circular Aero glyph when `cfg.isAeroWizard`!)
        if (cfg.isAeroWizard) {
            ::SetWindowTextW(hBtnBack, L"\x2190 &Back");
            ::MoveWindow(hBtnBack, S(12), S(5), S(34), S(28), TRUE);
        } else {
            ::SetWindowTextW(hBtnBack, L"< &Back");
            ::MoveWindow(hBtnBack, W - S(320), btnY, S(94), S(32), TRUE);
        }
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

        show(hLicRich,    pLic);
        show(hLicAccept,  pLic);
        show(hReadMeEdit, pReadMe);

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

        const bool canGoBack = pLic || pReadMe || pSetupType || pDir || pShort || pReady || pRepair || pSafeUn;
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
        } else if (pSetupType || pShort || pMaint) {
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
        ::SendMessageW(hBtnPrimary, BCM_SETSHIELD, 0, showShield ? TRUE : FALSE);

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
                DWORD cch = static_cast<DWORD>(std::size(descBuf));
                if (::MsiRecordGetStringW(hRecord, 2, descBuf, &cch) == ERROR_SUCCESS && descBuf[0] != L'\0') {
                    std::lock_guard<std::mutex> lk(self->msgMutex);
                    self->latestStatusText = descBuf;
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

    void StartMsiWorkerTransaction() {
        SyncEditableFieldsFromControls();
        currentPermille     = 0;
        lastAnnouncedDecile = -1;
        ::SendMessageW(hProgBar, PBM_SETPOS, 0, 0);

        currentPage = WizardPage::Installing;
        UpdateControlVisibilityForPage();
        ::InvalidateRect(hWnd, nullptr, TRUE);

        std::error_code ec;
        tempWorkDir = std::filesystem::temp_directory_path(ec) /
                      (L"setupwizard-" + std::to_wstring(::GetCurrentProcessId()));
        std::filesystem::create_directories(tempWorkDir, ec);
        tempMsiPath = tempWorkDir / L"Product.msi";
        msiLogPath  = std::filesystem::temp_directory_path(ec) /
                      (L"Setup-" + std::to_wstring(::GetCurrentProcessId()) + L".log");

        std::wstring extractErr;
        if (selectedAction != SelectedAction::MaintUninstall &&
            selectedAction != SelectedAction::MaintQuickRepair &&
            !ExtractEmbeddedMsiToFile(tempMsiPath, extractErr)) {
            capturedErrorText = extractErr;
            finalMsiResult    = ERROR_INSTALL_PACKAGE_OPEN_FAILED;
            OnTransactionCompleted();
            return;
        }

        const bool wantDesktopSc = (::SendMessageW(hDeskScChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
        const bool wantFileAssoc = (::SendMessageW(hFileAssocChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
        const bool wipeCache     = (::SendMessageW(hUninstCache, BM_GETCHECK, 0, 0) == BST_CHECKED);
        const bool wipePrefs     = (::SendMessageW(hUninstPrefs, BM_GETCHECK, 0, 0) == BST_CHECKED);

        cancelRequested.store(false);
        installRunning.store(true);

        if (workerThread.joinable()) workerThread.join();
        workerThread = std::thread([this, wantDesktopSc, wantFileAssoc, wipeCache, wipePrefs]() {
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
                if (ExtractEmbeddedMsiToFile(tempMsiPath, err)) {
                    rc = ::MsiInstallProductW(tempMsiPath.wstring().c_str(), L"REINSTALL=ALL REINSTALLMODE=vomus");
                } else {
                    rc = ::MsiReinstallProductW(
                        installedProductCode.c_str(),
                        REINSTALLMODE_FILEREPLACE | REINSTALLMODE_SHORTCUT | REINSTALLMODE_MACHINEDATA | REINSTALLMODE_USERDATA);
                }
            } else {
                std::wstring props = L"INSTALLDIR=\"" + installDir + L"\"";
                if (!startMenuGroup.empty()) {
                    props += L" STARTMENUFOLDER=\"" + startMenuGroup + L"\"";
                }
                props += wantDesktopSc ? L" DESKTOP_SHORTCUT=1" : L" DESKTOP_SHORTCUT=0";
                props += wantFileAssoc ? L" REGISTER_ASSOC=1"   : L" REGISTER_ASSOC=0";
                if (selectedAction == SelectedAction::InstallCompact) {
                    props += L" ADDLOCAL=Main";
                }
                rc = ::MsiInstallProductW(tempMsiPath.wstring().c_str(), props.c_str());
            }

            ::MsiSetExternalUIRecord(prevHandler, 0, nullptr, nullptr);
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
                    NavigateTo(WizardPage::CustomFolder, hDirEdit);
                } else {
                    NavigateTo(WizardPage::ReadyToInstall, hBtnPrimary);
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
                NavigateTo(WizardPage::ReadyToInstall, hBtnPrimary);
                break;

            case WizardPage::ReadyToInstall:
            case WizardPage::RepairMode:
            case WizardPage::SafeUninstall:
                StartMsiWorkerTransaction();
                break;

            case WizardPage::Maintenance:
                if (::SendMessageW(hMaintModify, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    selectedAction = SelectedAction::InstallCustom;
                    NavigateTo(WizardPage::CustomShortcuts, hSmEdit);
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
            case WizardPage::CustomFolder:
                NavigateTo(cfg.isAutorun16x9 ? WizardPage::Welcome : WizardPage::SetupType);
                break;
            case WizardPage::CustomShortcuts:
                NavigateTo(alreadyInstalled ? WizardPage::Maintenance : WizardPage::CustomFolder);
                break;
            case WizardPage::ReadyToInstall:
                NavigateTo((selectedAction == SelectedAction::InstallCustom) ? WizardPage::CustomShortcuts : WizardPage::SetupType);
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
        rsw::ui::GlassGlyph glyph;
        COLORREF            badgeColor;
    };

    StepHeaderInfo CurrentStepHeaderInfo() const noexcept {
        switch (currentPage) {
            case WizardPage::Welcome:         return {L"OFFICIAL SETUP WIZARD",    rsw::ui::GlassGlyph::Sparkle,     rsw::ui::brand::kRaggieBlue};
            case WizardPage::License:         return {L"LICENSE AGREEMENT",        rsw::ui::GlassGlyph::ShieldCheck, rsw::ui::brand::kEmeraldOk};
            case WizardPage::AutorunReadMe:   return {L"EDITION LINER NOTES",      rsw::ui::GlassGlyph::ReadMeBook,  rsw::ui::brand::kSkyCyan};
            case WizardPage::SetupType:       return {L"CHOOSE SETUP MODE",        rsw::ui::GlassGlyph::Sliders,     rsw::ui::brand::kVioletTune};
            case WizardPage::CustomFolder:    return {L"DESTINATION FOLDER",       rsw::ui::GlassGlyph::Folder,      rsw::ui::brand::kRaggieBlue};
            case WizardPage::CustomShortcuts: return {L"SHORTCUTS & INTEGRATION",  rsw::ui::GlassGlyph::PinGrid,     rsw::ui::brand::kSkyCyan};
            case WizardPage::ReadyToInstall:  return {L"READY TO APPLY CHANGES",   rsw::ui::GlassGlyph::Play,        rsw::ui::brand::kEmeraldOk};
            case WizardPage::Maintenance:     return {L"MAINTENANCE WIZARD",       rsw::ui::GlassGlyph::Gear,        rsw::ui::brand::kVioletTune};
            case WizardPage::RepairMode:      return {L"REPAIR & VERIFY OPTIONS",  rsw::ui::GlassGlyph::Reinstall,   rsw::ui::brand::kRaggieBlue};
            case WizardPage::SafeUninstall:   return {L"SAFE UNINSTALL",           rsw::ui::GlassGlyph::Uninstall,   rsw::ui::brand::kCoralRose};
            case WizardPage::Installing:      return {L"APPLYING SYSTEM CHANGES",  rsw::ui::GlassGlyph::Gear,        rsw::ui::brand::kRaggieBlue};
            case WizardPage::Finished:
                return (finalMsiResult == ERROR_SUCCESS || finalMsiResult == ERROR_SUCCESS_REBOOT_REQUIRED)
                    ? StepHeaderInfo{L"SETUP COMPLETE",    rsw::ui::GlassGlyph::ShieldCheck, rsw::ui::brand::kEmeraldOk}
                    : StepHeaderInfo{L"SETUP INTERRUPTED", rsw::ui::GlassGlyph::Uninstall,   rsw::ui::brand::kCoralRose};
        }
        return {L"OFFICIAL SETUP WIZARD", rsw::ui::GlassGlyph::Sparkle, rsw::ui::brand::kRaggieBlue};
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
                memDc, g, W, dpi, theme, winCap, hdrInfo.kickerSuffix,
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

                const bool deskOn = (::SendMessageW(hDeskScChk, BM_GETCHECK, 0, 0) == BST_CHECKED);
                std::wstring rows[4][2] = {
                    {L"Setup Mode:",         modeName},
                    {L"Destination Folder:", installDir},
                    {L"Install Scope:",      cfg.perMachine ? L"All Users (Administrator UAC)" : L"Current User (No UAC Needed)"},
                    {L"Shortcuts:",          L"Start Menu (" + startMenuGroup + L")" + (deskOn ? L" + Desktop" : L"")}
                };

                int ry = sumCard.top + S(46);
                for (int i = 0; i < 4; ++i) {
                    ::SelectObject(memDc, hFontBold);
                    ::SetTextColor(memDc, theme.textSecondary);
                    RECT kRc{sumCard.left + S(16), ry, sumCard.left + S(172), ry + S(28)};
                    ::DrawTextW(memDc, rows[i][0].c_str(), -1, &kRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

                    ::SelectObject(memDc, hFontBody);
                    ::SetTextColor(memDc, theme.textPrimary);
                    RECT vRc{sumCard.left + S(176), ry, sumCard.right - S(16), ry + S(28)};
                    ::DrawTextW(memDc, rows[i][1].c_str(), -1, &vRc, DT_LEFT | DT_SINGLELINE | DT_PATH_ELLIPSIS | DT_NOPREFIX);
                    ry += S(34);
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
                self->theme.Refresh(self->cfg.accentPrimary, self->cfg.accentSecondary);
                rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                self->ApplyFontsAndLayout();
                return 0;

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

            case WM_RSW_INSTALL_DONE:
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

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    // 1. Parse command-line arguments via `CommandLineToArgvW`
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (argv) {
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        ::LocalFree(argv);
    }

    // Fast automated self-check & `--extract-msi` flags used by CI & enterprise IT admins
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--self-check") {
            const auto hp = rsw::QueryHostPlatform();
            return (hp.majorVersion >= 10) ? 0 : 1;
        }
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

    // Silent CLI install mode (`/s`, `/qn`, `--silent`, `/quiet`)
    bool silentInstall = false;
    std::wstring silentExtraProps;
    for (const auto& a : args) {
        if (a == L"/s" || a == L"/S" || a == L"/qn" || a == L"/quiet" || a == L"--silent") {
            silentInstall = true;
        } else if (a.find(L'=') != std::wstring::npos && a.front() != L'-' && a.front() != L'/') {
            if (!silentExtraProps.empty()) silentExtraProps += L" ";
            silentExtraProps += a;
        }
    }

    if (silentInstall) {
        std::error_code ec;
        const auto tmpDir = std::filesystem::temp_directory_path(ec) /
                            (L"rsw-silent-" + std::to_wstring(::GetCurrentProcessId()));
        const auto tmpMsi = tmpDir / L"Product.msi";
        std::wstring err;
        if (!ExtractEmbeddedMsiToFile(tmpMsi, err)) return 1619;
        ::MsiSetInternalUI(INSTALLUILEVEL_NONE, nullptr);
        const UINT rc = ::MsiInstallProductW(tmpMsi.wstring().c_str(), silentExtraProps.c_str());
        std::filesystem::remove_all(tmpDir, ec);
        return static_cast<int>(rc);
    }

    // 2. Initialize COM, Common Controls v6, and built-in OS GDI+ (`System32\gdiplus.dll`)!
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    rsw::ui::ScopedGdiPlusSession gdiPlusSession;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    ::InitCommonControlsEx(&icc);

    SetupWizardApp app{};
    app.cfg = LoadBrandingConfigFromResource();

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
