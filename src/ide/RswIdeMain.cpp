// ================================================================================
// FILE: src/ide/RswIdeMain.cpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `InstallerStudio.exe` ("RaggieSoft Installer Studio")?
//     `InstallerStudio.exe` is our **7-View Native Win32 C++20 Visual IDE** for
//     authoring both standalone `.rsip` (RaggieSoft Installer Project) manifests
//     AND multi-project `.rsis` (RaggieSoft Installer Suite) workspaces! Compiled
//     directly with `cl.exe` and the stock Windows 11 SDK (`/MT` static binary,
//     zero .NET, zero Electron, zero VCL dependencies), it pairs:
//       1. **Package & Identity View**    : Product Name, Tagline, White-Label
//          Website, Version, Manufacturer, UpgradeCode GUID, Scope, & MSI/MSM Kind.
//       2. **Files & Folders View**       : Visual `[Payload.<id>]` `SysListView32`
//          table with one-click Add File, Add Directory, and Remove Payload buttons.
//       3. **Shortcuts & Registry View**  : Visual `[Shortcut.*]` & `[Registry.*]`
//          tables + quick-add forms for Start Menu/Desktop links and GPO keys.
//       4. **Wizard UI & Live Preview**   : Accent Hex colors, `standard` vs
//          `autorun16x9` layout picker, Icon/License/Artwork pickers, AND a
//          **real-time GDI Live Wizard Preview Canvas** right inside the IDE!
//       5. **Code Signing (Authenticode)**: Visual `[Signing]` editor supporting
//          Windows Certificate Store thumbprints AND Azure Artifact Signing!
//       6. **Enterprise .MST & .MSP**     : Diff two `.msi` databases into a GPO
//          `.mst` Transform or `.msp` Patch, or open directly in `msiprism.exe`!
//       7. **.RSIP Source (Lossless AST)**: Full monospace source view proving
//          100% of `;` and `#` comments and `@include` directives survive every
//          GUI edit byte-for-byte!
// ================================================================================

#include "../common/RswWin32UiTheme.hpp"
#include "../common/RswCryptoAndPe.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswSuiteWorkspace.hpp"
#include "../common/RswVersion.hpp"
#include "../compiler/RswAiSpecEmitter.hpp"
#include "../compiler/RswExtraPackages.hpp"
#include "../compiler/RswValidator.hpp"
#include "../compiler/RswVpatEmitter.hpp"

#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

enum StudioView : int {
    VIEW_PACKAGE   = 0,
    VIEW_PAYLOADS  = 1,
    VIEW_SHORTCUTS = 2,
    VIEW_WIZARD_UI = 3,
    VIEW_SIGNING   = 4,
    VIEW_MST_MSP   = 5,
    VIEW_SOURCE    = 6
};

enum IdeId : int {
    IDM_FILE_NEW        = 2001,
    IDM_FILE_OPEN       = 2002,
    IDM_FILE_SAVE       = 2003,
    IDM_FILE_EXIT       = 2004,
    IDM_BUILD_VALIDATE  = 2010,
    IDM_BUILD_MSI       = 2011,
    IDM_BUILD_MSM       = 2012,
    IDM_BUILD_VPAT      = 2013,
    IDM_BUILD_EMIT_AI   = 2014,
    IDM_LAUNCH_MSIPRISM = 2015,
    IDM_OPEN_OUTDIR     = 2016,

    IDC_TB_NEW          = 2101,
    IDC_TB_OPEN         = 2102,
    IDC_TB_SAVE         = 2103,
    IDC_TB_VALIDATE     = 2104,
    IDC_TB_BUILD_MSI    = 2105,
    IDC_TB_BUILD_MSM    = 2106,
    IDC_TB_EXPORT_VPAT  = 2107,
    IDC_TB_MSIPRISM     = 2108,
    IDC_ARCH_COMBO      = 2109,
    IDC_SUITE_COMBO     = 2110,

    IDC_NAV_VIEWS       = 2200,

    // View 0: Package & Identity
    IDC_PKG_NAME        = 2210,
    IDC_PKG_TAGLINE     = 2211,
    IDC_PKG_WEBSITE     = 2212,
    IDC_PKG_VERSION     = 2213,
    IDC_PKG_PUBLISHER   = 2214,
    IDC_PKG_UPGRADECODE = 2215,
    IDC_PKG_NEW_GUID    = 2216,
    IDC_PKG_SUBDIR      = 2217,
    IDC_PKG_MAINEXE     = 2218,
    IDC_PKG_SCOPE       = 2219,
    IDC_PKG_APPLY       = 2220,

    // View 1: Payloads
    IDC_PAY_GRID        = 2230,
    IDC_PAY_ADD_FILE    = 2231,
    IDC_PAY_ADD_FOLDER  = 2232,
    IDC_PAY_REMOVE      = 2233,

    // View 2: Shortcuts & Registry
    IDC_SC_GRID         = 2240,
    IDC_SC_NAME         = 2241,
    IDC_SC_TARGET       = 2242,
    IDC_SC_ADD_BTN      = 2243,
    IDC_REG_KEY         = 2244,
    IDC_REG_NAME        = 2245,
    IDC_REG_VALUE       = 2246,
    IDC_REG_ADD_BTN     = 2247,

    // View 3: Wizard UI & Live Preview
    IDC_UI_LAYOUT       = 2250,
    IDC_UI_PRIMARY_HEX  = 2251,
    IDC_UI_SECONDARY_HEX= 2252,
    IDC_UI_INSTALL_BTN  = 2253,
    IDC_UI_ICON_PATH    = 2254,
    IDC_UI_ICON_BROWSE  = 2255,
    IDC_UI_ART_PATH     = 2256,
    IDC_UI_ART_BROWSE   = 2257,
    IDC_UI_APPLY        = 2258,

    // View 4: Authenticode Signing
    IDC_SIG_ENABLED     = 2260,
    IDC_SIG_THUMBPRINT  = 2261,
    IDC_SIG_TIMESTAMP   = 2262,
    IDC_SIG_DLIB        = 2263,
    IDC_SIG_META        = 2264,
    IDC_SIG_APPLY       = 2265,

    // View 5: Enterprise GPO (.MST & .MSP)
    IDC_ENT_BASE_MSI    = 2270,
    IDC_ENT_BASE_BROWSE = 2271,
    IDC_ENT_TARG_MSI    = 2272,
    IDC_ENT_TARG_BROWSE = 2273,
    IDC_ENT_GEN_MST     = 2274,
    IDC_ENT_GEN_MSP     = 2275,

    // View 6: Lossless `.rsip` Source & Bottom Console
    IDC_SOURCE_EDIT     = 2301,
    IDC_CONSOLE_EDIT    = 2302
};

std::string BuildStarterRsipTemplate() {
    const std::string guid = rsw::GenerateRandomMsiGuid();
    std::ostringstream s;
    s << "; ==============================================================================\r\n"
      << "; RaggieSoft Installer Project (.rsip) — Author-Owned White-Label Manifest\r\n"
      << "; Every ';', '#', and '@include' line is preserved 100% intact across saves!\r\n"
      << "; ==============================================================================\r\n\r\n"
      << "[Package]\r\n"
      << "Name              = My Native Application\r\n"
      << "Tagline           = Fast, Accessible Windows Desktop Software\r\n"
      << "Version           = 1.0.0\r\n"
      << "Manufacturer      = Sample Studio\r\n"
      << "UpgradeCode       = " << guid << " ; Permanent UpgradeCode across releases\r\n"
      << "Architecture      = $(ARCH)\r\n"
      << "Scope             = machine\r\n"
      << "BuildBootstrapper = true\r\n\r\n"
      << "[Requirements]\r\n"
      << "MinWindowsBuild   = 22000\r\n\r\n"
      << "[Install]\r\n"
      << "DefaultSubdir     = Sample Studio\\My Native App\r\n"
      << "MainExecutable    = HelloRaggieSoft.exe\r\n\r\n"
      << "[UI]\r\n"
      << "WizardLayout      = standard\r\n"
      << "AccentPrimaryHex  = #2563EB\r\n"
      << "AccentSecondaryHex= #FBBF24\r\n"
      << "InstallButtonText = &Install\r\n\r\n"
      << "[Payload.App]\r\n"
      << "Source            = ..\\..\\bin\\$(ARCH)\\HelloRaggieSoft.exe\r\n";
    return s.str();
}

std::wstring RunChildCapture(const std::wstring& cmdLine, DWORD& outExitCode) {
    outExitCode = 1;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE hRead = nullptr, hWrite = nullptr;
    if (!::CreatePipe(&hRead, &hWrite, &sa, 0)) return L"[ERROR] CreatePipe failed.\r\n";
    ::SetHandleInformation(hRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.hStdInput  = ::GetStdHandle(STD_INPUT_HANDLE);

    PROCESS_INFORMATION pi{};
    std::wstring mutableCmd = cmdLine;
    const BOOL ok = ::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    ::CloseHandle(hWrite);
    if (!ok) {
        ::CloseHandle(hRead);
        return L"[ERROR] Failed launching instbuild.exe.\r\n";
    }

    std::string raw;
    char buf[4096];
    DWORD n = 0;
    while (::ReadFile(hRead, buf, sizeof(buf), &n, nullptr) && n > 0) raw.append(buf, n);
    ::CloseHandle(hRead);

    ::WaitForSingleObject(pi.hProcess, INFINITE);
    ::GetExitCodeProcess(pi.hProcess, &outExitCode);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);

    std::string crlf;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\n' && (i == 0 || raw[i - 1] != '\r')) crlf += '\r';
        crlf += raw[i];
    }
    return rsw::Utf8ToWide(crlf);
}

struct TaggedControl {
    int  viewIndex; // -1 = Always visible (toolbar / sidebar / console)
    HWND hwnd;
};

class InstallerStudioIde {
public:
    rsw::ui::RswTheme        theme{};
    HWND                     hWnd       = nullptr;
    HFONT                    hFontBanner= nullptr;
    HFONT                    hFontHead  = nullptr;
    HFONT                    hFontUi    = nullptr;
    HFONT                    hFontBold  = nullptr;
    HFONT                    hFontMono  = nullptr;

    std::filesystem::path    ideExeDir;
    std::filesystem::path    currentFilePath;
    rsw::RswSuiteWorkspace   activeSuite{};
    bool                     hasActiveSuite = false;
    rsw::RswManifestDocument astDoc{};
    int                      activeView = VIEW_PACKAGE;
    std::vector<TaggedControl> taggedControls;
    RECT                     previewCanvasRc{};

    // Persistent controls
    HWND hBtnNew = nullptr, hBtnOpen = nullptr, hBtnSave = nullptr, hBtnValidate = nullptr;
    HWND hBtnBuildMsi = nullptr, hBtnBuildMsm = nullptr, hBtnExportVpat = nullptr, hBtnMsiPrism = nullptr;
    HWND hArchCombo = nullptr, hSuiteCombo = nullptr;
    HWND hNavViews = nullptr, hConsoleEdit = nullptr;

    // View 0: Package & Identity
    HWND hPkgName = nullptr, hPkgTag = nullptr, hPkgWeb = nullptr, hPkgVer = nullptr, hPkgPub = nullptr;
    HWND hPkgGuid = nullptr, hPkgBtnGuid = nullptr, hPkgSub = nullptr, hPkgExe = nullptr, hPkgScope = nullptr, hPkgApply = nullptr;

    // View 1: Payloads
    HWND hPayGrid = nullptr, hPayAddFile = nullptr, hPayAddDir = nullptr, hPayRemove = nullptr;

    // View 2: Shortcuts & Registry
    HWND hScGrid = nullptr, hScName = nullptr, hScTarget = nullptr, hScAddBtn = nullptr;
    HWND hRegKey = nullptr, hRegName = nullptr, hRegVal = nullptr, hRegAddBtn = nullptr;

    // View 3: Wizard UI & Live Preview
    HWND hUiLayout = nullptr, hUiPrim = nullptr, hUiSec = nullptr, hUiInstBtn = nullptr;
    HWND hUiIcon = nullptr, hUiIconBtn = nullptr, hUiArt = nullptr, hUiArtBtn = nullptr, hUiApply = nullptr;

    // View 4: Authenticode Signing
    HWND hSigEnable = nullptr, hSigThumb = nullptr, hSigTs = nullptr, hSigDlib = nullptr, hSigMeta = nullptr, hSigApply = nullptr;

    // View 5: Enterprise .MST & .MSP
    HWND hEntBase = nullptr, hEntBaseBtn = nullptr, hEntTarg = nullptr, hEntTargBtn = nullptr, hEntGenMst = nullptr, hEntGenMsp = nullptr;

    // View 6: Full Monospace `.rsip` Source
    HWND hSourceEdit = nullptr;

    ~InstallerStudioIde() { DestroyFonts(); }

    void DestroyFonts() noexcept {
        for (HFONT* f : {&hFontBanner, &hFontHead, &hFontUi, &hFontBold, &hFontMono}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
    }

    HMENU BuildMenuBar() {
        HMENU hBar  = ::CreateMenu();
        HMENU hFile = ::CreatePopupMenu();
        ::AppendMenuW(hFile, MF_STRING, IDM_FILE_NEW,  L"&New .rsip Project\tCtrl+N");
        ::AppendMenuW(hFile, MF_STRING, IDM_FILE_OPEN, L"&Open .rsip Project or .rsis Suite...\tCtrl+O");
        ::AppendMenuW(hFile, MF_STRING, IDM_FILE_SAVE, L"&Save Project (Lossless AST)\tCtrl+S");
        ::AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(hFile, MF_STRING, IDM_FILE_EXIT, L"E&xit");

        HMENU hBuild = ::CreatePopupMenu();
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_VALIDATE,  L"&Validate Manifest (JSON Linter)\tF7");
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_MSI,       L"&Build Enterprise MSI + Setup.exe\tF5");
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_MSM,       L"Build &Merge Module (.msm)\tF6");
        ::AppendMenuW(hBuild, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_VPAT,      L"Export Installer &VPAT 2.5 HTML5 Report");
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_EMIT_AI,   L"Emit &llms.txt + SKILL.md");
        ::AppendMenuW(hBuild, MF_STRING, IDM_LAUNCH_MSIPRISM, L"Open &MSI Prism Inspector (msiprism.exe)");
        ::AppendMenuW(hBuild, MF_STRING, IDM_OPEN_OUTDIR,     L"Open &Output Folder in Explorer");

        ::AppendMenuW(hBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hFile),  L"&File");
        ::AppendMenuW(hBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hBuild), L"&Build & Compliance");
        return hBar;
    }

    HWND RegisterViewCtrl(int viewIdx, HWND h) {
        if (h) taggedControls.push_back({viewIdx, h});
        return h;
    }

    void CreateControls() {
        HINSTANCE hInst = ::GetModuleHandleW(nullptr);
        auto mkBtn = [&](int view, const wchar_t* txt, int id, DWORD style = BS_PUSHBUTTON) {
            return RegisterViewCtrl(view, ::CreateWindowExW(
                0, L"BUTTON", txt, WS_CHILD | WS_TABSTOP | style,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr));
        };
        auto mkLbl = [&](int view, const wchar_t* txt) {
            return RegisterViewCtrl(view, ::CreateWindowExW(
                0, L"STATIC", txt, WS_CHILD, 0, 0, 10, 10, hWnd, nullptr, hInst, nullptr));
        };
        auto mkEd = [&](int view, int id) {
            return RegisterViewCtrl(view, ::CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", L"", WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr));
        };

        // Top Toolbar (`view = -1` -> always visible)
        hBtnNew        = mkBtn(-1, L"&New",           IDC_TB_NEW);
        hBtnOpen       = mkBtn(-1, L"&Open...",       IDC_TB_OPEN);
        hBtnSave       = mkBtn(-1, L"&Save",          IDC_TB_SAVE);
        hBtnValidate   = mkBtn(-1, L"&Validate (F7)", IDC_TB_VALIDATE);
        hBtnBuildMsi   = mkBtn(-1, L"&Build MSI+EXE (F5)", IDC_TB_BUILD_MSI);
        hBtnBuildMsm   = mkBtn(-1, L"Build .&MSM",    IDC_TB_BUILD_MSM);
        hBtnExportVpat = mkBtn(-1, L"Export &VPAT 2.5", IDC_TB_EXPORT_VPAT);
        hBtnMsiPrism   = mkBtn(-1, L"MSI &Prism",     IDC_TB_MSIPRISM);

        hArchCombo = RegisterViewCtrl(-1, ::CreateWindowExW(
            0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
            0, 0, 90, 120, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_ARCH_COMBO)), hInst, nullptr));
        ::SendMessageW(hArchCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"arm64"));
        ::SendMessageW(hArchCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"x64"));
        const auto hp = rsw::QueryHostPlatform();
        ::SendMessageW(hArchCombo, CB_SETCURSEL, (hp.NativeArchSlug() == "x64") ? 1 : 0, 0);

        // Suite Project Switcher (`CBS_DROPDOWNLIST` populated when opening `.rsis` Suites!)
        hSuiteCombo = RegisterViewCtrl(-1, ::CreateWindowExW(
            0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 220, 260, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SUITE_COMBO)), hInst, nullptr));
        ::SendMessageW(hSuiteCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"[Standalone .rsip Mode]"));
        ::SendMessageW(hSuiteCombo, CB_SETCURSEL, 0, 0);

        // Left 7-View Navigation Sidebar (`view = -1`)
        hNavViews = RegisterViewCtrl(-1, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_TABSTOP | LBS_NOTIFY,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_NAV_VIEWS)), hInst, nullptr));
        const wchar_t* viewLabels[] = {
            L"1. Package & Identity",
            L"2. Files & Folders",
            L"3. Shortcuts & Registry",
            L"4. Wizard UI & Preview",
            L"5. Code Signing",
            L"6. Enterprise .MST / .MSP",
            L"7. .RSIP Source (AST)"
        };
        for (const wchar_t* vl : viewLabels) {
            ::SendMessageW(hNavViews, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(vl));
        }
        ::SendMessageW(hNavViews, LB_SETCURSEL, 0, 0);

        // --- VIEW 0: Package & Identity ---
        mkLbl(VIEW_PACKAGE, L"Product Name:");      hPkgName = mkEd(VIEW_PACKAGE, IDC_PKG_NAME);
        mkLbl(VIEW_PACKAGE, L"Subtitle / Tagline:");hPkgTag  = mkEd(VIEW_PACKAGE, IDC_PKG_TAGLINE);
        mkLbl(VIEW_PACKAGE, L"Footer Website:");    hPkgWeb  = mkEd(VIEW_PACKAGE, IDC_PKG_WEBSITE);
        mkLbl(VIEW_PACKAGE, L"MSI Version (X.Y.Z):");hPkgVer = mkEd(VIEW_PACKAGE, IDC_PKG_VERSION);
        mkLbl(VIEW_PACKAGE, L"Manufacturer:");      hPkgPub  = mkEd(VIEW_PACKAGE, IDC_PKG_PUBLISHER);
        mkLbl(VIEW_PACKAGE, L"UpgradeCode GUID:");  hPkgGuid = mkEd(VIEW_PACKAGE, IDC_PKG_UPGRADECODE);
        hPkgBtnGuid = mkBtn(VIEW_PACKAGE, L"New &GUID", IDC_PKG_NEW_GUID);
        mkLbl(VIEW_PACKAGE, L"Install Subfolder:"); hPkgSub  = mkEd(VIEW_PACKAGE, IDC_PKG_SUBDIR);
        mkLbl(VIEW_PACKAGE, L"Main Executable:");   hPkgExe  = mkEd(VIEW_PACKAGE, IDC_PKG_MAINEXE);
        mkLbl(VIEW_PACKAGE, L"Install Scope:");
        hPkgScope = RegisterViewCtrl(VIEW_PACKAGE, ::CreateWindowExW(
            0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
            0, 0, 140, 120, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PKG_SCOPE)), hInst, nullptr));
        ::SendMessageW(hPkgScope, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"machine"));
        ::SendMessageW(hPkgScope, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"user"));
        hPkgApply = mkBtn(VIEW_PACKAGE, L"&Apply Package Identity to .rsip AST (Preserves Comments)", IDC_PKG_APPLY);

        // --- VIEW 1: Files & Folders (`[Payload.*]`) ---
        hPayGrid = RegisterViewCtrl(VIEW_PAYLOADS, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PAY_GRID)), hInst, nullptr));
        ListView_SetExtendedListViewStyle(hPayGrid, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        hPayAddFile = mkBtn(VIEW_PAYLOADS, L"&Add Payload File...",   IDC_PAY_ADD_FILE);
        hPayAddDir  = mkBtn(VIEW_PAYLOADS, L"Add Payload &Folder...", IDC_PAY_ADD_FOLDER);
        hPayRemove  = mkBtn(VIEW_PAYLOADS, L"&Remove Selected",       IDC_PAY_REMOVE);

        // --- VIEW 2: Shortcuts & Registry ---
        hScGrid = RegisterViewCtrl(VIEW_SHORTCUTS, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SC_GRID)), hInst, nullptr));
        ListView_SetExtendedListViewStyle(hScGrid, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        mkLbl(VIEW_SHORTCUTS, L"Shortcut Name:");  hScName   = mkEd(VIEW_SHORTCUTS, IDC_SC_NAME);
        mkLbl(VIEW_SHORTCUTS, L"Target Exe:");     hScTarget = mkEd(VIEW_SHORTCUTS, IDC_SC_TARGET);
        hScAddBtn  = mkBtn(VIEW_SHORTCUTS, L"Add &Shortcut", IDC_SC_ADD_BTN);
        mkLbl(VIEW_SHORTCUTS, L"Registry Key:");   hRegKey   = mkEd(VIEW_SHORTCUTS, IDC_REG_KEY);
        mkLbl(VIEW_SHORTCUTS, L"Value Name:");     hRegName  = mkEd(VIEW_SHORTCUTS, IDC_REG_NAME);
        mkLbl(VIEW_SHORTCUTS, L"Value Data:");     hRegVal   = mkEd(VIEW_SHORTCUTS, IDC_REG_VALUE);
        hRegAddBtn = mkBtn(VIEW_SHORTCUTS, L"Add &Registry Row", IDC_REG_ADD_BTN);

        // --- VIEW 3: Wizard UI & Live Preview ---
        mkLbl(VIEW_WIZARD_UI, L"Wizard Layout:");
        hUiLayout = RegisterViewCtrl(VIEW_WIZARD_UI, ::CreateWindowExW(
            0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST,
            0, 0, 160, 120, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_UI_LAYOUT)), hInst, nullptr));
        ::SendMessageW(hUiLayout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"standard"));
        ::SendMessageW(hUiLayout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"autorun16x9"));
        mkLbl(VIEW_WIZARD_UI, L"Primary Accent Hex:");   hUiPrim    = mkEd(VIEW_WIZARD_UI, IDC_UI_PRIMARY_HEX);
        mkLbl(VIEW_WIZARD_UI, L"Secondary Accent Hex:"); hUiSec     = mkEd(VIEW_WIZARD_UI, IDC_UI_SECONDARY_HEX);
        mkLbl(VIEW_WIZARD_UI, L"Install Button Text:");  hUiInstBtn = mkEd(VIEW_WIZARD_UI, IDC_UI_INSTALL_BTN);
        mkLbl(VIEW_WIZARD_UI, L"Icon (.ico) Path:");     hUiIcon    = mkEd(VIEW_WIZARD_UI, IDC_UI_ICON_PATH);
        hUiIconBtn = mkBtn(VIEW_WIZARD_UI, L"Pick .&ico...", IDC_UI_ICON_BROWSE);
        mkLbl(VIEW_WIZARD_UI, L"Artwork (.png/.jpg):");  hUiArt     = mkEd(VIEW_WIZARD_UI, IDC_UI_ART_PATH);
        hUiArtBtn  = mkBtn(VIEW_WIZARD_UI, L"Pick &Art...", IDC_UI_ART_BROWSE);
        hUiApply   = mkBtn(VIEW_WIZARD_UI, L"&Apply Branding & Refresh Live Preview", IDC_UI_APPLY);

        // --- VIEW 4: Authenticode Code Signing ---
        hSigEnable = mkBtn(VIEW_SIGNING, L"&Enable Dual-Layer Authenticode Signing (Product.msi + Setup.exe)", IDC_SIG_ENABLED, BS_AUTOCHECKBOX);
        mkLbl(VIEW_SIGNING, L"Cert Store SHA-1 Thumbprint:");  hSigThumb = mkEd(VIEW_SIGNING, IDC_SIG_THUMBPRINT);
        mkLbl(VIEW_SIGNING, L"RFC 3161 Timestamp Server:");    hSigTs    = mkEd(VIEW_SIGNING, IDC_SIG_TIMESTAMP);
        mkLbl(VIEW_SIGNING, L"Azure Artifact Signing Dlib:");  hSigDlib  = mkEd(VIEW_SIGNING, IDC_SIG_DLIB);
        mkLbl(VIEW_SIGNING, L"Azure Metadata JSON Path:");     hSigMeta  = mkEd(VIEW_SIGNING, IDC_SIG_META);
        hSigApply  = mkBtn(VIEW_SIGNING, L"&Save Code Signing Settings to .rsip AST", IDC_SIG_APPLY);

        // --- VIEW 5: Enterprise .MST & .MSP ---
        mkLbl(VIEW_MST_MSP, L"Baseline Released .MSI:");       hEntBase    = mkEd(VIEW_MST_MSP, IDC_ENT_BASE_MSI);
        hEntBaseBtn = mkBtn(VIEW_MST_MSP, L"&Browse...", IDC_ENT_BASE_BROWSE);
        mkLbl(VIEW_MST_MSP, L"Customized / Upgraded .MSI:");   hEntTarg    = mkEd(VIEW_MST_MSP, IDC_ENT_TARG_MSI);
        hEntTargBtn = mkBtn(VIEW_MST_MSP, L"B&rowse...", IDC_ENT_TARG_BROWSE);
        hEntGenMst  = mkBtn(VIEW_MST_MSP, L"Generate Group Policy Transform (.&MST)...", IDC_ENT_GEN_MST);
        hEntGenMsp  = mkBtn(VIEW_MST_MSP, L"Generate Update Patch Package (.MS&P)...",   IDC_ENT_GEN_MSP);

        // --- VIEW 6: Lossless `.rsip` Source ---
        hSourceEdit = RegisterViewCtrl(VIEW_SOURCE, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_AUTOHSCROLL | ES_WANTRETURN,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SOURCE_EDIT)), hInst, nullptr));
        ::SendMessageW(hSourceEdit, EM_SETLIMITTEXT, 0, 0);

        // Bottom Console (`view = -1`)
        hConsoleEdit = RegisterViewCtrl(-1, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_CONSOLE_EDIT)), hInst, nullptr));
        ::SendMessageW(hConsoleEdit, EM_SETLIMITTEXT, 0, 0);

        InitListViewColumns();
    }

    void InitListViewColumns() {
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };
        auto addCol = [&](HWND grid, int idx, const wchar_t* text, int w) {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<LPWSTR>(text);
            col.cx = S(w);
            col.iSubItem = idx;
            ListView_InsertColumn(grid, idx, &col);
        };
        addCol(hPayGrid, 0, L"Payload ID",   140);
        addCol(hPayGrid, 1, L"Source Path",  400);
        addCol(hPayGrid, 2, L"Target Subdir",180);
        addCol(hPayGrid, 3, L"Feature",      110);

        addCol(hScGrid,  0, L"Kind",         110);
        addCol(hScGrid,  1, L"Section ID",   150);
        addCol(hScGrid,  2, L"Name / Key",   280);
        addCol(hScGrid,  3, L"Target / Value",290);
    }

    void LayoutChildren() {
        const UINT dpi = ::GetDpiForWindow(hWnd);
        DestroyFonts();
        const int ts = theme.textScalePercent;
        hFontBanner = rsw::ui::CreateScaledFont(L"Segoe UI",      13, FW_BOLD,     dpi, ts);
        hFontHead   = rsw::ui::CreateScaledFont(L"Segoe UI",      11, FW_SEMIBOLD, dpi, ts);
        hFontUi     = rsw::ui::CreateScaledFont(L"Segoe UI",       9, FW_NORMAL,   dpi, ts);
        hFontBold   = rsw::ui::CreateScaledFont(L"Segoe UI",       9, FW_SEMIBOLD, dpi, ts);
        hFontMono   = rsw::ui::CreateScaledFont(L"Cascadia Mono", 10, FW_NORMAL,   dpi, ts);

        for (const auto& tc : taggedControls) {
            ::SendMessageW(tc.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUi), TRUE);
        }
        for (HWND h : {hBtnBuildMsi, hPkgApply, hUiApply, hSigApply}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);
        }
        for (HWND h : {hSourceEdit, hConsoleEdit}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontMono), TRUE);
        }
        if (!theme.isHighContrast) {
            for (HWND g : {hPayGrid, hScGrid}) {
                ListView_SetBkColor(g, theme.cardBg);
                ListView_SetTextBkColor(g, theme.cardBg);
                ListView_SetTextColor(g, theme.textPrimary);
            }
        }

        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(640, rc.right - rc.left);
        const int H = std::max<int>(480, rc.bottom - rc.top);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, ts); };

        const int tbY = S(60);
        int tx = S(12);
        auto placeTb = [&](HWND h, int w) { ::MoveWindow(h, tx, tbY, S(w), S(30), TRUE); tx += S(w + 5); };
        placeTb(hBtnNew,        54);
        placeTb(hBtnOpen,       68);
        placeTb(hBtnSave,       58);
        placeTb(hBtnValidate,   98);
        placeTb(hBtnBuildMsi,   138);
        placeTb(hBtnBuildMsm,   88);
        placeTb(hBtnExportVpat, 114);
        placeTb(hBtnMsiPrism,   86);
        ::MoveWindow(hArchCombo,  tx, tbY + S(2), S(80), S(140), TRUE);
        tx += S(86);
        ::MoveWindow(hSuiteCombo, tx, tbY + S(2), std::max(S(160), W - tx - S(12)), S(280), TRUE);

        const int contentTop = S(100);
        const int consoleH   = S(132);
        const int consoleY   = H - consoleH - S(10);
        const int mainH      = std::max(S(220), consoleY - contentTop - S(8));
        const int navW       = S(214);

        ::MoveWindow(hNavViews,    S(12), contentTop, navW, mainH, TRUE);
        ::MoveWindow(hConsoleEdit, S(12), consoleY,   W - S(24), consoleH, TRUE);

        const int paneX = S(24) + navW;
        const int paneW = std::max(S(320), W - paneX - S(12));

        auto layoutFormPairs = [&](int viewIdx) {
            std::vector<HWND> vh;
            for (const auto& tc : taggedControls) {
                if (tc.viewIndex == viewIdx) vh.push_back(tc.hwnd);
            }
            return vh;
        };

        // View 0: Package & Identity
        {
            auto v = layoutFormPairs(VIEW_PACKAGE);
            int y = contentTop + S(4);
            const int lw = S(148), fw = std::min(S(460), paneW - lw - S(16));
            auto row = [&](HWND lbl, HWND ed, int rightReserve = 0) {
                ::MoveWindow(lbl, paneX, y + S(4), lw, S(22), TRUE);
                ::MoveWindow(ed,  paneX + lw, y, fw - rightReserve, S(26), TRUE);
                y += S(32);
            };
            if (v.size() >= 20) {
                row(v[0], v[1]);
                row(v[2], v[3]);
                row(v[4], v[5]);
                row(v[6], v[7]);
                row(v[8], v[9]);
                row(v[10], v[11], S(92));
                ::MoveWindow(hPkgBtnGuid, paneX + lw + fw - S(86), y - S(32), S(86), S(26), TRUE);
                row(v[13], v[14]);
                row(v[15], v[16]);
                ::MoveWindow(v[17], paneX, y + S(4), lw, S(22), TRUE);
                ::MoveWindow(hPkgScope, paneX + lw, y, S(160), S(140), TRUE);
                y += S(38);
                ::MoveWindow(hPkgApply, paneX, y, fw + lw, S(34), TRUE);
            }
        }

        // View 1: Payloads
        ::MoveWindow(hPayGrid,    paneX, contentTop, paneW, mainH - S(44), TRUE);
        ::MoveWindow(hPayAddFile, paneX, contentTop + mainH - S(36), S(164), S(32), TRUE);
        ::MoveWindow(hPayAddDir,  paneX + S(172), contentTop + mainH - S(36), S(176), S(32), TRUE);
        ::MoveWindow(hPayRemove,  paneX + S(356), contentTop + mainH - S(36), S(148), S(32), TRUE);

        // View 2: Shortcuts & Registry
        {
            auto v = layoutFormPairs(VIEW_SHORTCUTS);
            const int gridH = mainH - S(120);
            ::MoveWindow(hScGrid, paneX, contentTop, paneW, gridH, TRUE);
            int y1 = contentTop + gridH + S(10);
            if (v.size() >= 13) {
                ::MoveWindow(v[1], paneX,          y1 + S(4), S(100), S(22), TRUE);
                ::MoveWindow(hScName, paneX + S(104), y1, S(160), S(26), TRUE);
                ::MoveWindow(v[3], paneX + S(274), y1 + S(4), S(80), S(22), TRUE);
                ::MoveWindow(hScTarget, paneX + S(356), y1, S(180), S(26), TRUE);
                ::MoveWindow(hScAddBtn, paneX + S(546), y1, S(130), S(28), TRUE);

                int y2 = y1 + S(38);
                ::MoveWindow(v[6],  paneX,          y2 + S(4), S(90), S(22), TRUE);
                ::MoveWindow(hRegKey, paneX + S(92), y2, S(190), S(26), TRUE);
                ::MoveWindow(v[8],  paneX + S(290), y2 + S(4), S(80), S(22), TRUE);
                ::MoveWindow(hRegName,paneX + S(372), y2, S(110), S(26), TRUE);
                ::MoveWindow(v[10], paneX + S(490), y2 + S(4), S(74), S(22), TRUE);
                ::MoveWindow(hRegVal, paneX + S(566), y2, S(120), S(26), TRUE);
                ::MoveWindow(hRegAddBtn, paneX + S(694), y2, S(140), S(28), TRUE);
            }
        }

        // View 3: Wizard UI + Right-Side Live Painted Wizard Preview Canvas!
        {
            auto v = layoutFormPairs(VIEW_WIZARD_UI);
            const int formW = S(390);
            int y = contentTop + S(4);
            const int lw = S(136);
            if (v.size() >= 15) {
                ::MoveWindow(v[0], paneX, y + S(4), lw, S(22), TRUE);
                ::MoveWindow(hUiLayout, paneX + lw, y, formW - lw, S(120), TRUE);
                y += S(34);
                auto row = [&](HWND lbl, HWND ed, int rsv = 0) {
                    ::MoveWindow(lbl, paneX, y + S(4), lw, S(22), TRUE);
                    ::MoveWindow(ed,  paneX + lw, y, formW - lw - rsv, S(26), TRUE);
                    y += S(34);
                };
                row(v[2], hUiPrim);
                row(v[4], hUiSec);
                row(v[6], hUiInstBtn);
                row(v[8], hUiIcon, S(86));
                ::MoveWindow(hUiIconBtn, paneX + formW - S(80), y - S(34), S(80), S(26), TRUE);
                row(v[11], hUiArt, S(86));
                ::MoveWindow(hUiArtBtn,  paneX + formW - S(80), y - S(34), S(80), S(26), TRUE);
                ::MoveWindow(hUiApply, paneX, y + S(4), formW, S(34), TRUE);
            }
            previewCanvasRc = RECT{paneX + formW + S(20), contentTop + S(4), paneX + paneW, contentTop + std::min(mainH, S(310))};
        }

        // View 4: Authenticode Signing
        {
            auto v = layoutFormPairs(VIEW_SIGNING);
            int y = contentTop + S(6);
            const int lw = S(190), fw = std::min(S(460), paneW - lw - S(16));
            if (v.size() >= 10) {
                ::MoveWindow(hSigEnable, paneX, y, lw + fw, S(28), TRUE);
                y += S(38);
                auto row = [&](HWND lbl, HWND ed) {
                    ::MoveWindow(lbl, paneX, y + S(4), lw, S(22), TRUE);
                    ::MoveWindow(ed,  paneX + lw, y, fw, S(26), TRUE);
                    y += S(34);
                };
                row(v[1], hSigThumb);
                row(v[3], hSigTs);
                row(v[5], hSigDlib);
                row(v[7], hSigMeta);
                ::MoveWindow(hSigApply, paneX, y + S(6), lw + fw, S(34), TRUE);
            }
        }

        // View 5: Enterprise .MST & .MSP
        {
            auto v = layoutFormPairs(VIEW_MST_MSP);
            int y = contentTop + S(12);
            const int lw = S(190), fw = std::min(S(440), paneW - lw - S(104));
            if (v.size() >= 8) {
                ::MoveWindow(v[0], paneX, y + S(4), lw, S(22), TRUE);
                ::MoveWindow(hEntBase, paneX + lw, y, fw, S(26), TRUE);
                ::MoveWindow(hEntBaseBtn, paneX + lw + fw + S(8), y, S(88), S(26), TRUE);
                y += S(40);
                ::MoveWindow(v[3], paneX, y + S(4), lw, S(22), TRUE);
                ::MoveWindow(hEntTarg, paneX + lw, y, fw, S(26), TRUE);
                ::MoveWindow(hEntTargBtn, paneX + lw + fw + S(8), y, S(88), S(26), TRUE);
                y += S(48);
                ::MoveWindow(hEntGenMst, paneX, y, S(310), S(36), TRUE);
                ::MoveWindow(hEntGenMsp, paneX + S(322), y, S(310), S(36), TRUE);
            }
        }

        // View 6: Full Monospace `.rsip` Source
        ::MoveWindow(hSourceEdit, paneX, contentTop, paneW, mainH, TRUE);

        UpdateActiveViewVisibility();
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    void UpdateActiveViewVisibility() {
        for (const auto& tc : taggedControls) {
            if (tc.viewIndex == -1) {
                ::ShowWindow(tc.hwnd, SW_SHOW);
            } else {
                ::ShowWindow(tc.hwnd, (tc.viewIndex == activeView) ? SW_SHOW : SW_HIDE);
            }
        }
    }

    std::string ReadSourceEditorUtf8() const {
        const int cch = ::GetWindowTextLengthW(hSourceEdit);
        if (cch <= 0) return {};
        std::wstring ws(static_cast<std::size_t>(cch) + 1, L'\0');
        ::GetWindowTextW(hSourceEdit, ws.data(), cch + 1);
        ws.resize(static_cast<std::size_t>(cch));
        return rsw::WideToUtf8(ws);
    }

    void SyncSourceEditorFromAst() {
        astDoc.useCrlf = true;
        ::SetWindowTextW(hSourceEdit, rsw::Utf8ToWide(astDoc.Serialize()).c_str());
    }

    void SyncAllViewsFromAst() {
        rsw::RswManifestDocument effectiveDoc = astDoc;
        std::string incErr;
        rsw::ResolveManifestIncludesAndVars(
            astDoc,
            hasActiveSuite ? activeSuite.variables : std::map<std::string, std::string>{},
            effectiveDoc,
            incErr);

        ::SetWindowTextW(hPkgName, rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "Name")).c_str());
        ::SetWindowTextW(hPkgTag,  rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "Tagline")).c_str());
        ::SetWindowTextW(hPkgWeb,  rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "WebsiteLabel")).c_str());
        ::SetWindowTextW(hPkgVer,  rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "Version")).c_str());
        ::SetWindowTextW(hPkgPub,  rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "Manufacturer")).c_str());
        ::SetWindowTextW(hPkgGuid, rsw::Utf8ToWide(effectiveDoc.GetString("Package", "", "UpgradeCode")).c_str());
        ::SetWindowTextW(hPkgSub,  rsw::Utf8ToWide(effectiveDoc.GetString("Install", "", "DefaultSubdir")).c_str());
        ::SetWindowTextW(hPkgExe,  rsw::Utf8ToWide(effectiveDoc.GetString("Install", "", "MainExecutable")).c_str());
        const bool isUser = rsw::EqualsIgnoreCase(effectiveDoc.GetString("Package", "", "Scope", "machine"), "user");
        ::SendMessageW(hPkgScope, CB_SETCURSEL, isUser ? 1 : 0, 0);

        ListView_DeleteAllItems(hPayGrid);
        int pRow = 0;
        for (const auto& pid : effectiveDoc.GetNamedSectionIds("Payload")) {
            std::wstring wId  = rsw::Utf8ToWide(pid);
            std::wstring wSrc = rsw::Utf8ToWide(effectiveDoc.GetString("Payload", pid, "Source"));
            std::wstring wSub = rsw::Utf8ToWide(effectiveDoc.GetString("Payload", pid, "TargetSubdir"));
            std::wstring wFt  = rsw::Utf8ToWide(effectiveDoc.GetString("Payload", pid, "Feature", "Main"));
            LVITEMW it{};
            it.mask = LVIF_TEXT;
            it.iItem = pRow;
            it.pszText = wId.data();
            ListView_InsertItem(hPayGrid, &it);
            ListView_SetItemText(hPayGrid, pRow, 1, wSrc.data());
            ListView_SetItemText(hPayGrid, pRow, 2, wSub.data());
            ListView_SetItemText(hPayGrid, pRow, 3, wFt.data());
            ++pRow;
        }

        ListView_DeleteAllItems(hScGrid);
        int sRow = 0;
        for (const auto& sid : effectiveDoc.GetNamedSectionIds("Shortcut")) {
            std::wstring k = L"Shortcut";
            std::wstring wId = rsw::Utf8ToWide(sid);
            std::wstring wNm = rsw::Utf8ToWide(effectiveDoc.GetString("Shortcut", sid, "Name"));
            std::wstring wTg = rsw::Utf8ToWide(effectiveDoc.GetString("Shortcut", sid, "TargetFile"));
            LVITEMW it{LVIF_TEXT, sRow, 0, 0, 0, k.data(), 0, 0, 0, 0};
            ListView_InsertItem(hScGrid, &it);
            ListView_SetItemText(hScGrid, sRow, 1, wId.data());
            ListView_SetItemText(hScGrid, sRow, 2, wNm.data());
            ListView_SetItemText(hScGrid, sRow, 3, wTg.data());
            ++sRow;
        }
        for (const auto& rid : effectiveDoc.GetNamedSectionIds("Registry")) {
            std::wstring k = L"Registry";
            std::wstring wId = rsw::Utf8ToWide(rid);
            std::wstring wKy = rsw::Utf8ToWide(effectiveDoc.GetString("Registry", rid, "Key") + "\\" + effectiveDoc.GetString("Registry", rid, "Name"));
            std::wstring wVl = rsw::Utf8ToWide(effectiveDoc.GetString("Registry", rid, "Value"));
            LVITEMW it{LVIF_TEXT, sRow, 0, 0, 0, k.data(), 0, 0, 0, 0};
            ListView_InsertItem(hScGrid, &it);
            ListView_SetItemText(hScGrid, sRow, 1, wId.data());
            ListView_SetItemText(hScGrid, sRow, 2, wKy.data());
            ListView_SetItemText(hScGrid, sRow, 3, wVl.data());
            ++sRow;
        }

        const bool isAuto = rsw::EqualsIgnoreCase(effectiveDoc.GetString("UI", "", "WizardLayout", "standard"), "autorun16x9");
        ::SendMessageW(hUiLayout, CB_SETCURSEL, isAuto ? 1 : 0, 0);
        ::SetWindowTextW(hUiPrim,    rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "AccentPrimaryHex", "#2563EB")).c_str());
        ::SetWindowTextW(hUiSec,     rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "AccentSecondaryHex", "#FBBF24")).c_str());
        ::SetWindowTextW(hUiInstBtn, rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "InstallButtonText", "&Install")).c_str());
        ::SetWindowTextW(hUiIcon,    rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "IconFile")).c_str());
        ::SetWindowTextW(hUiArt,     rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "ArtworkPng")).c_str());

        ::SendMessageW(hSigEnable, BM_SETCHECK, effectiveDoc.GetBool("Signing", "", "Enabled", false) ? BST_CHECKED : BST_UNCHECKED, 0);
        ::SetWindowTextW(hSigThumb, rsw::Utf8ToWide(effectiveDoc.GetString("Signing", "", "Thumbprint")).c_str());
        ::SetWindowTextW(hSigTs,    rsw::Utf8ToWide(effectiveDoc.GetString("Signing", "", "TimestampUrl", "http://timestamp.acs.microsoft.com")).c_str());
        ::SetWindowTextW(hSigDlib,  rsw::Utf8ToWide(effectiveDoc.GetString("Signing", "", "DlibPath")).c_str());
        ::SetWindowTextW(hSigMeta,  rsw::Utf8ToWide(effectiveDoc.GetString("Signing", "", "MetadataJson")).c_str());
    }

    void LoadProjectText(const std::string& utf8, const std::filesystem::path& path, bool keepSuite = false) {
        if (!keepSuite) {
            hasActiveSuite = false;
            activeSuite = {};
            ::SendMessageW(hSuiteCombo, CB_RESETCONTENT, 0, 0);
            ::SendMessageW(hSuiteCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"[Standalone .rsip Mode]"));
            ::SendMessageW(hSuiteCombo, CB_SETCURSEL, 0, 0);
        }
        currentFilePath = path;
        astDoc = rsw::ParseManifestText(utf8, path);
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        std::wstring title = L"RaggieSoft Installer Studio — ";
        if (hasActiveSuite) {
            title += rsw::Utf8ToWide(activeSuite.name) + L" / ";
        }
        title += path.empty() ? L"[Untitled.rsip]" : path.filename().wstring();
        ::SetWindowTextW(hWnd, title.c_str());
    }

    void OpenProjectOrSuiteFile(const std::filesystem::path& path) {
        rsw::RswManifestDocument d;
        std::string err;
        if (!rsw::LoadManifestFile(path, d, err)) {
            AppendConsole(L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n");
            return;
        }
        if (rsw::IsSuiteFilePath(path)) {
            rsw::RswSuiteWorkspace sw;
            if (!rsw::ParseSuiteWorkspace(d, sw, err)) {
                AppendConsole(L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n");
                return;
            }
            activeSuite = std::move(sw);
            hasActiveSuite = true;
            ::SendMessageW(hSuiteCombo, CB_RESETCONTENT, 0, 0);
            for (const auto& pe : activeSuite.projects) {
                std::wstring item = rsw::Utf8ToWide("[" + pe.category + "] " + pe.id);
                ::SendMessageW(hSuiteCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
            }
            ::SendMessageW(hSuiteCombo, CB_SETCURSEL, 0, 0);
            AppendConsole(L"[SUITE] Opened '" + rsw::Utf8ToWide(activeSuite.name) + L"' (" +
                          std::to_wstring(activeSuite.projects.size()) + L" member .rsip projects).\r\n");
            if (!activeSuite.projects.empty()) {
                rsw::RswManifestDocument firstProj;
                if (rsw::LoadManifestFile(activeSuite.projects[0].resolvedPath, firstProj, err)) {
                    LoadProjectText(firstProj.Serialize(), activeSuite.projects[0].resolvedPath, true);
                }
            }
            return;
        }
        LoadProjectText(d.Serialize(), path, false);
    }

    static std::string ReadCtlUtf8(HWND h) {
        wchar_t buf[1024]{};
        ::GetWindowTextW(h, buf, static_cast<int>(std::size(buf)));
        return rsw::WideToUtf8(buf);
    }

    void AppendConsole(const std::wstring& msg) {
        const int len = ::GetWindowTextLengthW(hConsoleEdit);
        ::SendMessageW(hConsoleEdit, EM_SETSEL, len, len);
        ::SendMessageW(hConsoleEdit, EM_REPLACESEL, FALSE, reinterpret_cast<LPARAM>(msg.c_str()));
    }

    void ApplyPackageViewToAst() {
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        astDoc.SetString("Package", "", "Name",           ReadCtlUtf8(hPkgName));
        astDoc.SetString("Package", "", "Tagline",        ReadCtlUtf8(hPkgTag));
        const std::string web = ReadCtlUtf8(hPkgWeb);
        if (!web.empty()) astDoc.SetString("Package", "", "WebsiteLabel", web);
        astDoc.SetString("Package", "", "Version",        ReadCtlUtf8(hPkgVer));
        astDoc.SetString("Package", "", "Manufacturer",   ReadCtlUtf8(hPkgPub));
        astDoc.SetString("Package", "", "UpgradeCode",    ReadCtlUtf8(hPkgGuid));
        astDoc.SetString("Package", "", "Scope",          (::SendMessageW(hPkgScope, CB_GETCURSEL, 0, 0) == 1) ? "user" : "machine");
        astDoc.SetString("Install", "", "DefaultSubdir",  ReadCtlUtf8(hPkgSub));
        astDoc.SetString("Install", "", "MainExecutable", ReadCtlUtf8(hPkgExe));
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        AppendConsole(L"[AST] Updated [Package] & [Install] fields while preserving 100% of comments & @includes.\r\n");
    }

    void ApplyUiViewToAst() {
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        astDoc.SetString("UI", "", "WizardLayout",      (::SendMessageW(hUiLayout, CB_GETCURSEL, 0, 0) == 1) ? "autorun16x9" : "standard");
        astDoc.SetString("UI", "", "AccentPrimaryHex",  ReadCtlUtf8(hUiPrim));
        astDoc.SetString("UI", "", "AccentSecondaryHex",ReadCtlUtf8(hUiSec));
        astDoc.SetString("UI", "", "InstallButtonText", ReadCtlUtf8(hUiInstBtn));
        const std::string ico = ReadCtlUtf8(hUiIcon);
        if (!ico.empty()) astDoc.SetString("UI", "", "IconFile", ico);
        const std::string art = ReadCtlUtf8(hUiArt);
        if (!art.empty()) astDoc.SetString("UI", "", "ArtworkPng", art);
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        ::InvalidateRect(hWnd, nullptr, TRUE);
        AppendConsole(L"[AST] Updated [UI] branding & refreshed live Setup Wizard preview.\r\n");
    }

    void ApplySigningViewToAst() {
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        const bool en = (::SendMessageW(hSigEnable, BM_GETCHECK, 0, 0) == BST_CHECKED);
        astDoc.SetString("Signing", "", "Enabled",      en ? "true" : "false");
        astDoc.SetString("Signing", "", "Thumbprint",   ReadCtlUtf8(hSigThumb));
        astDoc.SetString("Signing", "", "TimestampUrl", ReadCtlUtf8(hSigTs));
        astDoc.SetString("Signing", "", "DlibPath",     ReadCtlUtf8(hSigDlib));
        astDoc.SetString("Signing", "", "MetadataJson", ReadCtlUtf8(hSigMeta));
        SyncSourceEditorFromAst();
        AppendConsole(L"[AST] Updated [Signing] Authenticode settings.\r\n");
    }

    bool PickPathDialog(bool folderMode, const wchar_t* filter, std::wstring& outPath) {
        if (folderMode) {
            IFileOpenDialog* pDlg = nullptr;
            if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))) && pDlg) {
                DWORD opts = 0;
                pDlg->GetOptions(&opts);
                pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                if (SUCCEEDED(pDlg->Show(hWnd))) {
                    IShellItem* pItem = nullptr;
                    if (SUCCEEDED(pDlg->GetResult(&pItem)) && pItem) {
                        PWSTR psz = nullptr;
                        if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                            outPath = psz;
                            ::CoTaskMemFree(psz);
                            pItem->Release();
                            pDlg->Release();
                            return true;
                        }
                        pItem->Release();
                    }
                }
                pDlg->Release();
            }
            return false;
        }
        wchar_t buf[MAX_PATH * 2]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = hWnd;
        ofn.lpstrFilter = filter;
        ofn.lpstrFile   = buf;
        ofn.nMaxFile    = static_cast<DWORD>(std::size(buf));
        ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!::GetOpenFileNameW(&ofn)) return false;
        outPath = buf;
        return true;
    }

    void AddPayloadNode(bool pickDirectory) {
        std::wstring chosen;
        if (!PickPathDialog(pickDirectory, L"All Files (*.*)\0*.*\0", chosen)) return;
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        const int idx = static_cast<int>(astDoc.GetNamedSectionIds("Payload").size() + 1);
        const std::string id = (pickDirectory ? "Dir" : "File") + std::to_string(idx);
        astDoc.SetString("Payload", id, "Source", rsw::WideToUtf8(chosen));
        astDoc.SetString("Payload", id, "Feature", "Main");
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        AppendConsole(L"[AST] Added [Payload." + rsw::Utf8ToWide(id) + L"].\r\n");
    }

    void RemoveSelectedPayload() {
        const int sel = ListView_GetNextItem(hPayGrid, -1, LVNI_SELECTED);
        if (sel < 0) return;
        wchar_t idBuf[128]{};
        ListView_GetItemText(hPayGrid, sel, 0, idBuf, static_cast<int>(std::size(idBuf)));
        const std::string targetId = rsw::WideToUtf8(idBuf);

        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        std::vector<rsw::RswAstLine> kept;
        for (const auto& ln : astDoc.lines) {
            if ((ln.kind == rsw::AstLineKind::SectionHeader || ln.kind == rsw::AstLineKind::KeyValue) &&
                rsw::EqualsIgnoreCase(ln.sectionPrefix, "Payload") &&
                rsw::EqualsIgnoreCase(ln.sectionItemId, targetId)) {
                continue;
            }
            kept.push_back(ln);
        }
        astDoc.lines = std::move(kept);
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        AppendConsole(L"[AST] Removed [Payload." + std::wstring(idBuf) + L"].\r\n");
    }

    bool SaveCurrentProject() {
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        astDoc.useCrlf = true;
        if (currentFilePath.empty()) {
            wchar_t buf[MAX_PATH * 2] = L"project.rsip";
            OPENFILENAMEW ofn{};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hWnd;
            ofn.lpstrFilter = L"RaggieSoft Installer Project (*.rsip)\0*.rsip\0All Files (*.*)\0*.*\0";
            ofn.lpstrFile   = buf;
            ofn.nMaxFile    = static_cast<DWORD>(std::size(buf));
            ofn.lpstrDefExt = L"rsip";
            ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
            if (!::GetSaveFileNameW(&ofn)) return false;
            currentFilePath = buf;
            astDoc.sourcePath = currentFilePath;
        }
        std::string err;
        if (!rsw::SaveManifestFile(currentFilePath, astDoc, err)) {
            AppendConsole(L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n");
            return false;
        }
        AppendConsole(L"[OK] Saved lossless .rsip manifest: " + currentFilePath.wstring() + L"\r\n");
        return true;
    }

    std::string SelectedArch() const {
        return (::SendMessageW(hArchCombo, CB_GETCURSEL, 0, 0) == 1) ? "x64" : "arm64";
    }

    void RunValidation() {
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        rsw::RswManifestDocument resolvedDoc;
        std::string incErr;
        ::SetWindowTextW(hConsoleEdit, L"");
        if (!rsw::ResolveManifestIncludesAndVars(
                astDoc,
                hasActiveSuite ? activeSuite.variables : std::map<std::string, std::string>{},
                resolvedDoc,
                incErr)) {
            AppendConsole(L"[ERROR] " + rsw::Utf8ToWide(incErr) + L"\r\n");
            return;
        }
        const auto rep = rsw::ValidateManifestDocument(resolvedDoc, SelectedArch());
        if (rep.IsOk()) {
            AppendConsole(L"[PASS] 0 errors — Project is valid for '" + rsw::Utf8ToWide(rep.resolvedArchitecture) + L"'.\r\n");
        } else {
            for (const auto& d : rep.diagnostics) {
                AppendConsole(L"[" + rsw::Utf8ToWide(d.code) + L" Line " + std::to_wstring(d.lineNumber) + L"] " +
                              rsw::Utf8ToWide(d.message) + L"\r\n");
            }
        }
    }

    void InvokeInstBuild(bool mergeModule) {
        if (!SaveCurrentProject()) return;
        const auto cliExe = ideExeDir / L"instbuild.exe";
        if (!std::filesystem::exists(cliExe)) {
            AppendConsole(L"[ERROR] instbuild.exe not found next to InstallerStudio.exe.\r\n");
            return;
        }
        const std::wstring arch = rsw::Utf8ToWide(SelectedArch());
        std::wstring cmd = L"\"" + cliExe.wstring() + L"\" ";
        if (mergeModule) {
            const auto msmOut = currentFilePath.parent_path() / L"out" /
                                (currentFilePath.stem().wstring() + L"-" + arch + L".msm");
            cmd += L"--emit-msm \"" + currentFilePath.wstring() + L"\" \"" + msmOut.wstring() + L"\" --arch=" + arch;
        } else {
            cmd += L"\"" + currentFilePath.wstring() + L"\" --arch=" + arch;
        }
        ::SetWindowTextW(hConsoleEdit, (L"> " + cmd + L"\r\n").c_str());
        ::UpdateWindow(hConsoleEdit);
        DWORD code = 1;
        AppendConsole(RunChildCapture(cmd, code));
    }

    void ExportVpatHtml() {
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        rsw::RswManifestDocument resolvedDoc;
        std::string err;
        if (!rsw::ResolveManifestIncludesAndVars(
                astDoc,
                hasActiveSuite ? activeSuite.variables : std::map<std::string, std::string>{},
                resolvedDoc,
                err)) {
            AppendConsole(L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n");
            return;
        }
        const auto outDir = currentFilePath.empty() ? (ideExeDir / L"vpat") : (currentFilePath.parent_path() / L"out");
        std::filesystem::path htmlPath;
        if (!rsw::EmitProjectInstallerVpatHtml(resolvedDoc, outDir, htmlPath, err)) {
            AppendConsole(L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n");
            return;
        }
        AppendConsole(L"[OK] Exported WCAG 2.2 AA & Section 508 VPAT 2.5 Report: " + htmlPath.wstring() + L"\r\n");
    }

    void PaintIdeSurface(HDC hdc) {
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        ::FillRect(hdc, &rc, theme.hbrWindow);
        RECT bannerRc{0, 0, rc.right, S(50)};
        rsw::ui::FillHorizontalGradient(hdc, bannerRc, theme.bannerNavyLeft, theme.bannerNavyRight);
        RECT accentRc{0, bannerRc.bottom, rc.right, bannerRc.bottom + S(4)};
        rsw::ui::FillHorizontalGradient(hdc, accentRc, theme.accentPrimary, theme.accentSecondary);

        ::SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ oldF = ::SelectObject(hdc, hFontBanner);
        ::SetTextColor(hdc, theme.bannerTitleText);
        RECT tRc{S(16), S(10), rc.right - S(16), S(42)};
        ::DrawTextW(hdc, L"RaggieSoft Installer Studio  •  Install Without Limits (.rsip / .rsis -> MSI • MSM • MST • MSP • Setup.exe)",
                    -1, &tRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        if (activeView == VIEW_WIZARD_UI && previewCanvasRc.right > previewCanvasRc.left + S(120)) {
            RECT pv = previewCanvasRc;
            ::FillRect(hdc, &pv, theme.hbrCard);
            const COLORREF prim = rsw::ui::ParseHexColor(ReadCtlUtf8(hUiPrim), RGB(37, 99, 235));
            const COLORREF sec  = rsw::ui::ParseHexColor(ReadCtlUtf8(hUiSec),  RGB(251, 191, 36));
            const bool isAuto   = (::SendMessageW(hUiLayout, CB_GETCURSEL, 0, 0) == 1);

            const int headH = isAuto ? ((pv.bottom - pv.top) * 55 / 100) : S(56);
            RECT pvHead{pv.left, pv.top, pv.right, pv.top + headH};
            rsw::ui::FillHorizontalGradient(hdc, pvHead, RGB(11, 19, 43), RGB(20, 38, 78));
            RECT pvAcc{pv.left, pvHead.bottom, pv.right, pvHead.bottom + S(4)};
            rsw::ui::FillHorizontalGradient(hdc, pvAcc, prim, sec);

            ::SelectObject(hdc, hFontHead);
            ::SetTextColor(hdc, RGB(248, 250, 252));
            std::wstring prodTitle = rsw::Utf8ToWide(astDoc.GetString("Package", "", "Name", "My Application")) + L" Setup";
            RECT ptRc{pv.left + S(14), pv.top + S(12), pv.right - S(14), pvHead.bottom};
            ::DrawTextW(hdc, prodTitle.c_str(), -1, &ptRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

            ::SelectObject(hdc, hFontUi);
            ::SetTextColor(hdc, theme.textPrimary);
            RECT bodyRc{pv.left + S(14), pvAcc.bottom + S(12), pv.right - S(14), pv.bottom - S(42)};
            const wchar_t* pvCopy = isAuto
                ? L"Live Preview: 640x480 16:9 Autorun Micro-Installer\r\nOne-click install + Change Folder"
                : L"(o) Express Setup (Recommended)\r\n( ) Compact Setup\r\n( ) Custom Setup";
            ::DrawTextW(hdc, pvCopy, -1, &bodyRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

            RECT ctaRc{pv.right - S(104), pv.bottom - S(34), pv.right - S(14), pv.bottom - S(8)};
            HBRUSH hCta = ::CreateSolidBrush(prim);
            ::FillRect(hdc, &ctaRc, hCta);
            ::DeleteObject(hCta);
            ::SetTextColor(hdc, RGB(255, 255, 255));
            std::wstring btnCap = rsw::Utf8ToWide(ReadCtlUtf8(hUiInstBtn));
            ::DrawTextW(hdc, btnCap.c_str(), -1, &ctaRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        ::SelectObject(hdc, oldF);
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        InstallerStudioIde* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<InstallerStudioIde*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hWnd = hWnd;
        } else {
            self = reinterpret_cast<InstallerStudioIde*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        }
        if (!self) return ::DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
            case WM_CREATE:
                self->CreateControls();
                rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                self->LayoutChildren();
                return 0;
            case WM_SIZE:
                self->LayoutChildren();
                return 0;
            case WM_DPICHANGED: {
                const auto* r = reinterpret_cast<const RECT*>(lParam);
                if (r) ::SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
                self->LayoutChildren();
                return 0;
            }
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdc = ::BeginPaint(hWnd, &ps);
                self->PaintIdeSurface(hdc);
                ::EndPaint(hWnd, &ps);
                return 0;
            }
            case WM_CTLCOLORSTATIC:
            case WM_CTLCOLORBTN: {
                HDC dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, self->theme.textPrimary);
                ::SetBkColor(dc, self->theme.windowBg);
                return reinterpret_cast<LRESULT>(self->theme.hbrWindow);
            }
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORLISTBOX: {
                HDC dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, self->theme.textPrimary);
                ::SetBkColor(dc, self->theme.cardBg);
                return reinterpret_cast<LRESULT>(self->theme.hbrCard);
            }
            case WM_COMMAND: {
                const int id   = LOWORD(wParam);
                const int code = HIWORD(wParam);
                if (id == IDC_NAV_VIEWS && code == LBN_SELCHANGE) {
                    const LRESULT sel = ::SendMessageW(self->hNavViews, LB_GETCURSEL, 0, 0);
                    if (sel != LB_ERR) {
                        self->activeView = static_cast<int>(sel);
                        self->UpdateActiveViewVisibility();
                        ::InvalidateRect(hWnd, nullptr, TRUE);
                    }
                    return 0;
                }
                if (id == IDC_SUITE_COMBO && code == CBN_SELCHANGE && self->hasActiveSuite) {
                    const LRESULT sel = ::SendMessageW(self->hSuiteCombo, CB_GETCURSEL, 0, 0);
                    if (sel != CB_ERR && static_cast<std::size_t>(sel) < self->activeSuite.projects.size()) {
                        const auto& target = self->activeSuite.projects[static_cast<std::size_t>(sel)];
                        rsw::RswManifestDocument d;
                        std::string err;
                        if (rsw::LoadManifestFile(target.resolvedPath, d, err)) {
                            self->LoadProjectText(d.Serialize(), target.resolvedPath, true);
                        }
                    }
                    return 0;
                }
                switch (id) {
                    case IDM_FILE_NEW:
                    case IDC_TB_NEW:
                        self->LoadProjectText(BuildStarterRsipTemplate(), {});
                        return 0;
                    case IDM_FILE_OPEN:
                    case IDC_TB_OPEN: {
                        std::wstring p;
                        if (self->PickPathDialog(
                                false,
                                L"Installer Studio Projects & Suites (*.rsip;*.rsis;*.rssp;*.rsw)\0*.rsip;*.rsis;*.rssp;*.rssw;*.rsw\0All Files\0*.*\0",
                                p)) {
                            self->OpenProjectOrSuiteFile(p);
                        }
                        return 0;
                    }
                    case IDM_FILE_SAVE:
                    case IDC_TB_SAVE:
                        self->SaveCurrentProject();
                        return 0;
                    case IDM_FILE_EXIT:
                        ::DestroyWindow(hWnd);
                        return 0;
                    case IDC_PKG_NEW_GUID:
                        ::SetWindowTextW(self->hPkgGuid, rsw::Utf8ToWide(rsw::GenerateRandomMsiGuid()).c_str());
                        return 0;
                    case IDC_PKG_APPLY:
                        self->ApplyPackageViewToAst();
                        return 0;
                    case IDC_PAY_ADD_FILE:
                        self->AddPayloadNode(false);
                        return 0;
                    case IDC_PAY_ADD_FOLDER:
                        self->AddPayloadNode(true);
                        return 0;
                    case IDC_PAY_REMOVE:
                        self->RemoveSelectedPayload();
                        return 0;
                    case IDC_SC_ADD_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        const std::string sid = "Link" + std::to_string(self->astDoc.GetNamedSectionIds("Shortcut").size() + 1);
                        self->astDoc.SetString("Shortcut", sid, "Name",       ReadCtlUtf8(self->hScName));
                        self->astDoc.SetString("Shortcut", sid, "TargetFile", ReadCtlUtf8(self->hScTarget));
                        self->astDoc.SetString("Shortcut", sid, "Location",   "StartMenu");
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        return 0;
                    }
                    case IDC_REG_ADD_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        const std::string rid = "Reg" + std::to_string(self->astDoc.GetNamedSectionIds("Registry").size() + 1);
                        self->astDoc.SetString("Registry", rid, "Key",   ReadCtlUtf8(self->hRegKey));
                        self->astDoc.SetString("Registry", rid, "Name",  ReadCtlUtf8(self->hRegName));
                        self->astDoc.SetString("Registry", rid, "Value", ReadCtlUtf8(self->hRegVal));
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        return 0;
                    }
                    case IDC_UI_ICON_BROWSE: {
                        std::wstring p;
                        if (self->PickPathDialog(false, L"Icon Files (*.ico)\0*.ico\0", p)) ::SetWindowTextW(self->hUiIcon, p.c_str());
                        return 0;
                    }
                    case IDC_UI_ART_BROWSE: {
                        std::wstring p;
                        if (self->PickPathDialog(false, L"Artwork Images (*.png;*.jpg;*.jpeg)\0*.png;*.jpg;*.jpeg\0", p)) ::SetWindowTextW(self->hUiArt, p.c_str());
                        return 0;
                    }
                    case IDC_UI_APPLY:
                        self->ApplyUiViewToAst();
                        return 0;
                    case IDC_SIG_APPLY:
                        self->ApplySigningViewToAst();
                        return 0;
                    case IDC_ENT_BASE_BROWSE: {
                        std::wstring p;
                        if (self->PickPathDialog(false, L"MSI Databases (*.msi)\0*.msi\0", p)) ::SetWindowTextW(self->hEntBase, p.c_str());
                        return 0;
                    }
                    case IDC_ENT_TARG_BROWSE: {
                        std::wstring p;
                        if (self->PickPathDialog(false, L"MSI Databases (*.msi)\0*.msi\0", p)) ::SetWindowTextW(self->hEntTarg, p.c_str());
                        return 0;
                    }
                    case IDC_ENT_GEN_MST:
                    case IDC_ENT_GEN_MSP: {
                        const bool wantMst = (id == IDC_ENT_GEN_MST);
                        const std::filesystem::path baseP = rsw::Utf8ToWide(ReadCtlUtf8(self->hEntBase));
                        const std::filesystem::path targP = rsw::Utf8ToWide(ReadCtlUtf8(self->hEntTarg));
                        const auto outP = targP.parent_path() / (targP.stem().wstring() + (wantMst ? L".mst" : L".msp"));
                        std::string err;
                        const bool ok = wantMst
                            ? rsw::GenerateMsiTransform(baseP, targP, outP, err)
                            : rsw::GenerateMsiPatchPackage(baseP, targP, outP, err);
                        self->AppendConsole(ok ? (L"[OK] Emitted " + outP.wstring() + L"\r\n")
                                               : (L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n"));
                        return 0;
                    }
                    case IDM_BUILD_VALIDATE:
                    case IDC_TB_VALIDATE:
                        self->RunValidation();
                        return 0;
                    case IDM_BUILD_MSI:
                    case IDC_TB_BUILD_MSI:
                        self->InvokeInstBuild(false);
                        return 0;
                    case IDM_BUILD_MSM:
                    case IDC_TB_BUILD_MSM:
                        self->InvokeInstBuild(true);
                        return 0;
                    case IDM_BUILD_VPAT:
                    case IDC_TB_EXPORT_VPAT:
                        self->ExportVpatHtml();
                        return 0;
                    case IDM_BUILD_EMIT_AI: {
                        const auto outDir = self->currentFilePath.empty() ? (self->ideExeDir / L"specs") : (self->currentFilePath.parent_path() / L"specs");
                        std::string err;
                        if (rsw::EmitAiSpecifications(outDir, err)) self->AppendConsole(L"[OK] Emitted llms.txt & SKILL.md.\r\n");
                        return 0;
                    }
                    case IDM_LAUNCH_MSIPRISM:
                    case IDC_TB_MSIPRISM: {
                        const auto prismExe = self->ideExeDir / L"msiprism.exe";
                        ::ShellExecuteW(hWnd, L"open", prismExe.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        return 0;
                    }
                    case IDM_OPEN_OUTDIR: {
                        const auto outDir = self->currentFilePath.empty() ? self->ideExeDir : (self->currentFilePath.parent_path() / L"out");
                        ::ShellExecuteW(hWnd, L"open", outDir.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        return 0;
                    }
                }
                break;
            }
            case WM_DESTROY:
                ::PostQuitMessage(0);
                return 0;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (argv) {
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        ::LocalFree(argv);
    }

    // Headless CI smoke test verifying AST round-trip, `@include` resolution, comment retention, & VPAT export!
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--headless-smoke-test") {
            if (i + 1 >= args.size()) return 2;
            rsw::RswManifestDocument doc;
            std::string err;
            if (!rsw::LoadManifestFile(args[i + 1], doc, err)) return 1;
            const std::string origVer = doc.GetString("Package", "", "Version");
            doc.SetString("Package", "", "Version", "1.2.3");
            doc.SetString("UI", "", "AccentPrimaryHex", "#0EA5E9");
            const std::string roundTrip = doc.Serialize();
            if (roundTrip.find(';') == std::string::npos) return 3; // Comments MUST survive!
            doc.SetString("Package", "", "Version", origVer);

            rsw::RswManifestDocument resolved;
            if (!rsw::ResolveManifestIncludesAndVars(doc, {}, resolved, err)) return 6;

            std::error_code ec;
            const auto tmpVpatDir = std::filesystem::temp_directory_path(ec) / L"rsip-ide-vpat-smoke";
            std::filesystem::path vpatHtml;
            if (!rsw::EmitProjectInstallerVpatHtml(resolved, tmpVpatDir, vpatHtml, err) ||
                !std::filesystem::exists(vpatHtml)) {
                return 5;
            }
            std::filesystem::remove_all(tmpVpatDir, ec);

            const auto rep = rsw::ValidateManifestDocument(resolved, "arm64");
            return rep.IsOk() ? 0 : 4;
        }
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    ::InitCommonControlsEx(&icc);

    InstallerStudioIde ide{};
    ide.theme.Refresh();

    wchar_t exeBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, exeBuf, static_cast<DWORD>(std::size(exeBuf)));
    ide.ideExeDir = std::filesystem::path(exeBuf).parent_path();

    constexpr const wchar_t* kCls = L"RaggieSoftInstallerStudioIdeWindow";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &InstallerStudioIde::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kCls;
    ::RegisterClassExW(&wc);

    const UINT dpi = ::GetDpiForSystem();
    const int winW = rsw::ui::ScalePx(1260, dpi, ide.theme.textScalePercent);
    const int winH = rsw::ui::ScalePx(760,  dpi, ide.theme.textScalePercent);
    const int posX = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - winW) / 2);
    const int posY = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - winH) / 2);

    HMENU hMenu = ide.BuildMenuBar();
    HWND hWnd = ::CreateWindowExW(
        0, kCls, L"RaggieSoft Installer Studio",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        posX, posY, winW, winH,
        nullptr, hMenu, hInstance, &ide);
    if (!hWnd) {
        ::CoUninitialize();
        return 1;
    }

    if (!args.empty() && std::filesystem::exists(args.front())) {
        ide.OpenProjectOrSuiteFile(args.front());
    } else {
        ide.LoadProjectText(BuildStarterRsipTemplate(), {});
    }
    ide.AppendConsole(L"RaggieSoft Installer Studio ready. Open a standalone .rsip project or .rsis suite, press F7 to validate, or press F5 to build MSI + Setup.exe.\r\n");

    ACCEL accels[] = {
        {FCONTROL | FVIRTKEY, 'N',   IDM_FILE_NEW},
        {FCONTROL | FVIRTKEY, 'O',   IDM_FILE_OPEN},
        {FCONTROL | FVIRTKEY, 'S',   IDM_FILE_SAVE},
        {FVIRTKEY,            VK_F7, IDM_BUILD_VALIDATE},
        {FVIRTKEY,            VK_F5, IDM_BUILD_MSI},
        {FVIRTKEY,            VK_F6, IDM_BUILD_MSM}
    };
    HACCEL hAccel = ::CreateAcceleratorTableW(accels, static_cast<int>(std::size(accels)));

    ::ShowWindow(hWnd, nCmdShow);
    ::UpdateWindow(hWnd);

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!::TranslateAcceleratorW(hWnd, hAccel, &msg) && !::IsDialogMessageW(hWnd, &msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    if (hAccel) ::DestroyAcceleratorTable(hAccel);
    ::CoUninitialize();
    return 0;
}
