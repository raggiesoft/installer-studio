// ================================================================================
// FILE: src/common/RswWizardStyles.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file?
//     Version `0.5.0` introduces selectable first-party **Wizard Styles** spanning
//     every era of Windows software installation—from classic 1995 full-screen
//     Cobalt-Blue (`#000080`) and Microsoft ACME Setup Engine Teal (`#008080`)
//     backdrops with floating `#C0C0C0` 3D beveled dialogs, through `Wizard97` and
//     `AeroWizard`, all the way to modern Windows 11 `FluentMica` step-rail,
//     `OneClickSplash`, `PassiveToast`, and `Autorun16x9` launchers!
//
//     It also defines **Author-Defined Setup Types** (`Typical`, `Compact`,
//     `Complete`, `Custom`, & custom `[SetupType.<Id>]` blocks) and **Installation
//     Billboards** (`[Billboard.<Id>]`) shared identically between:
//       - `SetupWizard.exe` (`src/bootstrapper/RswSetupMain.cpp`) at runtime, AND
//       - `InstallerStudio.exe` (`src/ide/RswIdeMain.cpp`) in the live GDI+ Designer!
//
//   Non-Negotiable Accessibility Rule (`SPI_GETHIGHCONTRAST`):
//     Whenever a user enables a Windows Contrast Theme (*Aquatic*, *Desert*, *Dusk*,
//     *Night Sky*), `theme.isHighContrast` is `true` and EVERY wizard style—including
//     `Wizard95`, `FullScreenBlue`, and `FullScreenTeal`—automatically bypasses
//     custom retro colors/gradients to render pure semantic OS system colors!
// ================================================================================

#pragma once

#include "RswGlassRenderer.hpp"
#include "RswPlatform.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace rsw {

/// All selectable customer-facing `SetupWizard.exe` layout variations (`[UI] WizardLayout`).
enum class WizardStyleKind : int {
    StandardGlass  = 0,  // `standard` — Signature RaggieSoft Glass header + 64x64 medallion
    Wizard95       = 1,  // `wizard95` — Classic 1995 `#C0C0C0` 3D beveled dialog + 164px left sidebar
    Wizard97       = 2,  // `wizard97` — Classic enterprise top-banner sheet + etched rules
    AeroWizard     = 3,  // `aerowizard` — Command-bar header with 30x30 circular `<-` Back glyph
    FluentMica     = 4,  // `fluentmica` — Windows 11 Mica surface + 168px left vertical step rail
    OneClick       = 5,  // `oneclick` — Compact single-card express splash with "Customize..." toggle
    PassiveToast   = 6,  // `passivetoast` — Non-interactive `/passive` & `/qb` progress banner
    FullScreenBlue = 7,  // `fullscreenblue` — 1995 `#000080 -> #000000` full-screen wash + 3D dialog
    FullScreenTeal = 8,  // `fullscreenteal` — 1995 `#008080` ACME Setup Engine teal wash + 3D dialog
    Autorun16x9    = 9,  // `autorun16x9` — Widescreen 640x480 16:9 media/book launcher
    Simple         = 10  // `simple` — Ultra-compact single-dialog UI (`Destination folder` + `[...]` + progress + `[&Install]`/`[&Extract]`)
};

inline constexpr int kWizardStyleCount = 11;

/// Canonical `.rsip` manifest token for each `WizardStyleKind` (in exact IDE combo-box order).
inline constexpr const char* WizardStyleToken(WizardStyleKind k) noexcept {
    switch (k) {
        case WizardStyleKind::StandardGlass:  return "standard";
        case WizardStyleKind::Wizard95:       return "wizard95";
        case WizardStyleKind::Wizard97:       return "wizard97";
        case WizardStyleKind::AeroWizard:     return "aerowizard";
        case WizardStyleKind::FluentMica:     return "fluentmica";
        case WizardStyleKind::OneClick:       return "oneclick";
        case WizardStyleKind::PassiveToast:   return "passivetoast";
        case WizardStyleKind::FullScreenBlue: return "fullscreenblue";
        case WizardStyleKind::FullScreenTeal: return "fullscreenteal";
        case WizardStyleKind::Autorun16x9:    return "autorun16x9";
        case WizardStyleKind::Simple:         return "simple";
    }
    return "standard";
}

/// Human-friendly IDE dropdown label for each `WizardStyleKind`.
inline constexpr const wchar_t* WizardStyleDisplayLabel(WizardStyleKind k) noexcept {
    switch (k) {
        case WizardStyleKind::StandardGlass:  return L"standard (RaggieSoft Glass)";
        case WizardStyleKind::Wizard95:       return L"wizard95 (Classic 1995 3D Beveled)";
        case WizardStyleKind::Wizard97:       return L"wizard97 (Classic Top-Banner Sheet)";
        case WizardStyleKind::AeroWizard:     return L"aerowizard (Command Bar + <- Back)";
        case WizardStyleKind::FluentMica:     return L"fluentmica (Windows 11 Step Rail)";
        case WizardStyleKind::OneClick:       return L"oneclick (Compact 1-Click Splash)";
        case WizardStyleKind::PassiveToast:   return L"passivetoast (/passive Progress)";
        case WizardStyleKind::FullScreenBlue: return L"fullscreenblue (1995 Cobalt Wash)";
        case WizardStyleKind::FullScreenTeal: return L"fullscreenteal (1995 ACME #008080 Teal)";
        case WizardStyleKind::Autorun16x9:    return L"autorun16x9 (16:9 Media Autorun)";
        case WizardStyleKind::Simple:         return L"simple (Ultra-Compact Single-Dialog Setup / SFX)";
    }
    return L"standard (RaggieSoft Glass)";
}

/// Parses a case-insensitive `[UI] WizardLayout` value into `WizardStyleKind`.
inline WizardStyleKind ParseWizardStyleKind(std::string_view raw) noexcept {
    const std::string s = AsciiLower(TrimAscii(raw));
    if (s == "wizard95" || s == "win95")                 return WizardStyleKind::Wizard95;
    if (s == "wizard97" || s == "win97")                 return WizardStyleKind::Wizard97;
    if (s == "aerowizard" || s == "aero")                return WizardStyleKind::AeroWizard;
    if (s == "fluentmica" || s == "fluent" || s == "mica") return WizardStyleKind::FluentMica;
    if (s == "oneclick" || s == "oneclicksplash")        return WizardStyleKind::OneClick;
    if (s == "passivetoast" || s == "passive")           return WizardStyleKind::PassiveToast;
    if (s == "fullscreenblue" || s == "bluewash")        return WizardStyleKind::FullScreenBlue;
    if (s == "fullscreenteal" || s == "acme" || s == "tealwash") return WizardStyleKind::FullScreenTeal;
    if (s == "autorun16x9" || s == "autorun")            return WizardStyleKind::Autorun16x9;
    if (s == "simple" || s == "compact" || s == "minimal" || s == "sfx") return WizardStyleKind::Simple;
    return WizardStyleKind::StandardGlass;
}

/// Returns `true` when the floating dialog uses classic Windows 95 `#C0C0C0` 3D beveled chrome!
inline constexpr bool UsesClassic3dDialogSheet(WizardStyleKind k) noexcept {
    return k == WizardStyleKind::Wizard95 ||
           k == WizardStyleKind::FullScreenBlue ||
           k == WizardStyleKind::FullScreenTeal;
}

/// Returns `true` when `SetupWizard.exe` opens a full-monitor retro backdrop behind the setup dialog!
inline constexpr bool UsesFullScreenRetroBackdrop(WizardStyleKind k) noexcept {
    return k == WizardStyleKind::FullScreenBlue ||
           k == WizardStyleKind::FullScreenTeal;
}

/// Parses a string glyph name (`"Bolt"`, `"Cube"`, `"Blocks"`, `"Sliders"`, etc.) into `ui::GlassGlyph`.
inline ui::GlassGlyph ParseSetupGlyphName(std::string_view raw, ui::GlassGlyph fallback) noexcept {
    const std::string s = AsciiLower(TrimAscii(raw));
    if (s == "sparkle")     return ui::GlassGlyph::Sparkle;
    if (s == "shieldcheck") return ui::GlassGlyph::ShieldCheck;
    if (s == "bolt")        return ui::GlassGlyph::Bolt;
    if (s == "cube")        return ui::GlassGlyph::Cube;
    if (s == "sliders")     return ui::GlassGlyph::Sliders;
    if (s == "blocks")      return ui::GlassGlyph::Blocks;
    if (s == "folder")      return ui::GlassGlyph::Folder;
    if (s == "pingrid")     return ui::GlassGlyph::PinGrid;
    if (s == "play")        return ui::GlassGlyph::Play;
    if (s == "gear")        return ui::GlassGlyph::Gear;
    if (s == "reinstall")   return ui::GlassGlyph::Reinstall;
    if (s == "uninstall")   return ui::GlassGlyph::Uninstall;
    if (s == "readmebook")  return ui::GlassGlyph::ReadMeBook;
    return fallback;
}

/// Describes one selectable Setup Type option card (`Typical`, `Compact`, `Complete`, `Custom`, or `[SetupType.<Id>]`).
struct SetupTypeOptionSpec {
    std::string    id;
    std::wstring   titleMnemonic;       // Must contain `&` mnemonic, e.g. `L"&Typical (Recommended)"`
    std::wstring   description;
    std::string    tier = "typical";    // `"minimal"` (`INSTALLLEVEL=20`), `"typical"` (`100`), `"complete"` (`1000`), or `"custom"`
    std::string    explicitFeaturesCsv; // Optional comma-separated Feature IDs (`ADDLOCAL=Main,Templates`)
    ui::GlassGlyph glyph = ui::GlassGlyph::Bolt;
    COLORREF       rimRgb    = ui::brand::kSkyCyan;
    COLORREF       accentRgb = ui::brand::kWarmGold;
    bool           recommended = false;
};

/// Optional rotating feature showcase card displayed during `WizardPage::Installing` (`[Billboard.<Id>]`).
struct BillboardSlideSpec {
    std::wstring   headline;
    std::wstring   body;
    ui::GlassGlyph glyph = ui::GlassGlyph::Sparkle;
};

/// Builds the active list of `1..4` Setup Type option cards from `[UI] SetupTypePreset` + any `[SetupType.*]` overrides!
inline std::vector<SetupTypeOptionSpec> BuildEffectiveSetupTypeOptions(
    std::string_view presetToken,
    const std::vector<SetupTypeOptionSpec>& customTypes,
    const std::wstring& expressDesc,
    const std::wstring& compactDesc,
    const std::wstring& customDesc)
{
    if (!customTypes.empty()) {
        std::vector<SetupTypeOptionSpec> capped = customTypes;
        if (capped.size() > 4) capped.resize(4);
        return capped;
    }

    if (EqualsIgnoreCase(TrimAscii(presetToken), "win95_classic")) {
        return {
            {"Typical",  L"&Typical (Recommended)",
             L"Installs the most common program features recommended for everyday use.",
             "typical",  "", ui::GlassGlyph::Bolt,    ui::brand::kSkyCyan,    ui::brand::kWarmGold, true},
            {"Compact",  L"Co&mpact (Minimal)",
             L"Installs only the minimum required core files to conserve disk space.",
             "minimal",  "", ui::GlassGlyph::Cube,    ui::brand::kRaggieBlue, ui::brand::kSkyCyan,  false},
            {"Complete", L"Com&plete",
             L"Installs all program features, templates, documentation, and optional extras.",
             "complete", "", ui::GlassGlyph::Blocks,  ui::brand::kEmeraldOk,  ui::brand::kWarmGold, false},
            {"Custom",   L"C&ustom",
             L"Choose individual features, destination folder, and shortcuts step by step.",
             "custom",   "", ui::GlassGlyph::Sliders, ui::brand::kVioletTune, ui::brand::kWarmGold, false}
        };
    }

    return {
        {"Express", L"&Express Setup (Recommended)", expressDesc,
         "typical", "", ui::GlassGlyph::Bolt,    ui::brand::kSkyCyan,    ui::brand::kWarmGold, true},
        {"Compact", L"Co&mpact Core Setup",          compactDesc,
         "minimal", "", ui::GlassGlyph::Cube,    ui::brand::kRaggieBlue, ui::brand::kSkyCyan,  false},
        {"Custom",  L"C&ustom Setup (Choose Features, Folder && Shortcuts)", customDesc,
         "custom",  "", ui::GlassGlyph::Sliders, ui::brand::kVioletTune, ui::brand::kWarmGold, false}
    };
}

/// Applies the classic 1995 `#C0C0C0` 3D dialog sheet palette when `Wizard95`, `FullScreenBlue`, or
/// `FullScreenTeal` is active—UNLESS a Windows Contrast Theme (`SPI_GETHIGHCONTRAST`) is enabled!
inline void ApplyWizardStyleThemeOverrides(WizardStyleKind style, ui::RswTheme& theme) noexcept {
    if (theme.isHighContrast) {
        return; // Assistive High Contrast ALWAYS wins!
    }
    if (UsesClassic3dDialogSheet(style)) {
        theme.Release();
        theme.isDarkMode    = false;
        theme.windowBg      = RGB(192, 192, 192); // `#C0C0C0` Classic Windows 95 3D Face
        theme.cardBg        = RGB(255, 255, 255); // Crisp white sunken edit / tree wells
        theme.footerBg      = RGB(192, 192, 192); // Unified `#C0C0C0` sheet with etched divider
        theme.textPrimary   = RGB(0, 0, 0);       // `12.6:1` WCAG AAA contrast on `#C0C0C0`
        theme.textSecondary = RGB(30, 41, 59);    // `9.3:1` WCAG AAA contrast on `#C0C0C0`
        theme.borderSubtle  = RGB(128, 128, 128); // Classic `#808080` 3D shadow gray
        if (style == WizardStyleKind::FullScreenTeal) {
            theme.accentPrimary = RGB(0, 128, 128); // `#008080` ACME Teal accent
        } else {
            theme.accentPrimary = RGB(0, 0, 128);   // `#000080` Classic Win95 Titlebar Navy
        }
        theme.hbrWindow = ::CreateSolidBrush(theme.windowBg);
        theme.hbrCard   = ::CreateSolidBrush(theme.cardBg);
        theme.hbrFooter = ::CreateSolidBrush(theme.footerBg);
    }
}

namespace ui {

/// Draws a classic Windows 95 two-line etched horizontal separator (`#808080` shadow over `#FFFFFF` highlight).
inline void DrawClassicEtchedHorizontalRule(HDC hdc, int x1, int x2, int y, const RswTheme& theme) noexcept {
    if (theme.isHighContrast) {
        HPEN hcPen = ::CreatePen(PS_SOLID, 2, theme.borderSubtle);
        HGDIOBJ old = ::SelectObject(hdc, hcPen);
        ::MoveToEx(hdc, x1, y, nullptr);
        ::LineTo(hdc, x2, y);
        ::SelectObject(hdc, old);
        ::DeleteObject(hcPen);
        return;
    }
    HPEN shadowPen = ::CreatePen(PS_SOLID, 1, RGB(128, 128, 128));
    HPEN hilitePen = ::CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
    HGDIOBJ old = ::SelectObject(hdc, shadowPen);
    ::MoveToEx(hdc, x1, y, nullptr);
    ::LineTo(hdc, x2, y);
    ::SelectObject(hdc, hilitePen);
    ::MoveToEx(hdc, x1, y + 1, nullptr);
    ::LineTo(hdc, x2, y + 1);
    ::SelectObject(hdc, old);
    ::DeleteObject(shadowPen);
    ::DeleteObject(hilitePen);
}

/// Draws a classic Windows 95 3D raised (`isSunken = false`) or sunken (`isSunken = true`) beveled frame.
inline void DrawClassic3dBevelFrame(HDC hdc, const RECT& rc, bool isSunken, const RswTheme& theme) noexcept {
    if (theme.isHighContrast) {
        HPEN hcPen = ::CreatePen(PS_SOLID, 2, theme.borderSubtle);
        HGDIOBJ oldP = ::SelectObject(hdc, hcPen);
        HGDIOBJ oldB = ::SelectObject(hdc, ::GetStockObject(NULL_BRUSH));
        ::Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
        ::SelectObject(hdc, oldB);
        ::SelectObject(hdc, oldP);
        ::DeleteObject(hcPen);
        return;
    }
    RECT tmp = rc;
    ::DrawEdge(hdc, &tmp, isSunken ? EDGE_SUNKEN : EDGE_RAISED, BF_RECT);
}

/// Draws the classic `164px` Left Vertical Graphic Rail used on `Wizard95` & `Wizard97` Welcome/Finish screens!
inline void DrawClassicLeftSidebarRail(
    HDC hdc,
    Gdiplus::Graphics& g,
    const RECT& railRc,
    UINT dpi,
    const RswTheme& theme,
    bool isTealAcme,
    HICON hProductIcon,
    HBITMAP hArtworkBmp,
    int artW,
    int artH,
    std::wstring_view manufacturer) noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };
    const int rw = railRc.right - railRc.left;
    const int rh = railRc.bottom - railRc.top;
    if (rw <= 16 || rh <= 32) return;

    if (theme.isHighContrast) {
        ::FillRect(hdc, &railRc, theme.hbrCard);
        DrawClassic3dBevelFrame(hdc, railRc, true, theme);
        return;
    }

    if (hArtworkBmp && artW > 0 && artH > 0) {
        DrawAlphaBitmapFit(hdc, hArtworkBmp, artW, artH, railRc);
    } else {
        const Gdiplus::Color topCol = isTealAcme
            ? Gdiplus::Color(255, 0, 128, 128)   // `#008080` ACME Teal
            : Gdiplus::Color(255, 0, 0, 128);    // `#000080` Classic Setup Navy
        const Gdiplus::Color botCol = isTealAcme
            ? Gdiplus::Color(255, 0, 56, 64)
            : Gdiplus::Color(255, 8, 18, 48);
        Gdiplus::LinearGradientBrush vGrad(
            Gdiplus::PointF(static_cast<float>(railRc.left), static_cast<float>(railRc.top)),
            Gdiplus::PointF(static_cast<float>(railRc.left), static_cast<float>(railRc.bottom)),
            topCol, botCol);
        g.FillRectangle(&vGrad, railRc.left, railRc.top, rw, rh);

        // Retro 1995 geometric software grid accent lines inside the sidebar
        Gdiplus::Pen gridPen(Gdiplus::Color(38, 255, 255, 255), 1.0f);
        for (int gy = railRc.top + S(24); gy < railRc.bottom; gy += S(28)) {
            g.DrawLine(&gridPen, railRc.left + S(8), gy, railRc.right - S(8), gy);
        }

        const int icoSz = std::min(S(56), rw - S(24));
        const int icoX  = railRc.left + (rw - icoSz) / 2;
        const int icoY  = railRc.top + S(28);
        if (hProductIcon) {
            ::DrawIconEx(hdc, icoX, icoY, hProductIcon, icoSz, icoSz, 0, nullptr, DI_NORMAL);
        } else {
            DrawBrandSquircleBadge(g, static_cast<float>(icoX), static_cast<float>(icoY), static_cast<float>(icoSz), theme, manufacturer);
        }
    }
    DrawClassic3dBevelFrame(hdc, railRc, true, theme);
}

/// Draws the Classic Enterprise **Wizard 97** Top Header (`58..72px` tall) with bold title, indented
/// subtitle, right-hand product icon, and bottom etched rule!
inline void DrawWizard97TopHeader(
    HDC hdc,
    int clientW,
    UINT dpi,
    const RswTheme& theme,
    bool classicGrayMode,
    std::wstring_view stepTitle,
    std::wstring_view stepSubtitle,
    HICON hProductIcon) noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };
    const int hdrH = S(68);
    RECT hdrRc{0, 0, clientW, hdrH};

    if (theme.isHighContrast) {
        ::FillRect(hdc, &hdrRc, theme.hbrFooter);
    } else {
        const COLORREF bg = (!classicGrayMode && theme.isDarkMode) ? RGB(15, 23, 42) : RGB(255, 255, 255);
        HBRUSH hBr = ::CreateSolidBrush(bg);
        ::FillRect(hdc, &hdrRc, hBr);
        ::DeleteObject(hBr);
    }

    const int icoSz = S(40);
    const int icoX  = clientW - icoSz - S(18);
    const int icoY  = (hdrH - icoSz) / 2;
    if (hProductIcon) {
        ::DrawIconEx(hdc, icoX, icoY, hProductIcon, icoSz, icoSz, 0, nullptr, DI_NORMAL);
    }

    HFONT fBold = CreateScaledFont(L"Segoe UI", 11, FW_BOLD,   dpi, theme.textScalePercent);
    HFONT fSub  = CreateScaledFont(L"Segoe UI",  9, FW_NORMAL, dpi, theme.textScalePercent);
    ::SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldF = ::SelectObject(hdc, fBold);

    const COLORREF titleCol = theme.isHighContrast
        ? theme.textPrimary
        : ((!classicGrayMode && theme.isDarkMode) ? RGB(248, 250, 252) : RGB(15, 23, 42));
    const COLORREF subCol = theme.isHighContrast
        ? theme.textSecondary
        : ((!classicGrayMode && theme.isDarkMode) ? RGB(203, 213, 225) : RGB(51, 65, 85));

    ::SetTextColor(hdc, titleCol);
    RECT tRc{S(18), S(10), icoX - S(12), S(34)};
    ::DrawTextW(hdc, stepTitle.data(), static_cast<int>(stepTitle.size()), &tRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    ::SelectObject(hdc, fSub);
    ::SetTextColor(hdc, subCol);
    RECT sRc{S(30), S(34), icoX - S(12), hdrH - S(6)};
    ::DrawTextW(hdc, stepSubtitle.data(), static_cast<int>(stepSubtitle.size()), &sRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    ::SelectObject(hdc, oldF);
    ::DeleteObject(fBold);
    ::DeleteObject(fSub);

    DrawClassicEtchedHorizontalRule(hdc, 0, clientW, hdrH, theme);
}

/// Draws the Modern Windows 11 **Fluent Mica** Left Vertical Step-Progress Rail (`164px` wide)!
inline void DrawFluentMicaStepRail(
    HDC hdc,
    Gdiplus::Graphics& g,
    int railW,
    int topY,
    int bottomY,
    int activeStageIdx, // 0=Welcome, 1=License, 2=SetupType, 3=Customize, 4=Ready, 5=Install, 6=Finish
    UINT dpi,
    const RswTheme& theme,
    std::wstring_view productName,
    HICON hProductIcon) noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };
    RECT railRc{0, topY, railW, bottomY};

    if (!theme.isHighContrast) {
        const COLORREF railBg = theme.isDarkMode ? RGB(10, 16, 32) : RGB(235, 241, 249);
        HBRUSH hBr = ::CreateSolidBrush(railBg);
        ::FillRect(hdc, &railRc, hBr);
        ::DeleteObject(hBr);
    }

    HPEN sepPen = ::CreatePen(PS_SOLID, theme.isHighContrast ? 2 : 1, theme.borderSubtle);
    HGDIOBJ oldP = ::SelectObject(hdc, sepPen);
    ::MoveToEx(hdc, railW - 1, topY, nullptr);
    ::LineTo(hdc, railW - 1, bottomY);
    ::SelectObject(hdc, oldP);
    ::DeleteObject(sepPen);

    if (hProductIcon) {
        ::DrawIconEx(hdc, S(14), topY + S(14), hProductIcon, S(28), S(28), 0, nullptr, DI_NORMAL);
    }

    HFONT fProd = CreateScaledFont(L"Segoe UI", 9, FW_BOLD,     dpi, theme.textScalePercent);
    HFONT fStep = CreateScaledFont(L"Segoe UI", 9, FW_NORMAL,   dpi, theme.textScalePercent);
    HFONT fAct  = CreateScaledFont(L"Segoe UI", 9, FW_SEMIBOLD, dpi, theme.textScalePercent);
    ::SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldF = ::SelectObject(hdc, fProd);
    ::SetTextColor(hdc, theme.textPrimary);
    RECT pRc{S(48), topY + S(14), railW - S(8), topY + S(44)};
    ::DrawTextW(hdc, productName.data(), static_cast<int>(productName.size()), &pRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    static constexpr const wchar_t* kStepNames[7] = {
        L"Welcome",
        L"License",
        L"Setup Type",
        L"Customize",
        L"Ready",
        L"Installing",
        L"Completed"
    };

    const int startY = topY + S(58);
    const int rowH   = S(34);
    for (int i = 0; i < 7; ++i) {
        const int ry = startY + i * rowH;
        const bool isDone   = (i < activeStageIdx);
        const bool isActive = (i == activeStageIdx);

        if (isActive) {
            const Gdiplus::Color pillBg = theme.isHighContrast
                ? ToGpColor(::GetSysColor(COLOR_HIGHLIGHT), 60)
                : (theme.isDarkMode ? Gdiplus::Color(220, 24, 41, 78) : Gdiplus::Color(235, 219, 234, 254));
            Gdiplus::SolidBrush pBr(pillBg);
            Gdiplus::Pen pPen(ToGpColor(theme.accentPrimary), 1.4f);
            FillAndStrokeRoundRect(g, static_cast<float>(S(8)), static_cast<float>(ry),
                                   static_cast<float>(railW - S(16)), static_cast<float>(S(28)),
                                   static_cast<float>(S(6)), &pBr, &pPen);
        }

        const float cx = static_cast<float>(S(22));
        const float cy = static_cast<float>(ry + S(14));
        const float cr = static_cast<float>(S(8));
        if (isDone || isActive) {
            Gdiplus::SolidBrush dotBr(ToGpColor(isDone ? brand::kEmeraldOk : theme.accentPrimary));
            g.FillEllipse(&dotBr, cx - cr, cy - cr, cr * 2.0f, cr * 2.0f);
            if (isDone) {
                DrawVectorGlyph(g, GlassGlyph::ShieldCheck, cx, cy, cr * 0.8f);
            }
        } else {
            Gdiplus::Pen ringPen(ToGpColor(theme.textSecondary), 1.4f);
            g.DrawEllipse(&ringPen, cx - cr, cy - cr, cr * 2.0f, cr * 2.0f);
        }

        ::SelectObject(hdc, isActive ? fAct : fStep);
        ::SetTextColor(hdc, (isActive || isDone) ? theme.textPrimary : theme.textSecondary);
        RECT sRc{S(36), ry, railW - S(10), ry + S(28)};
        ::DrawTextW(hdc, kStepNames[i], -1, &sRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    ::SelectObject(hdc, oldF);
    ::DeleteObject(fProd);
    ::DeleteObject(fStep);
    ::DeleteObject(fAct);
}

/// Paints the nostalgic 1995 Full-Screen `SETUP.EXE` Backdrop (`FullScreenBlue` `#000080 -> #000000`
/// OR `FullScreenTeal` `#008080` Microsoft ACME Setup Engine Callback!), including:
///   1. Drop-shadowed top-left italic serif `"<ProductName> Setup"` title
///   2. Optional floating 3D beveled `[Billboard.*]` feature showcase card during installation
///   3. Classic 3D beveled sunken progress gauge (`0..100%`) docked at the bottom-right!
inline void DrawRetroFullScreenBackdrop(
    HDC hdc,
    Gdiplus::Graphics& g,
    const RECT& fullRc,
    UINT dpi,
    const RswTheme& theme,
    bool isTealAcme,
    std::wstring_view productName,
    const BillboardSlideSpec* activeBillboard,
    bool showProgressGauge,
    int progressPermille,
    std::wstring_view progressStatus) noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };
    const int W = std::max<int>(1, fullRc.right - fullRc.left);
    const int H = std::max<int>(1, fullRc.bottom - fullRc.top);

    // 1. Background wash (yields immediately to `COLOR_WINDOW` under Windows Contrast Themes!)
    if (theme.isHighContrast) {
        ::FillRect(hdc, &fullRc, theme.hbrWindow);
    } else if (isTealAcme) {
        // Authentic Microsoft ACME Setup Engine (`Office 95` / `VS 97`) `#008080` Desktop Teal!
        HBRUSH hTeal = ::CreateSolidBrush(RGB(0, 128, 128));
        ::FillRect(hdc, &fullRc, hTeal);
        ::DeleteObject(hTeal);
    } else {
        // Classic 1995 `SETUP.EXE` `#000080` Cobalt/Navy -> `#000000` vertical gradient wash!
        Gdiplus::LinearGradientBrush wash(
            Gdiplus::PointF(0.0f, static_cast<float>(fullRc.top)),
            Gdiplus::PointF(0.0f, static_cast<float>(fullRc.bottom)),
            Gdiplus::Color(255, 0, 0, 148),
            Gdiplus::Color(255, 0, 0, 12));
        g.FillRectangle(&wash, fullRc.left, fullRc.top, W, H);
    }

    // 2. Classic 1995 Top-Left Drop-Shadowed Serif Title (`Times New Roman` Bold Italic!)
    const std::wstring bannerTitle = std::wstring(productName) + L" Setup";
    const int ptSize = (W >= S(900)) ? 28 : 16; // Scales proportionally for both real monitors & IDE preview!
    const int pxHeight = -::MulDiv(ptSize, static_cast<int>(dpi > 0 ? dpi : 96), 72);
    HFONT hSerif = ::CreateFontW(
        pxHeight, 0, 0, 0, FW_BOLD, TRUE /* Italic! */, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, VARIABLE_PITCH | FF_ROMAN, L"Times New Roman");

    ::SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldF = ::SelectObject(hdc, hSerif);
    const int padX = S(24);
    const int padY = S(18);
    const int shOff = std::max(2, S(3));

    if (!theme.isHighContrast) {
        ::SetTextColor(hdc, RGB(0, 0, 0));
        RECT shadowRc{fullRc.left + padX + shOff, fullRc.top + padY + shOff, fullRc.right - padX, fullRc.top + padY + S(72)};
        ::DrawTextW(hdc, bannerTitle.c_str(), -1, &shadowRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    ::SetTextColor(hdc, theme.isHighContrast ? theme.textPrimary : RGB(255, 255, 255));
    RECT fgRc{fullRc.left + padX, fullRc.top + padY, fullRc.right - padX, fullRc.top + padY + S(72)};
    ::DrawTextW(hdc, bannerTitle.c_str(), -1, &fgRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    // 3. Dead-Center 3D-Beveled Billboard Card (`[Billboard.*]` during `Installing`!)
    //    In authentic 1995 `FullScreenBlue` & Microsoft ACME `FullScreenTeal` setups, the
    //    feature showcase billboard sits dead-center on the monitor while the compact
    //    progress meter docks cleanly at the bottom-right corner!
    if (activeBillboard != nullptr && !activeBillboard->headline.empty()) {
        const int bbW = std::min(S(620), W - S(64));
        const int bbH = S(176);
        const int bbX = fullRc.left + (W - bbW) / 2;
        const int bbY = fullRc.top  + (H - bbH) / 2;
        RECT dropSh{bbX + S(6), bbY + S(6), bbX + bbW + S(6), bbY + bbH + S(6)};
        if (!theme.isHighContrast) {
            HBRUSH hSh = ::CreateSolidBrush(RGB(0, 0, 0));
            ::FillRect(hdc, &dropSh, hSh);
            ::DeleteObject(hSh);
        }
        RECT bbRc{bbX, bbY, bbX + bbW, bbY + bbH};
        HBRUSH hFace = ::CreateSolidBrush(theme.isHighContrast ? theme.cardBg : RGB(192, 192, 192));
        ::FillRect(hdc, &bbRc, hFace);
        ::DeleteObject(hFace);
        DrawClassic3dBevelFrame(hdc, bbRc, false, theme);

        // Inner sunken header band inside the dead-center 1995 Billboard card
        RECT innerRc{bbX + S(12), bbY + S(12), bbRc.right - S(12), bbRc.bottom - S(12)};
        DrawClassic3dBevelFrame(hdc, innerRc, true, theme);

        DrawOptionBadge48(g, static_cast<float>(bbX + S(26)), static_cast<float>(bbY + S(32)),
                          static_cast<float>(S(54)), activeBillboard->glyph,
                          isTealAcme ? RGB(0, 128, 128) : RGB(0, 0, 128), brand::kWarmGold, theme);

        HFONT fBh = CreateScaledFont(L"Segoe UI", 13, FW_BOLD,   dpi, theme.textScalePercent);
        HFONT fBb = CreateScaledFont(L"Segoe UI", 10, FW_NORMAL, dpi, theme.textScalePercent);
        ::SelectObject(hdc, fBh);
        ::SetTextColor(hdc, theme.isHighContrast ? theme.textPrimary : (isTealAcme ? RGB(0, 96, 96) : RGB(0, 0, 128)));
        RECT bhRc{bbX + S(96), bbY + S(28), bbRc.right - S(26), bbY + S(62)};
        ::DrawTextW(hdc, activeBillboard->headline.c_str(), -1, &bhRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

        ::SelectObject(hdc, fBb);
        ::SetTextColor(hdc, theme.isHighContrast ? theme.textSecondary : RGB(15, 23, 42));
        RECT bdRc{bbX + S(96), bbY + S(66), bbRc.right - S(26), bbRc.bottom - S(22)};
        ::DrawTextW(hdc, activeBillboard->body.c_str(), -1, &bdRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        ::DeleteObject(fBh);
        ::DeleteObject(fBb);
    }

    // 4. Docked Bottom-Right 3D Beveled Progress Gauge (`0..100%`)
    if (showProgressGauge) {
        const int gW = std::min(S(420), W - S(40));
        const int gH = S(68);
        const int gX = fullRc.right - gW - S(24);
        const int gY = fullRc.bottom - gH - S(24);
        RECT gaugeRc{gX, gY, gX + gW, gY + gH};

        HBRUSH hFace = ::CreateSolidBrush(theme.isHighContrast ? theme.cardBg : RGB(192, 192, 192));
        ::FillRect(hdc, &gaugeRc, hFace);
        ::DeleteObject(hFace);
        DrawClassic3dBevelFrame(hdc, gaugeRc, false, theme);

        const int pct = std::clamp(progressPermille / 10, 0, 100);
        const std::wstring cap = (progressStatus.empty() ? L"Copying program files..." : std::wstring(progressStatus)) +
                                 L"  (" + std::to_wstring(pct) + L"%)";
        HFONT fCap = CreateScaledFont(L"Segoe UI", 9, FW_BOLD, dpi, theme.textScalePercent);
        ::SelectObject(hdc, fCap);
        ::SetTextColor(hdc, theme.isHighContrast ? theme.textPrimary : RGB(0, 0, 0));
        RECT cRc{gX + S(12), gY + S(8), gaugeRc.right - S(12), gY + S(28)};
        ::DrawTextW(hdc, cap.c_str(), -1, &cRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        ::DeleteObject(fCap);

        RECT wellRc{gX + S(12), gY + S(32), gaugeRc.right - S(12), gaugeRc.bottom - S(12)};
        HBRUSH hWell = ::CreateSolidBrush(theme.isHighContrast ? theme.windowBg : RGB(255, 255, 255));
        ::FillRect(hdc, &wellRc, hWell);
        ::DeleteObject(hWell);
        DrawClassic3dBevelFrame(hdc, wellRc, true, theme);

        const int innerW = std::max<int>(0, static_cast<int>(wellRc.right - wellRc.left - 4) * pct / 100);
        if (innerW > 0) {
            RECT fillRc{wellRc.left + 2, wellRc.top + 2, wellRc.left + 2 + innerW, wellRc.bottom - 2};
            const COLORREF barCol = theme.isHighContrast
                ? ::GetSysColor(COLOR_HIGHLIGHT)
                : (isTealAcme ? RGB(0, 128, 128) : RGB(0, 0, 128));
            HBRUSH hBar = ::CreateSolidBrush(barCol);
            ::FillRect(hdc, &fillRc, hBar);
            ::DeleteObject(hBar);
        }
    }

    ::SelectObject(hdc, oldF);
    ::DeleteObject(hSerif);
}

} // namespace ui
} // namespace rsw
