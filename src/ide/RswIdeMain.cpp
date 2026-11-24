// ================================================================================
// FILE: src/ide/RswIdeMain.cpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `InstallerStudio.exe` ("RaggieSoft Installer Studio")?
//     `InstallerStudio.exe` is our **9-View Enterprise Win32 C++20 Visual IDE**
//     and **4-Wizard AeroWizard Suite** for authoring standalone `.rsip` projects,
//     `@include` `.rsii` dialog/component presets, and multi-project `.rsis` suites!
//     Compiled directly with `cl.exe` + the stock Windows 11 SDK (`/MT` static
//     binary, zero .NET, zero Electron, zero Delphi VCL bloat), it provides:
//       1. **Package & Identity**          : Product metadata, GUIDs, & scope.
//       2. **Destination Files & Folders** : InstallShield-style `SysTreeView32`
//          Destination Computer folder tree (`INSTALLDIR`, `CommonFiles64`,
//          `ProgramData`, `AppData`, `LocalAppData`, `StartMenu`, `Desktop`,
//          `System64`, `Fonts` + custom `CreateFolder` subfolders) paired with a
//          filtered File / Component / KeyPath `SysListView32`!
//       3. **Setup Design (Components)**   : Hierarchical `Features -> Sub-Features
//          -> Components -> KeyPaths` (`File`, `Registry` per ICE38, or `Folder`
//          per ICE18) enforcing Microsoft's Official Component Rules!
//       4. **File Associations & Verbs**   : Visual `[FileAssociation.*]` table &
//          editor (`HKMU\Software\Classes` clean mode + Advertised MSI SQL tables).
//       5. **Shortcuts & Registry**        : Visual `[Shortcut.*]` & `[Registry.*]`.
//       6. **C++ Wizard Designer**         : Live GDI+ Designer for `SetupWizard.exe`
//          (`standard` Glass, `aerowizard` AeroWizard, & `autorun16x9`).
//       7. **MSI.DLL Dialog Designer**     : Dedicated `370x270` DLU visual form
//          designer for Windows Installer `Dialog` & `Control` SQL tables (`Express
//          vs. Custom` workflow + custom `[MsiDialog.*]` & `[MsiControl.*]`)!
//       8. **Signing, .MST & .MSP**        : Dual-layer Authenticode + GPO `.mst`
//          transforms & `.msp` patches.
//       9. **.RSIP Source (Lossless AST)** : 100% comment- & `@include`-preserving
//          monospace manifest editor!
// ================================================================================

#include "../common/RswGlassRenderer.hpp"
#include "../common/RswCryptoAndPe.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswSuiteWorkspace.hpp"
#include "../common/RswVersion.hpp"
#include "../compiler/RswAiSpecEmitter.hpp"
#include "../compiler/RswBootstrapperPackager.hpp"
#include "../compiler/RswExtraPackages.hpp"
#include "../compiler/RswValidator.hpp"
#include "../compiler/RswVpatEmitter.hpp"
#include "RswIdeAeroWizards.hpp"

#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

enum StudioView : int {
    VIEW_PACKAGE      = 0,
    VIEW_DEST_FOLDERS = 1,
    VIEW_SETUP_DESIGN = 2,
    VIEW_FILE_ASSOC   = 3,
    VIEW_SHORTCUTS    = 4,
    VIEW_CPP_WIZARD   = 5,
    VIEW_MSI_DIALOGS  = 6,
    VIEW_SIGN_REL     = 7,
    VIEW_SOURCE       = 8
};

enum IdeId : int {
    // Top Menu Commands
    IDM_FILE_NEW             = 2001,
    IDM_FILE_OPEN            = 2002,
    IDM_FILE_SAVE            = 2003,
    IDM_FILE_EXIT            = 2004,

    IDM_WIZ_PROJECT          = 2005,
    IDM_WIZ_FILE_ASSOC       = 2006,
    IDM_WIZ_COMPONENT        = 2007,
    IDM_WIZ_RELEASE          = 2008,
    IDM_WIZ_INSERT_PRESET    = 2009,

    IDM_BUILD_VALIDATE       = 2010,
    IDM_BUILD_MSI            = 2011,
    IDM_BUILD_MSM            = 2012,
    IDM_BUILD_VPAT           = 2013,
    IDM_BUILD_EMIT_AI        = 2014,
    IDM_LAUNCH_MSIPRISM      = 2015,
    IDM_OPEN_OUTDIR          = 2016,

    // Persistent Toolbar & Navigation
    IDC_TB_WIZARD            = 2100,
    IDC_TB_NEW               = 2101,
    IDC_TB_OPEN              = 2102,
    IDC_TB_SAVE              = 2103,
    IDC_TB_VALIDATE          = 2104,
    IDC_TB_BUILD_MSI         = 2105,
    IDC_TB_BUILD_MSM         = 2106,
    IDC_TB_EXPORT_VPAT       = 2107,
    IDC_TB_MSIPRISM          = 2108,
    IDC_ARCH_COMBO           = 2109,
    IDC_SUITE_COMBO          = 2110,
    IDC_NAV_VIEWS            = 2200,

    // View 0: Package & Identity
    IDC_PKG_NAME             = 2210,
    IDC_PKG_TAGLINE          = 2211,
    IDC_PKG_WEBSITE          = 2212,
    IDC_PKG_VERSION          = 2213,
    IDC_PKG_PUBLISHER        = 2214,
    IDC_PKG_UPGRADECODE      = 2215,
    IDC_PKG_NEW_GUID         = 2216,
    IDC_PKG_SUBDIR           = 2217,
    IDC_PKG_MAINEXE          = 2218,
    IDC_PKG_SCOPE            = 2219,
    IDC_PKG_APPLY            = 2220,

    // View 1: Destination Computer (`SysTreeView32` + `SysListView32`)
    IDC_DEST_TREE            = 2225,
    IDC_PAY_GRID             = 2230,
    IDC_PAY_ADD_FILE         = 2231,
    IDC_PAY_ADD_FOLDER       = 2232,
    IDC_DEST_NEW_SUBDIR      = 2233,
    IDC_PAY_REMOVE           = 2234,
    IDC_DEST_SUBDIR_EDIT     = 2235,

    // View 2: Setup Design (Features -> Components -> KeyPaths)
    IDC_DESIGN_TREE          = 2310,
    IDC_FEAT_ID              = 2311,
    IDC_FEAT_TITLE           = 2312,
    IDC_FEAT_LEVEL           = 2313,
    IDC_FEAT_PARENT          = 2314,
    IDC_FEAT_SAVE_BTN        = 2315,
    IDC_COMP_ID              = 2316,
    IDC_COMP_FEAT            = 2317,
    IDC_COMP_ROOT            = 2318,
    IDC_COMP_SUBDIR          = 2319,
    IDC_COMP_KP_KIND         = 2320,
    IDC_COMP_KP_TARGET       = 2321,
    IDC_COMP_SAVE_BTN        = 2322,
    IDC_COMP_WIZ_BTN         = 2323,

    // View 3: File Associations & Shell Verbs
    IDC_FA_GRID              = 2330,
    IDC_FA_EXT               = 2331,
    IDC_FA_PROGID            = 2332,
    IDC_FA_DESC              = 2333,
    IDC_FA_TARGET            = 2334,
    IDC_FA_VERBS             = 2335,
    IDC_FA_ADVERTISE         = 2336,
    IDC_FA_ADD_BTN           = 2337,
    IDC_FA_REMOVE_BTN        = 2338,
    IDC_FA_WIZ_BTN           = 2339,

    // View 4: Shortcuts & Registry
    IDC_SC_GRID              = 2240,
    IDC_SC_NAME              = 2241,
    IDC_SC_TARGET            = 2242,
    IDC_SC_ADD_BTN           = 2243,
    IDC_REG_KEY              = 2244,
    IDC_REG_NAME             = 2245,
    IDC_REG_VALUE            = 2246,
    IDC_REG_COMP             = 2247,
    IDC_REG_ADD_BTN          = 2248,

    // View 5: C++ Wizard Designer (`SetupWizard.exe` Glass / AeroWizard / Autorun)
    IDC_UI_LAYOUT            = 2250,
    IDC_UI_PRIMARY_HEX       = 2251,
    IDC_UI_SECONDARY_HEX     = 2252,
    IDC_UI_INSTALL_BTN       = 2253,
    IDC_UI_ICON_PATH         = 2254,
    IDC_UI_ICON_BROWSE       = 2255,
    IDC_UI_ART_PATH          = 2256,
    IDC_UI_ART_BROWSE        = 2257,
    IDC_UI_APPLY             = 2258,
    IDC_UI_PREVIEW_STEP      = 2259,

    // View 6: Traditional `MSI.DLL` `Dialog` Table Designer (`370 x 270` DLU)
    IDC_MSI_PRESET_COMBO     = 2350,
    IDC_MSI_DLG_SELECT       = 2351,
    IDC_MSI_NEW_DLG_ID       = 2352,
    IDC_MSI_NEW_DLG_TITLE    = 2353,
    IDC_MSI_NEW_DLG_AFTER    = 2354,
    IDC_MSI_ADD_DLG_BTN      = 2355,
    IDC_MSI_CTRL_ID          = 2356,
    IDC_MSI_CTRL_TYPE        = 2357,
    IDC_MSI_CTRL_XYWH        = 2358,
    IDC_MSI_CTRL_PROP        = 2359,
    IDC_MSI_CTRL_TEXT        = 2360,
    IDC_MSI_ADD_CTRL_BTN     = 2361,
    IDC_MSI_INCLUDE_BTN      = 2362,

    // View 7: Authenticode Signing, .MST & .MSP
    IDC_SIG_ENABLED          = 2260,
    IDC_SIG_THUMBPRINT       = 2261,
    IDC_SIG_TIMESTAMP        = 2262,
    IDC_SIG_DLIB             = 2263,
    IDC_SIG_META             = 2264,
    IDC_SIG_APPLY            = 2265,
    IDC_ENT_BASE_MSI         = 2270,
    IDC_ENT_BASE_BROWSE      = 2271,
    IDC_ENT_TARG_MSI         = 2272,
    IDC_ENT_TARG_BROWSE      = 2273,
    IDC_ENT_GEN_MST          = 2274,
    IDC_ENT_GEN_MSP          = 2275,
    IDC_ENT_REL_WIZ_BTN      = 2276,

    // View 8: Lossless `.rsip` Source & Bottom Console
    IDC_SOURCE_EDIT          = 2301,
    IDC_CONSOLE_EDIT         = 2302
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
      << "WizardLayout      = aerowizard\r\n"
      << "MsiDialogPreset   = express_custom\r\n"
      << "AccentPrimaryHex  = #2563EB\r\n"
      << "AccentSecondaryHex= #FBBF24\r\n"
      << "InstallButtonText = &Install\r\n\r\n"
      << "[Feature.Main]\r\n"
      << "Title             = Core Application Files\r\n"
      << "Description       = Essential program executables and libraries.\r\n"
      << "Level             = 1\r\n\r\n"
      << "[Payload.App]\r\n"
      << "Source            = ..\\..\\bin\\$(ARCH)\\HelloRaggieSoft.exe\r\n"
      << "TargetRoot        = INSTALLDIR\r\n"
      << "Feature           = Main\r\n";
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
    int  viewIndex; // `-1` = Persistent across all views
    HWND hwnd;
};

struct DestTreeNodeMeta {
    std::string targetRoot;   // `""` = `[Target Computer]` (All folders)
    std::string targetSubdir; // e.g. `"Plugins"`
};

class InstallerStudioIde {
public:
    rsw::ui::RswTheme          theme{};
    HWND                       hWnd        = nullptr;
    HFONT                      hFontBanner = nullptr;
    HFONT                      hFontHead   = nullptr;
    HFONT                      hFontUi     = nullptr;
    HFONT                      hFontBold   = nullptr;
    HFONT                      hFontMono   = nullptr;

    std::filesystem::path      ideExeDir;
    std::filesystem::path      currentFilePath;
    rsw::RswSuiteWorkspace     activeSuite{};
    bool                       hasActiveSuite = false;
    rsw::RswManifestDocument   astDoc{};
    int                        activeView = VIEW_PACKAGE;
    std::vector<TaggedControl> taggedControls;
    RECT                       previewCanvasRc{};
    RECT                       msiCanvasRc{};

    // Destination Tree selection state (`""` = show all folders)
    std::vector<DestTreeNodeMeta> destNodeMetas;
    std::string                   selectedDestRoot;
    std::string                   selectedDestSubdir;

    // Persistent Toolbar, Sidebar, & Console
    HWND hBtnWizard = nullptr, hBtnNew = nullptr, hBtnOpen = nullptr, hBtnSave = nullptr, hBtnValidate = nullptr;
    HWND hBtnBuildMsi = nullptr, hBtnBuildMsm = nullptr, hBtnExportVpat = nullptr, hBtnMsiPrism = nullptr;
    HWND hArchCombo = nullptr, hSuiteCombo = nullptr;
    HWND hNavViews = nullptr, hConsoleEdit = nullptr;

    // View 0: Package & Identity
    HWND hPkgName = nullptr, hPkgTag = nullptr, hPkgWeb = nullptr, hPkgVer = nullptr, hPkgPub = nullptr;
    HWND hPkgGuid = nullptr, hPkgBtnGuid = nullptr, hPkgSub = nullptr, hPkgExe = nullptr, hPkgScope = nullptr, hPkgApply = nullptr;

    // View 1: Destination Computer (`SysTreeView32` + `SysListView32`)
    HWND hDestTree = nullptr, hPayGrid = nullptr;
    HWND hPayAddFile = nullptr, hPayAddDir = nullptr, hDestSubEdit = nullptr, hDestNewSubBtn = nullptr, hPayRemove = nullptr;

    // View 2: Setup Design (`Features -> Components -> KeyPaths`)
    HWND hDesignTree = nullptr;
    HWND hFeatId = nullptr, hFeatTitle = nullptr, hFeatLevel = nullptr, hFeatParent = nullptr, hFeatSaveBtn = nullptr;
    HWND hCompId = nullptr, hCompFeat = nullptr, hCompRoot = nullptr, hCompSubdir = nullptr;
    HWND hCompKpKind = nullptr, hCompKpTarget = nullptr, hCompSaveBtn = nullptr, hCompWizBtn = nullptr;

    // View 3: File Associations & Verbs
    HWND hFaGrid = nullptr, hFaExt = nullptr, hFaProgId = nullptr, hFaDesc = nullptr;
    HWND hFaTarget = nullptr, hFaVerbs = nullptr, hFaAdvChk = nullptr;
    HWND hFaAddBtn = nullptr, hFaRemBtn = nullptr, hFaWizBtn = nullptr;

    // View 4: Shortcuts & Registry
    HWND hScGrid = nullptr, hScName = nullptr, hScTarget = nullptr, hScAddBtn = nullptr;
    HWND hRegKey = nullptr, hRegName = nullptr, hRegVal = nullptr, hRegComp = nullptr, hRegAddBtn = nullptr;

    // View 5: C++ Wizard Designer (`SetupWizard.exe`)
    HWND hUiLayout = nullptr, hUiPreviewStep = nullptr, hUiPrim = nullptr, hUiSec = nullptr, hUiInstBtn = nullptr;
    HWND hUiIcon = nullptr, hUiIconBtn = nullptr, hUiArt = nullptr, hUiArtBtn = nullptr, hUiApply = nullptr;
    HICON   hPreviewIcon = nullptr;
    HBITMAP hPreviewArt  = nullptr;
    int     previewArtW  = 0;
    int     previewArtH  = 0;

    // View 6: Traditional `MSI.DLL` `Dialog` Table Designer
    HWND hMsiPreset = nullptr, hMsiDlgSelect = nullptr;
    HWND hMsiNewId = nullptr, hMsiNewTitle = nullptr, hMsiNewAfter = nullptr, hMsiAddDlgBtn = nullptr;
    HWND hMsiCtrlId = nullptr, hMsiCtrlType = nullptr, hMsiCtrlXywh = nullptr;
    HWND hMsiCtrlProp = nullptr, hMsiCtrlText = nullptr, hMsiAddCtrlBtn = nullptr, hMsiIncludeBtn = nullptr;

    // View 7: Signing, .MST & .MSP
    HWND hSigEnable = nullptr, hSigThumb = nullptr, hSigTs = nullptr, hSigDlib = nullptr, hSigMeta = nullptr, hSigApply = nullptr;
    HWND hEntBase = nullptr, hEntBaseBtn = nullptr, hEntTarg = nullptr, hEntTargBtn = nullptr;
    HWND hEntGenMst = nullptr, hEntGenMsp = nullptr, hEntRelWizBtn = nullptr;

    // View 8: Full Monospace `.rsip` Source
    HWND hSourceEdit = nullptr;

    ~InstallerStudioIde() {
        DestroyFonts();
        ReleasePreviewAssets();
    }

    void DestroyFonts() noexcept {
        for (HFONT* f : {&hFontBanner, &hFontHead, &hFontUi, &hFontBold, &hFontMono}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
    }

    void ReleasePreviewAssets() noexcept {
        if (hPreviewIcon) { ::DestroyIcon(hPreviewIcon); hPreviewIcon = nullptr; }
        if (hPreviewArt)  { ::DeleteObject(hPreviewArt); hPreviewArt  = nullptr; }
        previewArtW = 0;
        previewArtH = 0;
    }

    HMENU BuildMenuBar() {
        HMENU hBar  = ::CreateMenu();
        HMENU hFile = ::CreatePopupMenu();
        ::AppendMenuW(hFile, MF_STRING, IDM_WIZ_PROJECT, L"New Project &Wizard (Aero Project Assistant)...\tCtrl+W");
        ::AppendMenuW(hFile, MF_STRING, IDM_FILE_NEW,    L"&New Starter .rsip Project\tCtrl+N");
        ::AppendMenuW(hFile, MF_STRING, IDM_FILE_OPEN,   L"&Open .rsip Project or .rsis Suite...\tCtrl+O");
        ::AppendMenuW(hFile, MF_STRING, IDM_FILE_SAVE,   L"&Save Project (Lossless AST)\tCtrl+S");
        ::AppendMenuW(hFile, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(hFile, MF_STRING, IDM_FILE_EXIT,   L"E&xit");

        HMENU hWiz = ::CreatePopupMenu();
        ::AppendMenuW(hWiz, MF_STRING, IDM_WIZ_PROJECT,       L"&New Project AeroWizard (Project Assistant)...\tCtrl+W");
        ::AppendMenuW(hWiz, MF_STRING, IDM_WIZ_COMPONENT,     L"&Component & KeyPath AeroWizard (Microsoft Rules)...");
        ::AppendMenuW(hWiz, MF_STRING, IDM_WIZ_FILE_ASSOC,    L"&File Association & Document Icon AeroWizard...");
        ::AppendMenuW(hWiz, MF_STRING, IDM_WIZ_RELEASE,       L"&Release, Transform (.MST) & Patch (.MSP) AeroWizard...");
        ::AppendMenuW(hWiz, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(hWiz, MF_STRING, IDM_WIZ_INSERT_PRESET, L"&Insert @include Sample Dialog Preset (.rsii)...");

        HMENU hBuild = ::CreatePopupMenu();
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_VALIDATE,  L"&Validate Manifest + Component Rules\tF7");
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_MSI,       L"&Build Enterprise MSI + Setup.exe\tF5");
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_MSM,       L"Build &Merge Module (.msm)\tF6");
        ::AppendMenuW(hBuild, MF_SEPARATOR, 0, nullptr);
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_VPAT,      L"Export Installer &VPAT 2.5 HTML5 Report");
        ::AppendMenuW(hBuild, MF_STRING, IDM_BUILD_EMIT_AI,   L"Emit &llms.txt + SKILL.md");
        ::AppendMenuW(hBuild, MF_STRING, IDM_LAUNCH_MSIPRISM, L"Open &MSI Prism Inspector (msiprism.exe)");
        ::AppendMenuW(hBuild, MF_STRING, IDM_OPEN_OUTDIR,     L"Open &Output Folder in Explorer");

        ::AppendMenuW(hBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hFile),  L"&File");
        ::AppendMenuW(hBar, MF_POPUP, reinterpret_cast<UINT_PTR>(hWiz),   L"&Wizards (Aero)");
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
        auto mkEd = [&](int view, int id, const wchar_t* initText = L"") {
            return RegisterViewCtrl(view, ::CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", initText, WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr));
        };
        auto mkCombo = [&](int view, int id, std::initializer_list<const wchar_t*> items, int defSel = 0) {
            HWND cb = RegisterViewCtrl(view, ::CreateWindowExW(
                0, L"COMBOBOX", nullptr, WS_CHILD | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 160, 180, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr));
            for (const wchar_t* s : items) {
                ::SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(s));
            }
            ::SendMessageW(cb, CB_SETCURSEL, defSel, 0);
            return cb;
        };

        // Top Persistent Toolbar (`view = -1`)
        hBtnWizard     = mkBtn(-1, L"Project &Wizard...", IDC_TB_WIZARD);
        hBtnNew        = mkBtn(-1, L"&New",               IDC_TB_NEW);
        hBtnOpen       = mkBtn(-1, L"&Open...",           IDC_TB_OPEN);
        hBtnSave       = mkBtn(-1, L"&Save",              IDC_TB_SAVE);
        hBtnValidate   = mkBtn(-1, L"&Validate (F7)",     IDC_TB_VALIDATE);
        hBtnBuildMsi   = mkBtn(-1, L"&Build MSI+EXE (F5)",IDC_TB_BUILD_MSI);
        hBtnBuildMsm   = mkBtn(-1, L".&MSM",              IDC_TB_BUILD_MSM);
        hBtnExportVpat = mkBtn(-1, L"&VPAT 2.5",          IDC_TB_EXPORT_VPAT);
        hBtnMsiPrism   = mkBtn(-1, L"MSI &Prism",         IDC_TB_MSIPRISM);

        const auto hp = rsw::QueryHostPlatform();
        hArchCombo  = mkCombo(-1, IDC_ARCH_COMBO, {L"arm64", L"x64"}, (hp.NativeArchSlug() == "x64") ? 1 : 0);
        hSuiteCombo = mkCombo(-1, IDC_SUITE_COMBO, {L"[Standalone .rsip Mode]"}, 0);

        // Left 9-View Navigation Sidebar (`view = -1`)
        hNavViews = RegisterViewCtrl(-1, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", nullptr, WS_CHILD | WS_TABSTOP | LBS_NOTIFY,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_NAV_VIEWS)), hInst, nullptr));
        const wchar_t* viewLabels[] = {
            L"1. Package & Identity",
            L"2. Destination Files & Folders",
            L"3. Setup Design (Components)",
            L"4. File Associations & Verbs",
            L"5. Shortcuts & Registry",
            L"6. C++ Wizard Designer",
            L"7. MSI.DLL Dialog Designer",
            L"8. Signing, .MST & .MSP",
            L"9. .RSIP Source (AST)"
        };
        for (const wchar_t* vl : viewLabels) {
            ::SendMessageW(hNavViews, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(vl));
        }
        ::SendMessageW(hNavViews, LB_SETCURSEL, 0, 0);

        // --- VIEW 0: Package & Identity ---
        mkLbl(VIEW_PACKAGE, L"&Product Name:");       hPkgName = mkEd(VIEW_PACKAGE, IDC_PKG_NAME);
        mkLbl(VIEW_PACKAGE, L"Subtitle / &Tagline:"); hPkgTag  = mkEd(VIEW_PACKAGE, IDC_PKG_TAGLINE);
        mkLbl(VIEW_PACKAGE, L"Footer &Website:");     hPkgWeb  = mkEd(VIEW_PACKAGE, IDC_PKG_WEBSITE);
        mkLbl(VIEW_PACKAGE, L"MSI &Version (X.Y.Z):");hPkgVer  = mkEd(VIEW_PACKAGE, IDC_PKG_VERSION);
        mkLbl(VIEW_PACKAGE, L"&Manufacturer:");       hPkgPub  = mkEd(VIEW_PACKAGE, IDC_PKG_PUBLISHER);
        mkLbl(VIEW_PACKAGE, L"&UpgradeCode GUID:");   hPkgGuid = mkEd(VIEW_PACKAGE, IDC_PKG_UPGRADECODE);
        hPkgBtnGuid = mkBtn(VIEW_PACKAGE, L"New &GUID", IDC_PKG_NEW_GUID);
        mkLbl(VIEW_PACKAGE, L"Install &Subfolder:");  hPkgSub  = mkEd(VIEW_PACKAGE, IDC_PKG_SUBDIR);
        mkLbl(VIEW_PACKAGE, L"Main &Executable:");    hPkgExe  = mkEd(VIEW_PACKAGE, IDC_PKG_MAINEXE);
        mkLbl(VIEW_PACKAGE, L"Install S&cope:");      hPkgScope= mkCombo(VIEW_PACKAGE, IDC_PKG_SCOPE, {L"machine", L"user"}, 0);
        hPkgApply = mkBtn(VIEW_PACKAGE, L"&Apply Package Identity to .rsip AST (Preserves Comments)", IDC_PKG_APPLY);

        // --- VIEW 1: Destination Computer (`SysTreeView32` + `SysListView32`) ---
        hDestTree = RegisterViewCtrl(VIEW_DEST_FOLDERS, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr,
            WS_CHILD | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DEST_TREE)), hInst, nullptr));
        hPayGrid = RegisterViewCtrl(VIEW_DEST_FOLDERS, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PAY_GRID)), hInst, nullptr));
        ListView_SetExtendedListViewStyle(hPayGrid, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        hPayAddFile    = mkBtn(VIEW_DEST_FOLDERS, L"&Add File Here...",   IDC_PAY_ADD_FILE);
        hPayAddDir     = mkBtn(VIEW_DEST_FOLDERS, L"Add &Folder Here...", IDC_PAY_ADD_FOLDER);
        hDestSubEdit   = mkEd(VIEW_DEST_FOLDERS,  IDC_DEST_SUBDIR_EDIT, L"Plugins");
        hDestNewSubBtn = mkBtn(VIEW_DEST_FOLDERS, L"+ &CreateFolder",     IDC_DEST_NEW_SUBDIR);
        hPayRemove     = mkBtn(VIEW_DEST_FOLDERS, L"&Remove Selected",    IDC_PAY_REMOVE);

        // --- VIEW 2: Setup Design (`Features -> Components -> KeyPaths`) ---
        hDesignTree = RegisterViewCtrl(VIEW_SETUP_DESIGN, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_TREEVIEWW, nullptr,
            WS_CHILD | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_DESIGN_TREE)), hInst, nullptr));
        mkLbl(VIEW_SETUP_DESIGN, L"Feature ID:");     hFeatId     = mkEd(VIEW_SETUP_DESIGN, IDC_FEAT_ID, L"Main");
        mkLbl(VIEW_SETUP_DESIGN, L"Feature Title:");  hFeatTitle  = mkEd(VIEW_SETUP_DESIGN, IDC_FEAT_TITLE, L"Core Application");
        mkLbl(VIEW_SETUP_DESIGN, L"Install Level:");  hFeatLevel  = mkEd(VIEW_SETUP_DESIGN, IDC_FEAT_LEVEL, L"1");
        mkLbl(VIEW_SETUP_DESIGN, L"Parent Feature:"); hFeatParent = mkEd(VIEW_SETUP_DESIGN, IDC_FEAT_PARENT, L"");
        hFeatSaveBtn = mkBtn(VIEW_SETUP_DESIGN, L"Save &Feature", IDC_FEAT_SAVE_BTN);

        mkLbl(VIEW_SETUP_DESIGN, L"Component ID:");   hCompId     = mkEd(VIEW_SETUP_DESIGN, IDC_COMP_ID, L"CoreExe");
        mkLbl(VIEW_SETUP_DESIGN, L"Owning Feature:"); hCompFeat   = mkEd(VIEW_SETUP_DESIGN, IDC_COMP_FEAT, L"Main");
        mkLbl(VIEW_SETUP_DESIGN, L"Target Root:");
        hCompRoot = mkCombo(VIEW_SETUP_DESIGN, IDC_COMP_ROOT, {
            L"INSTALLDIR", L"CommonFiles64Folder", L"CommonAppDataFolder", L"AppDataFolder",
            L"LocalAppDataFolder", L"ApplicationProgramsFolder", L"DesktopFolder", L"System64Folder", L"FontsFolder"
        }, 0);
        mkLbl(VIEW_SETUP_DESIGN, L"Target Subdir:");  hCompSubdir = mkEd(VIEW_SETUP_DESIGN, IDC_COMP_SUBDIR, L"");
        mkLbl(VIEW_SETUP_DESIGN, L"KeyPath Kind:");
        hCompKpKind = mkCombo(VIEW_SETUP_DESIGN, IDC_COMP_KP_KIND, {L"auto", L"file", L"registry", L"folder"}, 0);
        mkLbl(VIEW_SETUP_DESIGN, L"KeyPath Target:"); hCompKpTarget = mkEd(VIEW_SETUP_DESIGN, IDC_COMP_KP_TARGET, L"");
        hCompSaveBtn = mkBtn(VIEW_SETUP_DESIGN, L"Save &Component", IDC_COMP_SAVE_BTN);
        hCompWizBtn  = mkBtn(VIEW_SETUP_DESIGN, L"&Component & KeyPath AeroWizard...", IDC_COMP_WIZ_BTN);

        // --- VIEW 3: File Associations & Shell Verbs ---
        hFaGrid = RegisterViewCtrl(VIEW_FILE_ASSOC, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_FA_GRID)), hInst, nullptr));
        ListView_SetExtendedListViewStyle(hFaGrid, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        mkLbl(VIEW_FILE_ASSOC, L".Ext:");        hFaExt    = mkEd(VIEW_FILE_ASSOC, IDC_FA_EXT, L".rsdoc");
        mkLbl(VIEW_FILE_ASSOC, L"ProgId:");      hFaProgId = mkEd(VIEW_FILE_ASSOC, IDC_FA_PROGID, L"MyStudio.MyApp.Doc");
        mkLbl(VIEW_FILE_ASSOC, L"Description:"); hFaDesc   = mkEd(VIEW_FILE_ASSOC, IDC_FA_DESC, L"Application Document");
        mkLbl(VIEW_FILE_ASSOC, L"Target Exe:");  hFaTarget = mkEd(VIEW_FILE_ASSOC, IDC_FA_TARGET, L"HelloRaggieSoft.exe");
        mkLbl(VIEW_FILE_ASSOC, L"Verbs:");       hFaVerbs  = mkEd(VIEW_FILE_ASSOC, IDC_FA_VERBS, L"open:&Open:\"%1\"");
        hFaAdvChk = mkBtn(VIEW_FILE_ASSOC, L"&Advertise (ProgId/Verb Tables)", IDC_FA_ADVERTISE, BS_AUTOCHECKBOX);
        hFaAddBtn = mkBtn(VIEW_FILE_ASSOC, L"&Add Association", IDC_FA_ADD_BTN);
        hFaRemBtn = mkBtn(VIEW_FILE_ASSOC, L"&Remove Selected", IDC_FA_REMOVE_BTN);
        hFaWizBtn = mkBtn(VIEW_FILE_ASSOC, L"File Association &AeroWizard...", IDC_FA_WIZ_BTN);

        // --- VIEW 4: Shortcuts & Registry ---
        hScGrid = RegisterViewCtrl(VIEW_SHORTCUTS, ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SC_GRID)), hInst, nullptr));
        ListView_SetExtendedListViewStyle(hScGrid, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        mkLbl(VIEW_SHORTCUTS, L"Shortcut Name:"); hScName   = mkEd(VIEW_SHORTCUTS, IDC_SC_NAME);
        mkLbl(VIEW_SHORTCUTS, L"Target Exe:");    hScTarget = mkEd(VIEW_SHORTCUTS, IDC_SC_TARGET);
        hScAddBtn  = mkBtn(VIEW_SHORTCUTS, L"Add &Shortcut", IDC_SC_ADD_BTN);
        mkLbl(VIEW_SHORTCUTS, L"Reg Key:");       hRegKey   = mkEd(VIEW_SHORTCUTS, IDC_REG_KEY);
        mkLbl(VIEW_SHORTCUTS, L"Name:");          hRegName  = mkEd(VIEW_SHORTCUTS, IDC_REG_NAME);
        mkLbl(VIEW_SHORTCUTS, L"Value:");         hRegVal   = mkEd(VIEW_SHORTCUTS, IDC_REG_VALUE);
        mkLbl(VIEW_SHORTCUTS, L"Comp:");          hRegComp  = mkEd(VIEW_SHORTCUTS, IDC_REG_COMP);
        hRegAddBtn = mkBtn(VIEW_SHORTCUTS, L"Add &Registry", IDC_REG_ADD_BTN);

        // --- VIEW 5: C++ Wizard Designer (`SetupWizard.exe`) ---
        mkLbl(VIEW_CPP_WIZARD, L"Setup.exe Style:");
        hUiLayout = mkCombo(VIEW_CPP_WIZARD, IDC_UI_LAYOUT, {L"standard", L"aerowizard", L"autorun16x9"}, 1);
        mkLbl(VIEW_CPP_WIZARD, L"Preview Step:");
        hUiPreviewStep = mkCombo(VIEW_CPP_WIZARD, IDC_UI_PREVIEW_STEP, {
            L"Welcome & Hero Brand Card",
            L"Setup Type (48x48 Vector Cards)",
            L"Office-Style Repair Mode",
            L"Installing Live Progress",
            L"16:9 Autorun Cover & 2x2 Menu"
        }, 1);
        mkLbl(VIEW_CPP_WIZARD, L"Primary Accent Hex:");   hUiPrim    = mkEd(VIEW_CPP_WIZARD, IDC_UI_PRIMARY_HEX);
        mkLbl(VIEW_CPP_WIZARD, L"Secondary Accent Hex:"); hUiSec     = mkEd(VIEW_CPP_WIZARD, IDC_UI_SECONDARY_HEX);
        mkLbl(VIEW_CPP_WIZARD, L"Install Button Text:");  hUiInstBtn = mkEd(VIEW_CPP_WIZARD, IDC_UI_INSTALL_BTN);
        mkLbl(VIEW_CPP_WIZARD, L"Icon (.ico) Path:");     hUiIcon    = mkEd(VIEW_CPP_WIZARD, IDC_UI_ICON_PATH);
        hUiIconBtn = mkBtn(VIEW_CPP_WIZARD, L"Pick .&ico...", IDC_UI_ICON_BROWSE);
        mkLbl(VIEW_CPP_WIZARD, L"Artwork (.png/.jpg):");  hUiArt     = mkEd(VIEW_CPP_WIZARD, IDC_UI_ART_PATH);
        hUiArtBtn  = mkBtn(VIEW_CPP_WIZARD, L"Pick &Art...", IDC_UI_ART_BROWSE);
        hUiApply   = mkBtn(VIEW_CPP_WIZARD, L"&Apply C++ Wizard Branding & Refresh Canvas", IDC_UI_APPLY);

        // --- VIEW 6: Traditional `MSI.DLL` `Dialog` Table Designer (`370 x 270` DLU) ---
        mkLbl(VIEW_MSI_DIALOGS, L"MSI Workflow:");
        hMsiPreset = mkCombo(VIEW_MSI_DIALOGS, IDC_MSI_PRESET_COMBO, {
            L"express_custom (Express / Compact / Custom)",
            L"minimal (3-Screen Streamlined)"
        }, 0);
        mkLbl(VIEW_MSI_DIALOGS, L"Inspect Dialog:");
        hMsiDlgSelect = mkCombo(VIEW_MSI_DIALOGS, IDC_MSI_DLG_SELECT, {
            L"WelcomeDlg", L"LicenseAgreementDlg", L"SetupTypeDlg",
            L"CustomizeDlg", L"InstallDirDlg", L"ShortcutsDlg",
            L"RepairModeDlg", L"VerifyReadyDlg", L"ExitDialog"
        }, 2);
        mkLbl(VIEW_MSI_DIALOGS, L"New Dialog ID:");    hMsiNewId    = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_NEW_DLG_ID,    L"ServerConfigDlg");
        mkLbl(VIEW_MSI_DIALOGS, L"Banner Title:");     hMsiNewTitle = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_NEW_DLG_TITLE, L"Connection Settings");
        mkLbl(VIEW_MSI_DIALOGS, L"Splice After:");     hMsiNewAfter = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_NEW_DLG_AFTER, L"SetupTypeDlg");
        hMsiAddDlgBtn = mkBtn(VIEW_MSI_DIALOGS, L"+ Add [Msi&Dialog] to Sequence", IDC_MSI_ADD_DLG_BTN);

        mkLbl(VIEW_MSI_DIALOGS, L"Control ID:");       hMsiCtrlId   = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_CTRL_ID,   L"EndpointEdit");
        mkLbl(VIEW_MSI_DIALOGS, L"Control Type:");
        hMsiCtrlType = mkCombo(VIEW_MSI_DIALOGS, IDC_MSI_CTRL_TYPE, {
            L"Text", L"Edit", L"PathEdit", L"CheckBox", L"RadioButtonGroup", L"ComboBox", L"GroupBox"
        }, 1);
        mkLbl(VIEW_MSI_DIALOGS, L"X, Y, W, H (DLU):"); hMsiCtrlXywh = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_CTRL_XYWH, L"20, 74, 330, 18");
        mkLbl(VIEW_MSI_DIALOGS, L"MSI Property:");     hMsiCtrlProp = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_CTRL_PROP, L"ENDPOINT_URL");
        mkLbl(VIEW_MSI_DIALOGS, L"Text / Items:");     hMsiCtrlText = mkEd(VIEW_MSI_DIALOGS, IDC_MSI_CTRL_TEXT, L"https://raggiesoftstudio.com");
        hMsiAddCtrlBtn = mkBtn(VIEW_MSI_DIALOGS, L"+ Place [Msi&Control]", IDC_MSI_ADD_CTRL_BTN);
        hMsiIncludeBtn = mkBtn(VIEW_MSI_DIALOGS, L"@&include Sample Preset...", IDC_MSI_INCLUDE_BTN);

        // --- VIEW 7: Authenticode Signing, .MST & .MSP ---
        hSigEnable = mkBtn(VIEW_SIGN_REL, L"&Enable Dual-Layer Authenticode Signing (Product.msi + Setup.exe)", IDC_SIG_ENABLED, BS_AUTOCHECKBOX);
        mkLbl(VIEW_SIGN_REL, L"Cert SHA-1 Thumbprint:"); hSigThumb = mkEd(VIEW_SIGN_REL, IDC_SIG_THUMBPRINT);
        mkLbl(VIEW_SIGN_REL, L"Timestamp Server URL:");  hSigTs    = mkEd(VIEW_SIGN_REL, IDC_SIG_TIMESTAMP);
        mkLbl(VIEW_SIGN_REL, L"Azure Signing Dlib:");    hSigDlib  = mkEd(VIEW_SIGN_REL, IDC_SIG_DLIB);
        mkLbl(VIEW_SIGN_REL, L"Azure Metadata JSON:");   hSigMeta  = mkEd(VIEW_SIGN_REL, IDC_SIG_META);
        hSigApply  = mkBtn(VIEW_SIGN_REL, L"&Save Code Signing Settings", IDC_SIG_APPLY);

        mkLbl(VIEW_SIGN_REL, L"Baseline Released .MSI:"); hEntBase = mkEd(VIEW_SIGN_REL, IDC_ENT_BASE_MSI);
        hEntBaseBtn   = mkBtn(VIEW_SIGN_REL, L"&Browse...", IDC_ENT_BASE_BROWSE);
        mkLbl(VIEW_SIGN_REL, L"Target / Upgraded .MSI:"); hEntTarg = mkEd(VIEW_SIGN_REL, IDC_ENT_TARG_MSI);
        hEntTargBtn   = mkBtn(VIEW_SIGN_REL, L"B&rowse...", IDC_ENT_TARG_BROWSE);
        hEntGenMst    = mkBtn(VIEW_SIGN_REL, L"Emit .&MST Transform", IDC_ENT_GEN_MST);
        hEntGenMsp    = mkBtn(VIEW_SIGN_REL, L"Emit .MS&P Patch",     IDC_ENT_GEN_MSP);
        hEntRelWizBtn = mkBtn(VIEW_SIGN_REL, L"&Release / .MST / .MSP AeroWizard...", IDC_ENT_REL_WIZ_BTN);

        // --- VIEW 8: Lossless `.rsip` Source ---
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
            col.mask     = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText  = const_cast<LPWSTR>(text);
            col.cx       = S(w);
            col.iSubItem = idx;
            ListView_InsertColumn(grid, idx, &col);
        };
        addCol(hPayGrid, 0, L"Item / ID",    120);
        addCol(hPayGrid, 1, L"Destination",  165);
        addCol(hPayGrid, 2, L"Component",    120);
        addCol(hPayGrid, 3, L"Feature",       85);
        addCol(hPayGrid, 4, L"KeyPath",      110);
        addCol(hPayGrid, 5, L"Source / Info",230);

        addCol(hFaGrid,  0, L"Extension",     90);
        addCol(hFaGrid,  1, L"ProgId",       220);
        addCol(hFaGrid,  2, L"Description",  200);
        addCol(hFaGrid,  3, L"Target Exe",   150);
        addCol(hFaGrid,  4, L"Verbs",        150);
        addCol(hFaGrid,  5, L"MSI Mode",     130);

        addCol(hScGrid,  0, L"Kind",          95);
        addCol(hScGrid,  1, L"Section ID",   135);
        addCol(hScGrid,  2, L"Name / Key",   260);
        addCol(hScGrid,  3, L"Target / Value",240);
        addCol(hScGrid,  4, L"Component",    120);
    }

    void LayoutChildren() {
        const UINT dpi = ::GetDpiForWindow(hWnd);
        DestroyFonts();
        const int ts = theme.textScalePercent;
        hFontBanner = rsw::ui::CreateScaledFont(L"Segoe UI",      13, FW_BOLD,     dpi, ts);
        hFontHead   = rsw::ui::CreateScaledFont(L"Segoe UI",      10, FW_SEMIBOLD, dpi, ts);
        hFontUi     = rsw::ui::CreateScaledFont(L"Segoe UI",       9, FW_NORMAL,   dpi, ts);
        hFontBold   = rsw::ui::CreateScaledFont(L"Segoe UI",       9, FW_SEMIBOLD, dpi, ts);
        hFontMono   = rsw::ui::CreateScaledFont(L"Cascadia Mono", 10, FW_NORMAL,   dpi, ts);

        for (const auto& tc : taggedControls) {
            ::SendMessageW(tc.hwnd, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUi), TRUE);
        }
        for (HWND h : {hBtnWizard, hBtnBuildMsi, hPkgApply, hUiApply, hCompWizBtn, hFaWizBtn, hEntRelWizBtn}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);
        }
        for (HWND h : {hSourceEdit, hConsoleEdit}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontMono), TRUE);
        }
        if (!theme.isHighContrast) {
            for (HWND g : {hPayGrid, hFaGrid, hScGrid}) {
                ListView_SetBkColor(g, theme.cardBg);
                ListView_SetTextBkColor(g, theme.cardBg);
                ListView_SetTextColor(g, theme.textPrimary);
            }
            for (HWND tv : {hDestTree, hDesignTree}) {
                TreeView_SetBkColor(tv, theme.cardBg);
                TreeView_SetTextColor(tv, theme.textPrimary);
            }
        }

        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(760, rc.right - rc.left);
        const int H = std::max<int>(540, rc.bottom - rc.top);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, ts); };

        const int tbY = S(60);
        int tx = S(12);
        auto placeTb = [&](HWND h, int w) { ::MoveWindow(h, tx, tbY, S(w), S(30), TRUE); tx += S(w + 4); };
        placeTb(hBtnWizard,     118);
        placeTb(hBtnNew,         48);
        placeTb(hBtnOpen,        62);
        placeTb(hBtnSave,        52);
        placeTb(hBtnValidate,    92);
        placeTb(hBtnBuildMsi,   132);
        placeTb(hBtnBuildMsm,    56);
        placeTb(hBtnExportVpat,  76);
        placeTb(hBtnMsiPrism,    80);
        ::MoveWindow(hArchCombo,  tx, tbY + S(2), S(74), S(140), TRUE);
        tx += S(78);
        ::MoveWindow(hSuiteCombo, tx, tbY + S(2), std::max(S(140), W - tx - S(12)), S(280), TRUE);

        const int contentTop = S(98);
        const int consoleH   = S(118);
        const int consoleY   = H - consoleH - S(10);
        const int mainH      = std::max(S(260), consoleY - contentTop - S(8));
        const int navW       = S(224);

        ::MoveWindow(hNavViews,    S(12), contentTop, navW, mainH, TRUE);
        ::MoveWindow(hConsoleEdit, S(12), consoleY,   W - S(24), consoleH, TRUE);

        const int paneX = S(24) + navW;
        const int paneW = std::max(S(440), W - paneX - S(12));

        auto viewCtrls = [&](int viewIdx) {
            std::vector<HWND> vh;
            for (const auto& tc : taggedControls) {
                if (tc.viewIndex == viewIdx) vh.push_back(tc.hwnd);
            }
            return vh;
        };

        // View 0: Package & Identity
        {
            auto v = viewCtrls(VIEW_PACKAGE);
            int y = contentTop + S(4);
            const int lw = S(148), fw = std::min(S(480), paneW - lw - S(16));
            auto row = [&](HWND lbl, HWND ed, int rsv = 0) {
                ::MoveWindow(lbl, paneX, y + S(4), lw, S(22), TRUE);
                ::MoveWindow(ed,  paneX + lw, y, fw - rsv, S(26), TRUE);
                y += S(31);
            };
            if (v.size() >= 20) {
                row(v[0], v[1]); row(v[2], v[3]); row(v[4], v[5]); row(v[6], v[7]); row(v[8], v[9]);
                row(v[10], v[11], S(92));
                ::MoveWindow(hPkgBtnGuid, paneX + lw + fw - S(86), y - S(31), S(86), S(26), TRUE);
                row(v[13], v[14]); row(v[15], v[16]);
                ::MoveWindow(v[17], paneX, y + S(4), lw, S(22), TRUE);
                ::MoveWindow(hPkgScope, paneX + lw, y, S(160), S(140), TRUE);
                y += S(36);
                ::MoveWindow(hPkgApply, paneX, y, fw + lw, S(34), TRUE);
            }
        }

        // View 1: Destination Files & Folders (`SysTreeView32` left + `SysListView32` right!)
        {
            const int treeW = S(248);
            const int listX = paneX + treeW + S(8);
            const int listW = std::max(S(240), paneW - treeW - S(8));
            const int barY  = contentTop + mainH - S(34);
            ::MoveWindow(hDestTree, paneX, contentTop, treeW, mainH - S(42), TRUE);
            ::MoveWindow(hPayGrid,  listX, contentTop, listW, mainH - S(42), TRUE);

            ::MoveWindow(hDestSubEdit,   paneX,            barY + S(2), S(124), S(28), TRUE);
            ::MoveWindow(hDestNewSubBtn, paneX + S(128),   barY,        S(120), S(32), TRUE);
            ::MoveWindow(hPayAddFile,    listX,            barY,        S(136), S(32), TRUE);
            ::MoveWindow(hPayAddDir,     listX + S(142),   barY,        S(146), S(32), TRUE);
            ::MoveWindow(hPayRemove,     listX + S(294),   barY,        S(140), S(32), TRUE);
        }

        // View 2: Setup Design (`Features -> Components -> KeyPaths` tree + right inspector!)
        {
            auto v = viewCtrls(VIEW_SETUP_DESIGN);
            const int treeW = std::max(S(320), paneW - S(396));
            const int formX = paneX + treeW + S(12);
            const int formW = paneW - treeW - S(12);
            ::MoveWindow(hDesignTree, paneX, contentTop, treeW, mainH, TRUE);
            if (v.size() >= 22) {
                int y = contentTop + S(2);
                const int lw = S(116), fw = std::max(S(140), formW - lw);
                auto row = [&](HWND lbl, HWND ctl, int h = 24) {
                    ::MoveWindow(lbl, formX, y + S(3), lw, S(20), TRUE);
                    ::MoveWindow(ctl, formX + lw, y, fw, S(h), TRUE);
                    y += S(27);
                };
                row(v[1], hFeatId);
                row(v[3], hFeatTitle);
                row(v[5], hFeatLevel);
                row(v[7], hFeatParent);
                ::MoveWindow(hFeatSaveBtn, formX, y, formW, S(28), TRUE);
                y += S(38);

                row(v[10], hCompId);
                row(v[12], hCompFeat);
                row(v[14], hCompRoot, 160);
                row(v[16], hCompSubdir);
                row(v[18], hCompKpKind, 140);
                row(v[20], hCompKpTarget);
                ::MoveWindow(hCompSaveBtn, formX, y + S(2), (formW - S(8)) / 2, S(30), TRUE);
                ::MoveWindow(hCompWizBtn,  formX + (formW + S(8)) / 2, y + S(2), (formW - S(8)) / 2, S(30), TRUE);
            }
        }

        // View 3: File Associations & Shell Verbs
        {
            auto v = viewCtrls(VIEW_FILE_ASSOC);
            const int gridH = mainH - S(116);
            ::MoveWindow(hFaGrid, paneX, contentTop, paneW, gridH, TRUE);
            if (v.size() >= 15) {
                const int y1 = contentTop + gridH + S(8);
                ::MoveWindow(v[1], paneX,          y1 + S(4), S(38),  S(22), TRUE);
                ::MoveWindow(hFaExt, paneX + S(40), y1,        S(72),  S(26), TRUE);
                ::MoveWindow(v[3], paneX + S(120), y1 + S(4), S(50),  S(22), TRUE);
                ::MoveWindow(hFaProgId, paneX + S(172), y1,    S(200), S(26), TRUE);
                ::MoveWindow(v[5], paneX + S(380), y1 + S(4), S(78),  S(22), TRUE);
                ::MoveWindow(hFaDesc, paneX + S(460), y1,      std::max(S(140), paneW - S(460)), S(26), TRUE);

                const int y2 = y1 + S(34);
                ::MoveWindow(v[7], paneX,          y2 + S(4), S(74),  S(22), TRUE);
                ::MoveWindow(hFaTarget, paneX + S(76), y2,     S(150), S(26), TRUE);
                ::MoveWindow(v[9], paneX + S(234), y2 + S(4), S(46),  S(22), TRUE);
                ::MoveWindow(hFaVerbs, paneX + S(282), y2,     S(220), S(26), TRUE);
                ::MoveWindow(hFaAdvChk, paneX + S(512), y2,    std::max(S(180), paneW - S(512)), S(26), TRUE);

                const int y3 = y2 + S(34);
                ::MoveWindow(hFaAddBtn, paneX,          y3, S(148), S(30), TRUE);
                ::MoveWindow(hFaRemBtn, paneX + S(156), y3, S(148), S(30), TRUE);
                ::MoveWindow(hFaWizBtn, paneX + S(312), y3, S(248), S(30), TRUE);
            }
        }

        // View 4: Shortcuts & Registry
        {
            auto v = viewCtrls(VIEW_SHORTCUTS);
            const int gridH = mainH - S(112);
            ::MoveWindow(hScGrid, paneX, contentTop, paneW, gridH, TRUE);
            if (v.size() >= 15) {
                const int y1 = contentTop + gridH + S(8);
                ::MoveWindow(v[1], paneX,          y1 + S(4), S(98),  S(22), TRUE);
                ::MoveWindow(hScName, paneX + S(102), y1,      S(160), S(26), TRUE);
                ::MoveWindow(v[3], paneX + S(270), y1 + S(4), S(74),  S(22), TRUE);
                ::MoveWindow(hScTarget, paneX + S(346), y1,    S(180), S(26), TRUE);
                ::MoveWindow(hScAddBtn, paneX + S(534), y1,    S(126), S(28), TRUE);

                const int y2 = y1 + S(36);
                ::MoveWindow(v[6],  paneX,          y2 + S(4), S(60),  S(22), TRUE);
                ::MoveWindow(hRegKey, paneX + S(62), y2,        S(180), S(26), TRUE);
                ::MoveWindow(v[8],  paneX + S(250), y2 + S(4), S(46),  S(22), TRUE);
                ::MoveWindow(hRegName,paneX + S(298), y2,       S(96),  S(26), TRUE);
                ::MoveWindow(v[10], paneX + S(402), y2 + S(4), S(44),  S(22), TRUE);
                ::MoveWindow(hRegVal, paneX + S(448), y2,       S(110), S(26), TRUE);
                ::MoveWindow(v[12], paneX + S(566), y2 + S(4), S(46),  S(22), TRUE);
                ::MoveWindow(hRegComp,paneX + S(614), y2,       S(96),  S(26), TRUE);
                ::MoveWindow(hRegAddBtn, paneX + S(718), y2,    S(118), S(28), TRUE);
            }
        }

        // View 5: C++ Wizard Designer (`SetupWizard.exe`)
        {
            auto v = viewCtrls(VIEW_CPP_WIZARD);
            const int formW = S(380);
            int y = contentTop + S(4);
            const int lw = S(136);
            if (v.size() >= 17) {
                ::MoveWindow(v[0], paneX, y + S(4), lw, S(22), TRUE);
                ::MoveWindow(hUiLayout, paneX + lw, y, formW - lw, S(120), TRUE);
                y += S(32);
                ::MoveWindow(v[2], paneX, y + S(4), lw, S(22), TRUE);
                ::MoveWindow(hUiPreviewStep, paneX + lw, y, formW - lw, S(180), TRUE);
                y += S(32);
                auto row = [&](HWND lbl, HWND ed, int rsv = 0) {
                    ::MoveWindow(lbl, paneX, y + S(4), lw, S(22), TRUE);
                    ::MoveWindow(ed,  paneX + lw, y, formW - lw - rsv, S(26), TRUE);
                    y += S(32);
                };
                row(v[4],  hUiPrim);
                row(v[6],  hUiSec);
                row(v[8],  hUiInstBtn);
                row(v[10], hUiIcon, S(86));
                ::MoveWindow(hUiIconBtn, paneX + formW - S(80), y - S(32), S(80), S(26), TRUE);
                row(v[13], hUiArt, S(86));
                ::MoveWindow(hUiArtBtn,  paneX + formW - S(80), y - S(32), S(80), S(26), TRUE);
                ::MoveWindow(hUiApply, paneX, y + S(4), formW, S(34), TRUE);
            }
            previewCanvasRc = RECT{paneX + formW + S(18), contentTop, paneX + paneW, contentTop + std::min(mainH, S(430))};
        }

        // View 6: Traditional `MSI.DLL` `Dialog` Table Designer (`370 x 270` DLU!)
        {
            auto v = viewCtrls(VIEW_MSI_DIALOGS);
            const int formW = S(386);
            int y = contentTop + S(2);
            const int lw = S(126);
            if (v.size() >= 22) {
                auto row = [&](HWND lbl, HWND ctl, int h = 25) {
                    ::MoveWindow(lbl, paneX, y + S(3), lw, S(20), TRUE);
                    ::MoveWindow(ctl, paneX + lw, y, formW - lw, S(h), TRUE);
                    y += S(28);
                };
                row(v[0], hMsiPreset, 120);
                row(v[2], hMsiDlgSelect, 180);
                row(v[4], hMsiNewId);
                row(v[6], hMsiNewTitle);
                row(v[8], hMsiNewAfter);
                ::MoveWindow(hMsiAddDlgBtn, paneX, y, formW, S(28), TRUE);
                y += S(34);

                row(v[11], hMsiCtrlId);
                row(v[13], hMsiCtrlType, 160);
                row(v[15], hMsiCtrlXywh);
                row(v[17], hMsiCtrlProp);
                row(v[19], hMsiCtrlText);
                ::MoveWindow(hMsiAddCtrlBtn, paneX, y + S(2), (formW - S(8)) / 2, S(30), TRUE);
                ::MoveWindow(hMsiIncludeBtn, paneX + (formW + S(8)) / 2, y + S(2), (formW - S(8)) / 2, S(30), TRUE);
            }
            msiCanvasRc = RECT{paneX + formW + S(18), contentTop, paneX + paneW, contentTop + std::min(mainH, S(430))};
        }

        // View 7: Signing, .MST & .MSP
        {
            auto v = viewCtrls(VIEW_SIGN_REL);
            int y = contentTop + S(4);
            const int lw = S(180), fw = std::min(S(460), paneW - lw - S(100));
            if (v.size() >= 17) {
                ::MoveWindow(hSigEnable, paneX, y, lw + fw, S(26), TRUE);
                y += S(30);
                auto row = [&](HWND lbl, HWND ed) {
                    ::MoveWindow(lbl, paneX, y + S(3), lw, S(20), TRUE);
                    ::MoveWindow(ed,  paneX + lw, y, fw, S(25), TRUE);
                    y += S(29);
                };
                row(v[1], hSigThumb); row(v[3], hSigTs); row(v[5], hSigDlib); row(v[7], hSigMeta);
                ::MoveWindow(hSigApply, paneX, y + S(2), S(240), S(30), TRUE);
                y += S(42);

                ::MoveWindow(v[10], paneX, y + S(3), lw, S(20), TRUE);
                ::MoveWindow(hEntBase, paneX + lw, y, fw, S(25), TRUE);
                ::MoveWindow(hEntBaseBtn, paneX + lw + fw + S(6), y, S(84), S(25), TRUE);
                y += S(31);
                ::MoveWindow(v[13], paneX, y + S(3), lw, S(20), TRUE);
                ::MoveWindow(hEntTarg, paneX + lw, y, fw, S(25), TRUE);
                ::MoveWindow(hEntTargBtn, paneX + lw + fw + S(6), y, S(84), S(25), TRUE);
                y += S(36);

                ::MoveWindow(hEntGenMst,    paneX,          y, S(168), S(32), TRUE);
                ::MoveWindow(hEntGenMsp,    paneX + S(176), y, S(168), S(32), TRUE);
                ::MoveWindow(hEntRelWizBtn, paneX + S(352), y, S(280), S(32), TRUE);
            }
        }

        // View 8: Full Monospace `.rsip` Source
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

    rsw::RswManifestDocument ResolvedEffectiveDoc() const {
        rsw::RswManifestDocument effectiveDoc = astDoc;
        std::string incErr;
        rsw::ResolveManifestIncludesAndVars(
            astDoc,
            hasActiveSuite ? activeSuite.variables : std::map<std::string, std::string>{},
            effectiveDoc,
            incErr);
        return effectiveDoc;
    }

    // ----------------------------------------------------------------------------
    // POPULATE INSTALLSHIELD-STYLE DESTINATION COMPUTER TREE & SETUP DESIGN TREE
    // ----------------------------------------------------------------------------
    void RebuildDestinationTreeAndPayloadGrid(const rsw::RswManifestDocument& doc) {
        TreeView_DeleteAllItems(hDestTree);
        destNodeMetas.clear();

        auto insertNode = [&](HTREEITEM hParent, const std::wstring& label, std::string root, std::string sub) -> HTREEITEM {
            const std::size_t idx = destNodeMetas.size();
            destNodeMetas.push_back({std::move(root), std::move(sub)});
            TVINSERTSTRUCTW tvi{};
            tvi.hParent      = hParent;
            tvi.hInsertAfter = TVI_LAST;
            tvi.item.mask    = TVIF_TEXT | TVIF_PARAM;
            tvi.item.pszText = const_cast<LPWSTR>(label.c_str());
            tvi.item.lParam  = static_cast<LPARAM>(idx);
            return TreeView_InsertItem(hDestTree, &tvi);
        };

        HTREEITEM hTargetPcRoot = insertNode(TVI_ROOT, L"Destination Computer (All Files)", "", "");
        struct StdRoot { const char* id; const wchar_t* label; };
        const StdRoot stdRoots[] = {
            {"INSTALLDIR",                L"[INSTALLDIR] Application Folder"},
            {"CommonFiles64Folder",       L"[CommonFiles64Folder] Common Files"},
            {"CommonAppDataFolder",       L"[CommonAppDataFolder] ProgramData"},
            {"AppDataFolder",             L"[AppDataFolder] Roaming AppData"},
            {"LocalAppDataFolder",        L"[LocalAppDataFolder] LocalAppData"},
            {"ApplicationProgramsFolder", L"[ApplicationProgramsFolder] Start Menu"},
            {"DesktopFolder",             L"[DesktopFolder] Desktop"},
            {"System64Folder",            L"[System64Folder] System32"},
            {"FontsFolder",               L"[FontsFolder] Windows Fonts"}
        };

        std::map<std::string, HTREEITEM> rootMap;
        for (const auto& sr : stdRoots) {
            HTREEITEM hR = insertNode(hTargetPcRoot, sr.label, sr.id, "");
            rootMap[rsw::AsciiLower(sr.id)] = hR;
        }

        // Add subfolders declared in `[Folder.*]`, `[Payload.*]`, and `[Component.*]`
        std::set<std::string> seenSubs;
        auto addSubItem = [&](const std::string& rt, const std::string& sb) {
            if (sb.empty()) return;
            const std::string key = rsw::AsciiLower(rt + "::" + sb);
            if (!seenSubs.insert(key).second) return;
            auto it = rootMap.find(rsw::AsciiLower(rt));
            HTREEITEM parentItem = (it != rootMap.end()) ? it->second : rootMap["installdir"];
            insertNode(parentItem, rsw::Utf8ToWide(sb), rt, sb);
            TreeView_Expand(hDestTree, parentItem, TVE_EXPAND);
        };
        for (const auto& fid : doc.GetNamedSectionIds("Folder")) {
            addSubItem(doc.GetString("Folder", fid, "TargetRoot", "INSTALLDIR"), doc.GetString("Folder", fid, "TargetSubdir"));
        }
        for (const auto& pid : doc.GetNamedSectionIds("Payload")) {
            addSubItem(doc.GetString("Payload", pid, "TargetRoot", "INSTALLDIR"), doc.GetString("Payload", pid, "TargetSubdir"));
        }
        for (const auto& cid : doc.GetNamedSectionIds("Component")) {
            addSubItem(doc.GetString("Component", cid, "TargetRoot", "INSTALLDIR"), doc.GetString("Component", cid, "TargetSubdir"));
        }
        TreeView_Expand(hDestTree, hTargetPcRoot, TVE_EXPAND);

        RefreshPayloadListViewFilter(doc);
    }

    void RefreshPayloadListViewFilter(const rsw::RswManifestDocument& doc) {
        ListView_DeleteAllItems(hPayGrid);
        int row = 0;
        auto matchesFilter = [&](const std::string& rt, const std::string& sb) -> bool {
            if (selectedDestRoot.empty()) return true;
            if (!rsw::EqualsIgnoreCase(rt, selectedDestRoot)) return false;
            if (selectedDestSubdir.empty()) return true;
            return rsw::EqualsIgnoreCase(sb, selectedDestSubdir);
        };

        auto addRow = [&](const std::string& id, const std::string& dest, const std::string& comp,
                          const std::string& feat, const std::string& kp, const std::string& src) {
            std::wstring w0 = rsw::Utf8ToWide(id),   w1 = rsw::Utf8ToWide(dest);
            std::wstring w2 = rsw::Utf8ToWide(comp), w3 = rsw::Utf8ToWide(feat);
            std::wstring w4 = rsw::Utf8ToWide(kp),   w5 = rsw::Utf8ToWide(src);
            LVITEMW it{};
            it.mask    = LVIF_TEXT;
            it.iItem   = row;
            it.pszText = w0.data();
            ListView_InsertItem(hPayGrid, &it);
            ListView_SetItemText(hPayGrid, row, 1, w1.data());
            ListView_SetItemText(hPayGrid, row, 2, w2.data());
            ListView_SetItemText(hPayGrid, row, 3, w3.data());
            ListView_SetItemText(hPayGrid, row, 4, w4.data());
            ListView_SetItemText(hPayGrid, row, 5, w5.data());
            ++row;
        };

        for (const auto& pid : doc.GetNamedSectionIds("Payload")) {
            const std::string comp = doc.GetString("Payload", pid, "Component");
            const std::string rt   = doc.GetString("Payload", pid, "TargetRoot",
                                       comp.empty() ? "INSTALLDIR" : doc.GetString("Component", comp, "TargetRoot", "INSTALLDIR"));
            const std::string sb   = doc.GetString("Payload", pid, "TargetSubdir",
                                       comp.empty() ? "" : doc.GetString("Component", comp, "TargetSubdir"));
            if (!matchesFilter(rt, sb)) continue;
            const std::string dest = sb.empty() ? ("[" + rt + "]") : ("[" + rt + "]\\" + sb);
            const std::string feat = doc.GetString("Payload", pid, "Feature", "Main");
            const std::string kp   = comp.empty()
                ? (rsw::IsPerUserProfileRootDir(rt) ? "HKCU Reg (ICE38)" : "File (UUIDv5)")
                : ("[" + doc.GetString("Component", comp, "KeyPathKind", "auto") + "]");
            addRow("[Payload." + pid + "]", dest, comp.empty() ? "(Auto UUIDv5)" : comp, feat, kp, doc.GetString("Payload", pid, "Source"));
        }

        for (const auto& fid : doc.GetNamedSectionIds("Folder")) {
            const std::string rt = doc.GetString("Folder", fid, "TargetRoot", "INSTALLDIR");
            const std::string sb = doc.GetString("Folder", fid, "TargetSubdir");
            if (!matchesFilter(rt, sb)) continue;
            const std::string comp = doc.GetString("Folder", fid, "Component");
            addRow("[Folder." + fid + "]", "[" + rt + "]\\" + sb,
                   comp.empty() ? "(Auto CreateFolder)" : comp,
                   doc.GetString("Folder", fid, "Feature", "Main"),
                   "Folder (ICE18)", "CreateFolder + RemoveFolder");
        }
    }

    void RebuildSetupDesignTree(const rsw::RswManifestDocument& doc) {
        TreeView_DeleteAllItems(hDesignTree);
        auto addTv = [&](HTREEITEM hParent, const std::string& utf8Label) -> HTREEITEM {
            std::wstring wl = rsw::Utf8ToWide(utf8Label);
            TVINSERTSTRUCTW tvi{};
            tvi.hParent      = hParent;
            tvi.hInsertAfter = TVI_LAST;
            tvi.item.mask    = TVIF_TEXT;
            tvi.item.pszText = wl.data();
            return TreeView_InsertItem(hDesignTree, &tvi);
        };

        std::map<std::string, HTREEITEM> featNodes;
        auto featIds = doc.GetNamedSectionIds("Feature");
        if (featIds.empty()) {
            featNodes["main"] = addTv(TVI_ROOT, "Feature: Main (Level 1 — Default)");
        } else {
            for (const auto& fid : featIds) {
                const std::string par = doc.GetString("Feature", fid, "Parent");
                HTREEITEM hPar = TVI_ROOT;
                if (!par.empty() && featNodes.count(rsw::AsciiLower(par))) {
                    hPar = featNodes[rsw::AsciiLower(par)];
                }
                const std::string lbl = "Feature: " + fid + " — " + doc.GetString("Feature", fid, "Title", fid) +
                                        " (Level " + doc.GetString("Feature", fid, "Level", "1") + ")";
                featNodes[rsw::AsciiLower(fid)] = addTv(hPar, lbl);
            }
        }

        auto getFeatNode = [&](const std::string& fid) -> HTREEITEM {
            auto it = featNodes.find(rsw::AsciiLower(fid));
            if (it != featNodes.end()) return it->second;
            HTREEITEM hNew = addTv(TVI_ROOT, "Feature: " + fid);
            featNodes[rsw::AsciiLower(fid)] = hNew;
            return hNew;
        };

        std::map<std::string, HTREEITEM> compNodes;
        for (const auto& cid : doc.GetNamedSectionIds("Component")) {
            const std::string feat = doc.GetString("Component", cid, "Feature", "Main");
            const std::string rt   = doc.GetString("Component", cid, "TargetRoot", "INSTALLDIR");
            const std::string kp   = doc.GetString("Component", cid, "KeyPathKind", "auto");
            const std::string kpt  = doc.GetString("Component", cid, "KeyPathTarget");
            std::string lbl = "Component: " + cid + "  [" + rt + "]  •  KeyPath: " + kp;
            if (!kpt.empty()) lbl += " (" + kpt + ")";
            HTREEITEM hC = addTv(getFeatNode(feat), lbl);
            compNodes[rsw::AsciiLower(cid)] = hC;
        }

        for (const auto& pid : doc.GetNamedSectionIds("Payload")) {
            const std::string comp = doc.GetString("Payload", pid, "Component");
            const std::string src  = doc.GetString("Payload", pid, "Source");
            if (!comp.empty() && compNodes.count(rsw::AsciiLower(comp))) {
                addTv(compNodes[rsw::AsciiLower(comp)], "Payload." + pid + " -> " + src);
            } else {
                const std::string feat = doc.GetString("Payload", pid, "Feature", "Main");
                addTv(getFeatNode(feat), "[Auto UUIDv5 Component] Payload." + pid + " (KeyFile) -> " + src);
            }
        }

        for (const auto& [k, hNode] : featNodes) {
            (void)k;
            TreeView_Expand(hDesignTree, hNode, TVE_EXPAND);
        }
        for (const auto& [k, hNode] : compNodes) {
            (void)k;
            TreeView_Expand(hDesignTree, hNode, TVE_EXPAND);
        }
    }

    void SyncAllViewsFromAst() {
        const auto effectiveDoc = ResolvedEffectiveDoc();

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

        RebuildDestinationTreeAndPayloadGrid(effectiveDoc);
        RebuildSetupDesignTree(effectiveDoc);

        // Populate View 3: File Associations
        ListView_DeleteAllItems(hFaGrid);
        int faRow = 0;
        for (const auto& aid : effectiveDoc.GetNamedSectionIds("FileAssociation")) {
            std::wstring w0 = rsw::Utf8ToWide(effectiveDoc.GetString("FileAssociation", aid, "Extension"));
            std::wstring w1 = rsw::Utf8ToWide(effectiveDoc.GetString("FileAssociation", aid, "ProgId"));
            std::wstring w2 = rsw::Utf8ToWide(effectiveDoc.GetString("FileAssociation", aid, "Description"));
            std::wstring w3 = rsw::Utf8ToWide(effectiveDoc.GetString("FileAssociation", aid, "TargetFile"));
            std::wstring w4 = rsw::Utf8ToWide(effectiveDoc.GetString("FileAssociation", aid, "Verbs", "open:&Open:\"%1\""));
            std::wstring w5 = effectiveDoc.GetBool("FileAssociation", aid, "Advertise", false)
                ? L"Advertised MSI Tables"
                : L"Clean HKMU\\Classes";
            LVITEMW it{LVIF_TEXT, faRow, 0, 0, 0, w0.data(), 0, 0, 0, 0};
            ListView_InsertItem(hFaGrid, &it);
            ListView_SetItemText(hFaGrid, faRow, 1, w1.data());
            ListView_SetItemText(hFaGrid, faRow, 2, w2.data());
            ListView_SetItemText(hFaGrid, faRow, 3, w3.data());
            ListView_SetItemText(hFaGrid, faRow, 4, w4.data());
            ListView_SetItemText(hFaGrid, faRow, 5, w5.data());
            ++faRow;
        }
        const std::string mainExe = effectiveDoc.GetString("Install", "", "MainExecutable");
        if (!mainExe.empty()) {
            ::SetWindowTextW(hFaTarget, rsw::Utf8ToWide(mainExe).c_str());
        }

        // Populate View 4: Shortcuts & Registry
        ListView_DeleteAllItems(hScGrid);
        int sRow = 0;
        for (const auto& sid : effectiveDoc.GetNamedSectionIds("Shortcut")) {
            std::wstring k = L"Shortcut";
            std::wstring wId = rsw::Utf8ToWide(sid);
            std::wstring wNm = rsw::Utf8ToWide(effectiveDoc.GetString("Shortcut", sid, "Name"));
            std::wstring wTg = rsw::Utf8ToWide(effectiveDoc.GetString("Shortcut", sid, "TargetFile"));
            std::wstring wCp = L"(Auto ICE38 HKCU)";
            LVITEMW it{LVIF_TEXT, sRow, 0, 0, 0, k.data(), 0, 0, 0, 0};
            ListView_InsertItem(hScGrid, &it);
            ListView_SetItemText(hScGrid, sRow, 1, wId.data());
            ListView_SetItemText(hScGrid, sRow, 2, wNm.data());
            ListView_SetItemText(hScGrid, sRow, 3, wTg.data());
            ListView_SetItemText(hScGrid, sRow, 4, wCp.data());
            ++sRow;
        }
        for (const auto& rid : effectiveDoc.GetNamedSectionIds("Registry")) {
            std::wstring k = L"Registry";
            std::wstring wId = rsw::Utf8ToWide(rid);
            std::wstring wKy = rsw::Utf8ToWide(effectiveDoc.GetString("Registry", rid, "Key") + "\\" + effectiveDoc.GetString("Registry", rid, "Name"));
            std::wstring wVl = rsw::Utf8ToWide(effectiveDoc.GetString("Registry", rid, "Value"));
            std::string rcStr = effectiveDoc.GetString("Registry", rid, "Component");
            std::wstring wCp = rcStr.empty() ? L"(Auto 64-bit)" : rsw::Utf8ToWide(rcStr);
            LVITEMW it{LVIF_TEXT, sRow, 0, 0, 0, k.data(), 0, 0, 0, 0};
            ListView_InsertItem(hScGrid, &it);
            ListView_SetItemText(hScGrid, sRow, 1, wId.data());
            ListView_SetItemText(hScGrid, sRow, 2, wKy.data());
            ListView_SetItemText(hScGrid, sRow, 3, wVl.data());
            ListView_SetItemText(hScGrid, sRow, 4, wCp.data());
            ++sRow;
        }

        // Populate View 5: C++ Wizard Designer
        const std::string layStr = rsw::AsciiLower(effectiveDoc.GetString("UI", "", "WizardLayout", "standard"));
        int laySel = 0;
        if (layStr == "aerowizard")  laySel = 1;
        if (layStr == "autorun16x9") laySel = 2;
        ::SendMessageW(hUiLayout, CB_SETCURSEL, laySel, 0);
        if (laySel == 2) {
            ::SendMessageW(hUiPreviewStep, CB_SETCURSEL, 4, 0);
        } else if (::SendMessageW(hUiPreviewStep, CB_GETCURSEL, 0, 0) == 4) {
            ::SendMessageW(hUiPreviewStep, CB_SETCURSEL, 1, 0);
        }
        ::SetWindowTextW(hUiPrim,    rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "AccentPrimaryHex", "#2563EB")).c_str());
        ::SetWindowTextW(hUiSec,     rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "AccentSecondaryHex", "#FBBF24")).c_str());
        ::SetWindowTextW(hUiInstBtn, rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "InstallButtonText", "&Install")).c_str());
        ::SetWindowTextW(hUiIcon,    rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "IconFile")).c_str());
        ::SetWindowTextW(hUiArt,     rsw::Utf8ToWide(effectiveDoc.GetString("UI", "", "ArtworkPng")).c_str());
        RefreshPreviewAssetsFromDoc(effectiveDoc);

        // Populate View 6: MSI.DLL Dialog Designer
        const bool msiMin = rsw::EqualsIgnoreCase(effectiveDoc.GetString("UI", "", "MsiDialogPreset", "express_custom"), "minimal");
        ::SendMessageW(hMsiPreset, CB_SETCURSEL, msiMin ? 1 : 0, 0);

        wchar_t prevMsiDlg[128]{};
        ::GetWindowTextW(hMsiDlgSelect, prevMsiDlg, 128);
        ::SendMessageW(hMsiDlgSelect, CB_RESETCONTENT, 0, 0);
        const wchar_t* stdDlgs[] = {
            L"WelcomeDlg", L"LicenseAgreementDlg", L"SetupTypeDlg",
            L"CustomizeDlg", L"InstallDirDlg", L"ShortcutsDlg",
            L"RepairModeDlg", L"VerifyReadyDlg", L"ExitDialog"
        };
        int selDlgIdx = 2;
        int curIdx = 0;
        for (const wchar_t* sd : stdDlgs) {
            ::SendMessageW(hMsiDlgSelect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(sd));
            if (std::wcscmp(sd, prevMsiDlg) == 0) selDlgIdx = curIdx;
            ++curIdx;
        }
        for (const auto& cd : effectiveDoc.GetNamedSectionIds("MsiDialog")) {
            std::wstring wcd = rsw::Utf8ToWide(cd);
            ::SendMessageW(hMsiDlgSelect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wcd.c_str()));
            if (wcd == prevMsiDlg) selDlgIdx = curIdx;
            ++curIdx;
        }
        ::SendMessageW(hMsiDlgSelect, CB_SETCURSEL, selDlgIdx, 0);

        // Populate View 7: Signing
        ::SendMessageW(hSigEnable, BM_SETCHECK, effectiveDoc.GetBool("Signing", "", "Enabled", false) ? BST_CHECKED : BST_UNCHECKED, 0);
        ::SetWindowTextW(hSigThumb, rsw::Utf8ToWide(effectiveDoc.GetString("Signing", "", "Thumbprint")).c_str());
        ::SetWindowTextW(hSigTs,    rsw::Utf8ToWide(effectiveDoc.GetString("Signing", "", "TimestampUrl", "http://timestamp.acs.microsoft.com")).c_str());
        ::SetWindowTextW(hSigDlib,  rsw::Utf8ToWide(effectiveDoc.GetString("Signing", "", "DlibPath")).c_str());
        ::SetWindowTextW(hSigMeta,  rsw::Utf8ToWide(effectiveDoc.GetString("Signing", "", "MetadataJson")).c_str());
    }

    void RefreshPreviewAssetsFromDoc(const rsw::RswManifestDocument& doc) {
        ReleasePreviewAssets();
        const std::filesystem::path baseDir = currentFilePath.empty()
            ? std::filesystem::current_path()
            : currentFilePath.parent_path();
        const std::string arch = SelectedArch();
        auto resolveP = [&](std::string_view raw) -> std::filesystem::path {
            if (raw.empty()) return {};
            const std::string exp = rsw::ExpandArchToken(raw, arch);
            std::filesystem::path p(rsw::Utf8ToWide(exp));
            return (p.is_absolute() ? p : (baseDir / p)).lexically_normal();
        };

        std::error_code ec;
        const auto icoPath = resolveP(doc.GetString("UI", "", "IconFile"));
        if (!icoPath.empty() && std::filesystem::exists(icoPath, ec)) {
            hPreviewIcon = static_cast<HICON>(::LoadImageW(
                nullptr, icoPath.wstring().c_str(), IMAGE_ICON, 64, 64, LR_LOADFROMFILE | LR_DEFAULTCOLOR));
        }
        const auto artPath = resolveP(doc.GetString("UI", "", "ArtworkPng"));
        if (!artPath.empty() && std::filesystem::exists(artPath, ec)) {
            std::vector<std::uint8_t> bytes;
            std::string ignoreErr;
            if (rsw::ReadBinaryFileBytes(artPath, bytes, ignoreErr) && !bytes.empty()) {
                hPreviewArt = rsw::ui::DecodeImageBytesToHBitmap(bytes.data(), bytes.size(), previewArtW, previewArtH);
            }
        }
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
        selectedDestRoot.clear();
        selectedDestSubdir.clear();
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
        return rsw::TrimAscii(rsw::WideToUtf8(buf));
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
        const LRESULT lay = ::SendMessageW(hUiLayout, CB_GETCURSEL, 0, 0);
        const char* layStr = (lay == 1) ? "aerowizard" : ((lay == 2) ? "autorun16x9" : "standard");
        astDoc.SetString("UI", "", "WizardLayout",      layStr);
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
        AppendConsole(L"[AST] Updated [UI] C++ SetupWizard branding & refreshed live preview.\r\n");
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
        return rsw::ide_wizards::BrowsePathModal(hWnd, folderMode, filter, outPath);
    }

    void AddPayloadToSelectedDestFolder(bool pickDirectory) {
        std::wstring chosen;
        if (!PickPathDialog(pickDirectory, L"All Files (*.*)\0*.*\0", chosen)) return;
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        const int idx = static_cast<int>(astDoc.GetNamedSectionIds("Payload").size() + 1);
        const std::string id = (pickDirectory ? "Dir" : "File") + std::to_string(idx);
        const std::string rt = selectedDestRoot.empty() ? "INSTALLDIR" : selectedDestRoot;
        astDoc.SetString("Payload", id, "Source",     rsw::WideToUtf8(chosen));
        astDoc.SetString("Payload", id, "TargetRoot", rt);
        if (!selectedDestSubdir.empty()) {
            astDoc.SetString("Payload", id, "TargetSubdir", selectedDestSubdir);
        }
        astDoc.SetString("Payload", id, "Feature", "Main");
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        AppendConsole(L"[AST] Added [Payload." + rsw::Utf8ToWide(id) + L"] into [" + rsw::Utf8ToWide(rt) + L"].\r\n");
    }

    void CreateExplicitDestinationSubfolder() {
        const std::string sub = ReadCtlUtf8(hDestSubEdit);
        if (sub.empty()) return;
        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        const std::string rt = selectedDestRoot.empty() ? "INSTALLDIR" : selectedDestRoot;
        const std::string fullSub = selectedDestSubdir.empty() ? sub : (selectedDestSubdir + "\\" + sub);
        const std::string fid = "Dir" + std::to_string(astDoc.GetNamedSectionIds("Folder").size() + 1);
        astDoc.SetString("Folder", fid, "TargetRoot",        rt);
        astDoc.SetString("Folder", fid, "TargetSubdir",      fullSub);
        astDoc.SetString("Folder", fid, "Feature",           "Main");
        astDoc.SetString("Folder", fid, "RemoveOnUninstall", "true");
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        AppendConsole(L"[AST] Added [Folder." + rsw::Utf8ToWide(fid) + L"] (CreateFolder + ICE64 RemoveFolder).\r\n");
    }

    void RemoveSelectedDestItem() {
        const int sel = ListView_GetNextItem(hPayGrid, -1, LVNI_SELECTED);
        if (sel < 0) return;
        wchar_t labelBuf[160]{};
        ListView_GetItemText(hPayGrid, sel, 0, labelBuf, static_cast<int>(std::size(labelBuf)));
        std::string raw = rsw::WideToUtf8(labelBuf); // `"[Payload.App]"` or `"[Folder.Dir1]"`
        if (raw.size() > 2 && raw.front() == '[' && raw.back() == ']') {
            raw = raw.substr(1, raw.size() - 2);
        }
        auto dot = raw.find('.');
        if (dot == std::string::npos) return;
        const std::string secPfx = raw.substr(0, dot);
        const std::string secId  = raw.substr(dot + 1);

        astDoc = rsw::ParseManifestText(ReadSourceEditorUtf8(), currentFilePath);
        std::vector<rsw::RswAstLine> kept;
        for (const auto& ln : astDoc.lines) {
            if ((ln.kind == rsw::AstLineKind::SectionHeader || ln.kind == rsw::AstLineKind::KeyValue) &&
                rsw::EqualsIgnoreCase(ln.sectionPrefix, secPfx) &&
                rsw::EqualsIgnoreCase(ln.sectionItemId, secId)) {
                continue;
            }
            kept.push_back(ln);
        }
        astDoc.lines = std::move(kept);
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        AppendConsole(L"[AST] Removed [" + rsw::Utf8ToWide(raw) + L"].\r\n");
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
            currentFilePath   = buf;
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
            AppendConsole(L"[PASS] 0 errors — Manifest & Microsoft Component Rules (Single-Directory, ICE18, ICE38, ICE80) verified for '" +
                          rsw::Utf8ToWide(rep.resolvedArchitecture) + L"'.\r\n");
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

    void InsertIncludePresetDialog() {
        const std::filesystem::path tplRoot = ideExeDir.parent_path().parent_path() / L"templates" / L"dialogs";
        wchar_t buf[MAX_PATH * 2]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize     = sizeof(ofn);
        ofn.hwndOwner       = hWnd;
        ofn.lpstrInitialDir = tplRoot.wstring().c_str();
        ofn.lpstrFilter     = L"RaggieSoft Include Presets (*.rsii)\0*.rsii\0All Files (*.*)\0*.*\0";
        ofn.lpstrFile       = buf;
        ofn.nMaxFile        = static_cast<DWORD>(std::size(buf));
        ofn.Flags           = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!::GetOpenFileNameW(&ofn)) return;

        std::string raw = ReadSourceEditorUtf8();
        const std::string incLine = "@include \"" + rsw::WideToUtf8(buf) + "\"\r\n\r\n";
        raw = incLine + raw;
        astDoc = rsw::ParseManifestText(raw, currentFilePath);
        SyncSourceEditorFromAst();
        SyncAllViewsFromAst();
        ::InvalidateRect(hWnd, nullptr, FALSE);
        AppendConsole(L"[AST] Prepended @include \"" + std::wstring(buf) + L"\".\r\n");
    }

    // ----------------------------------------------------------------------------
    // LIVE CANVASES: VIEW 5 (`SetupWizard.exe` C++20) & VIEW 6 (`MSI.DLL` 370x270)
    // ----------------------------------------------------------------------------
    void PaintLiveWizardPreview(HDC hdcOut, const RECT& destRc, UINT dpi) {
        const int pvW = destRc.right - destRc.left;
        const int pvH = destRc.bottom - destRc.top;
        if (pvW < 180 || pvH < 180) return;

        const UINT pvDpi = static_cast<UINT>(std::clamp(pvW * 96 / 580, 68, static_cast<int>(dpi)));
        auto PS = [&](int px) { return rsw::ui::ScalePx(px, pvDpi, 100); };

        const COLORREF prim = rsw::ui::ParseHexColor(ReadCtlUtf8(hUiPrim), RGB(37, 99, 235));
        const COLORREF sec  = rsw::ui::ParseHexColor(ReadCtlUtf8(hUiSec),  RGB(251, 191, 36));
        rsw::ui::RswTheme pvTheme{};
        pvTheme.Refresh(prim, sec);
        pvTheme.textScalePercent = 100;

        HDC pvDc       = ::CreateCompatibleDC(hdcOut);
        HBITMAP pvBmp  = ::CreateCompatibleBitmap(hdcOut, pvW, pvH);
        HGDIOBJ oldBmp = ::SelectObject(pvDc, pvBmp);

        RECT localRc{0, 0, pvW, pvH};
        ::FillRect(pvDc, &localRc, pvTheme.hbrWindow);
        ::SetBkMode(pvDc, TRANSPARENT);

        Gdiplus::Graphics pvG(pvDc);
        pvG.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        pvG.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        pvG.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        const std::wstring prodName = rsw::Utf8ToWide(ReadCtlUtf8(hPkgName).empty() ? "My Native Application" : ReadCtlUtf8(hPkgName));
        const std::wstring pubName  = rsw::Utf8ToWide(ReadCtlUtf8(hPkgPub).empty()  ? "RaggieSoft" : ReadCtlUtf8(hPkgPub));
        const std::wstring tagline  = rsw::Utf8ToWide(ReadCtlUtf8(hPkgTag).empty()  ? "Fast, Accessible Windows Software" : ReadCtlUtf8(hPkgTag));
        const std::wstring webLbl   = rsw::Utf8ToWide(ReadCtlUtf8(hPkgWeb));
        const std::wstring ctaLbl   = rsw::Utf8ToWide(ReadCtlUtf8(hUiInstBtn).empty() ? "&Install" : ReadCtlUtf8(hUiInstBtn));

        const LRESULT laySel  = ::SendMessageW(hUiLayout, CB_GETCURSEL, 0, 0);
        const bool aeroMode   = (laySel == 1);
        const LRESULT stepSel = ::SendMessageW(hUiPreviewStep, CB_GETCURSEL, 0, 0);
        const int stepIdx     = (stepSel == CB_ERR) ? 1 : static_cast<int>(stepSel);
        const int footH       = PS(50);
        const int footY       = pvH - footH;

        rsw::ui::DrawGlassFooterBar(pvDc, pvG, pvW, pvH, footH, pvW - PS(198), pvDpi, pvTheme, pubName, webLbl);

        if (stepIdx == 4) {
            const int stageH = std::max(PS(140), footY - PS(94));
            RECT stageRc{0, 0, pvW, stageH};
            rsw::ui::FillHorizontalGradient(pvDc, stageRc, pvTheme.bannerNavyLeft, pvTheme.bannerNavyRight);
            if (hPreviewArt) {
                rsw::ui::DrawAlphaBitmapFit(pvDc, hPreviewArt, previewArtW, previewArtH, stageRc);
            } else {
                rsw::ui::DrawBrandSquircleBadge(pvG, static_cast<float>(PS(20)), static_cast<float>(PS(22)), static_cast<float>(PS(42)), pvTheme, pubName);
                HFONT fHead = rsw::ui::CreateScaledFont(L"Segoe UI", 14, FW_BOLD, pvDpi, 100);
                HGDIOBJ oF  = ::SelectObject(pvDc, fHead);
                ::SetTextColor(pvDc, pvTheme.bannerTitleText);
                RECT tRc{PS(20), PS(76), pvW - PS(20), PS(122)};
                ::DrawTextW(pvDc, prodName.c_str(), -1, &tRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                ::SelectObject(pvDc, oF);
                ::DeleteObject(fHead);
            }
            rsw::ui::DrawMultiStopAccentBar(pvG, 0.0f, static_cast<float>(stageH), static_cast<float>(pvW), static_cast<float>(PS(3)), pvTheme);

            const int cw = (pvW - PS(34)) / 2;
            const int ch = PS(36);
            const int ry1 = stageH + PS(8);
            const int ry2 = ry1 + ch + PS(5);
            RECT c0{PS(12),      ry1, PS(12) + cw,     ry1 + ch};
            RECT c1{PS(22) + cw, ry1, PS(22) + cw * 2, ry1 + ch};
            RECT c2{PS(12),      ry2, PS(12) + cw,     ry2 + ch};
            RECT c3{PS(22) + cw, ry2, PS(22) + cw * 2, ry2 + ch};
            rsw::ui::DrawAutorunChoiceCell(pvDc, pvG, c0, pvDpi, pvTheme, L"&Install Edition",  L"Add to local library", true, true, false);
            rsw::ui::DrawAutorunChoiceCell(pvDc, pvG, c1, pvDpi, pvTheme, L"View &ReadMe",      L"Edition notes & credits", false, false, false);
            rsw::ui::DrawAutorunChoiceCell(pvDc, pvG, c2, pvDpi, pvTheme, L"C&hange Folder...", L"Pick custom library path", false, false, false);
            rsw::ui::DrawAutorunChoiceCell(pvDc, pvG, c3, pvDpi, pvTheme, L"E&xit Installer",   L"Close without changes", false, false, false);
        } else {
            rsw::ui::GlassGlyph stepGlyph = rsw::ui::GlassGlyph::Sliders;
            COLORREF stepCol              = rsw::ui::brand::kVioletTune;
            const wchar_t* kicker         = L"Choose how you would like to install";
            if (stepIdx == 0) { stepGlyph = rsw::ui::GlassGlyph::Sparkle;   stepCol = rsw::ui::brand::kRaggieBlue; kicker = L"Welcome to the Setup Wizard"; }
            if (stepIdx == 2) { stepGlyph = rsw::ui::GlassGlyph::Reinstall; stepCol = rsw::ui::brand::kRaggieBlue; kicker = L"Repair & Verify Installation"; }
            if (stepIdx == 3) { stepGlyph = rsw::ui::GlassGlyph::Gear;      stepCol = rsw::ui::brand::kRaggieBlue; kicker = L"Applying System Changes"; }

            if (aeroMode) {
                rsw::ui::DrawAeroWizardTopHeader(pvDc, pvG, pvW, pvDpi, pvTheme, prodName + L" Setup", kicker, hPreviewIcon, stepGlyph, stepCol, pubName);
                // Circular Aero `<-` Back glyph preview in top-left!
                Gdiplus::SolidBrush bkBr(rsw::ui::ToGpColor(pvTheme.accentPrimary, 210));
                pvG.FillEllipse(&bkBr, static_cast<float>(PS(14)), static_cast<float>(PS(7)), static_cast<float>(PS(24)), static_cast<float>(PS(24)));
                Gdiplus::Pen arrPen(Gdiplus::Color(255, 255, 255, 255), 2.0f);
                pvG.DrawLine(&arrPen, static_cast<float>(PS(31)), static_cast<float>(PS(19)), static_cast<float>(PS(20)), static_cast<float>(PS(19)));
                pvG.DrawLine(&arrPen, static_cast<float>(PS(25)), static_cast<float>(PS(14)), static_cast<float>(PS(20)), static_cast<float>(PS(19)));
                pvG.DrawLine(&arrPen, static_cast<float>(PS(25)), static_cast<float>(PS(24)), static_cast<float>(PS(20)), static_cast<float>(PS(19)));
            } else {
                rsw::ui::DrawGlassTopHeader(pvDc, pvG, pvW, pvDpi, pvTheme, pubName, rsw::ui::UpperWide(kicker), prodName, hPreviewIcon, stepGlyph, stepCol);
            }

            const int bTop = PS(100);
            const int cx   = PS(16);
            const int cw   = pvW - PS(32);

            if (stepIdx == 0) {
                RECT heroRc{cx, bTop, cx + cw, bTop + PS(68)};
                rsw::ui::DrawHeroBrandCard(pvDc, pvG, heroRc, pvDpi, pvTheme, pubName, tagline, webLbl, L"OFFICIAL INSTALLER", hPreviewIcon);
                HFONT fH = rsw::ui::CreateScaledFont(L"Segoe UI", 13, FW_BOLD, pvDpi, 100);
                HFONT fB = rsw::ui::CreateScaledFont(L"Segoe UI",  9, FW_NORMAL, pvDpi, 100);
                HGDIOBJ oF = ::SelectObject(pvDc, fH);
                ::SetTextColor(pvDc, pvTheme.textPrimary);
                const std::wstring wHead = L"Welcome to " + prodName + L" Setup";
                RECT hRc{cx + PS(4), bTop + PS(80), cx + cw, bTop + PS(110)};
                ::DrawTextW(pvDc, wHead.c_str(), -1, &hRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                ::SelectObject(pvDc, fB);
                ::SetTextColor(pvDc, pvTheme.textSecondary);
                RECT pRc{cx + PS(4), bTop + PS(112), cx + cw, footY - PS(12)};
                ::DrawTextW(pvDc, L"100% WCAG 2.2 AA Accessible Win32 C++20 Wizard — AeroWizard & Glass modes, keyboard focus rings, High Contrast & Dark Mode.", -1, &pRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
                ::SelectObject(pvDc, oF);
                ::DeleteObject(fH);
                ::DeleteObject(fB);
            } else if (stepIdx == 1) {
                const int ch = std::min(PS(68), (footY - bTop - PS(24)) / 3);
                RECT r0{cx, bTop,                    cx + cw, bTop + ch};
                RECT r1{cx, bTop + ch + PS(8),       cx + cw, bTop + ch * 2 + PS(8)};
                RECT r2{cx, bTop + (ch + PS(8)) * 2, cx + cw, bTop + ch * 3 + PS(16)};
                rsw::ui::DrawAccessibleOptionCard(pvDc, pvG, r0, pvDpi, pvTheme, rsw::ui::GlassGlyph::Bolt,    rsw::ui::brand::kSkyCyan,    rsw::ui::brand::kWarmGold, L"&Express Setup (Recommended)", L"One-click setup into the default folder with recommended shortcuts.", true, true, false);
                rsw::ui::DrawAccessibleOptionCard(pvDc, pvG, r1, pvDpi, pvTheme, rsw::ui::GlassGlyph::Cube,    rsw::ui::brand::kRaggieBlue, rsw::ui::brand::kSkyCyan,  L"Co&mpact Core Setup",          L"Installs essential application files with minimal disk footprint.", false, false, false);
                rsw::ui::DrawAccessibleOptionCard(pvDc, pvG, r2, pvDpi, pvTheme, rsw::ui::GlassGlyph::Sliders, rsw::ui::brand::kVioletTune, rsw::ui::brand::kWarmGold, L"C&ustom Setup",                 L"Choose features, destination folder, and shortcuts step by step.", false, false, false);
            } else if (stepIdx == 2) {
                RECT r0{cx, bTop + PS(8),  cx + cw, bTop + PS(84)};
                RECT r1{cx, bTop + PS(96), cx + cw, bTop + PS(172)};
                rsw::ui::DrawAccessibleOptionCard(pvDc, pvG, r0, pvDpi, pvTheme, rsw::ui::GlassGlyph::ShieldCheck, rsw::ui::brand::kEmeraldOk,  rsw::ui::brand::kWarmGold, L"&Quick Check & Repair (Recommended)", L"Verifies installed files and restores only missing or damaged files.", true, true, false);
                rsw::ui::DrawAccessibleOptionCard(pvDc, pvG, r1, pvDpi, pvTheme, rsw::ui::GlassGlyph::Reinstall,   rsw::ui::brand::kRaggieBlue, rsw::ui::brand::kSkyCyan,  L"&Full Reinstall (Refresh All Files)", L"Replaces every program file and shortcut from the setup package.", false, false, false);
            } else {
                RECT heroRc{cx, bTop, cx + cw, bTop + PS(66)};
                rsw::ui::DrawHeroBrandCard(pvDc, pvG, heroRc, pvDpi, pvTheme, pubName, tagline, webLbl, L"CONFIGURING SYSTEM", hPreviewIcon);
                Gdiplus::SolidBrush boxBr(rsw::ui::ToGpColor(pvTheme.cardBg));
                Gdiplus::Pen boxPen(rsw::ui::ToGpColor(pvTheme.borderSubtle), 1.0f);
                rsw::ui::FillAndStrokeRoundRect(pvG, static_cast<float>(cx), static_cast<float>(bTop + PS(78)), static_cast<float>(cw), static_cast<float>(PS(104)), static_cast<float>(PS(10)), &boxBr, &boxPen);
                Gdiplus::SolidBrush trackBr(rsw::ui::ToGpColor(pvTheme.borderSubtle));
                Gdiplus::SolidBrush fillBr(rsw::ui::ToGpColor(rsw::ui::brand::kEmeraldOk));
                rsw::ui::FillAndStrokeRoundRect(pvG, static_cast<float>(cx + PS(16)), static_cast<float>(bTop + PS(140)), static_cast<float>(cw - PS(32)), static_cast<float>(PS(18)), static_cast<float>(PS(9)), &trackBr);
                rsw::ui::FillAndStrokeRoundRect(pvG, static_cast<float>(cx + PS(16)), static_cast<float>(bTop + PS(140)), static_cast<float>((cw - PS(32)) * 68 / 100), static_cast<float>(PS(18)), static_cast<float>(PS(9)), &fillBr);
            }
        }

        Gdiplus::SolidBrush ctaBr(rsw::ui::ToGpColor(prim));
        rsw::ui::FillAndStrokeRoundRect(
            pvG, static_cast<float>(pvW - PS(184)), static_cast<float>(footY + PS(10)),
            static_cast<float>(PS(96)), static_cast<float>(footH - PS(20)), static_cast<float>(PS(6)), &ctaBr);
        HFONT fBtn = rsw::ui::CreateScaledFont(L"Segoe UI", 9, FW_BOLD, pvDpi, 100);
        HGDIOBJ oF = ::SelectObject(pvDc, fBtn);
        ::SetTextColor(pvDc, RGB(255, 255, 255));
        RECT ctaRc{pvW - PS(184), footY + PS(10), pvW - PS(88), footY + footH - PS(10)};
        ::DrawTextW(pvDc, ctaLbl.c_str(), -1, &ctaRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        ::SelectObject(pvDc, oF);
        ::DeleteObject(fBtn);

        Gdiplus::Pen outerFrame(rsw::ui::ToGpColor(pvTheme.accentPrimary, 210), 1.5f);
        pvG.DrawRectangle(&outerFrame, 0.5f, 0.5f, static_cast<float>(pvW) - 1.0f, static_cast<float>(pvH) - 1.0f);

        ::BitBlt(hdcOut, destRc.left, destRc.top, pvW, pvH, pvDc, 0, 0, SRCCOPY);
        ::SelectObject(pvDc, oldBmp);
        ::DeleteObject(pvBmp);
        ::DeleteDC(pvDc);
    }

    /// Renders a live `370 x 270` DLU Windows Installer (`MSI.DLL`) Wizard 97 Form Designer Canvas!
    void PaintMsiDialogDesignerCanvas(HDC hdcOut, const RECT& destRc, UINT dpi) {
        const int maxW = destRc.right - destRc.left;
        const int maxH = destRc.bottom - destRc.top;
        if (maxW < 200 || maxH < 160) return;

        // Maintain exact `370 : 270` Windows Installer dialog-unit aspect ratio!
        int cw = maxW;
        int ch = cw * 270 / 370;
        if (ch > maxH) {
            ch = maxH;
            cw = ch * 370 / 270;
        }
        auto DX = [&](int dluX) { return dluX * cw / 370; };
        auto DY = [&](int dluY) { return dluY * ch / 270; };

        HDC dc         = ::CreateCompatibleDC(hdcOut);
        HBITMAP bmp    = ::CreateCompatibleBitmap(hdcOut, cw, ch);
        HGDIOBJ oldBmp = ::SelectObject(dc, bmp);

        // Classic `msiexec.exe` Wizard 97 surface (`#F0F0F0` body + `#FFFFFF` top 44-DLU banner!)
        HBRUSH hGray  = ::CreateSolidBrush(RGB(240, 240, 240));
        HBRUSH hWhite = ::CreateSolidBrush(RGB(255, 255, 255));
        RECT fullRc{0, 0, cw, ch};
        ::FillRect(dc, &fullRc, hGray);

        wchar_t dlgNameBuf[128]{};
        ::GetWindowTextW(hMsiDlgSelect, dlgNameBuf, 128);
        const std::string dlgId = rsw::WideToUtf8(dlgNameBuf);
        const auto doc = ResolvedEffectiveDoc();

        const bool fullBleedWelcome = (dlgId == "WelcomeDlg" || dlgId == "ExitDialog");
        if (fullBleedWelcome) {
            RECT sideRc{0, 0, DX(122), DY(234)};
            rsw::ui::FillHorizontalGradient(dc, sideRc, RGB(11, 19, 43), RGB(37, 99, 235));
            RECT rightWhite{DX(122), 0, cw, DY(234)};
            ::FillRect(dc, &rightWhite, hWhite);
        } else {
            RECT banRc{0, 0, cw, DY(44)};
            ::FillRect(dc, &banRc, hWhite);
            HPEN lPen = ::CreatePen(PS_SOLID, 1, RGB(180, 188, 198));
            HGDIOBJ oP = ::SelectObject(dc, lPen);
            ::MoveToEx(dc, 0, DY(44), nullptr); ::LineTo(dc, cw, DY(44));
            ::SelectObject(dc, oP);
            ::DeleteObject(lPen);
        }

        HPEN botPen = ::CreatePen(PS_SOLID, 1, RGB(180, 188, 198));
        HGDIOBJ oP2 = ::SelectObject(dc, botPen);
        ::MoveToEx(dc, 0, DY(234), nullptr); ::LineTo(dc, cw, DY(234));
        ::SelectObject(dc, oP2);
        ::DeleteObject(botPen);

        HFONT fBold = rsw::ui::CreateScaledFont(L"Segoe UI", 9, FW_BOLD,   dpi, 100);
        HFONT fNorm = rsw::ui::CreateScaledFont(L"Segoe UI", 8, FW_NORMAL, dpi, 100);
        ::SetBkMode(dc, TRANSPARENT);
        HGDIOBJ oF = ::SelectObject(dc, fBold);
        ::SetTextColor(dc, RGB(15, 23, 42));

        auto drawBoxCtrl = [&](int x, int y, int w, int h, const std::wstring& cap, COLORREF borderCol, bool sunkenFill) {
            RECT r{DX(x), DY(y), DX(x + w), DY(y + h)};
            if (sunkenFill) ::FillRect(dc, &r, hWhite);
            HPEN p = ::CreatePen(PS_SOLID, 1, borderCol);
            HGDIOBJ op = ::SelectObject(dc, p);
            HGDIOBJ ob = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
            ::Rectangle(dc, r.left, r.top, r.right, r.bottom);
            ::SelectObject(dc, ob);
            ::SelectObject(dc, op);
            ::DeleteObject(p);
            RECT tr{r.left + 6, r.top + 2, r.right - 4, r.bottom - 2};
            ::DrawTextW(dc, cap.c_str(), -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        };

        // Footer Wizard 97 Buttons (`Back`, `Next`/`Install`, `Cancel`) + DLU Ruler Badge
        ::SelectObject(dc, fNorm);
        std::wstring badge = L"MSI.DLL Dialog: " + std::wstring(dlgNameBuf) + L" (370x270 DLU)";
        RECT bdgRc{DX(10), DY(238), DX(176), DY(264)};
        ::SetTextColor(dc, RGB(71, 85, 105));
        ::DrawTextW(dc, badge.c_str(), -1, &bdgRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ::SetTextColor(dc, RGB(15, 23, 42));
        drawBoxCtrl(180, 243, 54, 17, L"< Back",  RGB(148, 163, 184), false);
        drawBoxCtrl(238, 243, 54, 17, (dlgId == "VerifyReadyDlg") ? L"Install" : L"Next >", RGB(37, 99, 235), false);
        drawBoxCtrl(304, 243, 54, 17, L"Cancel",  RGB(148, 163, 184), false);

        // Built-in or Custom Dialog Content Preview
        if (dlgId == "WelcomeDlg" || dlgId == "ExitDialog") {
            ::SelectObject(dc, fBold);
            RECT t{DX(135), DY(22), DX(355), DY(70)};
            ::DrawTextW(dc, (dlgId == "WelcomeDlg") ? L"Welcome to [ProductName] Setup" : L"Completed [ProductName] Setup", -1, &t, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
            ::SelectObject(dc, fNorm);
            RECT d{DX(135), DY(80), DX(355), DY(160)};
            ::DrawTextW(dc, L"Native Windows Installer relational Dialog table preview.\r\nSupports silent /qn GPO deployment & full interactive UI.", -1, &d, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        } else {
            std::wstring hTitle = rsw::Utf8ToWide(doc.GetString("MsiDialog", dlgId, "HeaderTitle", dlgId));
            std::wstring hSub   = rsw::Utf8ToWide(doc.GetString("MsiDialog", dlgId, "HeaderSubtitle", "Windows Installer 370x270 Wizard 97 SQL Dialog"));
            if (dlgId == "SetupTypeDlg")   { hTitle = L"Choose Setup Mode"; hSub = L"Express, Compact Core, or Custom Setup."; }
            if (dlgId == "CustomizeDlg")   { hTitle = L"Select Features";   hSub = L"Native MSI SelectionTree feature selector."; }
            if (dlgId == "InstallDirDlg")  { hTitle = L"Destination Folder";hSub = L"PathEdit + SpawnDialog(BrowseDlg)."; }
            if (dlgId == "ShortcutsDlg")   { hTitle = L"Shortcuts & File Associations"; hSub = L"DESKTOP_SHORTCUT & REGISTER_ASSOC checkboxes."; }
            if (dlgId == "RepairModeDlg")  { hTitle = L"Repair & Verify Installation";  hSub = L"Quick Check (pecms) vs Full Reinstall (vomus)."; }
            if (dlgId == "VerifyReadyDlg") { hTitle = L"Ready to Install [ProductName]";hSub = L"Summary & UAC elevation trigger."; }

            ::SelectObject(dc, fBold);
            RECT ht{DX(15), DY(6), DX(350), DY(22)};
            ::DrawTextW(dc, hTitle.c_str(), -1, &ht, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);
            ::SelectObject(dc, fNorm);
            RECT hs{DX(20), DY(23), DX(350), DY(42)};
            ::DrawTextW(dc, hSub.c_str(), -1, &hs, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX);

            if (dlgId == "SetupTypeDlg") {
                drawBoxCtrl(20, 58,  330, 42, L"(o) &Express Setup (Recommended)  [RSW_SETUP_MODE]", RGB(37, 99, 235), true);
                drawBoxCtrl(20, 106, 330, 42, L"( ) Co&mpact Core Setup  (AddLocal=Main)",           RGB(148, 163, 184), true);
                drawBoxCtrl(20, 154, 330, 42, L"( ) C&ustom Setup -> CustomizeDlg -> InstallDirDlg", RGB(148, 163, 184), true);
            } else if (dlgId == "CustomizeDlg") {
                drawBoxCtrl(20,  56, 198, 150, L"[SelectionTree: Main & Sub-Features]", RGB(100, 116, 139), true);
                drawBoxCtrl(226, 56, 126, 150, L"[Feature Description & Cost]",         RGB(148, 163, 184), false);
            } else if (dlgId == "InstallDirDlg") {
                drawBoxCtrl(20,  82, 258, 18, L"[INSTALLDIR]", RGB(100, 116, 139), true);
                drawBoxCtrl(284, 81, 66,  19, L"B&rowse...",   RGB(37, 99, 235),   false);
            } else if (dlgId == "ShortcutsDlg") {
                drawBoxCtrl(24, 68, 322, 20, L"[x] &Create a Desktop shortcut  [DESKTOP_SHORTCUT]", RGB(37, 99, 235), true);
                drawBoxCtrl(24, 98, 322, 20, L"[x] &Register file extensions   [REGISTER_ASSOC]",   RGB(37, 99, 235), true);
            } else if (dlgId == "RepairModeDlg") {
                drawBoxCtrl(20, 62,  330, 50, L"(o) &Quick Check & Repair (ReinstallMode=pecms)", RGB(16, 185, 129), true);
                drawBoxCtrl(20, 120, 330, 50, L"( ) &Full Reinstall (ReinstallMode=vomus)",       RGB(148, 163, 184), true);
            } else if (dlgId == "LicenseAgreementDlg") {
                drawBoxCtrl(20, 54,  330, 132, L"[ScrollableText: License.rtf]", RGB(100, 116, 139), true);
                drawBoxCtrl(20, 196, 330, 18,  L"[x] I have &read and accept the terms  [IAGREECHECK]", RGB(37, 99, 235), false);
            }
        }

        // Overlay any author-placed `[MsiControl.<cid>]` targeting `dlgId`!
        for (const auto& cid : doc.GetNamedSectionIds("MsiControl")) {
            if (!rsw::EqualsIgnoreCase(doc.GetString("MsiControl", cid, "Dialog"), dlgId)) continue;
            const int x  = doc.GetInt("MsiControl", cid, "X", 20);
            const int y  = doc.GetInt("MsiControl", cid, "Y", 60);
            const int w  = doc.GetInt("MsiControl", cid, "Width", 330);
            const int h  = doc.GetInt("MsiControl", cid, "Height", 18);
            const std::string tp = doc.GetString("MsiControl", cid, "Type", "Text");
            const std::string pr = doc.GetString("MsiControl", cid, "Property");
            const std::string tx = doc.GetString("MsiControl", cid, "Text", cid);
            std::string cap = "[" + tp + "] " + tx;
            if (!pr.empty()) cap += "  {" + pr + "}";
            drawBoxCtrl(x, y, w, h, rsw::Utf8ToWide(cap), RGB(124, 58, 237), rsw::EqualsIgnoreCase(tp, "Edit") || rsw::EqualsIgnoreCase(tp, "PathEdit"));
        }

        HPEN rim = ::CreatePen(PS_SOLID, 2, theme.accentPrimary);
        HGDIOBJ oRim = ::SelectObject(dc, rim);
        HGDIOBJ oNb  = ::SelectObject(dc, ::GetStockObject(NULL_BRUSH));
        ::Rectangle(dc, 0, 0, cw, ch);
        ::SelectObject(dc, oNb);
        ::SelectObject(dc, oRim);
        ::DeleteObject(rim);

        ::SelectObject(dc, oF);
        ::DeleteObject(fBold);
        ::DeleteObject(fNorm);
        ::DeleteObject(hGray);
        ::DeleteObject(hWhite);

        ::BitBlt(hdcOut, destRc.left, destRc.top, cw, ch, dc, 0, 0, SRCCOPY);
        ::SelectObject(dc, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(dc);
    }

    void PaintIdeSurface(HDC hdcOut) {
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(1, rc.right - rc.left);
        const int H = std::max<int>(1, rc.bottom - rc.top);
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        HDC hdc        = ::CreateCompatibleDC(hdcOut);
        HBITMAP memBmp = ::CreateCompatibleBitmap(hdcOut, W, H);
        HGDIOBJ oldBmp = ::SelectObject(hdc, memBmp);

        ::FillRect(hdc, &rc, theme.hbrWindow);
        RECT bannerRc{0, 0, W, S(50)};
        rsw::ui::FillHorizontalGradient(hdc, bannerRc, theme.bannerNavyLeft, theme.bannerNavyRight);

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        rsw::ui::DrawBrandSquircleBadge(g, static_cast<float>(S(14)), static_cast<float>(S(9)), static_cast<float>(S(32)), theme, L"RaggieSoft");
        rsw::ui::DrawMultiStopAccentBar(g, 0.0f, static_cast<float>(S(50)), static_cast<float>(W), static_cast<float>(S(4)), theme);

        ::SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ oldF = ::SelectObject(hdc, hFontBanner);
        ::SetTextColor(hdc, theme.bannerTitleText);
        RECT tRc{S(54), S(8), W - S(16), S(44)};
        ::DrawTextW(hdc, L"RaggieSoft Installer Studio  •  Install Without Limits (AeroWizards • Destination Tree • KeyPaths • Dual C++ & MSI.DLL Designer)",
                    -1, &tRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        if (activeView == VIEW_CPP_WIZARD && previewCanvasRc.right > previewCanvasRc.left + S(160)) {
            PaintLiveWizardPreview(hdc, previewCanvasRc, dpi);
        } else if (activeView == VIEW_MSI_DIALOGS && msiCanvasRc.right > msiCanvasRc.left + S(160)) {
            PaintMsiDialogDesignerCanvas(hdc, msiCanvasRc, dpi);
        }

        ::SelectObject(hdc, oldF);
        ::BitBlt(hdcOut, 0, 0, W, H, hdc, 0, 0, SRCCOPY);
        ::SelectObject(hdc, oldBmp);
        ::DeleteObject(memBmp);
        ::DeleteDC(hdc);
    }

    // ----------------------------------------------------------------------------
    // MAIN IDE WINDOW PROCEDURE
    // ----------------------------------------------------------------------------
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
            case WM_NOTIFY: {
                const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
                if (nm && nm->idFrom == IDC_DEST_TREE && nm->code == TVN_SELCHANGEDW) {
                    const auto* nmtv = reinterpret_cast<const NMTREEVIEWW*>(lParam);
                    const std::size_t idx = static_cast<std::size_t>(nmtv->itemNew.lParam);
                    if (idx < self->destNodeMetas.size()) {
                        self->selectedDestRoot   = self->destNodeMetas[idx].targetRoot;
                        self->selectedDestSubdir = self->destNodeMetas[idx].targetSubdir;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        self->RefreshPayloadListViewFilter(self->ResolvedEffectiveDoc());
                    }
                    return 0;
                }
                break;
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
                if ((id == IDC_UI_PREVIEW_STEP || id == IDC_MSI_DLG_SELECT) && code == CBN_SELCHANGE) {
                    ::InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
                if (id == IDC_MSI_PRESET_COMBO && code == CBN_SELCHANGE) {
                    self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                    const bool minMode = (::SendMessageW(self->hMsiPreset, CB_GETCURSEL, 0, 0) == 1);
                    self->astDoc.SetString("UI", "", "MsiDialogPreset", minMode ? "minimal" : "express_custom");
                    self->SyncSourceEditorFromAst();
                    ::InvalidateRect(hWnd, nullptr, FALSE);
                    return 0;
                }
                if (id == IDC_UI_LAYOUT && code == CBN_SELCHANGE) {
                    const LRESULT lay = ::SendMessageW(self->hUiLayout, CB_GETCURSEL, 0, 0);
                    ::SendMessageW(self->hUiPreviewStep, CB_SETCURSEL, (lay == 2) ? 4 : 1, 0);
                    ::InvalidateRect(hWnd, nullptr, FALSE);
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
                    // --- 4 Guided Native Win32 AeroWizards (`PSH_AEROWIZARD`)! ---
                    case IDM_WIZ_PROJECT:
                    case IDC_TB_WIZARD: {
                        std::string genText;
                        bool autoVal = false;
                        if (rsw::ide_wizards::RunNewProjectAeroWizard(hWnd, genText, autoVal)) {
                            self->LoadProjectText(genText, {});
                            self->AppendConsole(L"[AEROWIZARD] Generated new .rsip project via Project Assistant.\r\n");
                            if (autoVal) self->RunValidation();
                        }
                        return 0;
                    }
                    case IDM_WIZ_FILE_ASSOC:
                    case IDC_FA_WIZ_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        if (rsw::ide_wizards::RunFileAssociationAeroWizard(hWnd, self->astDoc)) {
                            self->SyncSourceEditorFromAst();
                            self->SyncAllViewsFromAst();
                            self->AppendConsole(L"[AEROWIZARD] Added [FileAssociation.*] to .rsip AST.\r\n");
                        }
                        return 0;
                    }
                    case IDM_WIZ_COMPONENT:
                    case IDC_COMP_WIZ_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        if (rsw::ide_wizards::RunComponentKeyPathAeroWizard(hWnd, self->astDoc)) {
                            self->SyncSourceEditorFromAst();
                            self->SyncAllViewsFromAst();
                            self->AppendConsole(L"[AEROWIZARD] Added [Component.*] & KeyPath to .rsip AST.\r\n");
                        }
                        return 0;
                    }
                    case IDM_WIZ_RELEASE:
                    case IDC_ENT_REL_WIZ_BTN: {
                        rsw::ide_wizards::ReleaseWizardResult res{};
                        if (rsw::ide_wizards::RunReleaseMstMspAeroWizard(hWnd, res)) {
                            if (res.modeIdx == 2) {
                                self->InvokeInstBuild(false);
                                self->ExportVpatHtml();
                            } else {
                                const std::filesystem::path baseP = rsw::Utf8ToWide(res.baselineMsi);
                                const std::filesystem::path targP = rsw::Utf8ToWide(res.targetMsi);
                                const bool wantMst = (res.modeIdx == 0);
                                const auto outP = targP.parent_path() / (targP.stem().wstring() + (wantMst ? L".mst" : L".msp"));
                                std::string err;
                                const bool ok = wantMst
                                    ? rsw::GenerateMsiTransform(baseP, targP, outP, err)
                                    : rsw::GenerateMsiPatchPackage(baseP, targP, outP, err);
                                self->AppendConsole(ok ? (L"[AEROWIZARD] Emitted " + outP.wstring() + L"\r\n")
                                                       : (L"[ERROR] " + rsw::Utf8ToWide(err) + L"\r\n"));
                                if (ok && res.openInPrism) {
                                    const auto prismExe = self->ideExeDir / L"msiprism.exe";
                                    std::wstring arg = L"\"" + outP.wstring() + L"\"";
                                    ::ShellExecuteW(hWnd, L"open", prismExe.wstring().c_str(), arg.c_str(), nullptr, SW_SHOWNORMAL);
                                }
                            }
                        }
                        return 0;
                    }
                    case IDM_WIZ_INSERT_PRESET:
                    case IDC_MSI_INCLUDE_BTN:
                        self->InsertIncludePresetDialog();
                        return 0;

                    // --- File & Standard Views ---
                    case IDM_FILE_NEW:
                    case IDC_TB_NEW:
                        self->LoadProjectText(BuildStarterRsipTemplate(), {});
                        return 0;
                    case IDM_FILE_OPEN:
                    case IDC_TB_OPEN: {
                        std::wstring p;
                        if (self->PickPathDialog(
                                false,
                                L"Installer Studio Projects & Suites (*.rsip;*.rsis;*.rsii)\0*.rsip;*.rsis;*.rsii;*.rssp;*.rsw\0All Files\0*.*\0",
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

                    // --- View 1: Destination Files & Folders ---
                    case IDC_PAY_ADD_FILE:
                        self->AddPayloadToSelectedDestFolder(false);
                        return 0;
                    case IDC_PAY_ADD_FOLDER:
                        self->AddPayloadToSelectedDestFolder(true);
                        return 0;
                    case IDC_DEST_NEW_SUBDIR:
                        self->CreateExplicitDestinationSubfolder();
                        return 0;
                    case IDC_PAY_REMOVE:
                        self->RemoveSelectedDestItem();
                        return 0;

                    // --- View 2: Setup Design (Features & Components) ---
                    case IDC_FEAT_SAVE_BTN: {
                        const std::string fid = ReadCtlUtf8(self->hFeatId);
                        if (fid.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        self->astDoc.SetString("Feature", fid, "Title", ReadCtlUtf8(self->hFeatTitle));
                        self->astDoc.SetString("Feature", fid, "Level", ReadCtlUtf8(self->hFeatLevel));
                        const std::string par = ReadCtlUtf8(self->hFeatParent);
                        if (!par.empty()) self->astDoc.SetString("Feature", fid, "Parent", par);
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Saved [Feature." + rsw::Utf8ToWide(fid) + L"].\r\n");
                        return 0;
                    }
                    case IDC_COMP_SAVE_BTN: {
                        const std::string cid = ReadCtlUtf8(self->hCompId);
                        if (cid.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        const int rtIdx = std::clamp(static_cast<int>(::SendMessageW(self->hCompRoot, CB_GETCURSEL, 0, 0)), 0, 8);
                        const int kpIdx = std::clamp(static_cast<int>(::SendMessageW(self->hCompKpKind, CB_GETCURSEL, 0, 0)), 0, 3);
                        const char* kpStrs[] = {"auto", "file", "registry", "folder"};
                        self->astDoc.SetString("Component", cid, "Feature",     ReadCtlUtf8(self->hCompFeat));
                        self->astDoc.SetString("Component", cid, "TargetRoot",  rsw::ide_wizards::kRootIds[rtIdx]);
                        const std::string sub = ReadCtlUtf8(self->hCompSubdir);
                        if (!sub.empty()) self->astDoc.SetString("Component", cid, "TargetSubdir", sub);
                        self->astDoc.SetString("Component", cid, "Guid",        "*");
                        self->astDoc.SetString("Component", cid, "KeyPathKind", kpStrs[kpIdx]);
                        const std::string kpt = ReadCtlUtf8(self->hCompKpTarget);
                        if (!kpt.empty()) self->astDoc.SetString("Component", cid, "KeyPathTarget", kpt);
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Saved [Component." + rsw::Utf8ToWide(cid) + L"].\r\n");
                        return 0;
                    }

                    // --- View 3: File Associations ---
                    case IDC_FA_ADD_BTN: {
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        const std::string aid = "Assoc" + std::to_string(self->astDoc.GetNamedSectionIds("FileAssociation").size() + 1);
                        self->astDoc.SetString("FileAssociation", aid, "Extension",   ReadCtlUtf8(self->hFaExt));
                        self->astDoc.SetString("FileAssociation", aid, "ProgId",      ReadCtlUtf8(self->hFaProgId));
                        self->astDoc.SetString("FileAssociation", aid, "Description", ReadCtlUtf8(self->hFaDesc));
                        self->astDoc.SetString("FileAssociation", aid, "TargetFile",  ReadCtlUtf8(self->hFaTarget));
                        self->astDoc.SetString("FileAssociation", aid, "Verbs",       ReadCtlUtf8(self->hFaVerbs));
                        self->astDoc.SetString("FileAssociation", aid, "Advertise",
                                               (::SendMessageW(self->hFaAdvChk, BM_GETCHECK, 0, 0) == BST_CHECKED) ? "true" : "false");
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        self->AppendConsole(L"[AST] Added [FileAssociation." + rsw::Utf8ToWide(aid) + L"].\r\n");
                        return 0;
                    }
                    case IDC_FA_REMOVE_BTN: {
                        const int sel = ListView_GetNextItem(self->hFaGrid, -1, LVNI_SELECTED);
                        if (sel < 0) return 0;
                        const auto ids = self->astDoc.GetNamedSectionIds("FileAssociation");
                        if (static_cast<std::size_t>(sel) < ids.size()) {
                            const std::string targetId = ids[static_cast<std::size_t>(sel)];
                            self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                            std::vector<rsw::RswAstLine> kept;
                            for (const auto& ln : self->astDoc.lines) {
                                if ((ln.kind == rsw::AstLineKind::SectionHeader || ln.kind == rsw::AstLineKind::KeyValue) &&
                                    rsw::EqualsIgnoreCase(ln.sectionPrefix, "FileAssociation") &&
                                    rsw::EqualsIgnoreCase(ln.sectionItemId, targetId)) continue;
                                kept.push_back(ln);
                            }
                            self->astDoc.lines = std::move(kept);
                            self->SyncSourceEditorFromAst();
                            self->SyncAllViewsFromAst();
                        }
                        return 0;
                    }

                    // --- View 4: Shortcuts & Registry ---
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
                        const std::string rComp = ReadCtlUtf8(self->hRegComp);
                        if (!rComp.empty()) self->astDoc.SetString("Registry", rid, "Component", rComp);
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        return 0;
                    }

                    // --- View 5: C++ Wizard Designer ---
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

                    // --- View 6: MSI.DLL Dialog Designer ---
                    case IDC_MSI_ADD_DLG_BTN: {
                        const std::string did = ReadCtlUtf8(self->hMsiNewId);
                        if (did.empty()) return 0;
                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        self->astDoc.SetString("MsiDialog", did, "Title",       "[ProductName] Setup");
                        self->astDoc.SetString("MsiDialog", did, "Width",       "370");
                        self->astDoc.SetString("MsiDialog", did, "Height",      "270");
                        self->astDoc.SetString("MsiDialog", did, "Banner",      "true");
                        self->astDoc.SetString("MsiDialog", did, "HeaderTitle", ReadCtlUtf8(self->hMsiNewTitle));
                        self->astDoc.SetString("MsiDialog", did, "PrevDialog",  ReadCtlUtf8(self->hMsiNewAfter));
                        self->astDoc.SetString("MsiDialog", did, "NextDialog",  "VerifyReadyDlg");
                        self->SyncSourceEditorFromAst();
                        self->SyncAllViewsFromAst();
                        const LRESULT idx = ::SendMessageW(self->hMsiDlgSelect, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(rsw::Utf8ToWide(did).c_str()));
                        if (idx != CB_ERR) ::SendMessageW(self->hMsiDlgSelect, CB_SETCURSEL, idx, 0);
                        ::InvalidateRect(hWnd, nullptr, FALSE);
                        self->AppendConsole(L"[MSI DIALOG] Added [MsiDialog." + rsw::Utf8ToWide(did) + L"] spliced into Dialog sequence.\r\n");
                        return 0;
                    }
                    case IDC_MSI_ADD_CTRL_BTN: {
                        const std::string cid = ReadCtlUtf8(self->hMsiCtrlId);
                        wchar_t dlgBuf[128]{};
                        ::GetWindowTextW(self->hMsiDlgSelect, dlgBuf, 128);
                        const std::string dlg = rsw::WideToUtf8(dlgBuf);
                        if (cid.empty() || dlg.empty()) return 0;

                        const char* tpNames[] = {"Text", "Edit", "PathEdit", "CheckBox", "RadioButtonGroup", "ComboBox", "GroupBox"};
                        const int tpIdx = std::clamp(static_cast<int>(::SendMessageW(self->hMsiCtrlType, CB_GETCURSEL, 0, 0)), 0, 6);

                        int vals[4] = {20, 74, 330, 18};
                        std::string xywh = ReadCtlUtf8(self->hMsiCtrlXywh);
                        for (char& ch : xywh) if (ch == ',') ch = ' ';
                        std::istringstream iss(xywh);
                        iss >> vals[0] >> vals[1] >> vals[2] >> vals[3];

                        self->astDoc = rsw::ParseManifestText(self->ReadSourceEditorUtf8(), self->currentFilePath);
                        self->astDoc.SetString("MsiControl", cid, "Dialog", dlg);
                        self->astDoc.SetString("MsiControl", cid, "Type",   tpNames[tpIdx]);
                        self->astDoc.SetString("MsiControl", cid, "X",      std::to_string(vals[0]));
                        self->astDoc.SetString("MsiControl", cid, "Y",      std::to_string(vals[1]));
                        self->astDoc.SetString("MsiControl", cid, "Width",  std::to_string(vals[2]));
                        self->astDoc.SetString("MsiControl", cid, "Height", std::to_string(vals[3]));
                        const std::string prop = ReadCtlUtf8(self->hMsiCtrlProp);
                        if (!prop.empty()) self->astDoc.SetString("MsiControl", cid, "Property", prop);
                        const std::string txt = ReadCtlUtf8(self->hMsiCtrlText);
                        if (!txt.empty()) {
                            if (tpIdx == 4 || tpIdx == 5) self->astDoc.SetString("MsiControl", cid, "Items", txt);
                            else if (tpIdx == 1)          self->astDoc.SetString("MsiControl", cid, "DefaultValue", txt);
                            else                          self->astDoc.SetString("MsiControl", cid, "Text", txt);
                        }
                        self->SyncSourceEditorFromAst();
                        ::InvalidateRect(hWnd, nullptr, FALSE);
                        self->AppendConsole(L"[MSI CONTROL] Placed [MsiControl." + rsw::Utf8ToWide(cid) + L"] onto " + std::wstring(dlgBuf) + L".\r\n");
                        return 0;
                    }

                    // --- View 7: Signing, .MST & .MSP ---
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

                    // --- Build & Compliance Commands ---
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
            if (roundTrip.find(';') == std::string::npos) return 3;
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
    rsw::ui::ScopedGdiPlusSession gdiPlusSession;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES | ICC_TAB_CLASSES};
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
    const int winW = rsw::ui::ScalePx(1320, dpi, ide.theme.textScalePercent);
    const int winH = rsw::ui::ScalePx(790,  dpi, ide.theme.textScalePercent);
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
    ide.AppendConsole(L"RaggieSoft Installer Studio ready. Click 'Project Wizard...' (Ctrl+W) for the 5-step AeroWizard, explore Destination Folders & Setup Design, or press F5 to build.\r\n");

    ACCEL accels[] = {
        {FCONTROL | FVIRTKEY, 'W',   IDM_WIZ_PROJECT},
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
