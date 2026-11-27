// ================================================================================
// FILE: src/prism/MsiPrismMain.cpp
// PROJECT: RaggieSoft MSI Prism — Standalone & Studio Relational MSI Table Editor
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `msiprism.exe` ("RaggieSoft MSI Prism")?
//     `msiprism.exe` is RaggieSoft's zero-dependency static `/MT` 64-bit (`x64` &
//     `ARM64`) Windows Installer relational SQL database editor and inspector—
//     available both bundled inside **RaggieSoft Installer Studio** AND as a
//     standalone Windows 11 developer utility:
//       1. **Direct Read/Write Relational Table Editing (`MSIDBOPEN_TRANSACT`)**:
//          - Create brand-new `.msi` databases (`&New .MSI...`), open existing
//            `.msi`, `.msm` (Merge Module), or `.msp` (Patch Package) archives,
//            and save copies (`&Save Copy As...`).
//          - Add new rows (`+ &Add Row...`), edit any row or `SummaryInformation`
//            property by double-clicking or clicking `&Edit Row...`, delete rows
//            (`&Delete Row`), and import/export `Binary` or `Icon` table streams!
//       2. **Interactive Live MSI SQL Query & DDL/DML Bar**:
//          - Type any MSI SQL query (`SELECT`, `INSERT INTO`, `UPDATE`, `DELETE`,
//            `CREATE TABLE`, `ALTER TABLE`) into the top SQL bar and click
//            `E&xecute SQL` to inspect result sets or commit schema/row changes!
//       3. **Categorized Schema Tree, ICE80 Verifier, & `.MST` / `.MSP` Diffing**:
//          - Groups `SummaryInformation` + `_Tables` into 5 logical categories
//            with 32-bit ARGB vector icons, True Dark/Light/High-Contrast themes,
//            Per-Monitor V2 DPI scaling, `.mst` Transform & `.msp` Patch diffing,
//            and headless CI flags (`--inspect-summary`, `--exec-sql`,
//            `--export-table-json`, `--export-screenshots`).
// ================================================================================

#include "../common/RswGlassRenderer.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswVersion.hpp"
#include "../compiler/RswExtraPackages.hpp"
#include "../compiler/RswMsiCompiler.hpp"

#include <commdlg.h>
#include <shellapi.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "shell32.lib")

namespace {

enum PrismId : int {
    IDC_PRISM_OPEN       = 3001,
    IDC_PRISM_NEW        = 3002,
    IDC_PRISM_SAVE_COPY  = 3003,
    IDC_PRISM_ADD_ROW    = 3004,
    IDC_PRISM_EDIT_ROW   = 3005,
    IDC_PRISM_DEL_ROW    = 3006,
    IDC_PRISM_VERIFY     = 3007,
    IDC_PRISM_GEN_MST    = 3008,
    IDC_PRISM_GEN_MSP    = 3009,
    IDC_PRISM_TABLETREE  = 3010,
    IDC_PRISM_ROWGRID    = 3011,
    IDC_PRISM_STATUS     = 3012,
    IDC_PRISM_SQL_LABEL  = 3013,
    IDC_PRISM_SQL_EDIT   = 3014,
    IDC_PRISM_SQL_RUN    = 3015,
    IDC_PRISM_EXPORT_JSON= 3016,
    IDM_PRISM_SYS_ABOUT  = 0x1F20
};

struct SummaryPropRow {
    UINT           pid;
    const wchar_t* name;
    bool           isInteger;
};

// Standard OLE `SummaryInformation` Property IDs (`PID_*`) defined by `msi.dll`:
constexpr SummaryPropRow kKnownSummaryProps[] = {
    {1,  L"PID_CODEPAGE (1)",               true},
    {2,  L"PID_TITLE (2)",                  false},
    {3,  L"PID_SUBJECT / Product (3)",      false},
    {4,  L"PID_AUTHOR / Manufacturer (4)",  false},
    {5,  L"PID_KEYWORDS (5)",               false},
    {6,  L"PID_COMMENTS (6)",               false},
    {7,  L"PID_TEMPLATE / Platform;LCID (7)", false},
    {8,  L"PID_LASTAUTHOR / Transforms (8)",false},
    {9,  L"PID_REVNUMBER / PackageCode (9)",false},
    {14, L"PID_PAGECOUNT / Schema (14)",    true},
    {15, L"PID_WORDCOUNT / SourceFlags (15)",true},
    {18, L"PID_APPNAME / Generator (18)",   false},
    {19, L"PID_SECURITY (19)",              true}
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

/// Queries primary key column names for a relational MSI table via `MsiDatabaseGetPrimaryKeysW`.
std::vector<std::wstring> QueryTablePrimaryKeys(MSIHANDLE hDb, const std::wstring& tableName) {
    std::vector<std::wstring> pks;
    rsw::ScopedMsiHandle hPkRec;
    if (::MsiDatabaseGetPrimaryKeysW(hDb, tableName.c_str(), hPkRec.Put()) == ERROR_SUCCESS) {
        const UINT cnt = ::MsiRecordGetFieldCount(hPkRec.Get());
        for (UINT i = 1; i <= cnt; ++i) {
            wchar_t buf[128]{};
            DWORD cch = static_cast<DWORD>(std::size(buf));
            if (::MsiRecordGetStringW(hPkRec.Get(), i, buf, &cch) == ERROR_SUCCESS) {
                pks.emplace_back(buf);
            }
        }
    }
    return pks;
}

/// Classifies a Windows Installer SQL table name into one of 5 logical schema categories (`0..4`).
int ClassifyMsiSqlTableCategory(const std::wstring& t) noexcept {
    auto eq = [&](const wchar_t* s) { return _wcsicmp(t.c_str(), s) == 0; };
    if (eq(L"[SummaryInformation]") || eq(L"_Validation") || eq(L"_Tables") || eq(L"_Columns") ||
        eq(L"_Streams") || eq(L"_Storages") || eq(L"Media") || eq(L"Binary") || eq(L"Icon") ||
        eq(L"MsiDigitalCertificate") || eq(L"MsiDigitalSignature") || eq(L"MsiPatchMetadata") ||
        eq(L"MsiPatchSequence")) {
        return 0; // Metadata & Streams
    }
    if (eq(L"Property") || eq(L"Directory") || eq(L"Feature") || eq(L"FeatureComponents") ||
        eq(L"Component") || eq(L"File") || eq(L"CreateFolder") || eq(L"RemoveFile") ||
        eq(L"DuplicateFile") || eq(L"MoveFile") || eq(L"ReserveCost") || eq(L"Font") ||
        eq(L"Upgrade") || eq(L"ModuleSignature") || eq(L"ModuleComponents") ||
        eq(L"ModuleDependency") || eq(L"ModuleExclusion")) {
        return 1; // Core Packaging Tables
    }
    if (eq(L"Registry") || eq(L"RemoveRegistry") || eq(L"Shortcut") || eq(L"ProgId") ||
        eq(L"Extension") || eq(L"Verb") || eq(L"MIME") || eq(L"Class") || eq(L"TypeLib") ||
        eq(L"AppId") || eq(L"Environment") || eq(L"IniFile") || eq(L"RemoveIniFile") ||
        eq(L"ServiceInstall") || eq(L"ServiceControl") || eq(L"LockPermissions") ||
        eq(L"MsiLockPermissionsEx")) {
        return 2; // Registry, Shortcuts & Classes
    }
    if (eq(L"Dialog") || eq(L"Control") || eq(L"ControlEvent") || eq(L"ControlCondition") ||
        eq(L"EventMapping") || eq(L"RadioButton") || eq(L"ComboBox") || eq(L"ListBox") ||
        eq(L"ListView") || eq(L"CheckBox") || eq(L"TextStyle") || eq(L"Billboard") ||
        eq(L"BBControl") || eq(L"UIText") || eq(L"ActionText") || eq(L"Error")) {
        return 3; // Dialogs & UI Controls
    }
    return 4; // Sequences, Locators & Custom Actions
}

struct PrismColumnMeta {
    std::wstring name;
    std::wstring typeSpec; // e.g. `s72`, `S0`, `i2`, `I4`, `v0`
    bool         isInteger = false;
    bool         isBinary  = false;
    bool         isNullable= false;
    bool         isPrimary = false;
};

// ================================================================================
// MODAL DYNAMIC ROW / SUMMARY-PROPERTY EDITOR DIALOG (`RunRowEditorModalDialog`)
//   Builds a clean, DPI-aware, Dark/Light/High-Contrast modal form dynamically for
//   any MSI SQL table (or `SummaryInformation` property), including file pickers
//   for `v0`/`V0` Binary stream columns!
// ================================================================================
struct RowEditorField {
    std::wstring label;
    std::wstring value;
    bool         readOnly = false;
    bool         isBinary = false;
    bool         isInt    = false;
};

struct RowEditorModalState {
    rsw::ui::RswTheme           theme{};
    std::wstring                windowTitle;
    std::vector<RowEditorField> fields;
    std::vector<HWND>           edits;
    std::vector<HWND>           browseBtns;
    HFONT                       hFontUi   = nullptr;
    HFONT                       hFontMono = nullptr;
    bool                        accepted  = false;

    ~RowEditorModalState() {
        if (hFontUi)   ::DeleteObject(hFontUi);
        if (hFontMono) ::DeleteObject(hFontMono);
    }

    static LRESULT CALLBACK DlgWndProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
        RowEditorModalState* st = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            st = static_cast<RowEditorModalState*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hDlg, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(st));
        } else {
            st = reinterpret_cast<RowEditorModalState*>(::GetWindowLongPtrW(hDlg, GWLP_USERDATA));
        }
        if (!st) return ::DefWindowProcW(hDlg, msg, wParam, lParam);

        switch (msg) {
            case WM_CREATE: {
                const UINT dpi = ::GetDpiForWindow(hDlg);
                auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, st->theme.textScalePercent); };
                st->hFontUi   = rsw::ui::CreateScaledFont(L"Segoe UI",      9, FW_NORMAL, dpi, st->theme.textScalePercent);
                st->hFontMono = rsw::ui::CreateScaledFont(L"Cascadia Mono", 9, FW_NORMAL, dpi, st->theme.textScalePercent);
                HINSTANCE hInst = ::GetModuleHandleW(nullptr);

                int y = S(14);
                for (std::size_t i = 0; i < st->fields.size(); ++i) {
                    const auto& f = st->fields[i];
                    HWND hLbl = ::CreateWindowExW(
                        0, L"STATIC", f.label.c_str(), WS_CHILD | WS_VISIBLE,
                        S(16), y + S(4), S(196), S(22), hDlg, nullptr, hInst, nullptr);
                    ::SendMessageW(hLbl, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFontUi), TRUE);

                    const int edW = f.isBinary ? S(286) : S(374);
                    DWORD edStyle = WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | (f.readOnly ? ES_READONLY : 0);
                    HWND hEd = ::CreateWindowExW(
                        WS_EX_CLIENTEDGE, L"EDIT", f.value.c_str(), edStyle,
                        S(218), y, edW, S(26), hDlg,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(4100 + static_cast<int>(i))), hInst, nullptr);
                    ::SendMessageW(hEd, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFontMono), TRUE);
                    st->edits.push_back(hEd);

                    if (f.isBinary && !f.readOnly) {
                        HWND hBr = ::CreateWindowExW(
                            0, L"BUTTON", L"B&rowse...", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                            S(510), y - S(1), S(82), S(28), hDlg,
                            reinterpret_cast<HMENU>(static_cast<INT_PTR>(4300 + static_cast<int>(i))), hInst, nullptr);
                        ::SendMessageW(hBr, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFontUi), TRUE);
                        rsw::ui::AttachAccessiblePushButtonSubclass(hBr, &st->theme, false, false);
                        st->browseBtns.push_back(hBr);
                    }
                    y += S(34);
                }

                HWND hOk = ::CreateWindowExW(
                    0, L"BUTTON", L"&Commit to Database", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    S(312), y + S(8), S(172), S(32), hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDOK)), hInst, nullptr);
                HWND hCancel = ::CreateWindowExW(
                    0, L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    S(492), y + S(8), S(100), S(32), hDlg, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDCANCEL)), hInst, nullptr);
                ::SendMessageW(hOk,     WM_SETFONT, reinterpret_cast<WPARAM>(st->hFontUi), TRUE);
                ::SendMessageW(hCancel, WM_SETFONT, reinterpret_cast<WPARAM>(st->hFontUi), TRUE);
                rsw::ui::AttachAccessiblePushButtonSubclass(hOk,     &st->theme, true,  false);
                rsw::ui::AttachAccessiblePushButtonSubclass(hCancel, &st->theme, false, false);
                rsw::ui::ApplyWindowChromeAndA11y(hDlg, st->theme);
                return 0;
            }
            case WM_ERASEBKGND: {
                RECT rc{};
                ::GetClientRect(hDlg, &rc);
                ::FillRect(reinterpret_cast<HDC>(wParam), &rc, st->theme.hbrWindow);
                return 1;
            }
            case WM_CTLCOLORSTATIC: {
                HDC dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, st->theme.textPrimary);
                ::SetBkColor(dc, st->theme.windowBg);
                return reinterpret_cast<LRESULT>(st->theme.hbrWindow);
            }
            case WM_CTLCOLOREDIT: {
                HDC dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, st->theme.textPrimary);
                ::SetBkColor(dc, st->theme.cardBg);
                return reinterpret_cast<LRESULT>(st->theme.hbrCard);
            }
            case WM_COMMAND: {
                const int id = LOWORD(wParam);
                if (id >= 4300 && id < 4300 + static_cast<int>(st->fields.size())) {
                    const std::size_t fIdx = static_cast<std::size_t>(id - 4300);
                    wchar_t buf[MAX_PATH * 2]{};
                    OPENFILENAMEW ofn{};
                    ofn.lStructSize = sizeof(ofn);
                    ofn.hwndOwner   = hDlg;
                    ofn.lpstrFilter = L"All Stream Files (*.*)\0*.*\0";
                    ofn.lpstrFile   = buf;
                    ofn.nMaxFile    = static_cast<DWORD>(std::size(buf));
                    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
                    if (::GetOpenFileNameW(&ofn)) {
                        ::SetWindowTextW(st->edits[fIdx], buf);
                    }
                    return 0;
                }
                if (id == IDOK) {
                    for (std::size_t i = 0; i < st->fields.size() && i < st->edits.size(); ++i) {
                        const int len = ::GetWindowTextLengthW(st->edits[i]);
                        std::wstring val(static_cast<std::size_t>(std::max(0, len)) + 1, L'\0');
                        if (len > 0) ::GetWindowTextW(st->edits[i], val.data(), len + 1);
                        val.resize(static_cast<std::size_t>(std::max(0, len)));
                        st->fields[i].value = std::move(val);
                    }
                    st->accepted = true;
                    ::DestroyWindow(hDlg);
                    return 0;
                }
                if (id == IDCANCEL) {
                    st->accepted = false;
                    ::DestroyWindow(hDlg);
                    return 0;
                }
                break;
            }
            case WM_CLOSE:
                st->accepted = false;
                ::DestroyWindow(hDlg);
                return 0;
        }
        return ::DefWindowProcW(hDlg, msg, wParam, lParam);
    }
};

bool RunRowEditorModalDialog(HWND hOwner, const rsw::ui::RswTheme& theme, const std::wstring& title, std::vector<RowEditorField>& ioFields) {
    static bool sRegistered = false;
    constexpr const wchar_t* kModalCls = L"RaggieSoftMsiPrismRowModal";
    HINSTANCE hInst = ::GetModuleHandleW(nullptr);
    if (!sRegistered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &RowEditorModalState::DlgWndProc;
        wc.hInstance     = hInst;
        wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kModalCls;
        ::RegisterClassExW(&wc);
        sRegistered = true;
    }

    RowEditorModalState st{};
    st.theme       = theme;
    st.windowTitle = title;
    st.fields      = ioFields;

    const UINT dpi = ::GetDpiForWindow(hOwner ? hOwner : ::GetDesktopWindow());
    auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };
    const int clientW = S(612);
    const int clientH = S(72 + static_cast<int>(ioFields.size()) * 34);
    RECT wRc{0, 0, clientW, std::min(S(680), clientH)};
    constexpr DWORD kStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    ::AdjustWindowRectExForDpi(&wRc, kStyle, FALSE, WS_EX_DLGMODALFRAME, dpi);
    const int wW = wRc.right - wRc.left;
    const int wH = wRc.bottom - wRc.top;

    RECT ownRc{0, 0, ::GetSystemMetrics(SM_CXSCREEN), ::GetSystemMetrics(SM_CYSCREEN)};
    if (hOwner) ::GetWindowRect(hOwner, &ownRc);
    const int posX = ownRc.left + std::max(0, ((ownRc.right - ownRc.left) - wW) / 2);
    const int posY = ownRc.top  + std::max(0, ((ownRc.bottom - ownRc.top) - wH) / 2);

    if (hOwner) ::EnableWindow(hOwner, FALSE);
    HWND hDlg = ::CreateWindowExW(
        WS_EX_DLGMODALFRAME, kModalCls, title.c_str(), kStyle,
        posX, posY, wW, wH, hOwner, nullptr, hInst, &st);
    if (!hDlg) {
        if (hOwner) ::EnableWindow(hOwner, TRUE);
        return false;
    }
    ::ShowWindow(hDlg, SW_SHOWNORMAL);
    ::UpdateWindow(hDlg);
    if (!st.edits.empty()) ::SetFocus(st.edits.front());

    MSG msg{};
    while (::IsWindow(hDlg) && ::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!::IsDialogMessageW(hDlg, &msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }
    if (hOwner) {
        ::EnableWindow(hOwner, TRUE);
        ::SetForegroundWindow(hOwner);
    }
    if (st.accepted) ioFields = std::move(st.fields);
    return st.accepted;
}

// ================================================================================
// HEADLESS CLI SQL MUTATION & JSON TABLE EXPORT HELPERS
// ================================================================================
bool ExecuteDirectMsiSqlOnFile(const std::filesystem::path& dbPath, const std::wstring& sql, std::wstring& outStatus) {
    rsw::ScopedMsiHandle hDb, hView;
    if (::MsiOpenDatabaseW(dbPath.wstring().c_str(), MSIDBOPEN_TRANSACT, hDb.Put()) != ERROR_SUCCESS) {
        outStatus = L"[ERROR] Could not open MSI/MSM database in read/write mode: " + dbPath.wstring();
        return false;
    }
    if (::MsiDatabaseOpenViewW(hDb.Get(), sql.c_str(), hView.Put()) != ERROR_SUCCESS ||
        ::MsiViewExecute(hView.Get(), 0) != ERROR_SUCCESS) {
        outStatus = L"[ERROR] MSI SQL syntax or constraint error executing: " + sql;
        return false;
    }
    if (::MsiDatabaseCommit(hDb.Get()) != ERROR_SUCCESS) {
        outStatus = L"[ERROR] MsiDatabaseCommit failed while saving changes.";
        return false;
    }
    outStatus = L"[OK] Committed SQL change to " + dbPath.filename().wstring() + L": " + sql;
    return true;
}

std::string EscapeJsonStr(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out.push_back(c); break;
        }
    }
    return out;
}

bool ExportMsiTableToJsonFile(const std::filesystem::path& dbPath, const std::wstring& tableName, const std::filesystem::path& outJson, std::wstring& outStatus) {
    rsw::ScopedMsiHandle hDb, hView;
    bool isPatch = false;
    if (!OpenAnyMsiOrPatchReadOnly(dbPath, hDb, isPatch)) {
        outStatus = L"[ERROR] Could not open database: " + dbPath.wstring();
        return false;
    }
    const std::wstring sql = L"SELECT * FROM `" + tableName + L"`";
    if (::MsiDatabaseOpenViewW(hDb.Get(), sql.c_str(), hView.Put()) != ERROR_SUCCESS ||
        ::MsiViewExecute(hView.Get(), 0) != ERROR_SUCCESS) {
        outStatus = L"[ERROR] Table '" + tableName + L"' was not found in database.";
        return false;
    }
    rsw::ScopedMsiHandle hNames, hTypes;
    ::MsiViewGetColumnInfo(hView.Get(), MSICOLINFO_NAMES, hNames.Put());
    ::MsiViewGetColumnInfo(hView.Get(), MSICOLINFO_TYPES, hTypes.Put());
    const UINT colCount = ::MsiRecordGetFieldCount(hNames.Get());
    std::vector<std::string> names(colCount + 1);
    std::vector<wchar_t>     kinds(colCount + 1, L's');
    for (UINT c = 1; c <= colCount; ++c) {
        wchar_t nb[128]{}, tb[32]{};
        DWORD cn = 128, ct = 32;
        ::MsiRecordGetStringW(hNames.Get(), c, nb, &cn);
        ::MsiRecordGetStringW(hTypes.Get(), c, tb, &ct);
        names[c] = rsw::WideToUtf8(nb);
        if (tb[0]) kinds[c] = tb[0];
    }

    std::error_code ec;
    if (!outJson.parent_path().empty()) std::filesystem::create_directories(outJson.parent_path(), ec);
    std::ofstream out(outJson, std::ios::binary | std::ios::trunc);
    if (!out) {
        outStatus = L"[ERROR] Could not write JSON file: " + outJson.wstring();
        return false;
    }

    out << "[\n";
    bool firstRow = true;
    int count = 0;
    rsw::ScopedMsiHandle hRec;
    while (::MsiViewFetch(hView.Get(), hRec.Put()) == ERROR_SUCCESS) {
        if (!firstRow) out << ",\n";
        firstRow = false;
        out << "  {";
        for (UINT c = 1; c <= colCount; ++c) {
            if (c > 1) out << ", ";
            out << "\"" << EscapeJsonStr(names[c]) << "\": ";
            if (::MsiRecordIsNull(hRec.Get(), c)) {
                out << "null";
            } else if (kinds[c] == L'i' || kinds[c] == L'I' || kinds[c] == L'j' || kinds[c] == L'J') {
                out << ::MsiRecordGetInteger(hRec.Get(), c);
            } else if (kinds[c] == L'v' || kinds[c] == L'V') {
                out << ::MsiRecordDataSize(hRec.Get(), c);
            } else {
                wchar_t sb[1024]{};
                DWORD cch = static_cast<DWORD>(std::size(sb));
                ::MsiRecordGetStringW(hRec.Get(), c, sb, &cch);
                out << "\"" << EscapeJsonStr(rsw::WideToUtf8(sb)) << "\"";
            }
        }
        out << "}";
        ++count;
    }
    out << "\n]\n";
    outStatus = L"[OK] Exported " + std::to_wstring(count) + L" row(s) from `" + tableName + L"` -> " + outJson.wstring();
    return true;
}

// ================================================================================
// MAIN `RaggieSoft MSI Prism` WINDOW (`MsiPrismWindow`)
// ================================================================================
class MsiPrismWindow {
public:
    rsw::ui::RswTheme                        theme{};
    HWND                                     hWnd        = nullptr;
    HFONT                                    hFontTitle  = nullptr;
    HFONT                                    hFontUi     = nullptr;
    HFONT                                    hFontMono   = nullptr;
    HIMAGELIST                               hTreeIcons  = nullptr;

    std::filesystem::path                    currentPath;
    rsw::ScopedMsiHandle                     hOpenDb;
    bool                                     isPatchPkg  = false;
    std::wstring                             activeTable = L"[SummaryInformation]";
    std::vector<PrismColumnMeta>             activeCols;
    std::vector<std::vector<std::wstring>>   activeRows;
    std::vector<std::wstring>                treeNodeNames;
    std::map<std::wstring, HTREEITEM>        tableItemMap;

    HWND hBtnOpen = nullptr, hBtnNew = nullptr, hBtnSaveCopy = nullptr;
    HWND hBtnAddRow = nullptr, hBtnEditRow = nullptr, hBtnDelRow = nullptr, hBtnExportJson = nullptr;
    HWND hBtnVerify = nullptr, hBtnGenMst = nullptr, hBtnGenMsp = nullptr;
    HWND hSqlLabel = nullptr, hSqlEdit = nullptr, hBtnSqlRun = nullptr;
    HWND hTableTree = nullptr, hRowGrid = nullptr, hStatusEdit = nullptr;

    ~MsiPrismWindow() {
        for (HFONT* f : {&hFontTitle, &hFontUi, &hFontMono}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
        if (hTreeIcons) {
            ::ImageList_Destroy(hTreeIcons);
            hTreeIcons = nullptr;
        }
    }

    void RefreshTreeThemeAndIcons() {
        if (!hTableTree) return;
        const UINT dpi = ::GetDpiForWindow(hWnd);
        HIMAGELIST newIcons = rsw::ui::CreateStudioTreeImageList(dpi, theme);
        TreeView_SetImageList(hTableTree, newIcons, TVSIL_NORMAL);
        if (hTreeIcons) {
            ::ImageList_Destroy(hTreeIcons);
        }
        hTreeIcons = newIcons;
        rsw::ui::ApplyDarkTreeViewTheme(hTableTree, theme, dpi);
        rsw::ui::ApplyDarkScrollbarTheme(hTableTree, theme);
    }

    void CreateControls() {
        HINSTANCE hInst = ::GetModuleHandleW(nullptr);
        auto mkBtn = [&](const wchar_t* txt, int id, bool primary = false) {
            HWND h = ::CreateWindowExW(
                0, L"BUTTON", txt, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
            rsw::ui::AttachAccessiblePushButtonSubclass(h, &theme, primary, false);
            return h;
        };

        hBtnOpen       = mkBtn(L"&Open...",        IDC_PRISM_OPEN, true);
        hBtnNew        = mkBtn(L"&New .MSI...",    IDC_PRISM_NEW);
        hBtnSaveCopy   = mkBtn(L"&Save As...",     IDC_PRISM_SAVE_COPY);
        hBtnAddRow     = mkBtn(L"+ &Add Row...",   IDC_PRISM_ADD_ROW);
        hBtnEditRow    = mkBtn(L"&Edit Row...",    IDC_PRISM_EDIT_ROW);
        hBtnDelRow     = mkBtn(L"&Delete Row",     IDC_PRISM_DEL_ROW);
        hBtnExportJson = mkBtn(L"Export &JSON...", IDC_PRISM_EXPORT_JSON);
        hBtnVerify     = mkBtn(L"&Verify ICE80",   IDC_PRISM_VERIFY);
        hBtnGenMst     = mkBtn(L"Diff -> .&MST...",IDC_PRISM_GEN_MST);
        hBtnGenMsp     = mkBtn(L"Diff -> .MS&P...",IDC_PRISM_GEN_MSP);

        hSqlLabel = ::CreateWindowExW(
            0, L"STATIC", L"MSI S&QL:", WS_CHILD | WS_VISIBLE,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PRISM_SQL_LABEL)), hInst, nullptr);
        hSqlEdit = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"SELECT * FROM `Property`",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PRISM_SQL_EDIT)), hInst, nullptr);
        hBtnSqlRun = mkBtn(L"E&xecute SQL", IDC_PRISM_SQL_RUN, true);

        hTableTree = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"Database Schema Tables",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PRISM_TABLETREE)), hInst, nullptr);

        hRowGrid = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, nullptr,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PRISM_ROWGRID)), hInst, nullptr);
        ListView_SetExtendedListViewStyle(hRowGrid, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        rsw::ui::AttachDarkAwareListViewSubclass(hRowGrid, &theme);

        hStatusEdit = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT",
            L"Open or create an .msi, .msm, or .msp database in RaggieSoft MSI Prism to inspect and edit SQL tables directly.",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PRISM_STATUS)), hInst, nullptr);

        if (HMENU hSys = ::GetSystemMenu(hWnd, FALSE)) {
            ::AppendMenuW(hSys, MF_SEPARATOR, 0, nullptr);
            ::AppendMenuW(hSys, MF_STRING, IDM_PRISM_SYS_ABOUT, L"&About RaggieSoft MSI Prism...");
        }

        RefreshTreeThemeAndIcons();
        rsw::ui::ApplyDarkScrollbarTheme(hStatusEdit, theme);
    }

    void LayoutChildren() {
        const UINT dpi = ::GetDpiForWindow(hWnd);
        for (HFONT* f : {&hFontTitle, &hFontUi, &hFontMono}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
        hFontTitle = rsw::ui::CreateScaledFont(L"Segoe UI",      13, FW_BOLD,   dpi, theme.textScalePercent);
        hFontUi    = rsw::ui::CreateScaledFont(L"Segoe UI",       9, FW_NORMAL, dpi, theme.textScalePercent);
        hFontMono  = rsw::ui::CreateScaledFont(L"Cascadia Mono",  9, FW_NORMAL, dpi, theme.textScalePercent);

        for (HWND h : {hBtnOpen, hBtnNew, hBtnSaveCopy, hBtnAddRow, hBtnEditRow, hBtnDelRow,
                       hBtnExportJson, hBtnVerify, hBtnGenMst, hBtnGenMsp, hSqlLabel, hBtnSqlRun, hTableTree, hRowGrid}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUi), TRUE);
        }
        for (HWND m : {hSqlEdit, hStatusEdit}) {
            if (m) ::SendMessageW(m, WM_SETFONT, reinterpret_cast<WPARAM>(hFontMono), TRUE);
        }

        RefreshTreeThemeAndIcons();
        ListView_SetBkColor(hRowGrid, theme.cardBg);
        ListView_SetTextBkColor(hRowGrid, theme.cardBg);
        ListView_SetTextColor(hRowGrid, theme.textPrimary);

        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(640, rc.right - rc.left);
        const int H = std::max<int>(380, rc.bottom - rc.top);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        const int tbY = S(60);
        int tx = S(12);
        auto placeBtn = [&](HWND h, int w) { ::MoveWindow(h, tx, tbY, S(w), S(29), TRUE); tx += S(w + 5); };
        placeBtn(hBtnOpen,       78);
        placeBtn(hBtnNew,        92);
        placeBtn(hBtnSaveCopy,   84);
        placeBtn(hBtnAddRow,     98);
        placeBtn(hBtnEditRow,    92);
        placeBtn(hBtnDelRow,     90);
        placeBtn(hBtnExportJson, 102);
        placeBtn(hBtnVerify,     100);
        placeBtn(hBtnGenMst,     112);
        placeBtn(hBtnGenMsp,     112);

        const int sqlY = S(96);
        ::MoveWindow(hSqlLabel,  S(12), sqlY + S(4), S(64), S(22), TRUE);
        ::MoveWindow(hSqlEdit,   S(78), sqlY, std::max(S(180), W - S(208)), S(27), TRUE);
        ::MoveWindow(hBtnSqlRun, W - S(122), sqlY - S(1), S(110), S(29), TRUE);

        const int topY    = S(132);
        const int statusH = S(84);
        const int mainH   = std::max(S(140), H - topY - statusH - S(18));
        const int leftW   = S(252);

        ::MoveWindow(hTableTree,  S(12), topY, leftW, mainH, TRUE);
        ::MoveWindow(hRowGrid,    S(22) + leftW, topY, W - leftW - S(34), mainH, TRUE);
        ::MoveWindow(hStatusEdit, S(12), topY + mainH + S(8), W - S(24), statusH, TRUE);
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    void ClearGridColumns() {
        ListView_DeleteAllItems(hRowGrid);
        while (ListView_DeleteColumn(hRowGrid, 0)) {}
    }

    HTREEITEM InsertSchemaNode(HTREEITEM hParent, const std::wstring& displayText, const std::wstring& tableId, rsw::ui::StudioTreeIcon icon) {
        const std::size_t idx = treeNodeNames.size();
        treeNodeNames.push_back(tableId);

        TVINSERTSTRUCTW tvi{};
        tvi.hParent             = hParent;
        tvi.hInsertAfter        = TVI_LAST;
        tvi.item.mask           = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
        tvi.item.pszText        = const_cast<LPWSTR>(displayText.c_str());
        tvi.item.iImage         = static_cast<int>(icon);
        tvi.item.iSelectedImage = static_cast<int>(icon);
        tvi.item.lParam         = static_cast<LPARAM>(idx);
        HTREEITEM hItem = TreeView_InsertItem(hTableTree, &tvi);
        if (!tableId.empty()) {
            std::wstring low = tableId;
            for (wchar_t& c : low) c = static_cast<wchar_t>(::towlower(c));
            tableItemMap[low] = hItem;
        }
        return hItem;
    }

    bool SelectTableByName(const wchar_t* name) {
        if (!name) return false;
        std::wstring low = name;
        for (wchar_t& c : low) c = static_cast<wchar_t>(::towlower(c));
        auto it = tableItemMap.find(low);
        if (it == tableItemMap.end()) return false;
        HTREEITEM hPar = TreeView_GetParent(hTableTree, it->second);
        if (hPar) TreeView_Expand(hTableTree, hPar, TVE_EXPAND);
        TreeView_SelectItem(hTableTree, it->second);
        TreeView_EnsureVisible(hTableTree, it->second);
        PopulateSelectedTable(name);
        return true;
    }

    void LoadDatabaseFile(const std::filesystem::path& path, const std::wstring& reselectTable = L"") {
        rsw::ScopedMsiHandle newDb;
        bool patchFlag = false;
        if (!OpenAnyMsiOrPatchReadOnly(path, newDb, patchFlag)) {
            ::SetWindowTextW(hStatusEdit, (L"[ERROR] Failed opening Windows Installer database: " + path.wstring()).c_str());
            return;
        }

        currentPath = path;
        hOpenDb     = std::move(newDb);
        isPatchPkg  = patchFlag;

        TreeView_DeleteAllItems(hTableTree);
        treeNodeNames.clear();
        tableItemMap.clear();

        const auto tables = QueryTableNames(hOpenDb.Get());
        std::vector<std::wstring> buckets[5];
        buckets[0].push_back(L"[SummaryInformation]");
        for (const auto& t : tables) {
            buckets[ClassifyMsiSqlTableCategory(t)].push_back(t);
        }

        const wchar_t* catTitles[5] = {
            L"Metadata & Streams",
            L"Core Packaging Tables",
            L"Registry, Shortcuts & Classes",
            L"Dialogs & UI Controls",
            L"Sequences, Locators & Actions"
        };

        const std::wstring rootLbl = path.filename().wstring() + L" (" + std::to_wstring(tables.size()) + L" SQL tables)";
        HTREEITEM hRoot = InsertSchemaNode(TVI_ROOT, rootLbl, L"", rsw::ui::StudioTreeIcon::ComputerRoot);

        for (int c = 0; c < 5; ++c) {
            if (buckets[c].empty()) continue;
            const std::wstring catLbl = std::wstring(catTitles[c]) + L" (" + std::to_wstring(buckets[c].size()) + L")";
            HTREEITEM hCat = InsertSchemaNode(hRoot, catLbl, L"", rsw::ui::StudioTreeIcon::CategoryFolder);
            for (const auto& tbl : buckets[c]) {
                const auto ico = (tbl == L"[SummaryInformation]")
                    ? rsw::ui::StudioTreeIcon::CertShield
                    : rsw::ui::StudioTreeIcon::SqlTable;
                InsertSchemaNode(hCat, tbl, tbl, ico);
            }
            TreeView_Expand(hTableTree, hCat, TVE_EXPAND);
        }
        TreeView_Expand(hTableTree, hRoot, TVE_EXPAND);

        if (!reselectTable.empty() && SelectTableByName(reselectTable.c_str())) {
            // Kept previous selection
        } else {
            SelectTableByName(L"[SummaryInformation]");
        }

        const std::wstring title = L"RaggieSoft MSI Prism v" + std::wstring(rsw::kVersionStringWide) + L" — " + path.filename().wstring();
        ::SetWindowTextW(hWnd, title.c_str());
        ::SetWindowTextW(
            hStatusEdit,
            (L"[OK] Opened " + std::wstring(isPatchPkg ? L"Patch (.msp, read-only)" : L"Database (.msi/.msm, read/write ready)") + L": " +
             path.wstring() + L" (" + std::to_wstring(tables.size()) + L" SQL tables — double-click any row to edit).").c_str());
    }

    void PopulateGridFromOpenView(MSIHANDLE hView, const std::vector<std::wstring>& pkNames) {
        ClearGridColumns();
        activeCols.clear();
        activeRows.clear();

        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto addCol = [&](int idx, const std::wstring& label, int widthPx) {
            LVCOLUMNW col{};
            col.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText = const_cast<LPWSTR>(label.c_str());
            col.cx      = rsw::ui::ScalePx(widthPx, dpi, theme.textScalePercent);
            col.iSubItem= idx;
            ListView_InsertColumn(hRowGrid, idx, &col);
        };

        rsw::ScopedMsiHandle hNames, hTypes;
        ::MsiViewGetColumnInfo(hView, MSICOLINFO_NAMES, hNames.Put());
        ::MsiViewGetColumnInfo(hView, MSICOLINFO_TYPES, hTypes.Put());
        const UINT colCount = ::MsiRecordGetFieldCount(hNames.Get());

        for (UINT c = 1; c <= colCount; ++c) {
            wchar_t cName[128]{}, cType[32]{};
            DWORD cch = 128, tcch = 32;
            ::MsiRecordGetStringW(hNames.Get(), c, cName, &cch);
            ::MsiRecordGetStringW(hTypes.Get(), c, cType, &tcch);

            PrismColumnMeta cm{};
            cm.name       = cName;
            cm.typeSpec   = cType;
            cm.isBinary   = (cType[0] == L'v' || cType[0] == L'V');
            cm.isInteger  = (cType[0] == L'i' || cType[0] == L'I' || cType[0] == L'j' || cType[0] == L'J');
            cm.isNullable = (cType[0] >= L'A' && cType[0] <= L'Z');
            for (const auto& pk : pkNames) {
                if (_wcsicmp(pk.c_str(), cm.name.c_str()) == 0) cm.isPrimary = true;
            }
            const std::wstring headerLbl = cm.isPrimary ? (cm.name + L" [PK]") : cm.name;
            addCol(static_cast<int>(c - 1), headerLbl, cm.isPrimary ? 185 : 165);
            activeCols.push_back(std::move(cm));
        }

        int rowIdx = 0;
        rsw::ScopedMsiHandle hRec;
        while (::MsiViewFetch(hView, hRec.Put()) == ERROR_SUCCESS && rowIdx < 5000) {
            std::vector<std::wstring> rawVals;
            rawVals.reserve(colCount);
            for (UINT c = 1; c <= colCount; ++c) {
                std::wstring cellText;
                std::wstring rawCell;
                if (::MsiRecordIsNull(hRec.Get(), c)) {
                    cellText = L"";
                    rawCell  = L"";
                } else if (activeCols[c - 1].isBinary) {
                    const unsigned int byteSz = ::MsiRecordDataSize(hRec.Get(), c);
                    cellText = L"[Binary Stream: " + std::to_wstring(byteSz) + L" bytes]";
                    rawCell  = cellText;
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
                    rawCell = cellText;
                }
                rawVals.push_back(std::move(rawCell));

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
            activeRows.push_back(std::move(rawVals));
            ++rowIdx;
        }
    }

    void PopulateSelectedTable(const std::wstring& tableName) {
        if (!hOpenDb || tableName.empty()) return;
        activeTable = tableName;

        if (tableName == L"[SummaryInformation]") {
            ClearGridColumns();
            activeCols.clear();
            activeRows.clear();
            const UINT dpi = ::GetDpiForWindow(hWnd);
            auto addCol = [&](int idx, const wchar_t* label, int widthPx) {
                LVCOLUMNW col{};
                col.mask    = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                col.pszText = const_cast<LPWSTR>(label);
                col.cx      = rsw::ui::ScalePx(widthPx, dpi, theme.textScalePercent);
                col.iSubItem= idx;
                ListView_InsertColumn(hRowGrid, idx, &col);
            };
            addCol(0, L"Property ID", 250);
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
                    activeRows.push_back({sp.name, val == L"(not set)" ? L"" : val});
                    ++rowIdx;
                }
            }
            return;
        }

        const std::wstring sql = L"SELECT * FROM `" + tableName + L"`";
        if (hSqlEdit) ::SetWindowTextW(hSqlEdit, sql.c_str());

        rsw::ScopedMsiHandle hView;
        if (::MsiDatabaseOpenViewW(hOpenDb.Get(), sql.c_str(), hView.Put()) != ERROR_SUCCESS ||
            ::MsiViewExecute(hView.Get(), 0) != ERROR_SUCCESS) {
            return;
        }
        const auto pkNames = QueryTablePrimaryKeys(hOpenDb.Get(), tableName);
        PopulateGridFromOpenView(hView.Get(), pkNames);
    }

    void ExecuteInteractiveSqlBar() {
        if (currentPath.empty()) return;
        const int len = ::GetWindowTextLengthW(hSqlEdit);
        if (len <= 0) return;
        std::wstring sql(static_cast<std::size_t>(len) + 1, L'\0');
        ::GetWindowTextW(hSqlEdit, sql.data(), len + 1);
        sql.resize(static_cast<std::size_t>(len));

        std::wstring trimmed = sql;
        while (!trimmed.empty() && (trimmed.front() == L' ' || trimmed.front() == L'\t')) trimmed.erase(trimmed.begin());
        if (_wcsnicmp(trimmed.c_str(), L"SELECT", 6) == 0) {
            rsw::ScopedMsiHandle hView;
            if (::MsiDatabaseOpenViewW(hOpenDb.Get(), trimmed.c_str(), hView.Put()) != ERROR_SUCCESS ||
                ::MsiViewExecute(hView.Get(), 0) != ERROR_SUCCESS) {
                ::SetWindowTextW(hStatusEdit, (L"[ERROR] Invalid SELECT query: " + trimmed).c_str());
                return;
            }
            PopulateGridFromOpenView(hView.Get(), {});
            ::SetWindowTextW(hStatusEdit, (L"[OK] Returned " + std::to_wstring(activeRows.size()) + L" row(s) for: " + trimmed).c_str());
            return;
        }

        if (isPatchPkg) {
            ::SetWindowTextW(hStatusEdit, L"[WARN] .MSP patch files are read-only. Open an .MSI or .MSM database to execute mutating SQL.");
            return;
        }

        // Release read-only handle so `MSIDBOPEN_TRANSACT` acquires exclusive write access cleanly!
        const std::wstring savedTbl = activeTable;
        hOpenDb = rsw::ScopedMsiHandle{};
        std::wstring status;
        const bool ok = ExecuteDirectMsiSqlOnFile(currentPath, trimmed, status);
        LoadDatabaseFile(currentPath, savedTbl);
        ::SetWindowTextW(hStatusEdit, status.c_str());
        (void)ok;
    }

    void CreateNewBlankMsiDatabase() {
        std::filesystem::path newPath;
        if (!PickFileDialog(true, L"Windows Installer Database (*.msi)\0*.msi\0Merge Module (*.msm)\0*.msm\0", L"msi", newPath)) {
            return;
        }
        hOpenDb = rsw::ScopedMsiHandle{};
        std::error_code ec;
        std::filesystem::remove(newPath, ec);
        {
            rsw::ScopedMsiHandle hNew, hSum, hView;
            if (::MsiOpenDatabaseW(newPath.wstring().c_str(), MSIDBOPEN_CREATEDIRECT, hNew.Put()) != ERROR_SUCCESS) {
                ::SetWindowTextW(hStatusEdit, (L"[ERROR] Failed creating database: " + newPath.wstring()).c_str());
                return;
            }
            if (::MsiGetSummaryInformationW(hNew.Get(), nullptr, 12, hSum.Put()) == ERROR_SUCCESS) {
                const auto hp = rsw::QueryHostPlatform();
                const std::wstring tmpl = (hp.nativeMachine == IMAGE_FILE_MACHINE_ARM64) ? L"Arm64;1033" : L"x64;1033";
                const std::wstring pkgCode = rsw::Utf8ToWide(rsw::GenerateRandomMsiGuid());
                ::MsiSummaryInfoSetPropertyW(hSum.Get(), 1,  VT_I2,    65001, nullptr, nullptr);
                ::MsiSummaryInfoSetPropertyW(hSum.Get(), 2,  VT_LPSTR, 0, nullptr, L"Installation Database");
                ::MsiSummaryInfoSetPropertyW(hSum.Get(), 3,  VT_LPSTR, 0, nullptr, L"New RaggieSoft MSI Package");
                ::MsiSummaryInfoSetPropertyW(hSum.Get(), 4,  VT_LPSTR, 0, nullptr, L"RaggieSoft");
                ::MsiSummaryInfoSetPropertyW(hSum.Get(), 7,  VT_LPSTR, 0, nullptr, tmpl.c_str());
                ::MsiSummaryInfoSetPropertyW(hSum.Get(), 9,  VT_LPSTR, 0, nullptr, pkgCode.c_str());
                ::MsiSummaryInfoSetPropertyW(hSum.Get(), 14, VT_I4,    500, nullptr, nullptr);
                ::MsiSummaryInfoSetPropertyW(hSum.Get(), 15, VT_I4,    2,   nullptr, nullptr);
                ::MsiSummaryInfoSetPropertyW(hSum.Get(), 18, VT_LPSTR, 0, nullptr, L"RaggieSoft MSI Prism");
                ::MsiSummaryInfoPersist(hSum.Get());
            }
            const wchar_t* seedTables[] = {
                L"CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL PRIMARY KEY `Property`)",
                L"CREATE TABLE `Directory` (`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL PRIMARY KEY `Directory`)",
                L"CREATE TABLE `Feature` (`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64), `Description` CHAR(255), `Display` SHORT, `Level` SHORT NOT NULL, `Directory_` CHAR(72), `Attributes` SHORT NOT NULL PRIMARY KEY `Feature`)",
                L"CREATE TABLE `Component` (`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, `Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72) PRIMARY KEY `Component`)",
                L"CREATE TABLE `Registry` (`Registry` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL, `Name` CHAR(255), `Value` CHAR(0), `Component_` CHAR(72) NOT NULL PRIMARY KEY `Registry`)"
            };
            for (const wchar_t* ddl : seedTables) {
                rsw::ScopedMsiHandle hv;
                if (::MsiDatabaseOpenViewW(hNew.Get(), ddl, hv.Put()) == ERROR_SUCCESS) ::MsiViewExecute(hv.Get(), 0);
            }
            ::MsiDatabaseCommit(hNew.Get());
        }
        LoadDatabaseFile(newPath, L"Property");
    }

    void EditOrAddCurrentTableRow(bool isNewRow) {
        if (currentPath.empty() || isPatchPkg) return;
        const int selIdx = ListView_GetNextItem(hRowGrid, -1, LVNI_SELECTED);

        // Case A: Editing `SummaryInformation` property!
        if (activeTable == L"[SummaryInformation]") {
            if (isNewRow || selIdx < 0 || static_cast<std::size_t>(selIdx) >= std::size(kKnownSummaryProps)) {
                ::SetWindowTextW(hStatusEdit, L"[INFO] Select a SummaryInformation property row first to edit its value.");
                return;
            }
            const auto& sp = kKnownSummaryProps[selIdx];
            std::vector<RowEditorField> fields = {
                {sp.name, activeRows[static_cast<std::size_t>(selIdx)][1], false, false, sp.isInteger}
            };
            if (!RunRowEditorModalDialog(hWnd, theme, L"Edit SummaryInformation Property", fields)) return;

            hOpenDb = rsw::ScopedMsiHandle{};
            {
                rsw::ScopedMsiHandle hDb, hSum;
                if (::MsiOpenDatabaseW(currentPath.wstring().c_str(), MSIDBOPEN_TRANSACT, hDb.Put()) == ERROR_SUCCESS &&
                    ::MsiGetSummaryInformationW(hDb.Get(), nullptr, 10, hSum.Put()) == ERROR_SUCCESS) {
                    if (sp.isInteger) {
                        const INT iv = static_cast<INT>(_wtoi(fields[0].value.c_str()));
                        ::MsiSummaryInfoSetPropertyW(hSum.Get(), sp.pid, sp.pid == 1 ? VT_I2 : VT_I4, iv, nullptr, nullptr);
                    } else {
                        ::MsiSummaryInfoSetPropertyW(hSum.Get(), sp.pid, VT_LPSTR, 0, nullptr, fields[0].value.c_str());
                    }
                    ::MsiSummaryInfoPersist(hSum.Get());
                    ::MsiDatabaseCommit(hDb.Get());
                }
            }
            LoadDatabaseFile(currentPath, L"[SummaryInformation]");
            return;
        }

        // Case B: Adding or editing a relational SQL table row!
        if (activeCols.empty() || activeTable.front() == L'_') {
            ::SetWindowTextW(hStatusEdit, L"[INFO] Select a user-editable MSI table (e.g. Property, Feature, Component, Registry) first.");
            return;
        }
        if (!isNewRow && (selIdx < 0 || static_cast<std::size_t>(selIdx) >= activeRows.size())) {
            ::SetWindowTextW(hStatusEdit, L"[INFO] Select a row in the table grid (or double-click a row) to edit.");
            return;
        }

        std::vector<RowEditorField> fields;
        for (std::size_t c = 0; c < activeCols.size(); ++c) {
            const auto& cm = activeCols[c];
            std::wstring lbl = cm.name + L" (" + cm.typeSpec + (cm.isPrimary ? L", PK)" : L")");
            std::wstring initVal = isNewRow ? L"" : activeRows[static_cast<std::size_t>(selIdx)][c];
            if (!isNewRow && cm.isBinary) initVal.clear(); // Leave blank unless replacing binary stream!
            fields.push_back({std::move(lbl), std::move(initVal), (!isNewRow && cm.isPrimary), cm.isBinary, cm.isInteger});
        }

        const std::wstring dlgTitle = (isNewRow ? L"Insert Row into `" : L"Edit Row in `") + activeTable + L"`";
        if (!RunRowEditorModalDialog(hWnd, theme, dlgTitle, fields)) return;

        const std::wstring savedTbl = activeTable;
        hOpenDb = rsw::ScopedMsiHandle{};
        bool okCommit = false;
        std::wstring errDetail;
        {
            rsw::ScopedMsiHandle hDb;
            if (::MsiOpenDatabaseW(currentPath.wstring().c_str(), MSIDBOPEN_TRANSACT, hDb.Put()) == ERROR_SUCCESS) {
                if (isNewRow) {
                    std::wstring colsSql, qMarks;
                    for (std::size_t c = 0; c < activeCols.size(); ++c) {
                        if (c > 0) { colsSql += L", "; qMarks += L", "; }
                        colsSql += L"`" + activeCols[c].name + L"`";
                        qMarks  += L"?";
                    }
                    const std::wstring insSql = L"INSERT INTO `" + savedTbl + L"` (" + colsSql + L") VALUES (" + qMarks + L")";
                    rsw::ScopedMsiHandle hView, hRec(::MsiCreateRecord(static_cast<UINT>(activeCols.size())));
                    for (std::size_t c = 0; c < activeCols.size(); ++c) {
                        const UINT fNo = static_cast<UINT>(c + 1);
                        const auto& val = fields[c].value;
                        if (val.empty() && activeCols[c].isNullable) {
                            ::MsiRecordSetInteger(hRec.Get(), fNo, MSI_NULL_INTEGER);
                        } else if (activeCols[c].isBinary) {
                            if (!val.empty()) ::MsiRecordSetStreamW(hRec.Get(), fNo, val.c_str());
                        } else if (activeCols[c].isInteger) {
                            ::MsiRecordSetInteger(hRec.Get(), fNo, _wtoi(val.c_str()));
                        } else {
                            ::MsiRecordSetStringW(hRec.Get(), fNo, val.c_str());
                        }
                    }
                    if (::MsiDatabaseOpenViewW(hDb.Get(), insSql.c_str(), hView.Put()) == ERROR_SUCCESS &&
                        ::MsiViewExecute(hView.Get(), hRec.Get()) == ERROR_SUCCESS &&
                        ::MsiDatabaseCommit(hDb.Get()) == ERROR_SUCCESS) {
                        okCommit = true;
                    } else {
                        errDetail = L"Failed inserting row (check primary key uniqueness and non-null columns).";
                    }
                } else {
                    // Build `SELECT * FROM Table WHERE PK1=? AND PK2=?` and update via `MsiViewModify(MSIMODIFY_UPDATE)`!
                    std::wstring whereSql;
                    std::vector<std::size_t> pkIndices;
                    for (std::size_t c = 0; c < activeCols.size(); ++c) {
                        if (activeCols[c].isPrimary) {
                            if (!whereSql.empty()) whereSql += L" AND ";
                            whereSql += L"`" + activeCols[c].name + L"`=?";
                            pkIndices.push_back(c);
                        }
                    }
                    const std::wstring selSql = L"SELECT * FROM `" + savedTbl + L"`" + (whereSql.empty() ? L"" : (L" WHERE " + whereSql));
                    rsw::ScopedMsiHandle hView, hPkRec(::MsiCreateRecord(static_cast<UINT>(std::max<std::size_t>(1, pkIndices.size()))));
                    for (std::size_t k = 0; k < pkIndices.size(); ++k) {
                        const std::size_t ci = pkIndices[k];
                        if (activeCols[ci].isInteger) ::MsiRecordSetInteger(hPkRec.Get(), static_cast<UINT>(k + 1), _wtoi(fields[ci].value.c_str()));
                        else ::MsiRecordSetStringW(hPkRec.Get(), static_cast<UINT>(k + 1), fields[ci].value.c_str());
                    }
                    rsw::ScopedMsiHandle hRow;
                    if (::MsiDatabaseOpenViewW(hDb.Get(), selSql.c_str(), hView.Put()) == ERROR_SUCCESS &&
                        ::MsiViewExecute(hView.Get(), pkIndices.empty() ? 0 : hPkRec.Get()) == ERROR_SUCCESS &&
                        ::MsiViewFetch(hView.Get(), hRow.Put()) == ERROR_SUCCESS) {
                        for (std::size_t c = 0; c < activeCols.size(); ++c) {
                            if (activeCols[c].isPrimary) continue;
                            const UINT fNo = static_cast<UINT>(c + 1);
                            const auto& val = fields[c].value;
                            if (activeCols[c].isBinary) {
                                if (!val.empty()) ::MsiRecordSetStreamW(hRow.Get(), fNo, val.c_str());
                            } else if (val.empty() && activeCols[c].isNullable) {
                                ::MsiRecordSetInteger(hRow.Get(), fNo, MSI_NULL_INTEGER);
                            } else if (activeCols[c].isInteger) {
                                ::MsiRecordSetInteger(hRow.Get(), fNo, _wtoi(val.c_str()));
                            } else {
                                ::MsiRecordSetStringW(hRow.Get(), fNo, val.c_str());
                            }
                        }
                        if (::MsiViewModify(hView.Get(), MSIMODIFY_UPDATE, hRow.Get()) == ERROR_SUCCESS &&
                            ::MsiDatabaseCommit(hDb.Get()) == ERROR_SUCCESS) {
                            okCommit = true;
                        }
                    }
                    if (!okCommit) errDetail = L"Failed updating row in `" + savedTbl + L"`.";
                }
            }
        }
        LoadDatabaseFile(currentPath, savedTbl);
        ::SetWindowTextW(hStatusEdit, okCommit
            ? (L"[OK] Committed row changes in `" + savedTbl + L"` to " + currentPath.filename().wstring()).c_str()
            : (L"[ERROR] " + errDetail).c_str());
    }

    void DeleteSelectedTableRow() {
        if (currentPath.empty() || isPatchPkg || activeTable == L"[SummaryInformation]") return;
        const int selIdx = ListView_GetNextItem(hRowGrid, -1, LVNI_SELECTED);
        if (selIdx < 0 || static_cast<std::size_t>(selIdx) >= activeRows.size()) return;

        std::wstring whereSql;
        std::vector<std::size_t> pkIndices;
        for (std::size_t c = 0; c < activeCols.size(); ++c) {
            if (activeCols[c].isPrimary) {
                if (!whereSql.empty()) whereSql += L" AND ";
                whereSql += L"`" + activeCols[c].name + L"`=?";
                pkIndices.push_back(c);
            }
        }
        if (pkIndices.empty()) return;

        const auto& rowVals = activeRows[static_cast<std::size_t>(selIdx)];
        const std::wstring savedTbl = activeTable;
        hOpenDb = rsw::ScopedMsiHandle{};
        bool okDel = false;
        {
            rsw::ScopedMsiHandle hDb, hView, hPk(::MsiCreateRecord(static_cast<UINT>(pkIndices.size())));
            if (::MsiOpenDatabaseW(currentPath.wstring().c_str(), MSIDBOPEN_TRANSACT, hDb.Put()) == ERROR_SUCCESS) {
                for (std::size_t k = 0; k < pkIndices.size(); ++k) {
                    const std::size_t ci = pkIndices[k];
                    if (activeCols[ci].isInteger) ::MsiRecordSetInteger(hPk.Get(), static_cast<UINT>(k + 1), _wtoi(rowVals[ci].c_str()));
                    else ::MsiRecordSetStringW(hPk.Get(), static_cast<UINT>(k + 1), rowVals[ci].c_str());
                }
                const std::wstring delSql = L"DELETE FROM `" + savedTbl + L"` WHERE " + whereSql;
                if (::MsiDatabaseOpenViewW(hDb.Get(), delSql.c_str(), hView.Put()) == ERROR_SUCCESS &&
                    ::MsiViewExecute(hView.Get(), hPk.Get()) == ERROR_SUCCESS &&
                    ::MsiDatabaseCommit(hDb.Get()) == ERROR_SUCCESS) {
                    okDel = true;
                }
            }
        }
        LoadDatabaseFile(currentPath, savedTbl);
        ::SetWindowTextW(hStatusEdit, okDel
            ? (L"[OK] Deleted selected row from `" + savedTbl + L"`.").c_str()
            : L"[ERROR] Failed deleting selected row.");
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
        ::DrawTextW(hdc, L"RaggieSoft MSI Prism  •  64-Bit Read/Write MSI, MSM, MST & MSP SQL Table Editor",
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
            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
            case WM_THEMECHANGED: {
                if (rsw::ui::IsLiveThemeBroadcast(msg, wParam, lParam)) {
                    self->theme.Refresh(RGB(14, 165, 233), RGB(251, 191, 36));
                    rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                    for (HWND sc : {self->hRowGrid, self->hStatusEdit}) {
                        rsw::ui::ApplyDarkScrollbarTheme(sc, self->theme);
                    }
                    self->LayoutChildren();
                    ::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
                    return 0;
                }
                break;
            }
            case WM_SYSCOMMAND: {
                if ((wParam & 0xFFF0u) == IDM_PRISM_SYS_ABOUT) {
                    const std::wstring head = L"RaggieSoft MSI Prism v" + std::wstring(rsw::kVersionStringWide);
                    const std::wstring body =
                        L"Zero-dependency static /MT 64-bit (x64 & ARM64) relational SQL table editor and validator for Windows Installer (.msi, .msm, .mst, .msp) databases.\n\n"
                        L"Fictitious Persons & Entities Disclaimer: All narrative characters, bands, universities, and corporations across RaggieSoft creative works are fictitious.";
                    ::TaskDialog(hWnd, ::GetModuleHandleW(nullptr), L"About RaggieSoft MSI Prism",
                                 head.c_str(), body.c_str(), TDCBF_OK_BUTTON, TD_INFORMATION_ICON, nullptr);
                    return 0;
                }
                break;
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
            case WM_CTLCOLORSTATIC: {
                HDC dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, self->theme.textPrimary);
                ::SetBkColor(dc, self->theme.windowBg);
                return reinterpret_cast<LRESULT>(self->theme.hbrWindow);
            }
            case WM_CTLCOLORLISTBOX:
            case WM_CTLCOLOREDIT: {
                HDC dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, self->theme.textPrimary);
                ::SetBkColor(dc, self->theme.cardBg);
                return reinterpret_cast<LRESULT>(self->theme.hbrCard);
            }
            case WM_NOTIFY: {
                const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
                if (!nm) break;
                if (nm->idFrom == IDC_PRISM_TABLETREE && nm->code == TVN_SELCHANGEDW) {
                    const auto* nmtv = reinterpret_cast<const NMTREEVIEWW*>(lParam);
                    const std::size_t idx = static_cast<std::size_t>(nmtv->itemNew.lParam);
                    if (idx < self->treeNodeNames.size() && !self->treeNodeNames[idx].empty()) {
                        self->PopulateSelectedTable(self->treeNodeNames[idx]);
                    }
                    return 0;
                }
                if (nm->idFrom == IDC_PRISM_ROWGRID && nm->code == NM_DBLCLK) {
                    self->EditOrAddCurrentTableRow(false);
                    return 0;
                }
                break;
            }
            case WM_COMMAND: {
                const int id = LOWORD(wParam);
                if (id == IDC_PRISM_OPEN) {
                    std::filesystem::path p;
                    if (self->PickFileDialog(false, L"Windows Installer (*.msi;*.msm;*.msp)\0*.msi;*.msm;*.msp\0All Files\0*.*\0", L"msi", p)) {
                        self->LoadDatabaseFile(p);
                    }
                    return 0;
                }
                if (id == IDC_PRISM_NEW) {
                    self->CreateNewBlankMsiDatabase();
                    return 0;
                }
                if (id == IDC_PRISM_SAVE_COPY && !self->currentPath.empty()) {
                    std::filesystem::path dst;
                    if (self->PickFileDialog(true, L"Windows Installer (*.msi;*.msm)\0*.msi;*.msm\0All Files\0*.*\0", L"msi", dst)) {
                        std::error_code ec;
                        if (std::filesystem::copy_file(self->currentPath, dst, std::filesystem::copy_options::overwrite_existing, ec)) {
                            self->LoadDatabaseFile(dst, self->activeTable);
                        }
                    }
                    return 0;
                }
                if (id == IDC_PRISM_ADD_ROW) {
                    self->EditOrAddCurrentTableRow(true);
                    return 0;
                }
                if (id == IDC_PRISM_EDIT_ROW) {
                    self->EditOrAddCurrentTableRow(false);
                    return 0;
                }
                if (id == IDC_PRISM_DEL_ROW) {
                    self->DeleteSelectedTableRow();
                    return 0;
                }
                if (id == IDC_PRISM_SQL_RUN) {
                    self->ExecuteInteractiveSqlBar();
                    return 0;
                }
                if (id == IDC_PRISM_EXPORT_JSON && !self->currentPath.empty() && self->activeTable != L"[SummaryInformation]") {
                    std::filesystem::path outJ;
                    if (self->PickFileDialog(true, L"JSON Table Export (*.json)\0*.json\0", L"json", outJ)) {
                        std::wstring st;
                        ExportMsiTableToJsonFile(self->currentPath, self->activeTable, outJ, st);
                        ::SetWindowTextW(self->hStatusEdit, st.c_str());
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

    // Headless CLI inspection, direct SQL execution, & JSON table export modes!
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
        if (args[i] == L"--exec-sql") {
            if (i + 2 >= args.size()) return 2;
            std::wstring status;
            return ExecuteDirectMsiSqlOnFile(args[i + 1], args[i + 2], status) ? 0 : 1;
        }
        if (args[i] == L"--export-table-json") {
            if (i + 3 >= args.size()) return 2;
            std::wstring status;
            return ExportMsiTableToJsonFile(args[i + 1], args[i + 2], args[i + 3], status) ? 0 : 1;
        }
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    rsw::ui::ScopedGdiPlusSession gdiPlusSession;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_LISTVIEW_CLASSES | ICC_TREEVIEW_CLASSES};
    ::InitCommonControlsEx(&icc);

    std::filesystem::path exportShotDir;
    std::filesystem::path initialDbFile;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == L"--export-screenshots" && i + 1 < args.size()) {
            exportShotDir = args[i + 1];
            rsw::ui::ForcedThemeOverride() = 1;
            ++i;
        } else if (std::filesystem::exists(args[i])) {
            initialDbFile = args[i];
        }
    }

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
    const int W = rsw::ui::ScalePx(1160, dpi, prism.theme.textScalePercent);
    const int H = rsw::ui::ScalePx(710,  dpi, prism.theme.textScalePercent);
    HWND hWnd = ::CreateWindowExW(
        0, kCls, L"RaggieSoft MSI Prism",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        CW_USEDEFAULT, CW_USEDEFAULT, W, H,
        nullptr, nullptr, hInstance, &prism);
    if (!hWnd) {
        ::CoUninitialize();
        return 1;
    }

    if (!initialDbFile.empty()) {
        prism.LoadDatabaseFile(initialDbFile);
    }

    ::ShowWindow(hWnd, nCmdShow);
    ::UpdateWindow(hWnd);

    if (!exportShotDir.empty()) {
        prism.SelectTableByName(L"Feature");
        const bool okDark = rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"msiprism-sql-inspector.png");

        // Torture-test live `WM_SETTINGCHANGE(L"ImmersiveColorSet")` hot-swapping on `Component`!
        prism.SelectTableByName(L"Component");
        rsw::ui::ForcedThemeOverride() = 0;
        ::SendMessageW(hWnd, WM_SETTINGCHANGE, 0, reinterpret_cast<LPARAM>(L"ImmersiveColorSet"));
        const bool okLight = rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"msiprism-sql-inspector-light.png");

        ::DestroyWindow(hWnd);
        ::CoUninitialize();
        return (okDark && okLight) ? 0 : 2;
    }

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
