// ================================================================================
// FILE: src/ide/RswIdeAeroWizards.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file?
//     This header implements all **4 Guided Native Win32 AeroWizards** inside
//     `InstallerStudio.exe` using Microsoft's genuine **`PSH_AEROWIZARD`** engine
//     (`PropertySheetW` in `<prsht.h>` + `comctl32.dll` v6):
//       1. **New Project Wizard ("Project Assistant")** — 5 guided AeroWizard pages
//          taking a developer from raw `.exe`/folder -> Start Menu/Desktop shortcuts
//          -> Document File Association -> AeroWizard/Glass UI style -> ready-to-build
//          commented `.rsip` manifest!
//       2. **File Association & Document Icon Wizard** — 3 guided AeroWizard pages
//          configuring `.ext`, `ProgId`, Explorer type description, MIME type,
//          document icon index, shell verbs (`&Open`, `&Edit`), and Clean `HKMU` vs.
//          Advertised `ProgId`/`Extension`/`Verb` registration!
//       3. **Component & KeyPath Wizard** — 3 guided AeroWizard pages enforcing
//          Microsoft's official Windows Installer Component Rules (Single-Directory
//          Rule, File KeyPath, ICE38 `HKCU` Registry KeyPath, or ICE18 `CreateFolder`
//          KeyPath + `Permanent` / `NeverOverwrite` flags)!
//       4. **Release, Transform (`.MST`) & Patch (`.MSP`) Wizard** — 3 guided
//          AeroWizard pages generating enterprise GPO Transforms (`.mst`), Minor
//          Upgrade Patches (`.msp`), or Dual-Arch Releases with one-click handoff
//          into `msiprism.exe`!
//
//   How do we build `PSH_AEROWIZARD` pages without cluttering `.rc` files?
//     Windows' `PROPSHEETPAGEW` supports `PSP_DLGINDIRECT`, which accepts a compact
//     `WORD`-aligned `DLGTEMPLATE` struct in memory (`310 x 176` dialog units, `9pt`
//     `Segoe UI`). Inside `WM_INITDIALOG`, we call `EnableThemeDialogTexture(hDlg,
//     ETDT_ENABLE)` so controls blend seamlessly onto the AeroWizard canvas and
//     convert dialog units to Per-Monitor V2 pixels via `MapDialogRect`!
// ================================================================================

#pragma once

#include "../common/RswCryptoAndPe.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswSchema.hpp"
#include "../common/RswWin32UiTheme.hpp"

#include <commdlg.h>
#include <prsht.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

namespace rsw::ide_wizards {

/// Builds an aligned in-memory `DLGTEMPLATE` (`310 x 176` DLU, `9pt Segoe UI`) for `PSP_DLGINDIRECT`!
inline std::vector<std::uint8_t> BuildBlankAeroPageTemplate(short widthDlu = 310, short heightDlu = 176) {
    std::vector<std::uint8_t> buf;
    auto pushBytes = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    };
    auto pushU16 = [&](std::uint16_t v) { pushBytes(&v, sizeof(v)); };

    DLGTEMPLATE hdr{};
    hdr.style           = DS_SETFONT | WS_CHILD | WS_DISABLED | WS_CAPTION;
    hdr.dwExtendedStyle = 0;
    hdr.cdit            = 0;
    hdr.x               = 0;
    hdr.y               = 0;
    hdr.cx              = widthDlu;
    hdr.cy              = heightDlu;
    pushBytes(&hdr, sizeof(hdr));

    pushU16(0); // No menu
    pushU16(0); // Default dialog window class
    pushU16(0); // Empty page caption (`pszHeaderTitle` provides the Aero Main Instruction!)
    pushU16(9); // 9pt font
    const wchar_t face[] = L"Segoe UI";
    for (wchar_t ch : face) {
        pushU16(static_cast<std::uint16_t>(ch));
    }
    while ((buf.size() % 4u) != 0u) buf.push_back(0u);
    return buf;
}

/// Helper creating DLU-mapped accessible controls inside a `PSH_AEROWIZARD` page!
struct AeroPageBuilder {
    HWND  hDlg  = nullptr;
    HFONT hFont = nullptr;

    explicit AeroPageBuilder(HWND dlg) noexcept : hDlg(dlg) {
        ::EnableThemeDialogTexture(hDlg, ETDT_ENABLE);
        hFont = reinterpret_cast<HFONT>(::SendMessageW(hDlg, WM_GETFONT, 0, 0));
    }

    RECT MapDlu(int x, int y, int w, int h) const noexcept {
        RECT r{x, y, x + w, y + h};
        ::MapDialogRect(hDlg, &r);
        return r;
    }

    HWND AddLabel(const wchar_t* text, int x, int y, int w, int h = 10) const {
        const RECT r = MapDlu(x, y, w, h);
        HWND hw = ::CreateWindowExW(
            0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
            r.left, r.top, r.right - r.left, r.bottom - r.top,
            hDlg, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        if (hFont) ::SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), FALSE);
        return hw;
    }

    HWND AddEdit(int id, const std::wstring& initial, int x, int y, int w, int h = 13) const {
        const RECT r = MapDlu(x, y, w, h);
        HWND hw = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", initial.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            r.left, r.top, r.right - r.left, r.bottom - r.top,
            hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ::GetModuleHandleW(nullptr), nullptr);
        if (hFont) ::SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), FALSE);
        return hw;
    }

    HWND AddCheck(int id, const wchar_t* text, bool checked, int x, int y, int w, int h = 12) const {
        const RECT r = MapDlu(x, y, w, h);
        HWND hw = ::CreateWindowExW(
            0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            r.left, r.top, r.right - r.left, r.bottom - r.top,
            hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ::GetModuleHandleW(nullptr), nullptr);
        if (hFont) ::SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), FALSE);
        ::SendMessageW(hw, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        return hw;
    }

    HWND AddCombo(int id, const std::vector<const wchar_t*>& items, int selIdx, int x, int y, int w, int dropH = 90) const {
        const RECT r = MapDlu(x, y, w, dropH);
        HWND hw = ::CreateWindowExW(
            0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            r.left, r.top, r.right - r.left, r.bottom - r.top,
            hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ::GetModuleHandleW(nullptr), nullptr);
        if (hFont) ::SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), FALSE);
        for (const wchar_t* it : items) {
            ::SendMessageW(hw, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(it));
        }
        ::SendMessageW(hw, CB_SETCURSEL, selIdx, 0);
        return hw;
    }

    HWND AddButton(int id, const wchar_t* text, int x, int y, int w, int h = 14) const {
        const RECT r = MapDlu(x, y, w, h);
        HWND hw = ::CreateWindowExW(
            0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            r.left, r.top, r.right - r.left, r.bottom - r.top,
            hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ::GetModuleHandleW(nullptr), nullptr);
        if (hFont) ::SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), FALSE);
        return hw;
    }
};

inline std::string ReadDlgItemUtf8(HWND hDlg, int id) {
    wchar_t buf[1024]{};
    ::GetDlgItemTextW(hDlg, id, buf, static_cast<int>(std::size(buf)));
    return TrimAscii(WideToUtf8(buf));
}

inline bool IsDlgItemChecked(HWND hDlg, int id) {
    return ::IsDlgButtonChecked(hDlg, id) == BST_CHECKED;
}

inline int GetComboIndex(HWND hDlg, int id) {
    const LRESULT sel = ::SendDlgItemMessageW(hDlg, id, CB_GETCURSEL, 0, 0);
    return (sel == CB_ERR) ? 0 : static_cast<int>(sel);
}

inline bool BrowsePathModal(HWND hOwner, bool pickFolder, const wchar_t* filter, std::wstring& outPath) {
    if (pickFolder) {
        IFileOpenDialog* pDlg = nullptr;
        if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))) && pDlg) {
            DWORD opts = 0;
            pDlg->GetOptions(&opts);
            pDlg->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
            if (SUCCEEDED(pDlg->Show(hOwner))) {
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
    ofn.hwndOwner   = hOwner;
    ofn.lpstrFilter = filter;
    ofn.lpstrFile   = buf;
    ofn.nMaxFile    = static_cast<DWORD>(std::size(buf));
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!::GetOpenFileNameW(&ofn)) return false;
    outPath = buf;
    return true;
}

// ================================================================================
// WIZARD 1: NEW PROJECT AEROWIZARD ("PROJECT ASSISTANT" — 5 GUIDED PAGES!)
// ================================================================================
struct NewProjectWizardData {
    std::string productName      = "My Desktop Application";
    std::string tagline          = "Fast, Accessible Native Windows Software";
    std::string manufacturer     = "Sample Studio";
    std::string websiteLabel     = "";
    std::string version          = "1.0.0";
    bool        perUserScope     = false;

    std::string payloadSource    = "..\\..\\bin\\$(ARCH)\\HelloRaggieSoft.exe";
    std::string defaultSubdir    = "Sample Studio\\My Desktop Application";
    std::string mainExecutable   = "HelloRaggieSoft.exe";

    bool        addStartMenuSc   = true;
    bool        addDesktopSc     = true;
    std::string startMenuFolder  = "Sample Studio";

    bool        registerFileExt  = false;
    std::string fileExtension    = ".mydoc";
    std::string progId           = "SampleStudio.MyApp.Document";
    std::string extDescription   = "My Application Document";
    bool        advertiseAssoc   = false;

    int         wizardLayoutIdx  = 1; // `0` = standard Glass, `1` = aerowizard, `2` = autorun16x9
    int         msiPresetIdx     = 0; // `0` = express_custom, `1` = minimal
    std::string primaryHex       = "#2563EB";
    std::string secondaryHex     = "#FBBF24";
    bool        buildImmediately = false;
    bool        completed        = false;
};

struct NewProjPageCtx {
    int                   pageIndex = 0;
    NewProjectWizardData* data      = nullptr;
};

inline INT_PTR CALLBACK NewProjectAeroPageProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<NewProjPageCtx*>(::GetWindowLongPtrW(hDlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        const auto* psp = reinterpret_cast<const PROPSHEETPAGEW*>(lParam);
        ctx = reinterpret_cast<NewProjPageCtx*>(psp->lParam);
        ::SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));

        AeroPageBuilder b(hDlg);
        auto& d = *ctx->data;
        switch (ctx->pageIndex) {
            case 0: // Identity
                b.AddLabel(L"Enter your product identity and Windows Installer scope:", 6, 4, 296, 12);
                b.AddLabel(L"&Product Name:",      6, 22, 92); b.AddEdit(101, Utf8ToWide(d.productName),  102, 20, 198);
                b.AddLabel(L"&Tagline / Subtitle:",6, 42, 92); b.AddEdit(102, Utf8ToWide(d.tagline),      102, 40, 198);
                b.AddLabel(L"&Manufacturer:",      6, 62, 92); b.AddEdit(103, Utf8ToWide(d.manufacturer), 102, 60, 198);
                b.AddLabel(L"MSI &Version (X.Y.Z):",6,82, 92); b.AddEdit(104, Utf8ToWide(d.version),      102, 80, 90);
                b.AddLabel(L"&Footer Website:",    6, 102,92); b.AddEdit(105, Utf8ToWide(d.websiteLabel), 102, 100,198);
                b.AddLabel(L"Install &Scope:",     6, 124,92);
                b.AddCombo(106, {L"machine — Per-Machine (Program Files, UAC)", L"user — Current User (LocalAppData, Zero UAC)"}, d.perUserScope ? 1 : 0, 102, 122, 198);
                break;

            case 1: // Payload & Destination
                b.AddLabel(L"Choose your primary payload file or published application folder:", 6, 4, 296, 12);
                b.AddLabel(L"&Source File or Folder:", 6, 22, 296);
                b.AddEdit(201, Utf8ToWide(d.payloadSource), 6, 34, 206);
                b.AddButton(202, L"Pick &File...", 216, 33, 42);
                b.AddButton(203, L"Pick &Dir...",  261, 33, 42);
                b.AddLabel(L"&Destination Subfolder (under Program Files / LocalAppData):", 6, 60, 296);
                b.AddEdit(204, Utf8ToWide(d.defaultSubdir), 6, 72, 296);
                b.AddLabel(L"&Main Executable Filename (relative to INSTALLDIR):", 6, 98, 296);
                b.AddEdit(205, Utf8ToWide(d.mainExecutable), 6, 110, 296);
                break;

            case 2: // Shortcuts
                b.AddLabel(L"Configure Start Menu and Desktop shortcuts (ICE38 & ICE64 compliant):", 6, 4, 296, 12);
                b.AddCheck(301, L"&Create a Start Menu shortcut", d.addStartMenuSc, 6, 26, 290);
                b.AddCheck(302, L"Create a &Desktop shortcut (user can toggle during Custom Setup)", d.addDesktopSc, 6, 46, 290);
                b.AddLabel(L"Start &Menu Folder Group:", 6, 74, 110);
                b.AddEdit(303, Utf8ToWide(d.startMenuFolder), 118, 72, 184);
                break;

            case 3: // Optional File Association
                b.AddLabel(L"Optionally associate a document file extension with your application:", 6, 4, 296, 12);
                b.AddCheck(401, L"&Register a file extension with Windows Explorer", d.registerFileExt, 6, 22, 290);
                b.AddLabel(L"File &Extension:",  6, 44, 92); b.AddEdit(402, Utf8ToWide(d.fileExtension),  102, 42, 80);
                b.AddLabel(L"&ProgId:",          6, 64, 92); b.AddEdit(403, Utf8ToWide(d.progId),         102, 62, 198);
                b.AddLabel(L"Type &Description:",6, 84, 92); b.AddEdit(404, Utf8ToWide(d.extDescription), 102, 82, 198);
                b.AddLabel(L"MSI &Mode:",        6, 106,92);
                b.AddCombo(405, {L"Clean HKMU\\Software\\Classes (Recommended — Zero Self-Repair Popups)", L"Advertised MSI ProgId / Extension / Verb Tables"}, d.advertiseAssoc ? 1 : 0, 102, 104, 198);
                break;

            case 4: // UI & Finish
                b.AddLabel(L"Choose your runtime Setup.exe style and MSI.DLL dialog workflow:", 6, 4, 296, 12);
                b.AddLabel(L"Setup.exe &Layout:", 6, 24, 92);
                b.AddCombo(501, {L"standard — RaggieSoft Midnight Navy Glass", L"aerowizard — Modern Windows AeroWizard Chrome", L"autorun16x9 — 640x480 16:9 Media Autorun"}, d.wizardLayoutIdx, 102, 22, 198);
                b.AddLabel(L"MSI.DLL &Workflow:", 6, 46, 92);
                b.AddCombo(502, {L"express_custom — Express / Compact / Custom + Feature Tree", L"minimal — Streamlined 3-Screen MSI"}, d.msiPresetIdx, 102, 44, 198);
                b.AddLabel(L"&Primary Hex:",      6, 68, 92); b.AddEdit(503, Utf8ToWide(d.primaryHex),   102, 66, 80);
                b.AddLabel(L"&Secondary Hex:",    6, 90, 92); b.AddEdit(504, Utf8ToWide(d.secondaryHex), 102, 88, 80);
                b.AddCheck(505, L"&Validate project immediately in IDE after generating manifest", true, 6, 118, 290);
                break;
        }
        return TRUE;
    }

    if (!ctx || !ctx->data) return FALSE;
    auto& d = *ctx->data;

    if (msg == WM_COMMAND) {
        const int id = LOWORD(wParam);
        if (id == 202 || id == 203) {
            std::wstring chosen;
            if (BrowsePathModal(hDlg, id == 203, L"Executables & All Files (*.*)\0*.*\0", chosen)) {
                ::SetDlgItemTextW(hDlg, 201, chosen.c_str());
                std::filesystem::path p(chosen);
                if (id == 202) {
                    ::SetDlgItemTextW(hDlg, 205, p.filename().wstring().c_str());
                }
            }
            return TRUE;
        }
    }

    if (msg == WM_NOTIFY) {
        const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
        if (nm->code == PSN_SETACTIVE) {
            DWORD btns = PSWIZB_BACK | PSWIZB_NEXT;
            if (ctx->pageIndex == 0) btns = PSWIZB_NEXT;
            if (ctx->pageIndex == 4) btns = PSWIZB_BACK | PSWIZB_FINISH;
            PropSheet_SetWizButtons(::GetParent(hDlg), btns);
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        if (nm->code == PSN_WIZNEXT || nm->code == PSN_WIZFINISH) {
            if (ctx->pageIndex == 0) {
                d.productName  = ReadDlgItemUtf8(hDlg, 101);
                d.tagline      = ReadDlgItemUtf8(hDlg, 102);
                d.manufacturer = ReadDlgItemUtf8(hDlg, 103);
                d.version      = ReadDlgItemUtf8(hDlg, 104);
                d.websiteLabel = ReadDlgItemUtf8(hDlg, 105);
                d.perUserScope = (GetComboIndex(hDlg, 106) == 1);
                if (!d.manufacturer.empty() && !d.productName.empty()) {
                    d.defaultSubdir   = d.manufacturer + "\\" + d.productName;
                    d.startMenuFolder = d.manufacturer;
                }
            } else if (ctx->pageIndex == 1) {
                d.payloadSource  = ReadDlgItemUtf8(hDlg, 201);
                d.defaultSubdir  = ReadDlgItemUtf8(hDlg, 204);
                d.mainExecutable = ReadDlgItemUtf8(hDlg, 205);
            } else if (ctx->pageIndex == 2) {
                d.addStartMenuSc  = IsDlgItemChecked(hDlg, 301);
                d.addDesktopSc    = IsDlgItemChecked(hDlg, 302);
                d.startMenuFolder = ReadDlgItemUtf8(hDlg, 303);
            } else if (ctx->pageIndex == 3) {
                d.registerFileExt = IsDlgItemChecked(hDlg, 401);
                d.fileExtension   = ReadDlgItemUtf8(hDlg, 402);
                d.progId          = ReadDlgItemUtf8(hDlg, 403);
                d.extDescription  = ReadDlgItemUtf8(hDlg, 404);
                d.advertiseAssoc  = (GetComboIndex(hDlg, 405) == 1);
            } else if (ctx->pageIndex == 4) {
                d.wizardLayoutIdx  = GetComboIndex(hDlg, 501);
                d.msiPresetIdx     = GetComboIndex(hDlg, 502);
                d.primaryHex       = ReadDlgItemUtf8(hDlg, 503);
                d.secondaryHex     = ReadDlgItemUtf8(hDlg, 504);
                d.buildImmediately = IsDlgItemChecked(hDlg, 505);
                d.completed        = true;
            }
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
    }
    return FALSE;
}

inline bool RunNewProjectAeroWizard(HWND hOwner, std::string& outGeneratedRsipUtf8, bool& outAutoValidate) {
    NewProjectWizardData data{};
    auto dlgt = BuildBlankAeroPageTemplate();
    const wchar_t* headers[5] = {
        L"Step 1 of 5 — Application Identity & Install Scope",
        L"Step 2 of 5 — Application Files & Destination Folder",
        L"Step 3 of 5 — Start Menu & Desktop Shortcuts",
        L"Step 4 of 5 — Document File Associations",
        L"Step 5 of 5 — AeroWizard, Glass & MSI Dialog Style"
    };

    NewProjPageCtx ctxs[5]{};
    HPROPSHEETPAGE hPages[5]{};
    for (int i = 0; i < 5; ++i) {
        ctxs[i] = {i, &data};
        PROPSHEETPAGEW psp{};
        psp.dwSize         = sizeof(psp);
        psp.dwFlags        = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_DLGINDIRECT;
        psp.hInstance      = ::GetModuleHandleW(nullptr);
        psp.pResource      = reinterpret_cast<LPCDLGTEMPLATEW>(dlgt.data());
        psp.pszHeaderTitle = headers[i];
        psp.pfnDlgProc     = &NewProjectAeroPageProc;
        psp.lParam         = reinterpret_cast<LPARAM>(&ctxs[i]);
        hPages[i]          = ::CreatePropertySheetPageW(&psp);
    }

    PROPSHEETHEADERW psh{};
    psh.dwSize     = sizeof(psh);
    psh.dwFlags    = PSH_WIZARD | PSH_AEROWIZARD;
    psh.hwndParent = hOwner;
    psh.hInstance  = ::GetModuleHandleW(nullptr);
    psh.pszCaption = L"RaggieSoft Project Assistant — New .RSIP Installer";
    psh.nPages     = 5;
    psh.nStartPage = 0;
    psh.phpage     = hPages;

    ::PropertySheetW(&psh);
    if (!data.completed) return false;

    const char* layoutStr = (data.wizardLayoutIdx == 1) ? "aerowizard" : ((data.wizardLayoutIdx == 2) ? "autorun16x9" : "standard");
    const char* msiPreset = (data.msiPresetIdx == 1) ? "minimal" : "express_custom";

    std::ostringstream s;
    s << "; ==============================================================================\r\n"
      << "; RaggieSoft Installer Project (.rsip) — Generated by Project Assistant\r\n"
      << "; All comments (';' & '#') and '@include' directives survive GUI edits intact!\r\n"
      << "; ==============================================================================\r\n\r\n"
      << "[Package]\r\n"
      << "Name              = " << data.productName << "\r\n"
      << "Tagline           = " << data.tagline << "\r\n";
    if (!data.websiteLabel.empty()) {
        s << "WebsiteLabel      = " << data.websiteLabel << "\r\n";
    }
    s << "Version           = " << data.version << "\r\n"
      << "Manufacturer      = " << data.manufacturer << "\r\n"
      << "UpgradeCode       = " << GenerateRandomMsiGuid() << " ; Permanent UpgradeCode across releases\r\n"
      << "Architecture      = $(ARCH)\r\n"
      << "Scope             = " << (data.perUserScope ? "user" : "machine") << "\r\n"
      << "BuildBootstrapper = true\r\n\r\n"
      << "[Requirements]\r\n"
      << "MinWindowsBuild   = 22000\r\n\r\n"
      << "[Install]\r\n"
      << "DefaultSubdir     = " << data.defaultSubdir << "\r\n"
      << "StartMenuFolder   = " << data.startMenuFolder << "\r\n";
    if (!data.mainExecutable.empty()) {
        s << "MainExecutable    = " << data.mainExecutable << "\r\n";
    }
    s << "\r\n[UI]\r\n"
      << "WizardLayout      = " << layoutStr << "\r\n"
      << "MsiDialogPreset   = " << msiPreset << "\r\n"
      << "AccentPrimaryHex  = " << data.primaryHex << "\r\n"
      << "AccentSecondaryHex= " << data.secondaryHex << "\r\n"
      << "InstallButtonText = &Install\r\n\r\n"
      << "[Feature.Main]\r\n"
      << "Title             = " << data.productName << " Core Files\r\n"
      << "Description       = Essential program binaries and resources.\r\n"
      << "Level             = 1\r\n\r\n"
      << "[Payload.Core]\r\n"
      << "Source            = " << data.payloadSource << "\r\n"
      << "TargetRoot        = INSTALLDIR\r\n"
      << "Feature           = Main\r\n";

    if (data.addStartMenuSc && !data.mainExecutable.empty()) {
        s << "\r\n[Shortcut.StartMenuLink]\r\n"
          << "Name              = " << data.productName << "\r\n"
          << "TargetFile        = " << data.mainExecutable << "\r\n"
          << "Location          = StartMenu\r\n";
    }
    if (data.addDesktopSc && !data.mainExecutable.empty()) {
        s << "\r\n[Shortcut.DesktopLink]\r\n"
          << "Name              = " << data.productName << "\r\n"
          << "TargetFile        = " << data.mainExecutable << "\r\n"
          << "Location          = Desktop\r\n";
    }
    if (data.registerFileExt && !data.fileExtension.empty() && !data.mainExecutable.empty()) {
        s << "\r\n[FileAssociation.PrimaryDoc]\r\n"
          << "Extension         = " << data.fileExtension << "\r\n"
          << "ProgId            = " << data.progId << "\r\n"
          << "Description       = " << data.extDescription << "\r\n"
          << "TargetFile        = " << data.mainExecutable << "\r\n"
          << "Verbs             = open:&Open:\"%1\"\r\n"
          << "Advertise         = " << (data.advertiseAssoc ? "true" : "false") << "\r\n";
    }

    outGeneratedRsipUtf8 = s.str();
    outAutoValidate      = data.buildImmediately;
    return true;
}

// ================================================================================
// WIZARD 2: FILE ASSOCIATION & DOCUMENT ICON AEROWIZARD (3 GUIDED PAGES!)
// ================================================================================
struct FileAssocWizardData {
    std::string sectionId     = "DocType1";
    std::string extension     = ".rsdoc";
    std::string progId        = "MyCompany.MyApp.Document";
    std::string description   = "Application Document";
    std::string contentType   = "";
    std::string perceivedType = "document";
    std::string targetExe     = "App.exe";
    std::string iconIndex     = "0";
    std::string feature       = "Main";
    std::string verbs         = "open:&Open:\"%1\"";
    bool        advertise     = false;
    bool        completed     = false;
};

struct FileAssocPageCtx {
    int                  pageIndex = 0;
    FileAssocWizardData* data      = nullptr;
};

inline INT_PTR CALLBACK FileAssocAeroPageProc(HWND hDlg, UINT msg, WPARAM /*wParam*/, LPARAM lParam) {
    auto* ctx = reinterpret_cast<FileAssocPageCtx*>(::GetWindowLongPtrW(hDlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        const auto* psp = reinterpret_cast<const PROPSHEETPAGEW*>(lParam);
        ctx = reinterpret_cast<FileAssocPageCtx*>(psp->lParam);
        ::SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));
        AeroPageBuilder b(hDlg);
        auto& d = *ctx->data;
        if (ctx->pageIndex == 0) {
            b.AddLabel(L"Define the file extension, ProgId, and MIME metadata:", 6, 4, 296, 12);
            b.AddLabel(L"&Section ID:",     6, 24, 92); b.AddEdit(601, Utf8ToWide(d.sectionId),     102, 22, 120);
            b.AddLabel(L"File &Extension:", 6, 46, 92); b.AddEdit(602, Utf8ToWide(d.extension),     102, 44, 80);
            b.AddLabel(L"&ProgId:",         6, 68, 92); b.AddEdit(603, Utf8ToWide(d.progId),        102, 66, 198);
            b.AddLabel(L"&Description:",    6, 90, 92); b.AddEdit(604, Utf8ToWide(d.description),   102, 88, 198);
            b.AddLabel(L"&MIME Type:",      6, 112,92); b.AddEdit(605, Utf8ToWide(d.contentType),   102, 110,198);
            b.AddLabel(L"Perceived &Type:", 6, 134,92); b.AddEdit(606, Utf8ToWide(d.perceivedType), 102, 132,120);
        } else if (ctx->pageIndex == 1) {
            b.AddLabel(L"Bind the extension to a packaged executable and feature:", 6, 4, 296, 12);
            b.AddLabel(L"&Target Exe (under INSTALLDIR):", 6, 26, 296);
            b.AddEdit(611, Utf8ToWide(d.targetExe), 6, 38, 296);
            b.AddLabel(L"Document &Icon Index:", 6, 66, 110); b.AddEdit(612, Utf8ToWide(d.iconIndex), 120, 64, 60);
            b.AddLabel(L"Owning &Feature ID:",   6, 90, 110); b.AddEdit(613, Utf8ToWide(d.feature),   120, 88, 120);
        } else {
            b.AddLabel(L"Configure Explorer context-menu verbs and MSI registration mode:", 6, 4, 296, 12);
            b.AddLabel(L"Shell &Verbs (verb:MenuLabel:Args | ...):", 6, 24, 296);
            b.AddEdit(621, Utf8ToWide(d.verbs), 6, 36, 296);
            b.AddLabel(L"Registration &Mode:", 6, 66, 296);
            b.AddCombo(622, {
                L"Clean HKMU\\Software\\Classes (Recommended — Per-User & Machine, Zero Repair Popups)",
                L"Advertised MSI ProgId / Extension / Verb / MIME SQL Tables"
            }, d.advertise ? 1 : 0, 6, 78, 296);
        }
        return TRUE;
    }
    if (!ctx || !ctx->data) return FALSE;
    auto& d = *ctx->data;
    if (msg == WM_NOTIFY) {
        const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
        if (nm->code == PSN_SETACTIVE) {
            DWORD btns = PSWIZB_BACK | PSWIZB_NEXT;
            if (ctx->pageIndex == 0) btns = PSWIZB_NEXT;
            if (ctx->pageIndex == 2) btns = PSWIZB_BACK | PSWIZB_FINISH;
            PropSheet_SetWizButtons(::GetParent(hDlg), btns);
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        if (nm->code == PSN_WIZNEXT || nm->code == PSN_WIZFINISH) {
            if (ctx->pageIndex == 0) {
                d.sectionId     = ReadDlgItemUtf8(hDlg, 601);
                d.extension     = ReadDlgItemUtf8(hDlg, 602);
                d.progId        = ReadDlgItemUtf8(hDlg, 603);
                d.description   = ReadDlgItemUtf8(hDlg, 604);
                d.contentType   = ReadDlgItemUtf8(hDlg, 605);
                d.perceivedType = ReadDlgItemUtf8(hDlg, 606);
            } else if (ctx->pageIndex == 1) {
                d.targetExe = ReadDlgItemUtf8(hDlg, 611);
                d.iconIndex = ReadDlgItemUtf8(hDlg, 612);
                d.feature   = ReadDlgItemUtf8(hDlg, 613);
            } else {
                d.verbs     = ReadDlgItemUtf8(hDlg, 621);
                d.advertise = (GetComboIndex(hDlg, 622) == 1);
                d.completed = true;
            }
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
    }
    return FALSE;
}

inline bool RunFileAssociationAeroWizard(HWND hOwner, RswManifestDocument& ioDoc) {
    FileAssocWizardData data{};
    data.sectionId = "Assoc" + std::to_string(ioDoc.GetNamedSectionIds("FileAssociation").size() + 1);
    const std::string mainExe = ioDoc.GetString("Install", "", "MainExecutable");
    if (!mainExe.empty()) data.targetExe = mainExe;

    auto dlgt = BuildBlankAeroPageTemplate();
    const wchar_t* headers[3] = {
        L"Step 1 of 3 — File Extension, ProgId & MIME Type",
        L"Step 2 of 3 — Handler Executable & Document Icon",
        L"Step 3 of 3 — Explorer Verbs & MSI Registration Mode"
    };
    FileAssocPageCtx ctxs[3]{};
    HPROPSHEETPAGE hPages[3]{};
    for (int i = 0; i < 3; ++i) {
        ctxs[i] = {i, &data};
        PROPSHEETPAGEW psp{};
        psp.dwSize         = sizeof(psp);
        psp.dwFlags        = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_DLGINDIRECT;
        psp.hInstance      = ::GetModuleHandleW(nullptr);
        psp.pResource      = reinterpret_cast<LPCDLGTEMPLATEW>(dlgt.data());
        psp.pszHeaderTitle = headers[i];
        psp.pfnDlgProc     = &FileAssocAeroPageProc;
        psp.lParam         = reinterpret_cast<LPARAM>(&ctxs[i]);
        hPages[i]          = ::CreatePropertySheetPageW(&psp);
    }

    PROPSHEETHEADERW psh{};
    psh.dwSize     = sizeof(psh);
    psh.dwFlags    = PSH_WIZARD | PSH_AEROWIZARD;
    psh.hwndParent = hOwner;
    psh.hInstance  = ::GetModuleHandleW(nullptr);
    psh.pszCaption = L"File Association & Document Icon Wizard";
    psh.nPages     = 3;
    psh.phpage     = hPages;
    ::PropertySheetW(&psh);
    if (!data.completed || data.sectionId.empty()) return false;

    ioDoc.SetString("FileAssociation", data.sectionId, "Extension",   data.extension);
    ioDoc.SetString("FileAssociation", data.sectionId, "ProgId",      data.progId);
    ioDoc.SetString("FileAssociation", data.sectionId, "Description", data.description);
    ioDoc.SetString("FileAssociation", data.sectionId, "TargetFile",  data.targetExe);
    ioDoc.SetString("FileAssociation", data.sectionId, "Verbs",       data.verbs);
    if (!data.contentType.empty())   ioDoc.SetString("FileAssociation", data.sectionId, "ContentType",   data.contentType);
    if (!data.perceivedType.empty()) ioDoc.SetString("FileAssociation", data.sectionId, "PerceivedType", data.perceivedType);
    ioDoc.SetString("FileAssociation", data.sectionId, "IconIndex",   data.iconIndex);
    ioDoc.SetString("FileAssociation", data.sectionId, "Advertise",   data.advertise ? "true" : "false");
    ioDoc.SetString("FileAssociation", data.sectionId, "Feature",     data.feature);
    return true;
}

// ================================================================================
// WIZARD 3: COMPONENT & KEYPATH AEROWIZARD (MICROSOFT COMPONENT RULES — 3 PAGES!)
// ================================================================================
struct ComponentWizardData {
    std::string compId        = "CoreComponent";
    std::string featureId     = "Main";
    int         rootIdx       = 0;
    std::string targetSubdir  = "";
    int         keyPathIdx    = 0; // 0=auto, 1=file, 2=registry, 3=folder (CreateFolder)
    std::string keyPathTarget = "";
    bool        permanent     = false;
    bool        neverOverwrite= false;
    bool        sharedRefCount= false;
    std::string condition     = "";
    bool        completed     = false;
};

struct CompPageCtx {
    int                  pageIndex = 0;
    ComponentWizardData* data      = nullptr;
};

inline constexpr const char* kRootIds[] = {
    "INSTALLDIR", "CommonFiles64Folder", "CommonAppDataFolder", "AppDataFolder",
    "LocalAppDataFolder", "ApplicationProgramsFolder", "DesktopFolder",
    "System64Folder", "FontsFolder"
};

inline INT_PTR CALLBACK ComponentAeroPageProc(HWND hDlg, UINT msg, WPARAM /*wParam*/, LPARAM lParam) {
    auto* ctx = reinterpret_cast<CompPageCtx*>(::GetWindowLongPtrW(hDlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        const auto* psp = reinterpret_cast<const PROPSHEETPAGEW*>(lParam);
        ctx = reinterpret_cast<CompPageCtx*>(psp->lParam);
        ::SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));
        AeroPageBuilder b(hDlg);
        auto& d = *ctx->data;
        if (ctx->pageIndex == 0) {
            b.AddLabel(L"Microsoft Rule #1: Every resource in an MSI Component must share ONE single destination directory:", 6, 4, 296, 20);
            b.AddLabel(L"&Component ID:",   6, 30, 92); b.AddEdit(701, Utf8ToWide(d.compId),    102, 28, 150);
            b.AddLabel(L"Owning &Feature:", 6, 52, 92); b.AddEdit(702, Utf8ToWide(d.featureId), 102, 50, 150);
            b.AddLabel(L"Destination &Root:",6,74, 92);
            b.AddCombo(703, {
                L"INSTALLDIR — Application Folder",
                L"CommonFiles64Folder — Common Files",
                L"CommonAppDataFolder — ProgramData (All Users)",
                L"AppDataFolder — Roaming AppData (ICE38 HKCU)",
                L"LocalAppDataFolder — LocalAppData (ICE38 HKCU)",
                L"ApplicationProgramsFolder — Start Menu (ICE38)",
                L"DesktopFolder — User Desktop (ICE38)",
                L"System64Folder — Windows System32",
                L"FontsFolder — Windows Fonts (Auto RegisterFonts)"
            }, d.rootIdx, 102, 72, 198);
            b.AddLabel(L"Target &Subfolder:",6, 98, 92); b.AddEdit(704, Utf8ToWide(d.targetSubdir), 102, 96, 198);
        } else if (ctx->pageIndex == 1) {
            b.AddLabel(L"Choose how Windows Installer detects this Component's KeyPath health (ICE18 & ICE38):", 6, 4, 296, 20);
            b.AddLabel(L"&KeyPath Kind:", 6, 30, 92);
            b.AddCombo(711, {
                L"auto — Automatic (File in machine dir / HKCU Registry in user dir)",
                L"file — Explicit KeyFile filename inside this Component's folder",
                L"registry — Explicit [Registry.<id>] KeyPath (Required by ICE38 for user dirs)",
                L"folder — Empty Folder KeyPath (Emits CreateFolder row per ICE18)"
            }, d.keyPathIdx, 102, 28, 198);
            b.AddLabel(L"KeyPath &Target (Filename for 'file', or Registry ID for 'registry'):", 6, 58, 296);
            b.AddEdit(712, Utf8ToWide(d.keyPathTarget), 6, 70, 296);
        } else {
            b.AddLabel(L"Configure optional Windows Installer lifecycle flags and install condition:", 6, 4, 296, 14);
            b.AddCheck(721, L"&Permanent (msidbComponentAttributesPermanent — Keep on disk during uninstall)", d.permanent, 6, 26, 296);
            b.AddCheck(722, L"&Never Overwrite (Preserve user-edited config file or registry key if present)", d.neverOverwrite, 6, 46, 296);
            b.AddCheck(723, L"&Shared DLL Reference Count (msidbComponentAttributesSharedDllRefCount)", d.sharedRefCount, 6, 66, 296);
            b.AddLabel(L"Optional MSI &Condition (e.g. DESKTOP_SHORTCUT = \"1\"):", 6, 94, 296);
            b.AddEdit(724, Utf8ToWide(d.condition), 6, 106, 296);
        }
        return TRUE;
    }
    if (!ctx || !ctx->data) return FALSE;
    auto& d = *ctx->data;
    if (msg == WM_NOTIFY) {
        const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
        if (nm->code == PSN_SETACTIVE) {
            DWORD btns = PSWIZB_BACK | PSWIZB_NEXT;
            if (ctx->pageIndex == 0) btns = PSWIZB_NEXT;
            if (ctx->pageIndex == 2) btns = PSWIZB_BACK | PSWIZB_FINISH;
            PropSheet_SetWizButtons(::GetParent(hDlg), btns);
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        if (nm->code == PSN_WIZNEXT || nm->code == PSN_WIZFINISH) {
            if (ctx->pageIndex == 0) {
                d.compId       = ReadDlgItemUtf8(hDlg, 701);
                d.featureId    = ReadDlgItemUtf8(hDlg, 702);
                d.rootIdx      = GetComboIndex(hDlg, 703);
                d.targetSubdir = ReadDlgItemUtf8(hDlg, 704);
            } else if (ctx->pageIndex == 1) {
                d.keyPathIdx    = GetComboIndex(hDlg, 711);
                d.keyPathTarget = ReadDlgItemUtf8(hDlg, 712);
            } else {
                d.permanent      = IsDlgItemChecked(hDlg, 721);
                d.neverOverwrite = IsDlgItemChecked(hDlg, 722);
                d.sharedRefCount = IsDlgItemChecked(hDlg, 723);
                d.condition      = ReadDlgItemUtf8(hDlg, 724);
                d.completed      = true;
            }
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
    }
    return FALSE;
}

inline bool RunComponentKeyPathAeroWizard(HWND hOwner, RswManifestDocument& ioDoc) {
    ComponentWizardData data{};
    data.compId = "Comp" + std::to_string(ioDoc.GetNamedSectionIds("Component").size() + 1);

    auto dlgt = BuildBlankAeroPageTemplate();
    const wchar_t* headers[3] = {
        L"Step 1 of 3 — Component Identity & Destination Directory",
        L"Step 2 of 3 — KeyFile, Registry, or Folder KeyPath",
        L"Step 3 of 3 — Lifecycle Attributes & Condition"
    };
    CompPageCtx ctxs[3]{};
    HPROPSHEETPAGE hPages[3]{};
    for (int i = 0; i < 3; ++i) {
        ctxs[i] = {i, &data};
        PROPSHEETPAGEW psp{};
        psp.dwSize         = sizeof(psp);
        psp.dwFlags        = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_DLGINDIRECT;
        psp.hInstance      = ::GetModuleHandleW(nullptr);
        psp.pResource      = reinterpret_cast<LPCDLGTEMPLATEW>(dlgt.data());
        psp.pszHeaderTitle = headers[i];
        psp.pfnDlgProc     = &ComponentAeroPageProc;
        psp.lParam         = reinterpret_cast<LPARAM>(&ctxs[i]);
        hPages[i]          = ::CreatePropertySheetPageW(&psp);
    }

    PROPSHEETHEADERW psh{};
    psh.dwSize     = sizeof(psh);
    psh.dwFlags    = PSH_WIZARD | PSH_AEROWIZARD;
    psh.hwndParent = hOwner;
    psh.hInstance  = ::GetModuleHandleW(nullptr);
    psh.pszCaption = L"Component & KeyPath Wizard (Microsoft MSI Rules)";
    psh.nPages     = 3;
    psh.phpage     = hPages;
    ::PropertySheetW(&psh);
    if (!data.completed || data.compId.empty()) return false;

    const char* kpNames[] = {"auto", "file", "registry", "folder"};
    const char* rootStr   = kRootIds[std::clamp(data.rootIdx, 0, 8)];

    ioDoc.SetString("Component", data.compId, "Feature",     data.featureId.empty() ? "Main" : data.featureId);
    ioDoc.SetString("Component", data.compId, "TargetRoot",  rootStr);
    if (!data.targetSubdir.empty()) {
        ioDoc.SetString("Component", data.compId, "TargetSubdir", data.targetSubdir);
    }
    ioDoc.SetString("Component", data.compId, "Guid",        "*");
    ioDoc.SetString("Component", data.compId, "KeyPathKind", kpNames[std::clamp(data.keyPathIdx, 0, 3)]);
    if (!data.keyPathTarget.empty()) {
        ioDoc.SetString("Component", data.compId, "KeyPathTarget", data.keyPathTarget);
    }
    if (data.permanent)      ioDoc.SetString("Component", data.compId, "Permanent", "true");
    if (data.neverOverwrite) ioDoc.SetString("Component", data.compId, "NeverOverwrite", "true");
    if (data.sharedRefCount) ioDoc.SetString("Component", data.compId, "SharedDllRefCount", "true");
    if (!data.condition.empty()) ioDoc.SetString("Component", data.compId, "Condition", data.condition);
    return true;
}

// ================================================================================
// WIZARD 4: RELEASE, TRANSFORM (.MST) & PATCH (.MSP) AEROWIZARD (3 GUIDED PAGES!)
// ================================================================================
struct ReleaseWizardResult {
    int         modeIdx      = 0; // 0 = .mst Transform, 1 = .msp Patch, 2 = Full MSI + Setup.exe
    std::string baselineMsi;
    std::string targetMsi;
    std::string outputPath;
    bool        openInPrism  = true;
    bool        completed    = false;
};

struct RelPageCtx {
    int                  pageIndex = 0;
    ReleaseWizardResult* data      = nullptr;
};

inline INT_PTR CALLBACK ReleaseAeroPageProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<RelPageCtx*>(::GetWindowLongPtrW(hDlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        const auto* psp = reinterpret_cast<const PROPSHEETPAGEW*>(lParam);
        ctx = reinterpret_cast<RelPageCtx*>(psp->lParam);
        ::SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));
        AeroPageBuilder b(hDlg);
        auto& d = *ctx->data;
        if (ctx->pageIndex == 0) {
            b.AddLabel(L"Select the enterprise Windows Installer artifact you want to build:", 6, 4, 296, 14);
            b.AddCombo(801, {
                L"Group Policy Transform (.mst) — Customize an existing MSI without re-signing",
                L"Minor Upgrade Patch (.msp) — Differential binary delta between two MSI versions",
                L"Full Enterprise Release (.msi + Setup.exe + VPAT 2.5 Conformance Report)"
            }, d.modeIdx, 6, 26, 296);
        } else if (ctx->pageIndex == 1) {
            b.AddLabel(L"Select your Baseline .msi and Customized/Upgraded Target .msi (for .mst / .msp):", 6, 4, 296, 14);
            b.AddLabel(L"&Baseline Released .MSI:", 6, 24, 296);
            b.AddEdit(811, Utf8ToWide(d.baselineMsi), 6, 36, 244);
            b.AddButton(812, L"&Browse...", 254, 35, 48);
            b.AddLabel(L"&Target / Upgraded .MSI:", 6, 64, 296);
            b.AddEdit(813, Utf8ToWide(d.targetMsi), 6, 76, 244);
            b.AddButton(814, L"B&rowse...", 254, 75, 48);
        } else {
            b.AddLabel(L"Confirm post-build verification and MSI Prism inspection:", 6, 4, 296, 14);
            b.AddCheck(821, L"&Open generated .mst / .msp / .msi database in RaggieSoft MSI Prism", d.openInPrism, 6, 28, 296);
        }
        return TRUE;
    }
    if (!ctx || !ctx->data) return FALSE;
    auto& d = *ctx->data;
    if (msg == WM_COMMAND) {
        const int id = LOWORD(wParam);
        if (id == 812 || id == 814) {
            std::wstring p;
            if (BrowsePathModal(hDlg, false, L"MSI Databases (*.msi)\0*.msi\0All Files (*.*)\0*.*\0", p)) {
                ::SetDlgItemTextW(hDlg, (id == 812) ? 811 : 813, p.c_str());
            }
            return TRUE;
        }
    }
    if (msg == WM_NOTIFY) {
        const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
        if (nm->code == PSN_SETACTIVE) {
            DWORD btns = PSWIZB_BACK | PSWIZB_NEXT;
            if (ctx->pageIndex == 0) btns = PSWIZB_NEXT;
            if (ctx->pageIndex == 2) btns = PSWIZB_BACK | PSWIZB_FINISH;
            PropSheet_SetWizButtons(::GetParent(hDlg), btns);
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        if (nm->code == PSN_WIZNEXT || nm->code == PSN_WIZFINISH) {
            if (ctx->pageIndex == 0) {
                d.modeIdx = GetComboIndex(hDlg, 801);
            } else if (ctx->pageIndex == 1) {
                d.baselineMsi = ReadDlgItemUtf8(hDlg, 811);
                d.targetMsi   = ReadDlgItemUtf8(hDlg, 813);
            } else {
                d.openInPrism = IsDlgItemChecked(hDlg, 821);
                d.completed   = true;
            }
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
    }
    return FALSE;
}

inline bool RunReleaseMstMspAeroWizard(HWND hOwner, ReleaseWizardResult& outRes) {
    outRes = ReleaseWizardResult{};
    auto dlgt = BuildBlankAeroPageTemplate();
    const wchar_t* headers[3] = {
        L"Step 1 of 3 — Choose Release, .MST Transform, or .MSP Patch",
        L"Step 2 of 3 — Baseline & Target MSI Packages",
        L"Step 3 of 3 — Generate & Inspect in MSI Prism"
    };
    RelPageCtx ctxs[3]{};
    HPROPSHEETPAGE hPages[3]{};
    for (int i = 0; i < 3; ++i) {
        ctxs[i] = {i, &outRes};
        PROPSHEETPAGEW psp{};
        psp.dwSize         = sizeof(psp);
        psp.dwFlags        = PSP_DEFAULT | PSP_USEHEADERTITLE | PSP_DLGINDIRECT;
        psp.hInstance      = ::GetModuleHandleW(nullptr);
        psp.pResource      = reinterpret_cast<LPCDLGTEMPLATEW>(dlgt.data());
        psp.pszHeaderTitle = headers[i];
        psp.pfnDlgProc     = &ReleaseAeroPageProc;
        psp.lParam         = reinterpret_cast<LPARAM>(&ctxs[i]);
        hPages[i]          = ::CreatePropertySheetPageW(&psp);
    }

    PROPSHEETHEADERW psh{};
    psh.dwSize     = sizeof(psh);
    psh.dwFlags    = PSH_WIZARD | PSH_AEROWIZARD;
    psh.hwndParent = hOwner;
    psh.hInstance  = ::GetModuleHandleW(nullptr);
    psh.pszCaption = L"Release, Transform (.MST) & Patch (.MSP) Wizard";
    psh.nPages     = 3;
    psh.phpage     = hPages;
    ::PropertySheetW(&psh);
    return outRes.completed;
}

} // namespace rsw::ide_wizards
