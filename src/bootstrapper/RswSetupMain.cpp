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
//        - Older Windows setup wizards often used unlabeled `15px` radio dots next to
//          non-focusable static text, failing screen readers and target-size
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
#include "../common/RswRaggieScript.hpp"
#include "../common/RswRaggieScriptVm.hpp"
#include "../common/RswVersion.hpp"
#include "../common/RswWcagContrast.hpp"
#include "../common/RswWizardStyles.hpp"
#include "RswCompactAndSuiteUi.hpp"

#include <msi.h>
#include <msiquery.h>
#include <richedit.h>
#include <shellapi.h>
#include <shldisp.h>
#include <shlobj.h>
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
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

// Thread-safe `PostMessageW` / `SendMessageW` notifications from worker thread -> UI thread
constexpr UINT WM_RSW_PROGRESS_UPDATE = WM_APP + 10;
constexpr UINT WM_RSW_STATUS_TEXT     = WM_APP + 11;
constexpr UINT WM_RSW_INSTALL_DONE    = WM_APP + 12;
constexpr UINT WM_RSW_ASK_RETRY_DL    = WM_APP + 13;
constexpr UINT WM_RSW_REQUEST_UAC     = WM_APP + 14;
constexpr UINT WM_RSW_LOCK_CANCEL     = WM_APP + 15;
constexpr UINT WM_RSW_QUEUE_STATE     = WM_APP + 16;
constexpr UINT IDM_SYS_ABOUT_SETUP    = 0x1F10u;

// ============================================================================
// WINDOWS 7+ / WINDOWS 11 TASKBAR PROGRESS CONTROLLER (`ITaskbarList3`)
// ============================================================================
// PLAIN-ENGLISH TEACHING OVERVIEW:
//   Starting in Windows 7 (`explorer.exe`), every top-level taskbar button can
//   render a native built-in progress bar (`ITaskbarList3` via `CLSID_TaskbarList`):
//     - `TBPF_INDETERMINATE`: Pulsing marquee while preparing or awaiting UAC.
//     - `TBPF_NORMAL`:        Smooth green fill (`0..1000` permille) during install.
//     - `TBPF_PAUSED`:        Amber/yellow state when paused for a prompt (e.g. UAC or retry).
//     - `TBPF_ERROR`:         Red alert state if an installation error occurs.
//     - `TBPF_NOPROGRESS`:    Standard taskbar icon when idle or cleanly finished.
//   Two critical Win32 details handled here:
//     1. When Setup runs elevated (High Integrity), User Interface Privilege Isolation
//        (UIPI) blocks `TaskbarButtonCreated` from Medium-Integrity `explorer.exe` unless
//        we explicitly whitelist that message via `ChangeWindowMessageFilterEx(MSGFLT_ALLOW)`.
//     2. `pList->Release()` MUST run in `WM_DESTROY` (before `CoUninitialize()`), never
//        after the STA COM apartment has already torn down at the end of `wWinMain`!
struct TaskbarProgressHelper {
    ITaskbarList3* pList         = nullptr;
    UINT           wmCreated     = 0;
    TBPFLAG        lastState     = TBPF_NOPROGRESS;
    ULONGLONG      lastCompleted = 0;
    ULONGLONG      lastTotal     = 1000;

    void AttachWindow(HWND hWnd) noexcept {
        if (wmCreated == 0) {
            wmCreated = ::RegisterWindowMessageW(L"TaskbarButtonCreated");
        }
        if (hWnd && wmCreated != 0) {
            CHANGEFILTERSTRUCT cfs{sizeof(cfs), 0};
            ::ChangeWindowMessageFilterEx(hWnd, wmCreated, MSGFLT_ALLOW, &cfs);
        }
    }

    bool EnsureInitialized() noexcept {
        if (pList) return true;
        ITaskbarList3* raw = nullptr;
        const HRESULT hr = ::CoCreateInstance(
            CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw));
        if (SUCCEEDED(hr) && raw) {
            if (SUCCEEDED(raw->HrInit())) {
                pList = raw;
                return true;
            }
            raw->Release();
        }
        return false;
    }

    void Release() noexcept {
        if (pList) {
            pList->Release();
            pList = nullptr;
        }
    }

    ~TaskbarProgressHelper() {
        Release();
    }

    void ApplyCurrent(HWND hWnd) noexcept {
        if (!hWnd || !EnsureInitialized()) return;
        pList->SetProgressState(hWnd, lastState);
        if (lastState == TBPF_NORMAL || lastState == TBPF_PAUSED || lastState == TBPF_ERROR) {
            pList->SetProgressValue(hWnd, lastCompleted, std::max<ULONGLONG>(1, lastTotal));
        }
    }

    void SetNoProgress(HWND hWnd) noexcept {
        lastState     = TBPF_NOPROGRESS;
        lastCompleted = 0;
        if (hWnd && EnsureInitialized()) {
            pList->SetProgressState(hWnd, TBPF_NOPROGRESS);
        }
    }

    void SetIndeterminate(HWND hWnd) noexcept {
        lastState = TBPF_INDETERMINATE;
        if (hWnd && EnsureInitialized()) {
            pList->SetProgressState(hWnd, TBPF_INDETERMINATE);
        }
    }

    void SetNormalPermille(HWND hWnd, int permille) noexcept {
        lastState     = TBPF_NORMAL;
        lastCompleted = static_cast<ULONGLONG>(std::clamp(permille, 0, 1000));
        lastTotal     = 1000;
        if (hWnd && EnsureInitialized()) {
            pList->SetProgressState(hWnd, TBPF_NORMAL);
            pList->SetProgressValue(hWnd, lastCompleted, lastTotal);
        }
    }

    void SetPaused(HWND hWnd) noexcept {
        lastState = TBPF_PAUSED;
        if (hWnd && EnsureInitialized()) {
            const ULONGLONG shown = std::max<ULONGLONG>(40, lastCompleted);
            pList->SetProgressValue(hWnd, shown, lastTotal);
            pList->SetProgressState(hWnd, TBPF_PAUSED);
        }
    }

    void SetError(HWND hWnd) noexcept {
        lastState = TBPF_ERROR;
        if (hWnd && EnsureInitialized()) {
            const ULONGLONG errVal = std::max<ULONGLONG>(100, lastCompleted);
            pList->SetProgressValue(hWnd, errVal, lastTotal);
            pList->SetProgressState(hWnd, TBPF_ERROR);
        }
    }

    bool HandleMessage(HWND hWnd, UINT msg) noexcept {
        if (wmCreated != 0 && msg == wmCreated) {
            // `explorer.exe` created or recreated our taskbar button — re-bind COM & sync state!
            Release();
            ApplyCurrent(hWnd);
            return true;
        }
        return false;
    }
};

// Accessible Win32 Child Control IDs (supporting `Tab`, `Arrow`, & `Alt+Mnemonic` traversal)
enum ControlId : int {
    // `SetupType` 1..4-card radio group (`Express/Compact/Custom` OR `Typical/Compact/Complete/Custom`!)
    IDC_MODE_EXPRESS     = 1001,
    IDC_MODE_COMPACT     = 1002,
    IDC_MODE_CUSTOM      = 1003,
    IDC_MODE_CARD_4      = 1004,
    IDC_ONECLICK_CUST    = 1008,

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
    InstallComplete,
    InstallCustom,
    MaintModify,
    MaintQuickRepair,
    MaintFullReinstall,
    MaintUninstall
};

/// Parsed UTF-8 `[Branding]` resource (`#202`) stamped into `<Product>-setup.exe` by `instbuild.exe`.
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
    rsw::WizardStyleKind wizardStyle = rsw::WizardStyleKind::StandardGlass;
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
    std::string  setupTypePreset    = "express_custom";
    std::string  requestedExecLevel = "asInvoker";
    std::string  externalMsiName;
    bool         systemMenuAbout    = false;
    std::vector<rsw::SetupTypeOptionSpec> setupTypes;
    std::vector<rsw::BillboardSlideSpec>  billboards;
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
    cfg.wizardStyle      = rsw::ParseWizardStyleKind(kv["WizardLayout"]);
    cfg.isAutorun16x9    = (cfg.wizardStyle == rsw::WizardStyleKind::Autorun16x9);
    cfg.isAeroWizard     = (cfg.wizardStyle == rsw::WizardStyleKind::AeroWizard);
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
    if (!kv["SetupTypePreset"].empty()) {
        cfg.setupTypePreset = rsw::TrimAscii(kv["SetupTypePreset"]);
    }
    if (!kv["RequestedExecutionLevel"].empty()) {
        cfg.requestedExecLevel = std::string(rsw::NormalizeRequestedExecutionLevel(kv["RequestedExecutionLevel"]));
    }
    cfg.externalMsiName = rsw::TrimAscii(kv["ExternalMsiFileName"]);

    // Parse optional `[SetupType.<id>]` custom setup type cards (`0..4`)
    std::vector<rsw::SetupTypeOptionSpec> customTypes;
    const int stCount = std::clamp(std::atoi(kv["SetupTypeCount"].c_str()), 0, 8);
    for (int i = 0; i < stCount; ++i) {
        const std::string pfx = "SetupType_" + std::to_string(i) + "_";
        rsw::SetupTypeOptionSpec st{};
        st.id                  = rsw::TrimAscii(kv[pfx + "Id"]);
        st.titleMnemonic       = getW((pfx + "Title").c_str(), L"&" + rsw::Utf8ToWide(st.id.empty() ? "Standard" : st.id));
        st.description         = getW((pfx + "Description").c_str(), L"");
        st.tier                = rsw::AsciiLower(rsw::TrimAscii(kv[pfx + "Tier"]));
        if (st.tier.empty()) st.tier = "typical";
        st.explicitFeaturesCsv = rsw::TrimAscii(kv[pfx + "Features"]);
        st.glyph               = rsw::ParseSetupGlyphName(kv[pfx + "Glyph"], rsw::ui::GlassGlyph::Bolt);
        st.recommended         = rsw::EqualsIgnoreCase(kv[pfx + "Recommended"], "true");
        if (st.tier == "minimal") {
            st.rimRgb = rsw::ui::brand::kRaggieBlue; st.accentRgb = rsw::ui::brand::kSkyCyan;
        } else if (st.tier == "complete") {
            st.rimRgb = rsw::ui::brand::kEmeraldOk;  st.accentRgb = rsw::ui::brand::kWarmGold;
        } else if (st.tier == "custom") {
            st.rimRgb = rsw::ui::brand::kVioletTune; st.accentRgb = rsw::ui::brand::kWarmGold;
        } else {
            st.rimRgb = rsw::ui::brand::kSkyCyan;    st.accentRgb = rsw::ui::brand::kWarmGold;
        }
        customTypes.push_back(std::move(st));
    }
    cfg.setupTypes = rsw::BuildEffectiveSetupTypeOptions(
        cfg.setupTypePreset, customTypes, cfg.expressDesc, cfg.compactDesc, cfg.customDesc);
    cfg.systemMenuAbout = rsw::EqualsIgnoreCase(rsw::TrimAscii(kv["SystemMenuAbout"]), "true");

    // Parse optional `[Billboard.<id>]` installation feature showcase cards
    const int bbCount = std::clamp(std::atoi(kv["BillboardCount"].c_str()), 0, 16);
    for (int i = 0; i < bbCount; ++i) {
        const std::string pfx = "Billboard_" + std::to_string(i) + "_";
        rsw::BillboardSlideSpec bb{};
        bb.headline = getW((pfx + "Headline").c_str(), L"");
        bb.body     = getW((pfx + "Body").c_str(), L"");
        bb.glyph    = rsw::ParseSetupGlyphName(kv[pfx + "Glyph"], rsw::ui::GlassGlyph::Sparkle);
        if (!bb.headline.empty()) {
            cfg.billboards.push_back(std::move(bb));
        }
    }
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

/// Loads embedded `.rswpak` on-demand feature pack specs (`#215`) stamped into `Setup.exe`.
std::vector<rsw::FeaturePackRuntimeEntry> LoadEmbeddedFeaturePacksFromResource() {
    const std::uint8_t* ptr = nullptr;
    std::size_t sz = 0;
    if (!GetEmbeddedRcDataView(rsw::kResIdFeaturePackManifest, ptr, sz) || sz == 0) {
        return {};
    }
    return rsw::ParseFeaturePackManifestIni(std::string_view(reinterpret_cast<const char*>(ptr), sz));
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
    if (detail.find(L'\n') != std::wstring::npos) {
        return base + L"...\r\n" + detail;
    }
    return base + L": " + detail;
}

/// Condenses multiline `ActionData` (`"Copying new files...\r\nFile: Foo.exe\r\nDirectory: ..."`)
/// down to a crisp single-line status (`"Copying new files: Foo.exe"`) for compact retro
/// `Enable(PROGRESS)`, `OneClick`, and `PassiveToast` progress meters!
std::wstring CompactSingleLineProgressText(const std::wstring& raw) {
    const auto nl1 = raw.find_first_of(L"\r\n");
    if (nl1 == std::wstring::npos) return raw;
    std::wstring head = raw.substr(0, nl1);
    while (!head.empty() && head.back() == L'.') head.pop_back();

    std::size_t start2 = raw.find_first_not_of(L"\r\n", nl1);
    if (start2 == std::wstring::npos) return head + L"...";
    const auto nl2 = raw.find_first_of(L"\r\n", start2);
    std::wstring line2 = (nl2 == std::wstring::npos) ? raw.substr(start2) : raw.substr(start2, nl2 - start2);
    for (const wchar_t* pfx : {L"File: ", L"Folder: ", L"Shortcut: ", L"Key: ", L"Feature: ", L"Application: "}) {
        const std::size_t pLen = std::wcslen(pfx);
        if (line2.compare(0, pLen, pfx) == 0) {
            line2.erase(0, pLen);
            break;
        }
    }
    return line2.empty() ? (head + L"...") : (head + L": " + line2);
}

/// Formats estimated remaining wall-clock time (`"Time remaining: 1 minute 24 seconds"`)
/// once sufficient progress (`permille >= 35`) and elapsed duration (`>= 2000ms`) exist.
std::wstring FormatEstimatedTimeRemaining(ULONGLONG startTickMs, int permille) {
    if (startTickMs == 0 || permille < 35 || permille >= 995) return {};
    const ULONGLONG elapsedMs = ::GetTickCount64() - startTickMs;
    if (elapsedMs < 2000) return {};
    const double frac      = static_cast<double>(permille) / 1000.0;
    const double remSecRaw = (static_cast<double>(elapsedMs) / 1000.0) * ((1.0 - frac) / frac);
    const long long remSec = std::clamp<long long>(static_cast<long long>(remSecRaw + 0.5), 1LL, 86400LL);
    const long long mins   = remSec / 60;
    const long long secs   = remSec % 60;
    std::wstring out = L"Time remaining: ";
    if (mins > 0) {
        out += std::to_wstring(mins) + (mins == 1 ? L" minute " : L" minutes ");
    }
    out += std::to_wstring(secs) + (secs == 1 ? L" second" : L" seconds");
    return out;
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

/// Checks whether the current process already possesses active Administrator privileges—accounting
/// for BOTH standard UAC elevation (`TokenIsElevated`) AND UAC-disabled (`EnableLUA = 0`) Admin logins
/// via `CheckTokenMembership(BUILTIN\Administrators)`!
bool HasCurrentProcessAdminRights() noexcept {
    return rsw::QueryProcessPrivilegeState().HasEffectiveAdminRights();
}

/// Launches the installed application on the `Finished` page safely in the interactive user's
/// non-elevated Explorer desktop token!
/// Why does this matter in `v0.5.1`?
///   - Under default `RequestedExecutionLevel = asInvoker`, `SetupWizard.exe` is already un-elevated,
///     so `ShellExecuteW` starts the app directly as the standard user.
///   - When an author opts into `RequestedExecutionLevel = requireAdministrator` or `highestAvailable`,
///     `SetupWizard.exe` itself runs elevated (`isTokenElevated == true`). Delegating launch through
///     Windows Explorer's desktop shell view (`CLSID_ShellWindows` -> `SWC_DESKTOP` -> `IShellDispatch2`)
///     starts the user's app in their normal desktop token so drag-and-drop from Explorer works and
///     first-run `%APPDATA%` files aren't owned by `BUILTIN\Administrators`!
void LaunchInstalledAppDeElevated(const std::filesystem::path& exePath, const std::wstring& workDir) noexcept {
    const auto priv = rsw::QueryProcessPrivilegeState();
    if (!priv.isTokenElevated || !priv.isUacEnabled) {
        ::ShellExecuteW(nullptr, L"open", exePath.wstring().c_str(), nullptr, workDir.c_str(), SW_SHOWNORMAL);
        return;
    }

    bool delegatedViaExplorer = false;
    IShellWindows* pShellWindows = nullptr;
    if (SUCCEEDED(::CoCreateInstance(CLSID_ShellWindows, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(&pShellWindows))) && pShellWindows) {
        VARIANT vtEmpty{};
        ::VariantInit(&vtEmpty);
        long hwndDesktop = 0;
        IDispatch* pDesktopDisp = nullptr;
        if (SUCCEEDED(pShellWindows->FindWindowSW(&vtEmpty, &vtEmpty, SWC_DESKTOP, &hwndDesktop, SWFO_NEEDDISPATCH, &pDesktopDisp)) && pDesktopDisp) {
            IServiceProvider* pSp = nullptr;
            if (SUCCEEDED(pDesktopDisp->QueryInterface(IID_PPV_ARGS(&pSp))) && pSp) {
                IShellBrowser* pBrowser = nullptr;
                if (SUCCEEDED(pSp->QueryService(SID_STopLevelBrowser, IID_PPV_ARGS(&pBrowser))) && pBrowser) {
                    IShellView* pShellView = nullptr;
                    if (SUCCEEDED(pBrowser->QueryActiveShellView(&pShellView)) && pShellView) {
                        IDispatch* pBgDisp = nullptr;
                        if (SUCCEEDED(pShellView->GetItemObject(SVGIO_BACKGROUND, IID_PPV_ARGS(&pBgDisp))) && pBgDisp) {
                            IShellFolderViewDual* pFolderView = nullptr;
                            if (SUCCEEDED(pBgDisp->QueryInterface(IID_PPV_ARGS(&pFolderView))) && pFolderView) {
                                IDispatch* pAppDisp = nullptr;
                                if (SUCCEEDED(pFolderView->get_Application(&pAppDisp)) && pAppDisp) {
                                    IShellDispatch2* pShellDisp = nullptr;
                                    if (SUCCEEDED(pAppDisp->QueryInterface(IID_PPV_ARGS(&pShellDisp))) && pShellDisp) {
                                        BSTR bstrFile = ::SysAllocString(exePath.wstring().c_str());
                                        VARIANT vtArgs{}, vtDir{}, vtVerb{}, vtShow{};
                                        ::VariantInit(&vtArgs);
                                        ::VariantInit(&vtDir);
                                        ::VariantInit(&vtVerb);
                                        ::VariantInit(&vtShow);
                                        vtDir.vt      = VT_BSTR;
                                        vtDir.bstrVal = ::SysAllocString(workDir.c_str());
                                        vtVerb.vt     = VT_BSTR;
                                        vtVerb.bstrVal= ::SysAllocString(L"open");
                                        vtShow.vt     = VT_I4;
                                        vtShow.lVal   = SW_SHOWNORMAL;
                                        if (bstrFile && SUCCEEDED(pShellDisp->ShellExecute(bstrFile, vtArgs, vtDir, vtVerb, vtShow))) {
                                            delegatedViaExplorer = true;
                                        }
                                        if (bstrFile) ::SysFreeString(bstrFile);
                                        ::VariantClear(&vtDir);
                                        ::VariantClear(&vtVerb);
                                        pShellDisp->Release();
                                    }
                                    pAppDisp->Release();
                                }
                                pFolderView->Release();
                            }
                            pBgDisp->Release();
                        }
                        pShellView->Release();
                    }
                    pBrowser->Release();
                }
                pSp->Release();
            }
            pDesktopDisp->Release();
        }
        pShellWindows->Release();
    }

    if (!delegatedViaExplorer) {
        ::ShellExecuteW(nullptr, L"open", exePath.wstring().c_str(), nullptr, workDir.c_str(), SW_SHOWNORMAL);
    }
}

/// Executes an MSI transaction (`MsiInstallProductW`, `MsiConfigureProductExW`, or `MsiReinstallProductW`)
/// inside a **Polite Cross-Product Auto-Queue** on `Global\_MSIExecute`!
/// If another installer (e.g. *StoreTrainer* or Windows Update) is actively installing, this helper
/// pauses calmly instead of crashing with `ERROR_INSTALL_ALREADY_RUNNING` (`1618`), reports who holds
/// the slot when it's a fellow RaggieSoft installer, keeps `Cancel` responsive, and auto-resumes!
template <typename CancelPredFn, typename OnQueueWaitFn, typename OnQueueResumeFn, typename MsiInvokeFn>
UINT ExecuteMsiCallWithPoliteQueue(
    std::wstring_view myProductName,
    CancelPredFn&& isCanceled,
    OnQueueWaitFn&& onQueueWaiting,
    OnQueueResumeFn&& onQueueResumed,
    MsiInvokeFn&& invokeMsi)
{
    auto buildWaitMessage = [&]() -> std::wstring {
        const std::wstring holder = rsw::ScopedActiveMsiBeacon::ReadActiveProductName();
        if (!holder.empty() && holder != myProductName) {
            return L"Waiting for " + holder + L" Setup to finish installing...\r\nSetup will continue automatically.";
        }
        return L"Waiting for another Windows installation to finish...\r\nSetup will continue automatically.";
    };

    for (int attempt = 0; attempt < 7200; ++attempt) {
        if (isCanceled()) {
            return ERROR_INSTALL_USEREXIT;
        }

        bool waitedBeforeCall = false;
        while (rsw::IsSystemMsiExecuteMutexBusy()) {
            if (isCanceled()) {
                return ERROR_INSTALL_USEREXIT;
            }
            waitedBeforeCall = true;
            onQueueWaiting(buildWaitMessage());
            ::Sleep(250);
        }
        if (waitedBeforeCall) {
            onQueueResumed();
        }

        UINT rc = ERROR_INSTALL_FAILURE;
        {
            rsw::ScopedActiveMsiBeacon activeBeacon(myProductName);
            rc = invokeMsi();
        }

        if (rc != ERROR_INSTALL_ALREADY_RUNNING) {
            return rc;
        }
        if (isCanceled()) {
            return ERROR_INSTALL_USEREXIT;
        }
        onQueueWaiting(buildWaitMessage());
        for (int slice = 0; slice < 4; ++slice) {
            if (isCanceled()) {
                return ERROR_INSTALL_USEREXIT;
            }
            ::Sleep(250);
        }
        onQueueResumed();
    }
    return ERROR_INSTALL_ALREADY_RUNNING;
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
    HWND                    hBackdropWnd      = nullptr;
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
    bool                    isHeadlessExport  = false;
    std::wstring            instanceKey;
    UINT                    wmActivateInstance = 0;
    TaskbarProgressHelper   taskbarProgress;
    int                     selectedSetupTypeIdx = 0;
    std::string             selectedExplicitFeaturesCsv;
    std::wstring            selectedSetupTypeLabel = L"Express Setup (Recommended)";
    bool                    oneClickExpanded  = false;
    bool                    passiveAutoStarted = false;
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
    std::vector<rsw::FeaturePackRuntimeEntry>       featurePacks;

    // Standard Glass, Classic 3D, Fluent Mica, One-Click, & Autorun child controls
    HWND hModeCards[4] = {nullptr, nullptr, nullptr, nullptr};
    HWND hModeExpress = nullptr, hModeCompact = nullptr, hModeCustom = nullptr;
    HWND hBtnOneClickCust = nullptr;
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
    std::atomic<bool>       cancelLockedPointOfNoReturn{false};
    std::atomic<bool>       installRunning{false};
    std::atomic<bool>       awaitingUacElevation{false};
    ULONGLONG               installStartTickMs = 0;
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

    bool IsCompactOneClickSplash() const noexcept {
        return (cfg.wizardStyle == rsw::WizardStyleKind::OneClick ||
                cfg.wizardStyle == rsw::WizardStyleKind::Simple) &&
               !oneClickExpanded && !alreadyInstalled;
    }

    bool IsPassiveToastMode() const noexcept {
        return (cfg.wizardStyle == rsw::WizardStyleKind::PassiveToast);
    }

    bool UsesClassicWelcomeSidebar() const noexcept {
        return (rsw::UsesClassic3dDialogSheet(cfg.wizardStyle) ||
                cfg.wizardStyle == rsw::WizardStyleKind::Wizard97) &&
               (currentPage == WizardPage::Welcome || currentPage == WizardPage::Finished);
    }

    int FluentStepStageIndex() const noexcept {
        switch (currentPage) {
            case WizardPage::Welcome:
            case WizardPage::AutorunReadMe:   return 0;
            case WizardPage::License:         return 1;
            case WizardPage::SetupType:       return 2;
            case WizardPage::CustomFeatures:
            case WizardPage::CustomFolder:
            case WizardPage::CustomShortcuts: return 3;
            case WizardPage::ReadyToInstall:
            case WizardPage::Maintenance:
            case WizardPage::RepairMode:
            case WizardPage::SafeUninstall:   return 4;
            case WizardPage::Installing:      return 5;
            case WizardPage::Finished:        return 6;
        }
        return 0;
    }

    void RefreshEffectiveThemeAndChrome() noexcept {
        theme.Refresh(cfg.accentPrimary, cfg.accentSecondary);
        rsw::ApplyWizardStyleThemeOverrides(cfg.wizardStyle, theme);
        if (hWnd) {
            rsw::ui::ApplyWindowChromeAndA11y(hWnd, theme);
            if (!theme.isHighContrast) {
                const int backdropVal = (cfg.wizardStyle == rsw::WizardStyleKind::FluentMica) ? 4 : 1;
                ::DwmSetWindowAttribute(hWnd, 38 /* DWMWA_SYSTEMBACKDROP_TYPE */, &backdropVal, sizeof(backdropVal));
            }
        }
        if (hBackdropWnd) {
            ::InvalidateRect(hBackdropWnd, nullptr, TRUE);
        }
    }

    void SyncSelectedSetupTypeIndex(int idx) {
        if (cfg.setupTypes.empty()) {
            cfg.setupTypes = rsw::BuildEffectiveSetupTypeOptions(
                cfg.setupTypePreset, {}, cfg.expressDesc, cfg.compactDesc, cfg.customDesc);
        }
        const int clamped = std::clamp(idx, 0, static_cast<int>(cfg.setupTypes.size()) - 1);
        selectedSetupTypeIdx = clamped;
        const auto& st = cfg.setupTypes[static_cast<std::size_t>(clamped)];
        selectedExplicitFeaturesCsv = st.explicitFeaturesCsv;
        selectedSetupTypeLabel      = StripMnemonics(st.titleMnemonic);
        const std::string t = rsw::AsciiLower(st.tier);
        if (t == "minimal")       selectedAction = SelectedAction::InstallCompact;
        else if (t == "complete") selectedAction = SelectedAction::InstallComplete;
        else if (t == "custom")   selectedAction = SelectedAction::InstallCustom;
        else                      selectedAction = SelectedAction::InstallExpress;
    }

    void PopulateSetupTypeCardsFromConfig() {
        if (cfg.setupTypes.empty()) {
            cfg.setupTypes = rsw::BuildEffectiveSetupTypeOptions(
                cfg.setupTypePreset, {}, cfg.expressDesc, cfg.compactDesc, cfg.customDesc);
        }
        int recIdx = 0;
        for (std::size_t i = 0; i < cfg.setupTypes.size() && i < 4; ++i) {
            if (cfg.setupTypes[i].recommended) { recIdx = static_cast<int>(i); break; }
        }
        for (int i = 0; i < 4; ++i) {
            HWND hCard = hModeCards[i];
            if (!hCard) continue;
            if (static_cast<std::size_t>(i) < cfg.setupTypes.size()) {
                const auto& st = cfg.setupTypes[static_cast<std::size_t>(i)];
                for (auto& cv : cardVisuals) {
                    if (cv->hwnd == hCard) {
                        cv->glyph         = st.glyph;
                        cv->rimRgb        = st.rimRgb;
                        cv->accentRgb     = st.accentRgb;
                        cv->titleMnemonic = st.titleMnemonic;
                        cv->description   = st.description;
                        break;
                    }
                }
                const std::wstring uiaName = st.titleMnemonic + L" — " + st.description;
                ::SendMessageW(hCard, WM_SETREDRAW, FALSE, 0);
                ::SetWindowTextW(hCard, uiaName.c_str());
                ::SendMessageW(hCard, BM_SETCHECK, (i == recIdx) ? BST_CHECKED : BST_UNCHECKED, 0);
                ::SendMessageW(hCard, WM_SETREDRAW, TRUE, 0);
                ::InvalidateRect(hCard, nullptr, FALSE);
            } else {
                ::SendMessageW(hCard, BM_SETCHECK, BST_UNCHECKED, 0);
            }
        }
        SyncSelectedSetupTypeIndex(recIdx);
    }

    bool IsRetroDockedProgressMode() const noexcept {
        // Stay centered in the full `640x490` wizard dialog until UAC elevation has been approved,
        // so Windows UAC never anchors to a corner progress bar!
        return rsw::UsesFullScreenRetroBackdrop(cfg.wizardStyle) &&
               (currentPage == WizardPage::Installing) &&
               !awaitingUacElevation.load();
    }

    /// Locks or unlocks both the `Cancel` button and the window's `X` / `Alt+F4` (`SC_CLOSE`) command
    /// when `MSI.DLL` enters `RollbackCleanup` (removing `.rbf` backup files at the point of no return!).
    void SetCloseAndCancelLocked(bool locked) noexcept {
        if (hBtnCancel) {
            ::EnableWindow(hBtnCancel, locked ? FALSE : TRUE);
        }
        if (hWnd) {
            HMENU hSys = ::GetSystemMenu(hWnd, FALSE);
            if (hSys) {
                ::EnableMenuItem(hSys, SC_CLOSE, MF_BYCOMMAND | (locked ? (MF_DISABLED | MF_GRAYED) : MF_ENABLED));
            }
            ::DrawMenuBar(hWnd);
        }
    }

    void EnsureCustomSystemMenuAbout() noexcept {
        if (!hWnd || !cfg.systemMenuAbout || IsRetroDockedProgressMode()) return;
        HMENU hSys = ::GetSystemMenu(hWnd, FALSE);
        if (!hSys) return;
        MENUITEMINFOW mii{sizeof(mii)};
        mii.fMask = MIIM_ID;
        if (!::GetMenuItemInfoW(hSys, IDM_SYS_ABOUT_SETUP, FALSE, &mii)) {
            const std::wstring abtLbl = L"&About " + cfg.productName + L" Setup...";
            ::AppendMenuW(hSys, MF_SEPARATOR, 0, nullptr);
            ::AppendMenuW(hSys, MF_STRING, IDM_SYS_ABOUT_SETUP, abtLbl.c_str());
        }
    }

    void ComputeLogicalDialogSize(int& outLogW, int& outLogH) const noexcept {
        if (IsRetroDockedProgressMode()) {
            outLogW = 440; outLogH = 104;
        } else if (cfg.isAutorun16x9) {
            outLogW = 640; outLogH = 480;
        } else if (IsPassiveToastMode()) {
            outLogW = 500; outLogH = 236;
        } else if (IsCompactOneClickSplash()) {
            outLogW = 540; outLogH = 310;
        } else if (cfg.wizardStyle == rsw::WizardStyleKind::FluentMica) {
            outLogW = 680; outLogH = 496;
        } else if (rsw::UsesClassic3dDialogSheet(cfg.wizardStyle)) {
            outLogW = 640; outLogH = 490;
        } else {
            outLogW = 620; outLogH = 490;
        }
    }

    void ResizeDialogForActiveStyle() {
        if (!hWnd) return;
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        // Authentic 1995/1998 `Enable(PROGRESS)` Chrome + Mandatory Taskbar Button Rule:
        //   - During `IsRetroDockedProgressMode()` (`FullScreenBlue` & `FullScreenTeal` `Installing`),
        //     strip ALL window chrome (`WS_CAPTION`, `WS_SYSMENU`, `WS_MINIMIZEBOX`) so the docked
        //     progress meter is a pure, titlebar-free 3D `#C0C0C0` beveled panel!
        //   - Simultaneously keep `WS_EX_APPWINDOW` set on `GWL_EXSTYLE` (unless running headless
        //     `--export-screenshots`) so Windows Explorer ALWAYS displays Setup's taskbar icon and
        //     live `ITaskbarList3` progress bar—even when `hWnd` is owned by `WS_EX_TOOLWINDOW`
        //     `hBackdropWnd` or docked as a captionless `WS_POPUP` meter!
        const DWORD curStyle  = static_cast<DWORD>(::GetWindowLongW(hWnd, GWL_STYLE));
        const DWORD visBit    = (curStyle & WS_VISIBLE);
        const DWORD wantStyle = IsRetroDockedProgressMode()
            ? (WS_POPUP | WS_CLIPCHILDREN | visBit)
            : (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN | visBit);
        const bool styleChanged = ((curStyle & ~WS_CLIPSIBLINGS) != wantStyle);
        if (styleChanged) {
            ::SetWindowLongW(hWnd, GWL_STYLE, static_cast<LONG>(wantStyle));
        }

        DWORD curExStyle = static_cast<DWORD>(::GetWindowLongW(hWnd, GWL_EXSTYLE));
        bool exStyleChanged = false;
        if (!isHeadlessExport) {
            const DWORD wantExStyle = (curExStyle | WS_EX_APPWINDOW) & ~static_cast<DWORD>(WS_EX_TOOLWINDOW);
            if (wantExStyle != curExStyle) {
                ::SetWindowLongW(hWnd, GWL_EXSTYLE, static_cast<LONG>(wantExStyle));
                curExStyle     = wantExStyle;
                exStyleChanged = true;
            }
        }

        // Enforce sharp 90-degree corners (`DWMWCP_DONOTROUND = 1`) and suppress Windows 11 DWM border
        // halo (`DWMWA_COLOR_NONE = 0xFFFFFFFE`) on borderless retro progress & classic Win95 sheets!
        const int cornerPref = (IsRetroDockedProgressMode() || rsw::UsesClassic3dDialogSheet(cfg.wizardStyle)) ? 1 : 0;
        ::DwmSetWindowAttribute(hWnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &cornerPref, sizeof(cornerPref));
        if (IsRetroDockedProgressMode()) {
            const COLORREF noBorder = 0xFFFFFFFEu; // `DWMWA_COLOR_NONE`
            ::DwmSetWindowAttribute(hWnd, 34 /* DWMWA_BORDER_COLOR */, &noBorder, sizeof(noBorder));
        } else {
            const COLORREF defBorder = 0xFFFFFFFFu; // `DWMWA_COLOR_DEFAULT`
            ::DwmSetWindowAttribute(hWnd, 34 /* DWMWA_BORDER_COLOR */, &defBorder, sizeof(defBorder));
            EnsureCustomSystemMenuAbout();
        }

        int logW = 620, logH = 490;
        ComputeLogicalDialogSize(logW, logH);
        RECT winRc{0, 0, S(logW), S(logH)};
        ::AdjustWindowRectExForDpi(&winRc, wantStyle, FALSE, curExStyle, dpi);
        const int totalW = winRc.right - winRc.left;
        const int totalH = winRc.bottom - winRc.top;

        HMONITOR hMon = ::MonitorFromWindow(hWnd, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        RECT workRc{0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
        if (hMon && ::GetMonitorInfoW(hMon, &mi)) {
            workRc = rsw::UsesFullScreenRetroBackdrop(cfg.wizardStyle) ? mi.rcMonitor : mi.rcWork;
        }

        int posX = workRc.left + std::max<int>(0, ((workRc.right - workRc.left) - totalW) / 2);
        int posY = workRc.top  + std::max<int>(0, ((workRc.bottom - workRc.top) - totalH) / 2);
        if (IsRetroDockedProgressMode()) {
            // Dock the borderless `#C0C0C0` 3D `Enable(PROGRESS)` meter at bottom-right so the
            // `[Billboard.*]` showcase card sits unobstructed dead-center on the retro screen!
            posX = std::max<int>(workRc.left + S(16), workRc.right  - totalW - S(24));
            posY = std::max<int>(workRc.top  + S(16), workRc.bottom - totalH - S(24));
        }
        ::SetWindowPos(
            hWnd, nullptr, posX, posY, totalW, totalH,
            SWP_NOZORDER | SWP_NOACTIVATE | ((styleChanged || exStyleChanged) ? SWP_FRAMECHANGED : 0));
        if ((styleChanged || exStyleChanged) && !isHeadlessExport) {
            if (hAppIconBig) {
                ::SendMessageW(hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hAppIconBig));
            }
            HICON hSm = hAppIconSmall ? hAppIconSmall : hAppIconBig;
            if (hSm) {
                ::SendMessageW(hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSm));
            }
            taskbarProgress.ApplyCurrent(hWnd);
        }
        ApplyFontsAndLayout();
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
        if (!cfg.externalMsiName.empty()) {
            wchar_t selfBuf[MAX_PATH * 4]{};
            ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
            const auto sideMsi = std::filesystem::path(selfBuf).parent_path() / rsw::Utf8ToWide(cfg.externalMsiName);
            if (std::filesystem::exists(sideMsi, ec)) {
                tempMsiPath = sideMsi;
                return true;
            }
        }
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
        // Attach `.rswpak` byte costs onto any `Delivery = ondemand` features!
        for (auto& fn : featureNodes) {
            for (const auto& fp : featurePacks) {
                if (rsw::EqualsIgnoreCase(fn.id, fp.featureId) && fp.uncompressedBytes > 0) {
                    fn.ownBytes += fp.uncompressedBytes;
                }
            }
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
        constexpr UINT ID_ACT_RUN_SOURCE = 4005;

        ::AppendMenuW(hMenu, MF_STRING | ((fn.currentState == rsw::MsiTreeState::Local) ? MF_CHECKED : 0),
                      ID_ACT_LOCAL, L"&Will be installed on local hard drive");
        ::AppendMenuW(hMenu, MF_STRING | ((fn.currentState == rsw::MsiTreeState::EntireSubtree) ? MF_CHECKED : 0),
                      ID_ACT_SUBTREE, L"&Entire feature will be installed on local hard drive");
        if (fn.allowRunFromSource) {
            ::AppendMenuW(hMenu, MF_STRING | ((fn.currentState == rsw::MsiTreeState::RunFromSource) ? MF_CHECKED : 0),
                          ID_ACT_RUN_SOURCE, L"Will be installed to run from &CD / network");
        }
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
        else if (picked == ID_ACT_RUN_SOURCE) rsw::ApplyMsiFeatureActionRecursive(featureNodes, idx, rsw::MsiTreeState::RunFromSource);
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

        // 1. `SetupType` 1..4-Card Radio Group (`Express/Compact/Custom` OR `Typical/Compact/Complete/Custom`!)
        hModeCards[0] = CreateOptionRadioCard(
            IDC_MODE_EXPRESS, true, false,
            rsw::ui::GlassGlyph::Bolt, rsw::ui::brand::kSkyCyan, rsw::ui::brand::kWarmGold,
            L"&Express Setup (Recommended)", cfg.expressDesc);
        hModeCards[1] = CreateOptionRadioCard(
            IDC_MODE_COMPACT, false, false,
            rsw::ui::GlassGlyph::Cube, rsw::ui::brand::kRaggieBlue, rsw::ui::brand::kSkyCyan,
            L"Co&mpact Core Setup", cfg.compactDesc);
        hModeCards[2] = CreateOptionRadioCard(
            IDC_MODE_CUSTOM, false, false,
            rsw::ui::GlassGlyph::Sliders, rsw::ui::brand::kVioletTune, rsw::ui::brand::kWarmGold,
            L"C&ustom Setup (Choose Features, Folder && Shortcuts)", cfg.customDesc);
        hModeCards[3] = CreateOptionRadioCard(
            IDC_MODE_CARD_4, false, false,
            rsw::ui::GlassGlyph::Blocks, rsw::ui::brand::kEmeraldOk, rsw::ui::brand::kWarmGold,
            L"Com&plete", L"Installs all program features, templates, documentation, and optional extras.");
        hModeExpress = hModeCards[0];
        hModeCompact = hModeCards[1];
        hModeCustom  = hModeCards[2];
        LinkSiblingGroup({hModeCards[0], hModeCards[1], hModeCards[2], hModeCards[3]});
        PopulateSetupTypeCardsFromConfig();

        hBtnOneClickCust = mkBtn(L"C&ustomize...", IDC_ONECLICK_CUST, BS_PUSHBUTTON);

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
        rsw::ui::AttachAccessiblePushButtonSubclass(hFeatResetBtn,   &theme, false, false);
        rsw::ui::AttachAccessiblePushButtonSubclass(hDirBrowse,      &theme, false, false);
        rsw::ui::AttachAccessiblePushButtonSubclass(hFinViewLog,     &theme, false, false);
        rsw::ui::AttachAccessiblePushButtonSubclass(hBtnOneClickCust,&theme, false, true);
        rsw::ui::AttachAccessiblePushButtonSubclass(hBtnBack,        &theme, false, true);
        rsw::ui::AttachAccessiblePushButtonSubclass(hBtnPrimary,     &theme, true,  true);
        rsw::ui::AttachAccessiblePushButtonSubclass(hBtnCancel,      &theme, false, true);

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
                       hProgStatus, hFinLaunch, hFinViewLog, hBtnOneClickCust, hBtnBack, hBtnCancel}) {
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

        // Universal Footer Buttons (`[< &Back]  [&Next >]  [Cancel]` — or top-left circular `<-` Back in `AeroWizard`!)
        rsw::ui::SetPushButtonAeroCircleBackMode(hBtnBack, cfg.isAeroWizard);
        ::SetWindowTextW(hBtnBack, cfg.isAeroWizard ? L"&Back" : L"< &Back");
        ::MoveWindow(hBtnOneClickCust, S(18), btnY, S(118), S(32), TRUE);
        if (cfg.isAeroWizard) {
            ::MoveWindow(hBtnBack, S(10), S(5), S(28), S(28), TRUE);
        } else {
            ::MoveWindow(hBtnBack, W - S(320), btnY, S(94), S(32), TRUE);
        }
        ::MoveWindow(hBtnPrimary,      W - S(218), btnY, S(112), S(32), TRUE);
        ::MoveWindow(hBtnCancel,       W - S(98),  btnY, S(80),  S(32), TRUE);

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
        } else if (IsPassiveToastMode()) {
            // `500 x 236` Non-Interactive `/passive` Toast Banner
            ::MoveWindow(hProgStatus, S(24), S(104), W - S(48), S(24), TRUE);
            ::MoveWindow(hProgBar,    S(24), S(134), W - S(48), S(22), TRUE);
        } else if (IsRetroDockedProgressMode()) {
            // `440 x 104` Borderless Classic `Enable(PROGRESS)` Meter (`#C0C0C0` 3D bevel, ZERO titlebar!):
            // Status text across top, sunken progress bar on left, and classic 3D `Cancel` button right beside it!
            ::MoveWindow(hProgStatus, S(14), S(32), W - S(28),  S(20), TRUE);
            ::MoveWindow(hProgBar,    S(14), S(60), W - S(114), S(24), TRUE);
            ::MoveWindow(hBtnCancel,  W - S(92), S(58), S(78),  S(28), TRUE);
        } else if (IsCompactOneClickSplash()) {
            // `540 x 310` One-Click Express Splash Card
            ::MoveWindow(hLicAccept,  S(18), footerY - S(34), W - S(36), S(26), TRUE);
            ::MoveWindow(hProgStatus, S(24), S(116), W - S(48), S(24), TRUE);
            ::MoveWindow(hProgBar,    S(24), S(148), W - S(48), S(22), TRUE);
            ::MoveWindow(hFinLaunch,  S(22), footerY - S(38), W - S(180), S(26), TRUE);
            ::MoveWindow(hFinViewLog, W - S(152), footerY - S(40), S(134), S(28), TRUE);
        } else {
            // Multi-Step Wizard Canvas (`StandardGlass`, `Wizard95`, `Wizard97`, `AeroWizard`,
            // `FluentMica`, `FullScreenBlue`, `FullScreenTeal`, & expanded `OneClick`):
            // AUTHENTIC `Wizard95` SPECIFICATION:
            //   - In `Wizard97`, ONLY `Welcome` & `Finished` have the left graphic rail while interior
            //     pages switch to an `80px` top white header banner (`cardX = S(18)`).
            //   - In `Wizard95` (`UsesClassic3dDialogSheet`: `Wizard95`, `FullScreenBlue`, `FullScreenTeal`),
            //     ALL wizard pages keep the exact same `164px` sunken left graphic rail (`S(12)..S(176)`)
            //     with ZERO top header strip, placing every control in the right column (`cardX = S(192)`)!
            const bool isMica       = (cfg.wizardStyle == rsw::WizardStyleKind::FluentMica);
            const bool isWin95Sheet = rsw::UsesClassic3dDialogSheet(cfg.wizardStyle);
            const bool isWiz97Hdr   = (cfg.wizardStyle == rsw::WizardStyleKind::Wizard97);
            const int bodyTop = isMica ? S(42) : (isWin95Sheet ? S(50) : (isWiz97Hdr ? S(80) : (cfg.isAeroWizard ? S(88) : S(104))));
            const int cardX   = isMica ? S(180) : (isWin95Sheet ? S(192) : (cfg.isAeroWizard ? S(46) : S(18)));
            const int cardW   = W - cardX - S(18);

            // `SetupType`: 1..4 Full-Row Vector Option Cards (`Express/Compact/Custom` or `Typical/Compact/Complete/Custom`!)
            // Reserve `S(38)..S(42)` above the first card so 2-line wrapped product prompts NEVER clip!
            const int nCards   = std::clamp<int>(static_cast<int>(cfg.setupTypes.size()), 1, 4);
            const int optStart = bodyTop + (isWin95Sheet ? S(42) : ((nCards >= 4) ? S(38) : S(42)));
            const int optH     = (nCards >= 4) ? (isWin95Sheet ? S(74) : S(64)) : S(74);
            const int optGap   = (nCards >= 4) ? S(6)  : S(10);
            for (int i = 0; i < 4; ++i) {
                if (hModeCards[i]) {
                    ::MoveWindow(hModeCards[i], cardX, optStart + (optH + optGap) * i, cardW, optH, TRUE);
                }
            }

            // `Maintenance`: 3 Option Cards (no glass HeroBrandCard on `Wizard95`!)
            const int mntStart = bodyTop + (isWin95Sheet ? S(40) : S(74));
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

            // `SafeUninstall`: 2 safe unchecked checkboxes positioned below 2-line wrapped header + body!
            const int unChkBase = bodyTop + (isWin95Sheet ? S(130) : S(198));
            ::MoveWindow(hUninstCache, cardX + S(10), unChkBase,         cardW - S(20), S(26), TRUE);
            ::MoveWindow(hUninstPrefs, cardX + S(10), unChkBase + S(34), cardW - S(20), S(26), TRUE);

            // `License`: 2-line wrapped step intro + High-Contrast RichEdit + Accept checkbox
            ::MoveWindow(hLicRich,   cardX, bodyTop + S(38), cardW, footerY - bodyTop - S(82), TRUE);
            ::MoveWindow(hLicAccept, cardX, footerY - S(36), cardW, S(26), TRUE);

            // `CustomFeatures`: Reserve `S(40)` at top for 2-line wrapped prompt, then stack full-width
            // `SelectionTree` + `144px` `Feature Details` card on `Wizard95` (or side-by-side on wide sheets)!
            if (isWin95Sheet) {
                ::MoveWindow(hFeatTree,     cardX, bodyTop + S(40),  cardW,  S(150), TRUE);
                ::MoveWindow(hFeatResetBtn, cardX, bodyTop + S(346), S(136), S(28),  TRUE);
            } else {
                const int treeW = (cardW * 58) / 100;
                const int treeH = footerY - bodyTop - S(84);
                ::MoveWindow(hFeatTree,     cardX, bodyTop + S(40), treeW, treeH, TRUE);
                ::MoveWindow(hFeatResetBtn, cardX, bodyTop + S(46) + treeH, S(132), S(28), TRUE);
            }

            // `CustomFolder`: Path `EDIT` + `&Browse...` (Disk Space Card painted below!)
            const int dirLblOff = isWin95Sheet ? S(66) : S(38);
            const int dirEdtOff = isWin95Sheet ? S(90) : S(64);
            ::MoveWindow(hDirLabel,  cardX, bodyTop + dirLblOff, cardW,          S(22), TRUE);
            ::MoveWindow(hDirEdit,   cardX, bodyTop + dirEdtOff, cardW - S(98),  S(30), TRUE);
            ::MoveWindow(hDirBrowse, cardX + cardW - S(90), bodyTop + dirEdtOff - S(1), S(90), S(32), TRUE);

            // `CustomShortcuts`: Start Menu group + Desktop & File Association checkboxes
            const int smLblOff = isWin95Sheet ? S(62)  : S(42);
            const int smEdtOff = isWin95Sheet ? S(88)  : S(68);
            const int dskScOff = isWin95Sheet ? S(138) : S(124);
            const int flAscOff = isWin95Sheet ? S(172) : S(160);
            ::MoveWindow(hSmLabel,      cardX, bodyTop + smLblOff, cardW, S(22), TRUE);
            ::MoveWindow(hSmEdit,       cardX, bodyTop + smEdtOff, cardW, S(30), TRUE);
            ::MoveWindow(hDeskScChk,    cardX, bodyTop + dskScOff, cardW, S(28), TRUE);
            ::MoveWindow(hFileAssocChk, cardX, bodyTop + flAscOff, cardW, S(28), TRUE);

            // `Installing`: Detailed Progress Card (`ActionText` + multiline `File / Directory / Size` + `ProgressBar` + `TimeRemaining`)
            const int progTopOff = isWin95Sheet ? S(68) : S(136);
            ::MoveWindow(hProgStatus, cardX + S(16), bodyTop + progTopOff,         cardW - S(32), S(64), TRUE);
            ::MoveWindow(hProgBar,    cardX + S(16), bodyTop + progTopOff + S(68), cardW - S(32), S(22), TRUE);

            // `Finished`: Summary + `&Launch` checkbox positioned strictly inside the right-hand column (`S(192)`)
            // while reserving `S(168)` on the right so `hFinLaunch` never overlaps `View Setup Log`!
            const int finLeft = UsesClassicWelcomeSidebar() ? S(192) : (cardX + S(10));
            const int finW    = W - finLeft - S(168);
            ::MoveWindow(hFinLaunch,  finLeft, footerY - S(42), finW, S(26), TRUE);
            ::MoveWindow(hFinViewLog, W - S(162), footerY - S(48), S(144), S(30), TRUE);
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

        const bool oneClickSplash = IsCompactOneClickSplash();
        const bool passiveMode    = IsPassiveToastMode();
        const int  nSetupCards    = std::clamp<int>(static_cast<int>(cfg.setupTypes.size()), 1, 4);

        for (HWND c : hArCell) show(c, cfg.isAutorun16x9 && pWelcome);

        for (int i = 0; i < 4; ++i) {
            show(hModeCards[i], pSetupType && !oneClickSplash && !passiveMode && (i < nSetupCards));
        }

        show(hMaintModify, pMaint);
        show(hMaintRepair, pMaint);
        show(hMaintRemove, pMaint);

        show(hRepairQuick, pRepair);
        show(hRepairFull,  pRepair);

        show(hUninstCache, pSafeUn);
        show(hUninstPrefs, pSafeUn);

        show(hLicRich,      pLic && !oneClickSplash);
        show(hLicAccept,    pLic || (oneClickSplash && pWelcome && cfg.hasLicenseRtf));
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

        show(hProgStatus, pProg || passiveMode);
        show(hProgBar,    pProg || passiveMode);

        const bool canLaunch = pFin && !passiveMode &&
                               (finalMsiResult == ERROR_SUCCESS || finalMsiResult == ERROR_SUCCESS_REBOOT_REQUIRED) &&
                               (selectedAction != SelectedAction::MaintUninstall) &&
                               cfg.offerLaunch && !cfg.mainExecutable.empty();
        show(hFinLaunch,  canLaunch);
        show(hFinViewLog, pFin && !passiveMode && (finalMsiResult != ERROR_SUCCESS && finalMsiResult != ERROR_INSTALL_USEREXIT));

        const bool canGoBack = !passiveMode && !oneClickSplash &&
                               (pLic || pReadMe || pSetupType || pFeatures || pDir || pShort || pReady || pRepair || pSafeUn);
        show(hBtnOneClickCust, oneClickSplash && pWelcome);
        if (cfg.isAeroWizard && !passiveMode && !oneClickSplash) {
            show(hBtnBack, true);
            ::EnableWindow(hBtnBack, canGoBack ? TRUE : FALSE);
        } else {
            show(hBtnBack, canGoBack);
            ::EnableWindow(hBtnBack, TRUE);
        }
        show(hBtnPrimary,      !pProg && !passiveMode);
        show(hBtnCancel,       !pFin);

        const wchar_t* nextBtnTxt = cfg.isAeroWizard ? L"&Next" : L"&Next >";

        // Configure Primary Action Button text, enabled gate, & Windows UAC Shield icon!
        bool showShield = false;
        if (pWelcome) {
            if (cfg.isAutorun16x9) {
                if (autorunChoiceIdx == 0) {
                    const bool direct = !cfg.hasLicenseRtf || alreadyInstalled;
                    ::SetWindowTextW(hBtnPrimary, alreadyInstalled ? L"&Repair" : (direct ? cfg.installBtnText.c_str() : nextBtnTxt));
                    showShield = direct && cfg.perMachine;
                    ::EnableWindow(hBtnPrimary, TRUE);
                } else if (autorunChoiceIdx == 1) {
                    ::SetWindowTextW(hBtnPrimary, L"&Read Notes");
                    ::EnableWindow(hBtnPrimary, TRUE);
                } else if (autorunChoiceIdx == 2) {
                    ::SetWindowTextW(hBtnPrimary, alreadyInstalled ? L"&Continue >" : L"C&hange...");
                    ::EnableWindow(hBtnPrimary, TRUE);
                } else {
                    ::SetWindowTextW(hBtnPrimary, L"E&xit");
                    ::EnableWindow(hBtnPrimary, TRUE);
                }
            } else if (oneClickSplash) {
                ::SetWindowTextW(hBtnPrimary, cfg.installBtnText.c_str());
                const bool okGate = !cfg.hasLicenseRtf || (::SendMessageW(hLicAccept, BM_GETCHECK, 0, 0) == BST_CHECKED);
                ::EnableWindow(hBtnPrimary, okGate ? TRUE : FALSE);
                showShield = cfg.perMachine;
            } else {
                ::SetWindowTextW(hBtnPrimary, nextBtnTxt);
                ::EnableWindow(hBtnPrimary, TRUE);
            }
        } else if (pReadMe) {
            ::SetWindowTextW(hBtnPrimary, alreadyInstalled ? L"&Done" : cfg.installBtnText.c_str());
            ::EnableWindow(hBtnPrimary, TRUE);
            showShield = !alreadyInstalled && cfg.perMachine && !cfg.hasLicenseRtf;
        } else if (pLic) {
            const bool accepted = (::SendMessageW(hLicAccept, BM_GETCHECK, 0, 0) == BST_CHECKED);
            ::SetWindowTextW(hBtnPrimary, cfg.isAutorun16x9 ? cfg.installBtnText.c_str() : nextBtnTxt);
            ::EnableWindow(hBtnPrimary, accepted ? TRUE : FALSE);
            showShield = cfg.isAutorun16x9 && cfg.perMachine;
        } else if (pSetupType || pFeatures || pShort || pMaint) {
            ::SetWindowTextW(hBtnPrimary, nextBtnTxt);
            ::EnableWindow(hBtnPrimary, TRUE);
        } else if (pDir) {
            ::SetWindowTextW(hBtnPrimary, cfg.isAutorun16x9 ? cfg.installBtnText.c_str() : nextBtnTxt);
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
        const auto privState = rsw::QueryProcessPrivilegeState();
        const bool needsUac  = cfg.perMachine && privState.CanPromptForUacElevation();
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
        if (!self->cancelLockedPointOfNoReturn.load() && self->cancelRequested.load()) {
            return IDCANCEL; // Triggers clean Windows Installer transactional rollback!
        }

        const UINT msgKind = iMessageType & 0xFF000000u;
        switch (msgKind) {
            case INSTALLMESSAGE_COMMONDATA: {
                // `MSI.DLL` sends `COMMONDATA` subtype `2` with `Field 2 == 0` when disabling
                // Cancel (entering `InstallFinalize` / `.rbf` rollback script cleanup!).
                if (::MsiRecordGetFieldCount(hRecord) >= 2 &&
                    ::MsiRecordGetInteger(hRecord, 1) == 2 &&
                    ::MsiRecordGetInteger(hRecord, 2) == 0) {
                    if (!self->cancelLockedPointOfNoReturn.exchange(true)) {
                        ::PostMessageW(self->hWnd, WM_RSW_LOCK_CANCEL, 0, 0);
                    }
                }
                return IDOK;
            }
            case INSTALLMESSAGE_ACTIONSTART: {
                wchar_t actBuf[256]{};
                wchar_t descBuf[512]{};
                wchar_t tmplBuf[512]{};
                DWORD cAct  = static_cast<DWORD>(std::size(actBuf));
                DWORD cDesc = static_cast<DWORD>(std::size(descBuf));
                DWORD cTmpl = static_cast<DWORD>(std::size(tmplBuf));
                ::MsiRecordGetStringW(hRecord, 1, actBuf,  &cAct);
                ::MsiRecordGetStringW(hRecord, 2, descBuf, &cDesc);
                ::MsiRecordGetStringW(hRecord, 3, tmplBuf, &cTmpl);
                if (std::wcscmp(actBuf, L"RollbackCleanup") == 0 ||
                    std::wcsstr(descBuf, L"Removing backup") != nullptr) {
                    if (!self->cancelLockedPointOfNoReturn.exchange(true)) {
                        ::PostMessageW(self->hWnd, WM_RSW_LOCK_CANCEL, 0, 0);
                    }
                }
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
        } else if (selectedAction == SelectedAction::InstallComplete) {
            // Complete Setup raises `INSTALLLEVEL` to `1000` so every `minimal`, `typical`, and `complete` feature installs!
            msiProps += L" INSTALLLEVEL=1000";
        } else if ((selectedAction == SelectedAction::InstallCustom ||
                    selectedAction == SelectedAction::MaintModify) &&
                   !featureNodes.empty()) {
            msiProps += rsw::BuildMsiFeatureSelectionProps(featureNodes);
        }
        if (!selectedExplicitFeaturesCsv.empty() &&
            selectedAction != SelectedAction::InstallCustom &&
            selectedAction != SelectedAction::MaintModify) {
            msiProps += L" ADDLOCAL=" + rsw::Utf8ToWide(selectedExplicitFeaturesCsv);
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
        installStartTickMs  = ::GetTickCount64();
        capturedErrorText.clear();
        ::SendMessageW(hProgBar, PBM_SETPOS, 0, 0);

        cancelRequested.store(false);
        cancelLockedPointOfNoReturn.store(false);
        SetCloseAndCancelLocked(false);

        bool anyPendingPrereqElev = false;
        for (std::size_t idx : pendingPrereqIndices) {
            if (idx < prereqSpecs.size() && prereqSpecs[idx].requiresElevation) {
                anyPendingPrereqElev = true;
            }
        }
        const auto privState  = rsw::QueryProcessPrivilegeState();
        const bool needsAdmin = (cfg.perMachine || anyPendingPrereqElev);

        // Explicit Admin Rights Check When UAC Is Disabled (`EnableLUA = 0`):
        //   If UAC is turned off on a Standard User account, Windows cannot display a `runas`
        //   elevation prompt. Fail cleanly upfront with a clear, accessible explanation!
        if (needsAdmin && privState.IsStandardUserWithUacDisabled()) {
            const std::wstring cap = cfg.productName + L" Setup";
            const std::wstring msg =
                L"Installing " + cfg.productName + L" for all users requires Administrator privileges, "
                L"and User Account Control (UAC) elevation prompts are turned off on this computer.\n\n"
                L"Please sign in with an Administrator account (or re-enable UAC) and run Setup again.";
            if (IsPassiveToastMode()) {
                finalMsiResult = ERROR_ACCESS_DENIED;
                ::DestroyWindow(hWnd);
            } else {
                ::TaskDialog(hWnd, ::GetModuleHandleW(nullptr), cap.c_str(),
                             L"Administrator Privileges Required", msg.c_str(),
                             TDCBF_OK_BUTTON, TD_ERROR_ICON, nullptr);
            }
            return;
        }

        const bool willNeedUac = needsAdmin && !privState.HasEffectiveAdminRights();
        awaitingUacElevation.store(willNeedUac);

        currentPage = WizardPage::Installing;
        if (!isHeadlessExport) {
            taskbarProgress.SetIndeterminate(hWnd);
        }
        ResizeDialogForActiveStyle();
        ::InvalidateRect(hWnd, nullptr, TRUE);
        if (hBackdropWnd) ::InvalidateRect(hBackdropWnd, nullptr, FALSE);

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
            const bool needsElevation = (cfg.perMachine || anyPrereqNeedsElevation) && !HasCurrentProcessAdminRights();
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
                       << "ProductName=" << rsw::WideToUtf8(cfg.productName) << "\n"
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
                bool lastChildQueuePaused = false;
                auto syncStatusFromDisk = [&]() {
                    std::ifstream in(statusFile, std::ios::binary);
                    if (!in) return;
                    std::string linePerm, lineStat, lineErr, lineLock, lineQueue;
                    std::getline(in, linePerm);
                    std::getline(in, lineStat);
                    std::getline(in, lineErr);
                    std::getline(in, lineLock);
                    std::getline(in, lineQueue);
                    if (!linePerm.empty()  && linePerm.back()  == '\r') linePerm.pop_back();
                    if (!lineStat.empty()  && lineStat.back()  == '\r') lineStat.pop_back();
                    if (!lineErr.empty()   && lineErr.back()   == '\r') lineErr.pop_back();
                    if (!lineLock.empty()  && lineLock.back()  == '\r') lineLock.pop_back();
                    if (!lineQueue.empty() && lineQueue.back() == '\r') lineQueue.pop_back();

                    if (lineLock == "1" && !cancelLockedPointOfNoReturn.exchange(true)) {
                        ::PostMessageW(hWnd, WM_RSW_LOCK_CANCEL, 0, 0);
                    }
                    const bool qPaused = (lineQueue == "1");
                    if (qPaused != lastChildQueuePaused) {
                        lastChildQueuePaused = qPaused;
                        ::PostMessageW(hWnd, WM_RSW_QUEUE_STATE, qPaused ? 1 : 0, 0);
                    }
                    if (!linePerm.empty() && !qPaused) {
                        const int perm = std::clamp(std::atoi(linePerm.c_str()), 0, 1000);
                        ::PostMessageW(hWnd, WM_RSW_PROGRESS_UPDATE, static_cast<WPARAM>(perm), 0);
                    }
                    if (!lineStat.empty() || !lineErr.empty()) {
                        std::lock_guard<std::mutex> lk(msgMutex);
                        if (!lineStat.empty()) {
                            for (char& ch : lineStat) {
                                if (ch == '\t') ch = '\n';
                            }
                            latestStatusText = rsw::Utf8ToWide(lineStat);
                            ::PostMessageW(hWnd, WM_RSW_STATUS_TEXT, 0, 0);
                        }
                        if (!lineErr.empty()) {
                            capturedErrorText = rsw::Utf8ToWide(lineErr);
                        }
                    }
                };

                while (::WaitForSingleObject(hElevatedChild, 45) == WAIT_TIMEOUT) {
                    if (cancelRequested.load() && !cancelLockedPointOfNoReturn.load()) {
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

            UINT rc = ExecuteMsiCallWithPoliteQueue(
                cfg.productName,
                [&]() { return cancelRequested.load(); },
                [&](const std::wstring& waitMsg) {
                    ::PostMessageW(hWnd, WM_RSW_QUEUE_STATE, 1, 0);
                    postStatus(waitMsg);
                },
                [&]() {
                    ::PostMessageW(hWnd, WM_RSW_QUEUE_STATE, 0, 0);
                    postStatus(L"Starting Windows Installer transaction...");
                },
                [&]() -> UINT {
                    if (selectedAction == SelectedAction::MaintUninstall && !installedProductCode.empty()) {
                        std::wstring unProps;
                        if (wipeCache) unProps += L"REMOVE_CACHE=1 ";
                        if (wipePrefs) unProps += L"REMOVE_PREFS=1 ";
                        return ::MsiConfigureProductExW(installedProductCode.c_str(), INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT, unProps.c_str());
                    }
                    if (selectedAction == SelectedAction::MaintQuickRepair && !installedProductCode.empty()) {
                        return ::MsiReinstallProductW(
                            installedProductCode.c_str(),
                            REINSTALLMODE_FILEMISSING | REINSTALLMODE_FILEOLDERVERSION | REINSTALLMODE_FILEVERIFY |
                            REINSTALLMODE_MACHINEDATA | REINSTALLMODE_USERDATA | REINSTALLMODE_SHORTCUT);
                    }
                    if (selectedAction == SelectedAction::MaintFullReinstall && !installedProductCode.empty()) {
                        std::wstring err;
                        if (EnsureTempMsiExtracted(err)) {
                            return ::MsiInstallProductW(tempMsiPath.wstring().c_str(), L"REINSTALL=ALL REINSTALLMODE=vomus");
                        }
                        return ::MsiReinstallProductW(
                            installedProductCode.c_str(),
                            REINSTALLMODE_FILEREPLACE | REINSTALLMODE_SHORTCUT | REINSTALLMODE_MACHINEDATA | REINSTALLMODE_USERDATA);
                    }
                    return ::MsiInstallProductW(tempMsiPath.wstring().c_str(), msiProps.c_str());
                });

            ::MsiSetExternalUIRecord(prevHandler, 0, nullptr, nullptr);

            // Unpack any selected `Delivery = ondemand` `.rswpak` feature packs into `installDir`!
            if ((rc == ERROR_SUCCESS || rc == ERROR_SUCCESS_REBOOT_REQUIRED) &&
                selectedAction != SelectedAction::MaintUninstall &&
                !featurePacks.empty() && !installDir.empty()) {
                for (const auto& fp : featurePacks) {
                    bool wantPack = (selectedAction != SelectedAction::InstallCompact);
                    if (featuresLoaded && !featureNodes.empty()) {
                        wantPack = false;
                        for (const auto& fn : featureNodes) {
                            if (rsw::EqualsIgnoreCase(fn.id, fp.featureId)) {
                                wantPack = (fn.currentState == rsw::MsiTreeState::Local ||
                                            fn.currentState == rsw::MsiTreeState::EntireSubtree);
                                break;
                            }
                        }
                    }
                    if (!wantPack) continue;

                    std::filesystem::path pakFile = selfExeDir / rsw::Utf8ToWide(fp.fileName);
                    if (!std::filesystem::exists(pakFile, thEc) && !fp.downloadUrl.empty()) {
                        pakFile = tempWorkDir / rsw::Utf8ToWide(fp.fileName);
                        postStatus(L"Downloading feature pack: " + rsw::Utf8ToWide(fp.featureId) + L"...");
                        std::string dlErr;
                        if (!rsw::DownloadFileViaWinHttp(fp.downloadUrl, pakFile, &cancelRequested, nullptr, dlErr)) {
                            std::lock_guard<std::mutex> lk(msgMutex);
                            capturedErrorText = rsw::Utf8ToWide(dlErr);
                            rc = ERROR_INSTALL_FAILURE;
                            break;
                        }
                    }
                    if (std::filesystem::exists(pakFile, thEc)) {
                        postStatus(L"Verifying & unpacking feature pack: " + rsw::Utf8ToWide(fp.featureId) + L"...");
                        std::string pkErr;
                        if (!rsw::VerifyFeaturePackIntegrity(pakFile, fp.sha256Hex, false, pkErr) ||
                            !rsw::ExtractRswPakFileToDirectory(pakFile, std::filesystem::path(installDir), &cancelRequested, nullptr, pkErr)) {
                            std::lock_guard<std::mutex> lk(msgMutex);
                            capturedErrorText = rsw::Utf8ToWide(pkErr);
                            rc = ERROR_INSTALL_FAILURE;
                            break;
                        }
                    }
                }
            }

            if (rc == ERROR_SUCCESS && anyPrereqRequestedReboot) {
                rc = ERROR_SUCCESS_REBOOT_REQUIRED; // Latch prerequisite reboot request!
            }
            finalMsiResult = rc;
            installRunning.store(false);
            ::PostMessageW(hWnd, WM_RSW_INSTALL_DONE, static_cast<WPARAM>(rc), 0);
        });
    }

    void OnTransactionCompleted() {
        const bool cleanOrUserExit =
            (finalMsiResult == ERROR_SUCCESS ||
             finalMsiResult == ERROR_SUCCESS_REBOOT_REQUIRED ||
             finalMsiResult == ERROR_INSTALL_USEREXIT);
        currentPermille = (finalMsiResult == ERROR_SUCCESS || finalMsiResult == ERROR_SUCCESS_REBOOT_REQUIRED) ? 1000 : currentPermille;
        awaitingUacElevation.store(false);
        cancelLockedPointOfNoReturn.store(false);
        SetCloseAndCancelLocked(false);
        if (!isHeadlessExport) {
            if (cleanOrUserExit) {
                taskbarProgress.SetNoProgress(hWnd);
            } else {
                taskbarProgress.SetError(hWnd);
            }
        }
        if (IsPassiveToastMode()) {
            ::DestroyWindow(hWnd);
            return;
        }
        currentPage     = WizardPage::Finished;
        ResizeDialogForActiveStyle();
        ::SetFocus(hBtnPrimary);
        ::InvalidateRect(hWnd, nullptr, TRUE);
        if (hBackdropWnd) ::InvalidateRect(hBackdropWnd, nullptr, FALSE);
    }

    // ----------------------------------------------------------------------------
    // MULTI-STEP WIZARD FORWARD & BACKWARD NAVIGATION
    // ----------------------------------------------------------------------------
    void NavigateTo(WizardPage target, HWND initialFocus = nullptr) {
        SyncEditableFieldsFromControls();
        currentPage = target;
        if (!isHeadlessExport && target != WizardPage::Installing && target != WizardPage::Finished) {
            taskbarProgress.SetNoProgress(hWnd);
        }
        if (rsw::UsesClassic3dDialogSheet(cfg.wizardStyle) || cfg.wizardStyle == rsw::WizardStyleKind::Wizard97) {
            ApplyFontsAndLayout();
        } else {
            UpdateControlVisibilityForPage();
        }
        if (initialFocus && ::IsWindowVisible(initialFocus) && ::IsWindowEnabled(initialFocus)) {
            ::SetFocus(initialFocus);
        }
        ::InvalidateRect(hWnd, nullptr, TRUE);
        if (hBackdropWnd) ::InvalidateRect(hBackdropWnd, nullptr, FALSE);
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
                } else if (IsCompactOneClickSplash()) {
                    selectedAction = SelectedAction::InstallExpress;
                    StartMsiWorkerTransaction();
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
                    // De-elevate automatically through Explorer's desktop shell token when Setup ran as
                    // `requireAdministrator` or `highestAvailable` so the user's app never runs as Admin!
                    LaunchInstalledAppDeElevated(exePath, installDir);
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

        // 1. Footer Bar (Skipped on borderless `Enable(PROGRESS)` meter; Classic 3D Etched Rule on `#C0C0C0` sheets, or Branded Glass Footer Bar!)
        if (!IsRetroDockedProgressMode()) {
            if (rsw::UsesClassic3dDialogSheet(cfg.wizardStyle)) {
                rsw::ui::DrawClassicEtchedHorizontalRule(memDc, S(12), W - S(12), footerY, theme);
                if (!IsCompactOneClickSplash()) {
                    HGDIOBJ oFs = ::SelectObject(memDc, hFontSmall);
                    ::SetTextColor(memDc, theme.textSecondary);
                    const std::wstring pubFoot = cfg.websiteLabel.empty()
                        ? cfg.manufacturer
                        : (cfg.manufacturer + L"  •  " + cfg.websiteLabel);
                    RECT pfRc{S(16), footerY + S(4), std::max(S(40), btnsLeftX - S(12)), H};
                    ::DrawTextW(memDc, pubFoot.c_str(), -1, &pfRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    ::SelectObject(memDc, oFs);
                }
            } else if (cfg.isAeroWizard) {
                rsw::ui::DrawAeroWizardCommandFooter(memDc, W, H, footerH, theme);
            } else {
                const int leftFloor = ::IsWindowVisible(hBtnOneClickCust) ? S(146) : 0;
                if (leftFloor == 0) {
                    rsw::ui::DrawGlassFooterBar(
                        memDc, g, W, H, footerH, btnsLeftX, dpi, theme, cfg.manufacturer, cfg.websiteLabel);
                } else {
                    RECT footerRc{0, footerY, W, H};
                    ::FillRect(memDc, &footerRc, theme.hbrFooter);
                    HPEN divPen = ::CreatePen(PS_SOLID, 1, theme.borderSubtle);
                    HGDIOBJ oP = ::SelectObject(memDc, divPen);
                    ::MoveToEx(memDc, 0, footerY, nullptr);
                    ::LineTo(memDc, W, footerY);
                    ::SelectObject(memDc, oP);
                    ::DeleteObject(divPen);
                }
            }
        }

        auto DrawPanelSurface = [&](HDC hdc, Gdiplus::Graphics& grp, const RECT& rc, int radiusPx) {
            if (rsw::UsesClassic3dDialogSheet(cfg.wizardStyle)) {
                HBRUSH hBr = ::CreateSolidBrush(theme.cardBg);
                ::FillRect(hdc, &rc, hBr);
                ::DeleteObject(hBr);
                rsw::ui::DrawClassic3dBevelFrame(hdc, rc, true, theme);
            } else {
                DrawGlassPanelBox(grp, rc, radiusPx);
            }
        };

        // 1B. Passive Toast (`500 x 236`) OR Borderless Retro `Enable(PROGRESS)` Bottom-Right Meter (`440 x 104`, ZERO Titlebar!)
        const StepHeaderInfo hdrInfo = CurrentStepHeaderInfo();
        if (IsRetroDockedProgressMode()) {
            // Paint crisp outer 2px raised 3D `#C0C0C0` dialog bevel since OS caption/border chrome is stripped!
            rsw::ui::DrawClassic3dBevelFrame(memDc, clientRc, false /* raised outer frame */, theme);
            const int pct = std::clamp(currentPermille / 10, 0, 100);
            std::wstring phaseTitle = L"Installing " + cfg.productName + L"...";
            if (selectedAction == SelectedAction::MaintUninstall)     phaseTitle = L"Removing " + cfg.productName + L"...";
            if (selectedAction == SelectedAction::MaintQuickRepair ||
                selectedAction == SelectedAction::MaintFullReinstall) phaseTitle = L"Repairing " + cfg.productName + L"...";
            const std::wstring pctStr = std::to_wstring(pct) + L"%";
            HGDIOBJ oFr = ::SelectObject(memDc, hFontBold);
            ::SetTextColor(memDc, theme.textPrimary);
            RECT hRc{S(14), S(10), W - S(64), S(30)};
            ::DrawTextW(memDc, phaseTitle.c_str(), -1, &hRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            RECT pRc{W - S(60), S(10), W - S(14), S(30)};
            ::DrawTextW(memDc, pctStr.c_str(), -1, &pRc, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
            ::SelectObject(memDc, oFr);
            ::BitBlt(hdcOut, 0, 0, W, H, memDc, 0, 0, SRCCOPY);
            ::SelectObject(memDc, oldBmp);
            ::DeleteObject(memBmp);
            ::DeleteDC(memDc);
            return;
        }
        if (IsPassiveToastMode()) {
            rsw::ui::DrawGlassTopHeader(
                memDc, g, W, dpi, theme,
                cfg.manufacturer, L"PASSIVE INSTALLATION", cfg.productName,
                hAppIconBig, rsw::ui::GlassGlyph::Gear, rsw::ui::brand::kRaggieBlue);
            const int pct = std::clamp(currentPermille / 10, 0, 100);
            const std::wstring pctTxt = std::to_wstring(pct) + L"%";
            HGDIOBJ oFp = ::SelectObject(memDc, hFontSection);
            ::SetTextColor(memDc, theme.textPrimary);
            RECT pRc{W - S(90), S(100), W - S(24), S(128)};
            ::DrawTextW(memDc, pctTxt.c_str(), -1, &pRc, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
            ::SelectObject(memDc, oFp);
            ::BitBlt(hdcOut, 0, 0, W, H, memDc, 0, 0, SRCCOPY);
            ::SelectObject(memDc, oldBmp);
            ::DeleteObject(memBmp);
            ::DeleteDC(memDc);
            return;
        }

        // 1C. Compact One-Click Express Splash (`540 x 310` with `[C&ustomize...]` expansion button!)
        if (IsCompactOneClickSplash() && currentPage == WizardPage::Welcome) {
            rsw::ui::DrawGlassTopHeader(
                memDc, g, W, dpi, theme,
                cfg.manufacturer, L"ONE-CLICK EXPRESS SETUP", cfg.productName,
                hAppIconBig, rsw::ui::GlassGlyph::Bolt, rsw::ui::brand::kSkyCyan);
            RECT heroRc{S(18), S(102), W - S(18), S(168)};
            rsw::ui::DrawHeroBrandCard(
                memDc, g, heroRc, dpi, theme,
                cfg.manufacturer, cfg.tagline, cfg.websiteLabel,
                L"EXPRESS READY", hAppIconBig);
            HGDIOBJ oFo = ::SelectObject(memDc, hFontBody);
            ::SetTextColor(memDc, theme.textSecondary);
            const std::wstring ocCopy = L"Destination: " + installDir + L"  (Click Customize... to pick features or folder)";
            RECT ocRc{S(20), S(178), W - S(20), footerY - (cfg.hasLicenseRtf ? S(38) : S(8))};
            ::DrawTextW(memDc, ocCopy.c_str(), -1, &ocRc, DT_LEFT | DT_WORDBREAK | DT_PATH_ELLIPSIS | DT_NOPREFIX);
            ::SelectObject(memDc, oFo);
            ::BitBlt(hdcOut, 0, 0, W, H, memDc, 0, 0, SRCCOPY);
            ::SelectObject(memDc, oldBmp);
            ::DeleteObject(memBmp);
            ::DeleteDC(memDc);
            return;
        }

        // 1D. Classic `Wizard95` / `Wizard97` / `FullScreenBlue` / `FullScreenTeal` Welcome & Finish Left Sidebar Rail!
        if (UsesClassicWelcomeSidebar()) {
            RECT railRc{S(12), S(12), S(176), footerY - S(10)};
            rsw::ui::DrawClassicLeftSidebarRail(
                memDc, g, railRc, dpi, theme,
                cfg.wizardStyle == rsw::WizardStyleKind::FullScreenTeal,
                hAppIconBig, hArtworkBmp, artworkW, artworkH, cfg.manufacturer);

            HGDIOBJ oFc = ::SelectObject(memDc, hFontHeadline);
            ::SetTextColor(memDc, theme.textPrimary);
            const int rx = S(192);
            if (currentPage == WizardPage::Welcome) {
                RECT hRc{rx, S(18), W - S(18), S(84)};
                ::DrawTextW(memDc, cfg.welcomeHeadline.c_str(), -1, &hRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                ::SelectObject(memDc, hFontBody);
                ::SetTextColor(memDc, theme.textSecondary);
                const std::wstring introCopy = !cfg.welcomeIntro.empty()
                    ? cfg.welcomeIntro
                    : (L"This setup program installs " + cfg.productName + L" (v" + cfg.version +
                       L") onto your computer.\r\n\r\n"
                       L"It is strongly recommended that you save any open work before continuing.\r\n\r\n"
                       L"Choose Next to select your Setup Type, or Cancel to exit Setup without making changes.");
                RECT bRc{rx, S(92), W - S(18), footerY - S(64)};
                ::DrawTextW(memDc, introCopy.c_str(), -1, &bRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                ::SelectObject(memDc, hFontSmall);
                const bool isRaggie = rsw::ui::UpperWide(cfg.manufacturer).find(L"RAGGIESOFT") != std::wstring::npos;
                const std::wstring footNote = isRaggie
                    ? L"Zero Telemetry • Full Keyboard & Screen-Reader Access. Fictitious characters/entities in RaggieSoft works are works of fiction."
                    : (L"Version " + cfg.version + L" • Zero Telemetry • Full Keyboard & Screen-Reader Accessibility.");
                RECT fnRc{rx, footerY - S(54), W - S(18), footerY - S(10)};
                ::DrawTextW(memDc, footNote.c_str(), -1, &fnRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            } else {
                const bool okDone = (finalMsiResult == ERROR_SUCCESS || finalMsiResult == ERROR_SUCCESS_REBOOT_REQUIRED);
                const std::wstring finHead = okDone
                    ? ((selectedAction == SelectedAction::MaintUninstall) ? (cfg.productName + L" Was Removed") : (cfg.productName + L" Setup Complete"))
                    : L"Setup Did Not Complete";
                RECT hRc{rx, S(18), W - S(18), S(84)};
                ::DrawTextW(memDc, finHead.c_str(), -1, &hRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                ::SelectObject(memDc, hFontBody);
                ::SetTextColor(memDc, theme.textSecondary);
                const std::wstring finBody = okDone
                    ? (cfg.productName + L" (v" + cfg.version + L") has been configured in:\r\n" + installDir + L"\r\n\r\nClick Finish to exit Setup.")
                    : (L"Windows Installer returned status code " + std::to_wstring(finalMsiResult) + L".");
                RECT bRc{rx, S(92), W - S(18), footerY - S(58)};
                ::DrawTextW(memDc, finBody.c_str(), -1, &bRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            }
            ::SelectObject(memDc, oFc);
            ::BitBlt(hdcOut, 0, 0, W, H, memDc, 0, 0, SRCCOPY);
            ::SelectObject(memDc, oldBmp);
            ::DeleteObject(memBmp);
            ::DeleteDC(memDc);
            return;
        }

        // 2. Top Header Stage (Autorun `16:9` Cover Stage, Autorun Compact Interior Header, Fluent Mica Step Rail, Wizard97 Header, Aero, or Glass!)
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

        const bool isMica       = (cfg.wizardStyle == rsw::WizardStyleKind::FluentMica);
        const bool isWin95Sheet = rsw::UsesClassic3dDialogSheet(cfg.wizardStyle);
        const bool isWiz97Hdr   = (cfg.wizardStyle == rsw::WizardStyleKind::Wizard97);

        if (cfg.isAutorun16x9) {
            const std::wstring pubUpper = rsw::ui::UpperWide(cfg.manufacturer.empty() ? L"RaggieSoft" : cfg.manufacturer);
            const std::wstring kicker   = pubUpper + L"  |  " + hdrInfo.kickerSuffix;
            rsw::ui::DrawAutorunCompactInteriorHeader(
                memDc, g, W, dpi, theme, cfg.manufacturer, kicker, cfg.productName,
                hArtworkBmp, artworkW, artworkH);
        } else if (isMica) {
            rsw::ui::DrawFluentMicaStepRail(
                memDc, g, S(164), 0, footerY, FluentStepStageIndex(),
                dpi, theme, cfg.productName, hAppIconBig);
            HGDIOBJ oFm = ::SelectObject(memDc, hFontHeadline);
            ::SetTextColor(memDc, theme.textPrimary);
            RECT mTitleRc{S(180), S(10), W - S(18), S(38)};
            ::DrawTextW(memDc, hdrInfo.aeroInstruction, -1, &mTitleRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            ::SelectObject(memDc, oFm);
        } else if (isWin95Sheet) {
            // Authentic Windows 95 (`Wizard95`, `FullScreenBlue`, `FullScreenTeal`) page layout:
            // ALL pages share the exact same `164px` sunken left graphic rail (`S(12)..S(176)`)
            // and bold right-column headline (`S(192)`) as `Welcome` & `Finished` — ZERO Wizard 97 top header!
            RECT railRc{S(12), S(12), S(176), footerY - S(10)};
            rsw::ui::DrawClassicLeftSidebarRail(
                memDc, g, railRc, dpi, theme,
                cfg.wizardStyle == rsw::WizardStyleKind::FullScreenTeal,
                hAppIconBig, hArtworkBmp, artworkW, artworkH, cfg.manufacturer);
            HGDIOBJ oF95 = ::SelectObject(memDc, hFontHeadline);
            ::SetTextColor(memDc, theme.textPrimary);
            RECT t95Rc{S(192), S(14), W - S(18), S(46)};
            ::DrawTextW(memDc, hdrInfo.aeroInstruction, -1, &t95Rc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            ::SelectObject(memDc, oF95);
        } else if (isWiz97Hdr) {
            rsw::ui::DrawWizard97TopHeader(
                memDc, W, dpi, theme,
                false,
                hdrInfo.aeroInstruction, cfg.bannerTitle, hAppIconBig);
        } else if (cfg.isAeroWizard) {
            const std::wstring winCap = cfg.productName + L" Setup";
            rsw::ui::DrawAeroWizardTopHeader(
                memDc, g, W, dpi, theme, winCap, hdrInfo.aeroInstruction,
                hAppIconSmall ? hAppIconSmall : hAppIconBig,
                hdrInfo.glyph, hdrInfo.badgeColor, cfg.manufacturer, false, false);
        } else {
            rsw::ui::DrawGlassTopHeader(
                memDc, g, W, dpi, theme,
                cfg.manufacturer, hdrInfo.kickerSuffix, cfg.productName,
                hAppIconBig, hdrInfo.glyph, hdrInfo.badgeColor);
        }

        // 3. Page-Specific Content Canvas
        const int bodyTop = isMica ? S(42) : (isWin95Sheet ? S(50) : (isWiz97Hdr ? S(80) : (cfg.isAeroWizard ? S(88) : S(104))));
        const int cardX   = isMica ? S(180) : (isWin95Sheet ? S(192) : (cfg.isAeroWizard ? S(46) : S(18)));
        const int cardW   = W - cardX - S(18);
        HGDIOBJ oldF      = ::SelectObject(memDc, hFontBody);

        switch (currentPage) {
            case WizardPage::Welcome: {
                // Hero Brand Card (`"OFFICIAL INSTALLER"`)
                RECT heroRc{cardX, bodyTop, cardX + cardW, bodyTop + S(70)};
                rsw::ui::DrawHeroBrandCard(
                    memDc, g, heroRc, dpi, theme,
                    cfg.manufacturer, cfg.tagline, cfg.websiteLabel,
                    L"OFFICIAL INSTALLER", hAppIconBig);

                int textLeft = cardX + S(4);
                if (hArtworkBmp) {
                    RECT artThumb{cardX, bodyTop + S(82), cardX + S(138), bodyTop + S(246)};
                    DrawGlassPanelBox(g, artThumb, S(10));
                    RECT artInner{artThumb.left + S(4), artThumb.top + S(4), artThumb.right - S(4), artThumb.bottom - S(4)};
                    rsw::ui::DrawAlphaBitmapFit(memDc, hArtworkBmp, artworkW, artworkH, artInner);
                    textLeft = cardX + S(152);
                }

                ::SelectObject(memDc, (cfg.welcomeHeadline.size() > 36) ? hFontSection : hFontHeadline);
                ::SetTextColor(memDc, theme.textPrimary);
                RECT headRc{textLeft, bodyTop + S(80), W - S(22), bodyTop + S(134)};
                ::DrawTextW(memDc, cfg.welcomeHeadline.c_str(), -1, &headRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                // Bottom Trust & Fictitious Disclaimer Strip (`S(66)` tall so 3-line disclaimer never clips!)
                RECT tipCard{cardX, footerY - S(74), cardX + cardW, footerY - S(8)};
                DrawGlassPanelBox(g, tipCard, S(8));

                ::SelectObject(memDc, hFontBody);
                ::SetTextColor(memDc, theme.textSecondary);
                const std::wstring introCopy = !cfg.welcomeIntro.empty()
                    ? cfg.welcomeIntro
                    : (L"This wizard installs " + cfg.productName + L" (v" + cfg.version +
                       L") on your computer using native Windows Installer transaction safety.\r\n\r\n"
                       L"Click Next to choose Express Setup for one-click installation, or Custom Setup to choose your destination folder and shortcuts.");
                RECT introRc{textLeft, bodyTop + S(138), W - S(22), tipCard.top - S(8)};
                ::DrawTextW(memDc, introCopy.c_str(), -1, &introRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

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
                    RECT pRc{cardX, bodyTop, cardX + cardW, bodyTop + S(36)};
                    ::DrawTextW(memDc, L"Please review the software license agreement below before continuing:", -1, &pRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                }
                break;
            }

            case WizardPage::SetupType: {
                ::SelectObject(memDc, hFontBold);
                ::SetTextColor(memDc, theme.textPrimary);
                const std::wstring prompt = L"Choose how you would like to install " + cfg.productName + L":";
                RECT pRc{cardX, bodyTop, cardX + cardW, bodyTop + S(38)};
                ::DrawTextW(memDc, prompt.c_str(), -1, &pRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            }

            case WizardPage::CustomFeatures: {
                ::SelectObject(memDc, hFontBold);
                ::SetTextColor(memDc, theme.textPrimary);
                RECT pRc{cardX, bodyTop, cardX + cardW, bodyTop + S(38)};
                ::DrawTextW(memDc, L"Click any feature's drive icon (or press Space) to choose how it is installed:", -1, &pRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                // `Feature Details` + Live Byte Cost Card (stacked below `SelectionTree` on `Wizard95` so both get full width, or side-by-side on wide sheets!)
                const int treeW  = (cardW * 58) / 100;
                const int detX   = cardX + treeW + S(12);
                RECT detCard = isWin95Sheet
                    ? RECT{cardX, bodyTop + S(196), cardX + cardW, bodyTop + S(340)}
                    : RECT{detX,  bodyTop + S(40),  cardX + cardW, footerY - S(14)};
                DrawPanelSurface(memDc, g, detCard, S(10));

                if (!featureNodes.empty() && selectedFeatureIdx < featureNodes.size()) {
                    const auto& fn = featureNodes[selectedFeatureIdx];
                    std::wstring volLbl;
                    std::uint64_t freeB = 0;
                    const bool hasVol = QueryVolumeFreeBytesForPath(installDir, volLbl, freeB);
                    const COLORREF stateTextCol = theme.isHighContrast
                        ? theme.textPrimary
                        : (theme.isDarkMode ? RGB(125, 211, 252) : RGB(30, 64, 175));
                    const std::wstring stTxt = rsw::DescribeMsiTreeState(fn.currentState);

                    if (isWin95Sheet) {
                        ::SelectObject(memDc, hFontBold);
                        ::SetTextColor(memDc, theme.textPrimary);
                        RECT tRc{detCard.left + S(12), detCard.top + S(7), detCard.right - S(12), detCard.top + S(27)};
                        ::DrawTextW(memDc, fn.title.c_str(), -1, &tRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                        rsw::PaintSingleMsiStateCell(
                            g, static_cast<float>(detCard.left + S(12)), static_cast<float>(detCard.top + S(31)),
                            static_cast<float>(S(30)), static_cast<float>(S(18)), fn.currentState, theme);
                        ::SelectObject(memDc, hFontSmall);
                        ::SetTextColor(memDc, stateTextCol);
                        RECT stRc{detCard.left + S(48), detCard.top + S(29), detCard.right - S(12), detCard.top + S(63)};
                        ::DrawTextW(memDc, stTxt.c_str(), -1, &stRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                        ::SelectObject(memDc, hFontBody);
                        ::SetTextColor(memDc, theme.textSecondary);
                        RECT dRc{detCard.left + S(12), detCard.top + S(65), detCard.right - S(12), detCard.bottom - S(28)};
                        ::DrawTextW(memDc, fn.description.c_str(), -1, &dRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                        ::SelectObject(memDc, hFontSmall);
                        ::SetTextColor(memDc, theme.textPrimary);
                        std::wstring costLine = L"Feature: " + FormatByteSizeFriendly(fn.ownBytes) +
                                                L"  •  Selected: " + FormatByteSizeFriendly(estimatedReqBytes);
                        if (hasVol) costLine += L"  •  Free: " + FormatByteSizeFriendly(freeB);
                        RECT cRc{detCard.left + S(12), detCard.bottom - S(24), detCard.right - S(12), detCard.bottom - S(5)};
                        ::DrawTextW(memDc, costLine.c_str(), -1, &cRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                    } else {
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
                        ::SetTextColor(memDc, stateTextCol);
                        RECT stRc{detCard.left + S(50), detCard.top + S(39), detCard.right - S(12), detCard.top + S(88)};
                        ::DrawTextW(memDc, stTxt.c_str(), -1, &stRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                        ::SelectObject(memDc, hFontBody);
                        ::SetTextColor(memDc, theme.textSecondary);
                        RECT dRc{detCard.left + S(14), detCard.top + S(92), detCard.right - S(14), detCard.bottom - S(72)};
                        ::DrawTextW(memDc, fn.description.c_str(), -1, &dRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

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
                }
                break;
            }

            case WizardPage::CustomFolder: {
                if (isWin95Sheet) {
                    ::SelectObject(memDc, hFontBody);
                    ::SetTextColor(memDc, theme.textSecondary);
                    const std::wstring dirIntro =
                        L"Setup will install " + cfg.productName + L" in the following folder.\r\n"
                        L"To install to a different folder, enter it below or click Browse.";
                    RECT hRc{cardX, bodyTop, cardX + cardW, bodyTop + S(62)};
                    ::DrawTextW(memDc, dirIntro.c_str(), -1, &hRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                } else {
                    ::SelectObject(memDc, hFontSection);
                    ::SetTextColor(memDc, theme.textPrimary);
                    RECT hRc{cardX, bodyTop + S(2), cardX + cardW, bodyTop + S(30)};
                    ::DrawTextW(memDc, L"Select Destination Folder", -1, &hRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                }

                // Live `GetDiskFreeSpaceExW` Glass Capacity Card!
                wchar_t editBuf[MAX_PATH * 4]{};
                std::wstring livePath = installDir;
                if (hDirEdit && ::GetWindowTextW(hDirEdit, editBuf, static_cast<int>(std::size(editBuf))) > 0) {
                    livePath = editBuf;
                }
                std::wstring volLabel;
                std::uint64_t freeBytes = 0;
                const bool hasDisk = QueryVolumeFreeBytesForPath(livePath, volLabel, freeBytes);

                const int diskTopOff = isWin95Sheet ? S(138) : S(114);
                RECT diskCard{cardX, bodyTop + diskTopOff, cardX + cardW, bodyTop + diskTopOff + S(100)};
                DrawPanelSurface(memDc, g, diskCard, S(10));
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
                if (isWin95Sheet) {
                    ::SelectObject(memDc, hFontBody);
                    ::SetTextColor(memDc, theme.textSecondary);
                    RECT hRc{cardX, bodyTop, cardX + cardW, bodyTop + S(58)};
                    ::DrawTextW(memDc, L"Setup will add program icons to the Start Menu folder listed below. You may type a new folder name or choose optional shortcuts:", -1, &hRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                } else {
                    ::SelectObject(memDc, hFontSection);
                    ::SetTextColor(memDc, theme.textPrimary);
                    RECT hRc{cardX, bodyTop + S(2), cardX + cardW, bodyTop + S(32)};
                    ::DrawTextW(memDc, L"Configure Start Menu & Shortcuts", -1, &hRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                }
                break;
            }

            case WizardPage::ReadyToInstall: {
                int sumTop = bodyTop + S(78);
                if (!isWin95Sheet) {
                    RECT heroRc{cardX, bodyTop, cardX + cardW, bodyTop + S(66)};
                    rsw::ui::DrawHeroBrandCard(
                        memDc, g, heroRc, dpi, theme,
                        cfg.manufacturer, cfg.tagline, cfg.websiteLabel,
                        L"READY TO INSTALL", hAppIconBig);
                } else {
                    ::SelectObject(memDc, hFontBody);
                    ::SetTextColor(memDc, theme.textPrimary);
                    RECT intro95{cardX, bodyTop + S(2), cardX + cardW, bodyTop + S(66)};
                    ::DrawTextW(memDc, L"Setup has enough information to start copying the program files. If you want to review or change any settings, click Back. If you are satisfied with the settings, click Install.", -1, &intro95, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                    sumTop = bodyTop + S(72);
                }

                RECT sumCard{cardX, sumTop, cardX + cardW, footerY - S(14)};
                if (isWin95Sheet) {
                    HBRUSH hSunk = ::CreateSolidBrush(theme.cardBg);
                    ::FillRect(memDc, &sumCard, hSunk);
                    ::DeleteObject(hSunk);
                    rsw::ui::DrawClassic3dBevelFrame(memDc, sumCard, true, theme);
                } else {
                    DrawGlassPanelBox(g, sumCard, S(10));
                }

                ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.textPrimary);
                RECT shRc{sumCard.left + S(16), sumCard.top + S(12), sumCard.right - S(16), sumCard.top + S(38)};
                ::DrawTextW(memDc, L"Review Your Setup Summary", -1, &shRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

                std::wstring modeName = selectedSetupTypeLabel.empty() ? L"Express Setup (Recommended)" : selectedSetupTypeLabel;
                if (selectedAction == SelectedAction::MaintModify) modeName = L"Modify Installed Features";

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
                if (!isWin95Sheet) {
                    RECT heroRc{cardX, bodyTop, cardX + cardW, bodyTop + S(62)};
                    rsw::ui::DrawHeroBrandCard(
                        memDc, g, heroRc, dpi, theme,
                        cfg.manufacturer, cfg.tagline, cfg.websiteLabel,
                        L"MAINTENANCE WIZARD", hAppIconBig);
                } else {
                    ::SelectObject(memDc, hFontBody);
                    ::SetTextColor(memDc, theme.textPrimary);
                    RECT m95{cardX, bodyTop, cardX + cardW, bodyTop + S(36)};
                    ::DrawTextW(memDc, L"Modify, repair, or remove installed program components:", -1, &m95, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                }
                break;
            }

            case WizardPage::RepairMode: {
                ::SelectObject(memDc, hFontBold);
                ::SetTextColor(memDc, theme.textPrimary);
                RECT hRc{cardX, bodyTop, cardX + cardW, bodyTop + S(40)};
                ::DrawTextW(memDc, L"Choose how you would like Setup to repair your installation:", -1, &hRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            }

            case WizardPage::SafeUninstall: {
                int boxTop = bodyTop + S(78);
                if (!isWin95Sheet) {
                    RECT heroRc{cardX, bodyTop, cardX + cardW, bodyTop + S(66)};
                    rsw::ui::DrawHeroBrandCard(
                        memDc, g, heroRc, dpi, theme,
                        cfg.manufacturer, cfg.tagline, cfg.websiteLabel,
                        L"SAFE UNINSTALL", hAppIconBig);
                } else {
                    boxTop = bodyTop + S(8);
                }

                RECT boxRc{cardX, boxTop, cardX + cardW, footerY - S(16)};
                if (isWin95Sheet) {
                    rsw::ui::DrawClassic3dBevelFrame(memDc, boxRc, true, theme);
                } else {
                    DrawGlassPanelBox(g, boxRc, S(10));
                }

                ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.textPrimary);
                const std::wstring uHead = L"Remove " + cfg.productName + L" from Your Computer?";
                RECT uhRc{boxRc.left + S(16), boxRc.top + S(12), boxRc.right - S(16), boxRc.top + S(60)};
                ::DrawTextW(memDc, uHead.c_str(), -1, &uhRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                ::SelectObject(memDc, hFontBody);
                ::SetTextColor(memDc, theme.textSecondary);
                const std::wstring uSub = L"Your personal documents and projects are NEVER deleted. Optional cleanup items below are left unchecked by default for safety:";
                RECT usRc{boxRc.left + S(16), boxRc.top + S(62), boxRc.right - S(16), boxRc.top + S(114)};
                ::DrawTextW(memDc, uSub.c_str(), -1, &usRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                break;
            }

            case WizardPage::Installing: {
                int progTop = bodyTop + S(74);
                if (!isWin95Sheet) {
                    RECT heroRc{cardX, bodyTop, cardX + cardW, bodyTop + S(64)};
                    rsw::ui::DrawHeroBrandCard(
                        memDc, g, heroRc, dpi, theme,
                        cfg.manufacturer, cfg.tagline, cfg.websiteLabel,
                        L"CONFIGURING SYSTEM", hAppIconBig);
                } else {
                    progTop = bodyTop + S(10);
                }

                RECT progCard{cardX, progTop, cardX + cardW, progTop + S(206)};
                DrawPanelSurface(memDc, g, progCard, S(10));

                ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.textPrimary);
                std::wstring phaseTitle = L"Installing " + cfg.productName + L"...";
                if (selectedAction == SelectedAction::MaintUninstall)     phaseTitle = L"Removing " + cfg.productName + L"...";
                if (selectedAction == SelectedAction::MaintQuickRepair ||
                    selectedAction == SelectedAction::MaintFullReinstall) phaseTitle = L"Repairing " + cfg.productName + L"...";

                RECT ptRc{progCard.left + S(16), progCard.top + S(12), progCard.right - S(76), progCard.top + S(58)};
                ::DrawTextW(memDc, phaseTitle.c_str(), -1, &ptRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

                const int pct = std::clamp(currentPermille / 10, 0, 100);
                const std::wstring pctStr = std::to_wstring(pct) + L"%";
                RECT pctRc{progCard.right - S(74), progCard.top + S(12), progCard.right - S(16), progCard.top + S(40)};
                ::DrawTextW(memDc, pctStr.c_str(), -1, &pctRc, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);

                const std::wstring etaStr = FormatEstimatedTimeRemaining(installStartTickMs, currentPermille);
                if (!etaStr.empty()) {
                    ::SelectObject(memDc, hFontSmall);
                    ::SetTextColor(memDc, theme.textSecondary);
                    RECT etaRc{progCard.left + S(16), progCard.bottom - S(30), progCard.right - S(16), progCard.bottom - S(8)};
                    ::DrawTextW(memDc, etaStr.c_str(), -1, &etaRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
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

                std::wstring finHead = okDone ? (cfg.productName + L" Is Ready!") : L"Setup Did Not Complete";
                if (okDone && selectedAction == SelectedAction::MaintUninstall) {
                    finHead = cfg.productName + L" Was Removed";
                }
                ::SelectObject(memDc, (finHead.size() > 34) ? hFontSection : hFontHeadline);
                ::SetTextColor(memDc, theme.textPrimary);
                RECT fhRc{finCard.left + S(18), finCard.top + S(12), finCard.right - S(18), finCard.top + S(68)};
                ::DrawTextW(memDc, finHead.c_str(), -1, &fhRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

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
                RECT fbRc{finCard.left + S(18), finCard.top + S(70), finCard.right - S(18), finCard.bottom - S(12)};
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

    static constexpr UINT_PTR kPassiveAutostartTimerId = 77;

    // ----------------------------------------------------------------------------
    // FULL-MONITOR RETRO 1995 COBALT & MICROSOFT ACME `#008080` TEAL BACKDROP WNDPROC
    // ----------------------------------------------------------------------------
    static LRESULT CALLBACK BackdropWndProc(HWND hBack, UINT msg, WPARAM wParam, LPARAM lParam) {
        SetupWizardApp* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<SetupWizardApp*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hBack, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        } else {
            self = reinterpret_cast<SetupWizardApp*>(::GetWindowLongPtrW(hBack, GWLP_USERDATA));
        }
        if (!self) return ::DefWindowProcW(hBack, msg, wParam, lParam);

        switch (msg) {
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdcOut = ::BeginPaint(hBack, &ps);
                RECT rc{};
                ::GetClientRect(hBack, &rc);
                const int W = std::max<int>(1, rc.right - rc.left);
                const int H = std::max<int>(1, rc.bottom - rc.top);
                HDC memDc   = ::CreateCompatibleDC(hdcOut);
                HBITMAP bmp = ::CreateCompatibleBitmap(hdcOut, W, H);
                HGDIOBJ oB  = ::SelectObject(memDc, bmp);

                const rsw::BillboardSlideSpec* bbPtr = nullptr;
                if (self->currentPage == WizardPage::Installing && !self->cfg.billboards.empty()) {
                    const std::size_t bbIdx = static_cast<std::size_t>(std::max(0, self->currentPermille / 250)) % self->cfg.billboards.size();
                    bbPtr = &self->cfg.billboards[bbIdx];
                }
                std::wstring statCopy;
                {
                    std::lock_guard<std::mutex> lk(self->msgMutex);
                    statCopy = self->latestStatusText;
                }
                {
                    Gdiplus::Graphics g(memDc);
                    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                    g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
                    rsw::ui::DrawRetroFullScreenBackdrop(
                        memDc, g, rc, ::GetDpiForWindow(hBack), self->theme,
                        self->cfg.wizardStyle == rsw::WizardStyleKind::FullScreenTeal,
                        self->cfg.productName, bbPtr,
                        false /* Live docked bottom-right hWnd progress meter owns progress + Cancel! */,
                        self->currentPermille, statCopy);
                }

                ::BitBlt(hdcOut, 0, 0, W, H, memDc, 0, 0, SRCCOPY);
                ::SelectObject(memDc, oB);
                ::DeleteObject(bmp);
                ::DeleteDC(memDc);
                ::EndPaint(hBack, &ps);
                return 0;
            }
            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
                ::InvalidateRect(hBack, nullptr, TRUE);
                break;
        }
        return ::DefWindowProcW(hBack, msg, wParam, lParam);
    }

    /// Renders a deterministic `1280 x 800` composite PNG capturing BOTH the full-screen 1995
    /// Cobalt-Blue (`#000080`) or Microsoft ACME Setup Engine Teal (`#008080`) backdrop (with
    /// italic serif drop-shadowed title, floating 3D `[Billboard]` card, & bottom-right sunken
    /// progress gauge) AND the live Win32 `#C0C0C0` 3D beveled `SetupWizard.exe` dialog!
    bool CaptureRetroFullScreenCompositePng(const std::filesystem::path& outPngPath) {
        if (!hWnd || !::IsWindow(hWnd)) return false;

        ::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
        MSG msg{};
        for (int pass = 0; pass < 4; ++pass) {
            while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                ::TranslateMessage(&msg);
                ::DispatchMessageW(&msg);
            }
            ::Sleep(20);
        }

        RECT dlgRc{};
        if (!::GetWindowRect(hWnd, &dlgRc)) return false;
        const int dlgW = dlgRc.right - dlgRc.left;
        const int dlgH = dlgRc.bottom - dlgRc.top;
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        const int canvasW = std::max(S(1280), dlgW + S(520));
        const int canvasH = std::max(S(800),  dlgH + S(210));

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = canvasW;
        bmi.bmiHeader.biHeight      = -canvasH;
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* pvCanvas = nullptr;
        HDC screenDc   = ::GetDC(nullptr);
        HDC canvasDc   = ::CreateCompatibleDC(screenDc);
        HBITMAP hCanvasDib = ::CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &pvCanvas, nullptr, 0);

        BITMAPINFO dlgBmi = bmi;
        dlgBmi.bmiHeader.biWidth  = dlgW;
        dlgBmi.bmiHeader.biHeight = -dlgH;
        void* pvDlg = nullptr;
        HDC dlgDc   = ::CreateCompatibleDC(screenDc);
        HBITMAP hDlgDib = ::CreateDIBSection(screenDc, &dlgBmi, DIB_RGB_COLORS, &pvDlg, nullptr, 0);
        ::ReleaseDC(nullptr, screenDc);

        if (!hCanvasDib || !pvCanvas || !hDlgDib || !pvDlg) {
            if (hCanvasDib) ::DeleteObject(hCanvasDib);
            if (hDlgDib)    ::DeleteObject(hDlgDib);
            ::DeleteDC(canvasDc);
            ::DeleteDC(dlgDc);
            return false;
        }

        HGDIOBJ oldCanvasBmp = ::SelectObject(canvasDc, hCanvasDib);
        HGDIOBJ oldDlgBmp    = ::SelectObject(dlgDc, hDlgDib);

        rsw::BillboardSlideSpec demoBillboard{
            L"Zero-Dependency Win32 C++20 Packaging",
            L"Author-owned .MSI, .MSM, .MST, .MSP & Universal x64+ARM64 Setup.exe with Dual-Layer Authenticode signing.",
            rsw::ui::GlassGlyph::ShieldCheck
        };
        if (!cfg.billboards.empty()) {
            demoBillboard = cfg.billboards.front();
        }

        {
            Gdiplus::Graphics g(canvasDc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
            rsw::ui::DrawRetroFullScreenBackdrop(
                canvasDc, g, RECT{0, 0, canvasW, canvasH}, dpi, theme,
                cfg.wizardStyle == rsw::WizardStyleKind::FullScreenTeal,
                cfg.productName, &demoBillboard,
                true /* showProgressGauge */, 640 /* 64% */, L"Copying native 64-bit program files...");
        }

        if (!::PrintWindow(hWnd, dlgDc, 0x00000002u)) {
            if (!::PrintWindow(hWnd, dlgDc, 0)) {
                ::SendMessageW(hWnd, WM_PRINT, reinterpret_cast<WPARAM>(dlgDc),
                               PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
            }
        }
        ::GdiFlush();

        // Place the floating `#C0C0C0` 3D beveled dialog in the classic left-centered 1995 zone
        // so the top-left serif title, top-right Billboard card, AND bottom-right progress gauge are 100% visible!
        const int placeX = S(64);
        const int placeY = S(116);
        RECT dropShadow{placeX + S(6), placeY + S(6), placeX + dlgW + S(6), placeY + dlgH + S(6)};
        HBRUSH hShBr = ::CreateSolidBrush(RGB(0, 0, 0));
        ::FillRect(canvasDc, &dropShadow, hShBr);
        ::DeleteObject(hShBr);

        ::BitBlt(canvasDc, placeX, placeY, dlgW, dlgH, dlgDc, 0, 0, SRCCOPY);
        ::GdiFlush();

        auto* bytes = static_cast<std::uint8_t*>(pvCanvas);
        const std::size_t totalPx = static_cast<std::size_t>(canvasW) * static_cast<std::size_t>(canvasH);
        for (std::size_t i = 0; i < totalPx; ++i) {
            bytes[i * 4 + 3] = 0xFF;
        }
        const bool ok = rsw::ui::SaveBgraPixelsToPngFile(bytes, canvasW, canvasH, outPngPath);

        ::SelectObject(dlgDc, oldDlgBmp);
        ::SelectObject(canvasDc, oldCanvasBmp);
        ::DeleteObject(hDlgDib);
        ::DeleteObject(hCanvasDib);
        ::DeleteDC(dlgDc);
        ::DeleteDC(canvasDc);
        return ok;
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
        if (!self->isHeadlessExport && self->taskbarProgress.HandleMessage(hWnd, msg)) {
            return 0;
        }
        if (!self->isHeadlessExport && self->wmActivateInstance != 0 && msg == self->wmActivateInstance) {
            if (::IsIconic(hWnd)) {
                ::ShowWindow(hWnd, SW_RESTORE);
            }
            if (self->hBackdropWnd && ::IsWindow(self->hBackdropWnd)) {
                ::ShowWindow(self->hBackdropWnd, SW_SHOWNOACTIVATE);
            }
            ::SetForegroundWindow(hWnd);
            ::BringWindowToTop(hWnd);
            if (self->hBtnPrimary && ::IsWindowVisible(self->hBtnPrimary) && ::IsWindowEnabled(self->hBtnPrimary)) {
                ::SetFocus(self->hBtnPrimary);
            }
            return 0;
        }

        switch (msg) {
            case WM_CREATE:
                if (!self->isHeadlessExport) {
                    self->taskbarProgress.AttachWindow(hWnd);
                    self->wmActivateInstance = rsw::RegisterSingleInstanceWindowTag(hWnd, self->instanceKey);
                }
                self->CreateControls();
                self->EnsureCustomSystemMenuAbout();
                self->RefreshEffectiveThemeAndChrome();
                self->ApplyFontsAndLayout();
                return 0;

            case WM_SYSCOMMAND: {
                const UINT cmdId = static_cast<UINT>(wParam & 0xFFF0u);
                if (cmdId == SC_CLOSE && self->installRunning.load() && self->cancelLockedPointOfNoReturn.load()) {
                    return 0; // Point of no return (removing `.rbf` rollback files): block Close!
                }
                if (cmdId == IDM_SYS_ABOUT_SETUP) {
                    const std::wstring cap  = L"About " + self->cfg.productName + L" Setup";
                    const std::wstring head = self->cfg.productName + L" (v" + self->cfg.version + L")";
                    const std::wstring body = L"Publisher: " + self->cfg.manufacturer +
                        (self->cfg.websiteLabel.empty() ? L"" : (L"\nWebsite: " + self->cfg.websiteLabel)) +
                        L"\n\nFull Keyboard Navigation (Tab, Arrow Keys, Alt+Mnemonics) and Windows Contrast Theme Accessibility Enabled.";
                    ::TaskDialog(hWnd, ::GetModuleHandleW(nullptr), cap.c_str(), head.c_str(), body.c_str(),
                                 TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
                    return 0;
                }
                break;
            }

            case WM_TIMER:
                if (wParam == kPassiveAutostartTimerId) {
                    ::KillTimer(hWnd, kPassiveAutostartTimerId);
                    if (self->IsPassiveToastMode() && !self->passiveAutoStarted) {
                        self->passiveAutoStarted = true;
                        self->selectedAction = self->alreadyInstalled
                            ? SelectedAction::MaintQuickRepair
                            : SelectedAction::InstallExpress;
                        self->StartMsiWorkerTransaction();
                    }
                    return 0;
                }
                break;

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
                const bool onCard = (!self->IsRetroDockedProgressMode() && hChild == self->hProgStatus) ||
                                    hChild == self->hUninstCache || hChild == self->hUninstPrefs;
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
                    self->RefreshEffectiveThemeAndChrome();
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
                    case IDC_MODE_COMPACT:
                    case IDC_MODE_CUSTOM:
                    case IDC_MODE_CARD_4:
                        self->SyncSelectedSetupTypeIndex(id - IDC_MODE_EXPRESS);
                        self->UpdateControlVisibilityForPage();
                        return 0;

                    case IDC_ONECLICK_CUST:
                        self->oneClickExpanded = true;
                        self->selectedAction   = SelectedAction::InstallCustom;
                        self->ResizeDialogForActiveStyle();
                        self->EnsureFeatureTreeLoaded();
                        self->NavigateTo(
                            (self->cfg.hasLicenseRtf && ::SendMessageW(self->hLicAccept, BM_GETCHECK, 0, 0) != BST_CHECKED)
                                ? WizardPage::License
                                : WizardPage::CustomFeatures);
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
                            if (self->cancelLockedPointOfNoReturn.load()) {
                                return 0; // Point of no return (`.rbf` cleanup in progress)!
                            }
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
                if (!self->isHeadlessExport) {
                    self->taskbarProgress.SetNormalPermille(hWnd, perm);
                }
                const int decile = perm / 100;
                if (decile != self->lastAnnouncedDecile) {
                    self->lastAnnouncedDecile = decile;
                    ::NotifyWinEvent(EVENT_OBJECT_VALUECHANGE, self->hProgBar, OBJID_CLIENT, CHILDID_SELF);
                }
                ::InvalidateRect(hWnd, nullptr, FALSE);
                if (self->hBackdropWnd) ::InvalidateRect(self->hBackdropWnd, nullptr, FALSE);
                return 0;
            }

            case WM_RSW_STATUS_TEXT: {
                std::wstring txt;
                {
                    std::lock_guard<std::mutex> lk(self->msgMutex);
                    txt = self->latestStatusText;
                }
                if (self->IsRetroDockedProgressMode() || self->IsPassiveToastMode() || self->IsCompactOneClickSplash()) {
                    txt = CompactSingleLineProgressText(txt);
                }
                ::SetWindowTextW(self->hProgStatus, txt.c_str());
                if (self->hBackdropWnd) ::InvalidateRect(self->hBackdropWnd, nullptr, FALSE);
                return 0;
            }

            case WM_RSW_ASK_RETRY_DL: {
                auto* p = reinterpret_cast<RetryDownloadPromptCtx*>(wParam);
                if (!p) return 0;
                if (!self->isHeadlessExport) {
                    self->taskbarProgress.SetPaused(hWnd);
                }
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
                if (p->retryChosen && !self->isHeadlessExport) {
                    self->taskbarProgress.SetIndeterminate(hWnd);
                }
                return 0;
            }

            case WM_RSW_REQUEST_UAC: {
                auto* u = reinterpret_cast<UacLaunchPromptCtx*>(wParam);
                if (!u) return 0;
                if (!self->isHeadlessExport) {
                    self->taskbarProgress.SetPaused(hWnd);
                }
                wchar_t selfExe[MAX_PATH * 4]{};
                ::GetModuleFileNameW(nullptr, selfExe, static_cast<DWORD>(std::size(selfExe)));
                const std::wstring workerParams = L"--elevated-msi-worker \"" + u->jobFilePath + L"\"";

                // Anchor UAC to the full-screen backdrop window (when active) or centered wizard window
                // so Windows 11 always centers the UAC prompt in the middle of the screen!
                const HWND uacOwner = (self->hBackdropWnd && ::IsWindow(self->hBackdropWnd)) ? self->hBackdropWnd : hWnd;

                SHELLEXECUTEINFOW sei{};
                sei.cbSize       = sizeof(sei);
                sei.fMask        = SEE_MASK_NOCLOSEPROCESS;
                sei.hwnd         = uacOwner;
                sei.lpVerb       = L"runas";
                sei.lpFile       = selfExe;
                sei.lpParameters = workerParams.c_str();
                sei.nShow        = SW_HIDE;
                if (!::ShellExecuteExW(&sei) || !sei.hProcess) {
                    u->win32Error     = ::GetLastError();
                    u->canceledByUser = (u->win32Error == ERROR_CANCELLED);
                } else {
                    u->hChildProcess = sei.hProcess;
                    // UAC approved! Transition smoothly into the docked 3D progress meter + live taskbar bar!
                    self->awaitingUacElevation.store(false);
                    if (!self->isHeadlessExport) {
                        self->taskbarProgress.SetIndeterminate(hWnd);
                    }
                    self->ResizeDialogForActiveStyle();
                    if (self->hBackdropWnd) ::InvalidateRect(self->hBackdropWnd, nullptr, FALSE);
                }
                return 0;
            }

            case WM_RSW_LOCK_CANCEL:
                self->SetCloseAndCancelLocked(true);
                return 0;

            case WM_RSW_QUEUE_STATE:
                if (!self->isHeadlessExport) {
                    if (wParam != 0) {
                        self->taskbarProgress.SetPaused(hWnd);
                    } else {
                        self->taskbarProgress.SetIndeterminate(hWnd);
                    }
                }
                return 0;

            case WM_RSW_INSTALL_DONE:
                self->awaitingUacElevation.store(false);
                if (static_cast<UINT>(wParam) == kSentinelUacDeclined) {
                    // User clicked "No" on UAC — clear taskbar bar, restore dialog geometry, and return to `pageBeforeInstall`!
                    self->installRunning.store(false);
                    self->cancelLockedPointOfNoReturn.store(false);
                    self->SetCloseAndCancelLocked(false);
                    if (!self->isHeadlessExport) {
                        self->taskbarProgress.SetNoProgress(hWnd);
                    }
                    self->currentPage = self->pageBeforeInstall;
                    self->ResizeDialogForActiveStyle();
                    if (self->hBtnPrimary) ::SetFocus(self->hBtnPrimary);
                    if (self->hBackdropWnd) ::InvalidateRect(self->hBackdropWnd, nullptr, FALSE);
                    return 0;
                }
                self->OnTransactionCompleted();
                return 0;

            case WM_CLOSE:
                if (self->installRunning.load()) {
                    if (self->cancelLockedPointOfNoReturn.load()) {
                        return 0; // Cannot cancel or close once `.rbf` rollback files are being removed!
                    }
                    self->cancelRequested.store(true);
                    return 0;
                }
                ::DestroyWindow(hWnd);
                return 0;

            case WM_DESTROY:
                rsw::UnregisterSingleInstanceWindowTag(hWnd, self->instanceKey);
                self->taskbarProgress.Release();
                if (self->hBackdropWnd && ::IsWindow(self->hBackdropWnd)) {
                    HWND hBack = self->hBackdropWnd;
                    self->hBackdropWnd = nullptr;
                    ::DestroyWindow(hBack);
                }
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
    long long             totalTicks   = 1000;
    long long             curTicks     = 0;
    long long             stepTicks    = 1;
    bool                  forward      = true;
    bool                  cancelLocked = false;
    bool                  queuePaused  = false;
    int                   permille     = 0;
    std::string           statusUtf8   = "Applying Windows Installer changes...";
    std::string           errorUtf8;
    std::wstring          curActionDesc;
    std::wstring          curActionTmpl;

    void FlushToDisk() const {
        if (statusFile.empty()) return;
        std::ofstream out(statusFile, std::ios::binary | std::ios::trunc);
        if (!out) return;
        auto encodeLines = [](std::string s) {
            std::string clean;
            clean.reserve(s.size());
            for (char c : s) {
                if (c == '\r') continue;
                clean.push_back(c == '\n' ? '\t' : c);
            }
            return clean;
        };
        out << permille << "\n"
            << encodeLines(statusUtf8) << "\n"
            << encodeLines(errorUtf8)  << "\n"
            << (cancelLocked ? "1" : "0") << "\n"
            << (queuePaused  ? "1" : "0") << "\n";
    }
};

INT CALLBACK OnElevatedMsiRecord(void* pvContext, UINT iMessageType, MSIHANDLE hRecord) {
    auto* st = static_cast<ElevatedWorkerProgressState*>(pvContext);
    if (!st) return 0;
    std::error_code ec;
    if (!st->cancelLocked && !st->cancelFile.empty() && std::filesystem::exists(st->cancelFile, ec)) {
        return IDCANCEL;
    }

    const UINT msgKind = iMessageType & 0xFF000000u;
    switch (msgKind) {
        case INSTALLMESSAGE_COMMONDATA: {
            if (::MsiRecordGetFieldCount(hRecord) >= 2 &&
                ::MsiRecordGetInteger(hRecord, 1) == 2 &&
                ::MsiRecordGetInteger(hRecord, 2) == 0) {
                if (!st->cancelLocked) {
                    st->cancelLocked = true;
                    st->FlushToDisk();
                }
            }
            return IDOK;
        }
        case INSTALLMESSAGE_ACTIONSTART: {
            wchar_t actBuf[256]{};
            wchar_t descBuf[512]{};
            wchar_t tmplBuf[512]{};
            DWORD cAct  = static_cast<DWORD>(std::size(actBuf));
            DWORD cDesc = static_cast<DWORD>(std::size(descBuf));
            DWORD cTmpl = static_cast<DWORD>(std::size(tmplBuf));
            ::MsiRecordGetStringW(hRecord, 1, actBuf,  &cAct);
            ::MsiRecordGetStringW(hRecord, 2, descBuf, &cDesc);
            ::MsiRecordGetStringW(hRecord, 3, tmplBuf, &cTmpl);
            if (std::wcscmp(actBuf, L"RollbackCleanup") == 0 ||
                std::wcsstr(descBuf, L"Removing backup") != nullptr) {
                st->cancelLocked = true;
            }
            if (descBuf[0] != L'\0') {
                st->curActionDesc = SanitizeUnexpandedMsiTokens(descBuf);
                st->curActionTmpl = tmplBuf;
                if (!st->curActionDesc.empty()) {
                    st->statusUtf8 = rsw::WideToUtf8(st->curActionDesc);
                    st->FlushToDisk();
                }
            } else if (st->cancelLocked) {
                st->FlushToDisk();
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
    const std::wstring prodName   = rsw::Utf8ToWide(kv["ProductName"].empty() ? "Application" : kv["ProductName"]);
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

    UINT rc = ExecuteMsiCallWithPoliteQueue(
        prodName,
        [&]() {
            std::error_code cEc;
            return !st.cancelFile.empty() && std::filesystem::exists(st.cancelFile, cEc);
        },
        [&](const std::wstring& waitMsg) {
            st.queuePaused = true;
            st.statusUtf8  = rsw::WideToUtf8(waitMsg);
            st.FlushToDisk();
        },
        [&]() {
            st.queuePaused = false;
            st.statusUtf8  = "Starting Windows Installer transaction...";
            st.FlushToDisk();
        },
        [&]() -> UINT {
            if (mode == "uninstall" && !prodCode.empty()) {
                return ::MsiConfigureProductExW(prodCode.c_str(), INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT, props.c_str());
            }
            if (mode == "quickrepair" && !prodCode.empty()) {
                return ::MsiReinstallProductW(
                    prodCode.c_str(),
                    REINSTALLMODE_FILEMISSING | REINSTALLMODE_FILEOLDERVERSION | REINSTALLMODE_FILEVERIFY |
                    REINSTALLMODE_MACHINEDATA | REINSTALLMODE_USERDATA | REINSTALLMODE_SHORTCUT);
            }
            if (mode == "fullreinstall") {
                if (!msiPath.empty() && std::filesystem::exists(msiPath)) {
                    return ::MsiInstallProductW(msiPath.c_str(), L"REINSTALL=ALL REINSTALLMODE=vomus");
                }
                if (!prodCode.empty()) {
                    return ::MsiReinstallProductW(
                        prodCode.c_str(),
                        REINSTALLMODE_FILEREPLACE | REINSTALLMODE_SHORTCUT | REINSTALLMODE_MACHINEDATA | REINSTALLMODE_USERDATA);
                }
            }
            return ::MsiInstallProductW(msiPath.c_str(), props.c_str());
        });

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
    std::wstring  upgradeCode;
    std::string   requestedExecLevel = "asInvoker";
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
    cfg.upgradeCode     = getW("UpgradeCode", L"");
    if (!kv["RequestedExecutionLevel"].empty()) {
        cfg.requestedExecLevel = std::string(rsw::NormalizeRequestedExecutionLevel(kv["RequestedExecutionLevel"]));
    }
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
    std::wstring            instanceKey;
    UINT                    wmActivateInstance = 0;
    TaskbarProgressHelper   taskbarProgress;

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
        taskbarProgress.SetIndeterminate(hWnd);
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
        if (self->taskbarProgress.HandleMessage(hWnd, msg)) {
            return 0;
        }
        if (self->wmActivateInstance != 0 && msg == self->wmActivateInstance) {
            if (::IsIconic(hWnd)) {
                ::ShowWindow(hWnd, SW_RESTORE);
            }
            ::SetForegroundWindow(hWnd);
            ::BringWindowToTop(hWnd);
            return 0;
        }

        switch (msg) {
            case WM_CREATE: {
                self->taskbarProgress.AttachWindow(hWnd);
                self->wmActivateInstance = rsw::RegisterSingleInstanceWindowTag(hWnd, self->instanceKey);
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
                self->taskbarProgress.SetNormalPermille(hWnd, static_cast<int>(wParam));
                ::InvalidateRect(hWnd, nullptr, FALSE);
                return 0;
            }
            case WM_UNIV_FINISHED: {
                if (wParam == 1) {
                    if (self->hProgBar) ::SendMessageW(self->hProgBar, PBM_SETPOS, 1000, 0);
                    self->taskbarProgress.SetNoProgress(hWnd);
                    self->downloadSucceeded = true;
                    ::DestroyWindow(hWnd);
                    return 0;
                }
                if (self->cancelFlag.load()) {
                    self->taskbarProgress.SetNoProgress(hWnd);
                    self->downloadSucceeded = false;
                    ::DestroyWindow(hWnd);
                    return 0;
                }
                self->taskbarProgress.SetError(hWnd);
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
                rsw::UnregisterSingleInstanceWindowTag(hWnd, self->instanceKey);
                self->taskbarProgress.Release();
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
    bool utilityMode  = false;

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
        if (a == L"--extract-msi" || a == L"/extract" || a == L"--export-screenshots") {
            headlessMode = true;
            utilityMode  = true;
        } else if (a == L"/s" || a == L"/S" || a == L"/qn" || a == L"/quiet" || a == L"--silent") {
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

    // UAC-Disabled (`EnableLUA = 0`) Admin Rights Gate when `RequestedExecutionLevel = requireAdministrator`!
    if (!utilityMode && ucfg.requestedExecLevel == "requireAdministrator") {
        const auto priv = rsw::QueryProcessPrivilegeState();
        if (!priv.HasEffectiveAdminRights()) {
            if (!headlessMode) {
                const std::wstring title = ucfg.productName + L" Setup";
                const std::wstring msg =
                    ucfg.productName + L" Setup requires Administrator privileges to run, and "
                    L"User Account Control (UAC) elevation is disabled on this computer.\n\n"
                    L"Please sign in with an Administrator account and run Setup again.";
                ::TaskDialog(nullptr, hInstance, title.c_str(), L"Administrator Privileges Required",
                             msg.c_str(), TDCBF_CLOSE_BUTTON, TD_ERROR_ICON, nullptr);
            }
            return ERROR_ACCESS_DENIED;
        }
    }

    // Per-Product Single-Instance Guard (`SetupStub.<Key>` + `SetupUI.<Key>`):
    //   If either this Universal Stub OR the already-spawned native `x64`/`ARM64` `SetupWizard` UI
    //   for this `UpgradeCode` is already running, restore its window instead of opening a duplicate!
    const std::wstring instKey = rsw::DeriveProductInstanceKey(ucfg.upgradeCode, ucfg.manufacturer, ucfg.productName);
    HANDLE hStubMutex = nullptr;
    if (!utilityMode) {
        if (rsw::DoesLocalMutexExist(L"Local\\RaggieSoft.SetupUI." + instKey)) {
            if (headlessMode) return ERROR_INSTALL_ALREADY_RUNNING;
            rsw::ActivateExistingProductSetupWindow(instKey);
            return 0;
        }
        bool stubAlreadyRunning = false;
        hStubMutex = rsw::CreatePermissiveLocalMutex(L"Local\\RaggieSoft.SetupStub." + instKey, stubAlreadyRunning);
        if (stubAlreadyRunning) {
            if (hStubMutex) ::CloseHandle(hStubMutex);
            if (headlessMode) return ERROR_INSTALL_ALREADY_RUNNING;
            rsw::ActivateExistingProductSetupWindow(instKey);
            return 0;
        }
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
                ui.instanceKey    = instKey;
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
                constexpr DWORD kStyle   = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
                constexpr DWORD kExStyle = WS_EX_APPWINDOW;
                ::AdjustWindowRectExForDpi(&wRc, kStyle, FALSE, kExStyle, dpi);
                const int wW = wRc.right - wRc.left;
                const int wH = wRc.bottom - wRc.top;
                const int wX = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - wW) / 2);
                const int wY = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - wH) / 2);

                const std::wstring wTitle = ucfg.productName + L" Setup";
                HWND hUnivWnd = ::CreateWindowExW(
                    kExStyle, kUnivWndClass, wTitle.c_str(), kStyle, wX, wY, wW, wH, nullptr, nullptr, hInstance, &ui);
                if (hUnivWnd) {
                    if (ui.hIconBig) {
                        ::SendMessageW(hUnivWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(ui.hIconBig));
                    }
                    HICON hSm = ui.hIconSmall ? ui.hIconSmall : ui.hIconBig;
                    if (hSm) {
                        ::SendMessageW(hUnivWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSm));
                    }
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

    // Release the outer `SetupStub` mutex right before spawning the native child installer so the
    // child cleanly acquires `SetupUI.<Key>` and inherits foreground activation rights!
    if (hStubMutex) {
        ::CloseHandle(hStubMutex);
        hStubMutex = nullptr;
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
    ::AllowSetForegroundWindow(pi.dwProcessId);
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

    // Fast automated self-check, `--elevated-msi-worker`, `--elevated-native-worker`, & `--native-uninstall` flags
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--elevated-msi-worker") {
            if (i + 1 >= args.size()) return ERROR_BAD_ARGUMENTS;
            return RunElevatedMsiWorkerFromJobFile(std::filesystem::path(args[i + 1]));
        }
        if (args[i] == L"--elevated-native-worker") {
            if (i + 1 >= args.size()) return ERROR_BAD_ARGUMENTS;
            return rsw::compact_suite::RunElevatedNativeWorkerJob(std::filesystem::path(args[i + 1]));
        }
        if (args[i] == L"--elevated-suite-worker") {
            if (i + 1 >= args.size()) return ERROR_BAD_ARGUMENTS;
            return rsw::compact_suite::RunElevatedSuiteWorkerJob(std::filesystem::path(args[i + 1]));
        }
        if (args[i] == L"--elevated-rssc-worker") {
            if (i + 2 >= args.size()) return ERROR_BAD_ARGUMENTS;
            const std::filesystem::path statePath(args[i + 1]);
            const std::filesystem::path resultPath(args[i + 2]);
            const std::uint8_t* bcPtr = nullptr;
            std::size_t bcSz = 0;
            std::vector<std::uint8_t> bcRaw;
            std::string bcErr;
            rsw::rssc::ScriptProgram prog;
            if (!GetEmbeddedRcDataView(rsw::kResIdScriptBytecode, bcPtr, bcSz) || bcSz == 0 ||
                !rsw::DecompressLzmsOrCopyRaw(bcPtr, bcSz, bcRaw, bcErr) ||
                !rsw::rssc::LoadRaggieScriptBytecode(bcRaw.data(), bcRaw.size(), prog, bcErr)) {
                return ERROR_INVALID_DATA;
            }
            rsw::rssc::VmExecutionOptions wOpts{};
            wOpts.isElevatedWorker = true;
            wchar_t selfExe[MAX_PATH * 4]{};
            ::GetModuleFileNameW(nullptr, selfExe, static_cast<DWORD>(std::size(selfExe)));
            wOpts.runningExePath = std::filesystem::path(selfExe);
            const std::uint8_t* pakPtr = nullptr;
            std::size_t pakSz = 0;
            if (GetEmbeddedRcDataView(rsw::kResIdSfxOrNativeArchive, pakPtr, pakSz) && pakSz > 0) {
                wOpts.embeddedPakBytes = pakPtr;
                wOpts.embeddedPakSize  = pakSz;
            }
            std::ifstream sf(statePath, std::ios::binary);
            std::ostringstream sss;
            sss << sf.rdbuf();
            ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            rsw::rssc::RaggieScriptVm vm(prog, wOpts);
            vm.ImportWizardStateJson(sss.str());
            const auto res = vm.ExecuteCommitPhaseOnly();
            {
                std::ofstream rf(resultPath, std::ios::binary | std::ios::trunc);
                rsw::rssc::Value outMap = rsw::rssc::Value::FromMap({
                    {"ok",           rsw::rssc::Value::FromBool(res.ok)},
                    {"exitCode",     rsw::rssc::Value::FromInt(res.exitCode)},
                    {"rebootNeeded", rsw::rssc::Value::FromBool(res.rebootNeeded)},
                    {"rolledBack",   rsw::rssc::Value::FromBool(res.rolledBack)},
                    {"errorMessage", rsw::rssc::Value::FromStr(res.errorMessage)}
                });
                std::ostringstream jss;
                rsw::rssc::json_detail::StringifyJsonRec(jss, outMap, 2, 0);
                const std::string jstr = jss.str();
                rf.write(jstr.data(), static_cast<std::streamsize>(jstr.size()));
            }
            ::CoUninitialize();
            return res.exitCode;
        }
        if (args[i] == L"--native-uninstall") {
            bool quietUninst = false;
            for (const auto& ua : args) {
                if (ua == L"/s" || ua == L"/S" || ua == L"/qn" || ua == L"/quiet" || ua == L"--silent") quietUninst = true;
            }
            wchar_t selfBuf[MAX_PATH * 4]{};
            ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
            const auto metaPath = std::filesystem::path(selfBuf).parent_path() / L"Uninstall.rswm";
            if (!quietUninst) {
                int chosen = IDNO;
                ::TaskDialog(nullptr, hInstance, L"Application Uninstall",
                             L"Remove Application?",
                             L"Do you want to cleanly uninstall this application from your computer? Your personal documents will not be deleted.",
                             TDCBF_YES_BUTTON | TDCBF_NO_BUTTON, TD_INFORMATION_ICON, &chosen);
                if (chosen != IDYES) return ERROR_INSTALL_USEREXIT;
            }
            std::string unErr;
            return rsw::ExecuteNativeUninstallFromMeta(metaPath, true, unErr) ? 0 : ERROR_INSTALL_FAILURE;
        }
        if (args[i] == L"--self-check") {
            const auto hp = rsw::QueryHostPlatform();
            return (hp.majorVersion >= 10) ? 0 : 1;
        }
    }

    // Unified Suite Chainer Mode (`PackageKind = suite` -> `kResIdSuiteManifest` `#230`!)
    {
        const std::uint8_t* suitePtr = nullptr;
        std::size_t suiteSz = 0;
        if (GetEmbeddedRcDataView(rsw::kResIdSuiteManifest, suitePtr, suiteSz) && suiteSz > 0) {
            return rsw::compact_suite::RunSuiteChainerEntrypoint(
                hInstance, nCmdShow, args,
                std::string_view(reinterpret_cast<const char*>(suitePtr), suiteSz));
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

    // Lightweight Self-Extractor (`PackageKind = sfx`) & Zero-MSI Native Direct Engine (`Engine = native` — `#221`)!
    {
        const std::uint8_t* natPtr = nullptr;
        std::size_t natSz = 0;
        if (GetEmbeddedRcDataView(rsw::kResIdNativeManifest, natPtr, natSz) && natSz > 0) {
            return rsw::compact_suite::RunNativeOrSfxEntrypoint(
                hInstance, nCmdShow, args,
                std::string_view(reinterpret_cast<const char*>(natPtr), natSz));
        }
    }

    // Standalone `RaggieScript` (`.rssc` -> `kResIdScriptBytecode` `#225`) Native Execution Mode!
    {
        const std::uint8_t* rsscPtr = nullptr;
        std::size_t rsscSz = 0;
        const std::uint8_t* msiCheckPtr = nullptr;
        std::size_t msiCheckSz = 0;
        if (GetEmbeddedRcDataView(rsw::kResIdScriptBytecode, rsscPtr, rsscSz) && rsscSz > 0 &&
            !GetEmbeddedRcDataView(rsw::kResIdEmbeddedMsi, msiCheckPtr, msiCheckSz)) {
            std::vector<std::uint8_t> bcRaw;
            std::string bcErr;
            rsw::rssc::ScriptProgram prog;
            if (!rsw::DecompressLzmsOrCopyRaw(rsscPtr, rsscSz, bcRaw, bcErr) ||
                !rsw::rssc::LoadRaggieScriptBytecode(bcRaw.data(), bcRaw.size(), prog, bcErr)) {
                return ERROR_INVALID_DATA;
            }

            rsw::rssc::VmExecutionOptions vmOpts{};
            vmOpts.enableDeferredUacBroker = true;
            wchar_t selfBuf[MAX_PATH * 4]{};
            ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
            vmOpts.runningExePath = std::filesystem::path(selfBuf);

            for (const auto& wa : args) {
                const std::string a = rsw::WideToUtf8(wa);
                if (a == "/s" || a == "/S" || a == "/qn" || a == "/quiet" || a == "--silent") {
                    vmOpts.silentMode = true;
                } else if (a == "/passive" || a == "/qb" || a == "--passive") {
                    vmOpts.passiveMode = true;
                } else if (a.rfind("/l=", 0) == 0 || a.rfind("--log=", 0) == 0) {
                    vmOpts.logFilePath = std::filesystem::path(rsw::Utf8ToWide(a.substr(a.find('=') + 1)));
                } else if (a == "--repair") {
                    vmOpts.maintenanceMode   = true;
                    vmOpts.maintenanceAction = "repair";
                } else if (a == "--uninstall") {
                    vmOpts.maintenanceMode   = true;
                    vmOpts.maintenanceAction = "uninstall";
                } else if (a.find('=') != std::string::npos && a.front() != '-' && a.front() != '/') {
                    const auto eq = a.find('=');
                    vmOpts.cliProperties[a.substr(0, eq)] = a.substr(eq + 1);
                } else if (a.find(':') != std::string::npos && a.find(':') >= 2 && a.front() != '-' && a.front() != '/') {
                    const auto col = a.find(':');
                    const std::string k = a.substr(0, col);
                    vmOpts.cliProperties[k] = a.substr(col + 1);
                    vmOpts.lockedCliProperties.insert(k);
                } else {
                    vmOpts.rawCliSwitches.push_back(a);
                }
            }

            // Enforce minimum OS build (`9007` canonical RaggieSoft OS-unsupported exit code!)
            if (!rsw::MeetsWindowsBuildFloor(prog.package.minWinBuild)) {
                if (!vmOpts.silentMode) {
                    const std::wstring tit = rsw::Utf8ToWide(prog.package.name + " Setup");
                    ::TaskDialog(nullptr, hInstance, tit.c_str(), L"Unsupported Windows Version",
                                 L"This setup package requires a newer release of Windows.",
                                 TDCBF_CLOSE_BUTTON, TD_WARNING_ICON, nullptr);
                }
                return 9007;
            }

            // Enforce `requireAdministrator` when UAC is disabled (`740` = `ERROR_ELEVATION_REQUIRED`)
            if (rsw::EqualsIgnoreCase(prog.package.uac, "requireAdministrator")) {
                const auto priv = rsw::QueryProcessPrivilegeState();
                if (!priv.HasEffectiveAdminRights()) {
                    if (!vmOpts.silentMode) {
                        const std::wstring tit = rsw::Utf8ToWide(prog.package.name + " Setup");
                        ::TaskDialog(nullptr, hInstance, tit.c_str(), L"Administrator Privileges Required",
                                     L"Administrator privileges are required to install this application.",
                                     TDCBF_CLOSE_BUTTON, TD_ERROR_ICON, nullptr);
                    }
                    return ERROR_ELEVATION_REQUIRED;
                }
            }

            // Extract any embedded `RSWPAK1` (`#220`) `__support__` staging files to `%TEMP%`!
            const std::uint8_t* pakPtr = nullptr;
            std::size_t pakSz = 0;
            std::filesystem::path tempSupportRoot;
            std::error_code ec;
            if (GetEmbeddedRcDataView(rsw::kResIdSfxOrNativeArchive, pakPtr, pakSz) && pakSz > 0) {
                vmOpts.embeddedPakBytes = pakPtr;
                vmOpts.embeddedPakSize  = pakSz;
                std::vector<rsw::RswPakFileMetadata> metas;
                std::vector<std::uint8_t> payload;
                std::string pErr;
                if (rsw::UnpackRswPakBytesInMemory(pakPtr, pakSz, metas, payload, pErr)) {
                    for (const auto& m : metas) {
                        if (m.featureId != "__support__") continue;
                        if (tempSupportRoot.empty()) {
                            tempSupportRoot = std::filesystem::temp_directory_path(ec) /
                                              (L"rsw-rssc-sup-" + std::to_wstring(::GetCurrentProcessId()));
                            vmOpts.supportDirPath = tempSupportRoot / L"_support";
                        }
                        const auto dst = (vmOpts.supportDirPath / rsw::Utf8ToWide(m.archiveRelPath)).lexically_normal();
                        std::filesystem::create_directories(dst.parent_path(), ec);
                        std::ofstream out(dst, std::ios::binary | std::ios::trunc);
                        if (m.payloadSize > 0) {
                            out.write(reinterpret_cast<const char*>(payload.data() + m.payloadOffset),
                                      static_cast<std::streamsize>(m.payloadSize));
                        }
                    }
                }
            }

            ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
            INITCOMMONCONTROLSEX iccRssc{sizeof(iccRssc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
            ::InitCommonControlsEx(&iccRssc);

            rsw::rssc::RaggieScriptVm vm(prog, vmOpts);
            const auto res = vm.ExecuteFullLifecycle();

            if (!res.ok && !vmOpts.silentMode) {
                const std::wstring tit = rsw::Utf8ToWide(prog.package.name + " Setup");
                const std::wstring msg = rsw::Utf8ToWide(res.errorMessage) +
                    (res.rolledBack ? L"\n\nAll partial system changes were rolled back cleanly." : L"");
                ::TaskDialog(nullptr, hInstance, tit.c_str(), L"Installation Could Not Complete",
                             msg.c_str(), TDCBF_CLOSE_BUTTON, TD_ERROR_ICON, nullptr);
            }
            if (!tempSupportRoot.empty()) {
                std::filesystem::remove_all(tempSupportRoot, ec);
            }
            ::CoUninitialize();
            return res.exitCode;
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

    // Silent CLI install mode (`/s`, `/S`, `/qn`, `--silent`, `/quiet`), `/passive` (`/qb`), & `--export-screenshots`
    bool silentInstall  = false;
    bool cliPassiveMode = false;
    std::string cliStyleOverride;
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
        } else if (a == L"/passive" || a == L"/qb" || a == L"/qb-" || a == L"--passive") {
            cliPassiveMode = true;
        } else if (a.rfind(L"--wizard-style=", 0) == 0) {
            cliStyleOverride = rsw::WideToUtf8(a.substr(15));
        } else if (a.find(L'=') != std::wstring::npos && a.front() != L'-' && a.front() != L'/') {
            if (!silentExtraProps.empty()) silentExtraProps += L" ";
            silentExtraProps += a;
        }
    }

    if (silentInstall) {
        std::error_code ec;
        const auto silentCfg     = LoadBrandingConfigFromResource();
        const auto silentPrereqs = LoadEmbeddedPrerequisitesFromResource();
        const auto privState     = rsw::QueryProcessPrivilegeState();

        bool anyReqElev = false;
        const std::string arch = rsw::WideToUtf8(silentCfg.architecture);
        for (const auto& pr : silentPrereqs) {
            if (!rsw::IsPrerequisiteSatisfied(pr, arch) && pr.requiresElevation) {
                anyReqElev = true;
            }
        }
        if ((silentCfg.requestedExecLevel == "requireAdministrator" || silentCfg.perMachine || anyReqElev) &&
            !privState.HasEffectiveAdminRights()) {
            return ERROR_ACCESS_DENIED;
        }

        const std::wstring silentInstKey = rsw::DeriveProductInstanceKey(
            silentCfg.upgradeCode, silentCfg.manufacturer, silentCfg.productName);
        if (rsw::DoesLocalMutexExist(L"Local\\RaggieSoft.SetupStub." + silentInstKey)) {
            return ERROR_INSTALL_ALREADY_RUNNING;
        }
        bool sameProdRunning = false;
        HANDLE hSilentMutex = rsw::CreatePermissiveLocalMutex(
            L"Local\\RaggieSoft.SetupUI." + silentInstKey, sameProdRunning);
        if (sameProdRunning) {
            if (hSilentMutex) ::CloseHandle(hSilentMutex);
            return ERROR_INSTALL_ALREADY_RUNNING;
        }

        const auto tmpDir = std::filesystem::temp_directory_path(ec) /
                            (L"rsw-silent-" + std::to_wstring(::GetCurrentProcessId()));
        std::filesystem::create_directories(tmpDir, ec);

        wchar_t selfExeBuf[MAX_PATH * 4]{};
        ::GetModuleFileNameW(nullptr, selfExeBuf, static_cast<DWORD>(std::size(selfExeBuf)));
        const std::filesystem::path selfDir = std::filesystem::path(selfExeBuf).parent_path();

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
                if (hSilentMutex) ::CloseHandle(hSilentMutex);
                return 9010;
            }
            std::uint32_t rawCode = 0;
            std::string runErr;
            const auto outc = rsw::ExecuteStagedPrerequisiteInstaller(pr, prBin, rawCode, runErr);
            if (outc == rsw::PrereqExitOutcome::SucceededRebootRequested) anyReboot = true;
            else if (outc == rsw::PrereqExitOutcome::Failed && !pr.optional) {
                std::filesystem::remove_all(tmpDir, ec);
                if (hSilentMutex) ::CloseHandle(hSilentMutex);
                return static_cast<int>(rawCode ? rawCode : ERROR_INSTALL_FAILURE);
            }
        }

        const auto tmpMsi = tmpDir / L"Product.msi";
        std::wstring err;
        if (!ExtractEmbeddedMsiToFile(tmpMsi, err)) {
            std::filesystem::remove_all(tmpDir, ec);
            if (hSilentMutex) ::CloseHandle(hSilentMutex);
            return 1619;
        }
        ::MsiSetInternalUI(INSTALLUILEVEL_NONE, nullptr);
        const UINT rc = ExecuteMsiCallWithPoliteQueue(
            silentCfg.productName,
            []() { return false; },
            [](const std::wstring&) {},
            []() {},
            [&]() { return ::MsiInstallProductW(tmpMsi.wstring().c_str(), silentExtraProps.c_str()); });
        std::filesystem::remove_all(tmpDir, ec);
        if (hSilentMutex) ::CloseHandle(hSilentMutex);
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
    app.cfg          = LoadBrandingConfigFromResource();
    app.prereqSpecs  = LoadEmbeddedPrerequisitesFromResource();
    app.featurePacks = LoadEmbeddedFeaturePacksFromResource();
    app.instanceKey  = rsw::DeriveProductInstanceKey(app.cfg.upgradeCode, app.cfg.manufacturer, app.cfg.productName);
    if (!cliStyleOverride.empty()) {
        app.cfg.wizardStyle   = rsw::ParseWizardStyleKind(cliStyleOverride);
        app.cfg.isAutorun16x9 = (app.cfg.wizardStyle == rsw::WizardStyleKind::Autorun16x9);
        app.cfg.isAeroWizard  = (app.cfg.wizardStyle == rsw::WizardStyleKind::AeroWizard);
    }
    if (cliPassiveMode) {
        app.cfg.wizardStyle   = rsw::WizardStyleKind::PassiveToast;
        app.cfg.isAutorun16x9 = false;
        app.cfg.isAeroWizard  = false;
    }

    // 3. True Windows 11 Build Gate (`RtlGetVersion` pierces compatibility shims!)
    const auto hostOs = rsw::QueryHostPlatform();
    if (!rsw::MeetsWindowsBuildFloor(app.cfg.minWindowsBuild)) {
        const std::wstring title = app.cfg.productName + L" Setup";
        const std::wstring detail = app.cfg.osLaunchMessage + L"\n\nDetected OS Build: " + std::to_wstring(hostOs.buildNumber);
        ::TaskDialog(nullptr, hInstance, title.c_str(), L"Unsupported Windows Version", detail.c_str(), TDCBF_CLOSE_BUTTON, TD_WARNING_ICON, nullptr);
        ::CoUninitialize();
        return 1603;
    }

    // 3B. UAC-Disabled (`EnableLUA = 0`) Admin Rights Check when `RequestedExecutionLevel = requireAdministrator`!
    if (exportShotDir.empty() && app.cfg.requestedExecLevel == "requireAdministrator") {
        const auto priv = rsw::QueryProcessPrivilegeState();
        if (!priv.HasEffectiveAdminRights()) {
            const std::wstring title = app.cfg.productName + L" Setup";
            const std::wstring msg =
                app.cfg.productName + L" Setup requires Administrator privileges to run, and "
                L"User Account Control (UAC) elevation is disabled on this computer.\n\n"
                L"Please sign in with an Administrator account and run Setup again.";
            if (!cliPassiveMode) {
                ::TaskDialog(nullptr, hInstance, title.c_str(), L"Administrator Privileges Required",
                             msg.c_str(), TDCBF_CLOSE_BUTTON, TD_ERROR_ICON, nullptr);
            }
            ::CoUninitialize();
            return ERROR_ACCESS_DENIED;
        }
    }

    // 3C. Per-Product Single-Instance Guard (`Local\RaggieSoft.SetupUI.<UpgradeCode>`):
    //     If another instance of THIS product's installer is already open, bring it back to the
    //     foreground instead of launching a duplicate window (while allowing installers for
    //     different products to open freely side-by-side)!
    HANDLE hSingleInstanceMutex = nullptr;
    if (exportShotDir.empty()) {
        if (rsw::DoesLocalMutexExist(L"Local\\RaggieSoft.SetupStub." + app.instanceKey)) {
            rsw::ActivateExistingProductSetupWindow(app.instanceKey);
            ::CoUninitialize();
            return 0;
        }
        bool alreadyOpen = false;
        hSingleInstanceMutex = rsw::CreatePermissiveLocalMutex(
            L"Local\\RaggieSoft.SetupUI." + app.instanceKey, alreadyOpen);
        if (alreadyOpen) {
            if (hSingleInstanceMutex) ::CloseHandle(hSingleInstanceMutex);
            rsw::ActivateExistingProductSetupWindow(app.instanceKey);
            ::CoUninitialize();
            return 0;
        }
    }

    app.installDir     = ComputeDefaultInstallDir(app.cfg);
    app.startMenuGroup = app.cfg.startMenuFolder;
    app.DetectExistingInstallation();
    app.theme.Refresh(app.cfg.accentPrimary, app.cfg.accentSecondary);
    rsw::ApplyWizardStyleThemeOverrides(app.cfg.wizardStyle, app.theme);

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

    // 4. Register optional Full-Monitor Retro Backdrop (`#000080` Cobalt & `#008080` ACME Teal) + Main Setup Dialog!
    constexpr const wchar_t* kBackClass = L"RaggieSoftRetroSetupBackdropWindow";
    WNDCLASSEXW bwc{};
    bwc.cbSize        = sizeof(bwc);
    bwc.lpfnWndProc   = &SetupWizardApp::BackdropWndProc;
    bwc.hInstance     = hInstance;
    bwc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    bwc.lpszClassName = kBackClass;
    ::RegisterClassExW(&bwc);

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

    if (rsw::UsesFullScreenRetroBackdrop(app.cfg.wizardStyle) && exportShotDir.empty()) {
        POINT ptCursor{0, 0};
        ::GetCursorPos(&ptCursor);
        HMONITOR hMon = ::MonitorFromPoint(ptCursor, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO mi{sizeof(mi)};
        RECT monRc{0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
        if (hMon && ::GetMonitorInfoW(hMon, &mi)) {
            monRc = mi.rcMonitor;
        }
        // Full-screen Cobalt-Blue (`#000080`) / ACME Teal (`#008080`) wash has ZERO titlebar,
        // ZERO taskbar button (`WS_EX_TOOLWINDOW`), ZERO rounded corners, and ZERO DWM border!
        app.hBackdropWnd = ::CreateWindowExW(
            WS_EX_TOOLWINDOW, kBackClass, L"", WS_POPUP | WS_CLIPCHILDREN,
            monRc.left, monRc.top, monRc.right - monRc.left, monRc.bottom - monRc.top,
            nullptr, nullptr, hInstance, &app);
        if (app.hBackdropWnd) {
            const int doNotRound = 1; // `DWMWCP_DONOTROUND`
            const COLORREF noBorder = 0xFFFFFFFEu; // `DWMWA_COLOR_NONE`
            ::DwmSetWindowAttribute(app.hBackdropWnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &doNotRound, sizeof(doNotRound));
            ::DwmSetWindowAttribute(app.hBackdropWnd, 34 /* DWMWA_BORDER_COLOR */, &noBorder, sizeof(noBorder));
            ::ShowWindow(app.hBackdropWnd, SW_SHOWNOACTIVATE);
            ::UpdateWindow(app.hBackdropWnd);
        }
    }

    app.isHeadlessExport = !exportShotDir.empty();
    const UINT screenDpi = ::GetDpiForSystem();
    int logW = 620, logH = 490;
    app.ComputeLogicalDialogSize(logW, logH);
    RECT winRc{0, 0,
               rsw::ui::ScalePx(logW, screenDpi, app.theme.textScalePercent),
               rsw::ui::ScalePx(logH, screenDpi, app.theme.textScalePercent)};
    constexpr DWORD kWinStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    const DWORD winExStyle    = app.isHeadlessExport ? 0u : WS_EX_APPWINDOW;
    ::AdjustWindowRectExForDpi(&winRc, kWinStyle, FALSE, winExStyle, screenDpi);
    const int totalW = winRc.right - winRc.left;
    const int totalH = winRc.bottom - winRc.top;
    const int posX   = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - totalW) / 2);
    const int posY   = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - totalH) / 2);

    const std::wstring winTitle = app.cfg.productName + L" Setup";
    HWND hWnd = ::CreateWindowExW(
        winExStyle /* `WS_EX_APPWINDOW` guarantees a taskbar icon even when owned by `hBackdropWnd`! */,
        kWndClass, winTitle.c_str(), kWinStyle,
        posX, posY, totalW, totalH,
        app.hBackdropWnd /* Owned by full-screen backdrop so Z-order never hides dialog! */,
        nullptr, hInstance, &app);

    if (!hWnd) {
        if (app.hBackdropWnd) ::DestroyWindow(app.hBackdropWnd);
        if (hSingleInstanceMutex) ::CloseHandle(hSingleInstanceMutex);
        ::CoUninitialize();
        return 1;
    }

    if (app.hAppIconBig) {
        ::SendMessageW(hWnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(app.hAppIconBig));
    }
    if (HICON hSm = app.hAppIconSmall ? app.hAppIconSmall : app.hAppIconBig) {
        ::SendMessageW(hWnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hSm));
    }

    ::ShowWindow(hWnd, nCmdShow);
    ::UpdateWindow(hWnd);
    if (app.IsPassiveToastMode() && exportShotDir.empty()) {
        ::SetTimer(hWnd, SetupWizardApp::kPassiveAutostartTimerId, 120, nullptr);
    }

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

        // --- NEW IN v0.5.0: Export Fluent Mica Step-Rail, Classic Wizard95 3D, FullScreen Cobalt Blue, & ACME #008080 Teal! ---
        auto switchExportStyle = [&](rsw::WizardStyleKind style, int forcedTheme, const char* presetTok, WizardPage pg) {
            rsw::ui::ForcedThemeOverride() = forcedTheme;
            app.cfg.wizardStyle     = style;
            app.cfg.isAutorun16x9   = (style == rsw::WizardStyleKind::Autorun16x9);
            app.cfg.isAeroWizard    = (style == rsw::WizardStyleKind::AeroWizard);
            app.cfg.setupTypePreset = presetTok;
            app.cfg.setupTypes      = rsw::BuildEffectiveSetupTypeOptions(
                app.cfg.setupTypePreset, {}, app.cfg.expressDesc, app.cfg.compactDesc, app.cfg.customDesc);
            app.PopulateSetupTypeCardsFromConfig();
            app.currentPage = pg;
            app.RefreshEffectiveThemeAndChrome();
            app.ResizeDialogForActiveStyle();
        };

        switchExportStyle(rsw::WizardStyleKind::FluentMica, 1 /* Dark */, "express_custom", WizardPage::SetupType);
        const bool okMica = rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-fluent-mica.png");

        switchExportStyle(rsw::WizardStyleKind::Wizard95, 0 /* Classic #C0C0C0 */, "win95_classic", WizardPage::Welcome);
        const bool okWiz95 = rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"setupwizard-wizard95.png");

        switchExportStyle(rsw::WizardStyleKind::FullScreenBlue, 0, "win95_classic", WizardPage::SetupType);
        const bool okBlue = app.CaptureRetroFullScreenCompositePng(exportShotDir / L"setupwizard-fullscreen-blue.png");

        switchExportStyle(rsw::WizardStyleKind::FullScreenTeal, 0, "win95_classic", WizardPage::SetupType);
        const bool okTeal = app.CaptureRetroFullScreenCompositePng(exportShotDir / L"setupwizard-fullscreen-teal.png");

        ::DestroyWindow(hWnd);
        app.taskbarProgress.Release();
        ::CoUninitialize();
        return (okType && okTree && okUacDark && okUacLight && okLight && okMica && okWiz95 && okBlue && okTeal) ? 0 : 2;
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

    app.taskbarProgress.Release();
    if (hSingleInstanceMutex) ::CloseHandle(hSingleInstanceMutex);
    ::CoUninitialize();
    return 0;
}
