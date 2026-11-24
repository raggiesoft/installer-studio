// ================================================================================
// FILE: src/prism/MsiPrismMain.cpp
// PROJECT: RaggieSoft Installer Studio — MSI Prism ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `msiprism.exe` ("RaggieSoft MSI Prism")?
//     Microsoft's classic Windows SDK database viewer (`Orca.exe`) is still a
//     32-bit Windows 2000 binary tucked inside an `.msi` installer with no Dark
//     Mode, no ARM64 build, and no Per-Monitor V2 DPI scaling.
//
//     `msiprism.exe` is a zero-dependency static `/MT` Win32 C++20 database
//     inspector (`x64` & `ARM64`) built right into **RaggieSoft Installer Studio**:
//       1. Opens any `.msi` (Installation Database), `.msm` (Merge Module), or
//          `.msp` (Patch Package via `MSIDBOPEN_READONLY + MSIDBOPEN_PATCHFILE`).
//       2. Lists `SummaryInformation` plus every relational SQL table (`_Tables`)
//          in the left pane, and renders all columns and rows inside a fast,
//          keyboard-accessible Win32 `SysListView32` table!
//       3. Runs instant relational/ICE80 audits and generates `.mst` Transforms
//          or `.msp` Patch Packages by diffing two `.msi` files!
//       4. Supports `--inspect-summary <file>` for headless CI/CD & self-tests!
// ================================================================================

#include "../common/RswWin32UiTheme.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswVersion.hpp"
#include "../compiler/RswExtraPackages.hpp"
#include "../compiler/RswMsiCompiler.hpp"

#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

enum PrismId : int {
    IDC_PRISM_OPEN      = 3001,
    IDC_PRISM_VERIFY    = 3002,
    IDC_PRISM_GEN_MST   = 3003,
    IDC_PRISM_GEN_MSP   = 3004,
    IDC_PRISM_TABLELIST = 3010,
    IDC_PRISM_ROWGRID   = 3011,
    IDC_PRISM_STATUS    = 3012
};

struct SummaryPropRow {
    UINT           pid;
    const wchar_t* name;
};

// Standard OLE `SummaryInformation` Property IDs (`PID_*`) defined by `msi.dll`:
constexpr SummaryPropRow kKnownSummaryProps[] = {
    {1,  L"PID_CODEPAGE (1)"},
    {2,  L"PID_TITLE (2)"},
    {3,  L"PID_SUBJECT / Product (3)"},
    {4,  L"PID_AUTHOR / Manufacturer (4)"},
    {5,  L"PID_KEYWORDS (5)"},
    {6,  L"PID_COMMENTS (6)"},
    {7,  L"PID_TEMPLATE / Platform;LCID (7)"},
    {8,  L"PID_LASTAUTHOR / Transforms (8)"},
    {9,  L"PID_REVNUMBER / PackageCode (9)"},
    {14, L"PID_PAGECOUNT / Schema (14)"},
    {15, L"PID_WORDCOUNT / SourceFlags (15)"},
    {18, L"PID_APPNAME / Generator (18)"},
    {19, L"PID_SECURITY (19)"}
};

/// Formats a single `SummaryInformation` property (`VT_I2`, `VT_I4`, `VT_LPSTR`, or `VT_FILETIME`) as UTF-16 text.
std::wstring ReadSummaryPropertyDisplay(MSIHANDLE hSum, UINT pid) {
    UINT dataType = 0;
    INT iVal = 0;
    FILETIME ft{};
    wchar_t strBuf[1024]{};
    DWORD cch = static_cast<DWORD>(std::size(strBuf));
    if (::MsiSummaryInfoGetPropertyW(hSum, pid, &dataType, &iVal, &ft, strBuf, &cch) != ERROR_SUCCESS) {
        return L"(not set)";
    }
    if (dataType == VT_I2 || dataType == VT_I4) {
        return std::to_wstring(iVal);
    }
    if (dataType == VT_LPSTR) {
        return strBuf;
    }
    if (dataType == VT_FILETIME) {
        SYSTEMTIME st{};
        if (::FileTimeToSystemTime(&ft, &st)) {
            wchar_t dt[64]{};
            std::swprintf(dt, std::size(dt), L"%04u-%02u-%02u %02u:%02u:%02u UTC",
                          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
            return dt;
        }
    }
    return L"(empty)";
}

/// Opens an `.msi`, `.msm`, or `.msp` database read-only via `MsiOpenDatabaseW`.
bool OpenAnyMsiOrPatchReadOnly(const std::filesystem::path& path, rsw::ScopedMsiHandle& outDb, bool& outIsPatch) {
    const std::wstring ext = path.extension().wstring();
    outIsPatch = (_wcsicmp(ext.c_str(), L".msp") == 0);
    LPCWSTR mode = outIsPatch ? (MSIDBOPEN_READONLY + MSIDBOPEN_PATCHFILE) : MSIDBOPEN_READONLY;
    if (::MsiOpenDatabaseW(path.wstring().c_str(), mode, outDb.Put()) == ERROR_SUCCESS) {
        return true;
    }
    outIsPatch = !outIsPatch;
    mode = outIsPatch ? (MSIDBOPEN_READONLY + MSIDBOPEN_PATCHFILE) : MSIDBOPEN_READONLY;
    return ::MsiOpenDatabaseW(path.wstring().c_str(), mode, outDb.Put()) == ERROR_SUCCESS;
}

/// Queries the built-in `_Tables` system catalog to list every relational SQL table in the database.
std::vector<std::wstring> QueryTableNames(MSIHANDLE hDb) {
    std::vector<std::wstring> names;
    rsw::ScopedMsiHandle hView;
    if (::MsiDatabaseOpenViewW(hDb, L"SELECT `Name` FROM `_Tables`", hView.Put()) == ERROR_SUCCESS &&
        ::MsiViewExecute(hView.Get(), 0) == ERROR_SUCCESS) {
        rsw::ScopedMsiHandle hRec;
        while (::MsiViewFetch(hView.Get(), hRec.Put()) == ERROR_SUCCESS) {
            wchar_t buf[128]{};
            DWORD cch = static_cast<DWORD>(std::size(buf));
            if (::MsiRecordGetStringW(hRec.Get(), 1, buf, &cch) == ERROR_SUCCESS) {
                names.emplace_back(buf);
            }
        }
    }
    std::sort(names.begin(), names.end());
    return names;
}

class MsiPrismWindow {
public:
    rsw::ui::RswTheme     theme{};
    HWND                  hWnd       = nullptr;
    HFONT                 hFontTitle = nullptr;
    HFONT                 hFontUi    = nullptr;
    HFONT                 hFontMono  = nullptr;

    std::filesystem::path currentPath;
    rsw::ScopedMsiHandle  hOpenDb;
    bool                  isPatchPkg = false;

    HWND hBtnOpen = nullptr, hBtnVerify = nullptr, hBtnGenMst = nullptr, hBtnGenMsp = nullptr;
    HWND hTableList = nullptr, hRowGrid = nullptr, hStatusEdit = nullptr;

    ~MsiPrismWindow() {
        for (HFONT* f : {&hFontTitle, &hFontUi, &hFontMono}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
    }

    void CreateControls() {
        HINSTANCE hInst = ::GetModuleHandleW(nullptr);
        auto mkBtn = [&](const wchar_t* txt, int id) {
            return ::CreateWindowExW(
                0, L"BUTTON", txt, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
        };

        hBtnOpen   = mkBtn(L"&Open .MSI / .MSM / .MSP...", IDC_PRISM_OPEN);
        hBtnVerify = mkBtn(L"&Verify ICE80 / Schema",       IDC_PRISM_VERIFY);
        hBtnGenMst = mkBtn(L"Diff -> .&MST Transform...",   IDC_PRISM_GEN_MST);
        hBtnGenMsp = mkBtn(L"Diff -> .MS&P Patch...",       IDC_PRISM_GEN_MSP);

        hTableList = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"LISTBOX", nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PRISM_TABLELIST)), hInst, nullptr);

        hRowGrid = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PRISM_ROWGRID)), hInst, nullptr);
        ListView_SetExtendedListViewStyle(hRowGrid, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        hStatusEdit = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"Open an .msi, .msm, or .msp database in MSI Prism to inspect its SummaryInformation and relational SQL tables.",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PRISM_STATUS)), hInst, nullptr);
    }

    void LayoutChildren() {
        const UINT dpi = ::GetDpiForWindow(hWnd);
        for (HFONT* f : {&hFontTitle, &hFontUi, &hFontMono}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
        hFontTitle = rsw::ui::CreateScaledFont(L"Segoe UI",      13, FW_BOLD,   dpi, theme.textScalePercent);
        hFontUi    = rsw::ui::CreateScaledFont(L"Segoe UI",       9, FW_NORMAL, dpi, theme.textScalePercent);
        hFontMono  = rsw::ui::CreateScaledFont(L"Cascadia Mono",  9, FW_NORMAL, dpi, theme.textScalePercent);

        for (HWND h : {hBtnOpen, hBtnVerify, hBtnGenMst, hBtnGenMsp, hTableList, hRowGrid}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUi), TRUE);
        }
        if (hStatusEdit) ::SendMessageW(hStatusEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hFontMono), TRUE);

        if (!theme.isHighContrast) {
            ListView_SetBkColor(hRowGrid, theme.cardBg);
            ListView_SetTextBkColor(hRowGrid, theme.cardBg);
            ListView_SetTextColor(hRowGrid, theme.textPrimary);
        }

        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(420, rc.right - rc.left);
        const int H = std::max<int>(320, rc.bottom - rc.top);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        const int tbY = S(62);
        ::MoveWindow(hBtnOpen,   S(12),  tbY, S(186), S(30), TRUE);
        ::MoveWindow(hBtnVerify, S(206), tbY, S(156), S(30), TRUE);
        ::MoveWindow(hBtnGenMst, S(370), tbY, S(172), S(30), TRUE);
        ::MoveWindow(hBtnGenMsp, S(550), tbY, S(162), S(30), TRUE);

        const int topY    = S(102);
        const int statusH = S(96);
        const int mainH   = std::max(S(140), H - topY - statusH - S(20));
        const int leftW   = S(220);

        ::MoveWindow(hTableList,  S(12), topY, leftW, mainH, TRUE);
        ::MoveWindow(hRowGrid,    S(24) + leftW, topY, W - leftW - S(36), mainH, TRUE);
        ::MoveWindow(hStatusEdit, S(12), topY + mainH + S(8), W - S(24), statusH, TRUE);
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    void ClearGridColumns() {
        ListView_DeleteAllItems(hRowGrid);
        while (ListView_DeleteColumn(hRowGrid, 0)) {}
    }

    void LoadDatabaseFile(const std::filesystem::path& path) {
        rsw::ScopedMsiHandle newDb;
        bool patchFlag = false;
        if (!OpenAnyMsiOrPatchReadOnly(path, newDb, patchFlag)) {
            ::SetWindowTextW(hStatusEdit, (L"[ERROR] Failed opening Windows Installer database: " + path.wstring()).c_str());
            return;
        }

        currentPath = path;
        hOpenDb     = std::move(newDb);
        isPatchPkg  = patchFlag;

        ::SendMessageW(hTableList, LB_RESETCONTENT, 0, 0);
        ::SendMessageW(hTableList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"[SummaryInformation]"));
        const auto tables = QueryTableNames(hOpenDb.Get());
        for (const auto& t : tables) {
            ::SendMessageW(hTableList, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(t.c_str()));
        }
        ::SendMessageW(hTableList, LB_SETCURSEL, 0, 0);
        PopulateSelectedTable();

        const std::wstring title = L"RaggieSoft MSI Prism — " + path.filename().wstring();
        ::SetWindowTextW(hWnd, title.c_str());
        ::SetWindowTextW(
            hStatusEdit,
            (L"[OK] Opened " + std::wstring(isPatchPkg ? L"Patch (.msp)" : L"Database (.msi/.msm)") + L": " +
             path.wstring() + L" (" + std::to_wstring(tables.size()) + L" SQL tables).").c_str());
    }

    void PopulateSelectedTable() {
        if (!hOpenDb) return;
        const LRESULT sel = ::SendMessageW(hTableList, LB_GETCURSEL, 0, 0);
        if (sel == LB_ERR) return;

        wchar_t tableName[128]{};
        ::SendMessageW(hTableList, LB_GETTEXT, static_cast<WPARAM>(sel), reinterpret_cast<LPARAM>(tableName));

        ClearGridColumns();
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto addCol = [&](int idx, const wchar_t* label, int widthPx) {
            LVCOLUMNW col{};
            col.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<LPWSTR>(label);
            col.cx      = rsw::ui::ScalePx(widthPx, dpi, theme.textScalePercent);
            col.iSubItem= idx;
            ListView_InsertColumn(hRowGrid, idx, &col);
        };

        if (::lstrcmpW(tableName, L"[SummaryInformation]") == 0) {
            addCol(0, L"Property ID", 240);
            addCol(1, L"Value",       540);
            rsw::ScopedMsiHandle hSum;
            if (::MsiGetSummaryInformationW(hOpenDb.Get(), nullptr, 0, hSum.Put()) == ERROR_SUCCESS) {
                int rowIdx = 0;
                for (const auto& sp : kKnownSummaryProps) {
                    LVITEMW it{};
                    it.mask    = LVIF_TEXT;
                    it.iItem   = rowIdx;
                    it.pszText = const_cast<LPWSTR>(sp.name);
                    ListView_InsertItem(hRowGrid, &it);
                    std::wstring val = ReadSummaryPropertyDisplay(hSum.Get(), sp.pid);
                    ListView_SetItemText(hRowGrid, rowIdx, 1, val.data());
                    ++rowIdx;
                }
            }
            return;
        }

        const std::wstring sql = L"SELECT * FROM `" + std::wstring(tableName) + L"`";
        rsw::ScopedMsiHandle hView;
        if (::MsiDatabaseOpenViewW(hOpenDb.Get(), sql.c_str(), hView.Put()) != ERROR_SUCCESS ||
            ::MsiViewExecute(hView.Get(), 0) != ERROR_SUCCESS) {
            return;
        }

        rsw::ScopedMsiHandle hNames;
        rsw::ScopedMsiHandle hTypes;
        ::MsiViewGetColumnInfo(hView.Get(), MSICOLINFO_NAMES, hNames.Put());
        ::MsiViewGetColumnInfo(hView.Get(), MSICOLINFO_TYPES, hTypes.Put());
        const UINT colCount = ::MsiRecordGetFieldCount(hNames.Get());
        std::vector<bool> isBinaryCol(colCount + 1, false);

        for (UINT c = 1; c <= colCount; ++c) {
            wchar_t cName[128]{};
            DWORD cch = static_cast<DWORD>(std::size(cName));
            ::MsiRecordGetStringW(hNames.Get(), c, cName, &cch);
            addCol(static_cast<int>(c - 1), cName, 165);

            wchar_t cType[32]{};
            DWORD tcch = static_cast<DWORD>(std::size(cType));
            ::MsiRecordGetStringW(hTypes.Get(), c, cType, &tcch);
            if (cType[0] == L'v' || cType[0] == L'V') {
                isBinaryCol[c] = true;
            }
        }

        int rowIdx = 0;
        rsw::ScopedMsiHandle hRec;
        while (::MsiViewFetch(hView.Get(), hRec.Put()) == ERROR_SUCCESS && rowIdx < 5000) {
            for (UINT c = 1; c <= colCount; ++c) {
                std::wstring cellText;
                if (::MsiRecordIsNull(hRec.Get(), c)) {
                    cellText = L"";
                } else if (isBinaryCol[c]) {
                    const unsigned int byteSz = ::MsiRecordDataSize(hRec.Get(), c);
                    cellText = L"[Binary Stream: " + std::to_wstring(byteSz) + L" bytes]";
                } else {
                    wchar_t stackBuf[512]{};
                    DWORD cch = static_cast<DWORD>(std::size(stackBuf));
                    UINT grc = ::MsiRecordGetStringW(hRec.Get(), c, stackBuf, &cch);
                    if (grc == ERROR_MORE_DATA) {
                        std::wstring big(cch + 1, L'\0');
                        ++cch;
                        ::MsiRecordGetStringW(hRec.Get(), c, big.data(), &cch);
                        big.resize(cch);
                        cellText = std::move(big);
                    } else {
                        cellText = stackBuf;
                    }
                }

                if (c == 1) {
                    LVITEMW it{};
                    it.mask    = LVIF_TEXT;
                    it.iItem   = rowIdx;
                    it.pszText = cellText.data();
                    ListView_InsertItem(hRowGrid, &it);
                } else {
                    ListView_SetItemText(hRowGrid, rowIdx, static_cast<int>(c - 1), cellText.data());
                }
            }
            ++rowIdx;
        }
    }

    bool PickFileDialog(bool saveMode, const wchar_t* filter, const wchar_t* defExt, std::filesystem::path& outPath) {
        wchar_t buf[MAX_PATH * 2]{};
        OPENFILENAMEW ofn{};
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner   = hWnd;
        ofn.lpstrFilter = filter;
        ofn.lpstrFile   = buf;
        ofn.nMaxFile    = static_cast<DWORD>(std::size(buf));
        ofn.lpstrDefExt = defExt;
        ofn.Flags       = saveMode ? (OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST) : (OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST);
        const BOOL ok   = saveMode ? ::GetSaveFileNameW(&ofn) : ::GetOpenFileNameW(&ofn);
        if (!ok) return false;
        outPath = buf;
        return true;
    }

    void PaintBanner(HDC hdc) {
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        ::FillRect(hdc, &rc, theme.hbrWindow);
        RECT bannerRc{0, 0, rc.right, S(50)};
        rsw::ui::FillHorizontalGradient(hdc, bannerRc, theme.bannerNavyLeft, theme.bannerNavyRight);
        RECT accentRc{0, bannerRc.bottom, rc.right, bannerRc.bottom + S(4)};
        rsw::ui::FillHorizontalGradient(hdc, accentRc, RGB(14, 165, 233), RGB(251, 191, 36));

        ::SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ oldF = ::SelectObject(hdc, hFontTitle);
        ::SetTextColor(hdc, theme.bannerTitleText);
        RECT tRc{S(16), S(10), rc.right - S(16), S(42)};
        ::DrawTextW(hdc, L"RaggieSoft MSI Prism  •  Native 64-Bit MSI, MSM, MST & MSP SQL Inspector",
                    -1, &tRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
        ::SelectObject(hdc, oldF);
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        MsiPrismWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<MsiPrismWindow*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hWnd = hWnd;
        } else {
            self = reinterpret_cast<MsiPrismWindow*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
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
                self->PaintBanner(hdc);
                ::EndPaint(hWnd, &ps);
                return 0;
            }
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORSTATIC: {
                HDC dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, self->theme.textPrimary);
                ::SetBkColor(dc, self->theme.cardBg);
                return reinterpret_cast<LRESULT>(self->theme.hbrCard);
            }
            case WM_COMMAND: {
                const int id   = LOWORD(wParam);
                const int code = HIWORD(wParam);
                if (id == IDC_PRISM_TABLELIST && code == LBN_SELCHANGE) {
                    self->PopulateSelectedTable();
                    return 0;
                }
                if (id == IDC_PRISM_OPEN) {
                    std::filesystem::path p;
                    if (self->PickFileDialog(false, L"Windows Installer (*.msi;*.msm;*.msp)\0*.msi;*.msm;*.msp\0All Files\0*.*\0", L"msi", p)) {
                        self->LoadDatabaseFile(p);
                    }
                    return 0;
                }
                if (id == IDC_PRISM_VERIFY && !self->currentPath.empty()) {
                    std::string err;
                    bool ok = false;
                    if (_wcsicmp(self->currentPath.extension().wstring().c_str(), L".msm") == 0) {
                        ok = rsw::VerifyCompiledMsmDatabase(self->currentPath, err);
                    } else {
                        ok = rsw::VerifyCompiledMsiDatabase(self->currentPath, err);
                    }
                    ::SetWindowTextW(
                        self->hStatusEdit,
                        ok ? L"[PASS] ICE80 64-bit component flags, SummaryInformation, and relational tables verified cleanly!"
                           : (L"[FAIL] " + rsw::Utf8ToWide(err)).c_str());
                    return 0;
                }
                if ((id == IDC_PRISM_GEN_MST || id == IDC_PRISM_GEN_MSP) && !self->currentPath.empty()) {
                    std::filesystem::path targetMsi, outPkg;
                    if (!self->PickFileDialog(false, L"Target / Upgraded MSI (*.msi)\0*.msi\0", L"msi", targetMsi)) return 0;
                    const bool wantMst = (id == IDC_PRISM_GEN_MST);
                    if (!self->PickFileDialog(
                            true,
                            wantMst ? L"Transform (*.mst)\0*.mst\0" : L"Patch (*.msp)\0*.msp\0",
                            wantMst ? L"mst" : L"msp",
                            outPkg)) return 0;
                    std::string err;
                    const bool ok = wantMst
                        ? rsw::GenerateMsiTransform(self->currentPath, targetMsi, outPkg, err)
                        : rsw::GenerateMsiPatchPackage(self->currentPath, targetMsi, outPkg, err);
                    ::SetWindowTextW(
                        self->hStatusEdit,
                        ok ? (L"[OK] Generated " + outPkg.wstring()).c_str()
                           : (L"[ERROR] " + rsw::Utf8ToWide(err)).c_str());
                    return 0;
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

    // Headless inspection mode (`msiprism.exe --inspect-summary <file>`) used in CI & automated self-tests
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--inspect-summary") {
            if (i + 1 >= args.size()) return 2;
            rsw::ScopedMsiHandle hDb;
            bool isPatch = false;
            if (!OpenAnyMsiOrPatchReadOnly(args[i + 1], hDb, isPatch)) return 1;
            const auto tables = QueryTableNames(hDb.Get());
            rsw::ScopedMsiHandle hSum;
            if (::MsiGetSummaryInformationW(hDb.Get(), nullptr, 0, hSum.Put()) != ERROR_SUCCESS) return 3;
            const std::wstring gen = ReadSummaryPropertyDisplay(hSum.Get(), 18);
            return (tables.empty() && !isPatch && gen.empty()) ? 4 : 0;
        }
    }

    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES};
    ::InitCommonControlsEx(&icc);

    MsiPrismWindow prism{};
    prism.theme.Refresh(RGB(14, 165, 233), RGB(251, 191, 36));

    constexpr const wchar_t* kCls = L"RaggieSoftMsiPrismWindow";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &MsiPrismWindow::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kCls;
    ::RegisterClassExW(&wc);

    const UINT dpi = ::GetDpiForSystem();
    const int W = rsw::ui::ScalePx(1080, dpi, prism.theme.textScalePercent);
    const int H = rsw::ui::ScalePx(680,  dpi, prism.theme.textScalePercent);
    HWND hWnd = ::CreateWindowExW(
        0, kCls, L"RaggieSoft MSI Prism",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, W, H,
        nullptr, nullptr, hInstance, &prism);
    if (!hWnd) return 1;

    if (!args.empty() && std::filesystem::exists(args.front())) {
        prism.LoadDatabaseFile(args.front());
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
    return 0;
}
