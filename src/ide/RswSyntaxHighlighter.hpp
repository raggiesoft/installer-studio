// ================================================================================
// FILE: src/ide/RswSyntaxHighlighter.hpp
// PROJECT: RaggieSoft Installer Studio — Native RichEdit 5.0 Syntax Code Editor
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   How do we provide crisp, WCAG 2.2 AA accessible syntax highlighting and line
//   numbers for BOTH `.rssc` (RaggieScript) AND `.rsip`/`.rsis`/`.rsii` manifests
//   inside `InstallerStudio.exe` WITHOUT bundling 100 MB of Electron/Chromium?
//     Every installation of Windows 10 and Windows 11 ships `%SystemRoot%\System32\Msftedit.dll`
//     (`MSFTEDIT_CLASS` / `RICHEDIT50W`), Microsoft's native 64-bit RichEdit 5.0
//     text engine!
//     This header provides:
//       1. **Dynamic `Msftedit.dll` Loader (`EnsureRichEdit50Loaded`)**:
//          - Loads `Msftedit.dll` once per process so `MSFTEDIT_CLASS` (`RICHEDIT50W`)
//            is available with zero external NuGet or DLL dependencies.
//       2. **Theme-Aware WCAG 2.2 AA Token Palette (`SyntaxPalette`)**:
//          - Computes distinct, high-legibility colors for Dark Mode, Light Mode,
//            AND Windows Contrast Themes (`SPI_GETHIGHCONTRAST`) so comments,
//            keywords, built-in namespaces (`msi.*`, `reg.*`, `ui.*`, `ffi.*`),
//            strings, `${...}` interpolations, `v"..."` semantic versions, numbers,
//            and `[Section.Id]` headers are always effortless to read!
//       3. **Flicker-Free In-Place Lexical Colorizer (`ApplySyntaxHighlightingToRichEdit`)**:
//          - Suspends redraw + `EN_CHANGE` notifications (`WM_SETREDRAW(FALSE)` +
//            `EM_SETEVENTMASK`), preserves exact selection & scroll position
//            (`EM_EXGETSEL` / `EM_GETSCROLLPOS`), applies `CHARFORMAT2W` spans over
//            UTF-16 character offsets, and restores scroll & caret seamlessly!
//       4. **Synchronized Line-Number Gutter (`PaintLineNumberGutter`)**:
//          - Queries `EM_GETFIRSTVISIBLELINE` on `hSourceEdit` to paint crisp
//            right-aligned monospace line numbers alongside the active code buffer!
// ================================================================================

#pragma once

#include "../common/RswWin32UiTheme.hpp"

#include <richedit.h>

#include <algorithm>
#include <cwctype>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace rsw::ide_syntax {

enum class SourceSyntaxMode : int {
    ManifestIni = 0, // `.rsip`, `.rsis`, `.rsii`
    RaggieScript = 1 // `.rssc`
};

struct SyntaxPalette {
    COLORREF bg           = RGB(15, 23, 42);
    COLORREF gutterBg     = RGB(11, 18, 34);
    COLORREF gutterBorder = RGB(51, 65, 85);
    COLORREF gutterText   = RGB(148, 163, 184);
    COLORREF plain        = RGB(241, 245, 249);
    COLORREF comment      = RGB(110, 231, 183); // Accessible emerald/sage comment
    COLORREF keyword      = RGB(125, 211, 252); // Sky cyan (`package`, `install`, `if`, `fn`, `let`)
    COLORREF builtinNs    = RGB(251, 191, 36);  // Warm gold (`msi`, `reg`, `fs`, `proc`, `ui`, `ffi`)
    COLORREF typeOrEnum   = RGB(196, 181, 253); // Soft violet (`Scope`, `Hive`, `RegView`, `Uac`, `path`, `version`)
    COLORREF stringLit    = RGB(253, 186, 116); // Warm peach (`"..."`, `r"..."`)
    COLORREF numberOrVer  = RGB(244, 114, 182); // Rose (`v"10.0.22000"`, `9007`, `true`, `false`)
    COLORREF preproc      = RGB(248, 113, 113); // Coral (`#if`, `#define`, `include`, `@include`)
    COLORREF sectionHdr   = RGB(56, 189, 248);  // `[Package]`, `[Feature.Main]`

    static SyntaxPalette FromTheme(const rsw::ui::RswTheme& theme) noexcept {
        SyntaxPalette p{};
        if (theme.isHighContrast) {
            p.bg           = theme.windowBg;
            p.gutterBg     = theme.windowBg;
            p.gutterBorder = theme.borderSubtle;
            p.gutterText   = theme.textSecondary;
            p.plain        = theme.textPrimary;
            p.comment      = theme.textSecondary;
            p.keyword      = theme.accentPrimary;
            p.builtinNs    = theme.accentPrimary;
            p.typeOrEnum   = theme.textPrimary;
            p.stringLit    = theme.accentPrimary;
            p.numberOrVer  = theme.textPrimary;
            p.preproc      = theme.accentPrimary;
            p.sectionHdr   = theme.accentPrimary;
            return p;
        }
        if (theme.isDarkMode) {
            p.bg           = RGB(13, 20, 36);
            p.gutterBg     = RGB(9, 14, 26);
            p.gutterBorder = RGB(45, 58, 82);
            p.gutterText   = RGB(148, 163, 184);
            p.plain        = RGB(241, 245, 249);
            p.comment      = RGB(110, 231, 183);
            p.keyword      = RGB(125, 211, 252);
            p.builtinNs    = RGB(251, 191, 36);
            p.typeOrEnum   = RGB(196, 181, 253);
            p.stringLit    = RGB(253, 186, 116);
            p.numberOrVer  = RGB(249, 168, 212);
            p.preproc      = RGB(252, 165, 165);
            p.sectionHdr   = RGB(56, 189, 248);
        } else {
            // Crisp WCAG 2.2 AA Light Mode (>= 4.5:1 against `#FFFFFF`)!
            p.bg           = RGB(255, 255, 255);
            p.gutterBg     = RGB(241, 245, 249);
            p.gutterBorder = RGB(203, 213, 225);
            p.gutterText   = RGB(71, 85, 105);
            p.plain        = RGB(15, 23, 42);
            p.comment      = RGB(21, 128, 61);  // Deep forest green
            p.keyword      = RGB(29, 78, 216);  // Royal cobalt blue
            p.builtinNs    = RGB(146, 64, 14);  // Deep amber-brown
            p.typeOrEnum   = RGB(109, 40, 217); // Deep purple
            p.stringLit    = RGB(154, 52, 18);  // Rust sienna
            p.numberOrVer  = RGB(157, 23, 77);  // Deep magenta
            p.preproc      = RGB(185, 28, 28);  // Crimson
            p.sectionHdr   = RGB(3, 105, 161);  // Deep ocean blue
        }
        return p;
    }
};

struct ColorSpan {
    LONG     cpMin = 0;
    LONG     cpMax = 0;
    COLORREF color = 0;
    bool     bold  = false;
};

inline bool EnsureRichEdit50Loaded() noexcept {
    static HMODULE sMod = ::LoadLibraryW(L"Msftedit.dll");
    return sMod != nullptr;
}

inline SourceSyntaxMode DetectSyntaxMode(const std::filesystem::path& filePath, std::wstring_view textSample) noexcept {
    const std::wstring ext = filePath.extension().wstring();
    if (_wcsicmp(ext.c_str(), L".rssc") == 0) return SourceSyntaxMode::RaggieScript;
    if (_wcsicmp(ext.c_str(), L".rsip") == 0 ||
        _wcsicmp(ext.c_str(), L".rsis") == 0 ||
        _wcsicmp(ext.c_str(), L".rsii") == 0) {
        return SourceSyntaxMode::ManifestIni;
    }
    // Heuristic for unsaved buffers (`New RaggieScript` vs `New .rsip`)
    if (textSample.find(L"use installer;") != std::wstring_view::npos ||
        textSample.find(L"package \"") != std::wstring_view::npos ||
        textSample.find(L"install {") != std::wstring_view::npos) {
        return SourceSyntaxMode::RaggieScript;
    }
    return SourceSyntaxMode::ManifestIni;
}

inline bool IsRsscKeyword(std::wstring_view w) noexcept {
    static const std::set<std::wstring_view> kSet = {
        L"use", L"package", L"showcase", L"app", L"feature", L"param", L"payload",
        L"wizard", L"step", L"when", L"install", L"repair", L"uninstall", L"on",
        L"fn", L"function", L"let", L"const", L"ref", L"if", L"elif", L"else",
        L"while", L"repeat", L"until", L"for", L"in", L"break", L"continue",
        L"return", L"try", L"catch", L"throw", L"exit", L"from"
    };
    return kSet.count(w) > 0;
}

inline bool IsRsscBuiltInNamespace(std::wstring_view w) noexcept {
    static const std::set<std::wstring_view> kNs = {
        L"sys", L"fs", L"reg", L"ini", L"xml", L"json", L"str", L"regex",
        L"archive", L"crypto", L"net", L"proc", L"ffi", L"msi", L"prereq",
        L"shortcut", L"shell", L"env", L"arp", L"svc", L"task", L"firewall",
        L"acl", L"ui", L"feature", L"compile", L"showcase", L"log", L"product",
        L"def", L"assert", L"coalesce", L"len", L"range", L"typeof", L"sleep"
    };
    return kNs.count(w) > 0;
}

inline bool IsRsscTypeOrEnum(std::wstring_view w) noexcept {
    static const std::set<std::wstring_view> kTypes = {
        L"bool", L"int", L"float", L"string", L"path", L"version", L"array", L"map", L"any",
        L"Scope", L"Uac", L"Token", L"RegView", L"Icon", L"Hive", L"SetupMode", L"WizardStyle", L"Delivery",
        L"EXIT_OK", L"EXIT_ELEVATION_REQUIRED", L"EXIT_REBOOT_NEEDED", L"EXIT_UNSUPPORTED_OS",
        L"EXIT_PREREQ_MISSING", L"EXIT_DOWNLOAD_FAILED"
    };
    return kTypes.count(w) > 0;
}

/// Scans UTF-16 text (note: RichEdit 5.0 uses single `\r` internally for line breaks!) into `ColorSpan` tokens.
inline void TokenizeForHighlighting(
    std::wstring_view text,
    SourceSyntaxMode mode,
    const SyntaxPalette& pal,
    std::vector<ColorSpan>& outSpans)
{
    outSpans.clear();
    const std::size_t n = text.size();
    auto isLineBreak = [](wchar_t c) noexcept { return c == L'\r' || c == L'\n'; };

    if (mode == SourceSyntaxMode::ManifestIni) {
        std::size_t i = 0;
        while (i < n) {
            // Advance to end of current line
            std::size_t lineStart = i;
            while (i < n && !isLineBreak(text[i])) ++i;
            std::size_t lineEnd = i;
            while (i < n && isLineBreak(text[i])) ++i;

            std::size_t p = lineStart;
            while (p < lineEnd && (text[p] == L' ' || text[p] == L'\t')) ++p;
            if (p >= lineEnd) continue;

            if (text[p] == L';' || text[p] == L'#') {
                outSpans.push_back({static_cast<LONG>(p), static_cast<LONG>(lineEnd), pal.comment, false});
                continue;
            }
            if (text[p] == L'@') {
                outSpans.push_back({static_cast<LONG>(p), static_cast<LONG>(lineEnd), pal.preproc, true});
                continue;
            }
            if (text[p] == L'[') {
                std::size_t rb = p;
                while (rb < lineEnd && text[rb] != L']') ++rb;
                if (rb < lineEnd) ++rb;
                outSpans.push_back({static_cast<LONG>(p), static_cast<LONG>(rb), pal.sectionHdr, true});
                if (rb < lineEnd) {
                    outSpans.push_back({static_cast<LONG>(rb), static_cast<LONG>(lineEnd), pal.comment, false});
                }
                continue;
            }
            // `Key = Value ; inline comment`
            std::size_t eq = p;
            while (eq < lineEnd && text[eq] != L'=' && text[eq] != L';') ++eq;
            if (eq < lineEnd && text[eq] == L'=') {
                outSpans.push_back({static_cast<LONG>(p), static_cast<LONG>(eq), pal.keyword, false});
                std::size_t vp = eq + 1;
                // Highlight `$(VAR)` tokens or trailing `;` comment
                while (vp < lineEnd) {
                    if (text[vp] == L';') {
                        outSpans.push_back({static_cast<LONG>(vp), static_cast<LONG>(lineEnd), pal.comment, false});
                        break;
                    }
                    if (text[vp] == L'$' && vp + 1 < lineEnd && text[vp + 1] == L'(') {
                        std::size_t cl = vp + 2;
                        while (cl < lineEnd && text[cl] != L')') ++cl;
                        if (cl < lineEnd) ++cl;
                        outSpans.push_back({static_cast<LONG>(vp), static_cast<LONG>(cl), pal.builtinNs, true});
                        vp = cl;
                        continue;
                    }
                    ++vp;
                }
            }
        }
        return;
    }

    // Mode B: Full `RaggieScript` (`.rssc`) Lexical Colorizer!
    std::size_t i = 0;
    bool atLineStart = true;
    while (i < n) {
        const wchar_t c = text[i];
        if (isLineBreak(c)) {
            atLineStart = true;
            ++i;
            continue;
        }
        if (c == L' ' || c == L'\t') {
            ++i;
            continue;
        }

        // Preprocessor line `#if`, `#ifndef`, `#define`, `#else`, `#endif`, `#region`, `#error`
        if (atLineStart && c == L'#') {
            const std::size_t s = i;
            while (i < n && !isLineBreak(text[i])) ++i;
            outSpans.push_back({static_cast<LONG>(s), static_cast<LONG>(i), pal.preproc, true});
            atLineStart = false;
            continue;
        }
        atLineStart = false;

        // `//` single-line comment
        if (c == L'/' && i + 1 < n && text[i + 1] == L'/') {
            const std::size_t s = i;
            while (i < n && !isLineBreak(text[i])) ++i;
            outSpans.push_back({static_cast<LONG>(s), static_cast<LONG>(i), pal.comment, false});
            continue;
        }
        // `/* ... */` multi-line comment
        if (c == L'/' && i + 1 < n && text[i + 1] == L'*') {
            const std::size_t s = i;
            i += 2;
            while (i + 1 < n && !(text[i] == L'*' && text[i + 1] == L'/')) ++i;
            if (i + 1 < n) i += 2; else i = n;
            outSpans.push_back({static_cast<LONG>(s), static_cast<LONG>(i), pal.comment, false});
            continue;
        }

        // Semantic version literal `v"10.0.22000"` or raw string `r"..."`
        if ((c == L'v' || c == L'V' || c == L'r' || c == L'R') && i + 1 < n && text[i + 1] == L'"') {
            const bool isVer = (c == L'v' || c == L'V');
            const std::size_t s = i;
            i += 2;
            while (i < n && text[i] != L'"' && !isLineBreak(text[i])) ++i;
            if (i < n && text[i] == L'"') ++i;
            outSpans.push_back({static_cast<LONG>(s), static_cast<LONG>(i), isVer ? pal.numberOrVer : pal.stringLit, isVer});
            continue;
        }

        // Standard `"..."` string with `${...}` interpolation spans!
        if (c == L'"') {
            std::size_t segStart = i++;
            while (i < n && !isLineBreak(text[i])) {
                if (text[i] == L'\\' && i + 1 < n) {
                    i += 2;
                    continue;
                }
                if (text[i] == L'$' && i + 1 < n && text[i + 1] == L'{') {
                    if (i > segStart) {
                        outSpans.push_back({static_cast<LONG>(segStart), static_cast<LONG>(i), pal.stringLit, false});
                    }
                    const std::size_t interpStart = i;
                    i += 2;
                    int depth = 1;
                    while (i < n && depth > 0 && !isLineBreak(text[i])) {
                        if (text[i] == L'{') ++depth;
                        else if (text[i] == L'}') --depth;
                        ++i;
                    }
                    outSpans.push_back({static_cast<LONG>(interpStart), static_cast<LONG>(i), pal.builtinNs, true});
                    segStart = i;
                    continue;
                }
                if (text[i] == L'"') {
                    ++i;
                    break;
                }
                ++i;
            }
            if (i > segStart) {
                outSpans.push_back({static_cast<LONG>(segStart), static_cast<LONG>(i), pal.stringLit, false});
            }
            continue;
        }

        // Numeric & bare version literals
        if (c >= L'0' && c <= L'9') {
            const std::size_t s = i;
            while (i < n && (std::iswxdigit(text[i]) || text[i] == L'.' || text[i] == L'_' || text[i] == L'x' || text[i] == L'X')) ++i;
            outSpans.push_back({static_cast<LONG>(s), static_cast<LONG>(i), pal.numberOrVer, false});
            continue;
        }

        // Identifiers, keywords, namespaces, named argument labels (`title:`)
        if (std::iswalpha(c) || c == L'_') {
            const std::size_t s = i;
            while (i < n && (std::iswalnum(text[i]) || text[i] == L'_')) ++i;
            const std::wstring_view word = text.substr(s, i - s);
            if (word == L"include" || word == L"import") {
                outSpans.push_back({static_cast<LONG>(s), static_cast<LONG>(i), pal.preproc, true});
            } else if (word == L"true" || word == L"false" || word == L"null") {
                outSpans.push_back({static_cast<LONG>(s), static_cast<LONG>(i), pal.numberOrVer, true});
            } else if (IsRsscKeyword(word)) {
                outSpans.push_back({static_cast<LONG>(s), static_cast<LONG>(i), pal.keyword, true});
            } else if (IsRsscBuiltInNamespace(word)) {
                outSpans.push_back({static_cast<LONG>(s), static_cast<LONG>(i), pal.builtinNs, true});
            } else if (IsRsscTypeOrEnum(word)) {
                outSpans.push_back({static_cast<LONG>(s), static_cast<LONG>(i), pal.typeOrEnum, false});
            } else if (i < n && text[i] == L':') {
                // Property or named-argument label (`title:`, `shield:`, `view:`)
                outSpans.push_back({static_cast<LONG>(s), static_cast<LONG>(i), pal.typeOrEnum, false});
            }
            continue;
        }

        ++i;
    }
}

/// Applies flicker-free syntax highlighting to a `RICHEDIT50W` (`MSFTEDIT_CLASS`) control!
inline void ApplySyntaxHighlightingToRichEdit(
    HWND hRichEdit,
    SourceSyntaxMode mode,
    const rsw::ui::RswTheme& theme,
    bool& ioSuppressChangeGuard)
{
    if (!hRichEdit || !::IsWindow(hRichEdit)) return;
    const bool wasVisible = (::IsWindowVisible(hRichEdit) != FALSE);
    const SyntaxPalette pal = SyntaxPalette::FromTheme(theme);

    ioSuppressChangeGuard = true;
    const LRESULT prevMask = ::SendMessageW(hRichEdit, EM_SETEVENTMASK, 0, 0);
    ::SendMessageW(hRichEdit, WM_SETREDRAW, FALSE, 0);

    CHARRANGE savedSel{};
    ::SendMessageW(hRichEdit, EM_EXGETSEL, 0, reinterpret_cast<LPARAM>(&savedSel));
    POINT savedScroll{};
    ::SendMessageW(hRichEdit, EM_GETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&savedScroll));

    ::SendMessageW(hRichEdit, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(pal.bg));

    // Read RichEdit internal text (`GTL_DEFAULT` uses single `\r` per line matching `EM_EXSETSEL` character offsets!)
    GETTEXTLENGTHEX gtl{};
    gtl.flags    = GTL_DEFAULT;
    gtl.codepage = 1200; // UTF-16LE
    const LRESULT cch = ::SendMessageW(hRichEdit, EM_GETTEXTLENGTHEX, reinterpret_cast<WPARAM>(&gtl), 0);
    if (cch > 0 && cch < 400000) {
        std::wstring buf(static_cast<std::size_t>(cch) + 2, L'\0');
        GETTEXTEX gt{};
        gt.cb       = static_cast<DWORD>(buf.size() * sizeof(wchar_t));
        gt.flags    = GT_DEFAULT;
        gt.codepage = 1200;
        const LRESULT got = ::SendMessageW(hRichEdit, EM_GETTEXTEX, reinterpret_cast<WPARAM>(&gt), reinterpret_cast<LPARAM>(buf.data()));
        buf.resize(static_cast<std::size_t>(std::max<LRESULT>(0, got)));

        // Reset base color & non-bold weight across entire document (`SCF_ALL`)
        CHARFORMAT2W baseCf{};
        baseCf.cbSize      = sizeof(baseCf);
        baseCf.dwMask      = CFM_COLOR | CFM_BOLD;
        baseCf.dwEffects   = 0;
        baseCf.crTextColor = pal.plain;
        ::SendMessageW(hRichEdit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&baseCf));

        std::vector<ColorSpan> spans;
        TokenizeForHighlighting(buf, mode, pal, spans);

        CHARFORMAT2W spanCf{};
        spanCf.cbSize = sizeof(spanCf);
        spanCf.dwMask = CFM_COLOR | CFM_BOLD;
        for (const auto& sp : spans) {
            CHARRANGE cr{sp.cpMin, sp.cpMax};
            ::SendMessageW(hRichEdit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
            spanCf.crTextColor = sp.color;
            spanCf.dwEffects   = sp.bold ? CFE_BOLD : 0;
            ::SendMessageW(hRichEdit, EM_SETCHARFORMAT, SCF_SELECTION, reinterpret_cast<LPARAM>(&spanCf));
        }
    } else {
        CHARFORMAT2W baseCf{};
        baseCf.cbSize      = sizeof(baseCf);
        baseCf.dwMask      = CFM_COLOR;
        baseCf.crTextColor = pal.plain;
        ::SendMessageW(hRichEdit, EM_SETCHARFORMAT, SCF_ALL, reinterpret_cast<LPARAM>(&baseCf));
    }

    ::SendMessageW(hRichEdit, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&savedSel));
    ::SendMessageW(hRichEdit, EM_SETSCROLLPOS, 0, reinterpret_cast<LPARAM>(&savedScroll));
    ::SendMessageW(hRichEdit, WM_SETREDRAW, TRUE, 0);
    if (!wasVisible) {
        ::ShowWindow(hRichEdit, SW_HIDE);
    } else {
        ::InvalidateRect(hRichEdit, nullptr, TRUE);
    }
    ::SendMessageW(hRichEdit, EM_SETEVENTMASK, 0, prevMask);
    ioSuppressChangeGuard = false;
}

/// Paints a synchronized monospace line-number gutter directly to the left of `hRichEdit`.
inline void PaintLineNumberGutter(
    HDC hdc,
    const RECT& gutterRc,
    HWND hRichEdit,
    HFONT hFontMono,
    const rsw::ui::RswTheme& theme)
{
    if (!hRichEdit || gutterRc.right <= gutterRc.left) return;
    const SyntaxPalette pal = SyntaxPalette::FromTheme(theme);

    HBRUSH bgBr = ::CreateSolidBrush(pal.gutterBg);
    ::FillRect(hdc, &gutterRc, bgBr);
    ::DeleteObject(bgBr);

    HPEN borderPen = ::CreatePen(PS_SOLID, 1, pal.gutterBorder);
    HGDIOBJ oldPen = ::SelectObject(hdc, borderPen);
    ::MoveToEx(hdc, gutterRc.right - 1, gutterRc.top, nullptr);
    ::LineTo(hdc, gutterRc.right - 1, gutterRc.bottom);
    ::SelectObject(hdc, oldPen);
    ::DeleteObject(borderPen);

    HGDIOBJ oldFont = ::SelectObject(hdc, hFontMono);
    ::SetBkMode(hdc, TRANSPARENT);
    ::SetTextColor(hdc, pal.gutterText);

    TEXTMETRICW tm{};
    ::GetTextMetricsW(hdc, &tm);
    const int lineH = std::max<int>(14, tm.tmHeight);

    const int firstVis = static_cast<int>(::SendMessageW(hRichEdit, EM_GETFIRSTVISIBLELINE, 0, 0));
    const int totalLines = std::max<int>(1, static_cast<int>(::SendMessageW(hRichEdit, EM_GETLINECOUNT, 0, 0)));

    int y = gutterRc.top + 2;
    for (int ln = firstVis + 1; ln <= totalLines && y < gutterRc.bottom - 2; ++ln) {
        wchar_t numBuf[16]{};
        std::swprintf(numBuf, std::size(numBuf), L"%d", ln);
        RECT numRc{gutterRc.left + 4, y, gutterRc.right - 8, y + lineH};
        ::DrawTextW(hdc, numBuf, -1, &numRc, DT_RIGHT | DT_SINGLELINE | DT_VCENTER | DT_NOPREFIX);
        y += lineH;
    }

    ::SelectObject(hdc, oldFont);
}

} // namespace rsw::ide_syntax
