// ================================================================================
// FILE: src/common/RswWcagContrast.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What does this header do?
//     When a software publisher customizes their installer's brand palette
//     (`[UI] AccentPrimaryHex` and `AccentSecondaryHex`), picking a pastel yellow
//     for a white-text button or a dark navy border on a dark-mode card can
//     accidentally make buttons unreadable for low-vision users!
//
//     This header implements the official W3C **WCAG 2.2** relative luminance and
//     contrast-ratio formulas in pure zero-dependency C++20:
//       1. `RelativeLuminance(c)` : Linearizes sRGB (`0..255`) into `[0.0, 1.0]`.
//       2. `ContrastRatio(a, b)`  : Computes `(L_lighter + 0.05) / (L_darker + 0.05)`.
//       3. `SnapAccentToAccessibleBand(c)`:
//          Preserves the publisher's exact HSL brand hue and saturation while
//          adjusting lightness so the accent color simultaneously achieves:
//            - `>= 4.5:1` WCAG 2.2 AA contrast with crisp `#FFFFFF` button text
//            - `>= 4.5:1` WCAG 2.2 AA contrast against `#FFFFFF` Light Mode cards
//            - `>= 3.0:1` WCAG 2.2 AA SC 1.4.11 non-text contrast against `#15203A`
//              Dark Mode cards!
// ================================================================================

#pragma once

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace rsw::wcag {

/// Computes W3C WCAG 2.2 relative luminance `L` in `[0.0, 1.0]` for a Win32 `COLORREF`.
inline double RelativeLuminance(COLORREF c) noexcept {
    auto lin = [](int ch8) noexcept -> double {
        const double s = static_cast<double>(std::clamp(ch8, 0, 255)) / 255.0;
        return (s <= 0.04045) ? (s / 12.92) : std::pow((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * lin(GetRValue(c)) +
           0.7152 * lin(GetGValue(c)) +
           0.0722 * lin(GetBValue(c));
}

/// Computes the W3C WCAG 2.2 contrast ratio (`1.0` to `21.0`) between two `COLORREF` colors.
inline double ContrastRatio(COLORREF a, COLORREF b) noexcept {
    const double l1 = RelativeLuminance(a);
    const double l2 = RelativeLuminance(b);
    const double hi = (l1 > l2) ? l1 : l2;
    const double lo = (l1 > l2) ? l2 : l1;
    return (hi + 0.05) / (lo + 0.05);
}

/// Formats a Win32 `COLORREF` back into an uppercase `"#RRGGBB"` hex string for `.rsip` manifests.
inline std::string FormatHexColor(COLORREF c) {
    char buf[16]{};
    std::snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                  static_cast<unsigned>(GetRValue(c)),
                  static_cast<unsigned>(GetGValue(c)),
                  static_cast<unsigned>(GetBValue(c)));
    return std::string(buf);
}

/// Converts `COLORREF` sRGB into `(H in [0, 360), S in [0, 1], L in [0, 1])`.
inline void RgbToHsl(COLORREF c, double& outH, double& outS, double& outL) noexcept {
    const double r = GetRValue(c) / 255.0;
    const double g = GetGValue(c) / 255.0;
    const double b = GetBValue(c) / 255.0;
    const double mx = std::max({r, g, b});
    const double mn = std::min({r, g, b});
    const double d  = mx - mn;

    outL = (mx + mn) * 0.5;
    if (d < 1e-6) {
        outH = 0.0;
        outS = 0.0;
        return;
    }
    outS = (outL > 0.5) ? (d / (2.0 - mx - mn)) : (d / (mx + mn));
    if (mx == r) {
        outH = 60.0 * std::fmod(((g - b) / d) + 6.0, 6.0);
    } else if (mx == g) {
        outH = 60.0 * (((b - r) / d) + 2.0);
    } else {
        outH = 60.0 * (((r - g) / d) + 4.0);
    }
}

/// Converts `(H in [0, 360), S in [0, 1], L in [0, 1])` back into a Win32 `COLORREF`.
inline COLORREF HslToRgb(double hDeg, double s, double l) noexcept {
    s = std::clamp(s, 0.0, 1.0);
    l = std::clamp(l, 0.0, 1.0);
    if (s < 1e-6) {
        const int v = static_cast<int>(std::lround(l * 255.0));
        return RGB(v, v, v);
    }
    double h = std::fmod(hDeg, 360.0);
    if (h < 0.0) h += 360.0;

    const double c = (1.0 - std::abs(2.0 * l - 1.0)) * s;
    const double x = c * (1.0 - std::abs(std::fmod(h / 60.0, 2.0) - 1.0));
    const double m = l - c * 0.5;

    double r1 = 0.0, g1 = 0.0, b1 = 0.0;
    if (h < 60.0)       { r1 = c; g1 = x; b1 = 0; }
    else if (h < 120.0) { r1 = x; g1 = c; b1 = 0; }
    else if (h < 180.0) { r1 = 0; g1 = c; b1 = x; }
    else if (h < 240.0) { r1 = 0; g1 = x; b1 = c; }
    else if (h < 300.0) { r1 = x; g1 = 0; b1 = c; }
    else                { r1 = c; g1 = 0; b1 = x; }

    auto toByte = [m](double v) noexcept -> int {
        return std::clamp(static_cast<int>(std::lround((v + m) * 255.0)), 0, 255);
    };
    return RGB(toByte(r1), toByte(g1), toByte(b1));
}

/// Checks whether a primary brand accent color passes BOTH:
///   - `>= 4.5:1` WCAG 2.2 AA normal text contrast with `#FFFFFF` button captions, AND
///   - `>= 3.0:1` WCAG 2.2 AA SC 1.4.11 non-text UI contrast against `#15203A` Dark Mode cards!
inline bool MeetsDualThemeAccentContrast(
    COLORREF accent,
    double& outVsWhiteText,
    double& outVsDarkCard) noexcept
{
    outVsWhiteText = ContrastRatio(accent, RGB(255, 255, 255));
    outVsDarkCard  = ContrastRatio(accent, RGB(21, 32, 58)); // `#15203A` Dark Card
    return (outVsWhiteText >= 4.5) && (outVsDarkCard >= 3.0);
}

/// Adjusts `accent` along its own HSL hue & saturation curve so its relative luminance lands
/// cleanly inside `[0.150, 0.180]`, guaranteeing `>= 4.5:1` contrast with `#FFFFFF` button text
/// AND `>= 3.1:1` contrast against `#15203A` Dark Mode cards!
inline COLORREF SnapAccentToAccessibleBand(COLORREF accent) noexcept {
    double vsWhite = 0.0, vsDark = 0.0;
    if (MeetsDualThemeAccentContrast(accent, vsWhite, vsDark)) {
        return accent; // Already WCAG 2.2 AA compliant across both Light and Dark themes!
    }

    double h = 0.0, s = 0.0, l = 0.0;
    RgbToHsl(accent, h, s, l);
    if (s < 0.15) s = 0.65; // Give pure gray inputs a crisp accessible saturation if needed

    // Target W3C relative luminance ~0.165 (`~4.88:1` vs `#FFFFFF` AND `~3.33:1` vs `#15203A`!)
    const double curLum    = RelativeLuminance(accent);
    const double targetLum = (curLum > 0.180) ? 0.172 : 0.158;

    double lo = 0.02, hi = 0.96;
    COLORREF best = accent;
    double bestDiff = 999.0;
    for (int iter = 0; iter < 28; ++iter) {
        const double mid = (lo + hi) * 0.5;
        const COLORREF cand = HslToRgb(h, s, mid);
        const double lum = RelativeLuminance(cand);
        const double diff = std::abs(lum - targetLum);
        if (diff < bestDiff) {
            bestDiff = diff;
            best = cand;
        }
        if (lum < targetLum) lo = mid;
        else hi = mid;
    }
    return best;
}

} // namespace rsw::wcag
