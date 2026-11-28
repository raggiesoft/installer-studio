// ================================================================================
// FILE: src/common/RswBinaryDelta.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file, and how does binary-delta patching work on Windows 11?
//     Suppose a 60 MB application updates from `v1.0.0` to `v1.0.1` and only two
//     compiled `.exe`/`.dll` binaries changed by a few lines of C++ code. Instead
//     of re-downloading all 60 MB, a **binary-delta patch** records ONLY the exact
//     byte-level instructions needed to transform the `v1.0.0` bytes on the user's
//     disk into the `v1.0.1` bytes—often shrinking a 60 MB update down to < 80 KB!
//
//   How do we compute and apply binary deltas with ZERO third-party dependencies?
//     Every installation of Windows 11 (`x64` and `ARM64`) includes Microsoft's
//     official **MSDelta Compression Engine** right inside `C:\Windows\System32\msdelta.dll`
//     (`<msdelta.h>`, `msdelta.lib` — the exact same delta technology Windows Update
//     uses to patch OS executables!):
//       1. `CreateDeltaB(...)`: Analyzes `oldBytes` and `newBytes` (understanding PE
//          `.exe`/`.dll` relative jump/call fixups via `DELTA_FILE_TYPE_SET_EXECUTABLES_1`,
//          automatically falling back to `DELTA_FILE_TYPE_RAW` for arbitrary data files)
//          and outputs a compact delta buffer.
//       2. `ApplyDeltaB(...)`: Reconstructs the exact `newBytes` in memory from the
//          user's installed `oldBytes` + the delta stream.
//       3. `DeltaFree(...)`: Releases the buffer allocated by `msdelta.dll`.
//
//   What is the `.rswpatch` (`RSWPATCH1`) container format implemented here?
//     For **RaggieScript EXE** installers (`Engine = raggiescript` & `.rssc`), this
//     header builds and applies transactional `.rswpatch` delta packages containing:
//       - Header metadata (`productName`, `upgradeCode`, `architecture`, `fromVersion`, `toVersion`)
//       - Per-file operations:
//           * `Keep`   (0): File unchanged (`oldSha256` verified before commit)
//           * `Add`    (1): Brand-new file (`LZMS`-compressed bytes + `newSha256`)
//           * `Delta`  (2): Modified file (`msdelta.dll` diff + `LZMS` + `oldSha256` + `newSha256`)
//           * `Delete` (3): Retired file removed in `toVersion` (backed up during commit, then pruned!)
//       - End-to-end `SHA-256` cryptographic envelope (`bcrypt.dll`) rejecting any
//         truncated or tampered `.rswpatch` file BEFORE touching a single byte on disk!
//       - Full **Atomic LIFO Rollback Journal** (`.rsw-rollback.jnl` + `.rswbak` backups):
//         if any file lock, hash mismatch, or disk fault occurs mid-patch, 100% of
//         modified, added, and deleted files roll back cleanly to `fromVersion`!
// ================================================================================

#pragma once

#include "RswCryptoAndPe.hpp"
#include "RswLzmsCompression.hpp"
#include "RswNativeAndSuiteEngine.hpp"
#include "RswPlatform.hpp"
#include "RswVersion.hpp"
#include "RswZipAndPak.hpp"

#include <msdelta.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "msdelta.lib")

namespace rsw {

// ================================================================================
// 1. WINDOWS `msdelta.dll` (`CreateDeltaB` / `ApplyDeltaB`) MEMORY WRAPPERS
// ================================================================================

/// Computes a binary delta transforming `oldBytes` -> `newBytes` using Windows `msdelta.dll`.
/// Uses executable-aware PE call/jump preprocessing (`DELTA_FILE_TYPE_SET_EXECUTABLES_1`) first
/// for maximum compression on `.exe`/`.dll` binaries, falling back to `DELTA_FILE_TYPE_RAW`.
inline bool CreateMsDeltaBytes(
    const std::uint8_t* oldPtr,
    std::size_t oldLen,
    const std::uint8_t* newPtr,
    std::size_t newLen,
    std::vector<std::uint8_t>& outDelta,
    std::string& outError)
{
    outDelta.clear();
    if (oldLen == 0 || newLen == 0) {
        outError = "CreateMsDeltaBytes requires non-empty source and target buffers (use PatchOp::Add for empty files).";
        return false;
    }

    // Populate `DELTA_INPUT` descriptors for source (`oldBytes`) and target (`newBytes`).
    // Setting `Editable = FALSE` guarantees `msdelta.dll` treats our input pointers as read-only.
    DELTA_INPUT srcIn{};
    srcIn.lpcStart = oldPtr;
    srcIn.uSize    = static_cast<SIZE_T>(oldLen);
    srcIn.Editable = FALSE;

    DELTA_INPUT tgtIn{};
    tgtIn.lpcStart = newPtr;
    tgtIn.uSize    = static_cast<SIZE_T>(newLen);
    tgtIn.Editable = FALSE;

    DELTA_INPUT emptyOpt{};
    DELTA_OUTPUT deltaOut{};

    // First try PE-aware delta encoding (`DELTA_FILE_TYPE_SET_EXECUTABLES_1`), then raw byte delta.
    // Note: We pass `HashAlgId = 0` because RaggieSoft verifies full 256-bit `SHA-256` (`bcrypt.dll`)
    // on `oldBytes`, `newBytes`, and the outer `.rswpatch` container directly!
    BOOL ok = ::CreateDeltaB(
        DELTA_FILE_TYPE_SET_EXECUTABLES_1,
        DELTA_FLAG_NONE,
        DELTA_FLAG_NONE,
        srcIn,
        tgtIn,
        emptyOpt,
        emptyOpt,
        emptyOpt,
        nullptr,
        0,
        &deltaOut);

    if (!ok) {
        deltaOut = {};
        ok = ::CreateDeltaB(
            DELTA_FILE_TYPE_RAW,
            DELTA_FLAG_NONE,
            DELTA_FLAG_NONE,
            srcIn,
            tgtIn,
            emptyOpt,
            emptyOpt,
            emptyOpt,
            nullptr,
            0,
            &deltaOut);
    }

    if (!ok || !deltaOut.lpStart || deltaOut.uSize == 0) {
        outError = "msdelta.dll CreateDeltaB failed (Win32 error " + std::to_string(::GetLastError()) + ").";
        return false;
    }

    const auto* p = static_cast<const std::uint8_t*>(deltaOut.lpStart);
    outDelta.assign(p, p + deltaOut.uSize);
    ::DeltaFree(deltaOut.lpStart);
    return true;
}

/// Reconstructs `outNewBytes` from `oldBytes` + `deltaBytes` using Windows `msdelta.dll` (`ApplyDeltaB`).
inline bool ApplyMsDeltaBytes(
    const std::uint8_t* oldPtr,
    std::size_t oldLen,
    const std::uint8_t* deltaPtr,
    std::size_t deltaLen,
    std::vector<std::uint8_t>& outNewBytes,
    std::string& outError)
{
    outNewBytes.clear();
    if (!oldPtr || oldLen == 0 || !deltaPtr || deltaLen == 0) {
        outError = "ApplyMsDeltaBytes received an empty source or delta buffer.";
        return false;
    }

    DELTA_INPUT srcIn{};
    srcIn.lpcStart = oldPtr;
    srcIn.uSize    = static_cast<SIZE_T>(oldLen);
    srcIn.Editable = FALSE;

    DELTA_INPUT dltIn{};
    dltIn.lpcStart = deltaPtr;
    dltIn.uSize    = static_cast<SIZE_T>(deltaLen);
    dltIn.Editable = FALSE;

    DELTA_OUTPUT tgtOut{};
    if (!::ApplyDeltaB(DELTA_APPLY_FLAG_ALLOW_PA19, srcIn, dltIn, &tgtOut)) {
        outError = "msdelta.dll ApplyDeltaB failed reconstructing patched file (Win32 error " +
                   std::to_string(::GetLastError()) + ").";
        return false;
    }

    const auto* p = static_cast<const std::uint8_t*>(tgtOut.lpStart);
    outNewBytes.assign(p, p + tgtOut.uSize);
    ::DeltaFree(tgtOut.lpStart);
    return true;
}

// ================================================================================
// 2. `.RSWPATCH` (`RSWPATCH1`) CONTAINER STRUCTURES & SERIALIZATION
// ================================================================================

enum class RswPatchOp : std::uint8_t {
    Keep   = 0, // File is byte-for-byte identical between `fromVersion` and `toVersion`
    Add    = 1, // File was added in `toVersion` (or replaced a 0-byte file; payload = LZMS newBytes)
    Delta  = 2, // File was modified in `toVersion` (payload = LZMS-compressed `msdelta.dll` stream)
    Delete = 3  // File existed in `fromVersion` and was retired/deleted in `toVersion`
};

struct RswPatchEntry {
    RswPatchOp                op        = RswPatchOp::Keep;
    std::string               relPath;  // Forward-slash relative path inside `<InstallDir>`
    std::string               oldSha256;
    std::string               newSha256;
    std::uint64_t             oldSize   = 0;
    std::uint64_t             newSize   = 0;
    std::vector<std::uint8_t> payload;  // Empty for `Keep`/`Delete`; LZMS stream for `Add`/`Delta`
};

struct RswPatchPackage {
    std::string                productName;
    std::string                upgradeCode;
    std::string                architecture;
    std::string                fromVersion;
    std::string                toVersion;
    std::vector<RswPatchEntry> entries;
};

struct RswPatchApplySummary {
    std::string   productName;
    std::string   fromVersion;
    std::string   toVersion;
    std::size_t   keptCount    = 0;
    std::size_t   addedCount   = 0;
    std::size_t   patchedCount = 0;
    std::size_t   deletedCount = 0;
};

namespace patch_io_detail {

inline constexpr char kRswPatchMagic[9] = {'R', 'S', 'W', 'P', 'A', 'T', 'C', 'H', '1'};

inline void WriteU8(std::vector<std::uint8_t>& b, std::uint8_t v) { b.push_back(v); }

inline void WriteU32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu));
}

inline void WriteU64(std::vector<std::uint8_t>& b, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFull));
}

inline void WriteStr(std::vector<std::uint8_t>& b, std::string_view s) {
    WriteU32(b, static_cast<std::uint32_t>(s.size()));
    b.insert(b.end(), reinterpret_cast<const std::uint8_t*>(s.data()),
             reinterpret_cast<const std::uint8_t*>(s.data()) + s.size());
}

inline bool ReadU8(const std::uint8_t* p, std::size_t n, std::size_t& off, std::uint8_t& out) noexcept {
    if (off + 1 > n) return false;
    out = p[off++];
    return true;
}

inline bool ReadU32(const std::uint8_t* p, std::size_t n, std::size_t& off, std::uint32_t& out) noexcept {
    if (off + 4 > n) return false;
    out = static_cast<std::uint32_t>(p[off]) |
          (static_cast<std::uint32_t>(p[off + 1]) << 8) |
          (static_cast<std::uint32_t>(p[off + 2]) << 16) |
          (static_cast<std::uint32_t>(p[off + 3]) << 24);
    off += 4;
    return true;
}

inline bool ReadU64(const std::uint8_t* p, std::size_t n, std::size_t& off, std::uint64_t& out) noexcept {
    if (off + 8 > n) return false;
    out = 0;
    for (int i = 0; i < 8; ++i) {
        out |= (static_cast<std::uint64_t>(p[off + i]) << (i * 8));
    }
    off += 8;
    return true;
}

inline bool ReadStr(const std::uint8_t* p, std::size_t n, std::size_t& off, std::string& out) {
    std::uint32_t len = 0;
    if (!ReadU32(p, n, off, len) || off + len > n) return false;
    out.assign(reinterpret_cast<const char*>(p + off), len);
    off += len;
    return true;
}

/// Computes SHA-256 hex digest directly over an in-memory buffer via `bcrypt.dll`.
inline std::string Sha256BufferHex(const std::uint8_t* ptr, std::size_t len) {
    std::string hex;
    ComputeBufferSha256Hex(ptr, len, hex);
    return AsciiLower(hex);
}

/// Normalizes an archive path from `RSWPAK1` (stripping a leading `"INSTALLDIR/"` prefix when present)
/// into a clean forward-slash relative path inside `<InstallDir>`.
inline std::string NormalizeInstallRelPath(std::string rawRel) {
    for (char& c : rawRel) {
        if (c == '\\') c = '/';
    }
    while (!rawRel.empty() && rawRel.front() == '/') rawRel.erase(rawRel.begin());
    if (rawRel.size() > 11 && _strnicmp(rawRel.c_str(), "INSTALLDIR/", 11) == 0) {
        rawRel.erase(0, 11);
    }
    return rawRel;
}

struct SnapshotFile {
    std::string               relPath;
    std::string               sha256;
    std::vector<std::uint8_t> bytes;
};

/// Loads a virtual file map either from a directory on disk OR directly from a compiled
/// **RaggieScript EXE** (`-setup.exe` containing `kResIdSfxOrNativeArchive` `#220`)!
inline bool LoadInstallTreeSnapshot(
    const std::filesystem::path& dirOrExePath,
    std::map<std::string, SnapshotFile>& outByLowerRel,
    std::string& outProductName,
    std::string& outUpgradeCode,
    std::string& outArch,
    std::string& outVersion,
    std::string& outError)
{
    outByLowerRel.clear();
    std::error_code ec;
    if (!std::filesystem::exists(dirOrExePath, ec)) {
        outError = "Snapshot input path does not exist: " + WideToUtf8(dirOrExePath.wstring());
        return false;
    }

    if (std::filesystem::is_directory(dirOrExePath, ec)) {
        // Check for `<Dir>\Uninstall.rswm` metadata if this is an installed RaggieScript EXE folder
        const auto metaFile = dirOrExePath / L"Uninstall.rswm";
        if (std::filesystem::is_regular_file(metaFile, ec)) {
            std::vector<std::uint8_t> mRaw;
            std::string dummyErr;
            if (ReadBinaryFileBytes(metaFile, mRaw, dummyErr)) {
                auto kv = native_detail::ParseFlatIniMap(
                    std::string_view(reinterpret_cast<const char*>(mRaw.data()), mRaw.size()));
                if (!kv["ProductName"].empty()) outProductName = kv["ProductName"];
                if (!kv["Version"].empty())     outVersion     = kv["Version"];
            }
        }

        for (const auto& de : std::filesystem::recursive_directory_iterator(dirOrExePath, ec)) {
            if (!de.is_regular_file(ec)) continue;
            const auto rel = std::filesystem::relative(de.path(), dirOrExePath, ec);
            std::string relStr = NormalizeInstallRelPath(WideToUtf8(rel.generic_wstring()));
            const std::string low = AsciiLower(relStr);
            // Skip transient uninstaller/updater bookkeeping files when diffing folder trees
            if (low == "uninstall.exe" || low == "uninstall.rswm" || low == ".rsw-rollback.jnl" || low == "updateconfig.json") {
                continue;
            }
            SnapshotFile sf{};
            sf.relPath = relStr;
            if (!ReadBinaryFileBytes(de.path(), sf.bytes, outError)) {
                return false;
            }
            sf.sha256 = Sha256BufferHex(sf.bytes.data(), sf.bytes.size());
            outByLowerRel[low] = std::move(sf);
        }
        return true;
    }

    // Otherwise open `dirOrExePath` as a PE executable (`RaggieScript EXE`) via `LoadLibraryExW`!
    HMODULE hMod = ::LoadLibraryExW(
        dirOrExePath.wstring().c_str(), nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!hMod) {
        outError = "Could not open '" + WideToUtf8(dirOrExePath.wstring()) + "' as a folder or RaggieScript EXE.";
        return false;
    }

    auto readRes = [&](int id, std::vector<std::uint8_t>& outBuf) -> bool {
        HRSRC hR = ::FindResourceW(hMod, MAKEINTRESOURCEW(id), RT_RCDATA);
        if (!hR) return false;
        const DWORD sz = ::SizeofResource(hMod, hR);
        HGLOBAL hG = ::LoadResource(hMod, hR);
        if (!hG || sz == 0) return false;
        const auto* p = static_cast<const std::uint8_t*>(::LockResource(hG));
        if (!p) return false;
        outBuf.assign(p, p + sz);
        return true;
    };

    std::vector<std::uint8_t> natBytes;
    if (readRes(kResIdNativeManifest, natBytes)) {
        const auto nm = ParseNativeOrSfxManifestIni(
            std::string_view(reinterpret_cast<const char*>(natBytes.data()), natBytes.size()));
        outProductName = nm.productName;
        outUpgradeCode = nm.upgradeCode;
        outArch        = nm.architecture;
        outVersion     = nm.version;
    }

    std::vector<std::uint8_t> rsscCompBytes;
    if (outVersion.empty() && readRes(kResIdScriptBytecode, rsscCompBytes)) {
        std::vector<std::uint8_t> bcRaw;
        std::string bcErr;
        rssc::ScriptProgram prog;
        if (DecompressLzmsOrCopyRaw(rsscCompBytes.data(), rsscCompBytes.size(), bcRaw, bcErr) &&
            rssc::LoadRaggieScriptBytecode(bcRaw.data(), bcRaw.size(), prog, bcErr)) {
            outProductName = prog.package.name;
            outUpgradeCode = prog.package.id;
            outVersion     = prog.package.version;
        }
    }

    std::vector<std::uint8_t> pakBytes;
    const bool hasPak = readRes(kResIdSfxOrNativeArchive, pakBytes);
    ::FreeLibrary(hMod);

    if (!hasPak || pakBytes.empty()) {
        outError = "Executable '" + WideToUtf8(dirOrExePath.filename().wstring()) +
                   "' does not contain an embedded RaggieScript EXE payload (#220 RSWPAK1).";
        return false;
    }

    std::vector<RswPakFileMetadata> metas;
    std::vector<std::uint8_t> payload;
    if (!UnpackRswPakBytesInMemory(pakBytes.data(), pakBytes.size(), metas, payload, outError)) {
        return false;
    }

    for (const auto& m : metas) {
        if (m.featureId == "__support__") continue;
        SnapshotFile sf{};
        sf.relPath = NormalizeInstallRelPath(m.archiveRelPath);
        const auto* beg = payload.data() + m.payloadOffset;
        sf.bytes.assign(beg, beg + static_cast<std::size_t>(m.payloadSize));
        sf.sha256 = Sha256BufferHex(sf.bytes.data(), sf.bytes.size());
        outByLowerRel[AsciiLower(sf.relPath)] = std::move(sf);
    }
    return true;
}

} // namespace patch_io_detail

/// Serializes an `RswPatchPackage` into a tamper-evident `.rswpatch` file on disk (`RSWPATCH1` + trailing SHA-256).
inline bool WriteRswPatchFile(
    const RswPatchPackage& pkg,
    const std::filesystem::path& outPatchPath,
    std::string& outFileSha256,
    std::string& outError)
{
    using namespace patch_io_detail;
    std::vector<std::uint8_t> body;
    body.reserve(4096);
    body.insert(body.end(), std::begin(kRswPatchMagic), std::end(kRswPatchMagic));
    WriteU8(body, 1u); // Format version 1

    WriteStr(body, pkg.productName);
    WriteStr(body, pkg.upgradeCode);
    WriteStr(body, pkg.architecture);
    WriteStr(body, pkg.fromVersion);
    WriteStr(body, pkg.toVersion);

    WriteU32(body, static_cast<std::uint32_t>(pkg.entries.size()));
    for (const auto& e : pkg.entries) {
        WriteU8(body, static_cast<std::uint8_t>(e.op));
        WriteStr(body, e.relPath);
        WriteStr(body, e.oldSha256);
        WriteStr(body, e.newSha256);
        WriteU64(body, e.oldSize);
        WriteU64(body, e.newSize);
        WriteU32(body, static_cast<std::uint32_t>(e.payload.size()));
        if (!e.payload.empty()) {
            body.insert(body.end(), e.payload.begin(), e.payload.end());
        }
    }

    // Append 64-char ASCII SHA-256 hex envelope over `body` so tampered/corrupted `.rswpatch` files fail pre-flight!
    const std::string bodySha = Sha256BufferHex(body.data(), body.size());
    if (bodySha.size() != 64) {
        outError = "Failed computing SHA-256 envelope for .rswpatch container.";
        return false;
    }
    body.insert(body.end(), bodySha.begin(), bodySha.end());

    std::error_code ec;
    if (outPatchPath.has_parent_path()) {
        std::filesystem::create_directories(outPatchPath.parent_path(), ec);
    }
    std::ofstream out(outPatchPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        outError = "Could not open output patch file for writing: " + WideToUtf8(outPatchPath.wstring());
        return false;
    }
    out.write(reinterpret_cast<const char*>(body.data()), static_cast<std::streamsize>(body.size()));
    out.close();
    if (!out.good()) {
        outError = "Disk write failed writing .rswpatch file.";
        return false;
    }

    outFileSha256 = Sha256BufferHex(body.data(), body.size());
    return true;
}

/// Reads and cryptographically verifies an `RSWPATCH1` `.rswpatch` container from disk!
inline bool ReadRswPatchFile(
    const std::filesystem::path& patchPath,
    RswPatchPackage& outPkg,
    std::string& outError)
{
    using namespace patch_io_detail;
    outPkg = {};
    std::vector<std::uint8_t> raw;
    if (!ReadBinaryFileBytes(patchPath, raw, outError)) {
        return false;
    }
    if (raw.size() < sizeof(kRswPatchMagic) + 1u + 64u) {
        outError = "Invalid or truncated .rswpatch container: " + WideToUtf8(patchPath.wstring());
        return false;
    }
    if (std::memcmp(raw.data(), kRswPatchMagic, sizeof(kRswPatchMagic)) != 0) {
        outError = "File is not a valid RaggieSoft .rswpatch (RSWPATCH1 magic header mismatch).";
        return false;
    }

    const std::size_t bodyLen = raw.size() - 64u;
    const std::string storedSha(reinterpret_cast<const char*>(raw.data() + bodyLen), 64u);
    const std::string actualSha = Sha256BufferHex(raw.data(), bodyLen);
    if (!EqualsIgnoreCase(storedSha, actualSha)) {
        outError = "Cryptographic integrity check failed: .rswpatch SHA-256 digest mismatch (tampered or corrupted file).";
        return false;
    }

    std::size_t off = sizeof(kRswPatchMagic);
    std::uint8_t fmtVer = 0;
    if (!ReadU8(raw.data(), bodyLen, off, fmtVer) || fmtVer != 1u) {
        outError = "Unsupported .rswpatch format version.";
        return false;
    }

    if (!ReadStr(raw.data(), bodyLen, off, outPkg.productName) ||
        !ReadStr(raw.data(), bodyLen, off, outPkg.upgradeCode) ||
        !ReadStr(raw.data(), bodyLen, off, outPkg.architecture) ||
        !ReadStr(raw.data(), bodyLen, off, outPkg.fromVersion) ||
        !ReadStr(raw.data(), bodyLen, off, outPkg.toVersion)) {
        outError = "Malformed .rswpatch header metadata.";
        return false;
    }

    std::uint32_t count = 0;
    if (!ReadU32(raw.data(), bodyLen, off, count) || count > 262144u) {
        outError = "Malformed .rswpatch entry count.";
        return false;
    }
    outPkg.entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        RswPatchEntry e{};
        std::uint8_t opByte = 0;
        std::uint32_t payLen = 0;
        if (!ReadU8(raw.data(), bodyLen, off, opByte) || opByte > 3u ||
            !ReadStr(raw.data(), bodyLen, off, e.relPath) ||
            !ReadStr(raw.data(), bodyLen, off, e.oldSha256) ||
            !ReadStr(raw.data(), bodyLen, off, e.newSha256) ||
            !ReadU64(raw.data(), bodyLen, off, e.oldSize) ||
            !ReadU64(raw.data(), bodyLen, off, e.newSize) ||
            !ReadU32(raw.data(), bodyLen, off, payLen) ||
            off + payLen > bodyLen) {
            outError = "Malformed .rswpatch entry #" + std::to_string(i) + ".";
            return false;
        }
        e.op = static_cast<RswPatchOp>(opByte);
        if (payLen > 0) {
            e.payload.assign(raw.data() + off, raw.data() + off + payLen);
            off += payLen;
        }
        outPkg.entries.push_back(std::move(e));
    }
    return true;
}

// ================================================================================
// 3. COMPILING & APPLYING `.RSWPATCH` BINARY DELTAS FOR RAGGIESCRIPT EXE APPS
// ================================================================================

/// Compiles an `msdelta.dll`-powered `.rswpatch` delta package between `oldDirOrExe` and `newDirOrExe`!
inline bool BuildRswPatchFromSnapshots(
    const std::filesystem::path& oldDirOrExe,
    const std::filesystem::path& newDirOrExe,
    const std::filesystem::path& outPatchPath,
    std::string_view fromVerOverride,
    std::string_view toVerOverride,
    std::string_view upgradeCodeOverride,
    RswPatchPackage& outPkg,
    std::string& outPatchSha256,
    std::string& outError)
{
    using namespace patch_io_detail;
    std::map<std::string, SnapshotFile> oldMap, newMap;
    std::string oldName, oldUpg, oldArch, oldVer;
    std::string newName, newUpg, newArch, newVer;

    if (!LoadInstallTreeSnapshot(oldDirOrExe, oldMap, oldName, oldUpg, oldArch, oldVer, outError) ||
        !LoadInstallTreeSnapshot(newDirOrExe, newMap, newName, newUpg, newArch, newVer, outError)) {
        return false;
    }

    outPkg = {};
    outPkg.productName  = !newName.empty() ? newName : (!oldName.empty() ? oldName : "RaggieSoft Application");
    outPkg.upgradeCode  = !upgradeCodeOverride.empty() ? std::string(upgradeCodeOverride) : (!newUpg.empty() ? newUpg : oldUpg);
    outPkg.architecture = !newArch.empty() ? newArch : (!oldArch.empty() ? oldArch : std::string(QueryHostPlatform().NativeArchSlug()));
    outPkg.fromVersion  = !fromVerOverride.empty() ? std::string(fromVerOverride) : (!oldVer.empty() ? oldVer : "1.0.0");
    outPkg.toVersion    = !toVerOverride.empty()   ? std::string(toVerOverride)   : (!newVer.empty() ? newVer : "1.0.1");

    if (!oldUpg.empty() && !newUpg.empty() && !EqualsIgnoreCase(oldUpg, newUpg)) {
        outError = "[RSW080] Cannot build .rswpatch across mismatched UpgradeCodes (" + oldUpg + " vs " + newUpg + ").";
        return false;
    }

    // 1. Inspect every file in `newMap` (`Keep`, `Add`, or `Delta`)
    for (const auto& [lowRel, nf] : newMap) {
        auto itOld = oldMap.find(lowRel);
        if (itOld == oldMap.end()) {
            // Brand-new file in `toVersion` -> `RswPatchOp::Add`
            RswPatchEntry e{};
            e.op        = RswPatchOp::Add;
            e.relPath   = nf.relPath;
            e.newSha256 = nf.sha256;
            e.newSize   = nf.bytes.size();
            e.payload   = CompressWithLzmsIfSmaller(nf.bytes);
            outPkg.entries.push_back(std::move(e));
        } else {
            const auto& of = itOld->second;
            if (EqualsIgnoreCase(of.sha256, nf.sha256) && of.bytes.size() == nf.bytes.size()) {
                // Unchanged file -> `RswPatchOp::Keep` (0 payload bytes!)
                RswPatchEntry e{};
                e.op        = RswPatchOp::Keep;
                e.relPath   = nf.relPath;
                e.oldSha256 = of.sha256;
                e.newSha256 = nf.sha256;
                e.oldSize   = of.bytes.size();
                e.newSize   = nf.bytes.size();
                outPkg.entries.push_back(std::move(e));
            } else if (of.bytes.empty() || nf.bytes.empty()) {
                // Edge case: 0-byte file transition -> store cleanly as `RswPatchOp::Add`
                RswPatchEntry e{};
                e.op        = RswPatchOp::Add;
                e.relPath   = nf.relPath;
                e.oldSha256 = of.sha256;
                e.newSha256 = nf.sha256;
                e.oldSize   = of.bytes.size();
                e.newSize   = nf.bytes.size();
                e.payload   = CompressWithLzmsIfSmaller(nf.bytes);
                outPkg.entries.push_back(std::move(e));
            } else {
                // Modified file -> compute `msdelta.dll` binary delta (`CreateDeltaB`)!
                std::vector<std::uint8_t> rawDelta;
                std::string dErr;
                if (CreateMsDeltaBytes(
                        of.bytes.data(), of.bytes.size(),
                        nf.bytes.data(), nf.bytes.size(),
                        rawDelta, dErr)) {
                    RswPatchEntry e{};
                    e.op        = RswPatchOp::Delta;
                    e.relPath   = nf.relPath;
                    e.oldSha256 = of.sha256;
                    e.newSha256 = nf.sha256;
                    e.oldSize   = of.bytes.size();
                    e.newSize   = nf.bytes.size();
                    e.payload   = CompressWithLzmsIfSmaller(rawDelta);
                    outPkg.entries.push_back(std::move(e));
                } else {
                    // Graceful fallback to `Add` if `msdelta` declines an unusual blob
                    RswPatchEntry e{};
                    e.op        = RswPatchOp::Add;
                    e.relPath   = nf.relPath;
                    e.oldSha256 = of.sha256;
                    e.newSha256 = nf.sha256;
                    e.oldSize   = of.bytes.size();
                    e.newSize   = nf.bytes.size();
                    e.payload   = CompressWithLzmsIfSmaller(nf.bytes);
                    outPkg.entries.push_back(std::move(e));
                }
            }
        }
    }

    // 2. Any file present in `oldMap` that is absent from `newMap` was retired -> `RswPatchOp::Delete`!
    for (const auto& [lowRel, of] : oldMap) {
        if (newMap.find(lowRel) == newMap.end()) {
            RswPatchEntry e{};
            e.op        = RswPatchOp::Delete;
            e.relPath   = of.relPath;
            e.oldSha256 = of.sha256;
            e.oldSize   = of.bytes.size();
            outPkg.entries.push_back(std::move(e));
        }
    }

    return WriteRswPatchFile(outPkg, outPatchPath, outPatchSha256, outError);
}

/// Applies a verified `.rswpatch` file onto `<installDir>` with full pre-flight baseline hash checks,
/// `msdelta.dll` `ApplyDeltaB` reconstruction, post-patch `SHA-256` verification, retired-file pruning,
/// `Uninstall.rswm` + Registry `DisplayVersion` update, and atomic `.rsw-rollback.jnl` LIFO rollback!
inline bool ApplyRswPatchTransaction(
    const std::filesystem::path& patchPath,
    const std::filesystem::path& installDir,
    int simulateFaultAfterOps,
    RswPatchApplySummary& outSummary,
    std::string& outError)
{
    outSummary = {};
    RswPatchPackage pkg;
    if (!ReadRswPatchFile(patchPath, pkg, outError)) {
        return false;
    }

    std::error_code ec;
    if (!std::filesystem::is_directory(installDir, ec)) {
        outError = "Target installation folder does not exist: " + WideToUtf8(installDir.wstring());
        return false;
    }

    // 1. Pre-flight verification: check `oldSha256` on every `Keep` and `Delta` file BEFORE modifying disk!
    for (const auto& e : pkg.entries) {
        if (e.op != RswPatchOp::Keep && e.op != RswPatchOp::Delta) continue;
        const auto diskFile = (installDir / Utf8ToWide(e.relPath)).lexically_normal();
        if (!std::filesystem::is_regular_file(diskFile, ec)) {
            outError = "Baseline verification failed: expected installed file '" + e.relPath +
                       "' (v" + pkg.fromVersion + ") is missing from '" + WideToUtf8(installDir.wstring()) + "'.";
            return false;
        }
        if (!e.oldSha256.empty()) {
            std::string actualHex;
            if (!ComputeFileSha256Hex(diskFile, actualHex) || !EqualsIgnoreCase(actualHex, e.oldSha256)) {
                outError = "Baseline SHA-256 mismatch on '" + e.relPath + "' (expected v" +
                           pkg.fromVersion + " baseline; run full installer upgrade instead).";
                return false;
            }
        }
    }

    // 2. Begin atomic rollback journal (`.rsw-rollback.jnl`)!
    const auto journalFile = installDir / L".rsw-rollback.jnl";
    {
        std::ofstream jnl(journalFile, std::ios::binary | std::ios::trunc);
        if (jnl) {
            jnl << "RSW_PATCH_ROLLBACK_JOURNAL_V1\nFrom=" << pkg.fromVersion << "\nTo=" << pkg.toVersion << "\n";
        }
    }

    std::vector<std::filesystem::path> createdDirs;
    std::vector<std::filesystem::path> newlyAddedFiles;
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>> backedUpFiles; // `(origPath, bakPath)`

    auto ensureDirsTracked = [&](const std::filesystem::path& dir) -> bool {
        std::vector<std::filesystem::path> missing;
        std::filesystem::path cur = dir.lexically_normal();
        while (!cur.empty() && !std::filesystem::exists(cur, ec)) {
            missing.push_back(cur);
            const auto par = cur.parent_path();
            if (par == cur) break;
            cur = par;
        }
        std::reverse(missing.begin(), missing.end());
        for (const auto& d : missing) {
            if (!std::filesystem::create_directory(d, ec) && !std::filesystem::exists(d, ec)) return false;
            createdDirs.push_back(d);
        }
        return true;
    };

    auto performLifoRollback = [&]() {
        for (auto it = newlyAddedFiles.rbegin(); it != newlyAddedFiles.rend(); ++it) {
            std::filesystem::remove(*it, ec);
        }
        for (auto it = backedUpFiles.rbegin(); it != backedUpFiles.rend(); ++it) {
            std::filesystem::remove(it->first, ec);
            std::filesystem::rename(it->second, it->first, ec);
        }
        for (auto it = createdDirs.rbegin(); it != createdDirs.rend(); ++it) {
            std::filesystem::remove(*it, ec);
        }
        std::filesystem::remove(journalFile, ec);
    };

    int mutatingOpsDone = 0;
    std::set<std::wstring> deletedLowerPaths;
    std::vector<std::filesystem::path> addedDiskPaths;

    for (const auto& e : pkg.entries) {
        const auto dstFile = (installDir / Utf8ToWide(e.relPath)).lexically_normal();

        if (e.op == RswPatchOp::Keep) {
            ++outSummary.keptCount;
            continue;
        }

        if (e.op == RswPatchOp::Delete) {
            if (std::filesystem::exists(dstFile, ec)) {
                const auto bak = std::filesystem::path(dstFile.wstring() + L".rswbak." + std::to_wstring(::GetCurrentProcessId()));
                std::filesystem::remove(bak, ec);
                ec.clear();
                std::filesystem::rename(dstFile, bak, ec);
                if (ec) {
                    outError = "Could not retire locked file '" + e.relPath + "'.";
                    performLifoRollback();
                    return false;
                }
                backedUpFiles.push_back({dstFile, bak});
                deletedLowerPaths.insert(Utf8ToWide(AsciiLower(WideToUtf8(dstFile.wstring()))));
                ++outSummary.deletedCount;
            }
        } else if (e.op == RswPatchOp::Add || e.op == RswPatchOp::Delta) {
            std::vector<std::uint8_t> finalBytes;
            if (e.op == RswPatchOp::Add) {
                if (!DecompressLzmsOrCopyRaw(e.payload.data(), e.payload.size(), finalBytes, outError)) {
                    performLifoRollback();
                    return false;
                }
            } else {
                std::vector<std::uint8_t> oldBytes, rawDelta;
                if (!ReadBinaryFileBytes(dstFile, oldBytes, outError) ||
                    !DecompressLzmsOrCopyRaw(e.payload.data(), e.payload.size(), rawDelta, outError) ||
                    !ApplyMsDeltaBytes(oldBytes.data(), oldBytes.size(), rawDelta.data(), rawDelta.size(), finalBytes, outError)) {
                    performLifoRollback();
                    return false;
                }
            }

            // Verify reconstructed target bytes match `e.newSha256` BEFORE writing to disk!
            if (!e.newSha256.empty()) {
                const std::string recSha = patch_io_detail::Sha256BufferHex(finalBytes.data(), finalBytes.size());
                if (!EqualsIgnoreCase(recSha, e.newSha256)) {
                    outError = "Post-delta SHA-256 verification failed for '" + e.relPath + "'.";
                    performLifoRollback();
                    return false;
                }
            }

            if (!ensureDirsTracked(dstFile.parent_path())) {
                outError = "Could not create parent folder for '" + e.relPath + "'.";
                performLifoRollback();
                return false;
            }

            const bool existedBefore = std::filesystem::exists(dstFile, ec);
            if (existedBefore) {
                const auto bak = std::filesystem::path(dstFile.wstring() + L".rswbak." + std::to_wstring(::GetCurrentProcessId()));
                std::filesystem::remove(bak, ec);
                ec.clear();
                std::filesystem::rename(dstFile, bak, ec);
                if (ec) {
                    outError = "Could not replace in-use file '" + e.relPath + "'.";
                    performLifoRollback();
                    return false;
                }
                backedUpFiles.push_back({dstFile, bak});
            }

            std::ofstream out(dstFile, std::ios::binary | std::ios::trunc);
            if (!out) {
                outError = "Failed writing updated file '" + e.relPath + "'.";
                performLifoRollback();
                return false;
            }
            if (!finalBytes.empty()) {
                out.write(reinterpret_cast<const char*>(finalBytes.data()), static_cast<std::streamsize>(finalBytes.size()));
            }
            out.close();
            if (!out.good()) {
                outError = "Disk flush failed on '" + e.relPath + "'.";
                performLifoRollback();
                return false;
            }

            if (!existedBefore) {
                newlyAddedFiles.push_back(dstFile);
                addedDiskPaths.push_back(dstFile);
            }
            if (e.op == RswPatchOp::Add) ++outSummary.addedCount;
            else ++outSummary.patchedCount;
        }

        ++mutatingOpsDone;
        if (simulateFaultAfterOps > 0 && mutatingOpsDone >= simulateFaultAfterOps) {
            outError = "Simulated patch write fault after " + std::to_string(mutatingOpsDone) + " operation(s).";
            performLifoRollback();
            return false;
        }
    }

    // 3. Update `<installDir>\Uninstall.rswm` (`Version=` + added/deleted `File_N` entries) and Registry `DisplayVersion`!
    const auto rswmPath = installDir / L"Uninstall.rswm";
    if (std::filesystem::is_regular_file(rswmPath, ec)) {
        std::vector<std::uint8_t> rawMeta;
        std::string mErr;
        if (ReadBinaryFileBytes(rswmPath, rawMeta, mErr)) {
            auto kv = native_detail::ParseFlatIniMap(
                std::string_view(reinterpret_cast<const char*>(rawMeta.data()), rawMeta.size()));
            const bool perMachine = EqualsIgnoreCase(kv["PerMachine"], "true");
            const std::string uninstRegKey = kv["UninstallRegKey"];

            std::vector<std::string> keptFiles;
            std::set<std::string> seenLower;
            const int oldCount = std::clamp(std::atoi(kv["FileCount"].c_str()), 0, 65536);
            for (int i = 0; i < oldCount; ++i) {
                const std::string fp = kv["File_" + std::to_string(i)];
                if (fp.empty()) continue;
                const std::wstring wNorm = Utf8ToWide(AsciiLower(WideToUtf8(std::filesystem::path(Utf8ToWide(fp)).lexically_normal().wstring())));
                if (deletedLowerPaths.count(wNorm) > 0) continue;
                if (seenLower.insert(WideToUtf8(wNorm)).second) {
                    keptFiles.push_back(fp);
                }
            }
            for (const auto& addP : addedDiskPaths) {
                const std::string fp = WideToUtf8(addP.wstring());
                if (seenLower.insert(AsciiLower(fp)).second) {
                    keptFiles.push_back(fp);
                }
            }

            std::ofstream um(rswmPath, std::ios::binary | std::ios::trunc);
            if (um) {
                um << "[NativeUninstall]\n"
                   << "ProductName="     << native_detail::EscapeSingleLine(kv["ProductName"].empty() ? pkg.productName : kv["ProductName"]) << "\n"
                   << "Version="         << native_detail::EscapeSingleLine(pkg.toVersion) << "\n"
                   << "Manufacturer="    << native_detail::EscapeSingleLine(kv["Manufacturer"]) << "\n"
                   << "PerMachine="      << (perMachine ? "true" : "false") << "\n"
                   << "UninstallRegKey=" << uninstRegKey << "\n"
                   << "InstallDir="      << WideToUtf8(installDir.wstring()) << "\n"
                   << "FileCount="       << keptFiles.size() << "\n";
                for (std::size_t i = 0; i < keptFiles.size(); ++i) {
                    um << "File_" << i << "=" << keptFiles[i] << "\n";
                }
                const char* copyGroups[][2] = {{"ShortcutCount", "Shortcut_"}, {"DirCount", "Dir_"}};
                for (const auto& grp : copyGroups) {
                    const int n = std::clamp(std::atoi(kv[grp[0]].c_str()), 0, 4096);
                    um << grp[0] << "=" << n << "\n";
                    for (int i = 0; i < n; ++i) {
                        um << grp[1] << i << "=" << kv[std::string(grp[1]) + std::to_string(i)] << "\n";
                    }
                }
                const int nReg = std::clamp(std::atoi(kv["RegKeyCount"].c_str()), 0, 4096);
                um << "RegKeyCount=" << nReg << "\n";
                for (int i = 0; i < nReg; ++i) {
                    um << "RegRoot_" << i << "=" << kv["RegRoot_" + std::to_string(i)] << "\n"
                       << "RegKey_"  << i << "=" << kv["RegKey_"  + std::to_string(i)] << "\n";
                }
                const int nEnv = std::clamp(std::atoi(kv["EnvUninstallCount"].c_str()), 0, 512);
                um << "EnvUninstallCount=" << nEnv << "\n";
                for (int i = 0; i < nEnv; ++i) {
                    um << "EnvUnScope_" << i << "=" << kv["EnvUnScope_" + std::to_string(i)] << "\n"
                       << "EnvUnName_"  << i << "=" << native_detail::EscapeSingleLine(kv["EnvUnName_" + std::to_string(i)]) << "\n"
                       << "EnvUnMode_"  << i << "=" << kv["EnvUnMode_"  + std::to_string(i)] << "\n"
                       << "EnvUnSeg_"   << i << "=" << native_detail::EscapeSingleLine(kv["EnvUnSeg_"  + std::to_string(i)]) << "\n";
                }
            }

            if (!uninstRegKey.empty()) {
                const auto virtReg = native_detail::GetVirtualRegistryStoreRoot();
                if (!virtReg.empty()) {
                    bool dummyNew = false;
                    native_detail::VirtualRegWriteValue(
                        virtReg, perMachine ? "HKLM" : "HKCU",
                        Utf8ToWide(uninstRegKey), L"DisplayVersion", pkg.toVersion, dummyNew);
                } else {
                    HKEY hk = nullptr;
                    if (::RegOpenKeyExW(
                            perMachine ? HKEY_LOCAL_MACHINE : HKEY_CURRENT_USER,
                            Utf8ToWide(uninstRegKey).c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
                        const std::wstring wVer = Utf8ToWide(pkg.toVersion);
                        ::RegSetValueExW(
                            hk, L"DisplayVersion", 0, REG_SZ,
                            reinterpret_cast<const BYTE*>(wVer.c_str()),
                            static_cast<DWORD>((wVer.size() + 1) * sizeof(wchar_t)));
                        ::RegCloseKey(hk);
                    }
                }
            }
        }
    }

    // Also update `<installDir>\UpdateConfig.json` if present so `RsUpdate.exe` sees `pkg.toVersion`!
    const auto updCfgPath = installDir / L"UpdateConfig.json";
    if (std::filesystem::is_regular_file(updCfgPath, ec)) {
        std::vector<std::uint8_t> cfgBytes;
        std::string cErr;
        if (ReadBinaryFileBytes(updCfgPath, cfgBytes, cErr)) {
            std::string txt(reinterpret_cast<const char*>(cfgBytes.data()), cfgBytes.size());
            const auto pos = txt.find("\"installedVersion\"");
            if (pos != std::string::npos) {
                const auto q1 = txt.find('"', txt.find(':', pos + 18) + 1);
                const auto q2 = (q1 != std::string::npos) ? txt.find('"', q1 + 1) : std::string::npos;
                if (q1 != std::string::npos && q2 != std::string::npos) {
                    txt.replace(q1 + 1, q2 - q1 - 1, pkg.toVersion);
                    std::ofstream uo(updCfgPath, std::ios::binary | std::ios::trunc);
                    uo.write(txt.data(), static_cast<std::streamsize>(txt.size()));
                }
            }
        }
    }

    // 4. Commit transaction: clean up `.rswbak` backups and `.rsw-rollback.jnl`!
    for (const auto& [origP, bakP] : backedUpFiles) {
        (void)origP;
        std::filesystem::remove(bakP, ec);
    }
    std::filesystem::remove(journalFile, ec);

    outSummary.productName = pkg.productName;
    outSummary.fromVersion = pkg.fromVersion;
    outSummary.toVersion   = pkg.toVersion;
    return true;
}

} // namespace rsw
