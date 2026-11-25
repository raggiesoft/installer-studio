// ================================================================================
// FILE: src/common/RswGlassRenderer.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file, and why does it make our C++ installers look so good?
//     In legacy installer tools (like Delphi VCL `.dfm` forms), building modern
//     rounded cards, glowing headers, step medallions, and vector badges required
//     baking dozens of static `.png` images offline with Python scripts—and worse,
//     legacy VCL radio controls (`TRzRadioButton` + `TGlowLabel`) split each option
//     into an unlabeled 15-pixel dot and separate non-focusable text labels,
//     failing strict WCAG 2.2 AA / Section 508 screen-reader and target-size rules!
//
//     Instead, `RswGlassRenderer.hpp` renders our entire signature **RaggieSoft
//     Glass** & **16:9 CD-Autorun** visual design system dynamically at runtime in
//     100% native Win32 C++20 using **GDI+** (`gdiplus.dll`, shipped inside
//     `C:\Windows\System32` on every copy of Windows—zero external dependencies!):
//       1. **Universal Glass Top Header (`DrawGlassTopHeader`)**:
//          - 28x28 Squircle Brand Emblem (Midnight Navy + Sky Cyan + Warm Gold
//            with luminous vector lightbulb, or automatic publisher monogram for
//            white-label third-party vendors!)
//          - Kicker Eyebrow (`"RAGGIESOFT  |  OFFICIAL SETUP WIZARD"`) + 20pt Title
//          - 64x64 Step-Specific Product Medallion on top-right with a circular
//            24x24 color-coded vector step badge that updates on every wizard page!
//          - 4px Multi-Stop Gradient Accent Bar (`Cobalt -> Primary -> Gold -> Cyan`)
//       2. **Hero Brand Card (`DrawHeroBrandCard`)**:
//          - Deep Midnight Navy (`#0B132B`) rounded card with soft radial glow,
//            Warm Gold inner hairline, product icon, and high-contrast Status Pill
//            (`"OFFICIAL INSTALLER"`, `"MAINTENANCE WIZARD"`, `"READY TO INSTALL"`,
//            `"CONFIGURING SYSTEM"`, `"SAFE UNINSTALL"`, `"READY TO LAUNCH"`).
//       3. **WCAG 2.2 AA Accessible 48x48 Vector Option Cards (`DrawAccessibleOptionCard`)**:
//          - Full-row clickable target (`>= 64px` tall, exceeding WCAG 2.5.8!),
//            48x48 vector squircle icon (`Bolt`, `Cube`, `Sliders`, `Blocks`, `Gear`,
//            `ShieldCheck`, `Reinstall`, `Uninstall`), crisp radio indicator, bold
//            mnemonic heading + wrapped subtitle, and an unmistakable 2.5px high-contrast
//            keyboard focus ring (`>= 7.8:1` contrast, exceeding WCAG 2.4.11/2.4.13)!
//       4. **Automatic Windows Contrast Theme Compliance (`SPI_GETHIGHCONTRAST`)**:
//          - Whenever a user enables Windows High Contrast (`Aquatic`, `Desert`,
//            `Dusk`, `Night Sky`), decorative gradients step aside automatically for
//            pure OS system colors (`COLOR_WINDOW`, `COLOR_WINDOWTEXT`, `COLOR_HIGHLIGHT`).
// ================================================================================

#pragma once

#include "RswWin32UiTheme.hpp"

#include <objidl.h>
#include <shellapi.h>
namespace Gdiplus {
    using std::min;
    using std::max;
}
#include <gdiplus.h>

#include <cmath>
#include <cwctype>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "shell32.lib")

namespace rsw::ui {

/// RAII initializer for Microsoft GDI+ (`System32\gdiplus.dll`).
/// Construct once at the top of `wWinMain` in `SetupWizard.exe` and `InstallerStudio.exe`.
class ScopedGdiPlusSession {
public:
    ScopedGdiPlusSession() noexcept {
        Gdiplus::GdiplusStartupInput input{};
        status_ = Gdiplus::GdiplusStartup(&token_, &input, nullptr);
    }
    ~ScopedGdiPlusSession() {
        if (status_ == Gdiplus::Ok && token_ != 0) {
            Gdiplus::GdiplusShutdown(token_);
        }
    }
    ScopedGdiPlusSession(const ScopedGdiPlusSession&) = delete;
    ScopedGdiPlusSession& operator=(const ScopedGdiPlusSession&) = delete;

private:
    ULONG_PTR       token_  = 0;
    Gdiplus::Status status_ = Gdiplus::GenericError;
};

/// Step-specific vector glyphs matching `build-raggiesoft-glass-dialogs.py` 1:1!
enum class GlassGlyph {
    Sparkle,     // Welcome (`#0060F0` + `#FBBF24` 8-point star)
    ShieldCheck, // License, Finish, & Quick Check & Repair (`#10B981` emerald check)
    Bolt,        // Express Setup (`#38BDF8` + `#FBBF24` lightning bolt)
    Cube,        // Compact Core Setup (`#0060F0` + `#38BDF8` 3D isometric cube)
    Sliders,     // Custom Setup (`#A855F7` + `#FBBF24` 3-rail mixer sliders)
    Blocks,      // Components & Modify Features (`#A855F7` / `#38BDF8` 2x2 grid)
    Folder,      // Destination Folder (`#0060F0` + `#FBBF24` folder)
    PinGrid,     // Start Menu & File Associations (`#38BDF8` / `#A855F7` 4-tile grid)
    Play,        // Ready to Apply Changes (`#10B981` play triangle)
    Gear,        // Progress & Maintenance (`#0060F0` / `#A855F7` 8-tooth precision gear)
    Reinstall,   // Full Reinstall (`#0060F0` + `#38BDF8` dual refresh arcs)
    Uninstall,   // Safe Uninstall (`#F43F5E` coral-rose archive box)
    ReadMeBook   // ReadMe / Edition Liner Notes (`#38BDF8` + `#FBBF24` open document)
};

// Signature RaggieSoft Glass Brand Palette Constants
namespace brand {
    constexpr COLORREF kNavyDeep    = RGB(11, 19, 43);   // #0B132B
    constexpr COLORREF kNavyCard    = RGB(15, 25, 56);   // #0F1938
    constexpr COLORREF kRaggieBlue  = RGB(0, 96, 240);   // #0060F0
    constexpr COLORREF kSkyCyan     = RGB(56, 189, 248); // #38BDF8
    constexpr COLORREF kWarmGold    = RGB(251, 191, 36); // #FBBF24
    constexpr COLORREF kEmeraldOk   = RGB(16, 185, 129); // #10B981
    constexpr COLORREF kVioletTune  = RGB(168, 85, 247); // #A855F7
    constexpr COLORREF kCoralRose   = RGB(244, 63, 94);  // #F43F5E
} // namespace brand

/// Converts a Win32 `COLORREF` + `alpha` (`0..255`) into a `Gdiplus::Color`.
inline Gdiplus::Color ToGpColor(COLORREF c, BYTE alpha = 255) noexcept {
    return Gdiplus::Color(alpha, GetRValue(c), GetGValue(c), GetBValue(c));
}

/// Appends a smooth rounded rectangle (`radius` pixels) to a `Gdiplus::GraphicsPath`.
inline void BuildRoundedRectPath(
    Gdiplus::GraphicsPath& path,
    Gdiplus::REAL x,
    Gdiplus::REAL y,
    Gdiplus::REAL w,
    Gdiplus::REAL h,
    Gdiplus::REAL radius) noexcept
{
    path.Reset();
    if (w <= 0.0f || h <= 0.0f) return;
    const Gdiplus::REAL r = std::clamp(radius, 0.0f, std::min(w, h) * 0.5f);
    if (r <= 0.5f) {
        path.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    const Gdiplus::REAL d = r * 2.0f;
    path.AddArc(x,         y,         d, d, 180.0f, 90.0f);
    path.AddArc(x + w - d, y,         d, d, 270.0f, 90.0f);
    path.AddArc(x + w - d, y + h - d, d, d,   0.0f, 90.0f);
    path.AddArc(x,         y + h - d, d, d,  90.0f, 90.0f);
    path.CloseFigure();
}

/// Fills and/or outlines a rounded rectangle with smooth anti-aliasing.
inline void FillAndStrokeRoundRect(
    Gdiplus::Graphics& g,
    Gdiplus::REAL x,
    Gdiplus::REAL y,
    Gdiplus::REAL w,
    Gdiplus::REAL h,
    Gdiplus::REAL radius,
    const Gdiplus::Brush* fillBrush,
    const Gdiplus::Pen* borderPen = nullptr) noexcept
{
    Gdiplus::GraphicsPath path;
    BuildRoundedRectPath(path, x, y, w, h, radius);
    if (fillBrush) g.FillPath(fillBrush, &path);
    if (borderPen) g.DrawPath(borderPen, &path);
}

/// Returns uppercase version of `s` for kicker eyebrows (`"RAGGIESOFT  |  OFFICIAL SETUP WIZARD"`).
inline std::wstring UpperWide(std::wstring_view s) {
    std::wstring out;
    out.reserve(s.size());
    for (wchar_t ch : s) {
        out.push_back(static_cast<wchar_t>(std::towupper(ch)));
    }
    return out;
}

/// Draws any of our 13 vector step/option glyphs centered at `(cx, cy)` with radius `r`.
inline void DrawVectorGlyph(
    Gdiplus::Graphics& g,
    GlassGlyph kind,
    Gdiplus::REAL cx,
    Gdiplus::REAL cy,
    Gdiplus::REAL r,
    Gdiplus::Color fg = Gdiplus::Color(255, 255, 255, 255),
    Gdiplus::Color accent = ToGpColor(brand::kWarmGold, 255)) noexcept
{
    constexpr float kPi = 3.1415926535f;
    const Gdiplus::REAL stroke = std::max(1.8f, r * 0.22f);

    switch (kind) {
        case GlassGlyph::Sparkle: {
            Gdiplus::PointF pts[8]{};
            for (int k = 0; k < 8; ++k) {
                const float ang  = static_cast<float>(k) * (kPi / 4.0f) - (kPi / 2.0f);
                const float dist = (k % 2 == 0) ? (r * 0.86f) : (r * 0.32f);
                pts[k] = Gdiplus::PointF(cx + std::cos(ang) * dist, cy + std::sin(ang) * dist);
            }
            Gdiplus::SolidBrush accBr(accent);
            Gdiplus::SolidBrush fgBr(fg);
            g.FillPolygon(&accBr, pts, 8);
            g.FillEllipse(&fgBr, cx - r * 0.17f, cy - r * 0.17f, r * 0.34f, r * 0.34f);
            break;
        }

        case GlassGlyph::ShieldCheck: {
            Gdiplus::Pen checkPen(fg, std::max(2.2f, r * 0.28f));
            checkPen.SetStartCap(Gdiplus::LineCapRound);
            checkPen.SetEndCap(Gdiplus::LineCapRound);
            checkPen.SetLineJoin(Gdiplus::LineJoinRound);
            Gdiplus::PointF pts[3] = {
                Gdiplus::PointF(cx - r * 0.48f, cy + r * 0.04f),
                Gdiplus::PointF(cx - r * 0.12f, cy + r * 0.42f),
                Gdiplus::PointF(cx + r * 0.52f, cy - r * 0.36f)
            };
            g.DrawLines(&checkPen, pts, 3);
            break;
        }

        case GlassGlyph::Bolt: {
            Gdiplus::PointF bolt[6] = {
                Gdiplus::PointF(cx + r * 0.12f, cy - r * 0.80f),
                Gdiplus::PointF(cx - r * 0.48f, cy + r * 0.06f),
                Gdiplus::PointF(cx - r * 0.04f, cy + r * 0.06f),
                Gdiplus::PointF(cx - r * 0.18f, cy + r * 0.80f),
                Gdiplus::PointF(cx + r * 0.50f, cy - r * 0.10f),
                Gdiplus::PointF(cx + r * 0.06f, cy - r * 0.10f)
            };
            Gdiplus::SolidBrush accBr(accent);
            Gdiplus::Pen outPen(fg, std::max(1.2f, r * 0.09f));
            outPen.SetLineJoin(Gdiplus::LineJoinRound);
            g.FillPolygon(&accBr, bolt, 6);
            g.DrawPolygon(&outPen, bolt, 6);
            break;
        }

        case GlassGlyph::Cube: {
            Gdiplus::PointF topPts[4] = {
                Gdiplus::PointF(cx,             cy - r * 0.72f),
                Gdiplus::PointF(cx + r * 0.64f, cy - r * 0.34f),
                Gdiplus::PointF(cx,             cy + r * 0.04f),
                Gdiplus::PointF(cx - r * 0.64f, cy - r * 0.34f)
            };
            Gdiplus::PointF leftPts[4] = {
                Gdiplus::PointF(cx - r * 0.64f, cy - r * 0.34f),
                Gdiplus::PointF(cx,             cy + r * 0.04f),
                Gdiplus::PointF(cx,             cy + r * 0.74f),
                Gdiplus::PointF(cx - r * 0.64f, cy + r * 0.36f)
            };
            Gdiplus::PointF rightPts[4] = {
                Gdiplus::PointF(cx,             cy + r * 0.04f),
                Gdiplus::PointF(cx + r * 0.64f, cy - r * 0.34f),
                Gdiplus::PointF(cx + r * 0.64f, cy + r * 0.36f),
                Gdiplus::PointF(cx,             cy + r * 0.74f)
            };
            Gdiplus::SolidBrush bTop(ToGpColor(brand::kSkyCyan));
            Gdiplus::SolidBrush bLeft(Gdiplus::Color(255, 59, 130, 246));
            Gdiplus::SolidBrush bRight(Gdiplus::Color(255, 29, 78, 216));
            g.FillPolygon(&bTop,   topPts,   4);
            g.FillPolygon(&bLeft,  leftPts,  4);
            g.FillPolygon(&bRight, rightPts, 4);
            Gdiplus::Pen rimPen(fg, std::max(1.2f, stroke * 0.55f));
            rimPen.SetLineJoin(Gdiplus::LineJoinRound);
            Gdiplus::PointF hull[6] = {topPts[0], topPts[1], rightPts[2], rightPts[3], leftPts[3], leftPts[0]};
            g.DrawPolygon(&rimPen, hull, 6);
            break;
        }

        case GlassGlyph::Sliders: {
            const float knobPos[3] = {-0.34f, 0.32f, -0.08f};
            Gdiplus::Pen railPen(fg, std::max(1.8f, r * 0.16f));
            railPen.SetStartCap(Gdiplus::LineCapRound);
            railPen.SetEndCap(Gdiplus::LineCapRound);
            Gdiplus::SolidBrush knobBr(accent);
            Gdiplus::Pen knobRim(fg, std::max(1.1f, r * 0.09f));
            for (int i = 0; i < 3; ++i) {
                const float yy = cy + static_cast<float>(i - 1) * (r * 0.48f);
                g.DrawLine(&railPen, cx - r * 0.62f, yy, cx + r * 0.62f, yy);
                const float kx = cx + knobPos[i] * r;
                const float kr = r * 0.21f;
                g.FillEllipse(&knobBr, kx - kr, yy - kr, kr * 2.0f, kr * 2.0f);
                g.DrawEllipse(&knobRim, kx - kr, yy - kr, kr * 2.0f, kr * 2.0f);
            }
            break;
        }

        case GlassGlyph::Blocks:
        case GlassGlyph::PinGrid: {
            const float gap = r * 0.10f;
            const float box = r * 0.54f;
            const float offs[4][2] = {
                {-box - gap, -box - gap},
                { gap,       -box - gap},
                {-box - gap,  gap},
                { gap,        gap}
            };
            Gdiplus::SolidBrush fgBr(fg);
            Gdiplus::SolidBrush accBr(accent);
            for (int i = 0; i < 4; ++i) {
                const Gdiplus::Brush* br = (kind == GlassGlyph::Blocks && i == 1) ? &accBr : &fgBr;
                FillAndStrokeRoundRect(g, cx + offs[i][0], cy + offs[i][1], box, box, std::max(2.0f, r * 0.14f), br);
            }
            break;
        }

        case GlassGlyph::Folder: {
            Gdiplus::SolidBrush accBr(accent);
            Gdiplus::Pen rimPen(fg, std::max(1.4f, r * 0.12f));
            FillAndStrokeRoundRect(g, cx - r * 0.64f, cy - r * 0.50f, r * 0.60f, r * 0.30f, r * 0.10f, &accBr);
            FillAndStrokeRoundRect(g, cx - r * 0.64f, cy - r * 0.30f, r * 1.28f, r * 0.84f, r * 0.15f, &accBr, &rimPen);
            break;
        }

        case GlassGlyph::Play: {
            Gdiplus::PointF tri[3] = {
                Gdiplus::PointF(cx - r * 0.34f, cy - r * 0.54f),
                Gdiplus::PointF(cx - r * 0.34f, cy + r * 0.54f),
                Gdiplus::PointF(cx + r * 0.56f, cy)
            };
            Gdiplus::SolidBrush fgBr(fg);
            g.FillPolygon(&fgBr, tri, 3);
            break;
        }

        case GlassGlyph::Gear: {
            Gdiplus::SolidBrush fgBr(fg);
            Gdiplus::SolidBrush accBr(accent);
            Gdiplus::SolidBrush hubBr(ToGpColor(brand::kNavyDeep));
            for (int t = 0; t < 8; ++t) {
                const float ang = static_cast<float>(t) * (kPi / 4.0f);
                const float tx  = cx + std::cos(ang) * r * 0.58f;
                const float ty  = cy + std::sin(ang) * r * 0.58f;
                const float tr  = r * 0.19f;
                g.FillEllipse(&fgBr, tx - tr, ty - tr, tr * 2.0f, tr * 2.0f);
            }
            g.FillEllipse(&accBr, cx - r * 0.52f, cy - r * 0.52f, r * 1.04f, r * 1.04f);
            g.FillEllipse(&hubBr, cx - r * 0.24f, cy - r * 0.24f, r * 0.48f, r * 0.48f);
            break;
        }

        case GlassGlyph::Reinstall: {
            Gdiplus::Pen fgPen(fg, std::max(2.0f, r * 0.20f));
            Gdiplus::Pen accPen(accent, std::max(2.0f, r * 0.20f));
            fgPen.SetStartCap(Gdiplus::LineCapRound);
            fgPen.SetEndCap(Gdiplus::LineCapArrowAnchor);
            accPen.SetStartCap(Gdiplus::LineCapRound);
            accPen.SetEndCap(Gdiplus::LineCapArrowAnchor);
            const Gdiplus::RectF arcRc(cx - r * 0.64f, cy - r * 0.64f, r * 1.28f, r * 1.28f);
            g.DrawArc(&fgPen,  arcRc,  25.0f, 130.0f);
            g.DrawArc(&accPen, arcRc, 205.0f, 130.0f);
            Gdiplus::SolidBrush dotBr(accent);
            g.FillEllipse(&dotBr, cx - r * 0.18f, cy - r * 0.18f, r * 0.36f, r * 0.36f);
            break;
        }

        case GlassGlyph::Uninstall: {
            Gdiplus::Pen boxPen(fg, std::max(1.6f, r * 0.15f));
            Gdiplus::Pen lidPen(accent, stroke);
            Gdiplus::Pen minusPen(ToGpColor(brand::kCoralRose), stroke);
            lidPen.SetStartCap(Gdiplus::LineCapRound);
            lidPen.SetEndCap(Gdiplus::LineCapRound);
            minusPen.SetStartCap(Gdiplus::LineCapRound);
            minusPen.SetEndCap(Gdiplus::LineCapRound);
            FillAndStrokeRoundRect(g, cx - r * 0.56f, cy - r * 0.36f, r * 1.12f, r * 0.94f, r * 0.14f, nullptr, &boxPen);
            g.DrawLine(&lidPen,   cx - r * 0.68f, cy - r * 0.38f, cx + r * 0.68f, cy - r * 0.38f);
            g.DrawLine(&minusPen, cx - r * 0.28f, cy + r * 0.10f, cx + r * 0.28f, cy + r * 0.10f);
            break;
        }

        case GlassGlyph::ReadMeBook: {
            Gdiplus::SolidBrush pageBr(fg);
            Gdiplus::Pen spinePen(accent, std::max(1.8f, r * 0.16f));
            FillAndStrokeRoundRect(g, cx - r * 0.56f, cy - r * 0.64f, r * 1.12f, r * 1.28f, r * 0.14f, &pageBr, &spinePen);
            Gdiplus::Pen linePen(ToGpColor(brand::kNavyDeep), std::max(1.4f, r * 0.11f));
            for (int i = -1; i <= 1; ++i) {
                const float ly = cy + static_cast<float>(i) * (r * 0.28f);
                g.DrawLine(&linePen, cx - r * 0.34f, ly, cx + r * 0.34f, ly);
            }
            break;
        }
    }
}

/// Draws our signature RaggieSoft (or white-label Publisher) Squircle Badge (`22x22` or `28x28`).
inline void DrawBrandSquircleBadge(
    Gdiplus::Graphics& g,
    Gdiplus::REAL x,
    Gdiplus::REAL y,
    Gdiplus::REAL size,
    const RswTheme& theme,
    std::wstring_view manufacturer = L"RaggieSoft") noexcept
{
    const Gdiplus::REAL pad   = std::max(1.0f, size * 0.05f);
    const Gdiplus::REAL rad   = size * 0.24f;
    const Gdiplus::Color fill = theme.isHighContrast ? ToGpColor(theme.cardBg) : ToGpColor(brand::kNavyDeep, 248);
    const Gdiplus::Color rim1 = theme.isHighContrast ? ToGpColor(theme.accentPrimary) : ToGpColor(brand::kSkyCyan, 242);
    const Gdiplus::Color rim2 = theme.isHighContrast ? ToGpColor(theme.borderSubtle) : ToGpColor(brand::kWarmGold, 195);

    Gdiplus::SolidBrush bgBr(fill);
    Gdiplus::Pen outerPen(rim1, std::max(1.4f, size * 0.055f));
    FillAndStrokeRoundRect(g, x + pad, y + pad, size - pad * 2.0f, size - pad * 2.0f, rad, &bgBr, &outerPen);

    const Gdiplus::REAL inPad = pad + std::max(2.0f, size * 0.08f);
    Gdiplus::Pen innerPen(rim2, std::max(1.0f, size * 0.035f));
    FillAndStrokeRoundRect(g, x + inPad, y + inPad, size - inPad * 2.0f, size - inPad * 2.0f, std::max(2.0f, rad * 0.72f), nullptr, &innerPen);

    // For RaggieSoft, draw the luminous warm-gold lightbulb/sparkle emblem;
    // for third-party white-label publishers, render their crisp uppercase initial monogram!
    const bool isRaggieSoft = manufacturer.empty() ||
                              (UpperWide(manufacturer).find(L"RAGGIESOFT") != std::wstring::npos);
    if (isRaggieSoft) {
        const Gdiplus::REAL cx = x + size * 0.5f;
        const Gdiplus::REAL cy = y + size * 0.44f;
        const Gdiplus::REAL br = size * 0.21f;
        Gdiplus::SolidBrush bulbBr(Gdiplus::Color(255, 255, 244, 196));
        Gdiplus::Pen rayPen(ToGpColor(brand::kWarmGold), std::max(1.2f, size * 0.055f));
        g.FillEllipse(&bulbBr, cx - br, cy - br, br * 2.0f, br * 2.0f);
        g.FillRectangle(&bulbBr, cx - br * 0.48f, cy + br * 0.70f, br * 0.96f, br * 0.55f);
        g.DrawLine(&rayPen, cx - br * 0.45f, cy + br * 1.48f, cx + br * 0.45f, cy + br * 1.48f);
    } else {
        const wchar_t initial[2] = {static_cast<wchar_t>(std::towupper(manufacturer.front())), L'\0'};
        Gdiplus::FontFamily ff(L"Segoe UI");
        Gdiplus::Font font(&ff, size * 0.44f, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
        Gdiplus::SolidBrush txtBr(theme.isHighContrast ? ToGpColor(theme.textPrimary) : Gdiplus::Color(255, 255, 244, 204));
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        const Gdiplus::RectF box(x, y, size, size);
        g.DrawString(initial, 1, &font, box, &sf, &txtBr);
    }
}

/// Draws a 4-stop horizontal accent divider bar (`#0060F0 -> Primary -> #FBBF24 -> #38BDF8`)!
inline void DrawMultiStopAccentBar(
    Gdiplus::Graphics& g,
    Gdiplus::REAL x,
    Gdiplus::REAL y,
    Gdiplus::REAL w,
    Gdiplus::REAL h,
    const RswTheme& theme) noexcept
{
    if (w <= 0.0f || h <= 0.0f) return;
    if (theme.isHighContrast) {
        Gdiplus::SolidBrush hcBr(ToGpColor(theme.accentPrimary));
        g.FillRectangle(&hcBr, x, y, w, h);
        return;
    }

    Gdiplus::LinearGradientBrush grad(
        Gdiplus::PointF(x, y),
        Gdiplus::PointF(x + w, y),
        ToGpColor(brand::kRaggieBlue, 240),
        ToGpColor(brand::kSkyCyan, 90));
    Gdiplus::Color colors[4] = {
        ToGpColor(brand::kRaggieBlue,    245),
        ToGpColor(theme.accentPrimary,   245),
        ToGpColor(brand::kWarmGold,      235),
        ToGpColor(brand::kSkyCyan,       110)
    };
    Gdiplus::REAL positions[4] = {0.0f, 0.38f, 0.74f, 1.0f};
    grad.SetInterpolationColors(colors, positions, 4);
    g.FillRectangle(&grad, x, y, w, h);
}

/// Draws the `64x64` Step-Specific Product Medallion (`AeroPngImage1` equivalent)!
inline void DrawStepProductMedallion(
    HDC hdc,
    Gdiplus::Graphics& g,
    int x,
    int y,
    int sizePx,
    HICON hProductIcon,
    GlassGlyph stepGlyph,
    COLORREF stepBadgeColor,
    const RswTheme& theme,
    std::wstring_view manufacturer = L"RaggieSoft") noexcept
{
    const float sx = static_cast<float>(x);
    const float sy = static_cast<float>(y);
    const float sz = static_cast<float>(sizePx);
    const float frameSz = sz * 0.875f; // 56/64

    Gdiplus::SolidBrush frameBg(theme.isHighContrast ? ToGpColor(theme.cardBg) : ToGpColor(brand::kNavyDeep, 248));
    Gdiplus::Pen frameRim(ToGpColor(theme.accentPrimary, 245), std::max(1.5f, sz * 0.03f));
    FillAndStrokeRoundRect(g, sx + sz * 0.03f, sy + sz * 0.03f, frameSz, frameSz, sz * 0.20f, &frameBg, &frameRim);

    if (hProductIcon) {
        const int icoSz = static_cast<int>(sz * 0.68f);
        const int icoOff = static_cast<int>((frameSz - icoSz) * 0.5f + sz * 0.03f);
        ::DrawIconEx(hdc, x + icoOff, y + icoOff, hProductIcon, icoSz, icoSz, 0, nullptr, DI_NORMAL);
    } else {
        DrawBrandSquircleBadge(g, sx + sz * 0.11f, sy + sz * 0.11f, sz * 0.70f, theme, manufacturer);
    }

    // Bottom-right circular 24/64 step badge (`bx0..bx1`)
    const float bDiam = sz * 0.39f;
    const float bx    = sx + sz - bDiam - 1.0f;
    const float by    = sy + sz - bDiam - 1.0f;

    Gdiplus::SolidBrush haloBr(theme.isHighContrast ? ToGpColor(theme.windowBg) : ToGpColor(brand::kNavyDeep, 255));
    g.FillEllipse(&haloBr, bx - 2.0f, by - 2.0f, bDiam + 4.0f, bDiam + 4.0f);

    Gdiplus::SolidBrush badgeBr(theme.isHighContrast ? ToGpColor(theme.accentPrimary) : ToGpColor(stepBadgeColor, 255));
    Gdiplus::Pen badgeRim(Gdiplus::Color(240, 255, 255, 255), std::max(1.2f, sz * 0.022f));
    g.FillEllipse(&badgeBr, bx, by, bDiam, bDiam);
    g.DrawEllipse(&badgeRim, bx, by, bDiam, bDiam);

    const Gdiplus::Color glyphFg = theme.isHighContrast ? ToGpColor(theme.bannerTitleText) : Gdiplus::Color(255, 255, 255, 255);
    DrawVectorGlyph(g, stepGlyph, bx + bDiam * 0.5f, by + bDiam * 0.5f, bDiam * 0.35f, glyphFg, ToGpColor(brand::kWarmGold));
}

/// Draws a `48x48` double-rimmed squircle option badge (`opt_express_48`, `opt_custom_48`, etc.)!
inline void DrawOptionBadge48(
    Gdiplus::Graphics& g,
    Gdiplus::REAL x,
    Gdiplus::REAL y,
    Gdiplus::REAL size,
    GlassGlyph glyph,
    COLORREF rimRgb,
    COLORREF accentRgb,
    const RswTheme& theme) noexcept
{
    const Gdiplus::REAL pad = std::max(1.5f, size * 0.04f);
    const Gdiplus::REAL rad = size * 0.25f;

    const Gdiplus::Color bgOuter = theme.isHighContrast ? ToGpColor(theme.cardBg) : ToGpColor(brand::kNavyDeep, 248);
    const Gdiplus::Color bgInner = theme.isHighContrast ? ToGpColor(theme.cardBg) : ToGpColor(brand::kNavyCard, 238);
    const Gdiplus::Color rim1    = theme.isHighContrast ? ToGpColor(theme.accentPrimary) : ToGpColor(rimRgb, 245);
    const Gdiplus::Color rim2    = theme.isHighContrast ? ToGpColor(theme.borderSubtle)  : ToGpColor(accentRgb, 155);

    Gdiplus::SolidBrush br1(bgOuter);
    Gdiplus::Pen pen1(rim1, std::max(1.5f, size * 0.038f));
    FillAndStrokeRoundRect(g, x + pad, y + pad, size - pad * 2.0f, size - pad * 2.0f, rad, &br1, &pen1);

    const Gdiplus::REAL inPad = pad + std::max(2.5f, size * 0.06f);
    Gdiplus::SolidBrush br2(bgInner);
    Gdiplus::Pen pen2(rim2, std::max(1.0f, size * 0.022f));
    FillAndStrokeRoundRect(g, x + inPad, y + inPad, size - inPad * 2.0f, size - inPad * 2.0f, std::max(3.0f, rad * 0.78f), &br2, &pen2);

    const Gdiplus::Color fg = theme.isHighContrast ? ToGpColor(theme.textPrimary) : Gdiplus::Color(255, 255, 255, 255);
    const Gdiplus::Color ac = theme.isHighContrast ? ToGpColor(theme.accentPrimary) : ToGpColor(accentRgb, 255);
    DrawVectorGlyph(g, glyph, x + size * 0.5f, y + size * 0.5f, size * 0.30f, fg, ac);
}

/// Paints the Universal RaggieSoft Glass Top Header (`0..92px` scaled):
///   - 28x28 Squircle Emblem + Kicker Eyebrow + Bold Product Title + 64x64 Step Medallion + 4px Accent Bar!
inline void DrawGlassTopHeader(
    HDC hdc,
    Gdiplus::Graphics& g,
    int clientW,
    UINT dpi,
    const RswTheme& theme,
    std::wstring_view manufacturer,
    std::wstring_view kickerSuffix,
    std::wstring_view productName,
    HICON hProductIcon,
    GlassGlyph stepGlyph,
    COLORREF stepBadgeColor) noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };

    const int headerH = S(88);
    if (!theme.isHighContrast) {
        RECT hdrBg{0, 0, clientW, headerH};
        if (theme.isDarkMode) {
            FillHorizontalGradient(hdc, hdrBg, RGB(9, 15, 34), RGB(17, 30, 62));
        } else {
            FillHorizontalGradient(hdc, hdrBg, RGB(238, 243, 251), RGB(226, 235, 248));
        }
    }

    // 1. 28x28 Squircle Brand Emblem (`x=18, y=10`)
    DrawBrandSquircleBadge(g, static_cast<float>(S(18)), static_cast<float>(S(10)), static_cast<float>(S(28)), theme, manufacturer);

    // 2. Kicker Eyebrow (`"RAGGIESOFT  |  OFFICIAL SETUP WIZARD"`)
    const std::wstring pubUpper = UpperWide(manufacturer.empty() ? L"RaggieSoft" : manufacturer);
    const std::wstring kicker = pubUpper + L"  |  " + std::wstring(kickerSuffix);

    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font fKicker(&ff, static_cast<Gdiplus::REAL>(S(11)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
    Gdiplus::Font fTitle(&ff,  static_cast<Gdiplus::REAL>(S(21)), Gdiplus::FontStyleBold, Gdiplus::UnitPixel);

    const COLORREF kickCol = theme.isHighContrast
        ? theme.textSecondary
        : (theme.isDarkMode ? RGB(186, 230, 253) : RGB(30, 64, 175)); // >= 8.9:1 WCAG AAA!
    const COLORREF titleCol = theme.textPrimary;                      // >= 15.8:1 WCAG AAA!

    Gdiplus::SolidBrush kickBr(ToGpColor(kickCol));
    Gdiplus::SolidBrush titleBr(ToGpColor(titleCol));
    Gdiplus::StringFormat sf;
    sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    const float medalSz = static_cast<float>(S(64));
    const float medalX  = static_cast<float>(clientW - S(18)) - medalSz;

    g.DrawString(
        kicker.c_str(), -1, &fKicker,
        Gdiplus::RectF(static_cast<float>(S(54)), static_cast<float>(S(15)), medalX - static_cast<float>(S(64)), static_cast<float>(S(20))),
        &sf, &kickBr);

    // 3. Bold Product Title (`x=18, y=40`)
    g.DrawString(
        productName.data(), static_cast<INT>(productName.size()), &fTitle,
        Gdiplus::RectF(static_cast<float>(S(18)), static_cast<float>(S(40)), medalX - static_cast<float>(S(28)), static_cast<float>(S(38))),
        &sf, &titleBr);

    // 4. Top-Right 64x64 Step-Specific Product Medallion
    DrawStepProductMedallion(
        hdc, g, static_cast<int>(medalX), S(12), S(64),
        hProductIcon, stepGlyph, stepBadgeColor, theme, manufacturer);

    // 5. 4px Multi-Stop Gradient Accent Divider Bar (`y = 88..92`)
    DrawMultiStopAccentBar(
        g, static_cast<float>(S(18)), static_cast<float>(headerH),
        static_cast<float>(clientW - S(36)), static_cast<float>(S(4)), theme);
}

/// Paints the Modern Windows **AeroWizard** Command Header (`WizardLayout = aerowizard`):
///   - Single clean `AW_HEADERAREA` band (`0..90px`) without duplicating the OS window titlebar!
///   - `28x28` Brand Squircle Emblem + Kicker Eyebrow + `16pt` Aero Main Instruction + `60x60` Step Medallion!
///   - `< &Back` stays in the familiar Wizard97 bottom-right footer alongside `Next >` and `Cancel`.
inline void DrawAeroWizardTopHeader(
    HDC hdc,
    Gdiplus::Graphics& g,
    int clientW,
    UINT dpi,
    const RswTheme& theme,
    std::wstring_view windowCaption,
    std::wstring_view mainInstruction,
    HICON hProductIcon,
    GlassGlyph stepGlyph,
    COLORREF stepBadgeColor,
    std::wstring_view manufacturer = L"RaggieSoft") noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };

    RECT hdrBand{0, 0, clientW, S(90)};
    if (theme.isHighContrast) {
        ::FillRect(hdc, &hdrBand, theme.hbrFooter);
    } else if (theme.isDarkMode) {
        FillHorizontalGradient(hdc, hdrBand, RGB(11, 19, 40), RGB(17, 31, 62));
    } else {
        HBRUSH hWhite = ::CreateSolidBrush(RGB(255, 255, 255));
        ::FillRect(hdc, &hdrBand, hWhite);
        ::DeleteObject(hWhite);
    }

    // 1. `26x26` Brand Squircle Emblem + Uppercase Publisher/Product Eyebrow (`y = 12..36`)
    DrawBrandSquircleBadge(g, static_cast<float>(S(18)), static_cast<float>(S(12)), static_cast<float>(S(26)), theme, manufacturer);
    const std::wstring pubUpper = UpperWide(manufacturer.empty() ? L"RaggieSoft" : manufacturer);
    const std::wstring eyebrow  = pubUpper + L"  \x2022  " + UpperWide(windowCaption);

    HFONT hKickFont  = CreateScaledFont(L"Segoe UI",  9, FW_BOLD,     dpi, theme.textScalePercent);
    HFONT hInstrFont = CreateScaledFont(L"Segoe UI", 16, FW_SEMIBOLD, dpi, theme.textScalePercent);
    ::SetBkMode(hdc, TRANSPARENT);

    const int medalSz = S(60);
    const int medalX  = clientW - S(18) - medalSz;

    HGDIOBJ oldF = ::SelectObject(hdc, hKickFont);
    const COLORREF kickCol = theme.isHighContrast
        ? theme.textSecondary
        : (theme.isDarkMode ? RGB(186, 230, 253) : RGB(30, 64, 175));
    ::SetTextColor(hdc, kickCol);
    RECT capRc{S(52), S(13), medalX - S(12), S(37)};
    ::DrawTextW(hdc, eyebrow.data(), static_cast<int>(eyebrow.size()), &capRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    // 2. Aero Main Instruction (`AW_HEADERAREA` `TEXT_MAININSTRUCTION` — >= 10.4:1 WCAG AAA contrast!)
    const COLORREF aeroInstrCol = theme.isHighContrast
        ? theme.textPrimary
        : (theme.isDarkMode ? RGB(125, 211, 252) : RGB(0, 51, 153));
    ::SelectObject(hdc, hInstrFont);
    ::SetTextColor(hdc, aeroInstrCol);
    RECT insRc{S(18), S(42), medalX - S(12), S(84)};
    ::DrawTextW(hdc, mainInstruction.data(), static_cast<int>(mainInstruction.size()), &insRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    ::SelectObject(hdc, oldF);
    ::DeleteObject(hKickFont);
    ::DeleteObject(hInstrFont);

    // 3. `60x60` Step-Specific Product Medallion on top-right
    DrawStepProductMedallion(hdc, g, medalX, S(14), medalSz, hProductIcon, stepGlyph, stepBadgeColor, theme, manufacturer);

    // 4. Crisp Aero accent divider at `y = 88..90`
    DrawMultiStopAccentBar(
        g, 0.0f, static_cast<float>(S(88)),
        static_cast<float>(clientW), static_cast<float>(S(2)), theme);
}

/// Paints the Signature RaggieSoft `Hero Brand Card` (`render_hero_brand_card` 1:1 in native GDI+!)
/// displayed on Welcome, Maintenance Welcome, Ready to Install, Progress, Safe Uninstall, & Finish!
inline void DrawHeroBrandCard(
    HDC hdc,
    Gdiplus::Graphics& g,
    const RECT& cardRc,
    UINT dpi,
    const RswTheme& theme,
    std::wstring_view manufacturer,
    std::wstring_view tagline,
    std::wstring_view websiteLabel,
    std::wstring_view statusPillText,
    HICON hProductIcon) noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };
    const float x = static_cast<float>(cardRc.left);
    const float y = static_cast<float>(cardRc.top);
    const float w = static_cast<float>(cardRc.right - cardRc.left);
    const float h = static_cast<float>(cardRc.bottom - cardRc.top);
    if (w <= 40.0f || h <= 24.0f) return;

    const float rad = static_cast<float>(S(11));

    // Card surface: Deep Midnight Navy (`#0B132B`) in both Dark & Light Mode so the luminous
    // gold/white wordmark and `#FBBF24` status pill always hold >= 11.8:1 WCAG AAA contrast!
    // (In Windows High Contrast mode, honors `COLOR_WINDOW` & `COLOR_WINDOWTEXT`.)
    const Gdiplus::Color cardFill = theme.isHighContrast ? ToGpColor(theme.cardBg) : ToGpColor(brand::kNavyDeep, 248);
    const Gdiplus::Color outerRim = ToGpColor(theme.accentPrimary, 242);

    Gdiplus::SolidBrush bgBr(cardFill);
    Gdiplus::Pen outerPen(outerRim, std::max(1.5f, static_cast<float>(S(2)) * 0.8f));
    FillAndStrokeRoundRect(g, x, y, w, h, rad, &bgBr, &outerPen);

    if (!theme.isHighContrast) {
        // Soft ambient radial glow on the right side of the Hero Card
        Gdiplus::GraphicsPath clipPath;
        BuildRoundedRectPath(clipPath, x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, rad - 1.0f);
        Gdiplus::Region oldClip;
        g.GetClip(&oldClip);
        g.SetClip(&clipPath, Gdiplus::CombineModeIntersect);

        Gdiplus::GraphicsPath glowEllipse;
        glowEllipse.AddEllipse(x + w - static_cast<float>(S(190)), y - static_cast<float>(S(36)),
                               static_cast<float>(S(220)), h + static_cast<float>(S(72)));
        Gdiplus::PathGradientBrush pgb(&glowEllipse);
        pgb.SetCenterColor(ToGpColor(theme.accentPrimary, 78));
        INT surroundCount = 1;
        Gdiplus::Color surroundCol(0, GetRValue(theme.accentPrimary), GetGValue(theme.accentPrimary), GetBValue(theme.accentPrimary));
        pgb.SetSurroundColors(&surroundCol, &surroundCount);
        g.FillPath(&pgb, &glowEllipse);

        g.SetClip(&oldClip, Gdiplus::CombineModeReplace);

        // Subtle Warm Gold inner hairline (`#FBBF24` @ alpha 120)
        const float inOff = static_cast<float>(S(3));
        Gdiplus::Pen goldPen(ToGpColor(brand::kWarmGold, 120), 1.0f);
        FillAndStrokeRoundRect(g, x + inOff, y + inOff, w - inOff * 2.0f, h - inOff * 2.0f, std::max(3.0f, rad - inOff), nullptr, &goldPen);
    }

    // Left Emblem + Luminous Publisher Wordmark
    const float badgeSz = static_cast<float>(S(26));
    DrawBrandSquircleBadge(g, x + static_cast<float>(S(14)), y + static_cast<float>(S(11)), badgeSz, theme, manufacturer);

    Gdiplus::FontFamily ff(L"Segoe UI");
    Gdiplus::Font fWordmark(&ff, static_cast<Gdiplus::REAL>(S(18)), Gdiplus::FontStyleBold,    Gdiplus::UnitPixel);
    Gdiplus::Font fSub(&ff,      static_cast<Gdiplus::REAL>(S(12)), Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    Gdiplus::Font fPill(&ff,     static_cast<Gdiplus::REAL>(S(10)), Gdiplus::FontStyleBold,    Gdiplus::UnitPixel);

    const Gdiplus::Color wordCol = theme.isHighContrast ? ToGpColor(theme.textPrimary) : Gdiplus::Color(255, 248, 250, 252);
    const Gdiplus::Color subCol  = theme.isHighContrast ? ToGpColor(theme.textSecondary) : Gdiplus::Color(255, 186, 230, 253);
    Gdiplus::SolidBrush wordBr(wordCol);
    Gdiplus::SolidBrush subBr(subCol);

    Gdiplus::StringFormat sf;
    sf.SetTrimming(Gdiplus::StringTrimmingEllipsisCharacter);
    sf.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

    const int icoPx = S(46);
    const int icoX  = cardRc.right - icoPx - S(14);
    const int icoY  = cardRc.top + ((cardRc.bottom - cardRc.top) - icoPx) / 2;
    if (hProductIcon) {
        ::DrawIconEx(hdc, icoX, icoY, hProductIcon, icoPx, icoPx, 0, nullptr, DI_NORMAL);
    }

    // Status Pill (`"OFFICIAL INSTALLER"`, `"READY TO LAUNCH"`, etc.)
    float pillLeft = static_cast<float>(icoX - S(10));
    if (!statusPillText.empty()) {
        Gdiplus::RectF measured;
        g.MeasureString(statusPillText.data(), static_cast<INT>(statusPillText.size()), &fPill, Gdiplus::PointF(0, 0), &measured);
        const float pw = measured.Width + static_cast<float>(S(18));
        const float ph = static_cast<float>(S(20));
        const float px0 = static_cast<float>(icoX - S(12)) - pw;
        const float py0 = y + static_cast<float>(S(12));
        if (px0 > x + static_cast<float>(S(180))) {
            pillLeft = px0;
            Gdiplus::SolidBrush pillBg(theme.isHighContrast ? ToGpColor(theme.windowBg) : ToGpColor(brand::kNavyCard, 245));
            Gdiplus::Pen pillRim(theme.isHighContrast ? ToGpColor(theme.accentPrimary) : ToGpColor(brand::kSkyCyan, 195), 1.0f);
            FillAndStrokeRoundRect(g, px0, py0, pw, ph, ph * 0.5f, &pillBg, &pillRim);

            Gdiplus::SolidBrush pillTxtBr(theme.isHighContrast ? ToGpColor(theme.textPrimary) : ToGpColor(brand::kWarmGold, 255));
            Gdiplus::StringFormat cSf;
            cSf.SetAlignment(Gdiplus::StringAlignmentCenter);
            cSf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(statusPillText.data(), static_cast<INT>(statusPillText.size()), &fPill, Gdiplus::RectF(px0, py0, pw, ph), &cSf, &pillTxtBr);
        }
    }

    const std::wstring pubTitle = manufacturer.empty() ? L"RaggieSoft" : std::wstring(manufacturer);
    g.DrawString(
        pubTitle.c_str(), -1, &fWordmark,
        Gdiplus::RectF(x + static_cast<float>(S(46)), y + static_cast<float>(S(10)), pillLeft - (x + static_cast<float>(S(54))), static_cast<float>(S(26))),
        &sf, &wordBr);

    std::wstring subLine(tagline);
    if (!websiteLabel.empty()) {
        if (!subLine.empty()) subLine += L"  •  ";
        subLine += websiteLabel;
    }
    g.DrawString(
        subLine.c_str(), -1, &fSub,
        Gdiplus::RectF(x + static_cast<float>(S(16)), y + static_cast<float>(S(42)), static_cast<float>(icoX) - (x + static_cast<float>(S(26))), static_cast<float>(S(22))),
        &sf, &subBr);
}

/// Paints a WCAG 2.2 AA Accessible Interactive Option Card (`Express`, `Compact`, `Custom`,
/// `Modify`, `Repair`, `Uninstall`, `Quick Repair`, `Full Reinstall`)!
///   - `isSelected`: Highlights card border & fills crisp radio indicator dot.
///   - `hasFocus`  : Draws high-contrast 2.5px WCAG 2.4.11/2.4.13 focus ring!
///   - `isHovered` : Subtle elevation tint for pointer discoverability.
inline void DrawAccessibleOptionCard(
    HDC hdc,
    Gdiplus::Graphics& g,
    const RECT& rc,
    UINT dpi,
    const RswTheme& theme,
    GlassGlyph glyph,
    COLORREF badgeRimRgb,
    COLORREF badgeAccentRgb,
    std::wstring_view titleWithMnemonic,
    std::wstring_view description,
    bool isSelected,
    bool hasFocus,
    bool isHovered) noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };
    const float x = static_cast<float>(rc.left);
    const float y = static_cast<float>(rc.top);
    const float w = static_cast<float>(rc.right - rc.left);
    const float h = static_cast<float>(rc.bottom - rc.top);
    if (w <= 32.0f || h <= 24.0f) return;

    const float rad = static_cast<float>(S(10));

    // Card background & border selection state
    COLORREF bgRef = theme.cardBg;
    if (!theme.isHighContrast) {
        if (isSelected) {
            bgRef = theme.isDarkMode ? RGB(24, 41, 78) : RGB(239, 246, 255); // Subtle selected tint preserving >= 10:1 text contrast!
        } else if (isHovered) {
            bgRef = theme.isDarkMode ? RGB(26, 38, 68) : RGB(248, 250, 252);
        }
    }

    const COLORREF borderRef = isSelected ? theme.accentPrimary : theme.borderSubtle;
    const float borderWidth  = isSelected ? 2.0f : 1.0f;

    Gdiplus::SolidBrush cardBr(ToGpColor(bgRef));
    Gdiplus::Pen cardPen(ToGpColor(borderRef), borderWidth);
    FillAndStrokeRoundRect(g, x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, rad, &cardBr, &cardPen);

    // WCAG 2.2 SC 2.4.11 / 2.4.13 High-Contrast Keyboard Focus Ring!
    if (hasFocus) {
        const COLORREF focusRef = theme.isHighContrast
            ? ::GetSysColor(COLOR_HIGHLIGHT)
            : (theme.isDarkMode ? brand::kWarmGold : RGB(29, 78, 216)); // 12.6:1 Dark / 7.8:1 Light!
        Gdiplus::Pen focusPen(ToGpColor(focusRef), 2.5f);
        focusPen.SetDashStyle(Gdiplus::DashStyleSolid);
        FillAndStrokeRoundRect(g, x + 3.0f, y + 3.0f, w - 6.0f, h - 6.0f, std::max(4.0f, rad - 2.0f), nullptr, &focusPen);
    }

    // Left 48x48 Vector Squircle Badge
    const float badgeSz = static_cast<float>(std::min(S(48), static_cast<int>(h) - S(12)));
    const float badgeX  = x + static_cast<float>(S(12));
    const float badgeY  = y + (h - badgeSz) * 0.5f;
    DrawOptionBadge48(g, badgeX, badgeY, badgeSz, glyph, badgeRimRgb, badgeAccentRgb, theme);

    // Crisp Accessible Radio Circle (`x = badgeX + badgeSz + 12`)
    const float radioSz = static_cast<float>(S(18));
    const float radioX  = badgeX + badgeSz + static_cast<float>(S(12));
    const float radioY  = y + static_cast<float>(S(12));
    Gdiplus::Pen radioRim(ToGpColor(isSelected ? theme.accentPrimary : theme.textSecondary), 2.0f);
    g.DrawEllipse(&radioRim, radioX, radioY, radioSz, radioSz);
    if (isSelected) {
        const float dotPad = std::max(3.5f, radioSz * 0.25f);
        Gdiplus::SolidBrush dotBr(ToGpColor(theme.accentPrimary));
        g.FillEllipse(&dotBr, radioX + dotPad, radioY + dotPad, radioSz - dotPad * 2.0f, radioSz - dotPad * 2.0f);
    }

    // Heading (with Win32 `&` accelerator underline support via `DrawTextW`) + Wrapped Subtitle
    const int textLeft = static_cast<int>(radioX + radioSz + static_cast<float>(S(10)));
    const int textRight = rc.right - S(14);

    HFONT hTitleFont = CreateScaledFont(L"Segoe UI", 11, FW_BOLD,   dpi, theme.textScalePercent);
    HFONT hDescFont  = CreateScaledFont(L"Segoe UI",  9, FW_NORMAL, dpi, theme.textScalePercent);

    ::SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldF = ::SelectObject(hdc, hTitleFont);
    ::SetTextColor(hdc, theme.textPrimary);
    RECT titleRc{textLeft, rc.top + S(9), textRight, rc.top + S(31)};
    ::DrawTextW(hdc, titleWithMnemonic.data(), static_cast<int>(titleWithMnemonic.size()), &titleRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    ::SelectObject(hdc, hDescFont);
    ::SetTextColor(hdc, theme.textSecondary);
    RECT descRc{textLeft, rc.top + S(31), textRight, rc.bottom - S(6)};
    ::DrawTextW(hdc, description.data(), static_cast<int>(description.size()), &descRc, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);

    ::SelectObject(hdc, oldF);
    ::DeleteObject(hTitleFont);
    ::DeleteObject(hDescFont);
}

/// Paints a compact 2x2 CD-Autorun Radio Choice Cell (`640x480` `16:9` Micro-Installer mode)!
inline void DrawAutorunChoiceCell(
    HDC hdc,
    Gdiplus::Graphics& g,
    const RECT& rc,
    UINT dpi,
    const RswTheme& theme,
    std::wstring_view titleWithMnemonic,
    std::wstring_view subtitle,
    bool isSelected,
    bool hasFocus,
    bool isHovered) noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };
    const float x = static_cast<float>(rc.left);
    const float y = static_cast<float>(rc.top);
    const float w = static_cast<float>(rc.right - rc.left);
    const float h = static_cast<float>(rc.bottom - rc.top);
    const float rad = static_cast<float>(S(8));

    COLORREF bgRef = theme.cardBg;
    if (!theme.isHighContrast && isSelected) {
        bgRef = theme.isDarkMode ? RGB(24, 41, 78) : RGB(239, 246, 255);
    } else if (!theme.isHighContrast && isHovered) {
        bgRef = theme.isDarkMode ? RGB(26, 38, 68) : RGB(248, 250, 252);
    }

    Gdiplus::SolidBrush bgBr(ToGpColor(bgRef));
    Gdiplus::Pen borderPen(ToGpColor(isSelected ? theme.accentPrimary : theme.borderSubtle), isSelected ? 1.8f : 1.0f);
    FillAndStrokeRoundRect(g, x + 1.0f, y + 1.0f, w - 2.0f, h - 2.0f, rad, &bgBr, &borderPen);

    if (hasFocus) {
        const COLORREF focusRef = theme.isHighContrast
            ? ::GetSysColor(COLOR_HIGHLIGHT)
            : (theme.isDarkMode ? brand::kWarmGold : RGB(29, 78, 216));
        Gdiplus::Pen focusPen(ToGpColor(focusRef), 2.2f);
        FillAndStrokeRoundRect(g, x + 2.5f, y + 2.5f, w - 5.0f, h - 5.0f, std::max(3.0f, rad - 1.5f), nullptr, &focusPen);
    }

    const float radioSz = static_cast<float>(S(16));
    const float radioX  = x + static_cast<float>(S(10));
    const float radioY  = y + static_cast<float>(S(9));
    Gdiplus::Pen radioRim(ToGpColor(isSelected ? theme.accentPrimary : theme.textSecondary), 1.8f);
    g.DrawEllipse(&radioRim, radioX, radioY, radioSz, radioSz);
    if (isSelected) {
        const float dp = std::max(3.0f, radioSz * 0.25f);
        Gdiplus::SolidBrush dotBr(ToGpColor(theme.accentPrimary));
        g.FillEllipse(&dotBr, radioX + dp, radioY + dp, radioSz - dp * 2.0f, radioSz - dp * 2.0f);
    }

    const int tx = static_cast<int>(radioX + radioSz + static_cast<float>(S(8)));
    HFONT hBold = CreateScaledFont(L"Segoe UI", 10, FW_BOLD,   dpi, theme.textScalePercent);
    HFONT hSub  = CreateScaledFont(L"Segoe UI",  8, FW_NORMAL, dpi, theme.textScalePercent);
    ::SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldF = ::SelectObject(hdc, hBold);
    ::SetTextColor(hdc, theme.textPrimary);
    RECT tRc{tx, rc.top + S(5), rc.right - S(8), rc.top + S(24)};
    ::DrawTextW(hdc, titleWithMnemonic.data(), static_cast<int>(titleWithMnemonic.size()), &tRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

    ::SelectObject(hdc, hSub);
    ::SetTextColor(hdc, theme.textSecondary);
    RECT sRc{tx, rc.top + S(23), rc.right - S(8), rc.bottom - S(4)};
    ::DrawTextW(hdc, subtitle.data(), static_cast<int>(subtitle.size()), &sRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    ::SelectObject(hdc, oldF);
    ::DeleteObject(hBold);
    ::DeleteObject(hSub);
}

/// Paints the Compact `16:9` Autorun Interior Header (`0..90px` on ReadMe & License screens)
/// featuring the `144x76` rounded mini-OG cover thumbnail on top-right!
inline void DrawAutorunCompactInteriorHeader(
    HDC hdc,
    Gdiplus::Graphics& g,
    int clientW,
    UINT dpi,
    const RswTheme& theme,
    std::wstring_view manufacturer,
    std::wstring_view kickerText,
    std::wstring_view displayTitle,
    HBITMAP hOgBannerBmp,
    int ogBmpW,
    int ogBmpH) noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };

    DrawBrandSquircleBadge(g, static_cast<float>(S(18)), static_cast<float>(S(10)), static_cast<float>(S(28)), theme, manufacturer);

    const int thumbW = S(144);
    const int thumbH = S(74);
    const int thumbX = clientW - thumbW - S(18);
    const int thumbY = S(8);

    if (hOgBannerBmp && ogBmpW > 0 && ogBmpH > 0) {
        RECT thumbRc{thumbX, thumbY, thumbX + thumbW, thumbY + thumbH};
        DrawAlphaBitmapFit(hdc, hOgBannerBmp, ogBmpW, ogBmpH, thumbRc);
        Gdiplus::Pen rimPen(ToGpColor(theme.accentPrimary, 235), 2.0f);
        FillAndStrokeRoundRect(
            g, static_cast<float>(thumbX), static_cast<float>(thumbY),
            static_cast<float>(thumbW), static_cast<float>(thumbH), static_cast<float>(S(8)), nullptr, &rimPen);
    }

    HFONT hKick  = CreateScaledFont(L"Segoe UI",  8, FW_BOLD, dpi, theme.textScalePercent);
    HFONT hTitle = CreateScaledFont(L"Segoe UI", 14, FW_BOLD, dpi, theme.textScalePercent);
    ::SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldF = ::SelectObject(hdc, hKick);
    ::SetTextColor(hdc, theme.isHighContrast ? theme.textSecondary : (theme.isDarkMode ? RGB(186, 230, 253) : RGB(30, 64, 175)));
    RECT kRc{S(52), S(14), thumbX - S(12), S(34)};
    ::DrawTextW(hdc, kickerText.data(), static_cast<int>(kickerText.size()), &kRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

    ::SelectObject(hdc, hTitle);
    ::SetTextColor(hdc, theme.textPrimary);
    RECT tRc{S(18), S(38), thumbX - S(12), S(84)};
    ::DrawTextW(hdc, displayTitle.data(), static_cast<int>(displayTitle.size()), &tRc, DT_LEFT | DT_WORDBREAK | DT_END_ELLIPSIS | DT_NOPREFIX);

    ::SelectObject(hdc, oldF);
    ::DeleteObject(hKick);
    ::DeleteObject(hTitle);

    DrawMultiStopAccentBar(
        g, static_cast<float>(S(18)), static_cast<float>(S(88)),
        static_cast<float>(clientW - S(36)), static_cast<float>(S(3)), theme);
}

/// Paints the Glass Footer Bar (`22x22` Squircle Brand Emblem + `"Publisher  |  website"`).
inline void DrawGlassFooterBar(
    HDC hdc,
    Gdiplus::Graphics& g,
    int clientW,
    int clientH,
    int footerH,
    int buttonsLeftX,
    UINT dpi,
    const RswTheme& theme,
    std::wstring_view manufacturer,
    std::wstring_view websiteLabel) noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };
    RECT footerRc{0, clientH - footerH, clientW, clientH};
    ::FillRect(hdc, &footerRc, theme.hbrFooter);

    HPEN divPen = ::CreatePen(PS_SOLID, 1, theme.borderSubtle);
    HGDIOBJ oldPen = ::SelectObject(hdc, divPen);
    ::MoveToEx(hdc, 0, footerRc.top, nullptr);
    ::LineTo(hdc, clientW, footerRc.top);
    ::SelectObject(hdc, oldPen);
    ::DeleteObject(divPen);

    const float badgeSz = static_cast<float>(S(22));
    const float badgeY  = static_cast<float>(footerRc.top) + (static_cast<float>(footerH) - badgeSz) * 0.5f;
    DrawBrandSquircleBadge(g, static_cast<float>(S(18)), badgeY, badgeSz, theme, manufacturer);

    std::wstring label = manufacturer.empty() ? L"RaggieSoft" : std::wstring(manufacturer);
    if (!websiteLabel.empty()) {
        label += L"  |  ";
        label += websiteLabel;
    }

    HFONT hFootFont = CreateScaledFont(L"Segoe UI", 9, FW_BOLD, dpi, theme.textScalePercent);
    ::SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldF = ::SelectObject(hdc, hFootFont);
    ::SetTextColor(hdc, theme.textSecondary);
    RECT txtRc{S(46), footerRc.top, std::max(S(60), buttonsLeftX - S(12)), footerRc.bottom};
    ::DrawTextW(hdc, label.c_str(), -1, &txtRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    ::SelectObject(hdc, oldF);
    ::DeleteObject(hFootFont);
}

/// Paints a WCAG 2.2 AAA (`>= 15.8:1` contrast) Subclassed `BS_AUTOCHECKBOX` Row!
/// Why custom-paint checkboxes via `SetWindowSubclass` instead of relying on `WM_CTLCOLORSTATIC`?
///   Under Windows 11 Visual Styles (`ComCtl32 v6` + `uxtheme.dll`), standard `BS_AUTOCHECKBOX`
///   controls call `DrawThemeText(BP_CHECKBOX)` with hardcoded black `RGB(0,0,0)` text, ignoring
///   `SetTextColor` in `WM_CTLCOLORSTATIC` and rendering black-on-black text in Dark Mode!
///   Subclassing `BS_AUTOCHECKBOX` preserves 100% native UI Automation `CheckBox` semantics,
///   `BM_GETCHECK`, Spacebar toggling, and `Alt+Key` mnemonics while guaranteeing crisp contrast!
inline void DrawAccessibleCheckbox(
    HDC hdc,
    Gdiplus::Graphics& g,
    const RECT& rc,
    UINT dpi,
    const RswTheme& theme,
    std::wstring_view labelWithMnemonic,
    bool isChecked,
    bool hasFocus,
    bool isHovered,
    bool onCardSurface) noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };
    ::FillRect(hdc, &rc, onCardSurface ? theme.hbrCard : theme.hbrWindow);

    const float w = static_cast<float>(rc.right - rc.left);
    const float h = static_cast<float>(rc.bottom - rc.top);
    if (w <= 20.0f || h <= 12.0f) return;

    // 1. High-Contrast WCAG 2.4.11 / 2.4.13 Keyboard Focus Ring around the checkbox row
    if (hasFocus) {
        const COLORREF focusRef = theme.isHighContrast
            ? ::GetSysColor(COLOR_HIGHLIGHT)
            : (theme.isDarkMode ? brand::kWarmGold : RGB(29, 78, 216));
        Gdiplus::Pen focusPen(ToGpColor(focusRef), 2.0f);
        FillAndStrokeRoundRect(g, 1.0f, 1.0f, w - 2.0f, h - 2.0f, static_cast<float>(S(5)), nullptr, &focusPen);
    }

    // 2. Crisp `18x18` Rounded-4px Checkbox Indicator (`>= 7:1` non-text contrast!)
    const float boxSz = static_cast<float>(S(18));
    const float boxX  = static_cast<float>(S(4));
    const float boxY  = (h - boxSz) * 0.5f;
    const float rad   = static_cast<float>(S(4));

    if (isChecked) {
        const COLORREF fillCol = theme.isHighContrast ? ::GetSysColor(COLOR_HIGHLIGHT) : theme.accentPrimary;
        Gdiplus::SolidBrush fillBr(ToGpColor(fillCol));
        Gdiplus::Pen rimPen(ToGpColor(fillCol), 1.5f);
        FillAndStrokeRoundRect(g, boxX, boxY, boxSz, boxSz, rad, &fillBr, &rimPen);

        const COLORREF markCol = theme.isHighContrast ? ::GetSysColor(COLOR_HIGHLIGHTTEXT) : RGB(255, 255, 255);
        Gdiplus::Pen checkPen(ToGpColor(markCol), std::max(2.0f, boxSz * 0.13f));
        checkPen.SetStartCap(Gdiplus::LineCapRound);
        checkPen.SetEndCap(Gdiplus::LineCapRound);
        checkPen.SetLineJoin(Gdiplus::LineJoinRound);
        Gdiplus::PointF pts[3] = {
            Gdiplus::PointF(boxX + boxSz * 0.22f, boxY + boxSz * 0.52f),
            Gdiplus::PointF(boxX + boxSz * 0.43f, boxY + boxSz * 0.74f),
            Gdiplus::PointF(boxX + boxSz * 0.79f, boxY + boxSz * 0.28f)
        };
        g.DrawLines(&checkPen, pts, 3);
    } else {
        const COLORREF rimCol = isHovered ? theme.accentPrimary : theme.textSecondary;
        Gdiplus::SolidBrush boxBg(ToGpColor(theme.cardBg));
        Gdiplus::Pen rimPen(ToGpColor(rimCol), 1.6f);
        FillAndStrokeRoundRect(g, boxX, boxY, boxSz, boxSz, rad, &boxBg, &rimPen);
    }

    // 3. Crisp Label Text (`#F8FAFC` on `#0D1426` = 15.8:1 Dark / `#0F172A` on `#F4F7FB` = 16.1:1 Light!)
    HFONT hChkFont = CreateScaledFont(L"Segoe UI", 10, FW_NORMAL, dpi, theme.textScalePercent);
    ::SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldF = ::SelectObject(hdc, hChkFont);
    ::SetTextColor(hdc, theme.textPrimary);
    RECT txtRc{static_cast<int>(boxX + boxSz + static_cast<float>(S(9))), 0, rc.right - S(4), rc.bottom};
    ::DrawTextW(hdc, labelWithMnemonic.data(), static_cast<int>(labelWithMnemonic.size()), &txtRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    ::SelectObject(hdc, oldF);
    ::DeleteObject(hChkFont);
}

/// Applies Windows 11 Dark Mode (`DarkMode_Explorer`) or Daylight (`Explorer`) scrollbar chrome
/// onto standard Win32 scrollable controls (`SysListView32`, `SysTreeView32`, `LISTBOX`, `EDIT`, `RichEdit`).
inline void ApplyDarkScrollbarTheme(HWND hCtrl, const RswTheme& theme) noexcept {
    if (!hCtrl) return;
    if (theme.isHighContrast) {
        ::SetWindowTheme(hCtrl, nullptr, nullptr);
    } else {
        ::SetWindowTheme(hCtrl, theme.isDarkMode ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
}

/// Applies Windows 11 Dark Mode (`DarkMode_CFD`) or Daylight (`CFD`) theme class onto `COMBOBOX` controls
/// using the documented `SetWindowTheme` API (zero undocumented ordinals!).
inline void ApplyDarkComboBoxTheme(HWND hCombo, const RswTheme& theme) noexcept {
    if (!hCombo) return;
    if (theme.isHighContrast) {
        ::SetWindowTheme(hCombo, nullptr, nullptr);
    } else {
        ::SetWindowTheme(hCombo, theme.isDarkMode ? L"DarkMode_CFD" : L"CFD", nullptr);
    }
}

/// Paints a WCAG 2.2 AA/AAA Dark/Light/High-Contrast Accessible Push Button (`BS_PUSHBUTTON` / `BS_DEFPUSHBUTTON`)!
/// Why custom-paint push buttons via `SetWindowSubclass` instead of relying on `ComCtl32 v6` defaults?
///   Stock Win32 `WC_BUTTON` controls paint a bright `#FFFFFF` light-mode fill with black text even when
///   `DWMWA_USE_IMMERSIVE_DARK_MODE` is enabled on the parent window! Subclassing genuine `BUTTON` controls
///   preserves 100% native UI Automation (`Button` role), `Enter` default action, `Spacebar` press, and
///   `Alt+Key` mnemonics for NVDA/JAWS/Narrator while matching our Dark/Light Glass palette seamlessly!
inline void DrawAccessiblePushButton(
    HDC hdc,
    Gdiplus::Graphics& g,
    const RECT& rc,
    UINT dpi,
    const RswTheme& theme,
    std::wstring_view labelWithMnemonic,
    bool isPrimaryAccent,
    bool onFooterSurface,
    bool isEnabled,
    bool hasFocus,
    bool isHovered,
    bool isPressed,
    bool showUacShield,
    HICON hShieldIcon) noexcept
{
    auto S = [&](int px) { return ScalePx(px, dpi, theme.textScalePercent); };
    ::FillRect(hdc, &rc, onFooterSurface ? theme.hbrFooter : theme.hbrWindow);

    const float w = static_cast<float>(rc.right - rc.left);
    const float h = static_cast<float>(rc.bottom - rc.top);
    if (w <= 12.0f || h <= 10.0f) return;

    const float rad    = static_cast<float>(S(6));
    const bool  activeTab = !labelWithMnemonic.empty() && labelWithMnemonic.front() == L'[';

    COLORREF fillCol   = theme.cardBg;
    COLORREF borderCol = theme.borderSubtle;
    COLORREF textCol   = theme.textPrimary;
    float    borderPx  = 1.3f;

    if (theme.isHighContrast) {
        fillCol   = (isPrimaryAccent && isEnabled) ? ::GetSysColor(COLOR_HIGHLIGHT) : ::GetSysColor(COLOR_BTNFACE);
        textCol   = !isEnabled
            ? ::GetSysColor(COLOR_GRAYTEXT)
            : (isPrimaryAccent ? ::GetSysColor(COLOR_HIGHLIGHTTEXT) : ::GetSysColor(COLOR_BTNTEXT));
        borderCol = ::GetSysColor((hasFocus || isPrimaryAccent || activeTab) ? COLOR_HIGHLIGHT : COLOR_WINDOWFRAME);
        borderPx  = (hasFocus || isPrimaryAccent) ? 2.0f : 1.4f;
    } else if (isPrimaryAccent) {
        if (!isEnabled) {
            fillCol   = theme.isDarkMode ? RGB(30, 41, 59)   : RGB(226, 232, 240);
            borderCol = theme.isDarkMode ? RGB(71, 85, 105)  : RGB(148, 163, 184);
            textCol   = theme.isDarkMode ? RGB(148, 163, 184): RGB(100, 116, 139);
        } else {
            // High-contrast Royal Cobalt (`#1D4ED8` = 6.70:1 with `#FFFFFF`!)
            if (isPressed)      fillCol = RGB(30, 64, 175);  // #1E40AF (8.36:1)
            else if (isHovered) fillCol = RGB(37, 99, 235);  // #2563EB (5.17:1)
            else                fillCol = RGB(29, 78, 216);  // #1D4ED8 (6.70:1)
            borderCol = theme.isDarkMode ? RGB(96, 165, 250) : RGB(30, 58, 138);
            textCol   = RGB(255, 255, 255);
            borderPx  = 1.5f;
        }
    } else {
        if (!isEnabled) {
            fillCol   = theme.isDarkMode ? RGB(20, 29, 48)   : RGB(241, 245, 249);
            borderCol = theme.isDarkMode ? RGB(51, 65, 85)   : RGB(203, 213, 225);
            textCol   = theme.isDarkMode ? RGB(100, 116, 139): RGB(148, 163, 184);
        } else if (theme.isDarkMode) {
            // Elevated Dark Slate-Navy (`#212D48` with `#F8FAFC` text = 12.8:1 WCAG AAA!)
            if (isPressed)      fillCol = RGB(22, 31, 52);
            else if (isHovered) fillCol = RGB(45, 61, 96);
            else if (activeTab) fillCol = RGB(30, 48, 86);
            else                fillCol = RGB(33, 45, 72);
            borderCol = isHovered ? RGB(125, 211, 252) : (activeTab ? theme.accentPrimary : RGB(75, 93, 128));
            textCol   = RGB(248, 250, 252);
            if (activeTab) borderPx = 1.8f;
        } else {
            if (isPressed)      fillCol = RGB(226, 232, 240);
            else if (isHovered) fillCol = RGB(241, 245, 249);
            else if (activeTab) fillCol = RGB(239, 246, 255);
            else                fillCol = RGB(255, 255, 255);
            borderCol = (isHovered || activeTab) ? RGB(37, 99, 235) : RGB(148, 163, 184);
            textCol   = RGB(15, 23, 42);
            if (activeTab) borderPx = 1.8f;
        }
    }

    Gdiplus::SolidBrush fillBr(ToGpColor(fillCol));
    Gdiplus::Pen borderPen(ToGpColor(borderCol), borderPx);
    FillAndStrokeRoundRect(g, 1.0f, 1.0f, w - 2.0f, h - 2.0f, rad, &fillBr, &borderPen);

    // WCAG 2.4.11 / 2.4.13 High-Contrast Keyboard Focus Ring (`#FBBF24` Dark / `#1D4ED8` Light)
    if (hasFocus) {
        const COLORREF focusCol = theme.isHighContrast
            ? ::GetSysColor(COLOR_HIGHLIGHT)
            : (theme.isDarkMode ? brand::kWarmGold : (isPrimaryAccent ? RGB(255, 255, 255) : RGB(29, 78, 216)));
        Gdiplus::Pen focusPen(ToGpColor(focusCol), 2.2f);
        FillAndStrokeRoundRect(g, 2.5f, 2.5f, w - 5.0f, h - 5.0f, std::max(2.0f, rad - 1.5f), nullptr, &focusPen);
    }

    const int fontWeight = (isPrimaryAccent || activeTab) ? FW_BOLD : FW_SEMIBOLD;
    HFONT hBtnFont = CreateScaledFont(L"Segoe UI", 9, fontWeight, dpi, theme.textScalePercent);
    ::SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ oldF = ::SelectObject(hdc, hBtnFont);
    ::SetTextColor(hdc, textCol);

    RECT txtRc{S(6), 0, static_cast<int>(w) - S(6), static_cast<int>(h)};
    if (showUacShield && hShieldIcon && isEnabled) {
        const int icoSz = S(16);
        RECT measRc{0, 0, static_cast<int>(w), static_cast<int>(h)};
        ::DrawTextW(hdc, labelWithMnemonic.data(), static_cast<int>(labelWithMnemonic.size()), &measRc, DT_SINGLELINE | DT_CALCRECT);
        const int textW  = measRc.right - measRc.left;
        const int totalW = icoSz + S(6) + textW;
        const int startX = std::max(S(6), (static_cast<int>(w) - totalW) / 2);
        const int icoY   = (static_cast<int>(h) - icoSz) / 2;
        ::DrawIconEx(hdc, startX, icoY, hShieldIcon, icoSz, icoSz, 0, nullptr, DI_NORMAL);
        txtRc.left  = startX + icoSz + S(6);
        txtRc.right = std::min<LONG>(static_cast<LONG>(w) - S(4), txtRc.left + textW + S(6));
        ::DrawTextW(hdc, labelWithMnemonic.data(), static_cast<int>(labelWithMnemonic.size()), &txtRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    } else {
        ::DrawTextW(hdc, labelWithMnemonic.data(), static_cast<int>(labelWithMnemonic.size()), &txtRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    ::SelectObject(hdc, oldF);
    ::DeleteObject(hBtnFont);
}

namespace detail {

struct SharedCheckboxSubclassState {
    const RswTheme* themePtr      = nullptr;
    bool            onCardSurface = false;
    bool            isHovered     = false;
};

inline LRESULT CALLBACK SharedCheckboxSubclassProc(
    HWND hWnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR uIdSubclass,
    DWORD_PTR dwRefData) noexcept
{
    auto* st = reinterpret_cast<SharedCheckboxSubclassState*>(dwRefData);
    if (!st || !st->themePtr) {
        return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    switch (uMsg) {
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hWnd, &SharedCheckboxSubclassProc, uIdSubclass);
            delete st;
            return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);

        case WM_ERASEBKGND:
            return 1;

        case WM_MOUSEMOVE:
            if (!st->isHovered) {
                st->isHovered = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hWnd, 0};
                ::TrackMouseEvent(&tme);
                ::InvalidateRect(hWnd, nullptr, FALSE);
            }
            break;

        case WM_MOUSELEAVE:
            st->isHovered = false;
            ::InvalidateRect(hWnd, nullptr, FALSE);
            break;

        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case BM_SETCHECK: {
            const LRESULT res = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            ::InvalidateRect(hWnd, nullptr, FALSE);
            return res;
        }

        case WM_LBUTTONUP:
        case WM_KEYUP: {
            const LRESULT res = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            ::InvalidateRect(hWnd, nullptr, FALSE);
            return res;
        }

        case WM_PAINT:
        case WM_PRINTCLIENT: {
            RECT rc{};
            ::GetClientRect(hWnd, &rc);
            const int w = std::max<int>(1, rc.right - rc.left);
            const int h = std::max<int>(1, rc.bottom - rc.top);

            PAINTSTRUCT ps{};
            HDC targetDc = (uMsg == WM_PRINTCLIENT) ? reinterpret_cast<HDC>(wParam) : ::BeginPaint(hWnd, &ps);
            HDC memDc    = ::CreateCompatibleDC(targetDc);
            HBITMAP bmp  = ::CreateCompatibleBitmap(targetDc, w, h);
            HGDIOBJ oldB = ::SelectObject(memDc, bmp);

            wchar_t txtBuf[512]{};
            ::GetWindowTextW(hWnd, txtBuf, static_cast<int>(std::size(txtBuf)));
            const bool checked  = (::SendMessageW(hWnd, BM_GETCHECK, 0, 0) == BST_CHECKED);
            const bool hasFocus = (::GetFocus() == hWnd);
            const UINT dpi      = ::GetDpiForWindow(hWnd);

            {
                Gdiplus::Graphics g(memDc);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
                DrawAccessibleCheckbox(
                    memDc, g, RECT{0, 0, w, h}, dpi, *st->themePtr,
                    txtBuf, checked, hasFocus, st->isHovered, st->onCardSurface);
            }

            ::BitBlt(targetDc, 0, 0, w, h, memDc, 0, 0, SRCCOPY);
            ::SelectObject(memDc, oldB);
            ::DeleteObject(bmp);
            ::DeleteDC(memDc);
            if (uMsg == WM_PAINT) {
                ::EndPaint(hWnd, &ps);
            }
            return 0;
        }
    }
    return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

/// Custom-paints `SysHeader32` (including any unoccupied trailing strip to the right of the last column!)
/// so `SysListView32` report grids NEVER show blinding white header bars or black-on-dark text in Dark Mode!
inline LRESULT CALLBACK SharedHeaderSubclassProc(
    HWND hHdr,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR uIdSubclass,
    DWORD_PTR dwRefData) noexcept
{
    const auto* themePtr = reinterpret_cast<const RswTheme*>(dwRefData);
    if (!themePtr) {
        return ::DefSubclassProc(hHdr, uMsg, wParam, lParam);
    }

    switch (uMsg) {
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hHdr, &SharedHeaderSubclassProc, uIdSubclass);
            return ::DefSubclassProc(hHdr, uMsg, wParam, lParam);

        case WM_ERASEBKGND:
            return 1;

        case WM_PAINT:
        case WM_PRINTCLIENT: {
            RECT rc{};
            ::GetClientRect(hHdr, &rc);
            const int w = std::max<int>(1, rc.right - rc.left);
            const int h = std::max<int>(1, rc.bottom - rc.top);

            PAINTSTRUCT ps{};
            HDC targetDc = (uMsg == WM_PRINTCLIENT) ? reinterpret_cast<HDC>(wParam) : ::BeginPaint(hHdr, &ps);
            HDC memDc    = ::CreateCompatibleDC(targetDc);
            HBITMAP bmp  = ::CreateCompatibleBitmap(targetDc, w, h);
            HGDIOBJ oldB = ::SelectObject(memDc, bmp);

            const RswTheme& th = *themePtr;
            const COLORREF hdrBg = th.isHighContrast
                ? ::GetSysColor(COLOR_BTNFACE)
                : (th.isDarkMode ? RGB(11, 19, 43) : RGB(226, 232, 240));
            const COLORREF hdrFg = th.isHighContrast
                ? ::GetSysColor(COLOR_BTNTEXT)
                : (th.isDarkMode ? RGB(248, 250, 252) : RGB(15, 23, 42));
            const COLORREF hdrDiv = th.isHighContrast
                ? ::GetSysColor(COLOR_WINDOWFRAME)
                : (th.isDarkMode ? RGB(51, 65, 85) : RGB(148, 163, 184));

            HBRUSH bgBrush = ::CreateSolidBrush(hdrBg);
            RECT fullRc{0, 0, w, h};
            ::FillRect(memDc, &fullRc, bgBrush);
            ::DeleteObject(bgBrush);

            HPEN divPen = ::CreatePen(PS_SOLID, 1, hdrDiv);
            HGDIOBJ oldPen = ::SelectObject(memDc, divPen);
            ::MoveToEx(memDc, 0, h - 1, nullptr);
            ::LineTo(memDc, w, h - 1);

            const UINT dpi   = ::GetDpiForWindow(hHdr);
            const int padX   = ScalePx(8, dpi, th.textScalePercent);
            HFONT hHdrFont   = CreateScaledFont(L"Segoe UI", 9, FW_SEMIBOLD, dpi, th.textScalePercent);
            HGDIOBJ oldFont  = ::SelectObject(memDc, hHdrFont);
            ::SetBkMode(memDc, TRANSPARENT);
            ::SetTextColor(memDc, hdrFg);

            const int colCount = Header_GetItemCount(hHdr);
            for (int i = 0; i < colCount; ++i) {
                RECT itemRc{};
                if (!Header_GetItemRect(hHdr, i, &itemRc)) continue;

                wchar_t colText[256]{};
                HDITEMW hdi{};
                hdi.mask       = HDI_TEXT | HDI_FORMAT;
                hdi.pszText    = colText;
                hdi.cchTextMax = static_cast<int>(std::size(colText));
                Header_GetItem(hHdr, i, &hdi);

                UINT align = DT_LEFT;
                if ((hdi.fmt & HDF_JUSTIFYMASK) == HDF_RIGHT)       align = DT_RIGHT;
                else if ((hdi.fmt & HDF_JUSTIFYMASK) == HDF_CENTER) align = DT_CENTER;

                RECT txtRc{itemRc.left + padX, itemRc.top, std::max(itemRc.left + padX, itemRc.right - padX), itemRc.bottom - 1};
                ::DrawTextW(memDc, colText, -1, &txtRc, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);

                ::MoveToEx(memDc, itemRc.right - 1, itemRc.top + 4, nullptr);
                ::LineTo(memDc, itemRc.right - 1, itemRc.bottom - 4);
            }

            ::SelectObject(memDc, oldFont);
            ::DeleteObject(hHdrFont);
            ::SelectObject(memDc, oldPen);
            ::DeleteObject(divPen);

            ::BitBlt(targetDc, 0, 0, w, h, memDc, 0, 0, SRCCOPY);
            ::SelectObject(memDc, oldB);
            ::DeleteObject(bmp);
            ::DeleteDC(memDc);
            if (uMsg == WM_PAINT) {
                ::EndPaint(hHdr, &ps);
            }
            return 0;
        }
    }
    return ::DefSubclassProc(hHdr, uMsg, wParam, lParam);
}

inline void EnsureListViewHeaderSubclassed(HWND hListView, const RswTheme* themePtr) noexcept {
    if (!hListView || !themePtr) return;
    HWND hHdr = ListView_GetHeader(hListView);
    if (hHdr) {
        ::SetWindowSubclass(hHdr, &SharedHeaderSubclassProc, 0x52534844u, reinterpret_cast<DWORD_PTR>(themePtr));
        ::InvalidateRect(hHdr, nullptr, TRUE);
    }
}

inline LRESULT CALLBACK SharedListViewSubclassProc(
    HWND hListView,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR uIdSubclass,
    DWORD_PTR dwRefData) noexcept
{
    const auto* themePtr = reinterpret_cast<const RswTheme*>(dwRefData);
    if (uMsg == WM_NCDESTROY) {
        ::RemoveWindowSubclass(hListView, &SharedListViewSubclassProc, uIdSubclass);
        return ::DefSubclassProc(hListView, uMsg, wParam, lParam);
    }
    const LRESULT res = ::DefSubclassProc(hListView, uMsg, wParam, lParam);
    if (uMsg == LVM_INSERTCOLUMNW || uMsg == LVM_INSERTCOLUMNA || uMsg == WM_STYLECHANGED) {
        EnsureListViewHeaderSubclassed(hListView, themePtr);
    }
    return res;
}

} // namespace detail

/// Subclasses any Win32 `BS_AUTOCHECKBOX` so its checkmark, focus ring, and label text maintain
/// `>= 15.8:1` WCAG AAA contrast in Dark Mode, Light Mode, and Windows High Contrast!
inline void AttachAccessibleCheckboxSubclass(HWND hCheckbox, const RswTheme* themePtr, bool onCardSurface = false) noexcept {
    if (!hCheckbox || !themePtr) return;
    auto* st = new detail::SharedCheckboxSubclassState{themePtr, onCardSurface, false};
    if (!::SetWindowSubclass(hCheckbox, &detail::SharedCheckboxSubclassProc, 0x5253434Bu, reinterpret_cast<DWORD_PTR>(st))) {
        delete st;
    }
}

/// Subclasses any `SysListView32` (`LVS_REPORT`) and its child `SysHeader32` so column headers,
/// trailing header space, and scrollbars render cleanly in Dark Mode, Light Mode, and High Contrast!
inline void AttachDarkAwareListViewSubclass(HWND hListView, const RswTheme* themePtr) noexcept {
    if (!hListView || !themePtr) return;
    ApplyDarkScrollbarTheme(hListView, *themePtr);
    ::SetWindowSubclass(hListView, &detail::SharedListViewSubclassProc, 0x52534C56u, reinterpret_cast<DWORD_PTR>(themePtr));
    detail::EnsureListViewHeaderSubclassed(hListView, themePtr);
}

namespace detail {

struct SharedPushButtonSubclassState {
    const RswTheme* themePtr        = nullptr;
    bool            isPrimaryAccent = false;
    bool            onFooterSurface = false;
    bool            isHovered       = false;
    bool            showUacShield   = false;
    HICON           hShieldIcon     = nullptr;

    ~SharedPushButtonSubclassState() {
        if (hShieldIcon) {
            ::DestroyIcon(hShieldIcon);
            hShieldIcon = nullptr;
        }
    }
};

inline LRESULT CALLBACK SharedPushButtonSubclassProc(
    HWND hWnd,
    UINT uMsg,
    WPARAM wParam,
    LPARAM lParam,
    UINT_PTR uIdSubclass,
    DWORD_PTR dwRefData) noexcept
{
    auto* st = reinterpret_cast<SharedPushButtonSubclassState*>(dwRefData);
    if (!st || !st->themePtr) {
        return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
    }

    switch (uMsg) {
        case WM_NCDESTROY:
            ::RemoveWindowSubclass(hWnd, &SharedPushButtonSubclassProc, uIdSubclass);
            delete st;
            return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);

        case WM_ERASEBKGND:
            return 1;

        case BCM_SETSHIELD: {
            const bool wantShield = (lParam != 0);
            st->showUacShield = wantShield;
            if (wantShield && !st->hShieldIcon) {
                SHSTOCKICONINFO sii{};
                sii.cbSize = sizeof(sii);
                if (SUCCEEDED(::SHGetStockIconInfo(SIID_SHIELD, SHGSI_ICON | SHGSI_SMALLICON, &sii))) {
                    st->hShieldIcon = sii.hIcon;
                }
            }
            ::InvalidateRect(hWnd, nullptr, FALSE);
            return 0;
        }

        case WM_MOUSEMOVE:
            if (!st->isHovered) {
                st->isHovered = true;
                TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, hWnd, 0};
                ::TrackMouseEvent(&tme);
                ::InvalidateRect(hWnd, nullptr, FALSE);
            }
            break;

        case WM_MOUSELEAVE:
            st->isHovered = false;
            ::InvalidateRect(hWnd, nullptr, FALSE);
            break;

        case WM_SETTEXT:
        case WM_ENABLE:
        case BM_SETSTATE:
        case BM_SETSTYLE: {
            // Suppress `ComCtl32`'s internal GDI flash during text/enable/state updates!
            ::SendMessageW(hWnd, WM_SETREDRAW, FALSE, 0);
            const LRESULT res = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            ::SendMessageW(hWnd, WM_SETREDRAW, TRUE, 0);
            ::InvalidateRect(hWnd, nullptr, FALSE);
            return res;
        }

        case WM_SETFOCUS:
        case WM_KILLFOCUS:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_KEYDOWN:
        case WM_KEYUP: {
            const LRESULT res = ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
            ::InvalidateRect(hWnd, nullptr, FALSE);
            return res;
        }

        case WM_PAINT:
        case WM_PRINTCLIENT: {
            RECT rc{};
            ::GetClientRect(hWnd, &rc);
            const int w = std::max<int>(1, rc.right - rc.left);
            const int h = std::max<int>(1, rc.bottom - rc.top);

            PAINTSTRUCT ps{};
            HDC targetDc = (uMsg == WM_PRINTCLIENT) ? reinterpret_cast<HDC>(wParam) : ::BeginPaint(hWnd, &ps);
            HDC memDc    = ::CreateCompatibleDC(targetDc);
            HBITMAP bmp  = ::CreateCompatibleBitmap(targetDc, w, h);
            HGDIOBJ oldB = ::SelectObject(memDc, bmp);

            wchar_t txtBuf[256]{};
            ::GetWindowTextW(hWnd, txtBuf, static_cast<int>(std::size(txtBuf)));
            const bool isEnabled = (::IsWindowEnabled(hWnd) != FALSE);
            const bool hasFocus  = (::GetFocus() == hWnd);
            const UINT btnState  = static_cast<UINT>(::SendMessageW(hWnd, BM_GETSTATE, 0, 0));
            const bool isPressed = (btnState & BST_PUSHED) != 0;
            const DWORD styleLow = static_cast<DWORD>(::GetWindowLongW(hWnd, GWL_STYLE)) & 0x0Fu;
            const bool isPrimary = st->isPrimaryAccent || (styleLow == BS_DEFPUSHBUTTON);
            const UINT dpi       = ::GetDpiForWindow(hWnd);

            {
                Gdiplus::Graphics g(memDc);
                g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);
                DrawAccessiblePushButton(
                    memDc, g, RECT{0, 0, w, h}, dpi, *st->themePtr,
                    txtBuf, isPrimary, st->onFooterSurface, isEnabled, hasFocus,
                    st->isHovered, isPressed, st->showUacShield, st->hShieldIcon);
            }

            ::BitBlt(targetDc, 0, 0, w, h, memDc, 0, 0, SRCCOPY);
            ::SelectObject(memDc, oldB);
            ::DeleteObject(bmp);
            ::DeleteDC(memDc);
            if (uMsg == WM_PAINT) {
                ::EndPaint(hWnd, &ps);
            }
            return 0;
        }
    }
    return ::DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

} // namespace detail

/// Subclasses any Win32 `BS_PUSHBUTTON` / `BS_DEFPUSHBUTTON` so it renders in crisp Dark, Light, and
/// High-Contrast styles (with native UAC shield + UI Automation support and zero `ComCtl32` white flashes)!
inline void AttachAccessiblePushButtonSubclass(
    HWND hButton,
    const RswTheme* themePtr,
    bool isPrimaryAccent = false,
    bool onFooterSurface = false) noexcept
{
    if (!hButton || !themePtr) return;
    auto* st = new detail::SharedPushButtonSubclassState{themePtr, isPrimaryAccent, onFooterSurface, false, false, nullptr};
    if (!::SetWindowSubclass(hButton, &detail::SharedPushButtonSubclassProc, 0x52535042u, reinterpret_cast<DWORD_PTR>(st))) {
        delete st;
    }
}

// ================================================================================
// 32-BIT ARGB VECTOR `HIMAGELIST` & DARK-MODE `SysTreeView32` THEMING (`v0.4.2+`)
// ================================================================================
// 100-Level UI Lesson:
//   Why build a 32-bit premultiplied ARGB (`PixelFormat32bppPARGB` + `ILC_COLOR32`)
//   image list instead of classic 16-color bitmaps or opaque background fills?
//     When a user hovers or selects a row inside a Windows 11 `Explorer` /
//     `DarkMode_Explorer` `SysTreeView32`, `ComCtl32 v6` draws a rounded translucent
//     selection bar behind BOTH the icon AND the text. True per-pixel premultiplied
//     alpha (`AlphaBlend`) lets every vector icon float crisply on normal surfaces,
//     hover bars, and selected rows across Dark Mode, Light Mode, and High Contrast!
enum class StudioTreeIcon : int {
    CategoryFolder   = 0,  // Warm Gold open category folder (`Application Information`, `Setup Architecture`, etc.)
    SubFolder        = 1,  // Sky-accented destination / directory folder (`INSTALLDIR`, `Plugins`, etc.)
    ComputerRoot     = 2,  // Desktop monitor root (`Destination Computer`, `Target Registry`, `All Features`)
    PackageCube      = 3,  // 3D isometric cobalt/cyan cube (`Package & Identity`)
    FeatureBox       = 4,  // Stacked feature module with emerald badge (`Features`)
    ComponentGear    = 5,  // Violet precision component gear (`Components & KeyPaths`)
    FileLeaf         = 6,  // Crisp document sheet with folded corner (`Payload` / `File` / Help Topic)
    RegistryHive     = 7,  // Cyan-teal `RegEdit` hive cube cluster (`HKEY_CLASSES_ROOT` .. `HKEY_USERS`)
    RegistryKey      = 8,  // `RegEdit`-style amber key folder (`SOFTWARE\RaggieSoft\...`)
    RegistryValueSz  = 9,  // Crimson-rose `ab` string value badge (`REG_SZ` / `REG_EXPAND_SZ`)
    RegistryValueNum = 10, // Royal-cobalt `01` numeric value badge (`REG_DWORD`)
    ShortcutLink     = 11, // App tile with curved shortcut arrow (`Shortcuts`)
    FileExtDoc       = 12, // Document page with sky-blue `.EXT` ribbon (`File Associations`)
    VerbBolt         = 13, // Lightning bolt badge (`Pre-MSI Runtimes` & `Shell Verbs`)
    MergeModuleMsm   = 14, // Interlocking emerald/cobalt module block (`.MSM Merge Modules`)
    WizardWindow     = 15, // Mini window frame with cobalt header (`C++ Wizard Designer`)
    MsiDialogSql     = 16, // Mini `370x270` dialog form (`MSI.DLL Dialog Designer`)
    CertShield       = 17, // Emerald security shield with checkmark (`Signing, .MST & .MSP`)
    SourceCode       = 18, // Slate code badge with chevrons (`Lossless .RSIP Source`)
    SqlTable         = 19, // Relational 3x3 SQL table grid (`msiprism.exe` SQL tables)
    Count            = 20
};

/// Paints one DPI-scaled vector tree icon inside `[ox .. ox + s, 0 .. s]` on a transparent PARGB canvas.
inline void PaintSingleStudioTreeIcon(
    Gdiplus::Graphics& g,
    StudioTreeIcon kind,
    float ox,
    float s,
    const RswTheme& theme) noexcept
{
    const float cx = ox + s * 0.50f;
    const float cy = s * 0.50f;
    const float r  = s * 0.42f;
    const Gdiplus::Color white(255, 248, 250, 252);
    const Gdiplus::Color slateDark(255, 15, 23, 42);
    const Gdiplus::Color rimCol = theme.isDarkMode
        ? Gdiplus::Color(230, 226, 232, 240)
        : Gdiplus::Color(220, 30, 41, 59);

    switch (kind) {
        case StudioTreeIcon::CategoryFolder:
        case StudioTreeIcon::RegistryKey: {
            const Gdiplus::Color tabCol  = (kind == StudioTreeIcon::RegistryKey)
                ? Gdiplus::Color(255, 217, 119, 6)
                : Gdiplus::Color(255, 245, 158, 11);
            const Gdiplus::Color bodyCol = Gdiplus::Color(255, 251, 191, 36);
            Gdiplus::SolidBrush tabBr(tabCol);
            Gdiplus::SolidBrush bodyBr(bodyCol);
            Gdiplus::Pen pen(Gdiplus::Color(220, 146, 64, 14), 1.0f);
            FillAndStrokeRoundRect(g, ox + s * 0.12f, s * 0.18f, s * 0.38f, s * 0.22f, s * 0.06f, &tabBr);
            FillAndStrokeRoundRect(g, ox + s * 0.10f, s * 0.30f, s * 0.80f, s * 0.54f, s * 0.10f, &bodyBr, &pen);
            if (kind == StudioTreeIcon::RegistryKey) {
                Gdiplus::SolidBrush cyanDot(ToGpColor(brand::kRaggieBlue));
                g.FillEllipse(&cyanDot, ox + s * 0.56f, s * 0.46f, s * 0.22f, s * 0.22f);
            }
            break;
        }
        case StudioTreeIcon::SubFolder: {
            Gdiplus::SolidBrush tabBr(ToGpColor(brand::kRaggieBlue));
            Gdiplus::SolidBrush bodyBr(ToGpColor(brand::kSkyCyan));
            Gdiplus::Pen pen(Gdiplus::Color(220, 30, 64, 175), 1.0f);
            FillAndStrokeRoundRect(g, ox + s * 0.12f, s * 0.20f, s * 0.36f, s * 0.20f, s * 0.06f, &tabBr);
            FillAndStrokeRoundRect(g, ox + s * 0.10f, s * 0.32f, s * 0.80f, s * 0.52f, s * 0.10f, &bodyBr, &pen);
            break;
        }
        case StudioTreeIcon::ComputerRoot: {
            Gdiplus::SolidBrush bezelBr(Gdiplus::Color(255, 30, 41, 59));
            Gdiplus::SolidBrush scrBr(ToGpColor(brand::kSkyCyan));
            Gdiplus::Pen pen(rimCol, 1.1f);
            FillAndStrokeRoundRect(g, ox + s * 0.12f, s * 0.14f, s * 0.76f, s * 0.54f, s * 0.08f, &bezelBr, &pen);
            FillAndStrokeRoundRect(g, ox + s * 0.18f, s * 0.20f, s * 0.64f, s * 0.40f, s * 0.04f, &scrBr);
            g.DrawLine(&pen, cx, s * 0.68f, cx, s * 0.84f);
            g.DrawLine(&pen, ox + s * 0.28f, s * 0.84f, ox + s * 0.72f, s * 0.84f);
            break;
        }
        case StudioTreeIcon::PackageCube:
            DrawVectorGlyph(g, GlassGlyph::Cube, cx, cy, r * 1.05f, rimCol, ToGpColor(brand::kSkyCyan));
            break;
        case StudioTreeIcon::FeatureBox: {
            Gdiplus::SolidBrush b0(ToGpColor(brand::kRaggieBlue));
            Gdiplus::SolidBrush b1(ToGpColor(brand::kEmeraldOk));
            Gdiplus::Pen pen(white, 1.0f);
            FillAndStrokeRoundRect(g, ox + s * 0.12f, s * 0.16f, s * 0.54f, s * 0.54f, s * 0.10f, &b0, &pen);
            FillAndStrokeRoundRect(g, ox + s * 0.36f, s * 0.34f, s * 0.52f, s * 0.52f, s * 0.10f, &b1, &pen);
            break;
        }
        case StudioTreeIcon::ComponentGear:
            DrawVectorGlyph(g, GlassGlyph::Gear, cx, cy, r * 1.02f, ToGpColor(brand::kVioletTune), ToGpColor(brand::kWarmGold));
            break;
        case StudioTreeIcon::FileLeaf:
        case StudioTreeIcon::FileExtDoc: {
            Gdiplus::SolidBrush docBr(Gdiplus::Color(255, 241, 245, 249));
            Gdiplus::Pen docPen(Gdiplus::Color(255, 100, 116, 139), 1.0f);
            FillAndStrokeRoundRect(g, ox + s * 0.18f, s * 0.12f, s * 0.64f, s * 0.76f, s * 0.08f, &docBr, &docPen);
            Gdiplus::SolidBrush stripeBr(ToGpColor((kind == StudioTreeIcon::FileExtDoc) ? brand::kRaggieBlue : brand::kSkyCyan));
            FillAndStrokeRoundRect(g, ox + s * 0.24f, (kind == StudioTreeIcon::FileExtDoc) ? s * 0.50f : s * 0.28f,
                                   s * 0.52f, s * 0.22f, s * 0.05f, &stripeBr);
            break;
        }
        case StudioTreeIcon::RegistryHive: {
            // Classic Registry Editor teal/cyan 3-block cluster!
            Gdiplus::SolidBrush c1(Gdiplus::Color(255, 14, 165, 233));
            Gdiplus::SolidBrush c2(Gdiplus::Color(255, 6, 182, 212));
            Gdiplus::SolidBrush c3(Gdiplus::Color(255, 37, 99, 235));
            Gdiplus::Pen pen(white, 0.9f);
            FillAndStrokeRoundRect(g, ox + s * 0.14f, s * 0.44f, s * 0.34f, s * 0.34f, s * 0.06f, &c1, &pen);
            FillAndStrokeRoundRect(g, ox + s * 0.50f, s * 0.48f, s * 0.34f, s * 0.34f, s * 0.06f, &c2, &pen);
            FillAndStrokeRoundRect(g, ox + s * 0.32f, s * 0.14f, s * 0.34f, s * 0.34f, s * 0.06f, &c3, &pen);
            break;
        }
        case StudioTreeIcon::RegistryValueSz:
        case StudioTreeIcon::RegistryValueNum: {
            const bool isNum = (kind == StudioTreeIcon::RegistryValueNum);
            Gdiplus::SolidBrush bBr(ToGpColor(isNum ? brand::kRaggieBlue : brand::kCoralRose));
            Gdiplus::Pen bPen(white, 0.9f);
            FillAndStrokeRoundRect(g, ox + s * 0.10f, s * 0.22f, s * 0.80f, s * 0.56f, s * 0.12f, &bBr, &bPen);
            Gdiplus::Pen glyphPen(white, std::max(1.3f, s * 0.08f));
            if (isNum) {
                g.DrawEllipse(&glyphPen, ox + s * 0.22f, s * 0.34f, s * 0.22f, s * 0.32f);
                g.DrawLine(&glyphPen, ox + s * 0.64f, s * 0.33f, ox + s * 0.64f, s * 0.67f);
            } else {
                g.DrawLine(&glyphPen, ox + s * 0.24f, s * 0.64f, ox + s * 0.36f, s * 0.36f);
                g.DrawLine(&glyphPen, ox + s * 0.36f, s * 0.36f, ox + s * 0.48f, s * 0.64f);
                g.DrawLine(&glyphPen, ox + s * 0.60f, s * 0.34f, ox + s * 0.60f, s * 0.66f);
                g.DrawEllipse(&glyphPen, ox + s * 0.60f, s * 0.46f, s * 0.16f, s * 0.18f);
            }
            break;
        }
        case StudioTreeIcon::ShortcutLink: {
            Gdiplus::SolidBrush tileBr(ToGpColor(brand::kRaggieBlue));
            Gdiplus::Pen rimPen(white, 1.0f);
            FillAndStrokeRoundRect(g, ox + s * 0.14f, s * 0.16f, s * 0.72f, s * 0.68f, s * 0.14f, &tileBr, &rimPen);
            Gdiplus::Pen arrPen(ToGpColor(brand::kWarmGold), std::max(1.6f, s * 0.10f));
            arrPen.SetEndCap(Gdiplus::LineCapArrowAnchor);
            g.DrawLine(&arrPen, ox + s * 0.30f, s * 0.66f, ox + s * 0.68f, s * 0.32f);
            break;
        }
        case StudioTreeIcon::VerbBolt:
            DrawVectorGlyph(g, GlassGlyph::Bolt, cx, cy, r * 1.02f, slateDark, ToGpColor(brand::kWarmGold));
            break;
        case StudioTreeIcon::MergeModuleMsm: {
            Gdiplus::SolidBrush m1(ToGpColor(brand::kEmeraldOk));
            Gdiplus::SolidBrush m2(ToGpColor(brand::kVioletTune));
            Gdiplus::Pen pen(white, 0.9f);
            FillAndStrokeRoundRect(g, ox + s * 0.12f, s * 0.22f, s * 0.42f, s * 0.56f, s * 0.08f, &m1, &pen);
            FillAndStrokeRoundRect(g, ox + s * 0.46f, s * 0.22f, s * 0.42f, s * 0.56f, s * 0.08f, &m2, &pen);
            g.FillEllipse(&m1, ox + s * 0.44f, s * 0.38f, s * 0.20f, s * 0.24f);
            break;
        }
        case StudioTreeIcon::WizardWindow:
        case StudioTreeIcon::MsiDialogSql: {
            const bool isMsi = (kind == StudioTreeIcon::MsiDialogSql);
            Gdiplus::SolidBrush winBr(Gdiplus::Color(255, 241, 245, 249));
            Gdiplus::SolidBrush capBr(ToGpColor(isMsi ? brand::kVioletTune : brand::kRaggieBlue));
            Gdiplus::Pen pen(Gdiplus::Color(255, 71, 85, 105), 1.0f);
            FillAndStrokeRoundRect(g, ox + s * 0.10f, s * 0.16f, s * 0.80f, s * 0.68f, s * 0.09f, &winBr, &pen);
            FillAndStrokeRoundRect(g, ox + s * 0.10f, s * 0.16f, s * 0.80f, s * 0.22f, s * 0.07f, &capBr);
            Gdiplus::SolidBrush ctaBr(ToGpColor(isMsi ? brand::kWarmGold : brand::kEmeraldOk));
            FillAndStrokeRoundRect(g, ox + s * 0.52f, s * 0.60f, s * 0.28f, s * 0.14f, s * 0.04f, &ctaBr);
            break;
        }
        case StudioTreeIcon::CertShield: {
            Gdiplus::PointF pts[5] = {
                {cx,             s * 0.12f},
                {ox + s * 0.84f, s * 0.26f},
                {ox + s * 0.78f, s * 0.62f},
                {cx,             s * 0.88f},
                {ox + s * 0.22f, s * 0.62f}
            };
            Gdiplus::PointF shield[6] = {pts[0], pts[1], pts[2], pts[3], pts[4], {ox + s * 0.16f, s * 0.26f}};
            Gdiplus::SolidBrush shBr(ToGpColor(brand::kEmeraldOk));
            Gdiplus::Pen shPen(white, 1.0f);
            g.FillPolygon(&shBr, shield, 6);
            g.DrawPolygon(&shPen, shield, 6);
            DrawVectorGlyph(g, GlassGlyph::ShieldCheck, cx, cy + s * 0.02f, r * 0.68f, white, white);
            break;
        }
        case StudioTreeIcon::SourceCode: {
            Gdiplus::SolidBrush bgBr(Gdiplus::Color(255, 15, 23, 42));
            Gdiplus::Pen rim(ToGpColor(brand::kSkyCyan), 1.0f);
            FillAndStrokeRoundRect(g, ox + s * 0.10f, s * 0.16f, s * 0.80f, s * 0.68f, s * 0.10f, &bgBr, &rim);
            Gdiplus::Pen codePen(ToGpColor(brand::kWarmGold), std::max(1.4f, s * 0.08f));
            g.DrawLine(&codePen, ox + s * 0.38f, s * 0.36f, ox + s * 0.24f, cy);
            g.DrawLine(&codePen, ox + s * 0.24f, cy, ox + s * 0.38f, s * 0.64f);
            g.DrawLine(&codePen, ox + s * 0.62f, s * 0.36f, ox + s * 0.76f, cy);
            g.DrawLine(&codePen, ox + s * 0.76f, cy, ox + s * 0.62f, s * 0.64f);
            break;
        }
        case StudioTreeIcon::SqlTable:
        default: {
            Gdiplus::SolidBrush tblBr(Gdiplus::Color(255, 248, 250, 252));
            Gdiplus::SolidBrush hdrBr(ToGpColor(brand::kRaggieBlue));
            Gdiplus::Pen gridPen(Gdiplus::Color(255, 100, 116, 139), 1.0f);
            FillAndStrokeRoundRect(g, ox + s * 0.12f, s * 0.16f, s * 0.76f, s * 0.68f, s * 0.07f, &tblBr, &gridPen);
            FillAndStrokeRoundRect(g, ox + s * 0.12f, s * 0.16f, s * 0.76f, s * 0.22f, s * 0.05f, &hdrBr);
            g.DrawLine(&gridPen, ox + s * 0.42f, s * 0.38f, ox + s * 0.42f, s * 0.84f);
            g.DrawLine(&gridPen, ox + s * 0.12f, s * 0.58f, ox + s * 0.88f, s * 0.58f);
            break;
        }
    }
}

/// Builds a Per-Monitor V2 DPI-scaled 32-bpp premultiplied ARGB `HIMAGELIST` (`TVSIL_NORMAL`)
/// containing all `StudioTreeIcon` vector glyphs with zero selection/hover halo!
inline HIMAGELIST CreateStudioTreeImageList(UINT dpi, const RswTheme& theme) noexcept {
    const int sz    = std::max(16, ScalePx(18, dpi, theme.textScalePercent));
    const int count = static_cast<int>(StudioTreeIcon::Count);
    const int totalW = sz * count;

    Gdiplus::Bitmap sheet(totalW, sz, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(&sheet);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        g.Clear(Gdiplus::Color(0, 0, 0, 0));
        for (int i = 0; i < count; ++i) {
            PaintSingleStudioTreeIcon(g, static_cast<StudioTreeIcon>(i), static_cast<float>(i * sz), static_cast<float>(sz), theme);
        }
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = totalW;
    bmi.bmiHeader.biHeight      = -sz; // Top-down 32-bpp PARGB DIB
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HDC screenDc = ::GetDC(nullptr);
    HBITMAP hDib = ::CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    ::ReleaseDC(nullptr, screenDc);
    if (!hDib || !pvBits) {
        if (hDib) ::DeleteObject(hDib);
        return nullptr;
    }

    Gdiplus::Rect lockRc(0, 0, totalW, sz);
    Gdiplus::BitmapData bd{};
    if (sheet.LockBits(&lockRc, Gdiplus::ImageLockModeRead, PixelFormat32bppPARGB, &bd) == Gdiplus::Ok) {
        const std::size_t rowBytes = static_cast<std::size_t>(totalW) * 4u;
        for (int y = 0; y < sz; ++y) {
            const auto* srcRow = static_cast<const std::uint8_t*>(bd.Scan0) + static_cast<std::ptrdiff_t>(y) * bd.Stride;
            auto* dstRow       = static_cast<std::uint8_t*>(pvBits) + static_cast<std::size_t>(y) * rowBytes;
            std::memcpy(dstRow, srcRow, rowBytes);
        }
        sheet.UnlockBits(&bd);
    }

    HIMAGELIST hIml = ::ImageList_Create(sz, sz, ILC_COLOR32, count, 0);
    if (hIml) {
        ::ImageList_Add(hIml, hDib, nullptr);
    }
    ::DeleteObject(hDib);
    return hIml;
}

/// Applies Windows 11 `DarkMode_Explorer` / `Explorer` expand/collapse chevrons, dark scrollbars,
/// surface colors, connector lines, and accessible 24px row height to any `SysTreeView32`!
inline void ApplyDarkTreeViewTheme(HWND hTree, const RswTheme& theme, UINT dpi) noexcept {
    if (!hTree) return;
    if (theme.isHighContrast) {
        ::SetWindowTheme(hTree, L"", L"");
    } else {
        ::SetWindowTheme(hTree, theme.isDarkMode ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    }
    TreeView_SetBkColor(hTree, theme.cardBg);
    TreeView_SetTextColor(hTree, theme.textPrimary);
    ::SendMessageW(hTree, TVM_SETLINECOLOR, 0, static_cast<LPARAM>(theme.borderSubtle));
    const int rowH = std::max(22, ScalePx(24, dpi, theme.textScalePercent));
    ::SendMessageW(hTree, TVM_SETITEMHEIGHT, static_cast<WPARAM>(rowH), 0);
}

/// Encodes a 32-bpp BGRA pixel buffer (`width x height`, stride `width * 4`) to a `.png` file via OS WIC!
inline bool SaveBgraPixelsToPngFile(
    const std::uint8_t* pixels,
    int width,
    int height,
    const std::filesystem::path& outPngPath) noexcept
{
    if (!pixels || width <= 0 || height <= 0) return false;
    std::error_code ec;
    if (outPngPath.has_parent_path()) {
        std::filesystem::create_directories(outPngPath.parent_path(), ec);
    }

    IWICImagingFactory* pFactory = nullptr;
    if (FAILED(::CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory))) || !pFactory) {
        return false;
    }

    IWICStream* pStream = nullptr;
    IWICBitmapEncoder* pEncoder = nullptr;
    IWICBitmapFrameEncode* pFrame = nullptr;
    IPropertyBag2* pProps = nullptr;
    bool ok = false;

    if (SUCCEEDED(pFactory->CreateStream(&pStream)) && pStream &&
        SUCCEEDED(pStream->InitializeFromFilename(outPngPath.wstring().c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(pFactory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &pEncoder)) && pEncoder &&
        SUCCEEDED(pEncoder->Initialize(pStream, WICBitmapEncoderNoCache)) &&
        SUCCEEDED(pEncoder->CreateNewFrame(&pFrame, &pProps)) && pFrame &&
        SUCCEEDED(pFrame->Initialize(pProps)) &&
        SUCCEEDED(pFrame->SetSize(static_cast<UINT>(width), static_cast<UINT>(height)))) {

        WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
        if (SUCCEEDED(pFrame->SetPixelFormat(&fmt))) {
            const UINT stride = static_cast<UINT>(width) * 4u;
            const UINT bufSz  = stride * static_cast<UINT>(height);
            if (SUCCEEDED(pFrame->WritePixels(static_cast<UINT>(height), stride, bufSz, const_cast<BYTE*>(pixels))) &&
                SUCCEEDED(pFrame->Commit()) &&
                SUCCEEDED(pEncoder->Commit())) {
                ok = true;
            }
        }
    }

    if (pProps)   pProps->Release();
    if (pFrame)   pFrame->Release();
    if (pEncoder) pEncoder->Release();
    if (pStream)  pStream->Release();
    pFactory->Release();
    return ok;
}

/// Pumps pending Win32 messages and captures `hWnd` (full window chrome + all child controls) to a `.png` file!
inline bool CaptureWindowToPngFile(HWND hWnd, const std::filesystem::path& outPngPath) noexcept {
    if (!hWnd || !::IsWindow(hWnd)) return false;

    ::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
    MSG msg{};
    for (int pass = 0; pass < 4; ++pass) {
        while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
        ::Sleep(25);
    }
    ::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);

    RECT winRc{};
    if (!::GetWindowRect(hWnd, &winRc)) return false;
    const int w = winRc.right - winRc.left;
    const int h = winRc.bottom - winRc.top;
    if (w <= 16 || h <= 16) return false;

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = w;
    bmi.bmiHeader.biHeight      = -h; // Top-down 32-bpp BGRA DIB
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pvBits = nullptr;
    HDC screenDc = ::GetDC(nullptr);
    HDC memDc    = ::CreateCompatibleDC(screenDc);
    HBITMAP hDib = ::CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
    ::ReleaseDC(nullptr, screenDc);
    if (!hDib || !pvBits) {
        if (hDib) ::DeleteObject(hDib);
        ::DeleteDC(memDc);
        return false;
    }

    HGDIOBJ oldBmp = ::SelectObject(memDc, hDib);
    // `PW_RENDERFULLCONTENT` (`0x00000002`) captures DWM window chrome + all child windows accurately!
    if (!::PrintWindow(hWnd, memDc, 0x00000002u)) {
        if (!::PrintWindow(hWnd, memDc, 0)) {
            ::SendMessageW(hWnd, WM_PRINT, reinterpret_cast<WPARAM>(memDc),
                           PRF_NONCLIENT | PRF_CLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
        }
    }
    ::GdiFlush();

    // Force alpha channel to `255` (`0xFF`) across all pixels because classic GDI text/controls leave the
    // 4th byte of 32-bit DIBs at `0x00`, which would otherwise appear transparent in 32-bpp RGBA PNGs!
    auto* bytes = static_cast<std::uint8_t*>(pvBits);
    const std::size_t totalPx = static_cast<std::size_t>(w) * static_cast<std::size_t>(h);
    for (std::size_t i = 0; i < totalPx; ++i) {
        bytes[i * 4 + 3] = 0xFF;
    }

    const bool saved = SaveBgraPixelsToPngFile(bytes, w, h, outPngPath);
    ::SelectObject(memDc, oldBmp);
    ::DeleteObject(hDib);
    ::DeleteDC(memDc);
    return saved;
}

} // namespace rsw::ui
