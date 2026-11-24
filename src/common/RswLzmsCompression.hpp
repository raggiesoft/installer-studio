// ================================================================================
// FILE: src/common/RswLzmsCompression.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   How do enterprise single-EXE installers shrink 30 GB of SDKs, templates, and
//   binaries down to ~3 GB WITHOUT shipping third-party GPL/LGPL compression DLLs?
//
//     Starting in Windows 8 and refined in Windows 10/11, Microsoft built its own
//     high-ratio **LZMS (`COMPRESS_ALGORITHM_LZMS`)** solid block compression engine
//     directly into `C:\Windows\System32\Cabinet.dll` (`<compressapi.h>`), the exact
//     LZMA-class dictionary + range-coder + x86/ARM64 executable jump-filter engine
//     used by Windows `.esd` Electronic Software Download OS images!
//
//     This header wraps `CreateCompressor(COMPRESS_ALGORITHM_LZMS, ...)` and
//     `CreateDecompressor(COMPRESS_ALGORITHM_LZMS, ...)` behind a self-identifying
//     16-byte frame header (`"RSWLZMS1"` + 64-bit original byte length):
//       - `instbuild.exe` compresses embedded `Product.msi` and `[Prerequisite.*]`
//         binaries before writing them into `Setup.exe` `RT_RCDATA` resources.
//       - `SetupWizard.exe` inspects the first 8 bytes: if `"RSWLZMS1"` is present,
//         it transparently decompresses in memory via `Decompress(...)`; if raw
//         bytes are present, it passes them straight through!
//       - Also powers solid `.rswpak` On-Demand Feature Packs in Native & Streaming modes!
// ================================================================================

#pragma once

#include "RswPlatform.hpp"

#include <compressapi.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "Cabinet.lib")

namespace rsw {

inline constexpr char kRswLzmsMagic[8] = {'R', 'S', 'W', 'L', 'Z', 'M', 'S', '1'};

#pragma pack(push, 1)
struct RswLzmsHeader {
    char          magic[8];         // `"RSWLZMS1"`
    std::uint64_t uncompressedSize; // Exact original byte length
};
#pragma pack(pop)

/// Compresses `rawBytes` with Windows built-in `COMPRESS_ALGORITHM_LZMS`.
/// If `LZMS` shrinks the payload, returns `[RswLzmsHeader + compressedStream]`;
/// otherwise returns `rawBytes` unchanged so incompressible tiny blobs never grow!
inline std::vector<std::uint8_t> CompressWithLzmsIfSmaller(const std::vector<std::uint8_t>& rawBytes) {
    if (rawBytes.size() < 512 ||
        (rawBytes.size() >= sizeof(RswLzmsHeader) &&
         std::memcmp(rawBytes.data(), kRswLzmsMagic, sizeof(kRswLzmsMagic)) == 0)) {
        return rawBytes;
    }

    COMPRESSOR_HANDLE hComp = nullptr;
    if (!::CreateCompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &hComp) || !hComp) {
        return rawBytes;
    }

    SIZE_T compBound = 0;
    ::Compress(hComp, rawBytes.data(), rawBytes.size(), nullptr, 0, &compBound);
    if (compBound == 0) {
        ::CloseCompressor(hComp);
        return rawBytes;
    }

    std::vector<std::uint8_t> framed(sizeof(RswLzmsHeader) + compBound);
    SIZE_T actualCompSize = 0;
    const BOOL ok = ::Compress(
        hComp,
        rawBytes.data(),
        rawBytes.size(),
        framed.data() + sizeof(RswLzmsHeader),
        compBound,
        &actualCompSize);
    ::CloseCompressor(hComp);

    if (!ok || (sizeof(RswLzmsHeader) + actualCompSize >= rawBytes.size())) {
        return rawBytes;
    }

    RswLzmsHeader hdr{};
    std::memcpy(hdr.magic, kRswLzmsMagic, sizeof(kRswLzmsMagic));
    hdr.uncompressedSize = static_cast<std::uint64_t>(rawBytes.size());
    std::memcpy(framed.data(), &hdr, sizeof(hdr));
    framed.resize(sizeof(RswLzmsHeader) + actualCompSize);
    return framed;
}

/// Transparently decompresses an `"RSWLZMS1"` buffer (or copies raw bytes if unframed).
inline bool DecompressLzmsOrCopyRaw(
    const std::uint8_t* srcPtr,
    std::size_t srcSize,
    std::vector<std::uint8_t>& outBytes,
    std::string& outError)
{
    outBytes.clear();
    if (!srcPtr || srcSize == 0) {
        outError = "Empty resource stream.";
        return false;
    }

    if (srcSize < sizeof(RswLzmsHeader) ||
        std::memcmp(srcPtr, kRswLzmsMagic, sizeof(kRswLzmsMagic)) != 0) {
        // Uncompressed raw payload — copy directly!
        outBytes.assign(srcPtr, srcPtr + srcSize);
        return true;
    }

    RswLzmsHeader hdr{};
    std::memcpy(&hdr, srcPtr, sizeof(hdr));
    if (hdr.uncompressedSize == 0) {
        return true;
    }

    DECOMPRESSOR_HANDLE hDec = nullptr;
    if (!::CreateDecompressor(COMPRESS_ALGORITHM_LZMS, nullptr, &hDec) || !hDec) {
        outError = "CreateDecompressor(LZMS) failed (Win32 " + std::to_string(::GetLastError()) + ").";
        return false;
    }

    outBytes.resize(static_cast<std::size_t>(hdr.uncompressedSize));
    SIZE_T actualOut = 0;
    const BOOL ok = ::Decompress(
        hDec,
        srcPtr + sizeof(RswLzmsHeader),
        srcSize - sizeof(RswLzmsHeader),
        outBytes.data(),
        outBytes.size(),
        &actualOut);
    ::CloseDecompressor(hDec);

    if (!ok || actualOut != outBytes.size()) {
        outError = "LZMS decompression failed (Win32 " + std::to_string(::GetLastError()) + ").";
        outBytes.clear();
        return false;
    }
    return true;
}

} // namespace rsw
