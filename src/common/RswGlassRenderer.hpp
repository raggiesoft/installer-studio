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
namespace Gdiplus {
    using std::min;
    using std::max;
}
#include <gdiplus.h>

#include <cmath>
#include <cwctype>
#include <string>
#include <string_view>

#pragma comment(lib, "gdiplus.lib")

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

} // namespace rsw::ui
