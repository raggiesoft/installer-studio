// ================================================================================
// FILE: src/bootstrapper/RswSetupMain.cpp
// PROJECT: RaggieSoft Setup Wizard ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `rsswsetup.exe`?
//     When `rsswbuild.exe` compiles a project with `BuildBootstrapper = true`, it
//     copies this zero-dependency static `/MT` Win32 C++20 binary (`x64` or `ARM64`)
//     and embeds the compiled `Product.msi`, branding colors, product icon, license
//     agreement, and optional PNG cover/banner art into Win32 `RT_RCDATA` resources.
//
//   How does `rsswsetup.exe` install an `.msi` with our custom RaggieSoft UI instead
//   of showing old Windows 2000 gray dialogs?
//     Windows Installer (`msi.dll`) includes an official **External UI Handler API**:
//       1. `MsiSetInternalUI(INSTALLUILEVEL_NONE, nullptr)` suppresses MSI's internal
//          gray dialogs while keeping Windows Installer's transactional rollback and
//          built-in UAC elevation broker 100% active!
//       2. `MsiSetExternalUIRecord(&OnMsiExternalRecord, ...)` subscribes our C++
//          window to real-time progress ticks (`INSTALLMESSAGE_PROGRESS`), current
//          step descriptions (`INSTALLMESSAGE_ACTIONSTART`), and error messages!
//       3. `MsiInstallProductW(tempMsiPath, L"INSTALLDIR=\"...\"")` runs on a worker
//          `std::thread` while posting thread-safe `PostMessageW` updates back to our
//          60fps Per-Monitor V2 DPI, Dark Mode Mica, Contrast-Theme-safe UI!
//
//   What RaggieSoft visual layouts are built right into this single binary?
//     - **`WizardLayout = standard`**: Full branded RaggieSoft Glass Setup Wizard
//       with Midnight Navy + Cobalt/Gold gradient header, Express / Compact / Custom
//       setup mode radio cards, automatic Maintenance Mode (Quick Repair, Full
//       Reinstall, or Remove when already installed), RichEdit RTF license screen,
//       modern `IFileOpenDialog` folder browser, and safe non-elevated app launch!
//     - **`WizardLayout = autorun16x9`**: Crisp `640x480` widescreen `16:9` OpenGraph
//       CD-Autorun stage built for RaggieSoft Book & Album Micro-Installers!
// ================================================================================

#include "../common/RswWin32UiTheme.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswVersion.hpp"

#include <msi.h>
#include <msiquery.h>
#include <richedit.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "msi.lib")
#pragma comment(lib, "shell32.lib")

namespace {

// Custom `PostMessageW` notifications sent from the MSI worker thread -> UI thread
constexpr UINT WM_RSW_PROGRESS_UPDATE = WM_APP + 10;
constexpr UINT WM_RSW_STATUS_TEXT     = WM_APP + 11;
constexpr UINT WM_RSW_INSTALL_DONE    = WM_APP + 12;

// Accessible Win32 Child Control IDs (enabling `Tab` traversal & `Alt+Key` mnemonics)
enum ControlId : int {
    IDC_MODE_EXPRESS   = 1001,
    IDC_MODE_COMPACT   = 1002,
    IDC_MODE_CUSTOM    = 1003,
    IDC_MODE_DESC      = 1004,

    IDC_MAINT_REPAIR   = 1010,
    IDC_MAINT_REINSTALL= 1011,
    IDC_MAINT_REMOVE   = 1012,

    IDC_LIC_RICHEDIT   = 1020,
    IDC_LIC_ACCEPT_CHK = 1021,

    IDC_DIR_LABEL      = 1030,
    IDC_DIR_EDIT       = 1031,
    IDC_DIR_BROWSE_BTN = 1032,

    IDC_PROG_STATUS    = 1040,
    IDC_PROG_BAR       = 1041,

    IDC_FIN_SUMMARY    = 1050,
    IDC_FIN_LAUNCH_CHK = 1051,
    IDC_FIN_VIEWLOG_BTN= 1052,

    IDC_NAV_BACK       = 1090,
    IDC_NAV_PRIMARY    = 1091,
    IDC_NAV_CANCEL     = 1092,
    IDC_AUTORUN_FOLDER = 1093
};

enum class WizardPage {
    WelcomeMode,
    Maintenance,
    License,
    CustomFolder,
    Installing,
    Finished
};

enum class SelectedAction {
    InstallExpress,
    InstallCompact,
    InstallCustom,
    MaintQuickRepair,
    MaintFullReinstall,
    MaintUninstall
};

struct BrandingConfig {
    std::wstring productName        = L"Application";
    std::wstring tagline            = L"Setup Wizard";
    std::wstring websiteLabel       = L"";
    std::wstring version            = L"1.0.0";
    std::wstring manufacturer       = L"Publisher";
    std::wstring productCode;
    std::wstring upgradeCode;
    std::wstring defaultSubdir      = L"Application";
    std::wstring mainExecutable;
    bool         perMachine         = true;
    int          minWindowsBuild    = 22000;
    std::wstring osLaunchMessage    = L"This application requires a newer version of Windows.";
    bool         isAutorun16x9      = false;
    COLORREF     accentPrimary      = RGB(37, 99, 235);
    COLORREF     accentSecondary    = RGB(251, 191, 36);
    std::wstring welcomeHeadline    = L"Welcome to Setup";
    std::wstring bannerTitle        = L"Choose your preferred setup option below.";
    std::wstring installBtnText     = L"&Install";
    std::wstring finishLaunchText   = L"&Open application when setup finishes";
    bool         offerLaunch        = true;
    bool         hasLicenseRtf      = false;
    std::wstring expressDesc        = L"One-click setup into the default installation folder with recommended shortcuts.";
    std::wstring compactDesc        = L"Installs essential application files with minimal disk space.";
    std::wstring customDesc         = L"Choose your own installation folder before installing.";
    std::wstring authorOrArtist;
};

/// Loads a raw byte view `(ptr, size)` from an embedded `RT_RCDATA` resource inside our own `.exe`.
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

/// Parses `kResIdBrandingIni` (`[Branding]\nKey=Value\n...`) into `BrandingConfig`.
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
        return (it != kv.end() && !it->second.empty()) ? rsw::Utf8ToWide(it->second) : defVal;
    };

    cfg.productName      = getW("ProductName", cfg.productName);
    cfg.tagline          = getW("Tagline", cfg.tagline);
    cfg.websiteLabel     = getW("WebsiteLabel", cfg.websiteLabel);
    cfg.version          = getW("Version", cfg.version);
    cfg.manufacturer     = getW("Manufacturer", cfg.manufacturer);
    cfg.productCode      = getW("ProductCode", L"");
    cfg.upgradeCode      = getW("UpgradeCode", L"");
    cfg.defaultSubdir    = getW("DefaultSubdir", cfg.defaultSubdir);
    cfg.mainExecutable   = getW("MainExecutable", L"");
    cfg.perMachine       = rsw::EqualsIgnoreCase(kv["PerMachine"], "true");
    if (!kv["MinWindowsBuild"].empty()) {
        cfg.minWindowsBuild = std::atoi(kv["MinWindowsBuild"].c_str());
    }
    cfg.osLaunchMessage  = getW("OsLaunchMessage", cfg.osLaunchMessage);
    cfg.isAutorun16x9    = rsw::EqualsIgnoreCase(kv["WizardLayout"], "autorun16x9");
    cfg.accentPrimary    = rsw::ui::ParseHexColor(kv["AccentPrimaryHex"], cfg.accentPrimary);
    cfg.accentSecondary  = rsw::ui::ParseHexColor(kv["AccentSecondaryHex"], cfg.accentSecondary);
    cfg.welcomeHeadline  = getW("WelcomeHeadline", L"Welcome to " + cfg.productName + L" Setup");
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

/// Computes the default full Windows path (`C:\Program Files\RaggieSoft\...` or `%LOCALAPPDATA%\...`).
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

/// Writes the embedded `Product.msi` resource (`#201`) to a target file on disk.
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

/// Main state container for the interactive `rsswsetup.exe` window!
class SetupWizardApp {
public:
    BrandingConfig          cfg{};
    rsw::ui::RswTheme       theme{};
    HWND                    hWnd           = nullptr;
    HMODULE                 hRichEditDll   = nullptr;
    HICON                   hAppIconBig    = nullptr;
    HICON                   hAppIconSmall  = nullptr;
    HBITMAP                 hArtworkBmp    = nullptr;
    int                     artworkW       = 0;
    int                     artworkH       = 0;

    HFONT                   hFontTitle     = nullptr;
    HFONT                   hFontSection   = nullptr;
    HFONT                   hFontBody      = nullptr;
    HFONT                   hFontBold      = nullptr;
    HFONT                   hFontSmall     = nullptr;

    WizardPage              currentPage    = WizardPage::WelcomeMode;
    SelectedAction          selectedAction = SelectedAction::InstallExpress;
    bool                    alreadyInstalled = false;
    std::wstring            installedProductCode;
    std::wstring            installDir;
    std::filesystem::path   tempWorkDir;
    std::filesystem::path   tempMsiPath;
    std::filesystem::path   msiLogPath;

    // Child controls
    HWND hModeExpress = nullptr, hModeCompact = nullptr, hModeCustom = nullptr, hModeDesc = nullptr;
    HWND hMaintRepair = nullptr, hMaintReinstall = nullptr, hMaintRemove = nullptr;
    HWND hLicRich = nullptr, hLicAccept = nullptr;
    HWND hDirLabel = nullptr, hDirEdit = nullptr, hDirBrowse = nullptr;
    HWND hProgStatus = nullptr, hProgBar = nullptr;
    HWND hFinSummary = nullptr, hFinLaunch = nullptr, hFinViewLog = nullptr;
    HWND hBtnBack = nullptr, hBtnPrimary = nullptr, hBtnCancel = nullptr, hBtnAutorunFolder = nullptr;

    // Thread-safe MSI external UI progress state
    std::thread             workerThread;
    std::atomic<bool>       cancelRequested{false};
    std::atomic<bool>       installRunning{false};
    UINT                    finalMsiResult = ERROR_SUCCESS;
    std::mutex              msgMutex;
    std::wstring            latestStatusText = L"Preparing setup...";
    std::wstring            capturedErrorText;

    // MSI 4-part progress state machine (`INSTALLMESSAGE_PROGRESS`)
    long long               msiTotalTicks   = 1000;
    long long               msiCurTicks     = 0;
    long long               msiStepTicks    = 1;
    bool                    msiForward      = true;

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
        for (HFONT* pf : {&hFontTitle, &hFontSection, &hFontBody, &hFontBold, &hFontSmall}) {
            if (*pf) { ::DeleteObject(*pf); *pf = nullptr; }
        }
    }

    void RebuildFonts(UINT dpi) noexcept {
        DestroyFonts();
        const int ts = theme.textScalePercent;
        hFontTitle   = rsw::ui::CreateScaledFont(L"Segoe UI", 15, FW_BOLD,     dpi, ts);
        hFontSection = rsw::ui::CreateScaledFont(L"Segoe UI", 12, FW_SEMIBOLD, dpi, ts);
        hFontBody    = rsw::ui::CreateScaledFont(L"Segoe UI", 10, FW_NORMAL,   dpi, ts);
        hFontBold    = rsw::ui::CreateScaledFont(L"Segoe UI", 10, FW_SEMIBOLD, dpi, ts);
        hFontSmall   = rsw::ui::CreateScaledFont(L"Segoe UI",  9, FW_NORMAL,   dpi, ts);
    }

    void DetectExistingInstallation() {
        if (cfg.upgradeCode.empty()) return;
        wchar_t prodBuf[39]{};
        if (::MsiEnumRelatedProductsW(cfg.upgradeCode.c_str(), 0, 0, prodBuf) == ERROR_SUCCESS) {
            alreadyInstalled     = true;
            installedProductCode = prodBuf;
            currentPage          = WizardPage::Maintenance;
            selectedAction       = SelectedAction::MaintQuickRepair;
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

        // Page 1: Express / Compact / Custom radio group
        hModeExpress = mkBtn(L"&Express Setup (Recommended)", IDC_MODE_EXPRESS, BS_AUTORADIOBUTTON | WS_GROUP);
        hModeCompact = mkBtn(L"Co&mpact Setup",               IDC_MODE_COMPACT, BS_AUTORADIOBUTTON);
        hModeCustom  = mkBtn(L"C&ustom Setup",                IDC_MODE_CUSTOM,  BS_AUTORADIOBUTTON);
        hModeDesc    = mkStatic(cfg.expressDesc.c_str(), IDC_MODE_DESC);
        ::SendMessageW(hModeExpress, BM_SETCHECK, BST_CHECKED, 0);

        // Maintenance Page: Quick Repair / Full Reinstall / Remove
        hMaintRepair    = mkBtn(L"&Quick Repair — Restore missing or modified program files", IDC_MAINT_REPAIR,    BS_AUTORADIOBUTTON | WS_GROUP);
        hMaintReinstall = mkBtn(L"Fu&ll Reinstall — Refresh all application files and shortcuts", IDC_MAINT_REINSTALL, BS_AUTORADIOBUTTON);
        hMaintRemove    = mkBtn(L"&Remove — Uninstall this application from your computer",   IDC_MAINT_REMOVE,    BS_AUTORADIOBUTTON);
        ::SendMessageW(hMaintRepair, BM_SETCHECK, BST_CHECKED, 0);

        // License Agreement Page
        hLicRich = ::CreateWindowExW(
            WS_EX_CLIENTEDGE,
            hRichEditDll ? MSFTEDIT_CLASS : L"EDIT",
            L"",
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_LIC_RICHEDIT)), hInst, nullptr);
        LoadEmbeddedRtfIntoControl();
        hLicAccept = mkBtn(L"I &accept the terms in the License Agreement", IDC_LIC_ACCEPT_CHK, BS_AUTOCHECKBOX);

        // Custom Destination Folder Page
        hDirLabel  = mkStatic(L"Choose where you would like Setup to install the application:", IDC_DIR_LABEL);
        hDirEdit   = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", installDir.c_str(),
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DIR_EDIT)), hInst, nullptr);
        hDirBrowse = mkBtn(L"&Browse...", IDC_DIR_BROWSE_BTN, BS_PUSHBUTTON);

        // Progress Page
        hProgStatus = mkStatic(L"Preparing installation...", IDC_PROG_STATUS);
        hProgBar    = ::CreateWindowExW(
            0, PROGRESS_CLASSW, nullptr, WS_CHILD | PBS_SMOOTH,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PROG_BAR)), hInst, nullptr);
        ::SendMessageW(hProgBar, PBM_SETRANGE32, 0, 1000);

        // Finished Page
        hFinSummary = mkStatic(L"", IDC_FIN_SUMMARY);
        hFinLaunch  = mkBtn(cfg.finishLaunchText.c_str(), IDC_FIN_LAUNCH_CHK, BS_AUTOCHECKBOX);
        ::SendMessageW(hFinLaunch, BM_SETCHECK, (cfg.offerLaunch && !cfg.mainExecutable.empty()) ? BST_CHECKED : BST_UNCHECKED, 0);
        hFinViewLog = mkBtn(L"&View Setup Log", IDC_FIN_VIEWLOG_BTN, BS_PUSHBUTTON);

        // Bottom Navigation Bar
        hBtnAutorunFolder = mkBtn(L"C&hange Folder...", IDC_AUTORUN_FOLDER, BS_PUSHBUTTON);
        hBtnBack          = mkBtn(L"< &Back",           IDC_NAV_BACK,       BS_PUSHBUTTON);
        hBtnPrimary       = mkBtn(cfg.installBtnText.c_str(), IDC_NAV_PRIMARY, BS_DEFPUSHBUTTON);
        hBtnCancel        = mkBtn(L"Cancel",            IDC_NAV_CANCEL,     BS_PUSHBUTTON);
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
    }

    void ApplyFontsAndLayout() {
        const UINT dpi = ::GetDpiForWindow(hWnd);
        RebuildFonts(dpi);

        for (HWND h : {hModeExpress, hModeCompact, hModeCustom, hMaintRepair, hMaintReinstall, hMaintRemove, hBtnPrimary}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);
        }
        for (HWND h : {hModeDesc, hLicRich, hLicAccept, hDirLabel, hDirEdit, hDirBrowse,
                       hProgStatus, hFinSummary, hFinLaunch, hFinViewLog, hBtnBack, hBtnCancel, hBtnAutorunFolder}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBody), TRUE);
        }

        if (hLicRich && hRichEditDll && !theme.isHighContrast) {
            ::SendMessageW(hLicRich, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(theme.cardBg));
        }

        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = rc.right - rc.left;
        const int H = rc.bottom - rc.top;
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        const int footerH = S(58);
        const int footerY = H - footerH;
        const int btnY    = footerY + (footerH - S(32)) / 2;

        // Footer buttons
        ::MoveWindow(hBtnAutorunFolder, S(24),         btnY, S(136), S(32), TRUE);
        ::MoveWindow(hBtnBack,          W - S(322),    btnY, S(94),  S(32), TRUE);
        ::MoveWindow(hBtnPrimary,       W - S(220),    btnY, S(104), S(32), TRUE);
        ::MoveWindow(hBtnCancel,        W - S(108),    btnY, S(86),  S(32), TRUE);

        if (cfg.isAutorun16x9) {
            // 16:9 Autorun layout (`640x480`): top `290px` is artwork stage, middle `130px` is text/controls!
            const int bodyTop = S(308);
            ::MoveWindow(hDirLabel,    S(24), bodyTop,         W - S(48),  S(22), TRUE);
            ::MoveWindow(hDirEdit,     S(24), bodyTop + S(26), W - S(140), S(28), TRUE);
            ::MoveWindow(hDirBrowse,   W - S(108), bodyTop + S(25), S(84), S(30), TRUE);

            ::MoveWindow(hProgStatus,  S(24), bodyTop + S(8),  W - S(48),  S(24), TRUE);
            ::MoveWindow(hProgBar,     S(24), bodyTop + S(38), W - S(48),  S(22), TRUE);

            ::MoveWindow(hFinSummary,  S(24), bodyTop + S(4),  W - S(48),  S(44), TRUE);
            ::MoveWindow(hFinLaunch,   S(24), bodyTop + S(52), W - S(180), S(24), TRUE);
            ::MoveWindow(hFinViewLog,  W - S(148), bodyTop + S(50), S(124), S(28), TRUE);
        } else {
            // Standard Branded RaggieSoft Glass Wizard (`620x470`): top `84px` is Midnight Navy banner!
            const int bodyTop = S(104);
            const int leftX   = (hArtworkBmp != nullptr && currentPage == WizardPage::WelcomeMode) ? S(196) : S(28);
            const int bodyW   = W - leftX - S(28);

            ::MoveWindow(hModeExpress, leftX, bodyTop + S(44),  bodyW, S(28), TRUE);
            ::MoveWindow(hModeCompact, leftX, bodyTop + S(80),  bodyW, S(28), TRUE);
            ::MoveWindow(hModeCustom,  leftX, bodyTop + S(116), bodyW, S(28), TRUE);
            ::MoveWindow(hModeDesc,    leftX, bodyTop + S(162), bodyW, S(76), TRUE);

            ::MoveWindow(hMaintRepair,    S(28), bodyTop + S(52),  W - S(56), S(30), TRUE);
            ::MoveWindow(hMaintReinstall, S(28), bodyTop + S(94),  W - S(56), S(30), TRUE);
            ::MoveWindow(hMaintRemove,    S(28), bodyTop + S(136), W - S(56), S(30), TRUE);

            ::MoveWindow(hLicRich,   S(28), bodyTop + S(6),  W - S(56), footerY - bodyTop - S(54), TRUE);
            ::MoveWindow(hLicAccept, S(28), footerY - S(38), W - S(56), S(24), TRUE);

            ::MoveWindow(hDirLabel,  S(28), bodyTop + S(24), W - S(56),  S(24), TRUE);
            ::MoveWindow(hDirEdit,   S(28), bodyTop + S(56), W - S(156), S(30), TRUE);
            ::MoveWindow(hDirBrowse, W - S(120), bodyTop + S(55), S(92), S(32), TRUE);

            ::MoveWindow(hProgStatus, S(28), bodyTop + S(48), W - S(56), S(26), TRUE);
            ::MoveWindow(hProgBar,    S(28), bodyTop + S(82), W - S(56), S(24), TRUE);

            ::MoveWindow(hFinSummary, S(28), bodyTop + S(32), W - S(56), S(74), TRUE);
            ::MoveWindow(hFinLaunch,  S(28), bodyTop + S(116), W - S(56), S(28), TRUE);
            ::MoveWindow(hFinViewLog, S(28), bodyTop + S(156), S(148), S(32), TRUE);
        }

        UpdateControlVisibilityForPage();
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    void UpdateControlVisibilityForPage() {
        auto show = [](HWND h, bool vis) {
            if (h) ::ShowWindow(h, vis ? SW_SHOW : SW_HIDE);
        };

        const bool isWelcome = (currentPage == WizardPage::WelcomeMode);
        const bool isMaint   = (currentPage == WizardPage::Maintenance);
        const bool isLic     = (currentPage == WizardPage::License);
        const bool isDir     = (currentPage == WizardPage::CustomFolder);
        const bool isProg    = (currentPage == WizardPage::Installing);
        const bool isFin     = (currentPage == WizardPage::Finished);

        show(hModeExpress, isWelcome && !cfg.isAutorun16x9);
        show(hModeCompact, isWelcome && !cfg.isAutorun16x9);
        show(hModeCustom,  isWelcome && !cfg.isAutorun16x9);
        show(hModeDesc,    isWelcome && !cfg.isAutorun16x9);

        show(hMaintRepair,    isMaint);
        show(hMaintReinstall, isMaint);
        show(hMaintRemove,    isMaint);

        show(hLicRich,   isLic);
        show(hLicAccept, isLic);

        show(hDirLabel,  isDir);
        show(hDirEdit,   isDir);
        show(hDirBrowse, isDir);

        show(hProgStatus, isProg);
        show(hProgBar,    isProg);

        show(hFinSummary, isFin);
        const bool canLaunch = isFin && (finalMsiResult == ERROR_SUCCESS) &&
                               (selectedAction != SelectedAction::MaintUninstall) &&
                               cfg.offerLaunch && !cfg.mainExecutable.empty();
        show(hFinLaunch,  canLaunch);
        show(hFinViewLog, isFin && (finalMsiResult != ERROR_SUCCESS));

        show(hBtnAutorunFolder, cfg.isAutorun16x9 && isWelcome);
        show(hBtnBack,          (isLic || isDir));
        show(hBtnPrimary,       !isProg);
        show(hBtnCancel,        !isFin);

        // Configure primary button caption, UAC shield (only right before per-machine execution!), & enabled state
        bool showShield = false;
        if (isWelcome) {
            const bool goesStraightToInstall = (selectedAction != SelectedAction::InstallCustom) && !cfg.hasLicenseRtf;
            ::SetWindowTextW(hBtnPrimary, goesStraightToInstall ? cfg.installBtnText.c_str() : L"&Continue >");
            ::EnableWindow(hBtnPrimary, TRUE);
            showShield = goesStraightToInstall && cfg.perMachine;
        } else if (isMaint) {
            ::SetWindowTextW(hBtnPrimary, L"&Continue >");
            ::EnableWindow(hBtnPrimary, TRUE);
            showShield = cfg.perMachine;
        } else if (isLic) {
            const bool accepted = (::SendMessageW(hLicAccept, BM_GETCHECK, 0, 0) == BST_CHECKED);
            const bool nextIsDir = (selectedAction == SelectedAction::InstallCustom);
            ::SetWindowTextW(hBtnPrimary, nextIsDir ? L"&Next >" : cfg.installBtnText.c_str());
            ::EnableWindow(hBtnPrimary, accepted ? TRUE : FALSE);
            showShield = !nextIsDir && cfg.perMachine;
        } else if (isDir) {
            ::SetWindowTextW(hBtnPrimary, cfg.installBtnText.c_str());
            ::EnableWindow(hBtnPrimary, TRUE);
            showShield = cfg.perMachine;
        } else if (isFin) {
            ::SetWindowTextW(hBtnPrimary, L"&Finish");
            ::EnableWindow(hBtnPrimary, TRUE);
        }
        ::SendMessageW(hBtnPrimary, BCM_SETSHIELD, 0, showShield ? TRUE : FALSE);

        // Notify screen readers (Narrator / NVDA / JAWS) that the wizard stage changed
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
                        ::CoTaskMemFree(pszPath);
                    }
                    pItem->Release();
                }
            }
            pDlg->Release();
        }
    }

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
                    // Reset progress phase
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

    void StartMsiWorkerTransaction() {
        // Sync custom folder text if edited
        wchar_t editBuf[MAX_PATH * 4]{};
        if (::GetWindowTextW(hDirEdit, editBuf, static_cast<int>(std::size(editBuf))) > 0) {
            installDir = editBuf;
        }

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

        cancelRequested.store(false);
        installRunning.store(true);

        if (workerThread.joinable()) workerThread.join();
        workerThread = std::thread([this]() {
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
                rc = ::MsiConfigureProductExW(installedProductCode.c_str(), INSTALLLEVEL_DEFAULT, INSTALLSTATE_ABSENT, L"");
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
                    rc = ::MsiReinstallProductW(installedProductCode.c_str(), REINSTALLMODE_FILEREPLACE | REINSTALLMODE_SHORTCUT | REINSTALLMODE_MACHINEDATA | REINSTALLMODE_USERDATA);
                }
            } else {
                std::wstring props = L"INSTALLDIR=\"" + installDir + L"\"";
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
        currentPage = WizardPage::Finished;
        std::wstring summary;
        if (finalMsiResult == ERROR_SUCCESS || finalMsiResult == ERROR_SUCCESS_REBOOT_REQUIRED) {
            if (selectedAction == SelectedAction::MaintUninstall) {
                summary = cfg.productName + L" has been cleanly removed from your computer.";
            } else if (selectedAction == SelectedAction::MaintQuickRepair || selectedAction == SelectedAction::MaintFullReinstall) {
                summary = cfg.productName + L" has been verified and repaired.\r\n\r\nClick Finish to close Setup.";
            } else {
                summary = cfg.productName + L" (v" + cfg.version + L") is ready to use!\r\nInstalled to: " + installDir;
            }
        } else if (finalMsiResult == ERROR_INSTALL_USEREXIT) {
            summary = L"Setup was canceled. Any partial changes were rolled back automatically.";
        } else {
            summary = L"Setup could not complete (code " + std::to_wstring(finalMsiResult) + L").\r\n" +
                      (capturedErrorText.empty() ? L"Click View Setup Log for details." : capturedErrorText);
        }
        ::SetWindowTextW(hFinSummary, summary.c_str());
        UpdateControlVisibilityForPage();
        ::SetFocus(hBtnPrimary);
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    void OnPrimaryButtonClicked() {
        switch (currentPage) {
            case WizardPage::WelcomeMode:
                if (cfg.hasLicenseRtf) {
                    currentPage = WizardPage::License;
                    UpdateControlVisibilityForPage();
                    ::SetFocus(hLicAccept);
                } else if (selectedAction == SelectedAction::InstallCustom) {
                    currentPage = WizardPage::CustomFolder;
                    UpdateControlVisibilityForPage();
                    ::SetFocus(hDirEdit);
                } else {
                    StartMsiWorkerTransaction();
                }
                ::InvalidateRect(hWnd, nullptr, TRUE);
                break;

            case WizardPage::Maintenance:
                StartMsiWorkerTransaction();
                break;

            case WizardPage::License:
                if (selectedAction == SelectedAction::InstallCustom) {
                    currentPage = WizardPage::CustomFolder;
                    UpdateControlVisibilityForPage();
                    ::SetFocus(hDirEdit);
                    ::InvalidateRect(hWnd, nullptr, TRUE);
                } else {
                    StartMsiWorkerTransaction();
                }
                break;

            case WizardPage::CustomFolder:
                StartMsiWorkerTransaction();
                break;

            case WizardPage::Finished:
                if (finalMsiResult == ERROR_SUCCESS &&
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
        if (currentPage == WizardPage::CustomFolder) {
            currentPage = cfg.hasLicenseRtf ? WizardPage::License : WizardPage::WelcomeMode;
        } else if (currentPage == WizardPage::License) {
            currentPage = WizardPage::WelcomeMode;
        }
        UpdateControlVisibilityForPage();
        ::InvalidateRect(hWnd, nullptr, TRUE);
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

        // 1. Window surface + Footer bar
        ::FillRect(memDc, &clientRc, theme.hbrWindow);
        const int footerH = S(58);
        RECT footerRc{0, H - footerH, W, H};
        ::FillRect(memDc, &footerRc, theme.hbrFooter);

        HPEN dividerPen = ::CreatePen(PS_SOLID, 1, theme.borderSubtle);
        HGDIOBJ oldPen  = ::SelectObject(memDc, dividerPen);
        ::MoveToEx(memDc, 0, footerRc.top, nullptr);
        ::LineTo(memDc, W, footerRc.top);

        ::SetBkMode(memDc, TRANSPARENT);

        if (cfg.isAutorun16x9) {
            // 16:9 OpenGraph CD-Autorun Header Stage (`W x 296`)
            RECT heroRc{0, 0, W, S(292)};
            rsw::ui::FillHorizontalGradient(memDc, heroRc, theme.bannerNavyLeft, theme.bannerNavyRight);
            if (hArtworkBmp) {
                rsw::ui::DrawAlphaBitmapFit(memDc, hArtworkBmp, artworkW, artworkH, heroRc);
            }
            RECT accentBar{0, heroRc.bottom, W, heroRc.bottom + S(4)};
            rsw::ui::FillHorizontalGradient(memDc, accentBar, cfg.accentPrimary, cfg.accentSecondary);

            // Overlay title card when no full-bleed banner was supplied (or draw clean subtitle below)
            if (!hArtworkBmp) {
                HGDIOBJ oldF = ::SelectObject(memDc, hFontTitle);
                ::SetTextColor(memDc, theme.bannerTitleText);
                RECT tRc{S(32), S(88), W - S(32), S(140)};
                ::DrawTextW(memDc, cfg.productName.c_str(), -1, &tRc, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
                ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.bannerSubText);
                RECT sRc{S(32), S(136), W - S(32), S(190)};
                ::DrawTextW(memDc, cfg.tagline.c_str(), -1, &sRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                ::SelectObject(memDc, oldF);
            }

            if (currentPage == WizardPage::WelcomeMode) {
                HGDIOBJ oldF = ::SelectObject(memDc, hFontSection);
                ::SetTextColor(memDc, theme.textPrimary);
                std::wstring head = cfg.authorOrArtist.empty()
                    ? cfg.productName
                    : (cfg.productName + L" — " + cfg.authorOrArtist);
                RECT hRc{S(24), S(308), W - S(24), S(340)};
                ::DrawTextW(memDc, head.c_str(), -1, &hRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                ::SelectObject(memDc, hFontBody);
                ::SetTextColor(memDc, theme.textSecondary);
                RECT bRc{S(24), S(340), W - S(24), footerRc.top - S(10)};
                ::DrawTextW(memDc, cfg.tagline.c_str(), -1, &bRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                ::SelectObject(memDc, oldF);
            }
        } else {
            // Standard Branded RaggieSoft Midnight Navy Header (`W x 84`) + 4px Cobalt/Gold Accent Bar!
            RECT bannerRc{0, 0, W, S(84)};
            rsw::ui::FillHorizontalGradient(memDc, bannerRc, theme.bannerNavyLeft, theme.bannerNavyRight);
            RECT accentBar{0, bannerRc.bottom, W, bannerRc.bottom + S(4)};
            rsw::ui::FillHorizontalGradient(memDc, accentBar, theme.accentPrimary, theme.accentSecondary);

            int textLeft = S(28);
            if (hAppIconBig) {
                const int icoSz = S(44);
                ::DrawIconEx(memDc, S(24), S(20), hAppIconBig, icoSz, icoSz, 0, nullptr, DI_NORMAL);
                textLeft = S(80);
            }

            HGDIOBJ oldF = ::SelectObject(memDc, hFontTitle);
            ::SetTextColor(memDc, theme.bannerTitleText);
            const std::wstring topTitle = cfg.productName + L"  v" + cfg.version;
            RECT titleRc{textLeft, S(16), W - S(24), S(48)};
            ::DrawTextW(memDc, topTitle.c_str(), -1, &titleRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            ::SelectObject(memDc, hFontBody);
            ::SetTextColor(memDc, theme.bannerSubText);
            RECT subRc{textLeft, S(48), W - S(24), S(76)};
            ::DrawTextW(memDc, cfg.tagline.c_str(), -1, &subRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            // Optional left cover artwork card on Page 1 + Page section headline
            const int bodyTop = S(104);
            int headLeft = S(28);
            if (currentPage == WizardPage::WelcomeMode && hArtworkBmp) {
                RECT artCard{S(28), bodyTop, S(178), bodyTop + S(210)};
                ::FillRect(memDc, &artCard, theme.hbrCard);
                rsw::ui::DrawAlphaBitmapFit(memDc, hArtworkBmp, artworkW, artworkH, artCard);
                headLeft = S(196);
            }

            ::SelectObject(memDc, hFontSection);
            ::SetTextColor(memDc, theme.textPrimary);
            std::wstring pageHeadline = cfg.welcomeHeadline;
            if (currentPage == WizardPage::Maintenance)  pageHeadline = L"Modify, Repair, or Remove " + cfg.productName;
            if (currentPage == WizardPage::License)      pageHeadline = L"Software License Agreement";
            if (currentPage == WizardPage::CustomFolder) pageHeadline = L"Select Destination Folder";
            if (currentPage == WizardPage::Installing)   pageHeadline = L"Configuring " + cfg.productName + L"...";
            if (currentPage == WizardPage::Finished)     pageHeadline = L"Setup Complete";

            if (currentPage != WizardPage::License) {
                RECT hRc{headLeft, bodyTop, W - S(28), bodyTop + S(32)};
                ::DrawTextW(memDc, pageHeadline.c_str(), -1, &hRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
            }

            // 100% White-Label Publisher & Optional Website Badge in Footer
            ::SelectObject(memDc, hFontSmall);
            ::SetTextColor(memDc, theme.textSecondary);
            const std::wstring brandFoot = cfg.websiteLabel.empty()
                ? cfg.manufacturer
                : (cfg.manufacturer + L"  •  " + cfg.websiteLabel);
            RECT footTextRc{S(28), footerRc.top, W - S(336), footerRc.bottom};
            ::DrawTextW(memDc, brandFoot.c_str(), -1, &footTextRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            ::SelectObject(memDc, oldF);
        }

        ::BitBlt(hdcOut, 0, 0, W, H, memDc, 0, 0, SRCCOPY);

        ::SelectObject(memDc, oldPen);
        ::DeleteObject(dividerPen);
        ::SelectObject(memDc, oldBmp);
        ::DeleteObject(memBmp);
        ::DeleteDC(memDc);
    }

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
                return 1; // Double-buffered in WM_PAINT

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
                ::SetTextColor(hdcChild, self->theme.textPrimary);
                ::SetBkColor(hdcChild, self->theme.windowBg);
                return reinterpret_cast<LRESULT>(self->theme.hbrWindow);
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
                const int id = LOWORD(wParam);
                switch (id) {
                    case IDC_MODE_EXPRESS:
                        self->selectedAction = SelectedAction::InstallExpress;
                        ::SetWindowTextW(self->hModeDesc, self->cfg.expressDesc.c_str());
                        self->UpdateControlVisibilityForPage();
                        return 0;
                    case IDC_MODE_COMPACT:
                        self->selectedAction = SelectedAction::InstallCompact;
                        ::SetWindowTextW(self->hModeDesc, self->cfg.compactDesc.c_str());
                        self->UpdateControlVisibilityForPage();
                        return 0;
                    case IDC_MODE_CUSTOM:
                        self->selectedAction = SelectedAction::InstallCustom;
                        ::SetWindowTextW(self->hModeDesc, self->cfg.customDesc.c_str());
                        self->UpdateControlVisibilityForPage();
                        return 0;

                    case IDC_MAINT_REPAIR:
                        self->selectedAction = SelectedAction::MaintQuickRepair;
                        return 0;
                    case IDC_MAINT_REINSTALL:
                        self->selectedAction = SelectedAction::MaintFullReinstall;
                        return 0;
                    case IDC_MAINT_REMOVE:
                        self->selectedAction = SelectedAction::MaintUninstall;
                        return 0;

                    case IDC_LIC_ACCEPT_CHK:
                        self->UpdateControlVisibilityForPage();
                        return 0;

                    case IDC_DIR_BROWSE_BTN:
                        self->BrowseForDestinationFolder();
                        return 0;

                    case IDC_AUTORUN_FOLDER:
                        self->selectedAction = SelectedAction::InstallCustom;
                        self->currentPage    = WizardPage::CustomFolder;
                        self->UpdateControlVisibilityForPage();
                        ::SetFocus(self->hDirEdit);
                        ::InvalidateRect(hWnd, nullptr, TRUE);
                        return 0;

                    case IDC_FIN_VIEWLOG_BTN:
                        if (!self->msiLogPath.empty()) {
                            ::ShellExecuteW(hWnd, L"open", L"notepad.exe", self->msiLogPath.wstring().c_str(), nullptr, SW_SHOWNORMAL);
                        }
                        return 0;

                    case IDC_NAV_PRIMARY:
                    case IDOK:
                        self->OnPrimaryButtonClicked();
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

            case WM_RSW_PROGRESS_UPDATE:
                ::SendMessageW(self->hProgBar, PBM_SETPOS, wParam, 0);
                return 0;

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

    // Fast automated self-check flag used by `build-and-verify.ps1` / `rsswtests.exe`
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

    // 2. Initialize COM + Common Controls v6 for WIC PNG decoding & modern Windows 11 dialogs
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_PROGRESS_CLASS};
    ::InitCommonControlsEx(&icc);

    SetupWizardApp app{};
    app.cfg = LoadBrandingConfigFromResource();

    // 3. True Windows 11 Build Gate (`RtlGetVersion` pierces compat shims!)
    const auto hostOs = rsw::QueryHostPlatform();
    if (!rsw::MeetsWindowsBuildFloor(app.cfg.minWindowsBuild)) {
        const std::wstring title = app.cfg.productName + L" Setup";
        const std::wstring detail = app.cfg.osLaunchMessage + L"\n\nDetected OS Build: " + std::to_wstring(hostOs.buildNumber);
        ::TaskDialog(nullptr, hInstance, title.c_str(), L"Unsupported Windows Version", detail.c_str(), TDCBF_CLOSE_BUTTON, TD_WARNING_ICON, nullptr);
        ::CoUninitialize();
        return 1603;
    }

    app.installDir = ComputeDefaultInstallDir(app.cfg);
    app.DetectExistingInstallation();
    app.theme.Refresh(app.cfg.accentPrimary, app.cfg.accentSecondary);

    // Load product `.ico` (`#101` stamped by `rsswbuild.exe`, falling back to `IDI_APPLICATION`)
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
    const int logH = app.cfg.isAutorun16x9 ? 480 : 470;
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
    //    `Arrow` keys across radio buttons, `Enter` default action, `Esc` cancel, and `Alt+Key` mnemonics!)
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
