// ================================================================================
// FILE: src/common/RswLicensing.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com/installer-studio/editions
// DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   How does RaggieSoft Installer Studio know which Edition to present?
//     1. **No License File == Community Edition (`RswIdeEdition::Community`)**:
//        If no `.rswlic` license file is present on the workstation, both
//        `InstallerStudio.exe` and `instbuild.exe` automatically present the free
//        **Community Edition** (which permanently maintains 100% technical parity
//        with Architect Edition for hobbyists, solo creators, students, educators,
//        non-profits, and open-source maintainers, with zero watermarks).
//
//     2. **Offline Asymmetric ECDSA P-256 `.rswlic` Verification (`bcrypt.dll`)**:
//        Commercial workstations and CI/CD build agents (`Essentials`, `Developer`,
//        `Architect`, and All-Access `RaggieSoft Studio` Suite licenses) unlock
//        their licensed tier via a human-readable, copy-pasteable `.rswlic` file:
//
//          -----BEGIN RAGGIESOFT LICENSE-----
//          Format          = RSWLIC1
//          LicenseId       = RSW-2026-DEV-0001
//          Product         = installer-studio
//          Edition         = developer
//          Licensee        = Example Engineering Team
//          Email           = builds@raggiesoftstudio.com
//          Seats           = 5
//          IssuedDate      = 2026-11-27
//          MaintenanceThru = 2027-11-27
//          Signature       = <Base64 64-byte NIST P-256 ECDSA (r || s) signature>
//          -----END RAGGIESOFT LICENSE-----
//
//     3. **Why Asymmetric ECDSA P-256?**:
//        Because RaggieSoft Installer Studio is open-source MIT C++20 software,
//        ONLY our 64-byte public key (`kRaggieSoftLicensePubKeyP256`) ships in
//        source code. Windows `C:\Windows\System32\bcrypt.dll` (`BCryptVerifySignature`)
//        verifies the signature in `< 0.2ms` 100% offline—with ZERO activation
//        servers, ZERO network phone-home telemetry, ZERO hardware/motherboard
//        fingerprinting, and perpetual fallback!
//
//     4. **Ordered `.rswlic` Discovery Paths**:
//        a) Explicit CLI `--license-file=<path.rswlic>`
//        b) `RSW_LICENSE_KEY` environment variable (file path OR inline `.rswlic`
//           text / Base64 secret for GitHub Actions & Azure DevOps runners)
//        c) Portable alongside executable: `<ExeDir>\installer-studio.rswlic`
//           or `<ExeDir>\raggiesoft-studio.rswlic`
//        d) Per-User license folder: `%LOCALAPPDATA%\RaggieSoft\Licenses\*.rswlic`
//        e) Per-Machine IT folder: `%PROGRAMDATA%\RaggieSoft\Licenses\*.rswlic`
//        f) None found -> **Community Edition**!
// ================================================================================

#pragma once

#include "RswPlatform.hpp"
#include "RswVersion.hpp"

#include <bcrypt.h>
#include <shlobj.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "bcrypt.lib")

namespace rsw {

/// Official RaggieSoft Studio NIST P-256 (`BCRYPT_ECDSA_P256_ALGORITHM`) 64-byte Public Key `(Qx[32] || Qy[32])`.
/// Private signing key resides exclusively in offline publisher storage (`ai-staging/licensing/`).
inline constexpr std::array<std::uint8_t, 64> kRaggieSoftLicensePubKeyP256 = {
    0x05, 0xA4, 0x76, 0xF9, 0x42, 0xEA, 0x59, 0x83, 0x80, 0xF6, 0x78, 0xF4,
    0x2F, 0x04, 0xBF, 0xA7, 0xBB, 0x7C, 0xD2, 0xE7, 0xFC, 0xCE, 0xC2, 0x44,
    0x5E, 0xFB, 0xF7, 0x25, 0x57, 0x65, 0x2F, 0x9C, 0x16, 0xFC, 0x57, 0xA5,
    0x3A, 0xCB, 0xE9, 0xB0, 0x4D, 0x63, 0x61, 0xF2, 0x83, 0x6D, 0x6D, 0xE3,
    0xA0, 0x3C, 0xFE, 0x89, 0x77, 0xF1, 0x2A, 0xD5, 0x19, 0xC6, 0xB8, 0x4A,
    0xFC, 0x51, 0xEE, 0xDF
};

/// Parsed and cryptographically verified `.rswlic` certificate attributes.
struct RswLicenseCertificate {
    std::string   licenseId;
    std::string   product;         // `"installer-studio"` or `"raggiesoft-studio"` (8-Tool Suite)
    RswIdeEdition edition          = RswIdeEdition::Community;
    std::string   licensee;
    std::string   email;
    int           seats            = 1;
    std::string   issuedDate;
    std::string   maintenanceThru;
};

/// Active runtime license resolution state for `InstallerStudio.exe`, `instbuild.exe`, and `RsPatch.exe`.
struct RswLicenseStatus {
    bool                  isCommercialLicensed = false; // `false` when no `.rswlic` file is present (`Community Edition`)
    RswIdeEdition         edition              = RswIdeEdition::Community;
    RswLicenseCertificate cert{};
    std::filesystem::path sourceFilePath;
    std::string           sourceDescription    = "No .rswlic license file found — running Community Edition";
};

namespace license_detail {

/// Trims ASCII spaces, tabs, CR (`\r`), and LF (`\n`) so CRLF and LF `.rswlic` files parse identically!
inline std::string TrimLicWs(std::string_view sv) {
    std::size_t b = 0;
    std::size_t e = sv.size();
    while (b < e && std::isspace(static_cast<unsigned char>(sv[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(sv[e - 1]))) --e;
    return std::string(sv.substr(b, e - b));
}

/// Decodes an RFC 4648 Base64 string (ignoring embedded whitespace) into raw bytes.
inline bool Base64DecodeBytes(std::string_view b64, std::vector<std::uint8_t>& out) {
    out.clear();
    auto valOf = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+' || c == '-') return 62;
        if (c == '/' || c == '_') return 63;
        return -1;
    };

    std::uint32_t buf = 0;
    int bits = 0;
    for (unsigned char c : b64) {
        if (std::isspace(c)) continue;
        if (c == '=') break;
        const int v = valOf(c);
        if (v < 0) return false;
        buf = (buf << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buf >> bits) & 0xFFu));
        }
    }
    return !out.empty();
}

/// Computes a 32-byte SHA-256 digest over `text` using Windows CNG (`bcrypt.dll`).
inline bool ComputeSha256Raw(std::string_view text, std::array<std::uint8_t, 32>& outDigest) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hH  = nullptr;
    bool ok = false;
    if (::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) == 0) {
        DWORD objLen = 0, cb = 0;
        ::BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cb, 0);
        std::vector<UCHAR> obj(objLen);
        if (::BCryptCreateHash(hAlg, &hH, obj.data(), objLen, nullptr, 0, 0) == 0) {
            static const UCHAR kZero = 0;
            PUCHAR ptr = text.empty() ? const_cast<PUCHAR>(&kZero) : reinterpret_cast<PUCHAR>(const_cast<char*>(text.data()));
            if (::BCryptHashData(hH, ptr, static_cast<ULONG>(text.size()), 0) == 0 &&
                ::BCryptFinishHash(hH, outDigest.data(), static_cast<ULONG>(outDigest.size()), 0) == 0) {
                ok = true;
            }
            ::BCryptDestroyHash(hH);
        }
        ::BCryptCloseAlgorithmProvider(hAlg, 0);
    }
    return ok;
}

/// Verifies a 64-byte IEEE P1363 `(r || s)` ECDSA P-256 signature against a 64-byte `(Qx || Qy)` public key!
inline bool VerifyEcdsaP256Signature(
    std::string_view canonicalPayload,
    const std::array<std::uint8_t, 64>& pubKeyQxQy,
    const std::uint8_t* sigBytes,
    std::size_t sigLen)
{
    if (sigLen != 64) return false;
    std::array<std::uint8_t, 32> digest{};
    if (!ComputeSha256Raw(canonicalPayload, digest)) return false;

    // Construct the 72-byte `BCRYPT_ECCPUBLIC_BLOB` (`BCRYPT_ECDSA_PUBLIC_P256_MAGIC` + `cbKey = 32` + `Qx` + `Qy`).
    std::array<UCHAR, sizeof(BCRYPT_ECCKEY_BLOB) + 64> blob{};
    auto* hdr    = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
    hdr->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    hdr->cbKey   = 32;
    std::memcpy(blob.data() + sizeof(BCRYPT_ECCKEY_BLOB), pubKeyQxQy.data(), 64);

    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;
    if (::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) != 0) {
        return false;
    }
    bool verified = false;
    if (::BCryptImportKeyPair(
            hAlg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &hKey,
            blob.data(), static_cast<ULONG>(blob.size()), 0) == 0) {
        verified = (::BCryptVerifySignature(
                        hKey, nullptr,
                        digest.data(), static_cast<ULONG>(digest.size()),
                        const_cast<PUCHAR>(sigBytes), static_cast<ULONG>(sigLen), 0) == 0);
        ::BCryptDestroyKey(hKey);
    }
    ::BCryptCloseAlgorithmProvider(hAlg, 0);
    return verified;
}

/// Resolves a Windows Known Folder (`FOLDERID_LocalAppData` or `FOLDERID_ProgramData`).
inline std::filesystem::path GetKnownFolder(REFKNOWNFOLDERID rfid, const wchar_t* envFallback) {
    PWSTR raw = nullptr;
    std::filesystem::path out;
    if (SUCCEEDED(::SHGetKnownFolderPath(rfid, KF_FLAG_DEFAULT, nullptr, &raw)) && raw) {
        out = raw;
        ::CoTaskMemFree(raw);
        return out;
    }
    wchar_t buf[MAX_PATH * 2]{};
    if (envFallback && ::GetEnvironmentVariableW(envFallback, buf, static_cast<DWORD>(std::size(buf))) > 0) {
        return std::filesystem::path(buf);
    }
    return {};
}

inline bool ParseEditionSlugStrict(std::string_view slug, RswIdeEdition& outEd) noexcept {
    const std::string low = AsciiLower(TrimLicWs(slug));
    if (low == "community")  { outEd = RswIdeEdition::Community;  return true; }
    if (low == "essentials") { outEd = RswIdeEdition::Essentials; return true; }
    if (low == "developer")  { outEd = RswIdeEdition::Developer;  return true; }
    if (low == "architect")  { outEd = RswIdeEdition::Architect;  return true; }
    return false;
}

} // namespace license_detail

/// Returns the user-writable license storage folder: `%LOCALAPPDATA%\RaggieSoft\Licenses`.
inline std::filesystem::path GetUserLicenseStoreDir() {
    auto base = license_detail::GetKnownFolder(FOLDERID_LocalAppData, L"LOCALAPPDATA");
    if (base.empty()) {
        std::error_code ec;
        base = std::filesystem::temp_directory_path(ec);
    }
    return base / L"RaggieSoft" / L"Licenses";
}

/// Returns the machine-wide IT license storage folder: `%PROGRAMDATA%\RaggieSoft\Licenses`.
inline std::filesystem::path GetMachineLicenseStoreDir() {
    auto base = license_detail::GetKnownFolder(FOLDERID_ProgramData, L"PROGRAMDATA");
    if (base.empty()) return {};
    return base / L"RaggieSoft" / L"Licenses";
}

/// Parses and cryptographically verifies `.rswlic` license text against `pubKey64`.
/// Every non-signature `Key = Value` attribute inside the license block participates in the
/// deterministic `RSWLIC1_CANONICAL_V1` SHA-256 + ECDSA P-256 verification!
inline bool VerifyRswLicenseTextWithPublicKey(
    std::string_view rawInput,
    std::string_view expectedProductSlug,
    const std::array<std::uint8_t, 64>& pubKey64,
    RswLicenseCertificate& outCert,
    std::string& outError)
{
    outCert = {};
    std::string text = license_detail::TrimLicWs(rawInput);
    // Strip UTF-8 BOM if present
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
        text = license_detail::TrimLicWs(text);
    }

    // Allow CI/CD secrets (`RSW_LICENSE_KEY`) to supply single-line Base64-encoded `.rswlic` text!
    if (text.find('=') == std::string::npos || text.find('\n') == std::string::npos) {
        std::vector<std::uint8_t> decoded;
        if (license_detail::Base64DecodeBytes(text, decoded)) {
            std::string candidate(reinterpret_cast<const char*>(decoded.data()), decoded.size());
            if (candidate.find("Format") != std::string::npos && candidate.find("Signature") != std::string::npos) {
                text = std::move(candidate);
            }
        }
    }

    std::istringstream iss(text);
    std::string line;
    bool insideBlock = (text.find("-----BEGIN RAGGIESOFT LICENSE-----") == std::string::npos);
    std::map<std::string, std::string> fields;
    std::string sigB64;

    while (std::getline(iss, line)) {
        const std::string t = license_detail::TrimLicWs(line);
        if (t.empty() || t[0] == ';' || t[0] == '#') continue;
        if (t == "-----BEGIN RAGGIESOFT LICENSE-----") { insideBlock = true; continue; }
        if (t == "-----END RAGGIESOFT LICENSE-----")   { break; }
        if (!insideBlock) continue;

        const auto eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = AsciiLower(license_detail::TrimLicWs(std::string_view(t).substr(0, eq)));
        const std::string v = license_detail::TrimLicWs(std::string_view(t).substr(eq + 1));
        if (k.empty()) continue;
        if (k == "signature") sigB64 = v;
        else fields[k] = v;
    }

    if (fields["format"] != "RSWLIC1") {
        outError = "[RSW098] License file is missing 'Format = RSWLIC1' header.";
        return false;
    }
    if (fields["licenseid"].empty() || fields["licensee"].empty() || fields["issueddate"].empty() || sigB64.empty()) {
        outError = "[RSW098] License file is missing required LicenseId, Licensee, IssuedDate, or Signature fields.";
        return false;
    }

    const std::string prodLow = AsciiLower(fields["product"]);
    if (prodLow != AsciiLower(expectedProductSlug) && prodLow != "raggiesoft-studio") {
        outError = "[RSW098] License Product '" + fields["product"] + "' does not cover '" + std::string(expectedProductSlug) + "'.";
        return false;
    }

    RswIdeEdition parsedEd = RswIdeEdition::Community;
    if (!license_detail::ParseEditionSlugStrict(fields["edition"], parsedEd)) {
        outError = "[RSW098] License specifies unknown Edition '" + fields["edition"] + "'.";
        return false;
    }

    std::vector<std::uint8_t> sigBytes;
    if (!license_detail::Base64DecodeBytes(sigB64, sigBytes) || sigBytes.size() != 64) {
        outError = "[RSW098] License Signature is not a valid 64-byte ECDSA P-256 signature.";
        return false;
    }

    std::ostringstream canonical;
    canonical << "RSWLIC1_CANONICAL_V1\n";
    for (const auto& [k, v] : fields) {
        canonical << k << "=" << v << "\n";
    }

    if (!license_detail::VerifyEcdsaP256Signature(canonical.str(), pubKey64, sigBytes.data(), sigBytes.size())) {
        outError = "[RSW098] Cryptographic ECDSA P-256 signature verification failed (tampered or unauthenticated .rswlic file).";
        return false;
    }

    outCert.licenseId       = fields["licenseid"];
    outCert.product         = prodLow;
    outCert.edition         = parsedEd;
    outCert.licensee        = fields["licensee"];
    outCert.email           = fields["email"];
    outCert.seats           = std::max(1, std::atoi(fields["seats"].c_str()));
    outCert.issuedDate      = fields["issueddate"];
    outCert.maintenanceThru = fields["maintenancethru"];
    return true;
}

/// Verifies `.rswlic` text strictly against the embedded Production RaggieSoft ECDSA P-256 Public Key!
inline bool VerifyProductionRswLicenseFile(
    const std::filesystem::path& licPath,
    std::string_view expectedProductSlug,
    RswLicenseCertificate& outCert,
    std::string& outError)
{
    std::ifstream in(licPath, std::ios::binary);
    if (!in) {
        outError = "[RSW098] Could not read license file: " + WideToUtf8(licPath.wstring());
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return VerifyRswLicenseTextWithPublicKey(ss.str(), expectedProductSlug, kRaggieSoftLicensePubKeyP256, outCert, outError);
}

/// Discovers and verifies the active `.rswlic` license for `InstallerStudio.exe`, `instbuild.exe`, or `RsPatch.exe`.
/// Rule: **No license file found == present `Community Edition` (`RswIdeEdition::Community`)!**
inline bool ResolveActiveProductLicense(
    std::string_view explicitLicenseFileFlag,
    const std::filesystem::path& exeDir,
    RswLicenseStatus& outStatus,
    std::string& outFatalError)
{
    outStatus = {};
    outStatus.edition              = RswIdeEdition::Community;
    outStatus.isCommercialLicensed = false;
    outStatus.sourceDescription    = "No .rswlic license file present — Community Edition active";

    // 1. Explicit `--license-file=<path.rswlic>` CLI argument (strict: fail if missing or invalid!)
    if (!explicitLicenseFileFlag.empty()) {
        const std::filesystem::path explicitPath(Utf8ToWide(explicitLicenseFileFlag));
        RswLicenseCertificate cert{};
        if (!VerifyProductionRswLicenseFile(explicitPath, "installer-studio", cert, outFatalError)) {
            return false;
        }
        outStatus.isCommercialLicensed = (cert.edition != RswIdeEdition::Community);
        outStatus.edition              = cert.edition;
        outStatus.cert                 = std::move(cert);
        outStatus.sourceFilePath       = explicitPath;
        outStatus.sourceDescription    = WideToUtf8(explicitPath.wstring());
        return true;
    }

    // 2. Headless CI/CD `RSW_LICENSE_KEY` environment variable (file path OR inline `.rswlic` / Base64 secret!)
    wchar_t envBuf[8192]{};
    const DWORD envLen = ::GetEnvironmentVariableW(L"RSW_LICENSE_KEY", envBuf, static_cast<DWORD>(std::size(envBuf)));
    if (envLen > 0 && envLen < std::size(envBuf)) {
        const std::string envVal = TrimAscii(WideToUtf8(std::wstring_view(envBuf, envLen)));
        if (!envVal.empty()) {
            std::error_code ec;
            const std::filesystem::path maybePath(Utf8ToWide(envVal));
            RswLicenseCertificate cert{};
            if (std::filesystem::is_regular_file(maybePath, ec)) {
                if (!VerifyProductionRswLicenseFile(maybePath, "installer-studio", cert, outFatalError)) {
                    return false;
                }
                outStatus.sourceFilePath    = maybePath;
                outStatus.sourceDescription = "RSW_LICENSE_KEY (" + WideToUtf8(maybePath.wstring()) + ")";
            } else {
                if (!VerifyRswLicenseTextWithPublicKey(envVal, "installer-studio", kRaggieSoftLicensePubKeyP256, cert, outFatalError)) {
                    return false;
                }
                outStatus.sourceDescription = "RSW_LICENSE_KEY environment secret";
            }
            outStatus.isCommercialLicensed = (cert.edition != RswIdeEdition::Community);
            outStatus.edition              = cert.edition;
            outStatus.cert                 = std::move(cert);
            return true;
        }
    }

    // 3. Auto-discover `.rswlic` in `<ExeDir>`, `%LOCALAPPDATA%\RaggieSoft\Licenses`, and `%PROGRAMDATA%\RaggieSoft\Licenses`.
    std::vector<std::filesystem::path> searchDirs;
    if (!exeDir.empty()) searchDirs.push_back(exeDir);
    searchDirs.push_back(GetUserLicenseStoreDir());
    const auto machineDir = GetMachineLicenseStoreDir();
    if (!machineDir.empty()) searchDirs.push_back(machineDir);

    std::error_code ec;
    for (const auto& dir : searchDirs) {
        if (dir.empty() || !std::filesystem::is_directory(dir, ec)) continue;
        for (const wchar_t* fn : {L"installer-studio.rswlic", L"raggiesoft-studio.rswlic"}) {
            const auto candidate = dir / fn;
            if (!std::filesystem::is_regular_file(candidate, ec)) continue;
            RswLicenseCertificate cert{};
            std::string ignoreErr;
            if (VerifyProductionRswLicenseFile(candidate, "installer-studio", cert, ignoreErr)) {
                outStatus.isCommercialLicensed = (cert.edition != RswIdeEdition::Community);
                outStatus.edition              = cert.edition;
                outStatus.cert                 = std::move(cert);
                outStatus.sourceFilePath       = candidate;
                outStatus.sourceDescription    = WideToUtf8(candidate.wstring());
                return true;
            }
        }
    }

    // 4. No `.rswlic` license file found -> present Community Edition!
    return true;
}

/// Verifies `sourceRswlicPath` via ECDSA P-256 and installs it into `%LOCALAPPDATA%\RaggieSoft\Licenses\`!
inline bool ImportAndInstallUserLicenseFile(
    const std::filesystem::path& sourceRswlicPath,
    RswLicenseStatus& outNewStatus,
    std::string& outError)
{
    RswLicenseCertificate cert{};
    if (!VerifyProductionRswLicenseFile(sourceRswlicPath, "installer-studio", cert, outError)) {
        return false;
    }

    std::error_code ec;
    const auto storeDir = GetUserLicenseStoreDir();
    std::filesystem::create_directories(storeDir, ec);
    const wchar_t* targetName = (cert.product == "raggiesoft-studio")
        ? L"raggiesoft-studio.rswlic"
        : L"installer-studio.rswlic";
    const auto destPath = storeDir / targetName;
    if (std::filesystem::weakly_canonical(sourceRswlicPath, ec) != std::filesystem::weakly_canonical(destPath, ec)) {
        if (!std::filesystem::copy_file(sourceRswlicPath, destPath, std::filesystem::copy_options::overwrite_existing, ec)) {
            outError = "Verified license signature, but could not copy .rswlic to: " + WideToUtf8(destPath.wstring());
            return false;
        }
    }

    outNewStatus = {};
    outNewStatus.isCommercialLicensed = (cert.edition != RswIdeEdition::Community);
    outNewStatus.edition              = cert.edition;
    outNewStatus.cert                 = std::move(cert);
    outNewStatus.sourceFilePath       = destPath;
    outNewStatus.sourceDescription    = WideToUtf8(destPath.wstring());
    return true;
}

/// Removes user-installed `.rswlic` files from `%LOCALAPPDATA%\RaggieSoft\Licenses\` (returning to Community Edition).
inline bool RemoveUserInstalledLicenseFiles() noexcept {
    std::error_code ec;
    const auto storeDir = GetUserLicenseStoreDir();
    bool removedAny = false;
    for (const wchar_t* fn : {L"installer-studio.rswlic", L"raggiesoft-studio.rswlic"}) {
        if (std::filesystem::remove(storeDir / fn, ec)) {
            removedAny = true;
        }
    }
    return removedAny;
}

} // namespace rsw
