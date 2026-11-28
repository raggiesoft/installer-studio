// ================================================================================
// FILE: src/common/RswRsDialogs.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `RswRsDialogs.hpp`?
//     Sometimes an installer author wants a full blank-canvas WYSIWYG Dialog
//     Designer (`[Dialog.<id>]` / `[MsiDialog.<id>]`), and other times they want
//     to wire up a complete, accessible Windows 11 wizard in 10 lines of
//     `RaggieScript` using one-liner turnkey dialog functions!
//
//     This header implements the **`RsDialog` Standard Wizard Function Suite**
//     (`RsWelcome`, `RsLicense`, `RsReadme`, `RsUserInfo`, `RsSelectDestDir`,
//     `RsSetupType`, `RsFeatureTree`, `RsAskOptions`, `RsAskText`,
//     `RsReadyToInstall`, `RsFinish`, and `RsFinishReboot`):
//       - Every `Rs*` call renders a genuine Per-Monitor V2 DPI Win32 wizard
//         dialog (`560 x 400` logical px) honoring OS Dark Mode, Light Mode,
//         Windows Contrast Themes (`SPI_GETHIGHCONTRAST`), full `Tab` / `Alt+&`
//         keyboard navigation, and screen-reader UI Automation roles.
//       - Every `Rs*` call returns a canonical integer navigation code:
//           * `DialogResult.Next`   (`1`)  — User clicked `&Next >` / `&Install` / `&Finish`
//           * `DialogResult.Back`   (`12`) — User clicked `< &Back`
//           * `DialogResult.Cancel` (`2`)  — User canceled setup
//       - In `/s` silent mode (or when driven headlessly by `insttests.exe` via
//         `RsDialogMockAction`), `Rs*` never blocks on modal UI—applying any
//         scripted inputs to `ref` out-parameters and returning in `< 0.1ms`!
// ================================================================================

#pragma once

#include "RswGlassRenderer.hpp"
#include "RswPlatform.hpp"
#include "RswWin32UiTheme.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <richedit.h>
#include <shobjidl.h>
#include <uxtheme.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rsw::rssc::rsdlg {

// Canonical return values shared across all `Rs*` turnkey wizard functions!
inline constexpr int kResultNext   = 1;
inline constexpr int kResultCancel = 2;
inline constexpr int kResultBack   = 12;

enum class RsDialogKind {
    Welcome = 0,
    License,
    Readme,
    UserInfo,
    SelectDestDir,
    SetupType,
    FeatureTree,
    AskOptions,
    AskText,
    ReadyToInstall,
    Finish,
    FinishReboot
};

/// Deterministic scripted step used by automated tests (`insttests.exe`) to exercise
/// multi-page `Rs*` wizard sequences (`Next`, `< Back`, `ref` mutations, & `Cancel`) headlessly!
struct RsDialogMockStep {
    int                        returnCode = kResultNext; // `1` = Next, `12` = Back, `2` = Cancel
    std::string                strA;                     // Primary string override (`installDir`, `userName`, `setupType`, `text`)
    std::string                strB;                     // Secondary string override (`companyName`)
    std::string                strC;                     // Tertiary string override (`serialKey`)
    bool                       boolA      = true;        // Primary bool override (`accepted`, `launchApp`, `rebootNow`)
    bool                       boolB      = false;       // Secondary bool override (`viewReadme`)
    std::vector<bool>          optionChecks;             // Multi-option bool overrides for `RsAskOptions` / `RsFeatureTree`
};

struct RsFeatureItem {
    std::string id;
    std::string title;
    std::string description;
    bool        required = false;
    bool        selected = true;
};

struct RsDialogSpec {
    RsDialogKind               kind             = RsDialogKind::Welcome;
    std::string                windowTitle      = "Application Setup";
    std::string                headerTitle      = "Welcome";
    std::string                headerSubtitle;
    std::string                bodyText;
    std::string                fieldLabelA      = "&Value:";
    std::string                fieldLabelB      = "&Organization:";
    std::string                fieldLabelC      = "&Serial Number:";
    std::string                valA;
    std::string                valB;
    std::string                valC;
    bool                       boolA            = false;
    bool                       boolB            = false;
    bool                       requireSerial    = false;
    bool                       exclusiveOptions = false;
    bool                       passwordInput    = false;
    bool                       showBackButton   = true;
    bool                       showUacShield    = false;
    std::int64_t               minFreeMB        = 0;
    std::string                primaryBtnText   = "&Next >";
    std::string                checkLabelA      = "I &accept the terms in the License Agreement";
    std::string                checkLabelB      = "View &ReadMe";
    std::vector<std::string>   optionLabels;
    std::vector<std::string>   optionValues;
    std::vector<bool>          optionStates;
    std::vector<RsFeatureItem> features;
};

namespace detail {

enum CtrlIds : int {
    IDC_RSD_BACK     = 3101,
    IDC_RSD_NEXT     = 3102,
    IDC_RSD_CANCEL   = 3103,
    IDC_RSD_BROWSE   = 3104,
    IDC_RSD_EDIT_A   = 3110,
    IDC_RSD_EDIT_B   = 3111,
    IDC_RSD_EDIT_C   = 3112,
    IDC_RSD_CHK_A    = 3120,
    IDC_RSD_CHK_B    = 3121,
    IDC_RSD_DYN_BASE = 3200
};

struct ModalSession {
    RsDialogSpec*     spec      = nullptr;
    rsw::ui::RswTheme theme{};
    HFONT             hFontHead = nullptr;
    HFONT             hFontBody = nullptr;
    HWND              hEditA    = nullptr;
    HWND              hEditB    = nullptr;
    HWND              hEditC    = nullptr;
    HWND              hChkA     = nullptr;
    HWND              hChkB     = nullptr;
    HWND              hNextBtn  = nullptr;
    HWND              hDiskLbl  = nullptr;
    std::vector<HWND> dynChecks;
    int               result    = kResultCancel;
    bool              done      = false;

    ~ModalSession() {
        if (hFontHead) ::DeleteObject(hFontHead);
        if (hFontBody) ::DeleteObject(hFontBody);
    }
};

inline std::string ReadHwndUtf8(HWND h) {
    if (!h) return {};
    const int len = ::GetWindowTextLengthW(h);
    if (len <= 0) return {};
    std::wstring w(static_cast<std::size_t>(len) + 2, L'\0');
    const int n = ::GetWindowTextW(h, w.data(), len + 2);
    w.resize(static_cast<std::size_t>(std::max(0, n)));
    return WideToUtf8(w);
}

inline std::int64_t QueryFreeDiskMegabytes(const std::string& rawDir) {
    std::error_code ec;
    std::filesystem::path p = rawDir.empty()
        ? std::filesystem::current_path(ec)
        : std::filesystem::path(Utf8ToWide(rawDir));
    while (!p.empty() && !std::filesystem::exists(p, ec)) {
        const auto par = p.parent_path();
        if (par == p) break;
        p = par;
    }
    ULARGE_INTEGER avail{};
    if (!p.empty() && ::GetDiskFreeSpaceExW(p.wstring().c_str(), &avail, nullptr, nullptr)) {
        return static_cast<std::int64_t>(avail.QuadPart / (1024ull * 1024ull));
    }
    return 10240;
}

inline void RefreshDiskSpaceLabel(ModalSession* st) {
    if (!st || !st->hDiskLbl || !st->hEditA) return;
    const std::int64_t freeMb = QueryFreeDiskMegabytes(ReadHwndUtf8(st->hEditA));
    std::wostringstream oss;
    oss << L"Available free disk space: " << freeMb << L" MB";
    if (st->spec->minFreeMB > 0) {
        oss << L" (Minimum required: " << st->spec->minFreeMB << L" MB)";
    }
    ::SetWindowTextW(st->hDiskLbl, oss.str().c_str());
}

inline bool ValidateAndHarvestControls(HWND hWnd, ModalSession* st) {
    auto* sp = st->spec;
    switch (sp->kind) {
        case RsDialogKind::License:
            sp->boolA = (st->hChkA && ::SendMessageW(st->hChkA, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (!sp->boolA) return false;
            break;
        case RsDialogKind::UserInfo:
            sp->valA = TrimAscii(ReadHwndUtf8(st->hEditA));
            sp->valB = TrimAscii(ReadHwndUtf8(st->hEditB));
            sp->valC = TrimAscii(ReadHwndUtf8(st->hEditC));
            if (sp->valA.empty()) {
                ::TaskDialog(hWnd, nullptr, L"Information Required", L"Please enter your name",
                             L"Enter a user name before continuing.", TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
                ::SetFocus(st->hEditA);
                return false;
            }
            if (sp->requireSerial && sp->valC.empty()) {
                ::TaskDialog(hWnd, nullptr, L"Serial Key Required", L"Please enter your serial number",
                             L"A valid serial or registration key is required to continue.", TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
                ::SetFocus(st->hEditC);
                return false;
            }
            break;
        case RsDialogKind::SelectDestDir: {
            sp->valA = TrimAscii(ReadHwndUtf8(st->hEditA));
            if (sp->valA.empty()) {
                ::SetFocus(st->hEditA);
                return false;
            }
            const std::int64_t freeMb = QueryFreeDiskMegabytes(sp->valA);
            if (sp->minFreeMB > 0 && freeMb < sp->minFreeMB) {
                const std::wstring msg = L"The selected drive has " + std::to_wstring(freeMb) +
                                         L" MB free, which is less than the required " + std::to_wstring(sp->minFreeMB) + L" MB.";
                ::TaskDialog(hWnd, nullptr, L"Insufficient Disk Space", L"Not enough free disk space",
                             msg.c_str(), TDCBF_OK_BUTTON, TD_WARNING_ICON, nullptr);
                return false;
            }
            break;
        }
        case RsDialogKind::SetupType:
            for (std::size_t i = 0; i < st->dynChecks.size() && i < sp->optionValues.size(); ++i) {
                if (::SendMessageW(st->dynChecks[i], BM_GETCHECK, 0, 0) == BST_CHECKED) {
                    sp->valA = sp->optionValues[i];
                }
            }
            break;
        case RsDialogKind::FeatureTree:
            for (std::size_t i = 0; i < st->dynChecks.size() && i < sp->features.size(); ++i) {
                if (!sp->features[i].required) {
                    sp->features[i].selected = (::SendMessageW(st->dynChecks[i], BM_GETCHECK, 0, 0) == BST_CHECKED);
                }
            }
            break;
        case RsDialogKind::AskOptions:
            sp->optionStates.resize(st->dynChecks.size(), false);
            for (std::size_t i = 0; i < st->dynChecks.size(); ++i) {
                sp->optionStates[i] = (::SendMessageW(st->dynChecks[i], BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            break;
        case RsDialogKind::AskText:
            sp->valA = ReadHwndUtf8(st->hEditA);
            break;
        case RsDialogKind::Finish:
            if (st->hChkA) sp->boolA = (::SendMessageW(st->hChkA, BM_GETCHECK, 0, 0) == BST_CHECKED);
            if (st->hChkB) sp->boolB = (::SendMessageW(st->hChkB, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        case RsDialogKind::FinishReboot:
            if (!st->dynChecks.empty()) {
                sp->boolA = (::SendMessageW(st->dynChecks[0], BM_GETCHECK, 0, 0) == BST_CHECKED);
            }
            break;
        default:
            break;
    }
    return true;
}

inline LRESULT CALLBACK RsDialogWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    auto* st = reinterpret_cast<ModalSession*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    switch (msg) {
        case WM_NCCREATE: {
            auto* cs = reinterpret_cast<const CREATESTRUCTW*>(lParam);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
            return TRUE;
        }
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN: {
            if (!st) break;
            HDC hdc = reinterpret_cast<HDC>(wParam);
            ::SetTextColor(hdc, st->theme.textPrimary);
            ::SetBkColor(hdc, st->theme.windowBg);
            return reinterpret_cast<LRESULT>(st->theme.hbrWindow);
        }
        case WM_CTLCOLOREDIT: {
            if (!st) break;
            HDC hdc = reinterpret_cast<HDC>(wParam);
            ::SetTextColor(hdc, st->theme.textPrimary);
            ::SetBkColor(hdc, st->theme.cardBg);
            return reinterpret_cast<LRESULT>(st->theme.hbrCard);
        }
        case WM_PAINT: {
            PAINTSTRUCT ps{};
            HDC hdc = ::BeginPaint(hWnd, &ps);
            if (st && st->spec) {
                RECT rc{};
                ::GetClientRect(hWnd, &rc);
                const UINT dpi = ::GetDpiForWindow(hWnd);
                auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, st->theme.textScalePercent); };
                ::FillRect(hdc, &rc, st->theme.hbrWindow);

                RECT hdrRc{0, 0, rc.right, S(62)};
                rsw::ui::FillHorizontalGradient(hdc, hdrRc, st->theme.bannerNavyLeft, st->theme.bannerNavyRight);
                ::SetBkMode(hdc, TRANSPARENT);
                HGDIOBJ oldF = ::SelectObject(hdc, st->hFontHead);
                ::SetTextColor(hdc, st->theme.bannerTitleText);
                RECT tRc{S(20), S(10), rc.right - S(20), S(34)};
                const std::wstring wHead = Utf8ToWide(st->spec->headerTitle);
                ::DrawTextW(hdc, wHead.c_str(), -1, &tRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                ::SelectObject(hdc, st->hFontBody);
                ::SetTextColor(hdc, st->theme.bannerSubText);
                RECT sRc{S(20), S(35), rc.right - S(20), S(56)};
                const std::wstring wSub = Utf8ToWide(st->spec->headerSubtitle);
                ::DrawTextW(hdc, wSub.c_str(), -1, &sRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                HPEN sepPen = ::CreatePen(PS_SOLID, 1, st->theme.borderSubtle);
                HGDIOBJ oldP = ::SelectObject(hdc, sepPen);
                const int footTop = rc.bottom - S(50);
                ::MoveToEx(hdc, 0, footTop, nullptr);
                ::LineTo(hdc, rc.right, footTop);
                ::SelectObject(hdc, oldP);
                ::DeleteObject(sepPen);
                ::SelectObject(hdc, oldF);
            }
            ::EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_COMMAND: {
            if (!st || !st->spec) break;
            const int id   = LOWORD(wParam);
            const int code = HIWORD(wParam);
            if (id == IDC_RSD_CHK_A && st->spec->kind == RsDialogKind::License && st->hNextBtn) {
                const bool checked = (::SendMessageW(st->hChkA, BM_GETCHECK, 0, 0) == BST_CHECKED);
                ::EnableWindow(st->hNextBtn, checked ? TRUE : FALSE);
                return 0;
            }
            if (id == IDC_RSD_EDIT_A && code == EN_CHANGE && st->spec->kind == RsDialogKind::SelectDestDir) {
                RefreshDiskSpaceLabel(st);
                return 0;
            }
            if (id == IDC_RSD_BROWSE) {
                IFileOpenDialog* pDlg = nullptr;
                if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))) && pDlg) {
                    DWORD fos = 0;
                    pDlg->GetOptions(&fos);
                    pDlg->SetOptions(fos | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
                    if (SUCCEEDED(pDlg->Show(hWnd))) {
                        IShellItem* pItem = nullptr;
                        if (SUCCEEDED(pDlg->GetResult(&pItem)) && pItem) {
                            PWSTR psz = nullptr;
                            if (SUCCEEDED(pItem->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                                ::SetWindowTextW(st->hEditA, psz);
                                ::CoTaskMemFree(psz);
                                RefreshDiskSpaceLabel(st);
                            }
                            pItem->Release();
                        }
                    }
                    pDlg->Release();
                }
                return 0;
            }
            if (id == IDC_RSD_NEXT || id == IDOK) {
                if (st->hNextBtn && !::IsWindowEnabled(st->hNextBtn)) return 0;
                if (ValidateAndHarvestControls(hWnd, st)) {
                    st->result = kResultNext;
                    st->done   = true;
                }
                return 0;
            }
            if (id == IDC_RSD_BACK) {
                st->result = kResultBack;
                st->done   = true;
                return 0;
            }
            if (id == IDC_RSD_CANCEL || id == IDCANCEL) {
                const bool isFinishStep = st->spec &&
                    (st->spec->kind == RsDialogKind::Finish || st->spec->kind == RsDialogKind::FinishReboot);
                if (!isFinishStep) {
                    const std::wstring prodTitle = (st->spec && !st->spec->windowTitle.empty())
                        ? rsw::Utf8ToWide(st->spec->windowTitle)
                        : L"Application";
                    if (!rsw::ui::ConfirmSetupCancelDialog(hWnd, prodTitle, false)) {
                        return 0;
                    }
                }
                st->result = kResultCancel;
                st->done   = true;
                return 0;
            }
            break;
        }
        case WM_CLOSE:
            if (st) {
                const bool isFinishStep = st->spec &&
                    (st->spec->kind == RsDialogKind::Finish || st->spec->kind == RsDialogKind::FinishReboot);
                if (!isFinishStep) {
                    const std::wstring prodTitle = (st->spec && !st->spec->windowTitle.empty())
                        ? rsw::Utf8ToWide(st->spec->windowTitle)
                        : L"Application";
                    if (!rsw::ui::ConfirmSetupCancelDialog(hWnd, prodTitle, false)) {
                        return 0;
                    }
                }
                st->result = kResultCancel;
                st->done   = true;
            }
            return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

} // namespace detail

/// Displays one interactive `Rs*` wizard dialog (or resolves immediately using scripted mock steps in CI / `/s` silent mode).
inline int ExecuteRsDialog(
    RsDialogSpec& ioSpec,
    bool silentMode,
    HWND parentHwnd,
    std::vector<RsDialogMockStep>& ioMockQueue)
{
    if (!ioMockQueue.empty()) {
        const RsDialogMockStep step = ioMockQueue.front();
        ioMockQueue.erase(ioMockQueue.begin());
        if (!step.strA.empty()) ioSpec.valA = step.strA;
        if (!step.strB.empty()) ioSpec.valB = step.strB;
        if (!step.strC.empty()) ioSpec.valC = step.strC;
        ioSpec.boolA = step.boolA;
        ioSpec.boolB = step.boolB;
        for (std::size_t i = 0; i < step.optionChecks.size(); ++i) {
            if (i < ioSpec.optionStates.size()) ioSpec.optionStates[i] = step.optionChecks[i];
            if (i < ioSpec.features.size() && !ioSpec.features[i].required) ioSpec.features[i].selected = step.optionChecks[i];
        }
        return step.returnCode;
    }
    if (silentMode) {
        if (ioSpec.kind == RsDialogKind::License) ioSpec.boolA = true;
        return kResultNext;
    }

    HINSTANCE hInst = ::GetModuleHandleW(nullptr);
    static bool sRegistered = false;
    constexpr const wchar_t* kClsName = L"RaggieSoftRsDialogTurnkeyWindow";
    if (!sRegistered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc   = &detail::RsDialogWndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon         = ::LoadIconW(nullptr, IDI_APPLICATION);
        wc.lpszClassName = kClsName;
        ::RegisterClassExW(&wc);
        sRegistered = true;
    }

    detail::ModalSession st{};
    st.spec = &ioSpec;
    st.theme.Refresh();
    const UINT dpi = ::GetDpiForSystem();
    auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, st.theme.textScalePercent); };
    st.hFontHead = rsw::ui::CreateScaledFont(L"Segoe UI", 12, FW_BOLD,   dpi, st.theme.textScalePercent);
    st.hFontBody = rsw::ui::CreateScaledFont(L"Segoe UI",  9, FW_NORMAL, dpi, st.theme.textScalePercent);

    RECT winRc{0, 0, S(560), S(400)};
    constexpr DWORD kStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    ::AdjustWindowRectExForDpi(&winRc, kStyle, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int winW = winRc.right - winRc.left;
    const int winH = winRc.bottom - winRc.top;
    const int posX = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - winW) / 2);
    const int posY = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - winH) / 2);

    const std::wstring wCap = Utf8ToWide(ioSpec.windowTitle);
    HWND hWnd = ::CreateWindowExW(
        WS_EX_DLGMODALFRAME, kClsName, wCap.c_str(), kStyle,
        posX, posY, winW, winH, parentHwnd, nullptr, hInst, &st);
    if (!hWnd) return kResultCancel;

    if (st.theme.isDarkMode && !st.theme.isHighContrast) {
        const BOOL dark = TRUE;
        ::DwmSetWindowAttribute(hWnd, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark, sizeof(dark));
    }

    auto mkStatic = [&](const std::string& txt, int x, int y, int w, int h) {
        const std::wstring wt = Utf8ToWide(txt);
        HWND hs = ::CreateWindowExW(0, L"STATIC", wt.c_str(), WS_CHILD | WS_VISIBLE,
                                    S(x), S(y), S(w), S(h), hWnd, nullptr, hInst, nullptr);
        ::SendMessageW(hs, WM_SETFONT, reinterpret_cast<WPARAM>(st.hFontBody), TRUE);
        return hs;
    };
    auto mkEdit = [&](int id, const std::string& initVal, int x, int y, int w, int h, DWORD extra = ES_AUTOHSCROLL) {
        const std::wstring wt = Utf8ToWide(initVal);
        HWND he = ::CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", wt.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | extra,
                                    S(x), S(y), S(w), S(h), hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
        ::SendMessageW(he, WM_SETFONT, reinterpret_cast<WPARAM>(st.hFontBody), TRUE);
        return he;
    };
    auto mkBtn = [&](int id, const std::string& txt, int x, int y, int w, int h, bool def = false) {
        const std::wstring wt = Utf8ToWide(txt);
        HWND hb = ::CreateWindowExW(0, L"BUTTON", wt.c_str(),
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | (def ? BS_DEFPUSHBUTTON : BS_PUSHBUTTON),
                                    S(x), S(y), S(w), S(h), hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
        ::SendMessageW(hb, WM_SETFONT, reinterpret_cast<WPARAM>(st.hFontBody), TRUE);
        rsw::ui::AttachAccessiblePushButtonSubclass(hb, &st.theme, def, false);
        return hb;
    };

    switch (ioSpec.kind) {
        case RsDialogKind::Welcome:
        case RsDialogKind::ReadyToInstall:
            mkStatic(ioSpec.bodyText, 24, 80, 512, 240);
            break;
        case RsDialogKind::License: {
            mkEdit(detail::IDC_RSD_EDIT_A, ioSpec.bodyText, 22, 74, 516, 222,
                   ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL);
            const std::wstring wChk = Utf8ToWide(ioSpec.checkLabelA);
            st.hChkA = ::CreateWindowExW(0, L"BUTTON", wChk.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                         S(22), S(306), S(516), S(24), hWnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(detail::IDC_RSD_CHK_A)), hInst, nullptr);
            ::SendMessageW(st.hChkA, WM_SETFONT, reinterpret_cast<WPARAM>(st.hFontBody), TRUE);
            ::SendMessageW(st.hChkA, BM_SETCHECK, ioSpec.boolA ? BST_CHECKED : BST_UNCHECKED, 0);
            rsw::ui::AttachAccessibleCheckboxSubclass(st.hChkA, &st.theme, false);
            break;
        }
        case RsDialogKind::Readme:
            mkEdit(detail::IDC_RSD_EDIT_A, ioSpec.bodyText, 22, 74, 516, 256,
                   ES_MULTILINE | ES_READONLY | WS_VSCROLL | ES_AUTOVSCROLL);
            break;
        case RsDialogKind::UserInfo:
            mkStatic(ioSpec.bodyText.empty() ? "Enter customer registration details below:" : ioSpec.bodyText, 24, 74, 512, 22);
            mkStatic(ioSpec.fieldLabelA, 24, 106, 150, 22);
            st.hEditA = mkEdit(detail::IDC_RSD_EDIT_A, ioSpec.valA, 24, 128, 512, 26);
            mkStatic(ioSpec.fieldLabelB, 24, 166, 150, 22);
            st.hEditB = mkEdit(detail::IDC_RSD_EDIT_B, ioSpec.valB, 24, 188, 512, 26);
            mkStatic(ioSpec.fieldLabelC, 24, 226, 200, 22);
            st.hEditC = mkEdit(detail::IDC_RSD_EDIT_C, ioSpec.valC, 24, 248, 512, 26);
            break;
        case RsDialogKind::SelectDestDir:
            mkStatic(ioSpec.bodyText, 24, 76, 512, 40);
            mkStatic(ioSpec.fieldLabelA, 24, 126, 300, 22);
            st.hEditA = mkEdit(detail::IDC_RSD_EDIT_A, ioSpec.valA, 24, 150, 412, 26);
            mkBtn(detail::IDC_RSD_BROWSE, "B&rowse...", 444, 149, 92, 28, false);
            st.hDiskLbl = mkStatic("", 24, 190, 512, 24);
            detail::RefreshDiskSpaceLabel(&st);
            break;
        case RsDialogKind::SetupType: {
            mkStatic(ioSpec.bodyText, 24, 74, 512, 24);
            int y = 104;
            for (std::size_t i = 0; i < ioSpec.optionLabels.size(); ++i) {
                const std::wstring wl = Utf8ToWide(ioSpec.optionLabels[i]);
                const DWORD rStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0);
                HWND hr = ::CreateWindowExW(0, L"BUTTON", wl.c_str(), rStyle, S(28), S(y), S(504), S(28), hWnd,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(detail::IDC_RSD_DYN_BASE + static_cast<int>(i))), hInst, nullptr);
                ::SendMessageW(hr, WM_SETFONT, reinterpret_cast<WPARAM>(st.hFontBody), TRUE);
                const bool match = (i < ioSpec.optionValues.size() && EqualsIgnoreCase(ioSpec.valA, ioSpec.optionValues[i])) ||
                                   (ioSpec.valA.empty() && i == 0);
                if (match) ::SendMessageW(hr, BM_SETCHECK, BST_CHECKED, 0);
                st.dynChecks.push_back(hr);
                y += 38;
            }
            break;
        }
        case RsDialogKind::FeatureTree: {
            mkStatic(ioSpec.bodyText, 24, 74, 512, 24);
            int y = 104;
            for (std::size_t i = 0; i < ioSpec.features.size(); ++i) {
                const auto& ft = ioSpec.features[i];
                const std::wstring wl = Utf8ToWide(ft.title + (ft.description.empty() ? "" : (" — " + ft.description)));
                HWND hc = ::CreateWindowExW(0, L"BUTTON", wl.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                            S(28), S(y), S(504), S(26), hWnd,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(detail::IDC_RSD_DYN_BASE + static_cast<int>(i))), hInst, nullptr);
                ::SendMessageW(hc, WM_SETFONT, reinterpret_cast<WPARAM>(st.hFontBody), TRUE);
                ::SendMessageW(hc, BM_SETCHECK, (ft.required || ft.selected) ? BST_CHECKED : BST_UNCHECKED, 0);
                if (ft.required) ::EnableWindow(hc, FALSE);
                rsw::ui::AttachAccessibleCheckboxSubclass(hc, &st.theme, false);
                st.dynChecks.push_back(hc);
                y += 32;
            }
            break;
        }
        case RsDialogKind::AskOptions: {
            mkStatic(ioSpec.bodyText, 24, 74, 512, 28);
            int y = 108;
            for (std::size_t i = 0; i < ioSpec.optionLabels.size(); ++i) {
                const std::wstring wl = Utf8ToWide(ioSpec.optionLabels[i]);
                const DWORD bStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                    (ioSpec.exclusiveOptions ? (BS_AUTORADIOBUTTON | (i == 0 ? WS_GROUP : 0)) : BS_AUTOCHECKBOX);
                HWND hc = ::CreateWindowExW(0, L"BUTTON", wl.c_str(), bStyle, S(28), S(y), S(504), S(26), hWnd,
                                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(detail::IDC_RSD_DYN_BASE + static_cast<int>(i))), hInst, nullptr);
                ::SendMessageW(hc, WM_SETFONT, reinterpret_cast<WPARAM>(st.hFontBody), TRUE);
                const bool chk = (i < ioSpec.optionStates.size()) ? ioSpec.optionStates[i] : (ioSpec.exclusiveOptions && i == 0);
                ::SendMessageW(hc, BM_SETCHECK, chk ? BST_CHECKED : BST_UNCHECKED, 0);
                if (!ioSpec.exclusiveOptions) rsw::ui::AttachAccessibleCheckboxSubclass(hc, &st.theme, false);
                st.dynChecks.push_back(hc);
                y += 32;
            }
            break;
        }
        case RsDialogKind::AskText:
            mkStatic(ioSpec.bodyText, 24, 78, 512, 36);
            mkStatic(ioSpec.fieldLabelA, 24, 124, 300, 22);
            st.hEditA = mkEdit(detail::IDC_RSD_EDIT_A, ioSpec.valA, 24, 148, 512, 26,
                               ES_AUTOHSCROLL | (ioSpec.passwordInput ? ES_PASSWORD : 0));
            break;
        case RsDialogKind::Finish: {
            mkStatic(ioSpec.bodyText, 24, 80, 512, 120);
            if (!ioSpec.checkLabelA.empty()) {
                const std::wstring wA = Utf8ToWide(ioSpec.checkLabelA);
                st.hChkA = ::CreateWindowExW(0, L"BUTTON", wA.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                             S(28), S(220), S(500), S(26), hWnd,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(detail::IDC_RSD_CHK_A)), hInst, nullptr);
                ::SendMessageW(st.hChkA, WM_SETFONT, reinterpret_cast<WPARAM>(st.hFontBody), TRUE);
                ::SendMessageW(st.hChkA, BM_SETCHECK, ioSpec.boolA ? BST_CHECKED : BST_UNCHECKED, 0);
                rsw::ui::AttachAccessibleCheckboxSubclass(st.hChkA, &st.theme, false);
            }
            if (!ioSpec.checkLabelB.empty()) {
                const std::wstring wB = Utf8ToWide(ioSpec.checkLabelB);
                st.hChkB = ::CreateWindowExW(0, L"BUTTON", wB.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                             S(28), S(254), S(500), S(26), hWnd,
                                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(detail::IDC_RSD_CHK_B)), hInst, nullptr);
                ::SendMessageW(st.hChkB, WM_SETFONT, reinterpret_cast<WPARAM>(st.hFontBody), TRUE);
                ::SendMessageW(st.hChkB, BM_SETCHECK, ioSpec.boolB ? BST_CHECKED : BST_UNCHECKED, 0);
                rsw::ui::AttachAccessibleCheckboxSubclass(st.hChkB, &st.theme, false);
            }
            break;
        }
        case RsDialogKind::FinishReboot: {
            mkStatic(ioSpec.bodyText, 24, 80, 512, 100);
            HWND rNow = ::CreateWindowExW(0, L"BUTTON", L"Yes, &restart my computer now",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON | WS_GROUP,
                                          S(28), S(200), S(500), S(26), hWnd,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(detail::IDC_RSD_DYN_BASE)), hInst, nullptr);
            HWND rLat = ::CreateWindowExW(0, L"BUTTON", L"No, I will restart my computer &later",
                                          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTORADIOBUTTON,
                                          S(28), S(234), S(500), S(26), hWnd,
                                          reinterpret_cast<HMENU>(static_cast<INT_PTR>(detail::IDC_RSD_DYN_BASE + 1)), hInst, nullptr);
            ::SendMessageW(rNow, WM_SETFONT, reinterpret_cast<WPARAM>(st.hFontBody), TRUE);
            ::SendMessageW(rLat, WM_SETFONT, reinterpret_cast<WPARAM>(st.hFontBody), TRUE);
            ::SendMessageW(ioSpec.boolA ? rNow : rLat, BM_SETCHECK, BST_CHECKED, 0);
            st.dynChecks.push_back(rNow);
            st.dynChecks.push_back(rLat);
            break;
        }
    }

    if (ioSpec.showBackButton) {
        mkBtn(detail::IDC_RSD_BACK, "< &Back", 262, 360, 86, 28, false);
    }
    st.hNextBtn = mkBtn(detail::IDC_RSD_NEXT, ioSpec.primaryBtnText, 356, 360, 92, 28, true);
    if (ioSpec.showUacShield) {
        ::SendMessageW(st.hNextBtn, BCM_SETSHIELD, 0, TRUE);
    }
    if (ioSpec.kind == RsDialogKind::License && !ioSpec.boolA) {
        ::EnableWindow(st.hNextBtn, FALSE);
    }
    if (ioSpec.kind != RsDialogKind::Finish && ioSpec.kind != RsDialogKind::FinishReboot) {
        mkBtn(detail::IDC_RSD_CANCEL, "Cancel", 458, 360, 82, 28, false);
    }

    if (parentHwnd) ::EnableWindow(parentHwnd, FALSE);
    ::ShowWindow(hWnd, SW_SHOWNORMAL);
    ::UpdateWindow(hWnd);

    MSG m{};
    while (!st.done && ::GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (!::IsDialogMessageW(hWnd, &m)) {
            ::TranslateMessage(&m);
            ::DispatchMessageW(&m);
        }
    }

    if (parentHwnd) {
        ::EnableWindow(parentHwnd, TRUE);
        ::SetForegroundWindow(parentHwnd);
    }
    ::DestroyWindow(hWnd);
    return st.result;
}

} // namespace rsw::rssc::rsdlg
