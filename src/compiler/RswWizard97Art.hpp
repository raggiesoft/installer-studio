// ================================================================================
// FILE: src/compiler/RswWizard97Art.hpp
// PROJECT: RaggieSoft Setup Wizard ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is "Wizard 97" artwork in a Windows Installer (`.msi`) file?
//     When an IT admin or power user double-clicks a standalone `Product.msi`
//     directly (without `Setup.exe`), Windows' built-in `msiexec.exe` engine
//     renders classic Windows 2000 "Wizard 97" dialogs using two 24-bit `.bmp`
//     bitmaps stored in the MSI's `Binary` table:
//       1. `RswBannerBmp` (`493 x 58` px):
//          White header banner across interior pages (`LicenseAgreementDlg`,
//          `InstallDirDlg`, `ProgressDlg`) with a subtle right-side brand emblem
//          so black `Tahoma` headings (`x = 15..300`) keep >= 18:1 WCAG contrast!
//       2. `RswWelcomeBmp` (`493 x 312` px):
//          Classic split Wizard 97 backdrop for `WelcomeDlg` and `ExitDialog`—
//          deep RaggieSoft Midnight-to-Cobalt illustration strip on the left
//          (`x = 0..164`) and pure crisp white (`#FFFFFF`) on the right
//          (`x = 165..492`) behind the welcome headline and description!
//
//     Synthesizing these BMPs procedurally in memory means `rswbuild.exe` has
//     ZERO external image file dependencies!
// ================================================================================

#pragma once

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace rsw {

inline std::vector<std::uint8_t> Build24BitBmpBytes(
    int width,
    int height,
    const std::vector<std::uint8_t>& rgbTopDown)
{
    const int rowStride = ((width * 3 + 3) / 4) * 4;
    const std::uint32_t pixelBytes = static_cast<std::uint32_t>(rowStride * height);
    const std::uint32_t fileSize   = 14u + 40u + pixelBytes;

    std::vector<std::uint8_t> bmp(fileSize, 0);

    auto writeU16 = [&](std::size_t off, std::uint16_t v) {
        bmp[off + 0] = static_cast<std::uint8_t>(v & 0xFFu);
        bmp[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
    };
    auto writeU32 = [&](std::size_t off, std::uint32_t v) {
        bmp[off + 0] = static_cast<std::uint8_t>(v & 0xFFu);
        bmp[off + 1] = static_cast<std::uint8_t>((v >> 8) & 0xFFu);
        bmp[off + 2] = static_cast<std::uint8_t>((v >> 16) & 0xFFu);
        bmp[off + 3] = static_cast<std::uint8_t>((v >> 24) & 0xFFu);
    };

    // `BITMAPFILEHEADER` (14 bytes)
    bmp[0] = 'B';
    bmp[1] = 'M';
    writeU32(2, fileSize);
    writeU32(10, 54u);

    // `BITMAPINFOHEADER` (40 bytes, bottom-up scanlines for maximum Win2000 GDI compatibility)
    writeU32(14, 40u);
    writeU32(18, static_cast<std::uint32_t>(width));
    writeU32(22, static_cast<std::uint32_t>(height));
    writeU16(26, 1u);
    writeU16(28, 24u);
    writeU32(34, pixelBytes);
    writeU32(38, 3780u); // 96 DPI (~3780 pixels/meter)
    writeU32(42, 3780u);

    for (int y = 0; y < height; ++y) {
        const int srcY = height - 1 - y; // Flip top-down -> bottom-up DIB scanline
        std::size_t dstRow = 54u + static_cast<std::size_t>(y * rowStride);
        for (int x = 0; x < width; ++x) {
            const std::size_t srcIdx = static_cast<std::size_t>((srcY * width + x) * 3);
            bmp[dstRow + static_cast<std::size_t>(x * 3) + 0] = rgbTopDown[srcIdx + 2]; // B
            bmp[dstRow + static_cast<std::size_t>(x * 3) + 1] = rgbTopDown[srcIdx + 1]; // G
            bmp[dstRow + static_cast<std::size_t>(x * 3) + 2] = rgbTopDown[srcIdx + 0]; // R
        }
    }

    return bmp;
}

/// Generates the `493 x 58` Wizard 97 interior top banner bitmap (`RswBannerBmp`).
inline bool WriteDefaultWizard97BannerBmp(const std::filesystem::path& outPath) {
    constexpr int W = 493;
    constexpr int H = 58;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(W * H * 3), 255); // Pure white background

    auto setPx = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        const std::size_t idx = static_cast<std::size_t>((y * W + x) * 3);
        rgb[idx + 0] = r;
        rgb[idx + 1] = g;
        rgb[idx + 2] = b;
    };

    // Bottom 2-pixel Wizard 97 cobalt-to-amber accent divider (`y = 56..57`)
    for (int x = 0; x < W; ++x) {
        const double t = static_cast<double>(x) / static_cast<double>(W - 1);
        const auto r = static_cast<std::uint8_t>(14.0 + t * (217.0 - 14.0));
        const auto g = static_cast<std::uint8_t>(68.0 + t * (149.0 - 68.0));
        const auto b = static_cast<std::uint8_t>(168.0 + t * (28.0 - 168.0));
        setPx(x, H - 2, r, g, b);
        setPx(x, H - 1, 212, 218, 228);
    }

    // Crisp right-hand rounded 38x38 RaggieSoft cobalt emblem (`cx = 458, cy = 27`)
    const double cx = 458.0, cy = 27.0;
    for (int y = 8; y <= 46; ++y) {
        for (int x = 439; x <= 477; ++x) {
            const double dx = std::abs(x - cx);
            const double dy = std::abs(y - cy);
            const double dist = std::hypot(std::max(0.0, dx - 11.0), std::max(0.0, dy - 11.0));
            if (dist <= 7.5) {
                setPx(x, y, 15, 38, 86);
                // Inner golden 4-point star emblem
                if (dx + dy <= 10.0 || (dx <= 2.2 && dy <= 12.0) || (dy <= 2.2 && dx <= 12.0)) {
                    setPx(x, y, 245, 179, 36);
                }
            }
        }
    }

    const auto bytes = Build24BitBmpBytes(W, H, rgb);
    std::ofstream out(outPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

/// Generates the `493 x 312` Wizard 97 Welcome / Exit split bitmap (`RswWelcomeBmp`).
inline bool WriteDefaultWizard97WelcomeBmp(const std::filesystem::path& outPath) {
    constexpr int W = 493;
    constexpr int H = 312;
    constexpr int kSplitX = 164;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(W * H * 3), 255);

    auto setPx = [&](int x, int y, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
        if (x < 0 || x >= W || y < 0 || y >= H) return;
        const std::size_t idx = static_cast<std::size_t>((y * W + x) * 3);
        rgb[idx + 0] = r;
        rgb[idx + 1] = g;
        rgb[idx + 2] = b;
    };

    // Left hero column (`x = 0..163`): deep Midnight Navy -> Royal Cobalt vertical gradient
    for (int y = 0; y < H; ++y) {
        const double ty = static_cast<double>(y) / static_cast<double>(H - 1);
        for (int x = 0; x < kSplitX; ++x) {
            const double tx = static_cast<double>(x) / static_cast<double>(kSplitX - 1);
            auto r = static_cast<std::uint8_t>(11.0 + ty * 18.0 + tx * 8.0);
            auto g = static_cast<std::uint8_t>(22.0 + ty * 48.0 + tx * 16.0);
            auto b = static_cast<std::uint8_t>(54.0 + ty * 92.0 + tx * 24.0);

            // Radiant golden-cyan constellation emblem centered at `(82, 118)`
            const double dx = x - 82.0;
            const double dy = y - 118.0;
            const double rad = std::hypot(dx, dy);
            if (rad <= 46.0 && rad >= 42.5) {
                r = 92; g = 178; b = 248;
            } else if (std::abs(dx) + std::abs(dy) <= 22.0 ||
                       (std::abs(dx) <= 3.0 && std::abs(dy) <= 34.0) ||
                       (std::abs(dy) <= 3.0 && std::abs(dx) <= 34.0)) {
                r = 246; g = 186; b = 42;
            }
            setPx(x, y, r, g, b);
        }
        // Crisp vertical divider line at `x = 164`
        setPx(kSplitX, y, 206, 214, 226);
    }

    const auto bytes = Build24BitBmpBytes(W, H, rgb);
    std::ofstream out(outPath, std::ios::binary);
    out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

} // namespace rsw
