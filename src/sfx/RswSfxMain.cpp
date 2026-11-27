// ================================================================================
// FILE: src/sfx/RswSfxMain.cpp
// PROJECT: RaggieSoft Self-Extractor (`rswsfx.exe`)
// WEBSITE: https://raggiesoftstudio.com/installer-studio  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `rswsfx.exe` ("RaggieSoft Self-Extractor")?
//     `rswsfx.exe` is RaggieSoft's dedicated, single-purpose standalone desktop
//     and CLI utility for creating zero-dependency Windows 11 (`x64` & `ARM64`)
//     **`LZMS` Self-Extracting `.EXE` Archives** (`PackageKind = sfx`)—bundled
//     inside every edition of **RaggieSoft Installer Studio** (including
//     `Essentials` and `Community`) AND packaged as its own standalone utility!
//
//   Two Focused Delivery Modes:
//     1. **Mode A — Standard Folder / `.ZIP` Archive Extractor**:
//        Compresses any folder or `.zip` archive with Windows `COMPRESS_ALGORITHM_LZMS`
//        (`Cabinet.lib`) into one native `.exe` featuring Dark/Light/High-Contrast
//        WCAG 2.2 AA UI, destination folder picker, `DefaultExtractSubdir`, and an
//        optional `PostExtractRun` program or `PostExtractOpen` document launch.
//     2. **Mode B — Software Installation `%TEMP%` Wrapper (`_ISDEL.EXE`-Aware)**:
//        Extracts a multi-file setup payload into an isolated `%TEMP%\rsw-sfx-*`
//        staging directory, runs `PostExtractRun`, tracks both Win32 Job Object
//        child trees and classic companion cleanup helpers (`_ISDEL.EXE`,
//        `_INS*._MP`), and automatically scrubs `%TEMP%` once setup completes!
//
//   Dual GUI & Headless CLI Operation:
//     - Launch `rswsfx.exe` with no build flags to open the accessible Dark/Light
//       Win32 desktop builder window.
//     - Run `rswsfx.exe --pack <folder-or-zip> <output.exe> [options]` in scripts
//       or CI/CD pipelines to build self-extracting executables headlessly!
// ================================================================================

#include "../common/RswGlassRenderer.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswVersion.hpp"
#include "../common/RswWin32UiTheme.hpp"
#include "../compiler/RswBootstrapperPackager.hpp"
#include "../compiler/RswValidator.hpp"

#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

enum SfxCtrlId : int {
    IDC_SFX_SOURCE_EDIT   = 3101,
    IDC_SFX_PICK_FOLDER   = 3102,
    IDC_SFX_PICK_ZIP      = 3103,
    IDC_SFX_TITLE_EDIT    = 3104,
    IDC_SFX_PUB_EDIT      = 3105,
    IDC_SFX_VER_EDIT      = 3106,
    IDC_SFX_MODE_COMBO    = 3107,
    IDC_SFX_SUBDIR_EDIT   = 3108,
    IDC_SFX_POST_EDIT     = 3109,
    IDC_SFX_WAIT_EDIT     = 3110,
    IDC_SFX_UAC_COMBO     = 3111,
    IDC_SFX_ICON_EDIT     = 3112,
    IDC_SFX_PICK_ICON     = 3113,
    IDC_SFX_OUT_EDIT      = 3114,
    IDC_SFX_PICK_OUT      = 3115,
    IDC_SFX_BUILD_BTN     = 3120,
    IDC_SFX_EXPORT_RSIP   = 3121,
    IDC_SFX_ABOUT_BTN     = 3122,
    IDC_SFX_STATUS_EDIT   = 3130
};

struct SfxBuildSpec {
    std::filesystem::path sourcePath;
    std::filesystem::path outputExePath;
    std::string           title         = "Portable Archive";
    std::string           publisher     = "RaggieSoft";
    std::string           version       = "1.0.0";
    bool                  tempInstall   = false;
    std::string           defaultSubdir = "ExtractedFiles";
    std::string           postAction;          // `.exe` for `PostExtractRun` or doc for `PostExtractOpen`
    std::string           waitProcesses = "_ISDEL.EXE; _INS*._MP";
    std::string           uacLevel      = "asInvoker";
    std::filesystem::path iconPath;
    std::string           archOverride;
};

/// Synthesizes a clean, comment-documented `.rsip` (`PackageKind = sfx`) manifest from `spec`
/// so both `rswsfx.exe` builds AND `Export .RSIP...` share 100% identical compiler semantics!
std::string SynthesizeSfxRsipText(const SfxBuildSpec& spec) {
    const std::filesystem::path outAbs = spec.outputExePath.is_absolute()
        ? spec.outputExePath
        : (std::filesystem::current_path() / spec.outputExePath).lexically_normal();
    const std::string outDir  = rsw::WideToUtf8(outAbs.parent_path().wstring());
    const std::string outStem = rsw::WideToUtf8(outAbs.stem().wstring());

    const std::string postExt = [&]() {
        if (spec.postAction.empty()) return std::string{};
        std::filesystem::path p(rsw::Utf8ToWide(spec.postAction));
        return rsw::AsciiLower(rsw::WideToUtf8(p.extension().wstring()));
    }();
    const bool isRunExe = spec.tempInstall || postExt == ".exe" || postExt == ".cmd" || postExt == ".bat" || postExt == ".msi";

    std::ostringstream rsip;
    rsip << "; ==============================================================================\r\n"
         << "; Generated by RaggieSoft Self-Extractor (rswsfx.exe v" << rsw::kVersionString << ")\r\n"
         << "; Compatible 1:1 with RaggieSoft Installer Studio (InstallerStudio.exe & instbuild.exe)\r\n"
         << "; ==============================================================================\r\n\r\n"
         << "[Package]\r\n"
         << "Name                     = " << (spec.title.empty() ? "Portable Archive" : spec.title) << "\r\n"
         << "Version                  = " << (spec.version.empty() ? "1.0.0" : spec.version) << "\r\n"
         << "Manufacturer             = " << (spec.publisher.empty() ? "RaggieSoft" : spec.publisher) << "\r\n"
         << "PackageKind              = sfx\r\n"
         << "Scope                    = user\r\n"
         << "RequestedExecutionLevel  = " << rsw::NormalizeRequestedExecutionLevel(spec.uacLevel) << "\r\n"
         << "OutputDir                = " << outDir << "\r\n"
         << "OutputName               = " << outStem << "\r\n\r\n"
         << "[UI]\r\n"
         << "WizardLayout             = simple\r\n"
         << "AccentPrimaryHex         = #1D4ED8\r\n"
         << "AccentSecondaryHex       = #FBBF24\r\n"
         << "InstallButtonText        = " << (spec.tempInstall ? "&Install" : "&Extract") << "\r\n";
    if (!spec.iconPath.empty()) {
        rsip << "IconFile                 = " << rsw::WideToUtf8(spec.iconPath.wstring()) << "\r\n";
    }

    rsip << "\r\n[SFX]\r\n";
    if (spec.tempInstall) {
        rsip << "ExtractToTemp            = true\r\n"
             << "CleanupTempAfterRun      = true\r\n"
             << "PostExtractRun           = " << (spec.postAction.empty() ? "Setup.exe" : spec.postAction) << "\r\n"
             << "WaitMode                 = auto\r\n";
        if (!spec.waitProcesses.empty()) {
            rsip << "WaitForProcess           = " << spec.waitProcesses << "\r\n";
        }
    } else {
        if (!spec.defaultSubdir.empty()) {
            rsip << "DefaultExtractSubdir     = " << spec.defaultSubdir << "\r\n";
        }
        if (!spec.postAction.empty()) {
            if (isRunExe) {
                rsip << "PostExtractRun           = " << spec.postAction << "\r\n";
            } else {
                rsip << "PostExtractOpen          = " << spec.postAction << "\r\n";
            }
        }
    }

    rsip << "\r\n[Payload.Archive]\r\n"
         << "Source                   = " << rsw::WideToUtf8(spec.sourcePath.wstring()) << "\r\n";
    return rsip.str();
}

/// Compiles `spec` directly into a standalone `LZMS`-compressed `.exe` using `SetupWizard.exe`!
bool BuildSelfExtractingExe(
    const SfxBuildSpec& spec,
    const std::filesystem::path& sfxExeDir,
    std::filesystem::path& outBuiltExe,
    rsw::CompiledMsiSummary& outSummary,
    std::string& outError)
{
    std::error_code ec;
    if (spec.sourcePath.empty() || !std::filesystem::exists(spec.sourcePath, ec)) {
        outError = "Source folder or .ZIP archive does not exist: '" + rsw::WideToUtf8(spec.sourcePath.wstring()) + "'.";
        return false;
    }
    if (spec.outputExePath.empty()) {
        outError = "Output .EXE path cannot be empty.";
        return false;
    }
    const std::string rsipText = SynthesizeSfxRsipText(spec);
    const auto doc = rsw::ParseManifestText(rsipText, std::filesystem::current_path() / L"rswsfx-inline.rsip");
    const std::string arch = spec.archOverride.empty()
        ? std::string(rsw::QueryHostPlatform().NativeArchSlug())
        : spec.archOverride;
    return rsw::PackageNativeOrSfxExecutable(doc, arch, sfxExeDir, outSummary, outBuiltExe, outError);
}

/// Opens a modern Windows Vista+ `IFileOpenDialog` (`FOS_PICKFOLDERS` or file filter) or `GetSaveFileNameW`.
bool BrowseModalPath(HWND hOwner, bool pickFolder, bool saveDialog, const wchar_t* filter, std::wstring& outPath) {
    if (pickFolder) {
        IFileOpenDialog* dlg = nullptr;
        if (FAILED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dlg))) || !dlg) {
            return false;
        }
        DWORD opts = 0;
        dlg->GetOptions(&opts);
        dlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST);
        bool chosen = false;
        if (SUCCEEDED(dlg->Show(hOwner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dlg->GetResult(&item)) && item) {
                PWSTR psz = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                    outPath = psz;
                    ::CoTaskMemFree(psz);
                    chosen = true;
                }
                item->Release();
            }
        }
        dlg->Release();
        return chosen;
    }

    wchar_t buf[MAX_PATH * 2]{};
    if (!outPath.empty() && outPath.size() < std::size(buf)) {
        wcsncpy_s(buf, outPath.c_str(), _TRUNCATE);
    }
    OPENFILENAMEW ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hOwner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = static_cast<DWORD>(std::size(buf));
    if (saveDialog) {
        ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
        ofn.lpstrDefExt = L"exe";
        if (!::GetSaveFileNameW(&ofn)) return false;
    } else {
        ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
        if (!::GetOpenFileNameW(&ofn)) return false;
    }
    outPath = buf;
    return true;
}

struct RswSfxWindow {
    HWND                  hWnd = nullptr;
    std::filesystem::path exeDir;
    rsw::ui::RswTheme     theme{};
    HFONT                 hFontBanner = nullptr;
    HFONT                 hFontUi     = nullptr;
    HFONT                 hFontBold   = nullptr;
    HFONT                 hFontMono   = nullptr;

    std::vector<HWND> labels;
    HWND hSrcEdit = nullptr, hBtnFolder = nullptr, hBtnZip = nullptr;
    HWND hTitleEdit = nullptr, hPubEdit = nullptr, hVerEdit = nullptr;
    HWND hModeCombo = nullptr, hSubdirEdit = nullptr, hPostEdit = nullptr, hWaitEdit = nullptr;
    HWND hUacCombo = nullptr, hIconEdit = nullptr, hBtnIcon = nullptr;
    HWND hOutEdit = nullptr, hBtnOut = nullptr;
    HWND hBtnBuild = nullptr, hBtnExport = nullptr, hBtnAbout = nullptr;
    HWND hStatusEdit = nullptr;

    void DestroyFonts() noexcept {
        for (HFONT* f : {&hFontBanner, &hFontUi, &hFontBold, &hFontMono}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
    }

    static std::string ReadUtf8(HWND h) {
        wchar_t buf[1024]{};
        ::GetWindowTextW(h, buf, static_cast<int>(std::size(buf)));
        return rsw::TrimAscii(rsw::WideToUtf8(buf));
    }

    void SetStatus(const std::wstring& msg) {
        ::SetWindowTextW(hStatusEdit, msg.c_str());
    }

    SfxBuildSpec GatherSpecFromUi() const {
        SfxBuildSpec s{};
        s.sourcePath    = rsw::Utf8ToWide(ReadUtf8(hSrcEdit));
        s.outputExePath = rsw::Utf8ToWide(ReadUtf8(hOutEdit));
        s.title         = ReadUtf8(hTitleEdit);
        s.publisher     = ReadUtf8(hPubEdit);
        s.version       = ReadUtf8(hVerEdit);
        s.tempInstall   = (::SendMessageW(hModeCombo, CB_GETCURSEL, 0, 0) == 1);
        s.defaultSubdir = ReadUtf8(hSubdirEdit);
        s.postAction    = ReadUtf8(hPostEdit);
        s.waitProcesses = ReadUtf8(hWaitEdit);
        const LRESULT uacSel = ::SendMessageW(hUacCombo, CB_GETCURSEL, 0, 0);
        s.uacLevel = (uacSel == 2) ? "requireAdministrator" : ((uacSel == 1) ? "highestAvailable" : "asInvoker");
        const std::string ico = ReadUtf8(hIconEdit);
        if (!ico.empty()) s.iconPath = rsw::Utf8ToWide(ico);
        return s;
    }

    void CreateControls() {
        HINSTANCE hInst = ::GetModuleHandleW(nullptr);
        auto mkLbl = [&](const wchar_t* txt) {
            HWND h = ::CreateWindowExW(0, L"STATIC", txt, WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hWnd, nullptr, hInst, nullptr);
            labels.push_back(h);
            return h;
        };
        auto mkEd = [&](int id, const wchar_t* init = L"") {
            return ::CreateWindowExW(
                WS_EX_CLIENTEDGE, L"EDIT", init, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
        };
        auto mkBtn = [&](const wchar_t* txt, int id, bool primary = false) {
            HWND h = ::CreateWindowExW(
                0, L"BUTTON", txt, WS_CHILD | WS_VISIBLE | WS_TABSTOP | (primary ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
            rsw::ui::AttachAccessiblePushButtonSubclass(h, &theme, primary, false);
            return h;
        };
        auto mkCombo = [&](int id, std::initializer_list<const wchar_t*> items, int defIdx = 0) {
            HWND cb = ::CreateWindowExW(
                0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
                0, 0, 160, 180, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
            for (const wchar_t* it : items) ::SendMessageW(cb, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(it));
            ::SendMessageW(cb, CB_SETCURSEL, defIdx, 0);
            rsw::ui::ApplyDarkComboBoxTheme(cb, theme);
            return cb;
        };

        mkLbl(L"&Source Folder or .ZIP:");
        hSrcEdit   = mkEd(IDC_SFX_SOURCE_EDIT, L"");
        hBtnFolder = mkBtn(L"Pick &Folder...", IDC_SFX_PICK_FOLDER);
        hBtnZip    = mkBtn(L"Pick .&ZIP...",   IDC_SFX_PICK_ZIP);

        mkLbl(L"Archive &Title:");       hTitleEdit = mkEd(IDC_SFX_TITLE_EDIT, L"RaggieSoft Sample Archive");
        mkLbl(L"&Publisher:");           hPubEdit   = mkEd(IDC_SFX_PUB_EDIT,   L"RaggieSoft");
        mkLbl(L"&Version (X.Y.Z):");     hVerEdit   = mkEd(IDC_SFX_VER_EDIT,   L"1.0.0");

        mkLbl(L"Delivery &Mode:");
        hModeCombo = mkCombo(IDC_SFX_MODE_COMBO, {
            L"Mode A: Standard Folder / Archive Extractor (User chooses destination folder)",
            L"Mode B: %TEMP% Software Setup Wrapper (Unpack -> Run -> Wait _ISDEL.EXE -> Clean %TEMP%)"
        }, 0);

        mkLbl(L"Default &Subfolder:");   hSubdirEdit = mkEd(IDC_SFX_SUBDIR_EDIT, L"RaggieSoft\\ExtractedArchive");
        mkLbl(L"Post-Extract &Action:"); hPostEdit   = mkEd(IDC_SFX_POST_EDIT,   L"");
        mkLbl(L"&Wait Helper Procs:");   hWaitEdit   = mkEd(IDC_SFX_WAIT_EDIT,   L"_ISDEL.EXE; _INS*._MP");

        mkLbl(L"&UAC Execution Level:");
        hUacCombo = mkCombo(IDC_SFX_UAC_COMBO, {
            L"asInvoker (No UAC prompt — Standard User friendly)",
            L"highestAvailable (Elevate for Administrators)",
            L"requireAdministrator (Always prompt for UAC at launch)"
        }, 0);

        mkLbl(L"Custom .&ICO Icon:");    hIconEdit = mkEd(IDC_SFX_ICON_EDIT, L"");
        hBtnIcon = mkBtn(L"Pick .&ico...", IDC_SFX_PICK_ICON);

        mkLbl(L"&Output .EXE File:");    hOutEdit  = mkEd(IDC_SFX_OUT_EDIT,  L"Archive-SelfExtractor.exe");
        hBtnOut  = mkBtn(L"Save &As...", IDC_SFX_PICK_OUT);

        hBtnBuild  = mkBtn(L"&Build Self-Extracting .EXE", IDC_SFX_BUILD_BTN, true);
        hBtnExport = mkBtn(L"Export .&RSIP Project...",    IDC_SFX_EXPORT_RSIP);
        hBtnAbout  = mkBtn(L"A&bout",                      IDC_SFX_ABOUT_BTN);

        hStatusEdit = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT",
            L"Ready. Choose a source folder or .ZIP archive and click Build Self-Extracting .EXE.",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_SFX_STATUS_EDIT)), hInst, nullptr);
        rsw::ui::ApplyDarkScrollbarTheme(hStatusEdit, theme);
    }

    void Layout() {
        const UINT dpi = ::GetDpiForWindow(hWnd);
        DestroyFonts();
        const int ts = theme.textScalePercent;
        hFontBanner = rsw::ui::CreateScaledFont(L"Segoe UI",      13, FW_BOLD,     dpi, ts);
        hFontUi     = rsw::ui::CreateScaledFont(L"Segoe UI",       9, FW_NORMAL,   dpi, ts);
        hFontBold   = rsw::ui::CreateScaledFont(L"Segoe UI",       9, FW_SEMIBOLD, dpi, ts);
        hFontMono   = rsw::ui::CreateScaledFont(L"Cascadia Mono",  9, FW_NORMAL,   dpi, ts);

        for (HWND h : labels) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUi), TRUE);
        for (HWND h : {hSrcEdit, hBtnFolder, hBtnZip, hTitleEdit, hPubEdit, hVerEdit, hModeCombo,
                       hSubdirEdit, hPostEdit, hWaitEdit, hUacCombo, hIconEdit, hBtnIcon,
                       hOutEdit, hBtnOut, hBtnExport, hBtnAbout}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUi), TRUE);
        }
        if (hBtnBuild)   ::SendMessageW(hBtnBuild,   WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);
        if (hStatusEdit) ::SendMessageW(hStatusEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hFontMono), TRUE);

        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(640, rc.right - rc.left);
        const int H = std::max<int>(520, rc.bottom - rc.top);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, ts); };

        const int padX = S(18);
        const int lw   = S(162);
        const int fx   = padX + lw;
        const int fw   = W - fx - padX;
        int y          = S(68);

        if (labels.size() >= 11) {
            // Row 0: Source + Pick Folder + Pick ZIP
            ::MoveWindow(labels[0],  padX, y + S(4), lw, S(22), TRUE);
            ::MoveWindow(hSrcEdit,   fx,   y,        fw - S(214), S(26), TRUE);
            ::MoveWindow(hBtnFolder, fx + fw - S(208), y, S(104), S(26), TRUE);
            ::MoveWindow(hBtnZip,    fx + fw - S(98),  y, S(98),  S(26), TRUE);
            y += S(34);

            // Row 1: Title, Publisher, Version
            ::MoveWindow(labels[1],  padX, y + S(4), lw, S(22), TRUE);
            ::MoveWindow(hTitleEdit, fx,   y,        fw, S(26), TRUE);
            y += S(32);

            const int halfW = (fw - S(130)) / 2;
            ::MoveWindow(labels[2],  padX, y + S(4), lw, S(22), TRUE);
            ::MoveWindow(hPubEdit,   fx,   y,        halfW, S(26), TRUE);
            ::MoveWindow(labels[3],  fx + halfW + S(14), y + S(4), S(110), S(22), TRUE);
            ::MoveWindow(hVerEdit,   fx + halfW + S(126), y, fw - halfW - S(126), S(26), TRUE);
            y += S(34);

            // Row 3: Mode Combo
            ::MoveWindow(labels[4],  padX, y + S(4), lw, S(22), TRUE);
            ::MoveWindow(hModeCombo, fx,   y,        fw, S(160), TRUE);
            y += S(34);

            // Row 4–6: Subfolder, Post Action, Wait Helpers
            ::MoveWindow(labels[5],   padX, y + S(4), lw, S(22), TRUE);
            ::MoveWindow(hSubdirEdit, fx,   y,        fw, S(26), TRUE);
            y += S(32);

            ::MoveWindow(labels[6], padX, y + S(4), lw, S(22), TRUE);
            ::MoveWindow(hPostEdit, fx,   y,        fw, S(26), TRUE);
            y += S(32);

            ::MoveWindow(labels[7], padX, y + S(4), lw, S(22), TRUE);
            ::MoveWindow(hWaitEdit, fx,   y,        fw, S(26), TRUE);
            y += S(34);

            // Row 7: UAC Combo
            ::MoveWindow(labels[8], padX, y + S(4), lw, S(22), TRUE);
            ::MoveWindow(hUacCombo, fx,   y,        fw, S(160), TRUE);
            y += S(34);

            // Row 8: Icon
            ::MoveWindow(labels[9], padX, y + S(4), lw, S(22), TRUE);
            ::MoveWindow(hIconEdit, fx,   y,        fw - S(98), S(26), TRUE);
            ::MoveWindow(hBtnIcon,  fx + fw - S(92), y, S(92), S(26), TRUE);
            y += S(32);

            // Row 9: Output EXE
            ::MoveWindow(labels[10], padX, y + S(4), lw, S(22), TRUE);
            ::MoveWindow(hOutEdit,   fx,   y,        fw - S(98), S(26), TRUE);
            ::MoveWindow(hBtnOut,    fx + fw - S(92), y, S(92), S(26), TRUE);
            y += S(38);

            // Row 10: Action Buttons
            ::MoveWindow(hBtnBuild,  padX,          y, S(236), S(34), TRUE);
            ::MoveWindow(hBtnExport, padX + S(246), y, S(196), S(34), TRUE);
            ::MoveWindow(hBtnAbout,  W - padX - S(96), y, S(96), S(34), TRUE);
            y += S(42);

            // Bottom Console / Status
            ::MoveWindow(hStatusEdit, padX, y, W - padX * 2, std::max(S(64), H - y - S(14)), TRUE);
        }
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    void PaintBanner(HDC hdcOut) {
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(1, rc.right - rc.left);
        const int H = std::max<int>(1, rc.bottom - rc.top);
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        HDC dc = ::CreateCompatibleDC(hdcOut);
        HBITMAP bmp = ::CreateCompatibleBitmap(hdcOut, W, H);
        HGDIOBJ oldBmp = ::SelectObject(dc, bmp);

        ::FillRect(dc, &rc, theme.hbrWindow);
        RECT banRc{0, 0, W, S(52)};
        rsw::ui::FillHorizontalGradient(dc, banRc, theme.bannerNavyLeft, theme.bannerNavyRight);

        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        rsw::ui::DrawBrandSquircleBadge(g, static_cast<float>(S(16)), static_cast<float>(S(10)), static_cast<float>(S(32)), theme, L"RaggieSoft");
        rsw::ui::DrawMultiStopAccentBar(g, 0.0f, static_cast<float>(S(52)), static_cast<float>(W), static_cast<float>(S(4)), theme);

        ::SetBkMode(dc, TRANSPARENT);
        HGDIOBJ oldF = ::SelectObject(dc, hFontBanner);
        ::SetTextColor(dc, theme.bannerTitleText);
        RECT tRc{S(58), S(8), W - S(16), S(44)};
        ::DrawTextW(dc, L"RaggieSoft Self-Extractor  •  Native LZMS Archive & %TEMP% Software Wrapper",
                    -1, &tRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        ::SelectObject(dc, oldF);

        ::BitBlt(hdcOut, 0, 0, W, H, dc, 0, 0, SRCCOPY);
        ::SelectObject(dc, oldBmp);
        ::DeleteObject(bmp);
        ::DeleteDC(dc);
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        RswSfxWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<RswSfxWindow*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hWnd = hWnd;
        } else {
            self = reinterpret_cast<RswSfxWindow*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        }
        if (!self) return ::DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
            case WM_CREATE:
                self->CreateControls();
                rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                self->Layout();
                return 0;
            case WM_SIZE:
                self->Layout();
                return 0;
            case WM_DPICHANGED: {
                const auto* r = reinterpret_cast<const RECT*>(lParam);
                if (r) ::SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
                self->Layout();
                return 0;
            }
            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
            case WM_THEMECHANGED:
                if (rsw::ui::IsLiveThemeBroadcast(msg, wParam, lParam)) {
                    self->theme.Refresh();
                    rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                    rsw::ui::ApplyDarkComboBoxTheme(self->hModeCombo, self->theme);
                    rsw::ui::ApplyDarkComboBoxTheme(self->hUacCombo,  self->theme);
                    rsw::ui::ApplyDarkScrollbarTheme(self->hStatusEdit, self->theme);
                    self->Layout();
                    return 0;
                }
                break;
            case WM_ERASEBKGND:
                return 1;
            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC dc = ::BeginPaint(hWnd, &ps);
                self->PaintBanner(dc);
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
                const int id = LOWORD(wParam);
                switch (id) {
                    case IDC_SFX_PICK_FOLDER: {
                        std::wstring p;
                        if (BrowseModalPath(hWnd, true, false, nullptr, p)) {
                            ::SetWindowTextW(self->hSrcEdit, p.c_str());
                            const auto stem = std::filesystem::path(p).filename().wstring();
                            if (!stem.empty()) {
                                ::SetWindowTextW(self->hTitleEdit, stem.c_str());
                                ::SetWindowTextW(self->hOutEdit, (std::filesystem::path(p).parent_path() / (stem + L"-sfx.exe")).wstring().c_str());
                            }
                        }
                        return 0;
                    }
                    case IDC_SFX_PICK_ZIP: {
                        std::wstring p;
                        if (BrowseModalPath(hWnd, false, false, L"ZIP Archives (*.zip)\0*.zip\0All Files (*.*)\0*.*\0", p)) {
                            ::SetWindowTextW(self->hSrcEdit, p.c_str());
                            const auto stem = std::filesystem::path(p).stem().wstring();
                            if (!stem.empty()) {
                                ::SetWindowTextW(self->hTitleEdit, stem.c_str());
                                ::SetWindowTextW(self->hOutEdit, (std::filesystem::path(p).parent_path() / (stem + L"-sfx.exe")).wstring().c_str());
                            }
                        }
                        return 0;
                    }
                    case IDC_SFX_PICK_ICON: {
                        std::wstring p;
                        if (BrowseModalPath(hWnd, false, false, L"Icon Files (*.ico)\0*.ico\0", p)) {
                            ::SetWindowTextW(self->hIconEdit, p.c_str());
                        }
                        return 0;
                    }
                    case IDC_SFX_PICK_OUT: {
                        std::wstring p = rsw::Utf8ToWide(ReadUtf8(self->hOutEdit));
                        if (BrowseModalPath(hWnd, false, true, L"Windows Executable (*.exe)\0*.exe\0", p)) {
                            ::SetWindowTextW(self->hOutEdit, p.c_str());
                        }
                        return 0;
                    }
                    case IDC_SFX_BUILD_BTN: {
                        const auto spec = self->GatherSpecFromUi();
                        std::filesystem::path builtExe;
                        rsw::CompiledMsiSummary sum{};
                        std::string err;
                        if (BuildSelfExtractingExe(spec, self->exeDir, builtExe, sum, err)) {
                            self->SetStatus(L"[OK] Built LZMS Self-Extracting .EXE (" + std::to_wstring(sum.fileCount) +
                                            L" file(s), " + rsw::Utf8ToWide(sum.architecture) + L"):\r\n" + builtExe.wstring());
                        } else {
                            self->SetStatus(L"[ERROR] " + rsw::Utf8ToWide(err));
                        }
                        return 0;
                    }
                    case IDC_SFX_EXPORT_RSIP: {
                        std::wstring outRsip = L"self-extractor.rsip";
                        OPENFILENAMEW ofn{};
                        wchar_t buf[MAX_PATH * 2] = L"self-extractor.rsip";
                        ofn.lStructSize = sizeof(ofn);
                        ofn.hwndOwner   = hWnd;
                        ofn.lpstrFilter = L"RaggieSoft Installer Project (*.rsip)\0*.rsip\0";
                        ofn.lpstrFile   = buf;
                        ofn.nMaxFile    = static_cast<DWORD>(std::size(buf));
                        ofn.lpstrDefExt = L"rsip";
                        ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
                        if (::GetSaveFileNameW(&ofn)) {
                            const std::string rsipTxt = SynthesizeSfxRsipText(self->GatherSpecFromUi());
                            std::ofstream out(buf, std::ios::binary | std::ios::trunc);
                            out.write(rsipTxt.data(), static_cast<std::streamsize>(rsipTxt.size()));
                            self->SetStatus(L"[OK] Exported Installer Studio project manifest: " + std::wstring(buf));
                        }
                        return 0;
                    }
                    case IDC_SFX_ABOUT_BTN: {
                        const std::wstring head = L"RaggieSoft Self-Extractor v" + std::wstring(rsw::kVersionStringWide);
                        const std::wstring body =
                            L"Standalone zero-dependency Win32 C++20 LZMS Self-Extracting Archive & %TEMP% Setup Builder (x64 & ARM64).\n\n"
                            L"• Mode A: Standard Folder / .ZIP Extractor with Dark, Light & High-Contrast WCAG 2.2 AA UI\n"
                            L"• Mode B: %TEMP% Software Setup Wrapper watching child cleanup helpers (_ISDEL.EXE) before scrubbing %TEMP%\n"
                            L"• Included in all editions of RaggieSoft Installer Studio (Community, Essentials, Developer, & Architect)\n\n"
                            L"Fictitious Persons & Entities Disclaimer: All narrative characters, bands, universities, and corporations across RaggieSoft creative works are fictitious.";
                        ::TaskDialog(hWnd, ::GetModuleHandleW(nullptr), L"About RaggieSoft Self-Extractor",
                                     head.c_str(), body.c_str(), TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
                        return 0;
                    }
                }
                break;
            }
            case WM_DESTROY:
                self->DestroyFonts();
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
    std::vector<std::string> args;
    if (argv) {
        for (int i = 1; i < argc; ++i) args.push_back(rsw::WideToUtf8(argv[i]));
        ::LocalFree(argv);
    }

    wchar_t selfBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
    const std::filesystem::path exeDir = std::filesystem::path(selfBuf).parent_path();

    // 1. Headless CLI & Automated Smoke-Test Modes!
    if (!args.empty()) {
        auto attachCon = []() {
            if (::AttachConsole(ATTACH_PARENT_PROCESS)) {
                FILE* fp = nullptr;
                freopen_s(&fp, "CONOUT$", "w", stdout);
                freopen_s(&fp, "CONOUT$", "w", stderr);
            }
            ::SetConsoleOutputCP(CP_UTF8);
        };

        if (args[0] == "--headless-smoke-test") {
            std::error_code ec;
            const auto tmpDir = std::filesystem::temp_directory_path(ec) /
                                (L"rswsfx-smoke-" + std::to_wstring(::GetCurrentProcessId()));
            const auto srcDir = tmpDir / L"src";
            std::filesystem::create_directories(srcDir, ec);
            {
                std::ofstream f(srcDir / L"ReadMe.txt", std::ios::binary | std::ios::trunc);
                f << "RaggieSoft Self-Extractor Smoke Verification Payload";
            }
            SfxBuildSpec spec{};
            spec.sourcePath    = srcDir;
            spec.outputExePath = tmpDir / L"smoke-archive-sfx.exe";
            spec.title         = "Smoke Test Archive";
            std::filesystem::path builtExe;
            rsw::CompiledMsiSummary sum{};
            std::string err;
            const bool ok = BuildSelfExtractingExe(spec, exeDir, builtExe, sum, err) &&
                            std::filesystem::exists(builtExe, ec) &&
                            std::filesystem::file_size(builtExe, ec) > 32768;
            std::filesystem::remove_all(tmpDir, ec);
            return ok ? 0 : 1;
        }

        if (args[0] == "--help" || args[0] == "-h" || args[0] == "/?") {
            attachCon();
            std::cout
                << "RaggieSoft Self-Extractor (rswsfx.exe v" << rsw::kVersionString << ")\n"
                << "Usage:\n"
                << "  rswsfx.exe                                              (Open Interactive Win32 Builder GUI)\n"
                << "  rswsfx.exe --pack <folder-or-zip> <output.exe> [--title=...] [--publisher=...] [--version=X.Y.Z]\n"
                << "             [--uac=asInvoker|highestAvailable|requireAdministrator] [--subdir=...]\n"
                << "             [--temp-install --run=<rel.exe> [--wait=\"_ISDEL.EXE; _INS*._MP\"]] [--open=<rel.doc>]\n"
                << "  rswsfx.exe --headless-smoke-test\n";
            return 0;
        }

        if (args[0] == "--pack") {
            attachCon();
            if (args.size() < 3) {
                std::cerr << "[ERROR] Usage: rswsfx.exe --pack <folder-or-zip> <output.exe> [options]\n";
                return 2;
            }
            SfxBuildSpec spec{};
            spec.sourcePath    = rsw::Utf8ToWide(args[1]);
            spec.outputExePath = rsw::Utf8ToWide(args[2]);
            spec.title         = rsw::WideToUtf8(spec.sourcePath.stem().wstring());
            for (std::size_t i = 3; i < args.size(); ++i) {
                const auto& a = args[i];
                if (a == "--temp-install")            spec.tempInstall   = true;
                else if (a.rfind("--title=", 0) == 0) spec.title         = a.substr(8);
                else if (a.rfind("--publisher=", 0)==0) spec.publisher   = a.substr(12);
                else if (a.rfind("--version=", 0) == 0) spec.version     = a.substr(10);
                else if (a.rfind("--uac=", 0) == 0)   spec.uacLevel      = a.substr(6);
                else if (a.rfind("--subdir=", 0) == 0)spec.defaultSubdir = a.substr(9);
                else if (a.rfind("--run=", 0) == 0)   spec.postAction    = a.substr(6);
                else if (a.rfind("--open=", 0) == 0)  spec.postAction    = a.substr(7);
                else if (a.rfind("--wait=", 0) == 0)  spec.waitProcesses = a.substr(7);
                else if (a.rfind("--arch=", 0) == 0)  spec.archOverride  = a.substr(7);
                else if (a.rfind("--icon=", 0) == 0)  spec.iconPath      = rsw::Utf8ToWide(a.substr(7));
            }
            std::filesystem::path builtExe;
            rsw::CompiledMsiSummary sum{};
            std::string err;
            if (!BuildSelfExtractingExe(spec, exeDir, builtExe, sum, err)) {
                std::cerr << "[ERROR] " << err << "\n";
                return 1;
            }
            std::cout << "[OK] Built Self-Extracting .EXE (" << sum.architecture << ", "
                      << sum.fileCount << " file(s)): " << rsw::WideToUtf8(builtExe.wstring()) << "\n";
            return 0;
        }
    }

    // 2. Interactive Dark/Light/High-Contrast Win32 Desktop GUI Mode!
    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    rsw::ui::ScopedGdiPlusSession gpSession;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
    ::InitCommonControlsEx(&icc);

    RswSfxWindow app{};
    app.exeDir = exeDir;
    app.theme.Refresh();

    constexpr const wchar_t* kCls = L"RaggieSoftSelfExtractorWindow";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &RswSfxWindow::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kCls;
    ::RegisterClassExW(&wc);

    const UINT dpi = ::GetDpiForSystem();
    const int winW = rsw::ui::ScalePx(780, dpi, app.theme.textScalePercent);
    const int winH = rsw::ui::ScalePx(610, dpi, app.theme.textScalePercent);
    const int posX = std::max<int>(0, (::GetSystemMetrics(SM_CXSCREEN) - winW) / 2);
    const int posY = std::max<int>(0, (::GetSystemMetrics(SM_CYSCREEN) - winH) / 2);

    HWND hWnd = ::CreateWindowExW(
        0, kCls, L"RaggieSoft Self-Extractor",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        posX, posY, winW, winH, nullptr, nullptr, hInstance, &app);
    if (!hWnd) {
        ::CoUninitialize();
        return 1;
    }

    ::ShowWindow(hWnd, nCmdShow);
    ::UpdateWindow(hWnd);

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
