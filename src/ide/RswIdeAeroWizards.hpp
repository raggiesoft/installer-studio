// ================================================================================
// FILE: src/ide/RswIdeAeroWizards.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file?
//     This header implements all **6 Guided Native Win32 AeroWizards** inside
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
//       5. **Runtime Prerequisite & Merge Module (`.MSM`) Wizard** — 3 guided pages
//          covering `.NET 11`, `.NET 10 LTS`, `.NET 9`, `.NET 8 LTS`, `WebView2`,
//          `VC++`, Custom runtimes, and `.msm` Merge Modules!
//       6. **Self-Extracting `.EXE` (`PackageKind = sfx`) & Simple Native Setup
//          (`Engine = native`) Wizard** — 3 guided pages turning any folder or `.zip`
//          archive into an ultra-compact single-dialog self-extractor or instant
//          zero-MSI `Engine = native` installer with selectable UAC execution levels!
//
//   Why do we host AeroWizard pages in our own `ThemedAeroWizardModalHost`
//   instead of `comctl32.dll`'s legacy `PSH_AEROWIZARD` (`PropertySheetW`)?
//     Microsoft's Vista-era `PSH_AEROWIZARD` (`AEROWIZARD.msstyles`) hardcodes a
//     blinding `#FFFFFF` white background, `#F0F0F0` command bar, and `ETDT_ENABLE`
//     light tab texture even when Windows 11 is in Dark Mode! Our native C++20
//     `ThemedAeroWizardModalHost` hosts the exact same DLU-mapped `PSP_DLGINDIRECT`
//     child pages (`WM_INITDIALOG`, `PSN_SETACTIVE`, `PSN_WIZNEXT`, `PSN_WIZFINISH`,
//     and `PropSheet_SetWizButtons`) inside a Per-Monitor V2 DPI, `DWMWA_USE_IMMERSIVE_DARK_MODE`
//     AeroWizard frame (`DrawAeroWizardTopHeader` + `DrawGlassFooterBar` + `DarkMode_CFD`
//     edits/combos + accessible checkboxes/buttons + live `WM_SETTINGCHANGE` hot-swapping
//     + Windows Contrast Themes `SPI_GETHIGHCONTRAST`)!
// ================================================================================

#pragma once

#include "../common/RswCryptoAndPe.hpp"
#include "../common/RswGlassRenderer.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswPrerequisites.hpp"
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

inline constexpr const wchar_t* kAeroHostThemeProp = L"RswIdeAeroThemePtr";

namespace detail {

/// Subclasses each child AeroWizard dialog page (`DS_CONTROL | WS_CHILD`) so its background,
/// static labels, and `EDIT` / `COMBOBOX` fields honor Dark Mode, Light Mode, & High Contrast!
inline LRESULT CALLBACK AeroChildPageSubclassProc(
    HWND hPage,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR uIdSubclass,
    DWORD_PTR dwRefData) noexcept
{
    const auto* themePtr = reinterpret_cast<const ui::RswTheme*>(dwRefData);
    if (!themePtr) {
        return ::DefSubclassProc(hPage, uMsg, wParam, lParam);
    }

    switch (uMsg) {
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hPage, &AeroChildPageSubclassProc, uIdSubclass);
            return ::DefSubclassProc(hPage, uMsg, wParam, lParam);

        case WM_ERASEBKGND: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rc{};
            ::GetClientRect(hPage, &rc);
            ::FillRect(hdc, &rc, themePtr->hbrWindow);
            return 1;
        }

        case WM_CTLCOLORDLG:
            return reinterpret_cast<LRESULT>(themePtr->hbrWindow);

        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            ::SetTextColor(hdc, themePtr->textPrimary);
            ::SetBkColor(hdc, themePtr->windowBg);
            return reinterpret_cast<LRESULT>(themePtr->hbrWindow);
        }

        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORLISTBOX: {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            ::SetTextColor(hdc, themePtr->textPrimary);
            ::SetBkColor(hdc, themePtr->cardBg);
            return reinterpret_cast<LRESULT>(themePtr->hbrCard);
        }
    }
    return ::DefSubclassProc(hPage, uMsg, wParam, lParam);
}

inline void ApplyEditControlTheme(HWND hEdit, const ui::RswTheme& theme) noexcept {
    if (!hEdit) return;
    if (theme.isHighContrast) {
        ::SetWindowTheme(hEdit, nullptr, nullptr);
    } else {
        ::SetWindowTheme(hEdit, theme.isDarkMode ? L"DarkMode_CFD" : L"Explorer", nullptr);
    }
}

} // namespace detail

/// Builds an aligned in-memory `DLGTEMPLATE` (`312 x 176` DLU, `9pt Segoe UI`, `DS_CONTROL | WS_CHILD`)!
inline std::vector<std::uint8_t> BuildBlankAeroPageTemplate(short widthDlu = 312, short heightDlu = 176) {
    std::vector<std::uint8_t> buf;
    auto pushBytes = [&](const void* p, std::size_t n) {
        const auto* b = static_cast<const std::uint8_t*>(p);
        buf.insert(buf.end(), b, b + n);
    };
    auto pushU16 = [&](std::uint16_t v) { pushBytes(&v, sizeof(v)); };

    DLGTEMPLATE hdr{};
    hdr.style           = DS_SETFONT | DS_CONTROL | WS_CHILD;
    hdr.dwExtendedStyle = WS_EX_CONTROLPARENT;
    hdr.cdit            = 0;
    hdr.x               = 0;
    hdr.y               = 0;
    hdr.cx              = widthDlu;
    hdr.cy              = heightDlu;
    pushBytes(&hdr, sizeof(hdr));

    pushU16(0); // No menu
    pushU16(0); // Default dialog window class
    pushU16(0); // Empty page caption (`DrawAeroWizardTopHeader` renders the Aero Main Instruction!)
    pushU16(9); // 9pt font
    const wchar_t face[] = L"Segoe UI";
    for (wchar_t ch : face) {
        pushU16(static_cast<std::uint16_t>(ch));
    }
    while ((buf.size() % 4u) != 0u) buf.push_back(0u);
    return buf;
}

/// Helper creating DLU-mapped Dark/Light/High-Contrast accessible controls inside an AeroWizard page!
struct AeroPageBuilder {
    HWND                hDlg     = nullptr;
    HFONT               hFont    = nullptr;
    const ui::RswTheme* themePtr = nullptr;

    explicit AeroPageBuilder(HWND dlg) noexcept : hDlg(dlg) {
        hFont = reinterpret_cast<HFONT>(::SendMessageW(hDlg, WM_GETFONT, 0, 0));
        if (HWND hPar = ::GetParent(hDlg)) {
            themePtr = reinterpret_cast<const ui::RswTheme*>(::GetPropW(hPar, kAeroHostThemeProp));
        }
        if (themePtr) {
            ::SetWindowSubclass(hDlg, &detail::AeroChildPageSubclassProc, 0x52534157u, reinterpret_cast<DWORD_PTR>(themePtr));
        }
    }

    RECT MapDlu(int x, int y, int w, int h) const noexcept {
        RECT r{x, y, x + w, y + h};
        ::MapDialogRect(hDlg, &r);
        return r;
    }

    HWND AddLabel(const wchar_t* text, int x, int y, int w, int h = 11) const {
        const RECT r = MapDlu(x, y, w, h);
        HWND hw = ::CreateWindowExW(
            0, L"STATIC", text, WS_CHILD | WS_VISIBLE,
            r.left, r.top, r.right - r.left, r.bottom - r.top,
            hDlg, nullptr, ::GetModuleHandleW(nullptr), nullptr);
        if (hFont) ::SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), FALSE);
        return hw;
    }

    HWND AddEdit(int id, const std::wstring& initial, int x, int y, int w, int h = 14) const {
        const RECT r = MapDlu(x, y, w, h);
        HWND hw = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", initial.c_str(),
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            r.left, r.top, r.right - r.left, r.bottom - r.top,
            hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ::GetModuleHandleW(nullptr), nullptr);
        if (hFont) ::SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), FALSE);
        if (themePtr) detail::ApplyEditControlTheme(hw, *themePtr);
        return hw;
    }

    HWND AddCheck(int id, const wchar_t* text, bool checked, int x, int y, int w, int h = 14) const {
        const RECT r = MapDlu(x, y, w, h);
        HWND hw = ::CreateWindowExW(
            0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
            r.left, r.top, r.right - r.left, r.bottom - r.top,
            hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ::GetModuleHandleW(nullptr), nullptr);
        if (hFont) ::SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), FALSE);
        ::SendMessageW(hw, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
        if (themePtr) ui::AttachAccessibleCheckboxSubclass(hw, themePtr, false);
        return hw;
    }

    HWND AddCombo(int id, const std::vector<const wchar_t*>& items, int selIdx, int x, int y, int w, int dropH = 110) const {
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
        if (themePtr) ui::ApplyDarkComboBoxTheme(hw, *themePtr);
        return hw;
    }

    HWND AddButton(int id, const wchar_t* text, int x, int y, int w, int h = 15) const {
        const RECT r = MapDlu(x, y, w, h);
        HWND hw = ::CreateWindowExW(
            0, L"BUTTON", text, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
            r.left, r.top, r.right - r.left, r.bottom - r.top,
            hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), ::GetModuleHandleW(nullptr), nullptr);
        if (hFont) ::SendMessageW(hw, WM_SETFONT, reinterpret_cast<WPARAM>(hFont), FALSE);
        if (themePtr) ui::AttachAccessiblePushButtonSubclass(hw, themePtr, false, false);
        return hw;
    }
};

// ================================================================================
// NATIVE DARK / LIGHT / HIGH-CONTRAST AEROWIZARD MODAL HOST (`RunThemedAeroWizard`)
// ================================================================================
struct ThemedAeroWizardModalHost {
    static constexpr int IDC_AW_BACK = 3101;

    ui::RswTheme              theme{};
    HWND                      hOwner          = nullptr;
    bool                      wasOwnerEnabled = true;
    HWND                      hWnd            = nullptr;
    HWND                      hBtnBack        = nullptr;
    HWND                      hBtnNext        = nullptr;
    HWND                      hBtnCancel      = nullptr;
    HICON                     hIconBig        = nullptr;
    HICON                     hIconSmall      = nullptr;
    std::wstring              windowCaption;
    std::vector<std::wstring> pageHeaders;
    std::vector<HWND>         pageHwnds;
    ui::GlassGlyph            stepGlyph       = ui::GlassGlyph::Sparkle;
    COLORREF                  stepBadgeColor  = ui::brand::kRaggieBlue;
    int                       activePage      = 0;
    DWORD                     wizBtnFlags     = PSWIZB_NEXT;
    bool                      modalDone       = false;

    void ApplyWizardButtonFlags(DWORD flags) noexcept {
        wizBtnFlags = flags;
        if (hBtnBack) {
            ::EnableWindow(hBtnBack, (flags & PSWIZB_BACK) ? TRUE : FALSE);
        }
        if (hBtnNext) {
            const bool isFinish = (flags & (PSWIZB_FINISH | PSWIZB_DISABLEDFINISH)) != 0;
            ::SetWindowTextW(hBtnNext, isFinish ? L"&Finish" : L"&Next");
            const bool canAdvance = ((flags & PSWIZB_NEXT) != 0) || ((flags & PSWIZB_FINISH) != 0);
            ::EnableWindow(hBtnNext, canAdvance ? TRUE : FALSE);
        }
    }

    void LayoutHostAndActivePage() noexcept {
        if (!hWnd) return;
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return ui::ScalePx(px, dpi, theme.textScalePercent); };

        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(1, rc.right - rc.left);
        const int H = std::max<int>(1, rc.bottom - rc.top);

        const int footerH = S(48);
        const int footerY = H - footerH;
        const int btnY    = footerY + (footerH - S(30)) / 2;

        // Authentic AeroWizard (`PSH_AEROWIZARD`) Placement:
        //   - `hBtnBack` is the `28x28` circular `<-` Back button in the TOP-LEFT Aero command bar!
        //   - Bottom `AW_COMMANDAREA` holds ONLY `[Next]` / `[Finish]` and `[Cancel]` on the right!
        if (hBtnBack)   ::MoveWindow(hBtnBack,   S(10),     S(5), S(28), S(28), TRUE);
        if (hBtnNext)   ::MoveWindow(hBtnNext,   W - S(188), btnY, S(86), S(30), TRUE);
        if (hBtnCancel) ::MoveWindow(hBtnCancel, W - S(94),  btnY, S(78), S(30), TRUE);

        const int pageX = S(40);
        const int pageY = S(84);
        const int pageW = std::max<int>(S(200), W - pageX - S(18));
        const int pageH = std::max<int>(S(120), footerY - pageY - S(8));
        for (HWND hp : pageHwnds) {
            if (hp) ::MoveWindow(hp, pageX, pageY, pageW, pageH, TRUE);
        }
    }

    void ActivatePage(int targetIdx, bool focusFirstCtrl) noexcept {
        if (targetIdx < 0 || targetIdx >= static_cast<int>(pageHwnds.size())) return;
        activePage = targetIdx;

        const DWORD defFlags = (activePage > 0 ? PSWIZB_BACK : 0u) |
                               ((activePage + 1 == static_cast<int>(pageHwnds.size())) ? PSWIZB_FINISH : PSWIZB_NEXT);
        ApplyWizardButtonFlags(defFlags);

        for (int i = 0; i < static_cast<int>(pageHwnds.size()); ++i) {
            if (!pageHwnds[static_cast<std::size_t>(i)]) continue;
            ::ShowWindow(pageHwnds[static_cast<std::size_t>(i)], (i == activePage) ? SW_SHOW : SW_HIDE);
        }

        HWND hActive = pageHwnds[static_cast<std::size_t>(activePage)];
        if (hActive) {
            PSHNOTIFY psn{};
            psn.hdr.hwndFrom = hWnd;
            psn.hdr.idFrom   = 0;
            psn.hdr.code     = PSN_SETACTIVE;
            ::SetWindowLongPtrW(hActive, DWLP_MSGRESULT, 0);
            ::SendMessageW(hActive, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&psn));
            if (focusFirstCtrl) {
                HWND hFirst = ::GetNextDlgTabItem(hActive, nullptr, FALSE);
                if (hFirst) ::SetFocus(hFirst);
            }
        }
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    void CloseModal() noexcept {
        if (modalDone) return;
        modalDone = true;
        if (hOwner && ::IsWindow(hOwner) && wasOwnerEnabled) {
            ::EnableWindow(hOwner, TRUE);
            ::SetActiveWindow(hOwner);
        }
        if (hWnd && ::IsWindow(hWnd)) {
            ::DestroyWindow(hWnd);
        }
    }

    void OnNextOrFinishClicked() noexcept {
        if (activePage < 0 || activePage >= static_cast<int>(pageHwnds.size())) return;
        HWND hActive = pageHwnds[static_cast<std::size_t>(activePage)];
        if (!hActive) return;

        const bool isFinish = (wizBtnFlags & PSWIZB_FINISH) != 0;
        PSHNOTIFY psn{};
        psn.hdr.hwndFrom = hWnd;
        psn.hdr.idFrom   = 0;
        psn.hdr.code     = isFinish ? PSN_WIZFINISH : PSN_WIZNEXT;
        ::SetWindowLongPtrW(hActive, DWLP_MSGRESULT, 0);
        ::SendMessageW(hActive, WM_NOTIFY, 0, reinterpret_cast<LPARAM>(&psn));
        const LONG_PTR msgRes = ::GetWindowLongPtrW(hActive, DWLP_MSGRESULT);
        if (msgRes == -1) return; // Page vetoed navigation

        if (isFinish) {
            CloseModal();
        } else if (activePage + 1 < static_cast<int>(pageHwnds.size())) {
            ActivatePage(activePage + 1, true);
        }
    }

    void PaintHostChrome(HDC hdcOut) const noexcept {
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(1, rc.right - rc.left);
        const int H = std::max<int>(1, rc.bottom - rc.top);
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return ui::ScalePx(px, dpi, theme.textScalePercent); };

        HDC memDc      = ::CreateCompatibleDC(hdcOut);
        HBITMAP memBmp = ::CreateCompatibleBitmap(hdcOut, W, H);
        HGDIOBJ oldBmp = ::SelectObject(memDc, memBmp);

        ::FillRect(memDc, &rc, theme.hbrWindow);
        ::SetBkMode(memDc, TRANSPARENT);

        {
            Gdiplus::Graphics g(memDc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
            g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

            const std::wstring& curHeader =
                (activePage >= 0 && activePage < static_cast<int>(pageHeaders.size()))
                    ? pageHeaders[static_cast<std::size_t>(activePage)]
                    : windowCaption;
            ui::DrawAeroWizardTopHeader(
                memDc, g, W, dpi, theme,
                windowCaption, curHeader, hIconSmall ? hIconSmall : hIconBig,
                stepGlyph, stepBadgeColor, L"RaggieSoft", false, false);

            ui::DrawAeroWizardCommandFooter(memDc, W, H, S(48), theme);
        }

        ::BitBlt(hdcOut, 0, 0, W, H, memDc, 0, 0, SRCCOPY);
        ::SelectObject(memDc, oldBmp);
        ::DeleteObject(memBmp);
        ::DeleteDC(memDc);
    }

    static BOOL CALLBACK RefreshChildControlThemeCallback(HWND hChild, LPARAM lParam) noexcept {
        const auto* th = reinterpret_cast<const ui::RswTheme*>(lParam);
        if (!th) return TRUE;
        wchar_t cls[64]{};
        ::GetClassNameW(hChild, cls, static_cast<int>(std::size(cls)));
        if (_wcsicmp(cls, L"COMBOBOX") == 0) {
            ui::ApplyDarkComboBoxTheme(hChild, *th);
        } else if (_wcsicmp(cls, L"EDIT") == 0) {
            detail::ApplyEditControlTheme(hChild, *th);
        }
        ::InvalidateRect(hChild, nullptr, TRUE);
        return TRUE;
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        auto* self = reinterpret_cast<ThemedAeroWizardModalHost*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<ThemedAeroWizardModalHost*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hWnd = hWnd;
            ::SetPropW(hWnd, kAeroHostThemeProp, reinterpret_cast<HANDLE>(&self->theme));
            ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
            // Suppress duplicate Win32 non-client caption text/icon just like Microsoft's `PSH_AEROWIZARD`,
            // so the Wizard Title + Icon appear ONCE beside the top-left circular `<-` Back button!
            WTA_OPTIONS wtaOpts{};
            wtaOpts.dwFlags = WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON;
            wtaOpts.dwMask  = WTNCA_NODRAWCAPTION | WTNCA_NODRAWICON;
            ::SetWindowThemeAttribute(hWnd, WTA_NONCLIENT, &wtaOpts, sizeof(wtaOpts));
        }
        if (!self) return ::DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
            case WM_CREATE: {
                HINSTANCE hInst = ::GetModuleHandleW(nullptr);
                self->hBtnBack = ::CreateWindowExW(
                    0, L"BUTTON", L"&Back", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_AW_BACK)), hInst, nullptr);
                self->hBtnNext = ::CreateWindowExW(
                    0, L"BUTTON", L"&Next", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), hInst, nullptr);
                self->hBtnCancel = ::CreateWindowExW(
                    0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), hInst, nullptr);

                ui::AttachAccessiblePushButtonSubclass(self->hBtnBack,   &self->theme, false, false, true /* Aero Circular Back! */);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnNext,   &self->theme, true,  true);
                ui::AttachAccessiblePushButtonSubclass(self->hBtnCancel, &self->theme, false, true);
                return 0;
            }

            case PSM_SETWIZBUTTONS:
                self->ApplyWizardButtonFlags(static_cast<DWORD>(lParam));
                return 0;

            case WM_ERASEBKGND:
                return 1;

            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdc = ::BeginPaint(hWnd, &ps);
                self->PaintHostChrome(hdc);
                ::EndPaint(hWnd, &ps);
                return 0;
            }

            case WM_SIZE:
                self->LayoutHostAndActivePage();
                return 0;

            case WM_DPICHANGED: {
                const auto* suggested = reinterpret_cast<const RECT*>(lParam);
                if (suggested) {
                    ::SetWindowPos(hWnd, nullptr, suggested->left, suggested->top,
                                   suggested->right - suggested->left, suggested->bottom - suggested->top,
                                   SWP_NOZORDER | SWP_NOACTIVATE);
                }
                self->LayoutHostAndActivePage();
                ::InvalidateRect(hWnd, nullptr, TRUE);
                return 0;
            }

            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
            case WM_THEMECHANGED: {
                if (ui::IsLiveThemeBroadcast(msg, wParam, lParam)) {
                    self->theme.Refresh();
                    ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                    for (HWND hp : self->pageHwnds) {
                        if (hp) {
                            ::EnumChildWindows(hp, &RefreshChildControlThemeCallback, reinterpret_cast<LPARAM>(&self->theme));
                        }
                    }
                    ::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
                    return 0;
                }
                break;
            }

            case WM_COMMAND: {
                const int id = LOWORD(wParam);
                if (id == IDC_AW_BACK) {
                    if (self->activePage > 0) {
                        self->ActivatePage(self->activePage - 1, true);
                    }
                    return 0;
                }
                if (id == IDOK) {
                    if (self->hBtnNext && ::IsWindowEnabled(self->hBtnNext)) {
                        self->OnNextOrFinishClicked();
                    }
                    return 0;
                }
                if (id == IDCANCEL) {
                    self->CloseModal();
                    return 0;
                }
                break;
            }

            case WM_CLOSE:
                self->CloseModal();
                return 0;

            case WM_NCDESTROY:
                ::RemovePropW(hWnd, kAeroHostThemeProp);
                self->hWnd = nullptr;
                break;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

/// Runs a modal, Per-Monitor V2 DPI, Dark/Light/High-Contrast AeroWizard sheet hosting `pageCount` DLU pages!
inline void RunThemedAeroWizard(
    HWND hOwner,
    const wchar_t* windowCaption,
    int pageCount,
    const wchar_t* const* pageHeaders,
    DLGPROC pageDlgProc,
    const LPARAM* pageLParams,
    ui::GlassGlyph stepGlyph = ui::GlassGlyph::Sparkle,
    COLORREF stepBadgeColor  = ui::brand::kRaggieBlue,
    const std::filesystem::path* exportShotPath = nullptr)
{
    if (pageCount <= 0 || !pageHeaders || !pageDlgProc || !pageLParams) return;

    HINSTANCE hInst = ::GetModuleHandleW(nullptr);
    ThemedAeroWizardModalHost host{};
    host.theme.Refresh();
    host.hOwner          = hOwner;
    host.wasOwnerEnabled = (hOwner && ::IsWindow(hOwner)) ? (::IsWindowEnabled(hOwner) != FALSE) : false;
    host.windowCaption   = windowCaption ? windowCaption : L"RaggieSoft AeroWizard";
    host.stepGlyph       = stepGlyph;
    host.stepBadgeColor  = stepBadgeColor;
    for (int i = 0; i < pageCount; ++i) {
        host.pageHeaders.emplace_back(pageHeaders[i] ? pageHeaders[i] : L"");
    }

    host.hIconBig   = static_cast<HICON>(::LoadImageW(hInst, MAKEINTRESOURCEW(IDI_RAGGIESOFT_APP_ICON), IMAGE_ICON, 48, 48, LR_DEFAULTCOLOR));
    host.hIconSmall = static_cast<HICON>(::LoadImageW(hInst, MAKEINTRESOURCEW(IDI_RAGGIESOFT_APP_ICON), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR));
    if (!host.hIconBig) host.hIconBig = ::LoadIconW(nullptr, IDI_APPLICATION);

    constexpr const wchar_t* kHostCls = L"RaggieSoftThemedAeroWizardWindow";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &ThemedAeroWizardModalHost::WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = host.hIconBig;
    wc.hIconSm       = host.hIconSmall ? host.hIconSmall : host.hIconBig;
    wc.lpszClassName = kHostCls;
    ::RegisterClassExW(&wc);

    const UINT dpi = (hOwner && ::IsWindow(hOwner)) ? ::GetDpiForWindow(hOwner) : ::GetDpiForSystem();
    RECT wRc{0, 0,
             ui::ScalePx(656, dpi, host.theme.textScalePercent),
             ui::ScalePx(484, dpi, host.theme.textScalePercent)};
    constexpr DWORD kWinStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    constexpr DWORD kExStyle  = WS_EX_DLGMODALFRAME;
    ::AdjustWindowRectExForDpi(&wRc, kWinStyle, FALSE, kExStyle, dpi);
    const int winW = wRc.right - wRc.left;
    const int winH = wRc.bottom - wRc.top;

    int posX = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - winW) / 2);
    int posY = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - winH) / 2);
    if (hOwner && ::IsWindow(hOwner)) {
        RECT ow{};
        if (::GetWindowRect(hOwner, &ow)) {
            posX = ow.left + ((ow.right - ow.left) - winW) / 2;
            posY = ow.top  + ((ow.bottom - ow.top) - winH) / 2;
        }
        if (host.wasOwnerEnabled && !exportShotPath) {
            ::EnableWindow(hOwner, FALSE);
        }
    }

    HWND hHost = ::CreateWindowExW(
        kExStyle, kHostCls, host.windowCaption.c_str(), kWinStyle,
        posX, posY, winW, winH, hOwner, nullptr, hInst, &host);
    if (!hHost) {
        if (hOwner && host.wasOwnerEnabled) ::EnableWindow(hOwner, TRUE);
        return;
    }

    auto dlgt = BuildBlankAeroPageTemplate();
    std::vector<PROPSHEETPAGEW> fakePsps(static_cast<std::size_t>(pageCount));
    host.pageHwnds.resize(static_cast<std::size_t>(pageCount), nullptr);
    for (int i = 0; i < pageCount; ++i) {
        PROPSHEETPAGEW& psp = fakePsps[static_cast<std::size_t>(i)];
        psp.dwSize         = sizeof(psp);
        psp.hInstance      = hInst;
        psp.pszHeaderTitle = pageHeaders[i];
        psp.pfnDlgProc     = pageDlgProc;
        psp.lParam         = pageLParams[i];
        host.pageHwnds[static_cast<std::size_t>(i)] = ::CreateDialogIndirectParamW(
            hInst,
            reinterpret_cast<LPCDLGTEMPLATEW>(dlgt.data()),
            hHost,
            pageDlgProc,
            reinterpret_cast<LPARAM>(&psp));
    }

    host.LayoutHostAndActivePage();
    host.ActivatePage(0, !exportShotPath);
    ::ShowWindow(hHost, SW_SHOWNORMAL);
    ::UpdateWindow(hHost);

    if (exportShotPath) {
        ui::CaptureWindowToPngFile(hHost, *exportShotPath);
        if (pageCount >= 2) {
            host.ActivatePage(1, false);
            const auto step2Path = exportShotPath->parent_path() /
                                   (exportShotPath->stem().wstring() + L"-step2" + exportShotPath->extension().wstring());
            ui::CaptureWindowToPngFile(hHost, step2Path);
        }
        host.CloseModal();
        return;
    }

    MSG msg{};
    while (!host.modalDone && ::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) {
            host.CloseModal();
            break;
        }
        if (msg.message == WM_SYSKEYDOWN && msg.wParam == VK_LEFT && host.activePage > 0) {
            host.ActivatePage(host.activePage - 1, true);
            continue;
        }
        if (!::IsDialogMessageW(hHost, &msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    if (!host.modalDone) {
        host.CloseModal();
    }
}

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

inline bool RunNewProjectAeroWizard(
    HWND hOwner,
    std::string& outGeneratedRsipUtf8,
    bool& outAutoValidate,
    const std::filesystem::path* exportShotPath = nullptr)
{
    NewProjectWizardData data{};
    const wchar_t* headers[5] = {
        L"Step 1 of 5 — Application Identity & Install Scope",
        L"Step 2 of 5 — Application Files & Destination Folder",
        L"Step 3 of 5 — Start Menu & Desktop Shortcuts",
        L"Step 4 of 5 — Document File Associations",
        L"Step 5 of 5 — AeroWizard, Glass & MSI Dialog Style"
    };

    NewProjPageCtx ctxs[5]{};
    LPARAM lParams[5]{};
    for (int i = 0; i < 5; ++i) {
        ctxs[i]    = {i, &data};
        lParams[i] = reinterpret_cast<LPARAM>(&ctxs[i]);
    }
    RunThemedAeroWizard(
        hOwner, L"RaggieSoft Project Assistant — New .RSIP Installer",
        5, headers, &NewProjectAeroPageProc, lParams,
        ui::GlassGlyph::Sparkle, ui::brand::kRaggieBlue, exportShotPath);
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

    const wchar_t* headers[3] = {
        L"Step 1 of 3 — File Extension, ProgId & MIME Type",
        L"Step 2 of 3 — Handler Executable & Document Icon",
        L"Step 3 of 3 — Explorer Verbs & MSI Registration Mode"
    };
    FileAssocPageCtx ctxs[3]{};
    LPARAM lParams[3]{};
    for (int i = 0; i < 3; ++i) {
        ctxs[i]    = {i, &data};
        lParams[i] = reinterpret_cast<LPARAM>(&ctxs[i]);
    }
    RunThemedAeroWizard(
        hOwner, L"File Association & Document Icon Wizard",
        3, headers, &FileAssocAeroPageProc, lParams,
        ui::GlassGlyph::PinGrid, ui::brand::kVioletTune);
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

    const wchar_t* headers[3] = {
        L"Step 1 of 3 — Component Identity & Destination Directory",
        L"Step 2 of 3 — KeyFile, Registry, or Folder KeyPath",
        L"Step 3 of 3 — Lifecycle Attributes & Condition"
    };
    CompPageCtx ctxs[3]{};
    LPARAM lParams[3]{};
    for (int i = 0; i < 3; ++i) {
        ctxs[i]    = {i, &data};
        lParams[i] = reinterpret_cast<LPARAM>(&ctxs[i]);
    }
    RunThemedAeroWizard(
        hOwner, L"Component & KeyPath Wizard (Microsoft MSI Rules)",
        3, headers, &ComponentAeroPageProc, lParams,
        ui::GlassGlyph::Blocks, ui::brand::kEmeraldOk);
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
    const wchar_t* headers[3] = {
        L"Step 1 of 3 — Choose Release, .MST Transform, or .MSP Patch",
        L"Step 2 of 3 — Baseline & Target MSI Packages",
        L"Step 3 of 3 — Generate & Inspect in MSI Prism"
    };
    RelPageCtx ctxs[3]{};
    LPARAM lParams[3]{};
    for (int i = 0; i < 3; ++i) {
        ctxs[i]    = {i, &outRes};
        lParams[i] = reinterpret_cast<LPARAM>(&ctxs[i]);
    }
    RunThemedAeroWizard(
        hOwner, L"Release, Transform (.MST) & Patch (.MSP) Wizard",
        3, headers, &ReleaseAeroPageProc, lParams,
        ui::GlassGlyph::ShieldCheck, ui::brand::kEmeraldOk);
    return outRes.completed;
}

// ================================================================================
// WIZARD 5: RUNTIME PREREQUISITE & MERGE MODULE (`.MSM`) AEROWIZARD (3 PAGES!)
// ================================================================================
struct PrereqMsmWizardData {
    int         kindIdx       = 0; // 0..9 = Curated Presets, 10 = Custom Runtime, 11 = Merge Module (.msm)
    std::string sectionId     = "DotNet11Desktop";
    int         distIdx       = 0; // 0 = download, 1 = embedded, 2 = side-by-side
    std::string urlOrSource   = "https://aka.ms/dotnet/11.0/windowsdesktop-runtime-win-$(ARCH).exe";
    std::string silentArgs    = "/install /quiet /norestart";
    std::string successCodes  = "0, 1638";
    std::string rebootCodes   = "3010, 1641, 23";
    std::string msmFeature    = "Main";
    bool        completed     = false;
};

struct PrereqMsmPageCtx {
    int                  pageIndex = 0;
    PrereqMsmWizardData* data      = nullptr;
};

inline INT_PTR CALLBACK PrereqMsmAeroPageProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<PrereqMsmPageCtx*>(::GetWindowLongPtrW(hDlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        const auto* psp = reinterpret_cast<const PROPSHEETPAGEW*>(lParam);
        ctx = reinterpret_cast<PrereqMsmPageCtx*>(psp->lParam);
        ::SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));
        AeroPageBuilder b(hDlg);
        auto& d = *ctx->data;

        if (ctx->pageIndex == 0) {
            b.AddLabel(L"Choose a curated Microsoft runtime (.NET 11 / .NET 10 LTS), custom runtime, or .MSM:", 6, 4, 296, 14);
            b.AddLabel(L"Runtime / Module &Preset:", 6, 22, 296);
            b.AddCombo(901, {
                L"Microsoft .NET 11.0 Desktop Runtime (aka.ms x64 & ARM64)",
                L"Microsoft ASP.NET Core 11.0 Runtime (aka.ms x64 & ARM64)",
                L"Microsoft .NET 11.0 Base Console Runtime (aka.ms x64 & ARM64)",
                L"Microsoft .NET 10.0 LTS Desktop Runtime (aka.ms x64 & ARM64)",
                L"Microsoft ASP.NET Core 10.0 LTS Runtime (aka.ms x64 & ARM64)",
                L"Microsoft .NET 10.0 LTS Base Console Runtime (aka.ms x64 & ARM64)",
                L"Microsoft .NET 9.0 Desktop Runtime (aka.ms x64 & ARM64)",
                L"Microsoft .NET 8.0 LTS Desktop Runtime (aka.ms x64 & ARM64)",
                L"Microsoft Edge WebView2 Evergreen Bootstrapper",
                L"Microsoft Visual C++ 2015-2022 Redistributable (x64 & ARM64)",
                L"Custom Runtime Installer (.exe or .msi — runs BEFORE Product.msi)",
                L"Windows Installer Merge Module (.msm — merged into Product.msi)"
            }, d.kindIdx, 6, 34, 296, 140);
            b.AddLabel(L"&Section Identifier:", 6, 66, 110);
            b.AddEdit(902, Utf8ToWide(d.sectionId), 120, 64, 150);
            b.AddLabel(L"Note: Runtime prerequisites execute BEFORE msiexec so nested MSI redistributables never hit mutex error 1618.", 6, 94, 296, 28);
        } else if (ctx->pageIndex == 1) {
            b.AddLabel(L"Configure how Setup acquires this runtime or .MSM Merge Module:", 6, 4, 296, 14);
            b.AddLabel(L"&Delivery Mode (for Runtime Prerequisites):", 6, 22, 296);
            b.AddCombo(911, {
                L"download — Stream from official HTTPS URL on demand (zero Setup.exe bloat)",
                L"embedded — Compress with LZMS inside Setup.exe (100% offline / air-gapped)",
                L"side-by-side — Read from Redist\\ folder next to Setup.exe (with HTTPS fallback)"
            }, d.distIdx, 6, 34, 296);
            b.AddLabel(L"HTTPS &URL or Local .exe / .msi / .msm Source Path:", 6, 64, 296);
            b.AddEdit(912, Utf8ToWide(d.urlOrSource), 6, 76, 244);
            b.AddButton(913, L"&Browse...", 254, 75, 48);
        } else {
            b.AddLabel(L"Configure silent switches, reboot exit codes, or target MSI Feature:", 6, 4, 296, 14);
            b.AddLabel(L"&Silent CLI Switches:", 6, 24, 96);  b.AddEdit(921, Utf8ToWide(d.silentArgs),   106, 22, 196);
            b.AddLabel(L"S&uccess Exit Codes:", 6, 48, 96);  b.AddEdit(922, Utf8ToWide(d.successCodes), 106, 46, 120);
            b.AddLabel(L"&Reboot Exit Codes:",  6, 72, 96);  b.AddEdit(923, Utf8ToWide(d.rebootCodes),  106, 70, 120);
            b.AddLabel(L".MSM Owning &Feature:",6, 96, 96);  b.AddEdit(924, Utf8ToWide(d.msmFeature),   106, 94, 120);
            b.AddLabel(L"Any code in Reboot Exit Codes flags a pending restart and continues installing your app.", 6, 120, 296, 24);
        }
        return TRUE;
    }
    if (!ctx || !ctx->data) return FALSE;
    auto& d = *ctx->data;

    if (msg == WM_COMMAND) {
        const int id   = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == 901 && code == CBN_SELCHANGE) {
            const int sel = GetComboIndex(hDlg, 901);
            const char* defIds[] = {
                "DotNet11Desktop", "DotNet11AspNet", "DotNet11Runtime",
                "DotNet10Desktop", "DotNet10AspNet", "DotNet10Runtime",
                "DotNet9Desktop", "DotNet8Desktop", "EdgeWebView2", "VcRedist2022",
                "CustomRuntime", "SharedModule"
            };
            if (sel >= 0 && sel < 12) {
                ::SetDlgItemTextW(hDlg, 902, Utf8ToWide(defIds[sel]).c_str());
            }
            return TRUE;
        }
        if (id == 913) {
            std::wstring p;
            if (BrowsePathModal(hDlg, false, L"Runtimes & Merge Modules (*.exe;*.msi;*.msm)\0*.exe;*.msi;*.msm\0All Files (*.*)\0*.*\0", p)) {
                ::SetDlgItemTextW(hDlg, 912, p.c_str());
            }
            return TRUE;
        }
    }

    if (msg == WM_NOTIFY) {
        const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
        if (nm->code == PSN_SETACTIVE) {
            if (ctx->pageIndex == 1 && d.kindIdx >= 0 && static_cast<std::size_t>(d.kindIdx) < kCuratedRuntimePresets.size() && d.distIdx == 0) {
                const auto& pr = kCuratedRuntimePresets[static_cast<std::size_t>(d.kindIdx)];
                ::SetDlgItemTextW(hDlg, 912, Utf8ToWide(pr.urlArm64).c_str());
            }
            if (ctx->pageIndex == 2 && d.kindIdx >= 0 && static_cast<std::size_t>(d.kindIdx) < kCuratedRuntimePresets.size()) {
                const auto& pr = kCuratedRuntimePresets[static_cast<std::size_t>(d.kindIdx)];
                ::SetDlgItemTextW(hDlg, 921, Utf8ToWide(pr.silentArgs).c_str());
                ::SetDlgItemTextW(hDlg, 922, Utf8ToWide(pr.successCodes).c_str());
                ::SetDlgItemTextW(hDlg, 923, Utf8ToWide(pr.rebootCodes).c_str());
            }
            DWORD btns = PSWIZB_BACK | PSWIZB_NEXT;
            if (ctx->pageIndex == 0) btns = PSWIZB_NEXT;
            if (ctx->pageIndex == 2) btns = PSWIZB_BACK | PSWIZB_FINISH;
            PropSheet_SetWizButtons(::GetParent(hDlg), btns);
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        if (nm->code == PSN_WIZNEXT || nm->code == PSN_WIZFINISH) {
            if (ctx->pageIndex == 0) {
                d.kindIdx   = GetComboIndex(hDlg, 901);
                d.sectionId = ReadDlgItemUtf8(hDlg, 902);
                if (d.sectionId.empty()) d.sectionId = "Runtime1";
            } else if (ctx->pageIndex == 1) {
                d.distIdx     = GetComboIndex(hDlg, 911);
                d.urlOrSource = ReadDlgItemUtf8(hDlg, 912);
            } else {
                d.silentArgs   = ReadDlgItemUtf8(hDlg, 921);
                d.successCodes = ReadDlgItemUtf8(hDlg, 922);
                d.rebootCodes  = ReadDlgItemUtf8(hDlg, 923);
                d.msmFeature   = ReadDlgItemUtf8(hDlg, 924);
                d.completed    = true;
            }
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
    }
    return FALSE;
}

inline bool RunPrerequisiteMsmAeroWizard(HWND hOwner, RswManifestDocument& ioDoc) {
    PrereqMsmWizardData data{};
    const wchar_t* headers[3] = {
        L"Step 1 of 3 — Select Runtime Prerequisite (.NET 11 / 10) or .MSM",
        L"Step 2 of 3 — Delivery Mode (HTTPS Download, Embedded LZMS, or Redist\\)",
        L"Step 3 of 3 — Silent Switches & Reboot Exit Codes"
    };
    PrereqMsmPageCtx ctxs[3]{};
    LPARAM lParams[3]{};
    for (int i = 0; i < 3; ++i) {
        ctxs[i]    = {i, &data};
        lParams[i] = reinterpret_cast<LPARAM>(&ctxs[i]);
    }
    RunThemedAeroWizard(
        hOwner, L"Runtime Prerequisite & Merge Module (.MSM) Wizard",
        3, headers, &PrereqMsmAeroPageProc, lParams,
        ui::GlassGlyph::Bolt, ui::brand::kSkyCyan);

    if (!data.completed) return false;

    if (data.kindIdx == 11) {
        ioDoc.SetString("MergeModule", data.sectionId, "Source",     data.urlOrSource);
        ioDoc.SetString("MergeModule", data.sectionId, "Feature",    data.msmFeature.empty() ? "Main" : data.msmFeature);
        ioDoc.SetString("MergeModule", data.sectionId, "TargetRoot", "INSTALLDIR");
    } else {
        const char* presetNames[] = {
            "DotNet11Desktop", "DotNet11AspNet", "DotNet11Runtime",
            "DotNet10Desktop", "DotNet10AspNet", "DotNet10Runtime",
            "DotNet9Desktop", "DotNet8Desktop", "EdgeWebView2", "VcRedist2022", "Custom"
        };
        const char* distNames[] = {"download", "embedded", "side-by-side"};
        const std::string preset = presetNames[std::clamp(data.kindIdx, 0, 10)];
        const std::string dist   = distNames[std::clamp(data.distIdx, 0, 2)];

        ioDoc.SetString("Prerequisite", data.sectionId, "Preset",       preset);
        ioDoc.SetString("Prerequisite", data.sectionId, "Distribution", dist);
        if (dist == "embedded" || (dist == "side-by-side" && data.urlOrSource.rfind("http", 0) != 0)) {
            ioDoc.SetString("Prerequisite", data.sectionId, "SourceFile", data.urlOrSource);
        } else if (preset == "Custom" && !data.urlOrSource.empty()) {
            ioDoc.SetString("Prerequisite", data.sectionId, "Url", data.urlOrSource);
        }
        if (!data.silentArgs.empty())   ioDoc.SetString("Prerequisite", data.sectionId, "SilentArgs",       data.silentArgs);
        if (!data.successCodes.empty()) ioDoc.SetString("Prerequisite", data.sectionId, "SuccessExitCodes", data.successCodes);
        if (!data.rebootCodes.empty())  ioDoc.SetString("Prerequisite", data.sectionId, "RebootExitCodes",  data.rebootCodes);
    }
    return true;
}

// ================================================================================
// WIZARD 6: SELF-EXTRACTING `.EXE` (`PackageKind = sfx`) & SIMPLE NATIVE SETUP
//           (`Engine = native` + `WizardLayout = simple` — 3 GUIDED PAGES!)
// ================================================================================
struct SfxSimpleWizardData {
    // 0 = Portable Self-Extracting Archive .EXE (`sfx`)
    // 1 = Self-Extractor for Software Installation (`%TEMP%` -> Run Setup & Wait -> Auto-Clean!)
    // 2 = Simple Native Setup Project (`Engine = native` + `Uninstall.exe`)
    int         modeIdx             = 0;
    std::string productName         = "Portable App Pack";
    std::string version             = "1.0.0";
    std::string manufacturer        = "Sample Studio";
    std::string sourcePath          = "..\\..\\bin\\$(ARCH)\\HelloRaggieSoft.exe";

    int         execLevelIdx        = 0; // 0 = asInvoker, 1 = highestAvailable, 2 = requireAdministrator
    bool        perUserScope        = true;
    std::string defaultSubdir       = "Sample Studio\\Portable App Pack";
    bool        extractToTemp       = false;
    bool        allowChangeDir      = true;

    std::string postRunOrMainExe    = "HelloRaggieSoft.exe";
    std::string waitForProcess      = "";
    std::string postOpenDoc         = "";
    bool        cleanupTempAfterRun = false;
    bool        autoStartExtract    = false;
    bool        addShortcuts        = true;
    bool        autoValidate        = true;
    bool        completed           = false;
};

struct SfxSimplePageCtx {
    int                  pageIndex = 0;
    SfxSimpleWizardData* data      = nullptr;
};

inline INT_PTR CALLBACK SfxSimpleAeroPageProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<SfxSimplePageCtx*>(::GetWindowLongPtrW(hDlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        const auto* psp = reinterpret_cast<const PROPSHEETPAGEW*>(lParam);
        ctx = reinterpret_cast<SfxSimplePageCtx*>(psp->lParam);
        ::SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));
        AeroPageBuilder b(hDlg);
        auto& d = *ctx->data;

        if (ctx->pageIndex == 0) {
            b.AddLabel(L"Build an ultra-compact single-dialog Self-Extractor or RaggieScript EXE Setup:", 6, 4, 296, 14);
            b.AddLabel(L"Package &Mode:", 6, 22, 90);
            b.AddCombo(951, {
                L"Self-Extracting Archive .EXE (Portable Folder / .ZIP Unpacker)",
                L"Self-Extractor for Software Installation (%TEMP% -> Run Setup -> Auto-Clean)",
                L"RaggieScript EXE Setup Project (Engine = raggiescript — Instant Rollback + Uninstall.exe)"
            }, d.modeIdx, 98, 20, 204);
            b.AddLabel(L"&Product / Archive:", 6, 46, 90); b.AddEdit(952, Utf8ToWide(d.productName),  98, 44, 204);
            b.AddLabel(L"&Version (X.Y.Z):",   6, 68, 90); b.AddEdit(953, Utf8ToWide(d.version),      98, 66, 86);
            b.AddLabel(L"P&ublisher:",         6, 90, 90); b.AddEdit(954, Utf8ToWide(d.manufacturer), 98, 88, 204);
            b.AddLabel(L"Source &Folder, File, or .ZIP Archive:", 6, 114, 296);
            b.AddEdit(955, Utf8ToWide(d.sourcePath), 6, 126, 202);
            b.AddButton(956, L".&ZIP/File...", 212, 125, 44);
            b.AddButton(957, L"&Folder...",    258, 125, 44);
        } else if (ctx->pageIndex == 1) {
            b.AddLabel(L"Configure UAC execution level and destination / %TEMP% staging behavior:", 6, 4, 296, 14);
            b.AddLabel(L"&UAC Execution Level:", 6, 22, 296);
            b.AddCombo(961, {
                L"asInvoker — Standard user token (Elevate only if needed; zero UAC for SFX / per-user)",
                L"highestAvailable — Request elevation for Admin accounts, run standard for others",
                L"requireAdministrator — Always prompt for Administrator UAC at launch"
            }, d.execLevelIdx, 6, 34, 296);
            b.AddLabel(L"Install &Scope (for Simple Native Setup):", 6, 60, 296);
            b.AddCombo(962, {
                L"user — Current User (%LOCALAPPDATA%, Zero UAC Required)",
                L"machine — All Users (Program Files, Single UAC Prompt)"
            }, d.perUserScope ? 0 : 1, 6, 72, 296);
            b.AddLabel(L"&Default Destination Subfolder:", 6, 98, 296);
            b.AddEdit(963, Utf8ToWide(d.defaultSubdir), 6, 110, 296);
            b.AddCheck(964, L"Extract to isolated %&TEMP% staging folder (Software Installation SFX)", d.extractToTemp,  6, 128, 296);
            b.AddCheck(965, L"Allow user to &browse / edit destination folder in dialog",              d.allowChangeDir, 6, 146, 296);
        } else {
            b.AddLabel(L"Configure setup launch, companion process wait (e.g. _ISDEL.EXE), & %TEMP% cleanup:", 6, 2, 296, 12);
            b.AddLabel(L"&Executable / Setup to Run (PostExtractRun / MainExecutable):", 6, 16, 296);
            b.AddEdit(971, Utf8ToWide(d.postRunOrMainExe), 6, 27, 296);
            b.AddLabel(L"&Wait for Companion Process Before Cleanup (optional, e.g. _ISDEL.EXE; _INS*._MP):", 6, 44, 296);
            b.AddEdit(975, Utf8ToWide(d.waitForProcess), 6, 55, 296);
            b.AddLabel(L"Optional Bundled &Document to Open (PostExtractOpen, e.g. ReadMe.pdf):", 6, 72, 296);
            b.AddEdit(972, Utf8ToWide(d.postOpenDoc), 6, 83, 296);
            b.AddCheck(976, L"Auto-&clean %TEMP% folder after setup & companion processes finish", d.cleanupTempAfterRun, 6, 101, 296);
            b.AddCheck(977, L"&Auto-start extraction immediately on launch (zero extra click)",    d.autoStartExtract,    6, 117, 296);
            b.AddCheck(973, L"Create Start &Menu & Desktop shortcuts (Simple Native Setup mode)",  d.addShortcuts,        6, 133, 296);
            b.AddCheck(974, L"&Validate generated .rsip manifest immediately in IDE",              d.autoValidate,        6, 149, 296);
        }
        return TRUE;
    }

    if (!ctx || !ctx->data) return FALSE;
    auto& d = *ctx->data;

    if (msg == WM_COMMAND) {
        const int id = LOWORD(wParam);
        if (id == 956 || id == 957) {
            std::wstring chosen;
            if (BrowsePathModal(
                    hDlg,
                    id == 957,
                    L"ZIP Archives & Executables (*.zip;*.exe;*.*)\0*.zip;*.exe;*.*\0All Files (*.*)\0*.*\0",
                    chosen)) {
                ::SetDlgItemTextW(hDlg, 955, chosen.c_str());
                const std::filesystem::path p(chosen);
                if (id == 956 && EqualsIgnoreCase(WideToUtf8(p.extension().wstring()), ".exe")) {
                    d.postRunOrMainExe = WideToUtf8(p.filename().wstring());
                }
            }
            return TRUE;
        }
    }

    if (msg == WM_NOTIFY) {
        const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
        if (nm->code == PSN_SETACTIVE) {
            if (ctx->pageIndex == 1) {
                ::CheckDlgButton(hDlg, 964, d.extractToTemp ? BST_CHECKED : BST_UNCHECKED);
                ::CheckDlgButton(hDlg, 965, d.allowChangeDir ? BST_CHECKED : BST_UNCHECKED);
            } else if (ctx->pageIndex == 2) {
                ::CheckDlgButton(hDlg, 976, d.cleanupTempAfterRun ? BST_CHECKED : BST_UNCHECKED);
                ::CheckDlgButton(hDlg, 977, d.autoStartExtract ? BST_CHECKED : BST_UNCHECKED);
            }
            DWORD btns = PSWIZB_BACK | PSWIZB_NEXT;
            if (ctx->pageIndex == 0) btns = PSWIZB_NEXT;
            if (ctx->pageIndex == 2) btns = PSWIZB_BACK | PSWIZB_FINISH;
            PropSheet_SetWizButtons(::GetParent(hDlg), btns);
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        if (nm->code == PSN_WIZNEXT || nm->code == PSN_WIZFINISH) {
            if (ctx->pageIndex == 0) {
                const int prevMode = d.modeIdx;
                d.modeIdx      = GetComboIndex(hDlg, 951);
                d.productName  = ReadDlgItemUtf8(hDlg, 952);
                d.version      = ReadDlgItemUtf8(hDlg, 953);
                d.manufacturer = ReadDlgItemUtf8(hDlg, 954);
                d.sourcePath   = ReadDlgItemUtf8(hDlg, 955);
                if (!d.manufacturer.empty() && !d.productName.empty()) {
                    d.defaultSubdir = d.manufacturer + "\\" + d.productName;
                }
                if (d.modeIdx == 1 && prevMode != 1) {
                    // Preset sensible defaults for Software Installation `%TEMP%` Self-Extractor!
                    d.extractToTemp       = true;
                    d.allowChangeDir      = false;
                    d.cleanupTempAfterRun = true;
                    d.autoStartExtract    = true;
                }
            } else if (ctx->pageIndex == 1) {
                d.execLevelIdx   = GetComboIndex(hDlg, 961);
                d.perUserScope   = (GetComboIndex(hDlg, 962) == 0);
                d.defaultSubdir  = ReadDlgItemUtf8(hDlg, 963);
                d.extractToTemp  = IsDlgItemChecked(hDlg, 964);
                d.allowChangeDir = IsDlgItemChecked(hDlg, 965);
            } else {
                d.postRunOrMainExe    = ReadDlgItemUtf8(hDlg, 971);
                d.waitForProcess      = ReadDlgItemUtf8(hDlg, 975);
                d.postOpenDoc         = ReadDlgItemUtf8(hDlg, 972);
                d.cleanupTempAfterRun = IsDlgItemChecked(hDlg, 976);
                d.autoStartExtract    = IsDlgItemChecked(hDlg, 977);
                d.addShortcuts        = IsDlgItemChecked(hDlg, 973);
                d.autoValidate        = IsDlgItemChecked(hDlg, 974);
                d.completed           = true;
            }
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
    }
    return FALSE;
}

inline bool RunSfxAndSimpleSetupAeroWizard(
    HWND hOwner,
    std::string& outGeneratedRsipUtf8,
    bool& outAutoValidate,
    const std::filesystem::path* exportShotPath = nullptr)
{
    SfxSimpleWizardData data{};
    const wchar_t* headers[3] = {
        L"Step 1 of 3 — Self-Extractor or Simple Native Setup",
        L"Step 2 of 3 — UAC Execution Level & %TEMP% Staging",
        L"Step 3 of 3 — Setup Launch, Wait Process & Auto-Cleanup"
    };
    SfxSimplePageCtx ctxs[3]{};
    LPARAM lParams[3]{};
    for (int i = 0; i < 3; ++i) {
        ctxs[i]    = {i, &data};
        lParams[i] = reinterpret_cast<LPARAM>(&ctxs[i]);
    }
    RunThemedAeroWizard(
        hOwner, L"Self-Extracting .EXE & RaggieScript EXE Setup Wizard",
        3, headers, &SfxSimpleAeroPageProc, lParams,
        ui::GlassGlyph::Cube, ui::brand::kRaggieBlue, exportShotPath);
    if (!data.completed) return false;

    const bool isSfx = (data.modeIdx != 2);
    const char* execToks[] = {"asInvoker", "highestAvailable", "requireAdministrator"};
    const char* execLvl = execToks[std::clamp(data.execLevelIdx, 0, 2)];

    std::ostringstream s;
    s << "; ==============================================================================\r\n"
      << "; RaggieSoft " << (isSfx ? "Self-Extracting Archive (.EXE)" : "RaggieScript EXE Setup Project") << " (.rsip)\r\n"
      << "; Ultra-compact single-dialog UI (WizardLayout = simple) with LZMS compression!\r\n"
      << "; ==============================================================================\r\n\r\n"
      << "[Package]\r\n"
      << "Name                    = " << data.productName << "\r\n"
      << "Version                 = " << data.version << "\r\n"
      << "Manufacturer            = " << data.manufacturer << "\r\n";
    if (isSfx) {
        s << "PackageKind             = sfx\r\n";
    } else {
        s << "PackageKind             = msi\r\n"
          << "Engine                  = raggiescript\r\n"
          << "UpgradeCode             = " << GenerateRandomMsiGuid() << "\r\n"
          << "Scope                   = " << (data.perUserScope ? "user" : "machine") << "\r\n";
    }
    s << "Architecture            = $(ARCH)\r\n"
      << "RequestedExecutionLevel = " << execLvl << "\r\n\r\n";

    if (!isSfx) {
        s << "[Install]\r\n"
          << "DefaultSubdir           = " << data.defaultSubdir << "\r\n"
          << "AllowChangeFolder       = " << (data.allowChangeDir ? "true" : "false") << "\r\n";
        if (!data.postRunOrMainExe.empty()) {
            s << "MainExecutable          = " << data.postRunOrMainExe << "\r\n";
        }
        s << "\r\n";
    } else {
        const bool effTemp = data.extractToTemp || data.cleanupTempAfterRun;
        s << "[SFX]\r\n";
        if (!effTemp && !data.defaultSubdir.empty()) {
            s << "DefaultExtractSubdir    = " << data.defaultSubdir << "\r\n";
        }
        s << "ExtractToTemp           = " << (effTemp ? "true" : "false") << "\r\n"
          << "AllowChangeFolder       = " << (data.allowChangeDir ? "true" : "false") << "\r\n"
          << "CleanupTempAfterRun     = " << (data.cleanupTempAfterRun ? "true" : "false") << "\r\n"
          << "AutoStartExtract        = " << (data.autoStartExtract ? "true" : "false") << "\r\n";
        if (data.cleanupTempAfterRun || !data.waitForProcess.empty()) {
            s << "WaitMode                = " << (!data.waitForProcess.empty() ? "job_tree" : "auto") << "\r\n";
        }
        if (!data.waitForProcess.empty()) {
            s << "WaitForProcess          = " << data.waitForProcess << "\r\n";
        }
        if (!data.postRunOrMainExe.empty()) {
            s << "PostExtractRun          = " << data.postRunOrMainExe << "\r\n";
        }
        if (!data.postOpenDoc.empty()) {
            s << "PostExtractOpen         = " << data.postOpenDoc << "\r\n";
        }
        s << "\r\n";
    }

    s << "[UI]\r\n"
      << "WizardLayout            = simple\r\n"
      << "InstallButtonText       = " << (isSfx ? "&Extract" : "&Install") << "\r\n\r\n"
      << "[Payload.Archive]\r\n"
      << "Source                  = " << data.sourcePath << "\r\n";

    if (!isSfx && data.addShortcuts && !data.postRunOrMainExe.empty()) {
        s << "\r\n[Shortcut.StartMenuLink]\r\n"
          << "Name                    = " << data.productName << "\r\n"
          << "TargetFile              = " << data.postRunOrMainExe << "\r\n"
          << "Location                = StartMenu\r\n"
          << "\r\n[Shortcut.DesktopLink]\r\n"
          << "Name                    = " << data.productName << "\r\n"
          << "TargetFile              = " << data.postRunOrMainExe << "\r\n"
          << "Location                = Desktop\r\n";
    }

    outGeneratedRsipUtf8 = s.str();
    outAutoValidate      = data.autoValidate;
    return true;
}

// ================================================================================
// WIZARD 7: UNIFIED MULTI-PRODUCT SUITE PROJECT AEROWIZARD (`PackageKind = suite`)
//   Generates a Dual-Progress (`Overall` + `Component`) Suite Setup `.rsip`!
// ================================================================================
struct SuiteWizardData {
    std::string suiteName             = "Sample Studio Suite";
    std::string tagline               = "Unified Multi-Product Suite \xE2\x80\xA2 Single UAC & Clean Installed Apps";
    std::string version               = "1.0.0";
    std::string manufacturer          = "Sample Studio";
    bool        perMachine            = true;
    int         execLevelIdx          = 0; // 0 = asInvoker, 1 = highestAvailable, 2 = requireAdministrator
    int         deliveryIdx           = 0; // 0 = embedded, 1 = side-by-side (packages\), 2 = web (HTTPS Workload + SHA-256)
    int         rollbackIdx           = 0; // 0 = keep_completed, 1 = rollback_new (LIFO Rollback)
    bool        buildUniversal        = true;
    bool        showCompProgressBar   = true;
    std::string child1Name            = "Core Desktop IDE";
    std::string child1Source          = "..\\core-ide\\out\\core-ide-$(ARCH).msi";
    std::string child2Name            = "Companion Content Pack";
    std::string child2Source          = "..\\companion-pack\\out\\companion-pack-$(ARCH)-setup.exe";
    bool        autoValidate          = false; // Child paths may not be compiled yet on initial creation
    bool        completed             = false;
};

struct SuiteWizPageCtx {
    int              pageIndex = 0;
    SuiteWizardData* data      = nullptr;
};

inline INT_PTR CALLBACK SuiteProjectAeroPageProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<SuiteWizPageCtx*>(::GetWindowLongPtrW(hDlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        const auto* psp = reinterpret_cast<const PROPSHEETPAGEW*>(lParam);
        ctx = reinterpret_cast<SuiteWizPageCtx*>(psp->lParam);
        ::SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));
        AeroPageBuilder b(hDlg);
        auto& d = *ctx->data;

        if (ctx->pageIndex == 0) {
            b.AddLabel(L"Create a single-UAC Unified Suite Installer bundling .msi, RaggieScript EXE, & Add-On packs:", 6, 4, 296, 14);
            b.AddLabel(L"&Suite Title:",       6, 26, 90); b.AddEdit(981, Utf8ToWide(d.suiteName),    98, 24, 204);
            b.AddLabel(L"&Subtitle / Tagline:",6, 50, 90); b.AddEdit(982, Utf8ToWide(d.tagline),      98, 48, 204);
            b.AddLabel(L"&Version (X.Y.Z):",   6, 74, 90); b.AddEdit(983, Utf8ToWide(d.version),      98, 72, 90);
            b.AddLabel(L"&Publisher:",         6, 98, 90); b.AddEdit(984, Utf8ToWide(d.manufacturer), 98, 96, 204);
        } else if (ctx->pageIndex == 1) {
            b.AddLabel(L"Configure Suite Scope, Workload Delivery, Rollback Policy, & Dual Progress Bars:", 6, 4, 296, 12);
            b.AddLabel(L"Install &Scope:", 6, 18, 144);
            b.AddCombo(985, {
                L"machine — All Users (Program Files)",
                L"user — Current User (%LOCALAPPDATA%)"
            }, d.perMachine ? 0 : 1, 6, 29, 144);
            b.AddLabel(L"Outer &UAC Level:", 156, 18, 146);
            b.AddCombo(986, {
                L"asInvoker (Elevate on Install)",
                L"highestAvailable",
                L"requireAdministrator"
            }, d.execLevelIdx, 156, 29, 146);
            b.AddLabel(L"Package &Delivery:", 6, 52, 144);
            b.AddCombo(989, {
                L"embedded — Single Offline EXE",
                L"side-by-side — packages\\ Boxset",
                L"web — HTTPS Workload + SHA-256"
            }, d.deliveryIdx, 6, 63, 144);
            b.AddLabel(L"&Rollback Policy:", 156, 52, 146);
            b.AddCombo(990, {
                L"keep_completed (Fast Resume)",
                L"rollback_new (LIFO Undo)"
            }, d.rollbackIdx, 156, 63, 146);
            b.AddCheck(987, L"Build &Universal Combined Installer (x64 + ARM64 in one EXE)", d.buildUniversal, 6, 94, 296);
            b.AddCheck(988, L"Show &Component Progress Bar beneath monotonic Overall Suite Bar", d.showCompProgressBar, 6, 114, 296);
        } else {
            b.AddLabel(L"Add your first Suite member packages (.msi, RaggieScript EXE, or Add-On -setup.exe):", 6, 4, 296, 14);
            b.AddLabel(L"Member &1 Title:", 6, 24, 86);  b.AddEdit(991, Utf8ToWide(d.child1Name),   94, 22, 208);
            b.AddLabel(L"Member 1 &Path:",  6, 46, 86);  b.AddEdit(992, Utf8ToWide(d.child1Source), 94, 44, 156);
            b.AddButton(993, L"&Browse...", 254, 43, 48);
            b.AddLabel(L"Member &2 Title:", 6, 76, 86);  b.AddEdit(994, Utf8ToWide(d.child2Name),   94, 74, 208);
            b.AddLabel(L"Member 2 P&ath:",  6, 98, 86);  b.AddEdit(995, Utf8ToWide(d.child2Source), 94, 96, 156);
            b.AddButton(996, L"B&rowse...", 254, 95, 48);
            b.AddLabel(L"Tip: Standalone installs are dynamically adopted (SystemComponent=1) so Installed Apps stays clutter-free!", 6, 124, 296, 26);
        }
        return TRUE;
    }
    if (!ctx || !ctx->data) return FALSE;
    auto& d = *ctx->data;

    if (msg == WM_COMMAND) {
        const int id = LOWORD(wParam);
        if (id == 993 || id == 996) {
            std::wstring chosen;
            if (BrowsePathModal(hDlg, false, L"Suite Child Packages (*.msi;*.exe)\0*.msi;*.exe\0All Files (*.*)\0*.*\0", chosen)) {
                ::SetDlgItemTextW(hDlg, (id == 993) ? 992 : 995, chosen.c_str());
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
                d.suiteName    = ReadDlgItemUtf8(hDlg, 981);
                d.tagline      = ReadDlgItemUtf8(hDlg, 982);
                d.version      = ReadDlgItemUtf8(hDlg, 983);
                d.manufacturer = ReadDlgItemUtf8(hDlg, 984);
            } else if (ctx->pageIndex == 1) {
                d.perMachine          = (GetComboIndex(hDlg, 985) == 0);
                d.execLevelIdx        = GetComboIndex(hDlg, 986);
                d.deliveryIdx         = GetComboIndex(hDlg, 989);
                d.rollbackIdx         = GetComboIndex(hDlg, 990);
                d.buildUniversal      = IsDlgItemChecked(hDlg, 987);
                d.showCompProgressBar = IsDlgItemChecked(hDlg, 988);
            } else {
                d.child1Name   = ReadDlgItemUtf8(hDlg, 991);
                d.child1Source = ReadDlgItemUtf8(hDlg, 992);
                d.child2Name   = ReadDlgItemUtf8(hDlg, 994);
                d.child2Source = ReadDlgItemUtf8(hDlg, 995);
                d.completed    = true;
            }
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
    }
    return FALSE;
}

inline bool RunUnifiedSuiteAeroWizard(HWND hOwner, std::string& outGeneratedRsipUtf8) {
    SuiteWizardData data{};
    const wchar_t* headers[3] = {
        L"Step 1 of 3 — Unified Suite Identity & Publisher",
        L"Step 2 of 3 — Delivery Mode, Rollback Policy & Progress Bars",
        L"Step 3 of 3 — Initial Suite Member Applications"
    };
    SuiteWizPageCtx ctxs[3]{};
    LPARAM lParams[3]{};
    for (int i = 0; i < 3; ++i) {
        ctxs[i]    = {i, &data};
        lParams[i] = reinterpret_cast<LPARAM>(&ctxs[i]);
    }
    RunThemedAeroWizard(
        hOwner, L"Unified Multi-Product Suite Project Wizard",
        3, headers, &SuiteProjectAeroPageProc, lParams,
        ui::GlassGlyph::Blocks, ui::brand::kRaggieBlue);
    if (!data.completed) return false;

    const char* execToks[]  = {"asInvoker", "highestAvailable", "requireAdministrator"};
    const char* delivToks[] = {"embedded", "side-by-side", "web"};
    const char* rbToks[]    = {"keep_completed", "rollback_new"};
    const char* execLvl     = execToks[std::clamp(data.execLevelIdx, 0, 2)];
    const char* delivTok    = delivToks[std::clamp(data.deliveryIdx, 0, 2)];
    const char* rbTok       = rbToks[std::clamp(data.rollbackIdx, 0, 1)];

    std::ostringstream s;
    s << "; ==============================================================================\r\n"
      << "; RaggieSoft Unified Multi-Product Suite Project (PackageKind = suite)\r\n"
      << "; Features Byte-Weighted Monotonic Overall Progress (bound to Windows Taskbar),\r\n"
      << "; Secondary Per-Component Progress, & Dynamic ARPSYSTEMCOMPONENT Adoption!\r\n"
      << "; ==============================================================================\r\n\r\n"
      << "[Package]\r\n"
      << "Name                    = " << data.suiteName << "\r\n"
      << "Tagline                 = " << data.tagline << "\r\n"
      << "Version                 = " << data.version << "\r\n"
      << "Manufacturer            = " << data.manufacturer << "\r\n"
      << "PackageKind             = suite\r\n"
      << "SuiteRollbackPolicy     = " << rbTok << "\r\n"
      << "UpgradeCode             = " << GenerateRandomMsiGuid() << "\r\n"
      << "Architecture            = $(ARCH)\r\n"
      << "Scope                   = " << (data.perMachine ? "machine" : "user") << "\r\n"
      << "RequestedExecutionLevel = " << execLvl << "\r\n"
      << "BuildUniversal          = " << (data.buildUniversal ? "true" : "false") << "\r\n\r\n"
      << "[Install]\r\n"
      << "DefaultSubdir           = " << data.manufacturer << "\\" << data.suiteName << "\r\n\r\n"
      << "[UI]\r\n"
      << "InstallButtonText       = &Install Selected\r\n"
      << "ShowComponentProgressBar= " << (data.showCompProgressBar ? "true" : "false") << "\r\n\r\n";

    if (!data.child1Name.empty() && !data.child1Source.empty()) {
        const bool isMsi1 = EqualsIgnoreCase(WideToUtf8(std::filesystem::path(Utf8ToWide(data.child1Source)).extension().wstring()), ".msi");
        s << "[SuitePackage.CoreApp]\r\n"
          << "Name                    = " << data.child1Name << "\r\n"
          << "Delivery                = " << delivTok << "\r\n";
        if (std::string_view(delivTok) == "web") {
            s << "DownloadUrl             = " << data.child1Source << "\r\n"
              << "Sha256                  = 0000000000000000000000000000000000000000000000000000000000000000\r\n";
        } else {
            s << "Source                  = " << data.child1Source << "\r\n";
        }
        s << "Engine                  = " << (isMsi1 ? "msi" : "raggiescript") << "\r\n"
          << "UpgradeCode             = " << GenerateRandomMsiGuid() << "\r\n"
          << "Required                = true\r\n"
          << "RollbackBoundary        = true\r\n"
          << "AdoptStandalone         = true\r\n\r\n";
    }
    if (!data.child2Name.empty() && !data.child2Source.empty()) {
        const bool isMsi2 = EqualsIgnoreCase(WideToUtf8(std::filesystem::path(Utf8ToWide(data.child2Source)).extension().wstring()), ".msi");
        s << "[SuitePackage.CompanionTool]\r\n"
          << "Name                    = " << data.child2Name << "\r\n"
          << "Delivery                = " << delivTok << "\r\n";
        if (std::string_view(delivTok) == "web") {
            s << "DownloadUrl             = " << data.child2Source << "\r\n"
              << "Sha256                  = 0000000000000000000000000000000000000000000000000000000000000000\r\n";
        } else {
            s << "Source                  = " << data.child2Source << "\r\n";
        }
        s << "Engine                  = " << (isMsi2 ? "msi" : "raggiescript") << "\r\n"
          << "UpgradeCode             = " << GenerateRandomMsiGuid() << "\r\n"
          << "SelectedByDefault       = true\r\n"
          << "AdoptStandalone         = true\r\n";
    }

    outGeneratedRsipUtf8 = s.str();
    return true;
}

// ================================================================================
// WIZARD 8: HOST-AWARE ADD-ON / CONTENT PACK AEROWIZARD (`PackageKind = addon`)
//   First-class project type (`v0.9.0`) unlocked across ALL 4 Editions!
//   Locates an installed parent application (`[HostApp]`) and places books, albums,
//   level packs, themes, or plugins directly into the host's library folder!
// ================================================================================
struct AddOnWizardData {
    std::string addonName           = "Cedar Run Illustrated Novel";
    std::string tagline             = "Offline Add-On Content Pack";
    std::string version             = "1.0.0";
    std::string manufacturer        = "RaggieSoft Media";
    std::string sourceFolder        = "..\\content";
    int         presetIdx           = 1; // 0 = Custom Host, 1 = Book Reader, 2 = Listening Room, 3 = Host Plugins\ Dir
    std::string hostName            = "RaggieSoft Book Reader";
    std::string hostRegistryKey     = "Software\\RaggieSoft\\Book Reader";
    std::string hostFallbackSubdir  = "RaggieSoft\\Book Reader";
    std::string hostExe             = "BookReader.exe";
    int         targetRootIdx       = 0; // 0 = LocalAppData, 1 = HostInstallDir, 2 = CommonAppData, 3 = AppData
    std::string contentSubdir       = "RaggieSoft\\Books\\cedar-run";
    std::string hostDownloadUrl     = "https://books.raggiesoft.com";
    bool        requireHost         = true;
    bool        autoValidate        = false;
    bool        completed           = false;
};

struct AddOnWizPageCtx {
    int              pageIndex = 0;
    AddOnWizardData* data      = nullptr;
};

inline INT_PTR CALLBACK AddOnProjectAeroPageProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* ctx = reinterpret_cast<AddOnWizPageCtx*>(::GetWindowLongPtrW(hDlg, DWLP_USER));
    if (msg == WM_INITDIALOG) {
        const auto* psp = reinterpret_cast<const PROPSHEETPAGEW*>(lParam);
        ctx = reinterpret_cast<AddOnWizPageCtx*>(psp->lParam);
        ::SetWindowLongPtrW(hDlg, DWLP_USER, reinterpret_cast<LONG_PTR>(ctx));
        AeroPageBuilder b(hDlg);
        auto& d = *ctx->data;

        if (ctx->pageIndex == 0) {
            b.AddLabel(L"Create a Host-Aware Add-On / Content Pack Installer (PackageKind = addon):", 6, 4, 296, 14);
            b.AddLabel(L"&Add-On Title:",     6, 24, 90); b.AddEdit(961, Utf8ToWide(d.addonName),    98, 22, 204);
            b.AddLabel(L"&Subtitle / Note:",  6, 48, 90); b.AddEdit(962, Utf8ToWide(d.tagline),      98, 46, 204);
            b.AddLabel(L"&Version (X.Y.Z):",  6, 72, 90); b.AddEdit(963, Utf8ToWide(d.version),      98, 70, 90);
            b.AddLabel(L"&Publisher:",        6, 96, 90); b.AddEdit(964, Utf8ToWide(d.manufacturer), 98, 94, 204);
            b.AddLabel(L"&Content Folder:",   6, 120, 90);b.AddEdit(965, Utf8ToWide(d.sourceFolder), 98, 118, 152);
            b.AddButton(966, L"&Browse...", 254, 117, 48);
        } else if (ctx->pageIndex == 1) {
            b.AddLabel(L"Select or configure the parent Host Application this Add-On installs into:", 6, 4, 296, 14);
            b.AddLabel(L"Host &Preset:", 6, 22, 86);
            b.AddCombo(967, {
                L"Custom Host Application (Registry / EXE)",
                L"RaggieSoft Book Reader — Book Pack",
                L"RaggieSoft The Listening Room — Album Pack",
                L"Host Application Plugins\\ Subfolder"
            }, d.presetIdx, 94, 20, 208);
            b.AddLabel(L"&Host App Name:",    6, 48, 86); b.AddEdit(968, Utf8ToWide(d.hostName),           94, 46, 208);
            b.AddLabel(L"Host &Reg Key:",     6, 72, 86); b.AddEdit(969, Utf8ToWide(d.hostRegistryKey),    94, 70, 208);
            b.AddLabel(L"&Fallback Subdir:",  6, 96, 86); b.AddEdit(970, Utf8ToWide(d.hostFallbackSubdir), 94, 94, 208);
            b.AddLabel(L"Host &Executable:",  6, 120, 86);b.AddEdit(971, Utf8ToWide(d.hostExe),            94, 118, 208);
        } else {
            b.AddLabel(L"Choose where the content files are placed once the Host Application is verified:", 6, 4, 296, 14);
            b.AddLabel(L"Target &Root:", 6, 22, 296);
            b.AddCombo(972, {
                L"LocalAppData — Per-User %LOCALAPPDATA% (Zero UAC Prompt!)",
                L"HostInstallDir — Inside Detected Host Folder (<HostDir>\\<Subdir>)",
                L"CommonAppData — Shared All-Users %ProgramData%",
                L"AppData — Roaming %APPDATA%"
            }, d.targetRootIdx, 6, 34, 296);
            b.AddLabel(L"Content &Subdir:", 6, 62, 88);   b.AddEdit(973, Utf8ToWide(d.contentSubdir),   96, 60, 206);
            b.AddLabel(L"&Get Host URL:",   6, 86, 88);   b.AddEdit(974, Utf8ToWide(d.hostDownloadUrl), 96, 84, 206);
            b.AddCheck(975, L"&Require Host Installed (blocks orphaned files & exits 1605 in /s)", d.requireHost, 6, 112, 296);
            b.AddCheck(976, L"&Validate manifest immediately if Content Folder exists on disk", d.autoValidate, 6, 132, 296);
        }
        return TRUE;
    }
    if (!ctx || !ctx->data) return FALSE;
    auto& d = *ctx->data;

    if (msg == WM_COMMAND) {
        const int id   = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == 966) {
            std::wstring chosen;
            if (BrowsePathModal(hDlg, true, L"All Files (*.*)\0*.*\0", chosen)) {
                ::SetDlgItemTextW(hDlg, 965, chosen.c_str());
            }
            return TRUE;
        }
        if (id == 967 && code == CBN_SELCHANGE) {
            const int sel = GetComboIndex(hDlg, 967);
            if (sel == 1) {
                ::SetDlgItemTextW(hDlg, 968, L"RaggieSoft Book Reader");
                ::SetDlgItemTextW(hDlg, 969, L"Software\\RaggieSoft\\Book Reader");
                ::SetDlgItemTextW(hDlg, 970, L"RaggieSoft\\Book Reader");
                ::SetDlgItemTextW(hDlg, 971, L"BookReader.exe");
                d.targetRootIdx   = 0;
                d.contentSubdir   = "RaggieSoft\\Books\\" + SlugifyName(d.addonName);
                d.hostDownloadUrl = "https://books.raggiesoft.com";
            } else if (sel == 2) {
                ::SetDlgItemTextW(hDlg, 968, L"RaggieSoft The Listening Room");
                ::SetDlgItemTextW(hDlg, 969, L"Software\\RaggieSoft\\The Listening Room");
                ::SetDlgItemTextW(hDlg, 970, L"RaggieSoft\\The Listening Room");
                ::SetDlgItemTextW(hDlg, 971, L"ListeningRoom.exe");
                d.targetRootIdx   = 0;
                d.contentSubdir   = "RaggieSoft\\Music\\" + SlugifyName(d.addonName);
                d.hostDownloadUrl = "https://music.raggiesoft.com";
            } else if (sel == 3) {
                ::SetDlgItemTextW(hDlg, 968, L"My Studio Host Application");
                ::SetDlgItemTextW(hDlg, 969, L"Software\\Sample Studio\\My Host App");
                ::SetDlgItemTextW(hDlg, 970, L"Sample Studio\\My Host App");
                ::SetDlgItemTextW(hDlg, 971, L"MyHostApp.exe");
                d.targetRootIdx   = 1; // `HostInstallDir`
                d.contentSubdir   = "Plugins\\" + SlugifyName(d.addonName);
                d.hostDownloadUrl = "";
            }
            return TRUE;
        }
    }
    if (msg == WM_NOTIFY) {
        const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
        if (nm->code == PSN_SETACTIVE) {
            if (ctx->pageIndex == 2) {
                ::SendDlgItemMessageW(hDlg, 972, CB_SETCURSEL, d.targetRootIdx, 0);
                ::SetDlgItemTextW(hDlg, 973, Utf8ToWide(d.contentSubdir).c_str());
                ::SetDlgItemTextW(hDlg, 974, Utf8ToWide(d.hostDownloadUrl).c_str());
            }
            DWORD btns = PSWIZB_BACK | PSWIZB_NEXT;
            if (ctx->pageIndex == 0) btns = PSWIZB_NEXT;
            if (ctx->pageIndex == 2) btns = PSWIZB_BACK | PSWIZB_FINISH;
            PropSheet_SetWizButtons(::GetParent(hDlg), btns);
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
        if (nm->code == PSN_WIZNEXT || nm->code == PSN_WIZFINISH) {
            if (ctx->pageIndex == 0) {
                d.addonName    = ReadDlgItemUtf8(hDlg, 961);
                d.tagline      = ReadDlgItemUtf8(hDlg, 962);
                d.version      = ReadDlgItemUtf8(hDlg, 963);
                d.manufacturer = ReadDlgItemUtf8(hDlg, 964);
                d.sourceFolder = ReadDlgItemUtf8(hDlg, 965);
            } else if (ctx->pageIndex == 1) {
                d.presetIdx          = GetComboIndex(hDlg, 967);
                d.hostName           = ReadDlgItemUtf8(hDlg, 968);
                d.hostRegistryKey    = ReadDlgItemUtf8(hDlg, 969);
                d.hostFallbackSubdir = ReadDlgItemUtf8(hDlg, 970);
                d.hostExe            = ReadDlgItemUtf8(hDlg, 971);
            } else {
                d.targetRootIdx   = GetComboIndex(hDlg, 972);
                d.contentSubdir   = ReadDlgItemUtf8(hDlg, 973);
                d.hostDownloadUrl = ReadDlgItemUtf8(hDlg, 974);
                d.requireHost     = IsDlgItemChecked(hDlg, 975);
                d.autoValidate    = IsDlgItemChecked(hDlg, 976);
                d.completed       = true;
            }
            ::SetWindowLongPtrW(hDlg, DWLP_MSGRESULT, 0);
            return TRUE;
        }
    }
    return FALSE;
}

inline bool RunAddOnInstallerAeroWizard(HWND hOwner, std::string& outGeneratedRsipUtf8, bool& outAutoValidate) {
    AddOnWizardData data{};
    const wchar_t* headers[3] = {
        L"Step 1 of 3 — Add-On / Content Pack Identity & Files",
        L"Step 2 of 3 — Parent Host Application Discovery ([HostApp])",
        L"Step 3 of 3 — Content Placement & Missing-Host Safeguards"
    };
    AddOnWizPageCtx ctxs[3]{};
    LPARAM lParams[3]{};
    for (int i = 0; i < 3; ++i) {
        ctxs[i]    = {i, &data};
        lParams[i] = reinterpret_cast<LPARAM>(&ctxs[i]);
    }
    RunThemedAeroWizard(
        hOwner, L"Add-On / Content Pack Project Wizard",
        3, headers, &AddOnProjectAeroPageProc, lParams,
        ui::GlassGlyph::Folder, ui::brand::kEmeraldOk);
    if (!data.completed) return false;

    const char* rootToks[] = {"LocalAppData", "HostInstallDir", "CommonAppData", "AppData"};
    const char* targetRoot = rootToks[std::clamp(data.targetRootIdx, 0, 3)];
    const bool  perMachine = (data.targetRootIdx == 1 || data.targetRootIdx == 2);
    const std::string slug = SlugifyName(data.addonName);

    std::ostringstream s;
    s << "; ==============================================================================\r\n"
      << "; RaggieSoft Host-Aware Add-On / Content Pack Project (PackageKind = addon)\r\n"
      << "; Unlocked across ALL 4 Editions (Community, Essentials, Developer, Architect)!\r\n"
      << "; Locates '" << data.hostName << "' via [HostApp] and places content directly into\r\n"
      << "; its library directory (exiting cleanly with 1605 if the host is missing!).\r\n"
      << "; ==============================================================================\r\n\r\n"
      << "[Package]\r\n"
      << "Name                  = " << data.addonName << "\r\n"
      << "Tagline               = " << data.tagline << "\r\n"
      << "Version               = " << data.version << "\r\n"
      << "Manufacturer          = " << data.manufacturer << "\r\n"
      << "PackageKind           = addon\r\n"
      << "UpgradeCode           = " << GenerateRandomMsiGuid() << "\r\n"
      << "Architecture          = $(ARCH)\r\n"
      << "Scope                 = " << (perMachine ? "machine" : "user") << "\r\n"
      << "HideFromInstalledApps = true\r\n\r\n"
      << "[HostApp]\r\n"
      << "Name                  = " << data.hostName << "\r\n";
    if (!data.hostRegistryKey.empty()) {
        s << "RegistryKey           = " << data.hostRegistryKey << "\r\n";
    }
    if (!data.hostFallbackSubdir.empty()) {
        s << "FallbackSubdir        = " << data.hostFallbackSubdir << "\r\n";
    }
    if (!data.hostExe.empty()) {
        s << "Executable            = " << data.hostExe << "\r\n";
    }
    s << "ContentTargetRoot     = " << targetRoot << "\r\n"
      << "ContentSubdir         = " << data.contentSubdir << "\r\n"
      << "RequireHostInstalled  = " << (data.requireHost ? "true" : "false") << "\r\n";
    if (!data.hostDownloadUrl.empty()) {
        s << "DownloadUrl           = " << data.hostDownloadUrl << "\r\n";
    }
    s << "\r\n[UI]\r\n"
      << "WizardLayout          = simple\r\n"
      << "AllowChangeFolder     = false\r\n"
      << "InstallButtonText     = &Install Add-On\r\n"
      << "FinishLaunchCheckbox  = &Open " << data.hostName << " when finished\r\n\r\n"
      << "[Payload.Content]\r\n"
      << "Source                = " << data.sourceFolder << "\r\n";

    if (data.presetIdx == 1) {
        s << "\r\n[Registry.BookPackList]\r\n"
          << "Root                  = HKCU\r\n"
          << "Key                   = Software\\RaggieSoft\\Book Reader\\Books\\List\r\n"
          << "Name                  = " << slug << "\r\n"
          << "Value                 = [INSTALLDIR]\r\n";
    } else if (data.presetIdx == 2) {
        s << "\r\n[Registry.AlbumPackList]\r\n"
          << "Root                  = HKCU\r\n"
          << "Key                   = Software\\RaggieSoft\\The Listening Room\\Albums\\List\r\n"
          << "Name                  = " << slug << "\r\n"
          << "Value                 = [INSTALLDIR]\r\n";
    }

    outAutoValidate      = data.autoValidate;
    outGeneratedRsipUtf8 = s.str();
    return true;
}

} // namespace rsw::ide_wizards
