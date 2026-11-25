// ================================================================================
// FILE: src/prism/MsiPrismMain.cpp
// PROJECT: RaggieSoft Installer Studio — MSI Prism ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `msiprism.exe` ("RaggieSoft MSI Prism")?
//     Legacy SDK database table viewers were built as 32-bit era utilities with
//     flat ungrouped table lists, no Dark Mode, no native ARM64 build, and no
//     Per-Monitor V2 DPI scaling.
//
//     `msiprism.exe` is a zero-dependency static `/MT` Win32 C++20 database
//     inspector (`x64` & `ARM64`) built right into **RaggieSoft Installer Studio**:
//       1. Opens any `.msi` (Installation Database), `.msm` (Merge Module), or
//          `.msp` (Patch Package via `MSIDBOPEN_READONLY + MSIDBOPEN_PATCHFILE`).
//       2. Organizes `SummaryInformation` plus every relational SQL table (`_Tables`)
//          into a categorized, 32-bit ARGB icon-rich `SysTreeView32` schema tree
//          (`Metadata & Streams`, `Core Packaging Tables`, `Registry, Shortcuts &
//          Classes`, `Dialogs & UI Controls`, and `Sequences, Locators & Actions`)
//          paired with a fast, Dark-Mode-aware `SysListView32` relational row grid!
//       3. Runs instant relational/ICE80 audits and generates `.mst` Transforms
//          or `.msp` Patch Packages by diffing two `.msi` files!
//       4. Supports `--inspect-summary <file>` and `--export-screenshots <dir> [db]`
//          for headless CI/CD & automated UI verification!
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
#include <iostream>
#include <map>
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
    IDC_PRISM_TABLETREE = 3010,
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

class MsiPrismWindow {
public:
    rsw::ui::RswTheme                   theme{};
    HWND                                hWnd        = nullptr;
    HFONT                               hFontTitle  = nullptr;
    HFONT                               hFontUi     = nullptr;
    HFONT                               hFontMono   = nullptr;
    HIMAGELIST                          hTreeIcons  = nullptr;

    std::filesystem::path               currentPath;
    rsw::ScopedMsiHandle                hOpenDb;
    bool                                isPatchPkg  = false;
    std::wstring                        activeTable = L"[SummaryInformation]";
    std::vector<std::wstring>           treeNodeNames;
    std::map<std::wstring, HTREEITEM>   tableItemMap;

    HWND hBtnOpen = nullptr, hBtnVerify = nullptr, hBtnGenMst = nullptr, hBtnGenMsp = nullptr;
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
        auto mkBtn = [&](const wchar_t* txt, int id) {
            HWND h = ::CreateWindowExW(
                0, L"BUTTON", txt, WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
            rsw::ui::AttachAccessiblePushButtonSubclass(h, &theme, id == IDC_PRISM_OPEN, false);
            return h;
        };

        hBtnOpen   = mkBtn(L"&Open .MSI / .MSM / .MSP...", IDC_PRISM_OPEN);
        hBtnVerify = mkBtn(L"&Verify ICE80 / Schema",       IDC_PRISM_VERIFY);
        hBtnGenMst = mkBtn(L"Diff -> .&MST Transform...",   IDC_PRISM_GEN_MST);
        hBtnGenMsp = mkBtn(L"Diff -> .MS&P Patch...",       IDC_PRISM_GEN_MSP);

        // Categorized SQL Schema `SysTreeView32` with 32-bit ARGB vector icons (`v0.4.2+`)
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
            WS_EX_CLIENTEDGE, L"EDIT", L"Open an .msi, .msm, or .msp database in MSI Prism to inspect its SummaryInformation and categorized SQL tables.",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_MULTILINE | ES_READONLY | WS_VSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PRISM_STATUS)), hInst, nullptr);

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

        for (HWND h : {hBtnOpen, hBtnVerify, hBtnGenMst, hBtnGenMsp, hTableTree, hRowGrid}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUi), TRUE);
        }
        if (hStatusEdit) ::SendMessageW(hStatusEdit, WM_SETFONT, reinterpret_cast<WPARAM>(hFontMono), TRUE);

        RefreshTreeThemeAndIcons();
        ListView_SetBkColor(hRowGrid, theme.cardBg);
        ListView_SetTextBkColor(hRowGrid, theme.cardBg);
        ListView_SetTextColor(hRowGrid, theme.textPrimary);

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
        const int statusH = S(92);
        const int mainH   = std::max(S(140), H - topY - statusH - S(20));
        const int leftW   = S(252);

        ::MoveWindow(hTableTree,  S(12), topY, leftW, mainH, TRUE);
        ::MoveWindow(hRowGrid,    S(24) + leftW, topY, W - leftW - S(36), mainH, TRUE);
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
        SelectTableByName(L"[SummaryInformation]");

        const std::wstring title = L"RaggieSoft MSI Prism v" + std::wstring(rsw::kVersionStringWide) + L" — " + path.filename().wstring();
        ::SetWindowTextW(hWnd, title.c_str());
        ::SetWindowTextW(
            hStatusEdit,
            (L"[OK] Opened " + std::wstring(isPatchPkg ? L"Patch (.msp)" : L"Database (.msi/.msm)") + L": " +
             path.wstring() + L" (" + std::to_wstring(tables.size()) + L" SQL tables grouped by schema category).").c_str());
    }

    void PopulateSelectedTable(const std::wstring& tableName) {
        if (!hOpenDb || tableName.empty()) return;
        activeTable = tableName;

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

        if (tableName == L"[SummaryInformation]") {
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

        const std::wstring sql = L"SELECT * FROM `" + tableName + L"`";
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
        ::DrawTextW(hdc, L"RaggieSoft MSI Prism  •  Native 64-Bit MSI, MSM, MST & MSP SQL Schema Inspector",
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
            case WM_NOTIFY: {
                const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
                if (nm && nm->idFrom == IDC_PRISM_TABLETREE && nm->code == TVN_SELCHANGEDW) {
                    const auto* nmtv = reinterpret_cast<const NMTREEVIEWW*>(lParam);
                    const std::size_t idx = static_cast<std::size_t>(nmtv->itemNew.lParam);
                    if (idx < self->treeNodeNames.size() && !self->treeNodeNames[idx].empty()) {
                        self->PopulateSelectedTable(self->treeNodeNames[idx]);
                    }
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
    const int W = rsw::ui::ScalePx(1100, dpi, prism.theme.textScalePercent);
    const int H = rsw::ui::ScalePx(690,  dpi, prism.theme.textScalePercent);
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
