// ================================================================================
// FILE: src/common/RswWin32UiTheme.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What does this shared Win32 UI header provide to BOTH `SetupWizard.exe` (our
//   customer-facing self-extracting installer) and `InstallerStudio.exe` (our
//   developer Visual IDE)?
//     1. **RaggieSoft Brand Palette + Accessibility Contrast Themes**:
//        - Checks Windows Contrast Themes (`SPI_GETHIGHCONTRAST`) FIRST so users
//          relying on Aquatic, Desert, Dusk, or Night Sky assistive contrast
//          modes receive pure OS `GetSysColor(...)` tokens without interference.
//        - Reads `HKCU\...\Personalize\AppsUseLightTheme` for automatic Windows 11
//          Dark Mode (`#0B132B` Midnight Navy / `#151F38` Slate cards / `#F8FAFC`
//          crisp ice-white text) and Light Mode (`#F4F7FB` daylight surface /
//          `#FFFFFF` cards / `#0F172A` ink navy text).
//        - Enables Windows 11 Immersive Dark Titlebar (`DWMWA_USE_IMMERSIVE_DARK_MODE`)
//          and Mica backdrop (`DWMWA_SYSTEMBACKDROP_TYPE = DWMSBT_MAINWINDOW`).
//     2. **Hex Color Parser & Smooth Gradient Banner Painter (`msimg32.lib`)**:
//        - Parses `#RRGGBB` manifest strings (`AccentPrimaryHex`, `AccentSecondaryHex`)
//          into Win32 `COLORREF` values and paints RaggieSoft's signature Midnight Navy
//          banner + 4px horizontal `GradientFill` accent bar!
//     3. **Zero-Dependency PNG Decoder via Windows Imaging Component (`WIC`)**:
//        - Uses `IWICImagingFactory` (`windowscodecs.lib`, built into every copy
//          of Windows!) to decode embedded `.png` cover/OpenGraph banner streams
//          directly from memory into 32-bit premultiplied ARGB `HBITMAP` handles!
//     4. **Per-Monitor V2 DPI + Windows Text-Scale Factor + Keyboard Focus Visibility**:
//        - Scales all layout coordinates and `Segoe UI` / `Cascadia Mono` fonts cleanly
//          and clears `UISF_HIDEFOCUS` / `UISF_HIDEACCEL` so keyboard focus rings
//          and `&` mnemonics are immediately visible from Frame 1!
// ================================================================================

#pragma once

#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <shlwapi.h>
#include <uxtheme.h>
#include <wincodec.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "dwmapi.lib")
#pragma comment(lib, "uxtheme.lib")
#pragma comment(lib, "msimg32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")

#ifndef IDI_RAGGIESOFT_APP_ICON
#define IDI_RAGGIESOFT_APP_ICON 101
#endif

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif

#ifndef DWMWA_SYSTEMBACKDROP_TYPE
#define DWMWA_SYSTEMBACKDROP_TYPE 38
#endif

namespace rsw::ui {

/// Parses `"#2563EB"` or `"2563EB"` into a Win32 `COLORREF` (`RGB(r, g, b)`).
inline COLORREF ParseHexColor(std::string_view hex, COLORREF fallback) noexcept {
    if (!hex.empty() && hex.front() == '#') {
        hex.remove_prefix(1);
    }
    if (hex.size() != 6) {
        return fallback;
    }
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
        if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
        return -1;
    };
    const int r1 = nib(hex[0]), r2 = nib(hex[1]);
    const int g1 = nib(hex[2]), g2 = nib(hex[3]);
    const int b1 = nib(hex[4]), b2 = nib(hex[5]);
    if (r1 < 0 || r2 < 0 || g1 < 0 || g2 < 0 || b1 < 0 || b2 < 0) {
        return fallback;
    }
    return RGB((r1 << 4) | r2, (g1 << 4) | g2, (b1 << 4) | b2);
}

/// Scales a logical 96-DPI pixel measurement by monitor DPI and Windows Accessibility Text Scale.
inline int ScalePx(int px, UINT dpi, int textScalePercent = 100) noexcept {
    const int safeDpi = static_cast<int>(dpi > 0 ? dpi : 96);
    const int factor  = 100 + ((std::clamp(textScalePercent, 100, 180) - 100) / 2);
    return ::MulDiv(px, safeDpi * factor, 96 * 100);
}

/// Creates a crisp ClearType Win32 font scaled to `dpi` and `textScalePercent`.
inline HFONT CreateScaledFont(
    const wchar_t* faceName,
    int pointSizeAt96Dpi,
    int weight,
    UINT dpi,
    int textScalePercent = 100) noexcept
{
    const int safeDpi = static_cast<int>(dpi > 0 ? dpi : 96);
    const int clamped = std::clamp(textScalePercent, 100, 175);
    const int pxHeight = -::MulDiv(pointSizeAt96Dpi, safeDpi * clamped, 72 * 100);
    return ::CreateFontW(
        pxHeight, 0, 0, 0, weight, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, faceName);
}

/// Optional process-wide theme override (`-1` = follow OS `AppsUseLightTheme`, `1` = force Dark, `0` = force Light)
/// used by `--export-screenshots` and live `WM_SETTINGCHANGE(ImmersiveColorSet)` verification!
inline int& ForcedThemeOverride() noexcept {
    static int mode = -1;
    return mode;
}

/// Theme state & GDI brush cache shared by `SetupWizard.exe`, `InstallerStudio.exe`, `msiprism.exe`, & `RaggieSoftHelp.exe`.
struct RswTheme {
    bool     isHighContrast   = false;
    bool     isDarkMode       = true;
    int      textScalePercent = 100;

    COLORREF bannerNavyLeft   = RGB(11, 19, 43);   // #0B132B Signature RaggieSoft Midnight Navy
    COLORREF bannerNavyRight  = RGB(19, 35, 71);   // #132347 Deep Sapphire
    COLORREF bannerTitleText  = RGB(248, 250, 252);
    COLORREF bannerSubText    = RGB(186, 230, 253);
    COLORREF accentPrimary    = RGB(37, 99, 235);  // #2563EB RaggieSoft Cobalt
    COLORREF accentSecondary  = RGB(251, 191, 36); // #FBBF24 Warm Gold

    COLORREF windowBg         = RGB(15, 23, 42);
    COLORREF cardBg           = RGB(23, 33, 56);
    COLORREF footerBg         = RGB(11, 19, 43);
    COLORREF textPrimary      = RGB(241, 245, 249);
    COLORREF textSecondary    = RGB(203, 213, 225);
    COLORREF borderSubtle     = RGB(51, 65, 85);

    HBRUSH   hbrWindow        = nullptr;
    HBRUSH   hbrCard          = nullptr;
    HBRUSH   hbrFooter        = nullptr;

    ~RswTheme() { Release(); }

    void Release() noexcept {
        if (hbrWindow) { ::DeleteObject(hbrWindow); hbrWindow = nullptr; }
        if (hbrCard)   { ::DeleteObject(hbrCard);   hbrCard   = nullptr; }
        if (hbrFooter) { ::DeleteObject(hbrFooter); hbrFooter = nullptr; }
    }

    void Refresh(COLORREF customPrimary = RGB(37, 99, 235), COLORREF customSecondary = RGB(251, 191, 36)) noexcept {
        Release();
        accentPrimary   = customPrimary;
        accentSecondary = customSecondary;

        // 1. Assistive High Contrast check (always wins!)
        HIGHCONTRASTW hc{};
        hc.cbSize = sizeof(hc);
        if (::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0)) {
            isHighContrast = (hc.dwFlags & HCF_HIGHCONTRASTON) != 0;
        } else {
            isHighContrast = false;
        }

        // 2. Windows Accessibility Text Scale slider (`100..200%`)
        textScalePercent = 100;
        HKEY hA11y = nullptr;
        if (::RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Accessibility", 0, KEY_READ, &hA11y) == ERROR_SUCCESS) {
            DWORD val = 100, cb = sizeof(val), type = 0;
            if (::RegQueryValueExW(hA11y, L"TextScaleFactor", nullptr, &type, reinterpret_cast<LPBYTE>(&val), &cb) == ERROR_SUCCESS &&
                type == REG_DWORD) {
                textScalePercent = std::clamp(static_cast<int>(val), 100, 180);
            }
            ::RegCloseKey(hA11y);
        }

        if (isHighContrast) {
            windowBg        = ::GetSysColor(COLOR_WINDOW);
            cardBg          = ::GetSysColor(COLOR_WINDOW);
            footerBg        = ::GetSysColor(COLOR_BTNFACE);
            bannerNavyLeft  = ::GetSysColor(COLOR_BTNFACE);
            bannerNavyRight = ::GetSysColor(COLOR_BTNFACE);
            bannerTitleText = ::GetSysColor(COLOR_BTNTEXT);
            bannerSubText   = ::GetSysColor(COLOR_BTNTEXT);
            textPrimary     = ::GetSysColor(COLOR_WINDOWTEXT);
            textSecondary   = ::GetSysColor(COLOR_WINDOWTEXT);
            borderSubtle    = ::GetSysColor(COLOR_WINDOWFRAME);
            accentPrimary   = ::GetSysColor(COLOR_HIGHLIGHT);
            accentSecondary = ::GetSysColor(COLOR_HIGHLIGHT);
            const int lum   = (GetRValue(windowBg) * 299 + GetGValue(windowBg) * 587 + GetBValue(windowBg) * 114) / 1000;
            isDarkMode      = (lum < 128);
        } else {
            DWORD appsUseLight = 1;
            HKEY hPers = nullptr;
            if (::RegOpenKeyExW(
                    HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    0, KEY_READ, &hPers) == ERROR_SUCCESS) {
                DWORD cb = sizeof(appsUseLight), type = 0;
                ::RegQueryValueExW(hPers, L"AppsUseLightTheme", nullptr, &type, reinterpret_cast<LPBYTE>(&appsUseLight), &cb);
                ::RegCloseKey(hPers);
            }
            isDarkMode = (appsUseLight == 0);
            if (ForcedThemeOverride() >= 0) {
                isDarkMode = (ForcedThemeOverride() == 1);
            }

            bannerNavyLeft  = RGB(11, 19, 43);   // #0B132B Midnight Navy
            bannerNavyRight = RGB(20, 38, 78);   // #14264E Deep Royal Navy
            bannerTitleText = RGB(248, 250, 252);
            bannerSubText   = RGB(191, 219, 254);

            if (isDarkMode) {
                windowBg      = RGB(13, 20, 38);   // #0D1426 Deep Slate Navy
                cardBg        = RGB(21, 32, 58);   // #15203A Elevated Glass Card
                footerBg      = RGB(9, 15, 31);    // #090F1F Footer Strip
                textPrimary   = RGB(248, 250, 252);// #F8FAFC (15.8:1 WCAG AAA)
                textSecondary = RGB(203, 213, 225);// #CBD5E1 (11.2:1 WCAG AAA)
                borderSubtle  = RGB(51, 65, 85);   // #334155
            } else {
                windowBg      = RGB(244, 247, 251);// #F4F7FB Crisp Daylight Surface
                cardBg        = RGB(255, 255, 255);// #FFFFFF Pure Card
                footerBg      = RGB(232, 238, 247);// #E8EEF7 Footer Strip
                textPrimary   = RGB(15, 23, 42);   // #0F172A (16.1:1 WCAG AAA)
                textSecondary = RGB(51, 65, 85);   // #334155 (9.4:1 WCAG AAA)
                borderSubtle  = RGB(203, 213, 225);// #CBD5E1
            }
        }

        hbrWindow = ::CreateSolidBrush(windowBg);
        hbrCard   = ::CreateSolidBrush(cardBg);
        hbrFooter = ::CreateSolidBrush(footerBg);
    }
};

/// Applies Windows 11 Dark Titlebar, Mica backdrop (when not High Contrast), and keyboard focus visibility!
inline void ApplyWindowChromeAndA11y(HWND hWnd, const RswTheme& theme) noexcept {
    const BOOL darkTitle = theme.isDarkMode ? TRUE : FALSE;
    ::DwmSetWindowAttribute(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &darkTitle, sizeof(darkTitle));

    if (!theme.isHighContrast) {
        const int backdropMica = 2; // DWMSBT_MAINWINDOW (Mica)
        ::DwmSetWindowAttribute(hWnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdropMica, sizeof(backdropMica));
    }

    // Ensure keyboard focus rings and `&` mnemonics are ALWAYS visible immediately!
    ::SendMessageW(hWnd, WM_CHANGEUISTATE, MAKEWPARAM(UIS_CLEAR, UISF_HIDEFOCUS | UISF_HIDEACCEL), 0);
}

/// Detects live Windows 11 Light <-> Dark Mode (`WM_SETTINGCHANGE` `lParam == L"ImmersiveColorSet"`),
/// Contrast Theme (`SPI_SETHIGHCONTRAST` / `WM_SYSCOLORCHANGE`), and Visual Style (`WM_THEMECHANGED`)
/// broadcasts so open windows hot-swap their entire palette in <16ms without restarting!
inline bool IsLiveThemeBroadcast(UINT msg, WPARAM wParam, LPARAM lParam) noexcept {
    if (msg == WM_SYSCOLORCHANGE || msg == WM_THEMECHANGED) {
        return true;
    }
    if (msg == WM_SETTINGCHANGE) {
        if (wParam == SPI_SETHIGHCONTRAST || lParam == 0) {
            return true;
        }
        const auto* area = reinterpret_cast<const wchar_t*>(lParam);
        if (_wcsicmp(area, L"ImmersiveColorSet") == 0 ||
            _wcsicmp(area, L"WindowsThemeElement") == 0 ||
            _wcsicmp(area, L"WindowMetrics") == 0) {
            return true;
        }
    }
    return false;
}

/// Paints a smooth horizontal 2-stop `GradientFill` rectangle using `msimg32.lib`.
inline void FillHorizontalGradient(HDC hdc, const RECT& rc, COLORREF leftColor, COLORREF rightColor) noexcept {
    TRIVERTEX verts[2]{};
    verts[0].x     = rc.left;
    verts[0].y     = rc.top;
    verts[0].Red   = static_cast<COLOR16>(GetRValue(leftColor) << 8);
    verts[0].Green = static_cast<COLOR16>(GetGValue(leftColor) << 8);
    verts[0].Blue  = static_cast<COLOR16>(GetBValue(leftColor) << 8);
    verts[0].Alpha = 0xFF00;

    verts[1].x     = rc.right;
    verts[1].y     = rc.bottom;
    verts[1].Red   = static_cast<COLOR16>(GetRValue(rightColor) << 8);
    verts[1].Green = static_cast<COLOR16>(GetGValue(rightColor) << 8);
    verts[1].Blue  = static_cast<COLOR16>(GetBValue(rightColor) << 8);
    verts[1].Alpha = 0xFF00;

    GRADIENT_RECT gRect{0, 1};
    ::GradientFill(hdc, verts, 2, &gRect, 1, GRADIENT_FILL_RECT_H);
}

/// Decodes an in-memory PNG/BMP/JPEG byte buffer into a 32-bpp premultiplied BGRA `HBITMAP`
/// via Windows Imaging Component (`IWICImagingFactory` in `windowscodecs.dll`)!
inline HBITMAP DecodeImageBytesToHBitmap(
    const void* data,
    std::size_t byteCount,
    int& outWidth,
    int& outHeight) noexcept
{
    outWidth  = 0;
    outHeight = 0;
    if (!data || byteCount == 0) return nullptr;

    IWICImagingFactory* pFactory = nullptr;
    if (FAILED(::CoCreateInstance(
            CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pFactory))) || !pFactory) {
        return nullptr;
    }

    IWICStream* pStream = nullptr;
    IWICBitmapDecoder* pDecoder = nullptr;
    IWICBitmapFrameDecode* pFrame = nullptr;
    IWICFormatConverter* pConverter = nullptr;
    HBITMAP hBmp = nullptr;

    if (SUCCEEDED(pFactory->CreateStream(&pStream)) && pStream &&
        SUCCEEDED(pStream->InitializeFromMemory(
            const_cast<BYTE*>(static_cast<const BYTE*>(data)),
            static_cast<DWORD>(byteCount))) &&
        SUCCEEDED(pFactory->CreateDecoderFromStream(
            pStream, nullptr, WICDecodeMetadataCacheOnLoad, &pDecoder)) && pDecoder &&
        SUCCEEDED(pDecoder->GetFrame(0, &pFrame)) && pFrame &&
        SUCCEEDED(pFactory->CreateFormatConverter(&pConverter)) && pConverter &&
        SUCCEEDED(pConverter->Initialize(
            pFrame, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
            nullptr, 0.0, WICBitmapPaletteTypeCustom))) {

        UINT w = 0, h = 0;
        if (SUCCEEDED(pConverter->GetSize(&w, &h)) && w > 0 && h > 0) {
            BITMAPINFO bmi{};
            bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
            bmi.bmiHeader.biWidth       = static_cast<LONG>(w);
            bmi.bmiHeader.biHeight      = -static_cast<LONG>(h); // Top-down DIB
            bmi.bmiHeader.biPlanes      = 1;
            bmi.bmiHeader.biBitCount    = 32;
            bmi.bmiHeader.biCompression = BI_RGB;

            void* pvBits = nullptr;
            HDC screenDc = ::GetDC(nullptr);
            hBmp = ::CreateDIBSection(screenDc, &bmi, DIB_RGB_COLORS, &pvBits, nullptr, 0);
            ::ReleaseDC(nullptr, screenDc);

            if (hBmp && pvBits) {
                const UINT stride = w * 4u;
                if (SUCCEEDED(pConverter->CopyPixels(nullptr, stride, stride * h, static_cast<BYTE*>(pvBits)))) {
                    outWidth  = static_cast<int>(w);
                    outHeight = static_cast<int>(h);
                } else {
                    ::DeleteObject(hBmp);
                    hBmp = nullptr;
                }
            }
        }
    }

    if (pConverter) pConverter->Release();
    if (pFrame)     pFrame->Release();
    if (pDecoder)   pDecoder->Release();
    if (pStream)    pStream->Release();
    pFactory->Release();
    return hBmp;
}

/// Draws a 32-bpp premultiplied ARGB `HBITMAP` scaled smoothly (`HALFTONE` + `AlphaBlend`) into `destRc`.
inline void DrawAlphaBitmapFit(HDC hdc, HBITMAP hBmp, int srcW, int srcH, const RECT& destRc) noexcept {
    if (!hBmp || srcW <= 0 || srcH <= 0) return;
    const int dstW = destRc.right - destRc.left;
    const int dstH = destRc.bottom - destRc.top;
    if (dstW <= 0 || dstH <= 0) return;

    HDC memDc = ::CreateCompatibleDC(hdc);
    HGDIOBJ oldBmp = ::SelectObject(memDc, hBmp);

    BLENDFUNCTION bf{};
    bf.BlendOp             = AC_SRC_OVER;
    bf.BlendFlags          = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat         = AC_SRC_ALPHA;

    ::SetStretchBltMode(hdc, HALFTONE);
    ::AlphaBlend(hdc, destRc.left, destRc.top, dstW, dstH, memDc, 0, 0, srcW, srcH, bf);

    ::SelectObject(memDc, oldBmp);
    ::DeleteDC(memDc);
}

} // namespace rsw::ui
