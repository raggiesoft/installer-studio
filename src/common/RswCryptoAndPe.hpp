// ================================================================================
// FILE: src/common/RswCryptoAndPe.hpp
// PROJECT: RaggieSoft Setup Wizard ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What three low-level Windows Installer tasks live in this header?
//     1. **Random & Deterministic MSI GUIDs (`rpc.h` + `bcrypt.h`)**:
//        - `GenerateRandomMsiGuid()` calls Windows RPC's `UuidCreate` to mint fresh
//          `ProductCode` and Summary Information `PackageCode` GUIDs.
//        - `ComputeDeterministicComponentGuid(upgradeCode, logicalTargetKey)` uses
//          Windows Cryptography Next Generation (`bcrypt.dll` SHA-1) to compute an
//          **RFC 4122 Version 5 UUID** from your product's `UpgradeCode` + relative
//          install path (lowercased). That guarantees `HelloRaggieSoft.exe` receives
//          the **exact same ComponentId GUID** across v1.0, v1.1, and v2.0 without
//          requiring humans or AI to maintain fragile manual GUID spreadsheets!
//     2. **PE Version Harvesting (`winver.h` / `Version.lib`)**:
//        - Windows Installer's file-versioning rules (`REINSTALLMODE=omus`) decide
//          whether to overwrite an `.exe` or `.dll` during upgrades by inspecting the
//          `File.Version` and `File.Language` columns in the MSI `File` table.
//        - `HarvestPeVersionAndLanguage(filePath)` calls `GetFileVersionInfoW` and
//          `VerQueryValueW` on every `.exe`/`.dll` payload file so `File.Version` is
//          populated automatically!
//     3. **MSI `8.3|LongFileName` Formatter**:
//        - Windows Installer's `File.FileName` and `Directory.DefaultDir` columns
//          require `SHORTN~1.EXT|Long File Name.ext` syntax whenever a filename is
//          longer than 8.3 characters, contains spaces, or mixes case (ICE03).
// ================================================================================

#pragma once

#include "RswManifestAst.hpp"
#include "RswPlatform.hpp"

#include <bcrypt.h>
#include <rpc.h>
#include <winver.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "Bcrypt.lib")
#pragma comment(lib, "Rpcrt4.lib")
#pragma comment(lib, "Version.lib")

namespace rsw {

/// Formats 16 RFC 4122 big-endian UUID bytes as an uppercase MSI GUID `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}`.
inline std::string FormatRfc4122BytesAsMsiGuid(const std::array<std::uint8_t, 16>& b) {
    char buf[40]{};
    std::snprintf(
        buf,
        sizeof(buf),
        "{%02X%02X%02X%02X-%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        b[0], b[1], b[2], b[3],
        b[4], b[5],
        b[6], b[7],
        b[8], b[9],
        b[10], b[11], b[12], b[13], b[14], b[15]);
    return std::string(buf);
}

/// Generates a brand-new random Version 4 GUID via `UuidCreate` (`Rpcrt4.lib`).
inline std::string GenerateRandomMsiGuid() {
    UUID u{};
    (void)::UuidCreate(&u);
    char buf[40]{};
    std::snprintf(
        buf,
        sizeof(buf),
        "{%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
        u.Data1,
        u.Data2,
        u.Data3,
        u.Data4[0], u.Data4[1],
        u.Data4[2], u.Data4[3], u.Data4[4], u.Data4[5], u.Data4[6], u.Data4[7]);
    return std::string(buf);
}

/// Validates `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` syntax and normalizes to uppercase.
inline bool NormalizeMsiGuid(std::string_view raw, std::string& outNormalized) {
    std::string s = TrimAscii(raw);
    if (s.size() != 38 || s.front() != '{' || s.back() != '}') {
        return false;
    }
    for (std::size_t i = 1; i < 37; ++i) {
        if (i == 9 || i == 14 || i == 19 || i == 24) {
            if (s[i] != '-') return false;
        } else {
            char c = s[i];
            const bool isHex = (c >= '0' && c <= '9') ||
                               (c >= 'a' && c <= 'f') ||
                               (c >= 'A' && c <= 'F');
            if (!isHex) return false;
            if (c >= 'a' && c <= 'f') {
                s[i] = static_cast<char>(c - 'a' + 'A');
            }
        }
    }
    outNormalized = std::move(s);
    return true;
}

/// Computes an RFC 4122 Version 5 (SHA-1) deterministic MSI GUID using Windows CNG (`bcrypt.dll`).
/// Inputs: `upgradeCodeGuid` + architecture + normalized relative target key -> stable `{GUID}`!
inline std::string ComputeDeterministicComponentGuid(
    std::string_view upgradeCodeGuid,
    std::string_view archSlug,
    std::string_view logicalTargetKey)
{
    // Fixed first-party RaggieSoft Setup Wizard UUIDv5 namespace (`{72535731-4D53-4935-8A11-524147474945}`):
    static constexpr std::array<std::uint8_t, 16> kRswNamespace{{
        0x72, 0x53, 0x57, 0x31, 0x4D, 0x53, 0x49, 0x35,
        0x8A, 0x11, 0x52, 0x41, 0x47, 0x47, 0x49, 0x45
    }};

    std::string canonicalName = AsciiLower(upgradeCodeGuid) + "|" +
                                AsciiLower(archSlug) + "|" +
                                AsciiLower(logicalTargetKey);
    for (char& ch : canonicalName) {
        if (ch == '\\') ch = '/';
    }

    BCRYPT_ALG_HANDLE hAlg  = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::array<std::uint8_t, 20> sha1Digest{};

    if (::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) == 0) {
        DWORD objLen = 0, cbRes = 0;
        ::BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cbRes, 0);
        std::vector<UCHAR> hashObj(objLen);
        if (::BCryptCreateHash(hAlg, &hHash, hashObj.data(), objLen, nullptr, 0, 0) == 0) {
            ::BCryptHashData(hHash, const_cast<PUCHAR>(kRswNamespace.data()), static_cast<ULONG>(kRswNamespace.size()), 0);
            ::BCryptHashData(
                hHash,
                reinterpret_cast<PUCHAR>(canonicalName.data()),
                static_cast<ULONG>(canonicalName.size()),
                0);
            ::BCryptFinishHash(hHash, sha1Digest.data(), static_cast<ULONG>(sha1Digest.size()), 0);
            ::BCryptDestroyHash(hHash);
        }
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
    }

    std::array<std::uint8_t, 16> uuidBytes{};
    std::memcpy(uuidBytes.data(), sha1Digest.data(), 16);
    // RFC 4122 §4.3: set version = 5 (`0101xxxx` in octet 6) and variant = RFC 4122 (`10xxxxxx` in octet 8).
    uuidBytes[6] = static_cast<std::uint8_t>((uuidBytes[6] & 0x0Fu) | 0x50u);
    uuidBytes[8] = static_cast<std::uint8_t>((uuidBytes[8] & 0x3Fu) | 0x80u);
    return FormatRfc4122BytesAsMsiGuid(uuidBytes);
}

/// Reads an entire binary file from disk into a `std::vector<std::uint8_t>`.
inline bool ReadBinaryFileBytes(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>& outBytes,
    std::string& outError)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        outError = "Failed opening binary file for reading: " + WideToUtf8(path.wstring());
        return false;
    }
    const auto sz = in.tellg();
    if (sz < 0) {
        outError = "Failed determining size of file: " + WideToUtf8(path.wstring());
        return false;
    }
    outBytes.resize(static_cast<std::size_t>(sz));
    in.seekg(0, std::ios::beg);
    if (!outBytes.empty() && !in.read(reinterpret_cast<char*>(outBytes.data()), sz)) {
        outError = "Failed reading bytes from file: " + WideToUtf8(path.wstring());
        return false;
    }
    return true;
}

/// Computes an uppercase SHA-256 hex digest of a file on disk using `bcrypt.dll`.
inline std::string ComputeFileSha256Hex(const std::filesystem::path& filePath) {
    std::ifstream in(filePath, std::ios::binary);
    if (!in) return {};

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    std::array<std::uint8_t, 32> digest{};
    if (::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return {};
    }
    DWORD objLen = 0, cbRes = 0;
    ::BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cbRes, 0);
    std::vector<UCHAR> hashObj(objLen);
    if (::BCryptCreateHash(hAlg, &hHash, hashObj.data(), objLen, nullptr, 0, 0) == 0) {
        char buf[65536];
        while (in.read(buf, sizeof(buf)) || in.gcount() > 0) {
            ::BCryptHashData(hHash, reinterpret_cast<PUCHAR>(buf), static_cast<ULONG>(in.gcount()), 0);
        }
        ::BCryptFinishHash(hHash, digest.data(), static_cast<ULONG>(digest.size()), 0);
        ::BCryptDestroyHash(hHash);
    }
    ::BCryptCloseAlgorithmProvider(hAlg, 0);

    std::ostringstream oss;
    oss << std::uppercase << std::hex << std::setfill('0');
    for (auto b : digest) {
        oss << std::setw(2) << static_cast<int>(b);
    }
    return oss.str();
}

struct HarvestedPeMetadata {
    std::string version;   // Empty for non-PE data files; `"1.0.0.0"` for versioned `.exe`/`.dll`
    std::string language;  // Empty for non-PE data files; `"1033"` or `"0"` for versioned PE binaries
};

/// Inspects a file with `GetFileVersionInfoW` (`Version.lib`) to extract `File.Version` and `File.Language`.
inline HarvestedPeMetadata HarvestPeVersionAndLanguage(const std::filesystem::path& filePath) {
    HarvestedPeMetadata meta{};
    const std::wstring wPath = filePath.wstring();
    DWORD dummyHandle = 0;
    const DWORD infoSize = ::GetFileVersionInfoSizeW(wPath.c_str(), &dummyHandle);
    if (infoSize == 0) {
        return meta;
    }

    std::vector<std::uint8_t> buffer(infoSize);
    if (!::GetFileVersionInfoW(wPath.c_str(), 0, infoSize, buffer.data())) {
        return meta;
    }

    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT fixedLen = 0;
    if (::VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<LPVOID*>(&fixedInfo), &fixedLen) &&
        fixedInfo != nullptr && fixedLen >= sizeof(VS_FIXEDFILEINFO) &&
        fixedInfo->dwSignature == 0xFEEF04BDu) {
        const WORD v1 = HIWORD(fixedInfo->dwFileVersionMS);
        const WORD v2 = LOWORD(fixedInfo->dwFileVersionMS);
        const WORD v3 = HIWORD(fixedInfo->dwFileVersionLS);
        const WORD v4 = LOWORD(fixedInfo->dwFileVersionLS);
        meta.version = std::to_string(v1) + "." + std::to_string(v2) + "." +
                       std::to_string(v3) + "." + std::to_string(v4);

        // Query `\VarFileInfo\Translation` for the primary LCID, defaulting to `"0"` (language-neutral).
        struct LangAndCodePage {
            WORD language;
            WORD codePage;
        }* trans = nullptr;
        UINT transBytes = 0;
        if (::VerQueryValueW(
                buffer.data(),
                L"\\VarFileInfo\\Translation",
                reinterpret_cast<LPVOID*>(&trans),
                &transBytes) &&
            trans != nullptr && transBytes >= sizeof(LangAndCodePage)) {
            meta.language = std::to_string(trans[0].language);
        } else {
            meta.language = "0";
        }
    }

    return meta;
}

/// Formats a filename or directory name in MSI `SHORT~1.EXT|LongName` syntax with unique `indexSeed`
/// collision avoidance so `ICE03` `Filename` validation passes on every file and directory!
inline std::string FormatMsiShortLongName(std::string_view longName, std::size_t indexSeed) {
    auto isSafeShortChar = [](char c) noexcept {
        return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
    };

    // Check if `longName` is ALREADY a pure uppercase 8.3 name (no spaces, <= 1 dot, stem <= 8, ext <= 3).
    auto dot = longName.find('.');
    const bool singleOrNoDot = (dot == std::string_view::npos) ||
                               (longName.find('.', dot + 1) == std::string_view::npos && dot > 0);
    if (singleOrNoDot) {
        std::string_view stem = (dot == std::string_view::npos) ? longName : longName.substr(0, dot);
        std::string_view ext  = (dot == std::string_view::npos) ? std::string_view{} : longName.substr(dot + 1);
        if (!stem.empty() && stem.size() <= 8 && ext.size() <= 3) {
            bool allUpperSafe = true;
            for (char c : stem) if (!isSafeShortChar(c)) allUpperSafe = false;
            for (char c : ext)  if (!isSafeShortChar(c)) allUpperSafe = false;
            if (allUpperSafe) {
                return std::string(longName);
            }
        }
    }

    // Build deterministic `ABCD~<N>.EXT|LongName`
    auto lastDot = longName.rfind('.');
    std::string_view rawStem = (lastDot == std::string_view::npos || lastDot == 0) ? longName : longName.substr(0, lastDot);
    std::string_view rawExt  = (lastDot == std::string_view::npos || lastDot == 0) ? std::string_view{} : longName.substr(lastDot + 1);

    std::string cleanStem;
    for (char c : rawStem) {
        if (c >= 'a' && c <= 'z') cleanStem += static_cast<char>(c - 'a' + 'A');
        else if (isSafeShortChar(c)) cleanStem += c;
    }
    if (cleanStem.empty()) cleanStem = "FILE";

    std::string cleanExt;
    for (char c : rawExt) {
        if (cleanExt.size() == 3) break;
        if (c >= 'a' && c <= 'z') cleanExt += static_cast<char>(c - 'a' + 'A');
        else if (isSafeShortChar(c)) cleanExt += c;
    }

    std::string suffix = "~" + std::to_string(indexSeed);
    std::size_t maxPrefix = (suffix.size() < 8) ? (8 - suffix.size()) : 1;
    if (cleanStem.size() > maxPrefix) {
        cleanStem.resize(maxPrefix);
    }

    std::string shortPart = cleanStem + suffix;
    if (!cleanExt.empty()) {
        shortPart += ".";
        shortPart += cleanExt;
    }
    return shortPart + "|" + std::string(longName);
}

} // namespace rsw
