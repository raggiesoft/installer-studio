// ================================================================================
// FILE: src/common/RswZipAndPak.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file, and what three jobs does it handle in pure C++20?
//     1. **Zero-Dependency `.ZIP` Archive Reader & Writer (RFC 1951 DEFLATE)**:
//        - Lets authors pass any `.zip` file directly to `[Payload.*] Source = app.zip`
//          or the **Folder/ZIP-to-SFX AeroWizard** (`PackageKind = sfx`) without
//          requiring PowerShell, Explorer COM threads, or third-party DLLs!
//        - Implements a complete, self-contained RFC 1951 DEFLATE decompressor
//          (supporting Stored `00`, Fixed Huffman `01`, and Dynamic Huffman `10`
//          blocks + IEEE 802.3 CRC-32 verification + Zip-Slip `..` protection).
//     2. **`RSWPAK1` Solid `LZMS` Container Builder & Extractor**:
//        - Concatenates multi-file payloads into a single solid stream compressed
//          with Windows' built-in `COMPRESS_ALGORITHM_LZMS` (`Cabinet.lib`),
//          preserving relative folder structure and `FILETIME` timestamps.
//        - Powers THREE `v0.6.0` subsystems from one unified container format:
//            (a) `PackageKind = sfx` self-extracting `.exe` archives (`#220`),
//            (b) `Engine = native` zero-`msi.dll` direct installers (`#220`), and
//            (c) `.rswpak` external on-demand feature packs (`Delivery = ondemand`)!
//     3. **Cryptographic & Authenticode Verification (`WinVerifyTrust` + `SHA-256`)**:
//        - Verifies `SHA-256` (`bcrypt.dll`) digests and Windows `WinVerifyTrust`
//          (`wintrust.dll`) digital signatures on downloaded `.rswpak` / child
//          installer packages before a single byte is extracted to disk!
// ================================================================================

#pragma once

#include "RswLzmsCompression.hpp"
#include "RswPlatform.hpp"
#include "RswPrerequisites.hpp"

#include <softpub.h>
#include <wintrust.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "wintrust.lib")

namespace rsw {

/// Returns `true` when `p` has a case-insensitive `.zip` file extension.
inline bool IsZipArchivePath(const std::filesystem::path& p) noexcept {
    return EqualsIgnoreCase(WideToUtf8(p.extension().wstring()), ".zip");
}

/// Computes the standard PKZIP / IEEE 802.3 CRC-32 checksum over `[data, data + len)`.
inline std::uint32_t ComputeCrc32(const std::uint8_t* data, std::size_t len) noexcept {
    std::uint32_t crc = 0xFFFFFFFFu;
    for (std::size_t i = 0; i < len; ++i) {
        crc ^= static_cast<std::uint32_t>(data[i]);
        for (int bit = 0; bit < 8; ++bit) {
            const std::uint32_t mask = static_cast<std::uint32_t>(-static_cast<std::int32_t>(crc & 1u));
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

/// Sanitizes an archive entry path (`/`-normalized), stripping leading slashes, drive letters,
/// and `.` or `..` segments so untrusted `.zip` / `.rswpak` entries can NEVER escape the target folder!
inline std::string SanitizeArchiveRelativePath(std::string_view rawPath) {
    std::string norm(rawPath);
    for (char& c : norm) {
        if (c == '\\') c = '/';
    }
    // Strip any leading `C:` drive prefix
    if (norm.size() >= 2 && norm[1] == ':') {
        norm.erase(0, 2);
    }
    std::vector<std::string> parts;
    std::size_t pos = 0;
    while (pos <= norm.size()) {
        const auto slash = norm.find('/', pos);
        std::string seg = (slash == std::string::npos)
            ? norm.substr(pos)
            : norm.substr(pos, slash - pos);
        // Strip characters forbidden in Windows path components
        std::string cleanSeg;
        cleanSeg.reserve(seg.size());
        for (char ch : seg) {
            if (ch != ':' && ch != '*' && ch != '?' && ch != '"' && ch != '<' && ch != '>' && ch != '|' && ch != '\0') {
                cleanSeg.push_back(ch);
            }
        }
        cleanSeg = TrimAscii(cleanSeg);
        if (!cleanSeg.empty() && cleanSeg != ".") {
            if (cleanSeg == "..") {
                if (!parts.empty()) parts.pop_back();
            } else {
                parts.push_back(std::move(cleanSeg));
            }
        }
        if (slash == std::string::npos) break;
        pos = slash + 1;
    }

    std::string joined;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) joined.push_back('/');
        joined += parts[i];
    }
    return joined;
}

namespace zip_detail {

/// LSB-first bitstream reader used by RFC 1951 DEFLATE decompression.
struct DeflateBitReader {
    const std::uint8_t* data    = nullptr;
    std::size_t         size    = 0;
    std::size_t         bytePos = 0;
    std::uint32_t       bitBuf  = 0;
    int                 bitCnt  = 0;

    bool EnsureBits(int need) noexcept {
        while (bitCnt < need) {
            if (bytePos >= size) return false;
            bitBuf |= (static_cast<std::uint32_t>(data[bytePos++]) << bitCnt);
            bitCnt += 8;
        }
        return true;
    }

    bool ReadBits(int count, std::uint32_t& outVal) noexcept {
        if (count == 0) {
            outVal = 0;
            return true;
        }
        if (!EnsureBits(count)) return false;
        outVal = bitBuf & ((1u << count) - 1u);
        bitBuf >>= count;
        bitCnt -= count;
        return true;
    }

    void AlignToByte() noexcept {
        const int drop = bitCnt & 7;
        bitBuf >>= drop;
        bitCnt -= drop;
    }

    bool ReadAlignedU16(std::uint16_t& outVal) noexcept {
        AlignToByte();
        std::uint32_t lo = 0, hi = 0;
        if (!ReadBits(8, lo) || !ReadBits(8, hi)) return false;
        outVal = static_cast<std::uint16_t>(lo | (hi << 8));
        return true;
    }
};

/// Canonical Huffman decoding table (up to 15-bit codes per RFC 1951 §3.2.2).
struct HuffmanTable {
    std::array<int, 16> count{};
    std::vector<int>    symbols;

    bool Build(const int* lengths, int numSymbols) {
        count.fill(0);
        for (int i = 0; i < numSymbols; ++i) {
            if (lengths[i] < 0 || lengths[i] > 15) return false;
            if (lengths[i] > 0) ++count[static_cast<std::size_t>(lengths[i])];
        }
        // Check for an over-subscribed code tree
        int left = 1;
        for (int len = 1; len <= 15; ++len) {
            left <<= 1;
            left -= count[static_cast<std::size_t>(len)];
            if (left < 0) return false;
        }

        std::array<int, 16> offsets{};
        int sum = 0;
        for (int len = 1; len <= 15; ++len) {
            offsets[static_cast<std::size_t>(len)] = sum;
            sum += count[static_cast<std::size_t>(len)];
        }
        symbols.assign(static_cast<std::size_t>(std::max(1, sum)), 0);
        for (int sym = 0; sym < numSymbols; ++sym) {
            const int len = lengths[sym];
            if (len > 0) {
                symbols[static_cast<std::size_t>(offsets[static_cast<std::size_t>(len)]++)] = sym;
            }
        }
        return true;
    }

    bool Decode(DeflateBitReader& br, int& outSymbol) const noexcept {
        int code  = 0;
        int first = 0;
        int index = 0;
        for (int len = 1; len <= 15; ++len) {
            std::uint32_t bit = 0;
            if (!br.ReadBits(1, bit)) return false;
            code |= static_cast<int>(bit);
            const int cnt = count[static_cast<std::size_t>(len)];
            if (code - cnt < first) {
                const int pos = index + (code - first);
                if (pos < 0 || static_cast<std::size_t>(pos) >= symbols.size()) return false;
                outSymbol = symbols[static_cast<std::size_t>(pos)];
                return true;
            }
            index += cnt;
            first += cnt;
            first <<= 1;
            code <<= 1;
        }
        return false;
    }
};

/// Decompresses a raw RFC 1951 DEFLATE stream (`method == 8` inside `.zip`).
inline bool InflateRawDeflate(
    const std::uint8_t* compData,
    std::size_t compSize,
    std::size_t expectedUncompSize,
    std::vector<std::uint8_t>& outBytes,
    std::string& outError)
{
    outBytes.clear();
    outBytes.reserve(expectedUncompSize);

    static constexpr int kLenBase[29] = {
        3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
        35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258
    };
    static constexpr int kLenExtra[29] = {
        0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2,
        3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0
    };
    static constexpr int kDistBase[30] = {
        1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145,
        8193, 12289, 16385, 24577
    };
    static constexpr int kDistExtra[30] = {
        0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,
        7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13
    };
    static constexpr int kCodeOrder[19] = {
        16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15
    };

    DeflateBitReader br{compData, compSize, 0, 0, 0};
    bool isFinalBlock = false;

    while (!isFinalBlock) {
        std::uint32_t bfinal = 0, btype = 0;
        if (!br.ReadBits(1, bfinal) || !br.ReadBits(2, btype)) {
            outError = "Truncated DEFLATE block header inside ZIP archive.";
            return false;
        }
        isFinalBlock = (bfinal != 0);

        if (btype == 0) {
            // Uncompressed stored block (`BTYPE = 00`)
            std::uint16_t len = 0, nlen = 0;
            if (!br.ReadAlignedU16(len) || !br.ReadAlignedU16(nlen) ||
                static_cast<std::uint16_t>(~len) != nlen) {
                outError = "Corrupted stored block length in DEFLATE stream.";
                return false;
            }
            for (std::uint16_t i = 0; i < len; ++i) {
                std::uint32_t byteVal = 0;
                if (!br.ReadBits(8, byteVal)) {
                    outError = "Unexpected EOF inside DEFLATE stored block.";
                    return false;
                }
                outBytes.push_back(static_cast<std::uint8_t>(byteVal));
            }
        } else if (btype == 1 || btype == 2) {
            HuffmanTable litLenTable{};
            HuffmanTable distTable{};

            if (btype == 1) {
                // Fixed Huffman tables (`BTYPE = 01`)
                int litLengths[288]{};
                for (int i = 0; i <= 143; ++i) litLengths[i] = 8;
                for (int i = 144; i <= 255; ++i) litLengths[i] = 9;
                for (int i = 256; i <= 279; ++i) litLengths[i] = 7;
                for (int i = 280; i <= 287; ++i) litLengths[i] = 8;
                int distLengths[32]{};
                for (int i = 0; i < 32; ++i) distLengths[i] = 5;
                litLenTable.Build(litLengths, 288);
                distTable.Build(distLengths, 32);
            } else {
                // Dynamic Huffman tables (`BTYPE = 10`)
                std::uint32_t hlitRaw = 0, hdistRaw = 0, hclenRaw = 0;
                if (!br.ReadBits(5, hlitRaw) || !br.ReadBits(5, hdistRaw) || !br.ReadBits(4, hclenRaw)) {
                    outError = "Truncated dynamic Huffman header in DEFLATE stream.";
                    return false;
                }
                const int hlit  = static_cast<int>(hlitRaw) + 257;
                const int hdist = static_cast<int>(hdistRaw) + 1;
                const int hclen = static_cast<int>(hclenRaw) + 4;

                int clLengths[19]{};
                for (int i = 0; i < hclen; ++i) {
                    std::uint32_t cl = 0;
                    if (!br.ReadBits(3, cl)) {
                        outError = "Truncated code-length table in DEFLATE stream.";
                        return false;
                    }
                    clLengths[kCodeOrder[i]] = static_cast<int>(cl);
                }
                HuffmanTable clTable{};
                if (!clTable.Build(clLengths, 19)) {
                    outError = "Invalid code-length Huffman tree in DEFLATE stream.";
                    return false;
                }

                std::vector<int> allLengths(static_cast<std::size_t>(hlit + hdist), 0);
                int idx = 0;
                while (idx < hlit + hdist) {
                    int sym = 0;
                    if (!clTable.Decode(br, sym)) {
                        outError = "Failed decoding dynamic Huffman code lengths.";
                        return false;
                    }
                    if (sym < 16) {
                        allLengths[static_cast<std::size_t>(idx++)] = sym;
                    } else if (sym == 16) {
                        std::uint32_t rep = 0;
                        if (idx == 0 || !br.ReadBits(2, rep)) {
                            outError = "Invalid repeat-previous code (16) in DEFLATE tree.";
                            return false;
                        }
                        const int prev = allLengths[static_cast<std::size_t>(idx - 1)];
                        const int count = 3 + static_cast<int>(rep);
                        for (int k = 0; k < count && idx < hlit + hdist; ++k) {
                            allLengths[static_cast<std::size_t>(idx++)] = prev;
                        }
                    } else if (sym == 17 || sym == 18) {
                        std::uint32_t rep = 0;
                        if (!br.ReadBits(sym == 17 ? 3 : 7, rep)) {
                            outError = "Truncated zero-run repeat code in DEFLATE tree.";
                            return false;
                        }
                        const int count = (sym == 17 ? 3 : 11) + static_cast<int>(rep);
                        for (int k = 0; k < count && idx < hlit + hdist; ++k) {
                            allLengths[static_cast<std::size_t>(idx++)] = 0;
                        }
                    } else {
                        outError = "Unexpected symbol in DEFLATE code-length tree.";
                        return false;
                    }
                }
                if (!litLenTable.Build(allLengths.data(), hlit) ||
                    !distTable.Build(allLengths.data() + hlit, hdist)) {
                    outError = "Invalid literal/distance Huffman tree in DEFLATE stream.";
                    return false;
                }
            }

            // Decode literals and LZ77 `(length, distance)` back-references until end-of-block (`256`)
            for (;;) {
                int sym = 0;
                if (!litLenTable.Decode(br, sym)) {
                    outError = "Failed decoding literal/length symbol in DEFLATE stream.";
                    return false;
                }
                if (sym < 256) {
                    outBytes.push_back(static_cast<std::uint8_t>(sym));
                } else if (sym == 256) {
                    break; // End of block!
                } else if (sym <= 285) {
                    const int lenIdx = sym - 257;
                    std::uint32_t extraLen = 0;
                    if (!br.ReadBits(kLenExtra[lenIdx], extraLen)) {
                        outError = "Truncated length extra bits in DEFLATE stream.";
                        return false;
                    }
                    const int matchLen = kLenBase[lenIdx] + static_cast<int>(extraLen);

                    int distSym = 0;
                    if (!distTable.Decode(br, distSym) || distSym < 0 || distSym >= 30) {
                        outError = "Invalid distance symbol in DEFLATE stream.";
                        return false;
                    }
                    std::uint32_t extraDist = 0;
                    if (!br.ReadBits(kDistExtra[distSym], extraDist)) {
                        outError = "Truncated distance extra bits in DEFLATE stream.";
                        return false;
                    }
                    const std::size_t matchDist = static_cast<std::size_t>(kDistBase[distSym] + static_cast<int>(extraDist));
                    if (matchDist == 0 || matchDist > outBytes.size()) {
                        outError = "DEFLATE LZ77 back-reference exceeds output window.";
                        return false;
                    }
                    for (int m = 0; m < matchLen; ++m) {
                        const std::uint8_t b = outBytes[outBytes.size() - matchDist];
                        outBytes.push_back(b);
                    }
                } else {
                    outError = "Reserved literal/length symbol (>285) in DEFLATE stream.";
                    return false;
                }
            }
        } else {
            outError = "Invalid DEFLATE block type (BTYPE=11 reserved).";
            return false;
        }
    }

    return true;
}

inline std::uint16_t ReadLe16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}

inline std::uint32_t ReadLe32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8) |
           (static_cast<std::uint32_t>(p[2]) << 16) |
           (static_cast<std::uint32_t>(p[3]) << 24);
}

inline std::uint64_t ReadLe64(const std::uint8_t* p) noexcept {
    return static_cast<std::uint64_t>(ReadLe32(p)) |
           (static_cast<std::uint64_t>(ReadLe32(p + 4)) << 32);
}

inline void AppendLe16(std::vector<std::uint8_t>& buf, std::uint16_t v) {
    buf.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
}

inline void AppendLe32(std::vector<std::uint8_t>& buf, std::uint32_t v) {
    buf.push_back(static_cast<std::uint8_t>(v & 0xFFu));
    buf.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFFu));
    buf.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFFu));
    buf.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFFu));
}

inline void AppendLe64(std::vector<std::uint8_t>& buf, std::uint64_t v) {
    AppendLe32(buf, static_cast<std::uint32_t>(v & 0xFFFFFFFFull));
    AppendLe32(buf, static_cast<std::uint32_t>((v >> 32) & 0xFFFFFFFFull));
}

} // namespace zip_detail

/// Extracts a standard `.zip` archive (`Store = 0` & `DEFLATE = 8`, UTF-8 & ANSI names) into `destDir`,
/// verifying every file's CRC-32 checksum and preserving MS-DOS modification timestamps!
inline bool ExtractZipArchiveToDirectory(
    const std::filesystem::path& zipFilePath,
    const std::filesystem::path& destDir,
    std::vector<std::filesystem::path>& outExtractedFiles,
    std::string& outError)
{
    outExtractedFiles.clear();
    std::ifstream in(zipFilePath, std::ios::binary | std::ios::ate);
    if (!in) {
        outError = "Could not open ZIP archive: " + WideToUtf8(zipFilePath.wstring());
        return false;
    }
    const auto fileSz = static_cast<std::size_t>(in.tellg());
    if (fileSz < 22) {
        outError = "File is too small to be a valid ZIP archive: " + WideToUtf8(zipFilePath.wstring());
        return false;
    }
    std::vector<std::uint8_t> raw(fileSz);
    in.seekg(0, std::ios::beg);
    if (!in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(fileSz))) {
        outError = "Failed reading ZIP archive bytes: " + WideToUtf8(zipFilePath.wstring());
        return false;
    }

    // Locate End of Central Directory (`0x06054b50`) scanning backward up to 64 KB + 22 bytes
    const std::size_t minEocdOff = (fileSz > 65557u) ? (fileSz - 65557u) : 0u;
    std::size_t eocdOff = fileSz;
    for (std::size_t pos = fileSz - 22; ; --pos) {
        if (zip_detail::ReadLe32(raw.data() + pos) == 0x06054B50u) {
            eocdOff = pos;
            break;
        }
        if (pos == minEocdOff) break;
    }
    if (eocdOff >= fileSz) {
        outError = "End-of-Central-Directory signature (PK\\x05\\x06) not found in ZIP archive.";
        return false;
    }

    const std::uint16_t entryCount = zip_detail::ReadLe16(raw.data() + eocdOff + 10);
    const std::uint32_t cdSize     = zip_detail::ReadLe32(raw.data() + eocdOff + 12);
    const std::uint32_t cdOffset   = zip_detail::ReadLe32(raw.data() + eocdOff + 16);
    if (static_cast<std::size_t>(cdOffset) + cdSize > fileSz) {
        outError = "ZIP Central Directory bounds exceed archive file size.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destDir, ec);

    std::size_t curCd = cdOffset;
    for (std::uint16_t i = 0; i < entryCount; ++i) {
        if (curCd + 46 > fileSz || zip_detail::ReadLe32(raw.data() + curCd) != 0x02014B50u) {
            outError = "Corrupted Central Directory entry header in ZIP archive.";
            return false;
        }
        const std::uint16_t gpFlags   = zip_detail::ReadLe16(raw.data() + curCd + 8);
        const std::uint16_t method    = zip_detail::ReadLe16(raw.data() + curCd + 10);
        const std::uint16_t dosTime   = zip_detail::ReadLe16(raw.data() + curCd + 12);
        const std::uint16_t dosDate   = zip_detail::ReadLe16(raw.data() + curCd + 14);
        const std::uint32_t expectedCrc = zip_detail::ReadLe32(raw.data() + curCd + 16);
        const std::uint32_t compSz    = zip_detail::ReadLe32(raw.data() + curCd + 20);
        const std::uint32_t uncompSz  = zip_detail::ReadLe32(raw.data() + curCd + 24);
        const std::uint16_t nameLen   = zip_detail::ReadLe16(raw.data() + curCd + 28);
        const std::uint16_t extraLen  = zip_detail::ReadLe16(raw.data() + curCd + 30);
        const std::uint16_t commLen   = zip_detail::ReadLe16(raw.data() + curCd + 32);
        const std::uint32_t localOff  = zip_detail::ReadLe32(raw.data() + curCd + 42);

        if (curCd + 46 + nameLen + extraLen + commLen > fileSz) {
            outError = "Truncated Central Directory filename/extra field in ZIP archive.";
            return false;
        }

        std::string rawEntryName(reinterpret_cast<const char*>(raw.data() + curCd + 46), nameLen);
        if ((gpFlags & 0x0800u) == 0 && !rawEntryName.empty()) {
            // Convert legacy CP437 / system ANSI ZIP entry names cleanly to UTF-8 via Win32
            const int wLen = ::MultiByteToWideChar(CP_ACP, 0, rawEntryName.data(), static_cast<int>(rawEntryName.size()), nullptr, 0);
            if (wLen > 0) {
                std::wstring ws(static_cast<std::size_t>(wLen), L'\0');
                ::MultiByteToWideChar(CP_ACP, 0, rawEntryName.data(), static_cast<int>(rawEntryName.size()), ws.data(), wLen);
                rawEntryName = WideToUtf8(ws);
            }
        }
        curCd += 46u + nameLen + extraLen + commLen;

        const bool isDirectory = (!rawEntryName.empty() && (rawEntryName.back() == '/' || rawEntryName.back() == '\\'));
        const std::string safeRel = SanitizeArchiveRelativePath(rawEntryName);
        if (safeRel.empty()) continue;

        const std::filesystem::path outPath = (destDir / Utf8ToWide(safeRel)).lexically_normal();
        if (isDirectory) {
            std::filesystem::create_directories(outPath, ec);
            continue;
        }

        if (localOff + 30 > fileSz || zip_detail::ReadLe32(raw.data() + localOff) != 0x04034B50u) {
            outError = "Invalid Local File Header (PK\\x03\\x04) for '" + safeRel + "'.";
            return false;
        }
        const std::uint16_t locNameLen  = zip_detail::ReadLe16(raw.data() + localOff + 26);
        const std::uint16_t locExtraLen = zip_detail::ReadLe16(raw.data() + localOff + 28);
        const std::size_t dataPos       = static_cast<std::size_t>(localOff) + 30u + locNameLen + locExtraLen;
        if (dataPos + compSz > fileSz) {
            outError = "Compressed data for '" + safeRel + "' exceeds ZIP archive size.";
            return false;
        }

        std::vector<std::uint8_t> decoded;
        if (method == 0) {
            if (compSz != uncompSz) {
                outError = "Stored ZIP entry '" + safeRel + "' has mismatched sizes.";
                return false;
            }
            decoded.assign(raw.data() + dataPos, raw.data() + dataPos + compSz);
        } else if (method == 8) {
            if (!zip_detail::InflateRawDeflate(raw.data() + dataPos, compSz, uncompSz, decoded, outError)) {
                return false;
            }
        } else {
            outError = "Unsupported ZIP compression method (" + std::to_string(method) + ") on '" + safeRel +
                       "'. Standard Store (0) and DEFLATE (8) are supported.";
            return false;
        }

        if (decoded.size() != uncompSz || ComputeCrc32(decoded.data(), decoded.size()) != expectedCrc) {
            outError = "CRC-32 integrity check failed while extracting '" + safeRel + "' from ZIP archive.";
            return false;
        }

        std::filesystem::create_directories(outPath.parent_path(), ec);
        std::ofstream out(outPath, std::ios::binary | std::ios::trunc);
        if (!out) {
            outError = "Failed creating extracted file: " + WideToUtf8(outPath.wstring());
            return false;
        }
        if (!decoded.empty()) {
            out.write(reinterpret_cast<const char*>(decoded.data()), static_cast<std::streamsize>(decoded.size()));
        }
        out.close();

        // Restore original modification timestamp from MS-DOS date/time when valid
        FILETIME localFt{}, utcFt{};
        if (dosDate != 0 && ::DosDateTimeToFileTime(dosDate, dosTime, &localFt) &&
            ::LocalFileTimeToFileTime(&localFt, &utcFt)) {
            HANDLE hSet = ::CreateFileW(outPath.wstring().c_str(), FILE_WRITE_ATTRIBUTES,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hSet != INVALID_HANDLE_VALUE) {
                ::SetFileTime(hSet, nullptr, nullptr, &utcFt);
                ::CloseHandle(hSet);
            }
        }

        outExtractedFiles.push_back(outPath);
    }
    return true;
}

/// Emits a valid standard PKZIP archive (using RFC 1951 DEFLATE stored blocks `method = 8` for multi-byte
/// entries and `method = 0` for empty entries) so automated self-tests (`insttests.exe`) can build and
/// round-trip `.zip` inputs in memory without external CLI dependencies!
inline bool WriteSimpleZipArchive(
    const std::vector<std::pair<std::string, std::vector<std::uint8_t>>>& files,
    const std::filesystem::path& outZipPath,
    std::string& outError)
{
    std::vector<std::uint8_t> archive;
    struct CdRecord {
        std::string   utf8Name;
        std::uint16_t method    = 0;
        std::uint32_t crc32     = 0;
        std::uint32_t compSz    = 0;
        std::uint32_t uncompSz  = 0;
        std::uint32_t localOff  = 0;
    };
    std::vector<CdRecord> cdEntries;

    for (const auto& [rawName, content] : files) {
        std::string cleanName = SanitizeArchiveRelativePath(rawName);
        if (cleanName.empty()) continue;

        const std::uint32_t crc   = ComputeCrc32(content.data(), content.size());
        const std::uint32_t lOff  = static_cast<std::uint32_t>(archive.size());
        const bool useDeflateWrap = !content.empty() && content.size() <= 65535u;
        const std::uint16_t meth  = useDeflateWrap ? 8u : 0u;

        std::vector<std::uint8_t> payload;
        if (useDeflateWrap) {
            // Single final RFC 1951 stored block (`BFINAL=1, BTYPE=00` -> byte `0x01` + `LEN` + `~LEN` + data)
            const auto len16  = static_cast<std::uint16_t>(content.size());
            const auto nlen16 = static_cast<std::uint16_t>(~len16);
            payload.reserve(5 + content.size());
            payload.push_back(0x01u);
            zip_detail::AppendLe16(payload, len16);
            zip_detail::AppendLe16(payload, nlen16);
            payload.insert(payload.end(), content.begin(), content.end());
        } else {
            payload = content;
        }

        // Local File Header (`0x04034B50`)
        zip_detail::AppendLe32(archive, 0x04034B50u);
        zip_detail::AppendLe16(archive, 20u);
        zip_detail::AppendLe16(archive, 0x0800u); // UTF-8 bit 11
        zip_detail::AppendLe16(archive, meth);
        zip_detail::AppendLe16(archive, 0x6000u); // 12:00:00
        zip_detail::AppendLe16(archive, 0x5CFBu); // 2026-07-27
        zip_detail::AppendLe32(archive, crc);
        zip_detail::AppendLe32(archive, static_cast<std::uint32_t>(payload.size()));
        zip_detail::AppendLe32(archive, static_cast<std::uint32_t>(content.size()));
        zip_detail::AppendLe16(archive, static_cast<std::uint16_t>(cleanName.size()));
        zip_detail::AppendLe16(archive, 0u);
        archive.insert(archive.end(), cleanName.begin(), cleanName.end());
        archive.insert(archive.end(), payload.begin(), payload.end());

        cdEntries.push_back({
            cleanName, meth, crc,
            static_cast<std::uint32_t>(payload.size()),
            static_cast<std::uint32_t>(content.size()),
            lOff
        });
    }

    const std::uint32_t cdStart = static_cast<std::uint32_t>(archive.size());
    for (const auto& ce : cdEntries) {
        zip_detail::AppendLe32(archive, 0x02014B50u);
        zip_detail::AppendLe16(archive, 20u);
        zip_detail::AppendLe16(archive, 20u);
        zip_detail::AppendLe16(archive, 0x0800u);
        zip_detail::AppendLe16(archive, ce.method);
        zip_detail::AppendLe16(archive, 0x6000u);
        zip_detail::AppendLe16(archive, 0x5CFBu);
        zip_detail::AppendLe32(archive, ce.crc32);
        zip_detail::AppendLe32(archive, ce.compSz);
        zip_detail::AppendLe32(archive, ce.uncompSz);
        zip_detail::AppendLe16(archive, static_cast<std::uint16_t>(ce.utf8Name.size()));
        zip_detail::AppendLe16(archive, 0u);
        zip_detail::AppendLe16(archive, 0u);
        zip_detail::AppendLe16(archive, 0u);
        zip_detail::AppendLe16(archive, 0u);
        zip_detail::AppendLe32(archive, 0u);
        zip_detail::AppendLe32(archive, ce.localOff);
        archive.insert(archive.end(), ce.utf8Name.begin(), ce.utf8Name.end());
    }
    const std::uint32_t cdBytes = static_cast<std::uint32_t>(archive.size()) - cdStart;

    // End of Central Directory (`0x06054B50`)
    zip_detail::AppendLe32(archive, 0x06054B50u);
    zip_detail::AppendLe16(archive, 0u);
    zip_detail::AppendLe16(archive, 0u);
    zip_detail::AppendLe16(archive, static_cast<std::uint16_t>(cdEntries.size()));
    zip_detail::AppendLe16(archive, static_cast<std::uint16_t>(cdEntries.size()));
    zip_detail::AppendLe32(archive, cdBytes);
    zip_detail::AppendLe32(archive, cdStart);
    zip_detail::AppendLe16(archive, 0u);

    std::error_code ec;
    std::filesystem::create_directories(outZipPath.parent_path(), ec);
    std::ofstream out(outZipPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        outError = "Could not write ZIP file: " + WideToUtf8(outZipPath.wstring());
        return false;
    }
    out.write(reinterpret_cast<const char*>(archive.data()), static_cast<std::streamsize>(archive.size()));
    return out.good();
}

// ================================================================================
// `RSWPAK1` SOLID `LZMS` CONTAINER (FOR SFX, `Engine = native`, & `.rswpak` PACKS!)
// ================================================================================
struct RswPakInputItem {
    std::filesystem::path sourcePath;
    std::string           archiveRelPath; // e.g. `"HelloApp.exe"` (SFX) or `"INSTALLDIR/HelloApp.exe"` (Native/FeaturePack)
    std::string           featureId;      // Optional feature tag (`"Main"`, `"Templates"`, etc.)
};

struct RswPakFileMetadata {
    std::string   archiveRelPath;
    std::string   featureId;
    std::uint64_t payloadOffset      = 0;
    std::uint64_t payloadSize        = 0;
    std::uint64_t lastWriteTicksUtc  = 0;
    std::uint32_t crc32              = 0;
};

inline constexpr char kRswPakMagic[8] = {'R', 'S', 'W', 'P', 'A', 'K', '1', '\0'};

/// Builds a solid `LZMS`-compressed `RSWPAK1` archive in memory (`outPakBytes`).
inline bool BuildRswPakBytes(
    const std::vector<RswPakInputItem>& items,
    std::vector<std::uint8_t>& outPakBytes,
    std::uint64_t& outUncompressedTotal,
    std::string& outError)
{
    outPakBytes.clear();
    outUncompressedTotal = 0;

    std::vector<RswPakFileMetadata> metas;
    metas.reserve(items.size());
    std::vector<std::uint8_t> concatRaw;

    for (const auto& item : items) {
        const std::string safeRel = SanitizeArchiveRelativePath(item.archiveRelPath);
        if (safeRel.empty()) continue;

        std::ifstream in(item.sourcePath, std::ios::binary | std::ios::ate);
        if (!in) {
            outError = "Could not open payload file for RSWPAK packing: " + WideToUtf8(item.sourcePath.wstring());
            return false;
        }
        const auto sz = static_cast<std::size_t>(in.tellg());
        in.seekg(0, std::ios::beg);

        const std::uint64_t offset = static_cast<std::uint64_t>(concatRaw.size());
        concatRaw.resize(concatRaw.size() + sz);
        if (sz > 0 && !in.read(reinterpret_cast<char*>(concatRaw.data() + offset), static_cast<std::streamsize>(sz))) {
            outError = "Failed reading payload file: " + WideToUtf8(item.sourcePath.wstring());
            return false;
        }

        std::uint64_t ftTicks = 0;
        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (::GetFileAttributesExW(item.sourcePath.wstring().c_str(), GetFileExInfoStandard, &fad)) {
            ftTicks = (static_cast<std::uint64_t>(fad.ftLastWriteTime.dwHighDateTime) << 32) |
                      static_cast<std::uint64_t>(fad.ftLastWriteTime.dwLowDateTime);
        }
        const std::uint32_t crc = ComputeCrc32(concatRaw.data() + offset, sz);
        metas.push_back({safeRel, item.featureId, offset, static_cast<std::uint64_t>(sz), ftTicks, crc});
        outUncompressedTotal += static_cast<std::uint64_t>(sz);
    }

    const std::vector<std::uint8_t> compressedStream = CompressWithLzmsIfSmaller(concatRaw);

    // Serialize `RSWPAK1\0` header + directory table + solid LZMS stream
    outPakBytes.insert(outPakBytes.end(), std::begin(kRswPakMagic), std::end(kRswPakMagic));
    zip_detail::AppendLe32(outPakBytes, 1u); // Version 1
    zip_detail::AppendLe32(outPakBytes, static_cast<std::uint32_t>(metas.size()));
    zip_detail::AppendLe64(outPakBytes, outUncompressedTotal);

    for (const auto& m : metas) {
        zip_detail::AppendLe16(outPakBytes, static_cast<std::uint16_t>(m.archiveRelPath.size()));
        outPakBytes.insert(outPakBytes.end(), m.archiveRelPath.begin(), m.archiveRelPath.end());
        zip_detail::AppendLe16(outPakBytes, static_cast<std::uint16_t>(m.featureId.size()));
        outPakBytes.insert(outPakBytes.end(), m.featureId.begin(), m.featureId.end());
        zip_detail::AppendLe64(outPakBytes, m.payloadOffset);
        zip_detail::AppendLe64(outPakBytes, m.payloadSize);
        zip_detail::AppendLe64(outPakBytes, m.lastWriteTicksUtc);
        zip_detail::AppendLe32(outPakBytes, m.crc32);
    }

    zip_detail::AppendLe64(outPakBytes, static_cast<std::uint64_t>(compressedStream.size()));
    outPakBytes.insert(outPakBytes.end(), compressedStream.begin(), compressedStream.end());
    return true;
}

/// Writes a solid `LZMS` `.rswpak` file to disk (`<OutputStem>-<FeatureId>.rswpak`).
inline bool WriteRswPakFile(
    const std::vector<RswPakInputItem>& items,
    const std::filesystem::path& outPakPath,
    std::uint64_t& outUncompressedTotal,
    std::string& outSha256Hex,
    std::string& outError)
{
    std::vector<std::uint8_t> pakBytes;
    if (!BuildRswPakBytes(items, pakBytes, outUncompressedTotal, outError)) {
        return false;
    }
    std::error_code ec;
    std::filesystem::create_directories(outPakPath.parent_path(), ec);
    std::ofstream out(outPakPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        outError = "Could not create .rswpak file: " + WideToUtf8(outPakPath.wstring());
        return false;
    }
    out.write(reinterpret_cast<const char*>(pakBytes.data()), static_cast<std::streamsize>(pakBytes.size()));
    out.close();
    if (!ComputeFileSha256Hex(outPakPath, outSha256Hex)) {
        outError = "Failed computing SHA-256 digest for .rswpak file.";
        return false;
    }
    return true;
}

/// Parses an `RSWPAK1` byte buffer and decompresses its solid `LZMS` payload in memory so callers
/// can inspect metadata and extract selectively (with optional feature filtering, rollback journaling,
/// and per-file progress callbacks!).
inline bool UnpackRswPakBytesInMemory(
    const std::uint8_t* pakData,
    std::size_t pakSize,
    std::vector<RswPakFileMetadata>& outMetas,
    std::vector<std::uint8_t>& outConcatPayload,
    std::string& outError)
{
    outMetas.clear();
    outConcatPayload.clear();
    if (!pakData || pakSize < 32 || std::memcmp(pakData, kRswPakMagic, 8) != 0) {
        outError = "Invalid RSWPAK1 container header.";
        return false;
    }

    const std::uint32_t ver   = zip_detail::ReadLe32(pakData + 8);
    const std::uint32_t count = zip_detail::ReadLe32(pakData + 12);
    const std::uint64_t total = zip_detail::ReadLe64(pakData + 16);
    if (ver != 1) {
        outError = "Unsupported RSWPAK container version (" + std::to_string(ver) + ").";
        return false;
    }

    std::size_t pos = 24;
    outMetas.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (pos + 2 > pakSize) { outError = "Truncated RSWPAK path length."; return false; }
        const std::uint16_t pathLen = zip_detail::ReadLe16(pakData + pos);
        pos += 2;
        if (pos + pathLen + 2 > pakSize) { outError = "Truncated RSWPAK path string."; return false; }
        std::string relPath(reinterpret_cast<const char*>(pakData + pos), pathLen);
        pos += pathLen;

        const std::uint16_t featLen = zip_detail::ReadLe16(pakData + pos);
        pos += 2;
        if (pos + featLen + 28 > pakSize) { outError = "Truncated RSWPAK metadata entry."; return false; }
        std::string featId(reinterpret_cast<const char*>(pakData + pos), featLen);
        pos += featLen;

        RswPakFileMetadata m{};
        m.archiveRelPath    = SanitizeArchiveRelativePath(relPath);
        m.featureId         = std::move(featId);
        m.payloadOffset     = zip_detail::ReadLe64(pakData + pos);
        m.payloadSize       = zip_detail::ReadLe64(pakData + pos + 8);
        m.lastWriteTicksUtc = zip_detail::ReadLe64(pakData + pos + 16);
        m.crc32             = zip_detail::ReadLe32(pakData + pos + 24);
        pos += 28;
        outMetas.push_back(std::move(m));
    }

    if (pos + 8 > pakSize) {
        outError = "Truncated RSWPAK stream size.";
        return false;
    }
    const std::uint64_t compLen = zip_detail::ReadLe64(pakData + pos);
    pos += 8;
    if (pos + compLen > pakSize) {
        outError = "Truncated RSWPAK compressed stream.";
        return false;
    }

    if (!DecompressLzmsOrCopyRaw(pakData + pos, static_cast<std::size_t>(compLen), outConcatPayload, outError)) {
        return false;
    }
    if (outConcatPayload.size() != total) {
        outError = "Decompressed RSWPAK stream size mismatch.";
        return false;
    }

    for (const auto& m : outMetas) {
        if (m.payloadOffset + m.payloadSize > outConcatPayload.size()) {
            outError = "RSWPAK entry bounds exceed decompressed stream.";
            return false;
        }
        const std::uint32_t actualCrc = ComputeCrc32(
            outConcatPayload.data() + m.payloadOffset, static_cast<std::size_t>(m.payloadSize));
        if (actualCrc != m.crc32) {
            outError = "CRC-32 mismatch inside RSWPAK entry '" + m.archiveRelPath + "'.";
            return false;
        }
    }
    return true;
}

/// Extracts an `RSWPAK1` file on disk directly into `destRootDir` (used by `.rswpak` feature packs & SFX!).
inline bool ExtractRswPakFileToDirectory(
    const std::filesystem::path& pakFilePath,
    const std::filesystem::path& destRootDir,
    const std::atomic<bool>* cancelFlag,
    const std::function<void(int permille, const std::string& currentRelPath)>& onProgress,
    std::string& outError)
{
    std::ifstream in(pakFilePath, std::ios::binary | std::ios::ate);
    if (!in) {
        outError = "Could not open .rswpak file: " + WideToUtf8(pakFilePath.wstring());
        return false;
    }
    const auto sz = static_cast<std::size_t>(in.tellg());
    std::vector<std::uint8_t> raw(sz);
    in.seekg(0, std::ios::beg);
    if (sz > 0 && !in.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(sz))) {
        outError = "Failed reading .rswpak file: " + WideToUtf8(pakFilePath.wstring());
        return false;
    }

    std::vector<RswPakFileMetadata> metas;
    std::vector<std::uint8_t> payload;
    if (!UnpackRswPakBytesInMemory(raw.data(), raw.size(), metas, payload, outError)) {
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(destRootDir, ec);
    for (std::size_t i = 0; i < metas.size(); ++i) {
        if (cancelFlag && cancelFlag->load()) {
            outError = "Extraction canceled by user.";
            return false;
        }
        const auto& m = metas[i];
        if (m.archiveRelPath.empty()) continue;
        if (onProgress) {
            const int permille = static_cast<int>(((i + 1) * 1000u) / std::max<std::size_t>(1, metas.size()));
            onProgress(permille, m.archiveRelPath);
        }

        const std::filesystem::path outFile = (destRootDir / Utf8ToWide(m.archiveRelPath)).lexically_normal();
        std::filesystem::create_directories(outFile.parent_path(), ec);
        std::ofstream out(outFile, std::ios::binary | std::ios::trunc);
        if (!out) {
            outError = "Could not write extracted file: " + WideToUtf8(outFile.wstring());
            return false;
        }
        if (m.payloadSize > 0) {
            out.write(reinterpret_cast<const char*>(payload.data() + m.payloadOffset),
                      static_cast<std::streamsize>(m.payloadSize));
        }
        out.close();

        if (m.lastWriteTicksUtc != 0) {
            FILETIME ft{};
            ft.dwLowDateTime  = static_cast<DWORD>(m.lastWriteTicksUtc & 0xFFFFFFFFull);
            ft.dwHighDateTime = static_cast<DWORD>((m.lastWriteTicksUtc >> 32) & 0xFFFFFFFFull);
            HANDLE hSet = ::CreateFileW(outFile.wstring().c_str(), FILE_WRITE_ATTRIBUTES,
                                        FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
            if (hSet != INVALID_HANDLE_VALUE) {
                ::SetFileTime(hSet, nullptr, nullptr, &ft);
                ::CloseHandle(hSet);
            }
        }
    }
    return true;
}

/// Checks Windows `WinVerifyTrust` (`WINTRUST_ACTION_GENERIC_VERIFY_V2`) on `filePath`.
/// Returns `0` (`ERROR_SUCCESS`) for a trusted Authenticode signature, `TRUST_E_NOSIGNATURE` /
/// `TRUST_E_SUBJECT_FORM_UNKNOWN` when unsigned/raw-pack, or a negative HRESULT when a signature
/// IS present and tampered/untrusted!
inline LONG QueryFileAuthenticodeTrustStatus(const std::filesystem::path& filePath) noexcept {
    const std::wstring wPath = filePath.wstring();
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct       = sizeof(fileInfo);
    fileInfo.pcwszFilePath  = wPath.c_str();

    GUID policyGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA wtd{};
    wtd.cbStruct            = sizeof(wtd);
    wtd.dwUIChoice          = WTD_UI_NONE;
    wtd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wtd.dwUnionChoice       = WTD_CHOICE_FILE;
    wtd.pFile               = &fileInfo;
    wtd.dwStateAction       = WTD_STATEACTION_VERIFY;

    const LONG status = ::WinVerifyTrust(INVALID_HANDLE_VALUE, &policyGuid, &wtd);
    wtd.dwStateAction = WTD_STATEACTION_CLOSE;
    ::WinVerifyTrust(INVALID_HANDLE_VALUE, &policyGuid, &wtd);
    return status;
}

/// Verifies BOTH pinned `SHA-256` (`bcrypt.dll`) AND non-tampered Authenticode status (`WinVerifyTrust`)
/// on a downloaded or side-by-side `.rswpak` feature pack!
inline bool VerifyFeaturePackIntegrity(
    const std::filesystem::path& pakPath,
    std::string_view expectedSha256Hex,
    bool requireAuthenticode,
    std::string& outError)
{
    if (!expectedSha256Hex.empty()) {
        std::string actualHex;
        if (!ComputeFileSha256Hex(pakPath, actualHex) || !EqualsIgnoreCase(actualHex, expectedSha256Hex)) {
            outError = "Feature pack SHA-256 checksum mismatch on '" + WideToUtf8(pakPath.filename().wstring()) + "'.";
            return false;
        }
    }
    const LONG trust = QueryFileAuthenticodeTrustStatus(pakPath);
    if (trust == ERROR_SUCCESS) {
        return true;
    }
    if (!requireAuthenticode &&
        (trust == TRUST_E_NOSIGNATURE || trust == TRUST_E_SUBJECT_FORM_UNKNOWN || trust == TRUST_E_PROVIDER_UNKNOWN)) {
        return true;
    }
    outError = "Authenticode WinVerifyTrust check failed on '" + WideToUtf8(pakPath.filename().wstring()) +
               "' (HRESULT 0x" + std::to_string(static_cast<std::uint32_t>(trust)) + ").";
    return false;
}

// ================================================================================
// `.RSWPAK` FEATURE PACK MANIFEST (`kResIdFeaturePackManifest` `#215`)
// ================================================================================
struct FeaturePackRuntimeEntry {
    std::string   featureId;
    std::string   fileName;
    std::string   downloadUrl;
    std::string   sha256Hex;
    std::uint64_t uncompressedBytes = 0;
};

inline std::string SerializeFeaturePackManifestIni(const std::vector<FeaturePackRuntimeEntry>& packs) {
    std::ostringstream oss;
    oss << "[FeaturePacks]\nCount=" << packs.size() << "\n";
    for (std::size_t i = 0; i < packs.size(); ++i) {
        const std::string pfx = "Pack_" + std::to_string(i) + "_";
        oss << pfx << "Feature=" << packs[i].featureId << "\n"
            << pfx << "FileName=" << packs[i].fileName << "\n"
            << pfx << "Url=" << packs[i].downloadUrl << "\n"
            << pfx << "Sha256=" << packs[i].sha256Hex << "\n"
            << pfx << "UncompressedBytes=" << packs[i].uncompressedBytes << "\n";
    }
    return oss.str();
}

inline std::vector<FeaturePackRuntimeEntry> ParseFeaturePackManifestIni(std::string_view text) {
    std::map<std::string, std::string> kv;
    std::istringstream iss{std::string(text)};
    std::string line;
    while (std::getline(iss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '[' || line.front() == ';') continue;
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            kv[line.substr(0, eq)] = line.substr(eq + 1);
        }
    }
    const int count = std::clamp(std::atoi(kv["Count"].c_str()), 0, 128);
    std::vector<FeaturePackRuntimeEntry> out;
    for (int i = 0; i < count; ++i) {
        const std::string pfx = "Pack_" + std::to_string(i) + "_";
        FeaturePackRuntimeEntry fp{};
        fp.featureId = TrimAscii(kv[pfx + "Feature"]);
        fp.fileName  = TrimAscii(kv[pfx + "FileName"]);
        fp.downloadUrl = TrimAscii(kv[pfx + "Url"]);
        fp.sha256Hex = AsciiLower(TrimAscii(kv[pfx + "Sha256"]));
        if (!kv[pfx + "UncompressedBytes"].empty()) {
            fp.uncompressedBytes = static_cast<std::uint64_t>(std::strtoull(kv[pfx + "UncompressedBytes"].c_str(), nullptr, 10));
        }
        if (!fp.featureId.empty() && !fp.fileName.empty()) {
            out.push_back(std::move(fp));
        }
    }
    return out;
}

} // namespace rsw
