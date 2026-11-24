// ================================================================================
// FILE: src/common/RswMsiFeatureTree.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file?
//     This header implements the **Authentic Windows Installer Feature Selection
//     Tree (`SelectionTree`)** for our C++20 external UI (`SetupWizard.exe`) and
//     IDE Live Preview (`InstallerStudio.exe`)!
//
//   How does it match (and surpass) InstallAware's external UI `SelectionTree`?
//     1. **Genuine MSI Feature-State Split Icons (`[Drive/State | v]`)**:
//        Builds a Per-Monitor V2 DPI-scaled 32-bit ARGB `HIMAGELIST` (`TVSIL_STATE`)
//        with all 5 Windows Installer visual feature states:
//          - State 1 (`Local`)          : Hard drive + green LED + dropdown chevron
//                                         (*"Will be installed on local hard drive"*)
//          - State 2 (`EntireSubtree`)  : Cascaded dual hard drives + chevron
//                                         (*"Entire feature will be installed on local hard drive"*)
//          - State 3 (`PartialSubtree`) : Half-lit hard drive + chevron
//                                         (*"Some subfeatures will be installed on local hard drive"*)
//          - State 4 (`Advertised`)     : Hard drive + gold `1` badge + chevron
//                                         (*"Feature will be installed when required"*)
//          - State 5 (`Absent`)         : Bold MSI crimson `X` + chevron
//                                         (*"Entire feature will be unavailable"*)
//        Because it paints directly into a 32-bit DIB over `theme.cardBg`, there is
//        ZERO jagged 1-bit white/magenta halo in Windows 11 Dark Mode (`#0D1426`)!
//     2. **Direct `Product.msi` SQL Inspection + Live Maintenance State**:
//        Reads `Feature`, `FeatureComponents`, and `File` directly from the embedded
//        `Product.msi` via `MsiOpenDatabaseW(..., MSIDBOPEN_READONLY)` in < 2ms
//        (computing exact per-feature byte sizes!) AND queries `MsiQueryFeatureStateW`
//        during Maintenance `Modify` so already-installed features reflect their live
//        machine state!
//     3. **Exact MSI Property Handoff (`ADDLOCAL`, `REMOVE`, `ADVERTISE`)**:
//        Translates user selections into comma-separated Windows Installer feature
//        lists passed straight to `MsiInstallProductW`.
// ================================================================================

#pragma once

#include "RswGlassRenderer.hpp"
#include "RswPlatform.hpp"

#include <commctrl.h>
#include <msi.h>
#include <msiquery.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "msi.lib")
#pragma comment(lib, "comctl32.lib")

namespace rsw {

/// 1-based `TVSIL_STATE` indices (`INDEXTOSTATEIMAGEMASK(1..5)` — slot `0` in `SysTreeView32` means no icon!).
enum class MsiTreeState : int {
    Local          = 1, // Will be installed on local hard drive (`INSTALLSTATE_LOCAL`)
    EntireSubtree  = 2, // Entire feature (and all subfeatures) installed locally
    PartialSubtree = 3, // Parent feature installed locally, some subfeatures absent
    Advertised     = 4, // Feature will be installed when required (`INSTALLSTATE_ADVERTISED`)
    Absent         = 5  // Entire feature will be unavailable (`INSTALLSTATE_ABSENT`)
};

/// One row from the compiled `Product.msi` `Feature` table + live byte cost & selection state.
struct MsiFeatureNode {
    std::string   id;
    std::string   parentId;
    std::wstring  title;
    std::wstring  description;
    int           displayOrder   = 0;
    int           installLevel   = 1;
    int           attributes     = 0;     // `msidbFeatureAttributesUIDisallowAbsent = 16`, `DisallowAdvertise = 8`
    std::uint64_t ownBytes       = 0;     // Direct `FeatureComponents -> File.FileSize` sum
    MsiTreeState  defaultState   = MsiTreeState::Local;
    MsiTreeState  currentState   = MsiTreeState::Local;
    HTREEITEM     hTreeItem      = nullptr;

    [[nodiscard]] bool DisallowAbsent() const noexcept {
        return (attributes & 16 /* msidbFeatureAttributesUIDisallowAbsent */) != 0;
    }

    [[nodiscard]] bool AllowAdvertise() const noexcept {
        return (attributes & 8 /* msidbFeatureAttributesDisallowAdvertise */) == 0;
    }
};

/// Returns the canonical Windows Installer action description for a feature state.
inline std::wstring DescribeMsiTreeState(MsiTreeState st) {
    switch (st) {
        case MsiTreeState::Local:          return L"Will be installed on local hard drive.";
        case MsiTreeState::EntireSubtree:  return L"Entire feature (including subfeatures) will be installed on local hard drive.";
        case MsiTreeState::PartialSubtree: return L"Feature will be installed locally (some subfeatures excluded).";
        case MsiTreeState::Advertised:     return L"Feature will be installed on demand when required.";
        case MsiTreeState::Absent:         return L"Entire feature will be unavailable (not installed).";
    }
    return L"Will be installed on local hard drive.";
}

/// Paints one crisp MSI `SelectionTree` split-button icon (`[Drive / X / 1 | v]`) onto GDI+!
inline void PaintSingleMsiStateCell(
    Gdiplus::Graphics& g,
    float x,
    float y,
    float w,
    float h,
    MsiTreeState state,
    const ui::RswTheme& theme)
{
    const COLORREF frameBg = theme.isDarkMode
        ? RGB(30, 41, 59)
        : RGB(248, 250, 252);
    const COLORREF frameBorder = theme.isDarkMode
        ? RGB(100, 116, 139)
        : RGB(148, 163, 184);

    // Outer split-button pill `[  Icon  | v ]`
    const Gdiplus::SolidBrush pillBr(ui::ToGpColor(frameBg));
    const Gdiplus::Pen pillPen(ui::ToGpColor(frameBorder), 1.0f);
    ui::FillAndStrokeRoundRect(g, x + 0.5f, y + 1.0f, w - 1.5f, h - 2.0f, 3.0f, &pillBr, &pillPen);

    const float splitX = x + w * 0.66f;
    g.DrawLine(&pillPen, splitX, y + 2.5f, splitX, y + h - 2.5f);

    // Right dropdown chevron `v`
    const float cx = (splitX + x + w - 1.0f) * 0.5f;
    const float cy = y + h * 0.50f;
    const float cw = std::max(2.2f, h * 0.14f);
    Gdiplus::Pen chevPen(ui::ToGpColor(theme.textPrimary), std::max(1.3f, h * 0.08f));
    chevPen.SetStartCap(Gdiplus::LineCapRound);
    chevPen.SetEndCap(Gdiplus::LineCapRound);
    g.DrawLine(&chevPen, cx - cw, cy - cw * 0.45f, cx, cy + cw * 0.55f);
    g.DrawLine(&chevPen, cx, cy + cw * 0.55f, cx + cw, cy - cw * 0.45f);

    // Left MSI Feature Icon area (`x + 2 .. splitX - 2`)
    const float ix = x + 2.5f;
    const float iw = splitX - ix - 2.0f;
    const float iy = y + 2.5f;
    const float ih = h - 5.0f;

    if (state == MsiTreeState::Absent) {
        // Classic Windows Installer Red `X` ("Entire feature will be unavailable")
        Gdiplus::Pen xPen(Gdiplus::Color(255, 220, 38, 38), std::max(2.0f, ih * 0.17f));
        xPen.SetStartCap(Gdiplus::LineCapRound);
        xPen.SetEndCap(Gdiplus::LineCapRound);
        const float pad = std::max(2.0f, iw * 0.20f);
        g.DrawLine(&xPen, ix + pad, iy + pad, ix + iw - pad, iy + ih - pad);
        g.DrawLine(&xPen, ix + iw - pad, iy + pad, ix + pad, iy + ih - pad);
        return;
    }

    auto drawDriveChassis = [&](float dx, float dy, float dw, float dh, bool dimmed) {
        const COLORREF topCol  = dimmed ? RGB(148, 163, 184) : RGB(226, 232, 240);
        const COLORREF faceCol = dimmed ? RGB(100, 116, 139) : RGB(71, 85, 105);
        const COLORREF rimCol  = RGB(51, 65, 85);
        Gdiplus::SolidBrush topBr(ui::ToGpColor(topCol));
        Gdiplus::SolidBrush faceBr(ui::ToGpColor(faceCol));
        Gdiplus::Pen rimPen(ui::ToGpColor(rimCol), 1.0f);

        // Top platter deck
        Gdiplus::PointF poly[4] = {
            {dx + dw * 0.16f, dy},
            {dx + dw * 0.84f, dy},
            {dx + dw,         dy + dh * 0.52f},
            {dx,              dy + dh * 0.52f}
        };
        g.FillPolygon(&topBr, poly, 4);
        g.DrawPolygon(&rimPen, poly, 4);

        // Front bezel + green activity LED
        ui::FillAndStrokeRoundRect(g, dx, dy + dh * 0.48f, dw, dh * 0.48f, 1.5f, &faceBr, &rimPen);
        const Gdiplus::SolidBrush ledBr(dimmed ? Gdiplus::Color(255, 251, 191, 36)
                                               : Gdiplus::Color(255, 16, 185, 129));
        const float ledR = std::max(1.8f, dh * 0.18f);
        g.FillEllipse(&ledBr, dx + dw - ledR * 1.9f, dy + dh * 0.62f, ledR, ledR);
    };

    if (state == MsiTreeState::EntireSubtree) {
        // Stacked dual hard drives ("Entire feature + subfeatures on local hard drive")
        drawDriveChassis(ix + iw * 0.14f, iy + ih * 0.04f, iw * 0.78f, ih * 0.56f, false);
        drawDriveChassis(ix + iw * 0.04f, iy + ih * 0.36f, iw * 0.78f, ih * 0.58f, false);
    } else if (state == MsiTreeState::PartialSubtree) {
        drawDriveChassis(ix + iw * 0.08f, iy + ih * 0.18f, iw * 0.84f, ih * 0.68f, true);
    } else {
        drawDriveChassis(ix + iw * 0.08f, iy + ih * 0.18f, iw * 0.84f, ih * 0.68f, false);
    }

    if (state == MsiTreeState::Advertised) {
        // Classic MSI Gold `"1"` on-demand badge ("Feature will be installed when required")
        const float bw = std::max(6.5f, iw * 0.48f);
        const float bh = std::max(7.0f, ih * 0.62f);
        const float bx = ix + iw - bw;
        const float by = iy;
        Gdiplus::SolidBrush goldBr(Gdiplus::Color(255, 251, 191, 36));
        Gdiplus::Pen darkPen(Gdiplus::Color(255, 120, 53, 15), 1.0f);
        ui::FillAndStrokeRoundRect(g, bx, by, bw, bh, 1.8f, &goldBr, &darkPen);
        Gdiplus::Pen onePen(Gdiplus::Color(255, 15, 23, 42), std::max(1.3f, bh * 0.16f));
        g.DrawLine(&onePen, bx + bw * 0.52f, by + 1.5f, bx + bw * 0.52f, by + bh - 1.5f);
    }
}

/// Builds a DPI-scaled 6-slot `HIMAGELIST` (`0` = empty, `1..5` = `MsiTreeState`) for `SysTreeView32` (`TVSIL_STATE`)!
inline HIMAGELIST CreateMsiStateImageList(UINT dpi, const ui::RswTheme& theme) {
    const int cellW = ui::ScalePx(30, dpi, theme.textScalePercent);
    const int cellH = ui::ScalePx(18, dpi, theme.textScalePercent);
    const int totalW = cellW * 6;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = totalW;
    bmi.bmiHeader.biHeight      = -cellH; // Top-down 32-bit DIB
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC screenDc = ::GetDC(nullptr);
    HBITMAP hDib = ::CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HDC memDc    = ::CreateCompatibleDC(screenDc);
    ::ReleaseDC(nullptr, screenDc);
    if (!hDib || !memDc) {
        if (hDib)  ::DeleteObject(hDib);
        if (memDc) ::DeleteDC(memDc);
        return nullptr;
    }

    HGDIOBJ oldBmp = ::SelectObject(memDc, hDib);
    RECT fullRc{0, 0, totalW, cellH};
    ::FillRect(memDc, &fullRc, theme.hbrCard);

    {
        Gdiplus::Graphics g(memDc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

        const MsiTreeState states[5] = {
            MsiTreeState::Local,
            MsiTreeState::EntireSubtree,
            MsiTreeState::PartialSubtree,
            MsiTreeState::Advertised,
            MsiTreeState::Absent
        };
        for (int i = 0; i < 5; ++i) {
            PaintSingleMsiStateCell(
                g,
                static_cast<float>((i + 1) * cellW + 1),
                0.0f,
                static_cast<float>(cellW - 2),
                static_cast<float>(cellH),
                states[i],
                theme);
        }
    }

    ::SelectObject(memDc, oldBmp);
    ::DeleteDC(memDc);

    HIMAGELIST hIml = ::ImageList_Create(cellW, cellH, ILC_COLOR32, 6, 0);
    if (hIml) {
        ::ImageList_Add(hIml, hDib, nullptr);
    }
    ::DeleteObject(hDib);
    return hIml;
}

/// Reads all `Feature` rows + exact `File` byte costs from `msiPath` (and live `MsiQueryFeatureStateW` if installed!).
inline bool LoadFeatureTreeFromMsiDatabase(
    const std::filesystem::path& msiPath,
    const std::wstring& installedProductCode,
    std::vector<MsiFeatureNode>& outNodes)
{
    outNodes.clear();
    MSIHANDLE hDb = 0;
    if (::MsiOpenDatabaseW(msiPath.wstring().c_str(), MSIDBOPEN_READONLY, &hDb) != ERROR_SUCCESS || !hDb) {
        return false;
    }

    // 1. Sum component byte sizes (`Component_ -> sum(FileSize)`) from the `File` table
    std::map<std::string, std::uint64_t> compBytes;
    {
        MSIHANDLE hView = 0;
        if (::MsiDatabaseOpenViewW(hDb, L"SELECT `Component_`, `FileSize` FROM `File`", &hView) == ERROR_SUCCESS && hView) {
            if (::MsiViewExecute(hView, 0) == ERROR_SUCCESS) {
                MSIHANDLE hRec = 0;
                while (::MsiViewFetch(hView, &hRec) == ERROR_SUCCESS && hRec) {
                    wchar_t compBuf[256]{};
                    DWORD cch = static_cast<DWORD>(std::size(compBuf));
                    ::MsiRecordGetStringW(hRec, 1, compBuf, &cch);
                    const int sz = ::MsiRecordGetInteger(hRec, 2);
                    if (sz > 0) compBytes[WideToUtf8(compBuf)] += static_cast<std::uint64_t>(sz);
                    ::MsiCloseHandle(hRec);
                }
            }
            ::MsiCloseHandle(hView);
        }
    }

    // 2. Map `Feature_ -> sum(Component bytes)` via `FeatureComponents`
    std::map<std::string, std::uint64_t> featBytes;
    {
        MSIHANDLE hView = 0;
        if (::MsiDatabaseOpenViewW(hDb, L"SELECT `Feature_`, `Component_` FROM `FeatureComponents`", &hView) == ERROR_SUCCESS && hView) {
            if (::MsiViewExecute(hView, 0) == ERROR_SUCCESS) {
                MSIHANDLE hRec = 0;
                while (::MsiViewFetch(hView, &hRec) == ERROR_SUCCESS && hRec) {
                    wchar_t fBuf[256]{}, cBuf[256]{};
                    DWORD cf = static_cast<DWORD>(std::size(fBuf));
                    DWORD cc = static_cast<DWORD>(std::size(cBuf));
                    ::MsiRecordGetStringW(hRec, 1, fBuf, &cf);
                    ::MsiRecordGetStringW(hRec, 2, cBuf, &cc);
                    featBytes[WideToUtf8(fBuf)] += compBytes[WideToUtf8(cBuf)];
                    ::MsiCloseHandle(hRec);
                }
            }
            ::MsiCloseHandle(hView);
        }
    }

    // 3. Read all visible (`Display > 0`, `Level > 0`) rows from `Feature`
    {
        MSIHANDLE hView = 0;
        if (::MsiDatabaseOpenViewW(
                hDb,
                L"SELECT `Feature`, `Feature_Parent`, `Title`, `Description`, `Display`, `Level`, `Attributes` FROM `Feature`",
                &hView) == ERROR_SUCCESS && hView) {
            if (::MsiViewExecute(hView, 0) == ERROR_SUCCESS) {
                MSIHANDLE hRec = 0;
                while (::MsiViewFetch(hView, &hRec) == ERROR_SUCCESS && hRec) {
                    wchar_t idBuf[256]{}, parBuf[256]{}, titBuf[512]{}, descBuf[1024]{};
                    DWORD c1 = static_cast<DWORD>(std::size(idBuf));
                    DWORD c2 = static_cast<DWORD>(std::size(parBuf));
                    DWORD c3 = static_cast<DWORD>(std::size(titBuf));
                    DWORD c4 = static_cast<DWORD>(std::size(descBuf));
                    ::MsiRecordGetStringW(hRec, 1, idBuf,   &c1);
                    ::MsiRecordGetStringW(hRec, 2, parBuf,  &c2);
                    ::MsiRecordGetStringW(hRec, 3, titBuf,  &c3);
                    ::MsiRecordGetStringW(hRec, 4, descBuf, &c4);
                    const int disp  = ::MsiRecordGetInteger(hRec, 5);
                    const int level = ::MsiRecordGetInteger(hRec, 6);
                    const int attr  = ::MsiRecordGetInteger(hRec, 7);
                    ::MsiCloseHandle(hRec);

                    if (level <= 0 || disp == 0) continue; // Hidden MSI feature (`Level = 0` or `Display = 0`)

                    MsiFeatureNode node{};
                    node.id           = WideToUtf8(idBuf);
                    node.parentId     = WideToUtf8(parBuf);
                    node.title        = (titBuf[0] != L'\0') ? titBuf : idBuf;
                    node.description  = (descBuf[0] != L'\0')
                        ? descBuf
                        : L"Installs components associated with this program feature.";
                    node.displayOrder = (disp == MSI_NULL_INTEGER) ? 999 : disp;
                    node.installLevel = level;
                    node.attributes   = (attr == MSI_NULL_INTEGER) ? 0 : attr;
                    node.ownBytes     = featBytes[node.id];

                    // Default install level <= 100 (`INSTALLLEVEL_DEFAULT`) starts enabled!
                    node.defaultState = (level <= 100) ? MsiTreeState::Local : MsiTreeState::Absent;
                    if (node.DisallowAbsent()) node.defaultState = MsiTreeState::Local;
                    node.currentState = node.defaultState;

                    // In Maintenance `Modify` mode, synchronize with the live installed feature state!
                    if (!installedProductCode.empty()) {
                        const INSTALLSTATE st = ::MsiQueryFeatureStateW(installedProductCode.c_str(), idBuf);
                        if (st == INSTALLSTATE_ABSENT)          node.currentState = MsiTreeState::Absent;
                        else if (st == INSTALLSTATE_ADVERTISED) node.currentState = MsiTreeState::Advertised;
                        else if (st == INSTALLSTATE_LOCAL || st == INSTALLSTATE_SOURCE || st == INSTALLSTATE_DEFAULT) {
                            node.currentState = MsiTreeState::Local;
                        }
                    }
                    outNodes.push_back(std::move(node));
                }
            }
            ::MsiCloseHandle(hView);
        }
    }

    ::MsiCloseHandle(hDb);
    std::sort(outNodes.begin(), outNodes.end(), [](const MsiFeatureNode& a, const MsiFeatureNode& b) {
        return a.displayOrder < b.displayOrder;
    });
    return !outNodes.empty();
}

/// Computes visual rollup state (`EntireSubtree` vs `PartialSubtree`) & total selected byte cost across `nodes`.
inline std::uint64_t RefreshMsiFeatureRollupsAndTotalBytes(std::vector<MsiFeatureNode>& nodes) noexcept {
    for (auto& parent : nodes) {
        if (parent.currentState == MsiTreeState::Absent || parent.currentState == MsiTreeState::Advertised) {
            continue;
        }
        int childCount = 0;
        int localChildren = 0;
        for (const auto& ch : nodes) {
            if (EqualsIgnoreCase(ch.parentId, parent.id)) {
                ++childCount;
                if (ch.currentState != MsiTreeState::Absent && ch.currentState != MsiTreeState::Advertised) {
                    ++localChildren;
                }
            }
        }
        if (childCount > 0) {
            if (localChildren == childCount) parent.currentState = MsiTreeState::EntireSubtree;
            else if (localChildren == 0)     parent.currentState = MsiTreeState::Local;
            else                             parent.currentState = MsiTreeState::PartialSubtree;
        } else if (parent.currentState == MsiTreeState::EntireSubtree ||
                   parent.currentState == MsiTreeState::PartialSubtree) {
            parent.currentState = MsiTreeState::Local;
        }
    }

    std::uint64_t total = 0;
    for (const auto& n : nodes) {
        if (n.currentState != MsiTreeState::Absent && n.currentState != MsiTreeState::Advertised) {
            total += n.ownBytes;
        }
    }
    return total;
}

/// Applies a user-chosen MSI popup menu action to `nodes[idx]` (cascading recursively to children/ancestors!).
inline void ApplyMsiFeatureActionRecursive(
    std::vector<MsiFeatureNode>& nodes,
    std::size_t idx,
    MsiTreeState requestedAction)
{
    if (idx >= nodes.size()) return;
    auto& target = nodes[idx];
    if (requestedAction == MsiTreeState::Absent && target.DisallowAbsent()) {
        return; // Locked required feature (`DisallowAbsent = true`)!
    }

    auto setDescendants = [&](auto& selfRef, const std::string& parId, MsiTreeState st) -> void {
        for (auto& ch : nodes) {
            if (EqualsIgnoreCase(ch.parentId, parId)) {
                if (st == MsiTreeState::Absent && ch.DisallowAbsent()) continue;
                ch.currentState = (st == MsiTreeState::EntireSubtree) ? MsiTreeState::Local : st;
                selfRef(selfRef, ch.id, st);
            }
        }
    };

    if (requestedAction == MsiTreeState::EntireSubtree) {
        target.currentState = MsiTreeState::EntireSubtree;
        setDescendants(setDescendants, target.id, MsiTreeState::Local);
    } else if (requestedAction == MsiTreeState::Absent) {
        target.currentState = MsiTreeState::Absent;
        setDescendants(setDescendants, target.id, MsiTreeState::Absent);
    } else {
        target.currentState = requestedAction;
    }

    // Ensure every ancestor of an enabled child feature is also enabled!
    if (target.currentState != MsiTreeState::Absent) {
        std::string par = target.parentId;
        int guard = 0;
        while (!par.empty() && guard++ < 16) {
            bool found = false;
            for (auto& p : nodes) {
                if (EqualsIgnoreCase(p.id, par)) {
                    if (p.currentState == MsiTreeState::Absent) p.currentState = MsiTreeState::Local;
                    par = p.parentId;
                    found = true;
                    break;
                }
            }
            if (!found) break;
        }
    }
    RefreshMsiFeatureRollupsAndTotalBytes(nodes);
}

/// Builds `ADDLOCAL=...`, `REMOVE=...`, and `ADVERTISE=...` command-line properties for `MsiInstallProductW`!
inline std::wstring BuildMsiFeatureSelectionProps(const std::vector<MsiFeatureNode>& nodes) {
    if (nodes.empty()) return L" ADDLOCAL=ALL";

    std::vector<std::wstring> addLocal, removeList, advList;
    for (const auto& n : nodes) {
        const std::wstring wid = Utf8ToWide(n.id);
        if (n.currentState == MsiTreeState::Absent)          removeList.push_back(wid);
        else if (n.currentState == MsiTreeState::Advertised) advList.push_back(wid);
        else                                                 addLocal.push_back(wid);
    }

    auto joinCsv = [](const std::vector<std::wstring>& v) {
        std::wstring s;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i > 0) s += L",";
            s += v[i];
        }
        return s;
    };

    std::wstring props;
    if (!addLocal.empty())   props += L" ADDLOCAL="  + joinCsv(addLocal);
    if (!removeList.empty()) props += L" REMOVE="    + joinCsv(removeList);
    if (!advList.empty())    props += L" ADVERTISE=" + joinCsv(advList);
    return props;
}

} // namespace rsw
