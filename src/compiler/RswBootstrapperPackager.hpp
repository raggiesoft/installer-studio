// ================================================================================
// FILE: src/compiler/RswBootstrapperPackager.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What does this file do?
//     Once `RswMsiCompiler.hpp` has built a standalone Enterprise `Product.msi`,
//     this packager turns our pre-compiled Win32 external UI stub (`SetupWizard.exe`)
//     into a single, self-contained `<Product>-setup.exe` installer!
//
//   How do we embed `Product.msi`, custom `.ico` icons, PNG artwork, branding, and
//   `VERSIONINFO` into `Setup.exe` WITHOUT invoking `cl.exe` on customer machines?
//     Every Windows Portable Executable (`.exe` / `.dll`) contains a structured
//     `.rsrc` (Resource) section managed directly by the Windows kernel (`kernel32.dll`):
//       1. Copy the clean `SetupWizard.exe` binary matching the target CPU (`x64` or `arm64`).
//       2. If `[Signing] Enabled = true`, Authenticode-sign the inner `.msi` FIRST via
//          `signtool.exe` so enterprises extracting `Product.msi` (`--extract-msi`)
//          receive a cryptographically signed MSI database!
//       3. Call `BeginUpdateResourceW` -> `UpdateResourceW` -> `EndUpdateResourceW` to
//          inject:
//            - `RT_RCDATA` #201: The complete `Product.msi` file stream
//            - `RT_RCDATA` #202: UTF-8 branding & layout manifest (`[UI]` colors, modes, text)
//            - `RT_RCDATA` #203: License agreement RTF/text
//            - `RT_RCDATA` #204: Optional PNG artwork (`[UI] ArtworkPng`)
//            - `RT_GROUP_ICON` #101 + `RT_ICON`: Multi-resolution product `.ico` frames
//            - `RT_VERSION` #1: Freshly synthesized `VS_VERSION_INFO` carrying the
//              product's own `CompanyName`, `ProductName`, `FileDescription`, and
//              `FileVersion` (100% zero third-party watermarks!).
//       4. If `[Signing] Enabled = true`, Authenticode-sign the outer `<Product>-setup.exe`!
// ================================================================================

#pragma once

#include "../common/RswLzmsCompression.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswNativeAndSuiteEngine.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswPrerequisites.hpp"
#include "../common/RswVersion.hpp"
#include "RswMsiCompiler.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <imagehlp.h>
#include <sstream>
#include <string>
#include <vector>

#pragma comment(lib, "imagehlp.lib")

namespace rsw {

// Standard `.ico` file header structures on disk (`#pragma pack(push, 2)` matches Win32 `GRPICONDIR`!):
//   In a `.ico` file on disk, each entry ends with a 32-bit `dwImageOffset` file offset.
//   Inside a Win32 `.exe` `RT_GROUP_ICON` resource, that 32-bit offset is replaced by a
//   16-bit `nID` resource ordinal pointing to `RT_ICON` #1, #2, ..., #N!
#pragma pack(push, 2)
struct DiskIconDirHeader {
    std::uint16_t idReserved; // Must be 0
    std::uint16_t idType;     // 1 = Icon
    std::uint16_t idCount;    // Number of icon frames
};

struct DiskIconDirEntry {
    std::uint8_t  bWidth;
    std::uint8_t  bHeight;
    std::uint8_t  bColorCount;
    std::uint8_t  bReserved;
    std::uint16_t wPlanes;
    std::uint16_t wBitCount;
    std::uint32_t dwBytesInRes;
    std::uint32_t dwImageOffset;
};

struct PeGrpIconDirEntry {
    std::uint8_t  bWidth;
    std::uint8_t  bHeight;
    std::uint8_t  bColorCount;
    std::uint8_t  bReserved;
    std::uint16_t wPlanes;
    std::uint16_t wBitCount;
    std::uint32_t dwBytesInRes;
    std::uint16_t nID;        // Ordinal ID of corresponding `RT_ICON` resource!
};
#pragma pack(pop)

/// Parses `"Major.Minor.Build"` into 4 numeric 16-bit components `(Major, Minor, Build, 0)`
/// for `VS_FIXEDFILEINFO` inside `RT_VERSION`.
inline void ParseThreePartVersionNumbers(
    std::string_view ver,
    std::uint16_t& outMaj,
    std::uint16_t& outMin,
    std::uint16_t& outBld) noexcept
{
    outMaj = 1;
    outMin = 0;
    outBld = 0;
    int parts[3]{0, 0, 0};
    int idx = 0;
    for (char c : ver) {
        if (c == '.') {
            if (++idx >= 3) break;
        } else if (c >= '0' && c <= '9') {
            parts[idx] = parts[idx] * 10 + (c - '0');
        }
    }
    outMaj = static_cast<std::uint16_t>(std::clamp(parts[0], 0, 65535));
    outMin = static_cast<std::uint16_t>(std::clamp(parts[1], 0, 65535));
    outBld = static_cast<std::uint16_t>(std::clamp(parts[2], 0, 65535));
}

/// Synthesizes a binary-aligned Win32 `VS_VERSION_INFO` (`RT_VERSION` #1) resource buffer!
/// Why build `VS_VERSION_INFO` in memory?
///   Windows Explorer's "Properties -> Details" tab reads `RT_VERSION` #1 directly from `.exe`
///   headers. Building this exact tree (`VS_VERSIONINFO` -> `VS_FIXEDFILEINFO` + `StringFileInfo`
///   -> `040904B0` -> `String` children + `VarFileInfo` -> `Translation`) stamps the customer's
///   product metadata onto `<Product>-setup.exe` without needing `rc.exe` at package time!
class VersionResourceBuilder {
public:
    static std::vector<std::uint8_t> Build(
        std::string_view companyName,
        std::string_view fileDescription,
        std::string_view version3Part,
        std::string_view internalName,
        std::string_view legalCopyright,
        std::string_view originalFilename,
        std::string_view productName)
    {
        std::uint16_t vMaj = 1, vMin = 0, vBld = 0;
        ParseThreePartVersionNumbers(version3Part, vMaj, vMin, vBld);
        const std::string ver4Part = std::to_string(vMaj) + "." + std::to_string(vMin) + "." + std::to_string(vBld) + ".0";

        std::vector<std::uint8_t> buf;
        const std::size_t rootOff = BeginNode(buf, L"VS_VERSION_INFO", 0 /* binary value */, sizeof(VS_FIXEDFILEINFO));

        // Populate standard 52-byte `VS_FIXEDFILEINFO` (`0xFEEF04BD` signature)
        VS_FIXEDFILEINFO ffi{};
        ffi.dwSignature        = 0xFEEF04BDu;
        ffi.dwStrucVersion     = 0x00010000u;
        ffi.dwFileVersionMS    = (static_cast<DWORD>(vMaj) << 16) | static_cast<DWORD>(vMin);
        ffi.dwFileVersionLS    = (static_cast<DWORD>(vBld) << 16) | 0u;
        ffi.dwProductVersionMS = ffi.dwFileVersionMS;
        ffi.dwProductVersionLS = ffi.dwFileVersionLS;
        ffi.dwFileFlagsMask    = 0x3Fu;
        ffi.dwFileFlags        = 0u;
        ffi.dwFileOS           = VOS_NT_WINDOWS32;
        ffi.dwFileType         = VFT_APP;
        ffi.dwFileSubtype      = 0u;
        AppendBytes(buf, &ffi, sizeof(ffi));
        Align4(buf);

        // Child 1: `StringFileInfo` -> `040904B0` (US English, Unicode UTF-16LE)
        const std::size_t sfiOff   = BeginNode(buf, L"StringFileInfo", 1, 0);
        const std::size_t tableOff = BeginNode(buf, L"040904B0", 1, 0);

        AppendStringEntry(buf, L"CompanyName",      Utf8ToWide(companyName));
        AppendStringEntry(buf, L"FileDescription",  Utf8ToWide(fileDescription));
        AppendStringEntry(buf, L"FileVersion",      Utf8ToWide(ver4Part));
        AppendStringEntry(buf, L"InternalName",     Utf8ToWide(internalName));
        AppendStringEntry(buf, L"LegalCopyright",   Utf8ToWide(legalCopyright));
        AppendStringEntry(buf, L"OriginalFilename", Utf8ToWide(originalFilename));
        AppendStringEntry(buf, L"ProductName",      Utf8ToWide(productName));
        AppendStringEntry(buf, L"ProductVersion",   Utf8ToWide(version3Part));

        EndNode(buf, tableOff);
        EndNode(buf, sfiOff);

        // Child 2: `VarFileInfo` -> `Translation` (`0x0409, 0x04B0` = 1033, 1200)
        const std::size_t vfiOff = BeginNode(buf, L"VarFileInfo", 1, 0);
        const std::size_t trOff  = BeginNode(buf, L"Translation", 0, 4);
        const std::uint16_t trans[2]{0x0409, 0x04B0};
        AppendBytes(buf, trans, sizeof(trans));
        EndNode(buf, trOff);
        EndNode(buf, vfiOff);

        EndNode(buf, rootOff);
        return buf;
    }

private:
    static void Align4(std::vector<std::uint8_t>& b) {
        while ((b.size() % 4u) != 0u) {
            b.push_back(0u);
        }
    }

    static void AppendBytes(std::vector<std::uint8_t>& b, const void* src, std::size_t len) {
        const auto* p = static_cast<const std::uint8_t*>(src);
        b.insert(b.end(), p, p + len);
    }

    static void AppendU16(std::vector<std::uint8_t>& b, std::uint16_t v) {
        AppendBytes(b, &v, sizeof(v));
    }

    static void AppendWStringNull(std::vector<std::uint8_t>& b, std::wstring_view ws) {
        for (wchar_t ch : ws) {
            AppendU16(b, static_cast<std::uint16_t>(ch));
        }
        AppendU16(b, 0u);
    }

    static std::size_t BeginNode(
        std::vector<std::uint8_t>& b,
        std::wstring_view key,
        std::uint16_t wType,
        std::uint16_t wValueLength)
    {
        Align4(b);
        const std::size_t start = b.size();
        AppendU16(b, 0u);           // Placeholder for `wLength` (patched in `EndNode`)
        AppendU16(b, wValueLength); // `wValueLength` (bytes for binary, WCHAR count for text!)
        AppendU16(b, wType);        // `0` = binary data, `1` = text data
        AppendWStringNull(b, key);
        Align4(b);
        return start;
    }

    static void EndNode(std::vector<std::uint8_t>& b, std::size_t startOffset) {
        const auto totalBytes = static_cast<std::uint16_t>(b.size() - startOffset);
        std::memcpy(b.data() + startOffset, &totalBytes, sizeof(totalBytes));
    }

    static void AppendStringEntry(
        std::vector<std::uint8_t>& b,
        std::wstring_view key,
        std::wstring_view value)
    {
        // Note: For `String` nodes (`wType = 1`), Windows requires `wValueLength` in WCHARs INCLUDING null!
        const auto cchWithNull = static_cast<std::uint16_t>(value.size() + 1u);
        const std::size_t off = BeginNode(b, key, 1, cchWithNull);
        AppendWStringNull(b, value);
        EndNode(b, off);
    }
};

/// Inspects a Portable Executable (`.exe` or `.dll`) header on disk and returns its
/// COFF `IMAGE_FILE_HEADER::Machine` constant (`0x8664` AMD64, `0xAA64` ARM64, `0x014C` x86),
/// or `0` if the file is missing or not a valid PE image.
inline std::uint16_t ReadPeMachineArchitecture(const std::filesystem::path& pePath) noexcept {
    std::ifstream in(pePath, std::ios::binary);
    if (!in) return 0;
    IMAGE_DOS_HEADER dos{};
    if (!in.read(reinterpret_cast<char*>(&dos), sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return 0;
    }
    if (dos.e_lfanew <= 0 || dos.e_lfanew > 64 * 1024 * 1024) return 0;
    in.seekg(dos.e_lfanew, std::ios::beg);
    DWORD peSig = 0;
    IMAGE_FILE_HEADER fh{};
    if (!in.read(reinterpret_cast<char*>(&peSig), sizeof(peSig)) || peSig != IMAGE_NT_SIGNATURE) {
        return 0;
    }
    if (!in.read(reinterpret_cast<char*>(&fh), sizeof(fh))) {
        return 0;
    }
    return fh.Machine;
}

/// Synthesizes a complete UTF-8 Win32 Side-by-Side Application Manifest (`RT_MANIFEST` `#1`)
/// stamping the author's chosen `requestedExecutionLevel` (`asInvoker`, `highestAvailable`, or
/// `requireAdministrator`) while preserving Common Controls v6, Windows 11 `<supportedOS>`,
/// Per-Monitor V2 DPI scaling, and `longPathAware`!
inline std::string BuildSetupApplicationManifestXml(std::string_view rawExecLevel) {
    const std::string_view level = NormalizeRequestedExecutionLevel(rawExecLevel);
    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\r\n"
        << "<assembly xmlns=\"urn:schemas-microsoft-com:asm.v1\" manifestVersion=\"1.0\">\r\n"
        << "  <assemblyIdentity version=\"0.5.1.0\" name=\"RaggieSoft.SetupWizard.Bootstrapper\" type=\"win32\"/>\r\n"
        << "  <dependency>\r\n"
        << "    <dependentAssembly>\r\n"
        << "      <assemblyIdentity type=\"win32\" name=\"Microsoft.Windows.Common-Controls\" version=\"6.0.0.0\" processorArchitecture=\"*\" publicKeyToken=\"6595b64144ccf1df\" language=\"*\"/>\r\n"
        << "    </dependentAssembly>\r\n"
        << "  </dependency>\r\n"
        << "  <trustInfo xmlns=\"urn:schemas-microsoft-com:asm.v3\">\r\n"
        << "    <security>\r\n"
        << "      <requestedPrivileges>\r\n"
        << "        <requestedExecutionLevel level=\"" << level << "\" uiAccess=\"false\"/>\r\n"
        << "      </requestedPrivileges>\r\n"
        << "    </security>\r\n"
        << "  </trustInfo>\r\n"
        << "  <compatibility xmlns=\"urn:schemas-microsoft-com:compatibility.v1\">\r\n"
        << "    <application>\r\n"
        << "      <supportedOS Id=\"{8e0f7a12-bfb3-4fe8-b9a5-48fd50a15a9a}\"/>\r\n"
        << "    </application>\r\n"
        << "  </compatibility>\r\n"
        << "  <application xmlns=\"urn:schemas-microsoft-com:asm.v3\">\r\n"
        << "    <windowsSettings>\r\n"
        << "      <dpiAware xmlns=\"http://schemas.microsoft.com/SMI/2005/WindowsSettings\">true/pm</dpiAware>\r\n"
        << "      <dpiAwareness xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">PerMonitorV2, PerMonitor</dpiAwareness>\r\n"
        << "      <longPathAware xmlns=\"http://schemas.microsoft.com/SMI/2016/WindowsSettings\">true</longPathAware>\r\n"
        << "    </windowsSettings>\r\n"
        << "  </application>\r\n"
        << "</assembly>\r\n";
    return xml.str();
}

/// Enumerates existing language IDs on `RT_MANIFEST` `#1` inside a staged PE file BEFORE
/// calling `BeginUpdateResourceW` so we can replace/prune any alternate LCID (`0` vs `1033`)
/// and guarantee strictly ONE `RT_MANIFEST` `#1` resource (preventing `ERROR_SXS_CANT_GEN_ACTCTX`!).
inline std::vector<WORD> QueryExistingPeManifestLangIds(const std::filesystem::path& exePath) noexcept {
    std::vector<WORD> langs;
    HMODULE hMod = ::LoadLibraryExW(
        exePath.wstring().c_str(), nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!hMod) return langs;
    ::EnumResourceLanguagesW(
        hMod, RT_MANIFEST, MAKEINTRESOURCEW(1),
        [](HMODULE, LPCWSTR, LPCWSTR, WORD wLang, LONG_PTR lParam) -> BOOL {
            auto* vec = reinterpret_cast<std::vector<WORD>*>(lParam);
            vec->push_back(wLang);
            return TRUE;
        },
        reinterpret_cast<LONG_PTR>(&langs));
    ::FreeLibrary(hMod);
    return langs;
}

inline bool StampExecutionLevelManifestOnResource(
    HANDLE hRes,
    const std::vector<WORD>& existingManifestLangs,
    std::string_view rawExecLevel) noexcept
{
    constexpr WORD kLangEnUs = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    bool ok = true;
    for (WORD oldLang : existingManifestLangs) {
        if (oldLang != kLangEnUs) {
            ok = ok && ::UpdateResourceW(hRes, RT_MANIFEST, MAKEINTRESOURCEW(1), oldLang, nullptr, 0);
        }
    }
    const std::string xml = BuildSetupApplicationManifestXml(rawExecLevel);
    ok = ok && ::UpdateResourceW(
        hRes, RT_MANIFEST, MAKEINTRESOURCEW(1), kLangEnUs,
        const_cast<char*>(xml.data()), static_cast<DWORD>(xml.size()));
    return ok;
}

/// Strips any existing Authenticode `WIN_CERTIFICATE` entries (`IMAGE_DIRECTORY_ENTRY_SECURITY`)
/// from a copied PE template BEFORE calling `BeginUpdateResourceW` / `EndUpdateResourceW`.
/// Why is this essential?
///   When `SetupWizard.exe` inside `Program Files\RaggieSoft\Installer Studio\` is itself
///   Authenticode-signed, mutating its `.rsrc` section via `EndUpdateResourceW` without first
///   removing the old certificate table would leave a stale `SECURITY` data directory pointing
///   to shifted file offsets! Calling `ImageRemoveCertificate` (`imagehlp.dll`) resets the
///   security directory cleanly before stamping new resources and re-signing.
inline bool StripExistingPeCertificates(const std::filesystem::path& exePath) noexcept {
    HANDLE hFile = ::CreateFileW(
        exePath.wstring().c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD certCount = 0;
    if (::ImageEnumerateCertificates(hFile, CERT_SECTION_TYPE_ANY, &certCount, nullptr, 0) && certCount > 0) {
        for (DWORD i = certCount; i > 0; --i) {
            ::ImageRemoveCertificate(hFile, i - 1u);
        }
    }
    ::CloseHandle(hFile);
    return true;
}

/// Reads an optional Win32 environment variable as UTF-8 (`""` when unset) without CRT warnings.
inline std::string ReadSignEnvUtf8(const wchar_t* varName) {
    const DWORD needed = ::GetEnvironmentVariableW(varName, nullptr, 0);
    if (needed == 0) return {};
    std::wstring buf(needed, L'\0');
    const DWORD written = ::GetEnvironmentVariableW(varName, buf.data(), needed);
    if (written == 0) return {};
    buf.resize(written);
    return TrimAscii(WideToUtf8(buf));
}

/// Discovers `signtool.exe` inside the installed Windows 11 SDK (`C:\Program Files (x86)\Windows Kits\10\bin\...`).
/// Architecture-Matching Rule for Azure Artifact Signing (`/dlib`):
///   `signtool.exe` loads `Azure.CodeSigning.Dlib.dll` in-process via `LoadLibraryW`.
///   Because Microsoft's `Microsoft.Trusted.Signing.Client` NuGet package ships `x64` and `x86`
///   C++/CLI (`Ijwhost.dll`) binaries, running `arm64\signtool.exe` against `x64\Azure.CodeSigning.Dlib.dll`
///   on a Windows 11 ARM64 PC would fail with `0x800700C1` (`ERROR_BAD_EXE_FORMAT`).
///   Inspecting `ReadPeMachineArchitecture(optionalDlibPath)` selects the exact matching
///   `x64\signtool.exe`, `arm64\signtool.exe`, or `x86\signtool.exe` automatically!
inline std::filesystem::path LocateWindowsSdkSignTool(const std::filesystem::path& optionalDlibPath = {}) {
    std::error_code ec;
    const std::filesystem::path kitsBin = L"C:\\Program Files (x86)\\Windows Kits\\10\\bin";
    if (!std::filesystem::exists(kitsBin, ec)) {
        return {};
    }

    std::vector<std::filesystem::path> verDirs;
    for (const auto& de : std::filesystem::directory_iterator(kitsBin, ec)) {
        if (de.is_directory(ec)) {
            const std::wstring name = de.path().filename().wstring();
            if (name.rfind(L"10.", 0) == 0) verDirs.push_back(de.path());
        }
    }
    std::sort(verDirs.rbegin(), verDirs.rend()); // Newest SDK version first (`10.0.26100.0`)

    // If an external `/dlib` DLL was supplied, strictly match its PE `Machine` architecture!
    if (!optionalDlibPath.empty() && std::filesystem::exists(optionalDlibPath, ec)) {
        const std::uint16_t dlibMachine = ReadPeMachineArchitecture(optionalDlibPath);
        const wchar_t* requiredSubdir = nullptr;
        if (dlibMachine == IMAGE_FILE_MACHINE_AMD64)      requiredSubdir = L"x64";
        else if (dlibMachine == IMAGE_FILE_MACHINE_ARM64) requiredSubdir = L"arm64";
        else if (dlibMachine == IMAGE_FILE_MACHINE_I386)  requiredSubdir = L"x86";

        if (requiredSubdir != nullptr) {
            for (const auto& vd : verDirs) {
                auto candidate = vd / requiredSubdir / L"signtool.exe";
                if (std::filesystem::exists(candidate, ec)) {
                    return candidate;
                }
            }
        }
    }

    const wchar_t* archOrder[] = {L"arm64", L"x64", L"x86"};
    for (const auto& vd : verDirs) {
        for (const wchar_t* a : archOrder) {
            auto candidate = vd / a / L"signtool.exe";
            if (std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
        }
    }
    return {};
}

/// Runs Authenticode signing via `signtool.exe` (supporting local Certificate Store OR Azure Artifact Signing,
/// plus CI/release `RSW_SIGN_*` environment overrides so public `.rsip` repos stay free of local workstation paths).
inline bool RunAuthenticodeSignTool(
    const RswManifestDocument& doc,
    const std::filesystem::path& targetBinary,
    std::string& outError)
{
    const std::string envEnabled = ReadSignEnvUtf8(L"RSW_SIGN_ENABLED");
    const bool signEnabled = doc.GetBool("Signing", "", "Enabled", false) ||
                             envEnabled == "1" ||
                             EqualsIgnoreCase(envEnabled, "true");
    if (!signEnabled) {
        return true;
    }

    const std::filesystem::path baseDir = doc.sourcePath.empty()
        ? std::filesystem::current_path()
        : doc.sourcePath.parent_path();
    auto resolveOptPath = [&](std::string_view raw) -> std::filesystem::path {
        if (raw.empty()) return {};
        std::filesystem::path p(Utf8ToWide(raw));
        return p.is_absolute() ? p : (baseDir / p);
    };

    std::string rawDlib  = doc.GetString("Signing", "", "DlibPath");
    std::string rawMeta  = doc.GetString("Signing", "", "MetadataJson");
    std::string thumb    = doc.GetString("Signing", "", "Thumbprint");
    std::string tsUrl    = doc.GetString("Signing", "", "TimestampUrl");

    if (rawDlib.empty()) rawDlib = ReadSignEnvUtf8(L"RSW_SIGN_DLIB");
    if (rawMeta.empty()) rawMeta = ReadSignEnvUtf8(L"RSW_SIGN_METADATA");
    if (thumb.empty())   thumb   = ReadSignEnvUtf8(L"RSW_SIGN_THUMBPRINT");
    if (tsUrl.empty())   tsUrl   = ReadSignEnvUtf8(L"RSW_SIGN_TIMESTAMP_URL");
    if (tsUrl.empty())   tsUrl   = "http://timestamp.acs.microsoft.com";

    const auto dlibPath = resolveOptPath(rawDlib);
    const auto metaPath = resolveOptPath(rawMeta);

    const std::filesystem::path signTool = LocateWindowsSdkSignTool(dlibPath);
    if (signTool.empty()) {
        outError = "Signing is enabled, but a matching Windows SDK signtool.exe was not found.";
        return false;
    }

    // Embed human-readable `ProductName` (`/d`) and `AboutUrl` (`/du`) inside Authenticode `SpcSpOpusInfo`
    // so Windows 11 UAC (`consent.exe`) displays the real product title instead of a random `C:\Windows\Installer\xxxxxx.msi` cache filename!
    auto stripQuotesAndCtl = [](std::string s) {
        std::string clean;
        clean.reserve(s.size());
        for (char c : s) {
            if (c == '"' || c == '\r' || c == '\n') continue;
            clean.push_back(c);
        }
        return TrimAscii(clean);
    };
    const std::string opusDesc = stripQuotesAndCtl(doc.GetString("Package", "", "Name"));
    const std::string opusUrl  = stripQuotesAndCtl(doc.GetString("Package", "", "AboutUrl"));

    std::wostringstream cmd;
    cmd << L"\"" << signTool.wstring() << L"\" sign /fd SHA256 /td SHA256 /tr \""
        << Utf8ToWide(tsUrl) << L"\" ";
    if (!opusDesc.empty()) {
        cmd << L"/d \"" << Utf8ToWide(opusDesc) << L"\" ";
    }
    if (!opusUrl.empty()) {
        cmd << L"/du \"" << Utf8ToWide(opusUrl) << L"\" ";
    }

    if (!dlibPath.empty() && !metaPath.empty()) {
        cmd << L"/dlib \"" << dlibPath.wstring() << L"\" /dmdf \"" << metaPath.wstring() << L"\" ";
    } else if (!thumb.empty()) {
        cmd << L"/sha1 \"" << Utf8ToWide(thumb) << L"\" ";
    } else {
        cmd << L"/a ";
    }
    cmd << L"\"" << targetBinary.wstring() << L"\"";

    // Retry up to 3 times to ride through transient RFC 3161 timestamp or Azure RBAC warm-up blips.
    std::string lastOutput;
    DWORD lastExitCode = 1;
    for (int attempt = 1; attempt <= 3; ++attempt) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength              = sizeof(sa);
        sa.bInheritHandle       = TRUE;
        HANDLE hReadPipe        = nullptr;
        HANDLE hWritePipe       = nullptr;
        const bool hasPipe      = (::CreatePipe(&hReadPipe, &hWritePipe, &sa, 0) != FALSE);
        if (hasPipe) {
            ::SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);
        }

        STARTUPINFOW si{};
        si.cb = sizeof(si);
        if (hasPipe) {
            si.dwFlags    = STARTF_USESTDHANDLES;
            si.hStdOutput = hWritePipe;
            si.hStdError  = hWritePipe;
        }
        PROCESS_INFORMATION pi{};
        std::wstring mutableCmd = cmd.str();
        if (!::CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, hasPipe ? TRUE : FALSE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
            const DWORD err = ::GetLastError();
            if (hasPipe) {
                ::CloseHandle(hReadPipe);
                ::CloseHandle(hWritePipe);
            }
            outError = "Failed launching signtool.exe (" + std::to_string(err) + ").";
            return false;
        }
        if (hasPipe) {
            ::CloseHandle(hWritePipe);
        }

        std::string captured;
        if (hasPipe) {
            char buf[512];
            DWORD bytesRead = 0;
            while (::ReadFile(hReadPipe, buf, sizeof(buf), &bytesRead, nullptr) && bytesRead > 0) {
                captured.append(buf, bytesRead);
            }
            ::CloseHandle(hReadPipe);
        }

        ::WaitForSingleObject(pi.hProcess, INFINITE);
        ::GetExitCodeProcess(pi.hProcess, &lastExitCode);
        ::CloseHandle(pi.hThread);
        ::CloseHandle(pi.hProcess);

        if (lastExitCode == 0) {
            return true;
        }
        lastOutput = TrimAscii(captured);
        if (attempt < 3) {
            ::Sleep(2500);
        }
    }

    outError = "signtool.exe exited with code " + std::to_string(lastExitCode) +
               " while signing '" + WideToUtf8(targetBinary.filename().wstring()) + "'" +
               (lastOutput.empty() ? "." : (": " + lastOutput));
    return false;
}

/// Packages `compiledMsi` + product branding + `.ico` + `VS_VERSION_INFO` into `<OutputName>-setup.exe`!
inline bool PackageSelfExtractingBootstrapper(
    const RswManifestDocument& doc,
    const CompiledMsiSummary& msiSummary,
    const std::filesystem::path& compilerExeDir,
    std::filesystem::path& outSetupExePath,
    std::string& outError,
    bool skipInnerMsiSign = false)
{
    std::error_code ec;
    const std::string arch = msiSummary.architecture;

    // 1. Sign inner `Product.msi` BEFORE embedding it into `Setup.exe` (Dual-Layer Signing Step 1!)
    if (!skipInnerMsiSign && !RunAuthenticodeSignTool(doc, msiSummary.msiPath, outError)) {
        return false;
    }

    if (!doc.GetBool("Package", "", "BuildBootstrapper", true)) {
        outSetupExePath.clear();
        return true;
    }

    // 2. Locate the matching `SetupWizard.exe` native template binary (`x64` or `arm64`)
    std::vector<std::filesystem::path> stubCandidates = {
        compilerExeDir / L"SetupWizard.exe",
        compilerExeDir.parent_path() / Utf8ToWide(arch) / L"SetupWizard.exe",
        compilerExeDir / Utf8ToWide(arch) / L"SetupWizard.exe"
    };
    std::filesystem::path stubTemplate;
    for (const auto& c : stubCandidates) {
        if (std::filesystem::exists(c, ec)) {
            stubTemplate = c;
            break;
        }
    }
    if (stubTemplate.empty()) {
        outError = "Could not locate compiled bootstrapper stub 'SetupWizard.exe' for architecture '" + arch + "'.";
        return false;
    }

    const std::string msiStem = WideToUtf8(msiSummary.msiPath.stem().wstring());
    outSetupExePath = msiSummary.msiPath.parent_path() / (msiStem + "-setup.exe");

    // Stage PE resource injection & Authenticode signing on local NTFS `%TEMP%` first!
    // Why? Windows `EndUpdateResourceW` creates & memory-maps a temporary `RCX*.tmp` file
    // alongside the target EXE, which can intermittently fail over SMB / Parallels Shared
    // Folders (`Y:\`) on >1 GB payloads. Building on local `%TEMP%` and copying out is 100% atomic!
    const std::filesystem::path stageSetupPath =
        std::filesystem::temp_directory_path(ec) /
        Utf8ToWide("rsw-stage-" + std::to_string(::GetCurrentProcessId()) + "-" + msiStem + "-setup.exe");
    std::filesystem::remove(stageSetupPath, ec);
    if (!std::filesystem::copy_file(stubTemplate, stageSetupPath, std::filesystem::copy_options::overwrite_existing, ec)) {
        outError = "Failed staging SetupWizard.exe template to '" + WideToUtf8(stageSetupPath.wstring()) + "'.";
        return false;
    }
    StripExistingPeCertificates(stageSetupPath);

    // 3. Read `Product.msi` bytes, compress with Windows `COMPRESS_ALGORITHM_LZMS` (`Cabinet.lib`),
    //    resolve `[Prerequisite.*]` runtimes, and synthesize the UTF-8 branding & layout manifest (`#202`)!
    std::vector<std::uint8_t> rawMsiBytes;
    if (!ReadBinaryFileBytes(msiSummary.msiPath, rawMsiBytes, outError)) {
        return false;
    }
    std::vector<std::uint8_t> msiBytes = CompressWithLzmsIfSmaller(rawMsiBytes);

    const auto prereqList       = ResolveManifestPrerequisites(doc, arch);
    const std::string prereqStr = SerializePrerequisiteTableIni(prereqList);

    // Stage `embedded` runtime binaries in memory (compressed with `LZMS`!) and copy any
    // `side-by-side` `SourceFile` runtimes into `<OutputDir>\Redist\<FileName>`!
    struct EmbeddedPrereqBlob {
        int                       resId = 0;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<EmbeddedPrereqBlob> embeddedPrereqs;
    for (const auto& pr : prereqList) {
        if (pr.distribution == "embedded") {
            std::vector<std::uint8_t> rawPrBytes;
            if (!ReadBinaryFileBytes(pr.buildSourcePath, rawPrBytes, outError)) {
                return false;
            }
            embeddedPrereqs.push_back({pr.embeddedResId, CompressWithLzmsIfSmaller(rawPrBytes)});
        } else if (pr.distribution == "side-by-side" && !pr.buildSourcePath.empty() &&
                   std::filesystem::exists(pr.buildSourcePath, ec)) {
            const auto redistDir = outSetupExePath.parent_path() / L"Redist";
            std::filesystem::create_directories(redistDir, ec);
            std::filesystem::copy_file(
                pr.buildSourcePath,
                redistDir / Utf8ToWide(pr.fileName),
                std::filesystem::copy_options::overwrite_existing,
                ec);
        }
    }

    const std::filesystem::path baseDir = doc.sourcePath.empty()
        ? std::filesystem::current_path()
        : doc.sourcePath.parent_path();
    auto resolvePath = [&](std::string_view raw) -> std::filesystem::path {
        if (raw.empty()) return {};
        std::string exp = ExpandArchToken(raw, arch);
        std::filesystem::path p(Utf8ToWide(exp));
        return (p.is_absolute() ? p : (baseDir / p)).lexically_normal();
    };

    auto sanitizeSingleLine = [](std::string s) -> std::string {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '\r') continue;
            if (c == '\n') out += "\\n";
            else out += c;
        }
        return out;
    };

    const std::string requestedExecLvl = std::string(
        NormalizeRequestedExecutionLevel(doc.GetString("Package", "", "RequestedExecutionLevel", "asInvoker")));

    std::ostringstream brandIni;
    brandIni << "[Branding]\n"
             << "ProductName="        << sanitizeSingleLine(msiSummary.productName) << "\n"
             << "Tagline="            << sanitizeSingleLine(ExpandArchToken(doc.GetString("Package", "", "Tagline", msiSummary.manufacturer + " Setup"), arch)) << "\n"
             << "WebsiteLabel="       << sanitizeSingleLine(doc.GetString("Package", "", "WebsiteLabel", "")) << "\n"
             << "Version="            << sanitizeSingleLine(msiSummary.productVersion) << "\n"
             << "Manufacturer="       << sanitizeSingleLine(msiSummary.manufacturer) << "\n"
             << "Architecture="       << sanitizeSingleLine(msiSummary.architecture) << "\n"
             << "ProductCode="        << sanitizeSingleLine(msiSummary.productCode) << "\n"
             << "UpgradeCode="        << sanitizeSingleLine(msiSummary.upgradeCode) << "\n"
             << "RequestedExecutionLevel=" << requestedExecLvl << "\n"
             << "DefaultSubdir="      << sanitizeSingleLine(msiSummary.defaultSubdir) << "\n"
             << "StartMenuFolder="    << sanitizeSingleLine(doc.GetString("Install", "", "StartMenuFolder", msiSummary.manufacturer)) << "\n"
             << "MainExecutable="     << sanitizeSingleLine(msiSummary.mainExeRel) << "\n"
             << "PerMachine="         << (msiSummary.perMachine ? "true" : "false") << "\n"
             << "MinWindowsBuild="    << msiSummary.minWindowsBuild << "\n"
             << "SilentRebootExitCode=" << doc.GetInt("Requirements", "", "SilentRebootExitCode", 3010) << "\n"
             << "UncompressedMsiSize=" << rawMsiBytes.size() << "\n"
             << "OsLaunchMessage="    << sanitizeSingleLine(msiSummary.osLaunchMessage) << "\n"
             << "WizardLayout="       << sanitizeSingleLine(doc.GetString("UI", "", "WizardLayout", "standard")) << "\n"
             << "AccentPrimaryHex="   << sanitizeSingleLine(doc.GetString("UI", "", "AccentPrimaryHex", "#2563EB")) << "\n"
             << "AccentSecondaryHex=" << sanitizeSingleLine(doc.GetString("UI", "", "AccentSecondaryHex", "#FBBF24")) << "\n"
             << "WelcomeHeadline="    << sanitizeSingleLine(doc.GetString("UI", "", "WelcomeHeadline", "Welcome to " + msiSummary.productName + " Setup")) << "\n"
             << "WelcomeIntro="       << sanitizeSingleLine(doc.GetString("UI", "", "WelcomeIntro", doc.GetString("Package", "", "Description", ""))) << "\n"
             << "ReadMeText="         << sanitizeSingleLine(doc.GetString("UI", "", "ReadMeText", "")) << "\n"
             << "BannerTitle="        << sanitizeSingleLine(doc.GetString("UI", "", "BannerTitle", "Choose how you would like to install " + msiSummary.productName + ".")) << "\n"
             << "InstallButtonText="  << sanitizeSingleLine(msiSummary.installBtnText) << "\n"
             << "FinishLaunchCheckbox=" << sanitizeSingleLine(msiSummary.launchChkText) << "\n"
             << "OfferLaunchOnFinish=" << (msiSummary.offerLaunchOnFinish ? "true" : "false") << "\n"
             << "HasLicenseRtf="      << (!msiSummary.licenseRtfPath.empty() ? "true" : "false") << "\n"
             << "ExpressDescription=" << sanitizeSingleLine(doc.GetString("UI", "", "ExpressDescription",
                  "One-click setup into the default installation folder with recommended shortcuts. Skips extra prompts.")) << "\n"
             << "CompactDescription=" << sanitizeSingleLine(doc.GetString("UI", "", "CompactDescription",
                  "Installs essential application files with minimal disk footprint using recommended folder defaults.")) << "\n"
             << "CustomDescription="  << sanitizeSingleLine(doc.GetString("UI", "", "CustomDescription",
                  "Choose a custom destination folder, Start Menu group, and shortcuts step by step before installing.")) << "\n"
             << "AuthorOrArtist="     << sanitizeSingleLine(doc.GetString("UI", "", "AuthorOrArtist", "")) << "\n"
             << "SetupTypePreset="    << sanitizeSingleLine(doc.GetString("UI", "", "SetupTypePreset", "express_custom")) << "\n"
             << "SystemMenuAbout="    << (doc.GetBool("UI", "", "SystemMenuAbout", false) ? "true" : "false") << "\n"
             << "ExternalMsiFileName="<< (msiSummary.usesExternalMedia ? sanitizeSingleLine(WideToUtf8(msiSummary.msiPath.filename().wstring())) : std::string{}) << "\n";

    // Serialize optional author-defined `[SetupType.<id>]` cards (`Typical`, `Compact`, `Complete`, custom presets)
    const auto setupTypeIds = doc.GetNamedSectionIds("SetupType");
    brandIni << "SetupTypeCount=" << setupTypeIds.size() << "\n";
    for (std::size_t i = 0; i < setupTypeIds.size(); ++i) {
        const auto& sid = setupTypeIds[i];
        const std::string pfx = "SetupType_" + std::to_string(i) + "_";
        brandIni << pfx << "Id="          << sanitizeSingleLine(sid) << "\n"
                 << pfx << "Title="       << sanitizeSingleLine(doc.GetString("SetupType", sid, "Title", "&" + sid)) << "\n"
                 << pfx << "Description=" << sanitizeSingleLine(doc.GetString("SetupType", sid, "Description", "")) << "\n"
                 << pfx << "Tier="        << sanitizeSingleLine(doc.GetString("SetupType", sid, "Tier", "typical")) << "\n"
                 << pfx << "Features="    << sanitizeSingleLine(doc.GetString("SetupType", sid, "Features", "")) << "\n"
                 << pfx << "Glyph="       << sanitizeSingleLine(doc.GetString("SetupType", sid, "Glyph", "Bolt")) << "\n"
                 << pfx << "Recommended=" << (doc.GetBool("SetupType", sid, "Recommended", i == 0) ? "true" : "false") << "\n";
    }

    // Serialize optional `[Billboard.<id>]` slides shown on full-screen / progress surfaces during installation
    const auto billboardIds = doc.GetNamedSectionIds("Billboard");
    brandIni << "BillboardCount=" << billboardIds.size() << "\n";
    for (std::size_t i = 0; i < billboardIds.size(); ++i) {
        const auto& bid = billboardIds[i];
        const std::string pfx = "Billboard_" + std::to_string(i) + "_";
        brandIni << pfx << "Headline=" << sanitizeSingleLine(doc.GetString("Billboard", bid, "Headline", "")) << "\n"
                 << pfx << "Body="     << sanitizeSingleLine(doc.GetString("Billboard", bid, "Body", "")) << "\n"
                 << pfx << "Glyph="    << sanitizeSingleLine(doc.GetString("Billboard", bid, "Glyph", "Sparkle")) << "\n";
    }
    const std::string brandStr = brandIni.str();

    const std::string licenseRtf = LoadOrSynthesizeLicenseRtf(
        msiSummary.licenseRtfPath, msiSummary.productName, msiSummary.manufacturer);

    const std::filesystem::path artworkPath = resolvePath(doc.GetString("UI", "", "ArtworkPng"));
    std::vector<std::uint8_t> artworkBytes;
    if (!artworkPath.empty() && std::filesystem::exists(artworkPath, ec)) {
        ReadBinaryFileBytes(artworkPath, artworkBytes, outError);
    }

    // 4. Inspect existing `RT_MANIFEST` `#1` languages, then open PE resource table on local NTFS `stageSetupPath`!
    const auto existingManifestLangs = QueryExistingPeManifestLangIds(stageSetupPath);
    HANDLE hRes = ::BeginUpdateResourceW(stageSetupPath.wstring().c_str(), FALSE);
    if (!hRes) {
        const DWORD gle = ::GetLastError();
        std::filesystem::remove(stageSetupPath, ec);
        outError = "BeginUpdateResourceW failed on '" + WideToUtf8(stageSetupPath.wstring()) +
                   "' (" + std::to_string(gle) + ").";
        return false;
    }

    constexpr WORD kLangNeutral = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    constexpr WORD kLangEnUs    = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);

    bool ok = true;
    ok = ok && ::UpdateResourceW(
        hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdEmbeddedMsi), kLangNeutral,
        msiBytes.data(), static_cast<DWORD>(msiBytes.size()));
    ok = ok && ::UpdateResourceW(
        hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdBrandingIni), kLangNeutral,
        const_cast<char*>(brandStr.data()), static_cast<DWORD>(brandStr.size()));
    ok = ok && ::UpdateResourceW(
        hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdLicenseRtf), kLangNeutral,
        const_cast<char*>(licenseRtf.data()), static_cast<DWORD>(licenseRtf.size()));
    ok = ok && ::UpdateResourceW(
        hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdPrereqManifest), kLangNeutral,
        const_cast<char*>(prereqStr.data()), static_cast<DWORD>(prereqStr.size()));

    std::string featPackStr;
    if (!msiSummary.featurePacks.empty()) {
        featPackStr = SerializeFeaturePackManifestIni(msiSummary.featurePacks);
        ok = ok && ::UpdateResourceW(
            hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdFeaturePackManifest), kLangNeutral,
            const_cast<char*>(featPackStr.data()), static_cast<DWORD>(featPackStr.size()));
    }

    for (const auto& ep : embeddedPrereqs) {
        ok = ok && ::UpdateResourceW(
            hRes, RT_RCDATA, MAKEINTRESOURCEW(ep.resId), kLangNeutral,
            const_cast<std::uint8_t*>(ep.bytes.data()), static_cast<DWORD>(ep.bytes.size()));
    }

    if (!artworkBytes.empty()) {
        ok = ok && ::UpdateResourceW(
            hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdArtworkPng), kLangNeutral,
            artworkBytes.data(), static_cast<DWORD>(artworkBytes.size()));
    }

    // 5. Stamp custom `.ico` (`RT_GROUP_ICON` #101 + `RT_ICON` #1..N) if `[UI] IconFile` was supplied!
    if (ok && !msiSummary.iconFilePath.empty() && std::filesystem::exists(msiSummary.iconFilePath, ec)) {
        std::vector<std::uint8_t> icoBytes;
        if (ReadBinaryFileBytes(msiSummary.iconFilePath, icoBytes, outError) &&
            icoBytes.size() >= sizeof(DiskIconDirHeader)) {
            DiskIconDirHeader hdr{};
            std::memcpy(&hdr, icoBytes.data(), sizeof(hdr));
            const std::size_t entriesBytes = static_cast<std::size_t>(hdr.idCount) * sizeof(DiskIconDirEntry);
            if (hdr.idReserved == 0 && hdr.idType == 1 && hdr.idCount > 0 &&
                icoBytes.size() >= sizeof(hdr) + entriesBytes) {
                std::vector<std::uint8_t> grpBuf(sizeof(DiskIconDirHeader) + hdr.idCount * sizeof(PeGrpIconDirEntry));
                std::memcpy(grpBuf.data(), &hdr, sizeof(hdr));

                bool framesValid = true;
                for (std::uint16_t i = 0; i < hdr.idCount; ++i) {
                    DiskIconDirEntry de{};
                    std::memcpy(&de, icoBytes.data() + sizeof(hdr) + i * sizeof(de), sizeof(de));
                    if (static_cast<std::size_t>(de.dwImageOffset) + de.dwBytesInRes > icoBytes.size()) {
                        framesValid = false;
                        break;
                    }
                    const std::uint16_t iconId = static_cast<std::uint16_t>(i + 1u);
                    ok = ok && ::UpdateResourceW(
                        hRes, RT_ICON, MAKEINTRESOURCEW(iconId), kLangEnUs,
                        icoBytes.data() + de.dwImageOffset, de.dwBytesInRes);

                    PeGrpIconDirEntry ge{};
                    ge.bWidth       = de.bWidth;
                    ge.bHeight      = de.bHeight;
                    ge.bColorCount  = de.bColorCount;
                    ge.bReserved    = de.bReserved;
                    ge.wPlanes      = de.wPlanes;
                    ge.wBitCount    = de.wBitCount;
                    ge.dwBytesInRes = de.dwBytesInRes;
                    ge.nID          = iconId;
                    std::memcpy(grpBuf.data() + sizeof(hdr) + i * sizeof(ge), &ge, sizeof(ge));
                }
                if (framesValid) {
                    ok = ok && ::UpdateResourceW(
                        hRes, RT_GROUP_ICON, MAKEINTRESOURCEW(101), kLangEnUs,
                        grpBuf.data(), static_cast<DWORD>(grpBuf.size()));
                }
            }
        }
    }

    // 6. Stamp 100% first-party `VS_VERSION_INFO` (`RT_VERSION` #1) onto `<OutputName>-setup.exe`!
    const std::string setupFileName = WideToUtf8(outSetupExePath.filename().wstring());
    const std::string fileDesc = doc.GetString("Package", "", "Description", msiSummary.productName + " Setup");
    const std::string copyright = "Copyright (c) 2026 " + msiSummary.manufacturer;
    auto verBytes = VersionResourceBuilder::Build(
        msiSummary.manufacturer,
        fileDesc,
        msiSummary.productVersion,
        msiStem,
        copyright,
        setupFileName,
        msiSummary.productName);
    ok = ok && ::UpdateResourceW(
        hRes, RT_VERSION, MAKEINTRESOURCEW(1), kLangEnUs,
        verBytes.data(), static_cast<DWORD>(verBytes.size()));
    ok = ok && StampExecutionLevelManifestOnResource(hRes, existingManifestLangs, requestedExecLvl);

    if (!::EndUpdateResourceW(hRes, !ok) || !ok) {
        const DWORD gle = ::GetLastError();
        std::filesystem::remove(stageSetupPath, ec);
        outError = "EndUpdateResourceW failed while stamping '" + WideToUtf8(outSetupExePath.wstring()) +
                   "' (Win32 error " + std::to_string(gle) + ").";
        return false;
    }

    // 7. Sign outer staged EXE on local NTFS, then copy atomically to `outSetupExePath`!
    if (!RunAuthenticodeSignTool(doc, stageSetupPath, outError)) {
        std::filesystem::remove(stageSetupPath, ec);
        return false;
    }
    std::filesystem::remove(outSetupExePath, ec);
    if (!std::filesystem::copy_file(stageSetupPath, outSetupExePath, std::filesystem::copy_options::overwrite_existing, ec)) {
        std::filesystem::remove(stageSetupPath, ec);
        outError = "Failed copying stamped Setup.exe to '" + WideToUtf8(outSetupExePath.wstring()) +
                   "' (close any running Setup.exe window and retry).";
        return false;
    }
    std::filesystem::remove(stageSetupPath, ec);
    return true;
}

/// Checks whether a PE executable on disk has `IMAGE_FILE_MACHINE_AMD64` (`0x8664`) so our
/// outer Universal Stub starts natively on Intel/AMD `x64` AND via Prism on Windows 11 `ARM64`!
inline bool IsX64PortableExecutable(const std::filesystem::path& exePath) noexcept {
    std::ifstream in(exePath, std::ios::binary);
    if (!in) return false;
    IMAGE_DOS_HEADER dos{};
    if (!in.read(reinterpret_cast<char*>(&dos), sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }
    if (dos.e_lfanew <= 0 || dos.e_lfanew > 16 * 1024 * 1024) return false;
    in.seekg(dos.e_lfanew, std::ios::beg);
    DWORD peSig = 0;
    IMAGE_FILE_HEADER fh{};
    if (!in.read(reinterpret_cast<char*>(&peSig), sizeof(peSig)) || peSig != IMAGE_NT_SIGNATURE) {
        return false;
    }
    if (!in.read(reinterpret_cast<char*>(&fh), sizeof(fh))) {
        return false;
    }
    return fh.Machine == IMAGE_FILE_MACHINE_AMD64;
}

/// Locates the `SetupWizard.exe` native bootstrapper template matching `arch` (`"x64"` or `"arm64"`).
inline std::filesystem::path LocateSetupBootstrapperTemplate(
    const std::filesystem::path& compilerExeDir,
    std::string_view arch)
{
    std::error_code ec;
    const std::vector<std::filesystem::path> stubCandidates = {
        compilerExeDir.parent_path() / Utf8ToWide(arch) / L"SetupWizard.exe",
        compilerExeDir / Utf8ToWide(arch) / L"SetupWizard.exe",
        compilerExeDir / L"SetupWizard.exe"
    };
    for (const auto& c : stubCandidates) {
        if (std::filesystem::exists(c, ec)) {
            return c;
        }
    }
    return {};
}

/// Locates the `x64` (`IMAGE_FILE_MACHINE_AMD64`) `SetupWizard.exe` template used as the outer
/// universal dispatcher binary for both `*-offline-setup.exe` and `*-setup.exe`.
inline std::filesystem::path LocateUniversalOuterX64Stub(const std::filesystem::path& compilerExeDir) {
    std::error_code ec;
    const std::vector<std::filesystem::path> candidates = {
        compilerExeDir.parent_path() / L"x64" / L"SetupWizard.exe",
        compilerExeDir / L"x64" / L"SetupWizard.exe",
        compilerExeDir / L"SetupWizard.exe"
    };
    for (const auto& c : candidates) {
        if (std::filesystem::exists(c, ec) && IsX64PortableExecutable(c)) {
            return c;
        }
    }
    return {};
}

/// Derives a clean architecture-neutral filename stem (e.g., `raggiesoft-installer-studio`)
/// from `[Package] UniversalOutputName`, `OutputName`, or `Name`.
inline std::string DeriveUniversalOutputStem(const RswManifestDocument& doc) {
    std::string explicitStem = TrimAscii(doc.GetString("Package", "", "UniversalOutputName"));
    if (!explicitStem.empty()) return explicitStem;

    std::string rawOut = TrimAscii(doc.GetString("Package", "", "OutputName"));
    auto stripToken = [](std::string& s, std::string_view tok) {
        for (;;) {
            const auto pos = AsciiLower(s).find(AsciiLower(tok));
            if (pos == std::string::npos) break;
            s.erase(pos, tok.size());
        }
    };
    if (!rawOut.empty()) {
        stripToken(rawOut, "-$(ARCH)");
        stripToken(rawOut, "_$(ARCH)");
        stripToken(rawOut, "$(ARCH)");
        for (std::string_view suf : {"-x64", "-arm64", "-amd64", "_x64", "_arm64", "_amd64"}) {
            if (rawOut.size() > suf.size() &&
                EqualsIgnoreCase(std::string_view(rawOut).substr(rawOut.size() - suf.size()), suf)) {
                rawOut.resize(rawOut.size() - suf.size());
                break;
            }
        }
        while (!rawOut.empty() && (rawOut.back() == '-' || rawOut.back() == '_')) rawOut.pop_back();
        if (!rawOut.empty()) return rawOut;
    }

    const std::string prodName = doc.GetString("Package", "", "Name", "app");
    std::string kebab;
    for (char c : prodName) {
        if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) kebab.push_back(c);
        else if (c >= 'A' && c <= 'Z') kebab.push_back(static_cast<char>(c - 'A' + 'a'));
        else if (!kebab.empty() && kebab.back() != '-') kebab.push_back('-');
    }
    while (!kebab.empty() && kebab.back() == '-') kebab.pop_back();
    return kebab.empty() ? "application" : kebab;
}

/// Stamps optional `[UI] IconFile` (`RT_GROUP_ICON` #101) and publisher `VS_VERSION_INFO` (`RT_VERSION` #1)
/// onto an open `BeginUpdateResourceW` handle (`hRes`).
inline bool StampIconAndVersionOnExeResource(
    HANDLE hRes,
    const std::filesystem::path& iconFilePath,
    std::string_view companyName,
    std::string_view fileDescription,
    std::string_view productVersion,
    std::string_view internalName,
    std::string_view originalFileName,
    std::string_view productName,
    std::string& outError)
{
    constexpr WORD kLangEnUs = MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US);
    std::error_code ec;
    bool ok = true;

    if (!iconFilePath.empty() && std::filesystem::exists(iconFilePath, ec)) {
        std::vector<std::uint8_t> icoBytes;
        if (ReadBinaryFileBytes(iconFilePath, icoBytes, outError) &&
            icoBytes.size() >= sizeof(DiskIconDirHeader)) {
            DiskIconDirHeader hdr{};
            std::memcpy(&hdr, icoBytes.data(), sizeof(hdr));
            const std::size_t entriesBytes = static_cast<std::size_t>(hdr.idCount) * sizeof(DiskIconDirEntry);
            if (hdr.idReserved == 0 && hdr.idType == 1 && hdr.idCount > 0 &&
                icoBytes.size() >= sizeof(hdr) + entriesBytes) {
                std::vector<std::uint8_t> grpBuf(sizeof(DiskIconDirHeader) + hdr.idCount * sizeof(PeGrpIconDirEntry));
                std::memcpy(grpBuf.data(), &hdr, sizeof(hdr));

                bool framesValid = true;
                for (std::uint16_t i = 0; i < hdr.idCount; ++i) {
                    DiskIconDirEntry de{};
                    std::memcpy(&de, icoBytes.data() + sizeof(hdr) + i * sizeof(de), sizeof(de));
                    if (static_cast<std::size_t>(de.dwImageOffset) + de.dwBytesInRes > icoBytes.size()) {
                        framesValid = false;
                        break;
                    }
                    const std::uint16_t iconId = static_cast<std::uint16_t>(i + 1u);
                    ok = ok && ::UpdateResourceW(
                        hRes, RT_ICON, MAKEINTRESOURCEW(iconId), kLangEnUs,
                        icoBytes.data() + de.dwImageOffset, de.dwBytesInRes);

                    PeGrpIconDirEntry ge{};
                    ge.bWidth       = de.bWidth;
                    ge.bHeight      = de.bHeight;
                    ge.bColorCount  = de.bColorCount;
                    ge.bReserved    = de.bReserved;
                    ge.wPlanes      = de.wPlanes;
                    ge.wBitCount    = de.wBitCount;
                    ge.dwBytesInRes = de.dwBytesInRes;
                    ge.nID          = iconId;
                    std::memcpy(grpBuf.data() + sizeof(hdr) + i * sizeof(ge), &ge, sizeof(ge));
                }
                if (framesValid) {
                    ok = ok && ::UpdateResourceW(
                        hRes, RT_GROUP_ICON, MAKEINTRESOURCEW(101), kLangEnUs,
                        grpBuf.data(), static_cast<DWORD>(grpBuf.size()));
                }
            }
        }
    }

    const std::string copyright = "Copyright (c) 2026 " + std::string(companyName);
    auto verBytes = VersionResourceBuilder::Build(
        companyName, fileDescription, productVersion, internalName, copyright, originalFileName, productName);
    ok = ok && ::UpdateResourceW(
        hRes, RT_VERSION, MAKEINTRESOURCEW(1), kLangEnUs,
        verBytes.data(), static_cast<DWORD>(verBytes.size()));
    return ok;
}

/// Summary of combined Universal Single-File outputs emitted by `PackageUniversalInstallers(...)`.
struct UniversalPackageOutputs {
    std::filesystem::path offlineSetupExePath; // Single portable offline EXE bundling BOTH `x64` & `ARM64` installers!
    std::filesystem::path webStubExePath;      // Compact CPU-aware smart web stub downloading `x64` or `ARM64` on demand!
    std::string           sha256X64;
    std::string           sha256Arm64;
    std::uint64_t         sizeX64   = 0;
    std::uint64_t         sizeArm64 = 0;
};

/// Emits `<univStem>-offline-setup.exe` (both `x64` and `ARM64` native installers compressed inside one
/// self-contained portable `.exe`) AND, when `UniversalDownloadBaseUrl` is configured, `<univStem>-setup.exe`
/// (a ~400 KB CPU-aware WinHTTP web stub that pierces Prism via `IsWow64Process2`, downloads ONLY the
/// host processor's native installer with SHA-256 verification, and launches it natively!).
inline bool PackageUniversalInstallers(
    const RswManifestDocument& doc,
    const CompiledMsiSummary& x64Summary,
    const std::filesystem::path& x64SetupExePath,
    const CompiledMsiSummary& arm64Summary,
    const std::filesystem::path& arm64SetupExePath,
    const std::filesystem::path& compilerExeDir,
    UniversalPackageOutputs& outUniversal,
    std::string& outError)
{
    outUniversal = {};
    std::error_code ec;

    if (x64SetupExePath.empty() || !std::filesystem::exists(x64SetupExePath, ec)) {
        outError = "Universal packaging requires a compiled x64 Setup.exe payload.";
        return false;
    }
    if (arm64SetupExePath.empty() || !std::filesystem::exists(arm64SetupExePath, ec)) {
        outError = "Universal packaging requires a compiled ARM64 Setup.exe payload.";
        return false;
    }

    const std::filesystem::path outerX64Stub = LocateUniversalOuterX64Stub(compilerExeDir);
    if (outerX64Stub.empty()) {
        outError = "Could not locate x64 'SetupWizard.exe' (IMAGE_FILE_MACHINE_AMD64) required for Universal Launcher packaging.";
        return false;
    }

    std::vector<std::uint8_t> rawX64Bytes, rawArm64Bytes;
    if (!ReadBinaryFileBytes(x64SetupExePath, rawX64Bytes, outError) ||
        !ReadBinaryFileBytes(arm64SetupExePath, rawArm64Bytes, outError)) {
        return false;
    }

    outUniversal.sizeX64   = static_cast<std::uint64_t>(rawX64Bytes.size());
    outUniversal.sizeArm64 = static_cast<std::uint64_t>(rawArm64Bytes.size());
    if (!ComputeFileSha256Hex(x64SetupExePath, outUniversal.sha256X64) ||
        !ComputeFileSha256Hex(arm64SetupExePath, outUniversal.sha256Arm64)) {
        outError = "Failed computing SHA-256 digests for x64 / ARM64 installers.";
        return false;
    }

    const std::string univStem      = DeriveUniversalOutputStem(doc);
    const std::filesystem::path dir = x64SetupExePath.parent_path();
    const std::string x64FileName   = WideToUtf8(x64SetupExePath.filename().wstring());
    const std::string arm64FileName = WideToUtf8(arm64SetupExePath.filename().wstring());

    auto sanitizeLine = [](std::string s) {
        std::string out;
        out.reserve(s.size());
        for (char c : s) {
            if (c == '\r') continue;
            if (c == '\n') out += "\\n";
            else out += c;
        }
        return out;
    };

    const std::string requestedExecLvl = std::string(
        NormalizeRequestedExecutionLevel(doc.GetString("Package", "", "RequestedExecutionLevel", "asInvoker")));

    auto buildUniversalIni = [&](std::string_view mode, std::string_view urlX64, std::string_view urlArm64) {
        std::ostringstream ini;
        ini << "[Universal]\n"
            << "Mode="               << mode << "\n"
            << "ProductName="        << sanitizeLine(x64Summary.productName) << "\n"
            << "Tagline="            << sanitizeLine(ExpandArchToken(doc.GetString("Package", "", "Tagline", x64Summary.manufacturer + " Setup"), "universal")) << "\n"
            << "WebsiteLabel="       << sanitizeLine(doc.GetString("Package", "", "WebsiteLabel", "")) << "\n"
            << "Version="            << sanitizeLine(x64Summary.productVersion) << "\n"
            << "Manufacturer="       << sanitizeLine(x64Summary.manufacturer) << "\n"
            << "UpgradeCode="        << sanitizeLine(x64Summary.upgradeCode) << "\n"
            << "RequestedExecutionLevel=" << requestedExecLvl << "\n"
            << "MinWindowsBuild="    << x64Summary.minWindowsBuild << "\n"
            << "OsLaunchMessage="    << sanitizeLine(x64Summary.osLaunchMessage) << "\n"
            << "AccentPrimaryHex="   << sanitizeLine(doc.GetString("UI", "", "AccentPrimaryHex", "#2563EB")) << "\n"
            << "AccentSecondaryHex=" << sanitizeLine(doc.GetString("UI", "", "AccentSecondaryHex", "#FBBF24")) << "\n"
            << "X64FileName="        << sanitizeLine(x64FileName) << "\n"
            << "Arm64FileName="      << sanitizeLine(arm64FileName) << "\n"
            << "Sha256X64="          << outUniversal.sha256X64 << "\n"
            << "Sha256Arm64="        << outUniversal.sha256Arm64 << "\n"
            << "SizeX64="            << outUniversal.sizeX64 << "\n"
            << "SizeArm64="          << outUniversal.sizeArm64 << "\n"
            << "UrlX64="             << sanitizeLine(std::string(urlX64)) << "\n"
            << "UrlArm64="           << sanitizeLine(std::string(urlArm64)) << "\n";
        return ini.str();
    };

    constexpr WORD kLangNeutral = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);

    // 1. Emit Single-File Offline Universal Installer (`<univStem>-offline-setup.exe`: x64 + ARM64 combined!)
    {
        const auto offlinePath = dir / Utf8ToWide(univStem + "-offline-setup.exe");
        std::filesystem::remove(offlinePath, ec);
        if (!std::filesystem::copy_file(outerX64Stub, offlinePath, std::filesystem::copy_options::overwrite_existing, ec)) {
            outError = "Failed copying universal x64 stub to '" + WideToUtf8(offlinePath.wstring()) + "'.";
            return false;
        }
        StripExistingPeCertificates(offlinePath);
        const auto offlineManifestLangs     = QueryExistingPeManifestLangIds(offlinePath);

        std::vector<std::uint8_t> compX64   = CompressWithLzmsIfSmaller(rawX64Bytes);
        std::vector<std::uint8_t> compArm64 = CompressWithLzmsIfSmaller(rawArm64Bytes);
        const std::string offlineIni        = buildUniversalIni("offline", "", "");

        HANDLE hRes = ::BeginUpdateResourceW(offlinePath.wstring().c_str(), FALSE);
        if (!hRes) {
            outError = "BeginUpdateResourceW failed on offline universal EXE.";
            return false;
        }
        bool ok = true;
        ok = ok && ::UpdateResourceW(
            hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdUniversalManifest), kLangNeutral,
            const_cast<char*>(offlineIni.data()), static_cast<DWORD>(offlineIni.size()));
        ok = ok && ::UpdateResourceW(
            hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdUniversalSetupX64), kLangNeutral,
            compX64.data(), static_cast<DWORD>(compX64.size()));
        ok = ok && ::UpdateResourceW(
            hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdUniversalSetupArm64), kLangNeutral,
            compArm64.data(), static_cast<DWORD>(compArm64.size()));

        const std::string origName = WideToUtf8(offlinePath.filename().wstring());
        const std::string desc     = x64Summary.productName + " Offline Universal Setup (x64 & ARM64)";
        ok = ok && StampIconAndVersionOnExeResource(
            hRes, x64Summary.iconFilePath, x64Summary.manufacturer, desc,
            x64Summary.productVersion, univStem + "-offline", origName, x64Summary.productName, outError);
        ok = ok && StampExecutionLevelManifestOnResource(hRes, offlineManifestLangs, requestedExecLvl);

        if (!::EndUpdateResourceW(hRes, !ok) || !ok) {
            outError = "EndUpdateResourceW failed while stamping '" + origName + "'.";
            return false;
        }
        if (!RunAuthenticodeSignTool(doc, offlinePath, outError)) {
            return false;
        }
        outUniversal.offlineSetupExePath = offlinePath;
    }

    // 2. Emit Smart CPU-Aware Universal Web Stub (`<univStem>-setup.exe`) when `UniversalDownloadBaseUrl` is set!
    std::string baseUrl = TrimAscii(doc.GetString("Package", "", "UniversalDownloadBaseUrl"));
    while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();
    if (!baseUrl.empty()) {
        const auto webStubPath = dir / Utf8ToWide(univStem + "-setup.exe");
        // Safety guard: never overwrite one of the per-arch child EXEs if `OutputName` lacked `$(ARCH)`!
        if (webStubPath != x64SetupExePath && webStubPath != arm64SetupExePath) {
            std::filesystem::remove(webStubPath, ec);
            if (!std::filesystem::copy_file(outerX64Stub, webStubPath, std::filesystem::copy_options::overwrite_existing, ec)) {
                outError = "Failed copying universal x64 stub to '" + WideToUtf8(webStubPath.wstring()) + "'.";
                return false;
            }
            StripExistingPeCertificates(webStubPath);
            const auto webManifestLangs = QueryExistingPeManifestLangIds(webStubPath);

            const std::string urlX64   = baseUrl + "/" + x64FileName;
            const std::string urlArm64 = baseUrl + "/" + arm64FileName;
            const std::string webIni   = buildUniversalIni("web", urlX64, urlArm64);

            HANDLE hRes = ::BeginUpdateResourceW(webStubPath.wstring().c_str(), FALSE);
            if (!hRes) {
                outError = "BeginUpdateResourceW failed on universal web stub EXE.";
                return false;
            }
            bool ok = ::UpdateResourceW(
                hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdUniversalManifest), kLangNeutral,
                const_cast<char*>(webIni.data()), static_cast<DWORD>(webIni.size()));

            const std::string origName = WideToUtf8(webStubPath.filename().wstring());
            const std::string desc     = x64Summary.productName + " Setup (Auto-Detects x64 & ARM64)";
            ok = ok && StampIconAndVersionOnExeResource(
                hRes, x64Summary.iconFilePath, x64Summary.manufacturer, desc,
                x64Summary.productVersion, univStem + "-setup", origName, x64Summary.productName, outError);
            ok = ok && StampExecutionLevelManifestOnResource(hRes, webManifestLangs, requestedExecLvl);

            if (!::EndUpdateResourceW(hRes, !ok) || !ok) {
                outError = "EndUpdateResourceW failed while stamping '" + origName + "'.";
                return false;
            }
            if (!RunAuthenticodeSignTool(doc, webStubPath, outError)) {
                return false;
            }
            outUniversal.webStubExePath = webStubPath;
        }
    }

    (void)arm64Summary;
    return true;
}

/// Compiles either a lightweight Self-Extracting Archive (`PackageKind = sfx` -> `<OutputName>.exe`)
/// OR a zero-`msi.dll` Direct Native Installer (`Engine = native` -> `<OutputName>-setup.exe`),
/// supporting folder + `.zip` `[Payload.*]` inputs, `.rswpak` on-demand feature packs, selectable
/// `RequestedExecutionLevel` (`asInvoker | highestAvailable | requireAdministrator`), custom `.ico`,
/// publisher-owned `VERSIONINFO`, and Authenticode signing!
inline bool PackageNativeOrSfxExecutable(
    const RswManifestDocument& doc,
    std::string_view cliArchOverride,
    const std::filesystem::path& compilerExeDir,
    CompiledMsiSummary& outSummary,
    std::filesystem::path& outExePath,
    std::string& outError)
{
    const ValidationReport val = ValidateManifestDocument(doc, cliArchOverride);
    if (!val.IsOk()) {
        outError = "Manifest validation failed (" + std::to_string(val.ErrorCount()) +
                   " error(s)): " + val.diagnostics.front().message;
        return false;
    }

    const std::string arch       = val.resolvedArchitecture;
    const std::string pkgKindLow = AsciiLower(TrimAscii(doc.GetString("Package", "", "PackageKind", "msi")));
    const bool isSfx             = (pkgKindLow == "sfx");

    const std::filesystem::path baseDir = doc.sourcePath.empty()
        ? std::filesystem::current_path()
        : doc.sourcePath.parent_path();
    auto resolvePath = [&](std::string_view raw) -> std::filesystem::path {
        if (raw.empty()) return {};
        std::string exp = ExpandArchToken(raw, arch);
        std::filesystem::path p(Utf8ToWide(exp));
        return (p.is_absolute() ? p : (baseDir / p)).lexically_normal();
    };

    outSummary = CompiledMsiSummary{};
    outSummary.architecture        = arch;
    outSummary.productName         = ExpandArchToken(doc.GetString("Package", "", "Name"), arch);
    outSummary.productVersion      = ExpandArchToken(doc.GetString("Package", "", "Version"), arch);
    outSummary.manufacturer        = ExpandArchToken(doc.GetString("Package", "", "Manufacturer"), arch);
    outSummary.perMachine          = !isSfx && EqualsIgnoreCase(doc.GetString("Package", "", "Scope", "machine"), "machine");
    outSummary.minWindowsBuild     = doc.GetInt("Requirements", "", "MinWindowsBuild", 22000);
    outSummary.osLaunchMessage     = doc.GetString("Requirements", "", "OsLaunchMessage", "This application requires Windows 11 (Build 22000) or later.");
    const std::string rawDefaultSub = isSfx
        ? doc.GetString("SFX", "", "DefaultExtractSubdir", doc.GetString("Install", "", "DefaultSubdir"))
        : doc.GetString("Install", "", "DefaultSubdir");
    outSummary.defaultSubdir       = ExpandArchToken(rawDefaultSub, arch);
    outSummary.mainExeRel          = ExpandArchToken(doc.GetString("Install", "", "MainExecutable"), arch);
    outSummary.installBtnText      = doc.GetString("UI", "", "InstallButtonText", isSfx ? "&Extract" : "&Install");
    outSummary.launchChkText       = doc.GetString("UI", "", "FinishLaunchCheckbox", "&Launch application when finished");
    outSummary.offerLaunchOnFinish = doc.GetBool("UI", "", "OfferLaunchOnFinish", true);
    outSummary.iconFilePath        = resolvePath(doc.GetString("UI", "", "IconFile"));

    const std::string rawUpg = doc.GetString("Package", "", "UpgradeCode");
    if (!rawUpg.empty()) {
        NormalizeMsiGuid(rawUpg, outSummary.upgradeCode);
    } else {
        outSummary.upgradeCode = ComputeDeterministicComponentGuid(
            "{A18F4E20-7C93-4D62-9B44-524147474945}", arch,
            "sfx:" + outSummary.manufacturer + ":" + outSummary.productName);
    }
    outSummary.productCode = ComputeDeterministicComponentGuid(
        outSummary.upgradeCode, arch, "productcode:" + outSummary.productVersion);

    const std::string rawOutName = ExpandArchToken(doc.GetString("Package", "", "OutputName"), arch);
    const std::string outStem    = rawOutName.empty()
        ? (SlugifyName(outSummary.productName) + "-" + arch)
        : rawOutName;
    const std::filesystem::path outDir = resolvePath(doc.GetString("Package", "", "OutputDir", "out"));
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    outExePath = outDir / Utf8ToWide(isSfx ? (outStem + ".exe") : (outStem + "-setup.exe"));

    const std::filesystem::path stubTemplate = LocateSetupBootstrapperTemplate(compilerExeDir, arch);
    if (stubTemplate.empty()) {
        outError = "Could not locate SetupWizard.exe template binary for '" + arch + "'.";
        return false;
    }

    // Collect on-demand feature IDs (`[Feature.<Id>] Delivery = ondemand`)
    std::set<std::string> onDemandFeatLower;
    for (const auto& fid : doc.GetNamedSectionIds("Feature")) {
        if (!isSfx && EqualsIgnoreCase(doc.GetString("Feature", fid, "Delivery", "embedded"), "ondemand")) {
            onDemandFeatLower.insert(AsciiLower(fid));
        }
    }

    // Harvest `[Payload.*]` files, folders, and `.zip` archives into `RSWPAK1`!
    const auto scratchDir = std::filesystem::temp_directory_path(ec) /
                            (L"rsw-natpkg-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(scratchDir, ec);

    std::vector<RswPakInputItem> embeddedItems;
    std::vector<RswPakInputItem> onDemandItems;
    std::size_t zipCounter = 0;

    for (const auto& pid : doc.GetNamedSectionIds("Payload")) {
        const auto absSrc       = resolvePath(doc.GetString("Payload", pid, "Source"));
        const std::string tRoot = doc.GetString("Payload", pid, "TargetRoot", "INSTALLDIR");
        const std::string sub   = ExpandArchToken(doc.GetString("Payload", pid, "TargetSubdir"), arch);
        const bool recursive    = doc.GetBool("Payload", pid, "Recursive", true);
        const std::string feat  = doc.GetString("Payload", pid, "Feature", "Main");
        const bool isOnDemand   = (onDemandFeatLower.count(AsciiLower(feat)) > 0);

        auto pushFile = [&](const std::filesystem::path& srcFile, const std::string& relDir) {
            const std::string fn = WideToUtf8(srcFile.filename().wstring());
            std::string relPath  = relDir.empty() ? fn : (relDir + "/" + fn);
            std::string archPath = isSfx ? relPath : (tRoot + "/" + relPath);
            if (isOnDemand) onDemandItems.push_back({srcFile, archPath, feat});
            else embeddedItems.push_back({srcFile, archPath, feat});
        };

        auto harvestDir = [&](const std::filesystem::path& rootDir) {
            std::vector<std::filesystem::path> found;
            if (recursive) {
                for (const auto& de : std::filesystem::recursive_directory_iterator(rootDir, ec)) {
                    if (de.is_regular_file(ec)) found.push_back(de.path());
                }
            } else {
                for (const auto& de : std::filesystem::directory_iterator(rootDir, ec)) {
                    if (de.is_regular_file(ec)) found.push_back(de.path());
                }
            }
            std::wstring basePfx = rootDir.lexically_normal().generic_wstring();
            while (!basePfx.empty() && basePfx.back() == L'/') basePfx.pop_back();
            std::sort(found.begin(), found.end());
            for (const auto& fp : found) {
                std::wstring parGen = fp.parent_path().lexically_normal().generic_wstring();
                while (!parGen.empty() && parGen.back() == L'/') parGen.pop_back();
                std::string relPar;
                if (parGen.size() > basePfx.size() &&
                    _wcsnicmp(parGen.c_str(), basePfx.c_str(), basePfx.size()) == 0 &&
                    parGen[basePfx.size()] == L'/') {
                    relPar = WideToUtf8(parGen.substr(basePfx.size() + 1));
                }
                std::string combined = sub.empty() ? relPar : (relPar.empty() ? sub : (sub + "/" + relPar));
                pushFile(fp, combined);
            }
        };

        if (std::filesystem::is_regular_file(absSrc, ec) && IsZipArchivePath(absSrc)) {
            const auto zDir = scratchDir / ("zip-" + std::to_string(++zipCounter));
            std::vector<std::filesystem::path> zFiles;
            if (!ExtractZipArchiveToDirectory(absSrc, zDir, zFiles, outError)) {
                std::filesystem::remove_all(scratchDir, ec);
                return false;
            }
            harvestDir(zDir);
        } else if (std::filesystem::is_regular_file(absSrc, ec)) {
            pushFile(absSrc, sub);
        } else if (std::filesystem::is_directory(absSrc, ec)) {
            harvestDir(absSrc);
        }
    }

    if (embeddedItems.empty() && onDemandItems.empty()) {
        std::filesystem::remove_all(scratchDir, ec);
        outError = "No payload files were matched by [Payload.*] for " + std::string(isSfx ? "SFX" : "Native") + " packaging.";
        return false;
    }

    std::vector<std::uint8_t> pakBytes;
    std::uint64_t uncompTotal = 0;
    if (!BuildRswPakBytes(embeddedItems, pakBytes, uncompTotal, outError)) {
        std::filesystem::remove_all(scratchDir, ec);
        return false;
    }
    outSummary.fileCount = embeddedItems.size() + onDemandItems.size();

    // Emit any external `.rswpak` feature packs (`[Feature.<Id>] Delivery = ondemand`)
    std::string univDlBase = TrimAscii(doc.GetString("Package", "", "UniversalDownloadBaseUrl"));
    while (!univDlBase.empty() && univDlBase.back() == '/') univDlBase.pop_back();
    for (const auto& fid : doc.GetNamedSectionIds("Feature")) {
        if (onDemandFeatLower.count(AsciiLower(fid)) == 0) continue;
        std::vector<RswPakInputItem> fItems;
        for (const auto& it : onDemandItems) {
            if (EqualsIgnoreCase(it.featureId, fid)) fItems.push_back(it);
        }
        if (fItems.empty()) continue;
        const std::string pakName = outStem + "-" + SlugifyName(fid) + ".rswpak";
        const auto pakPath = outDir / Utf8ToWide(pakName);
        std::uint64_t fBytes = 0;
        std::string fSha;
        if (!WriteRswPakFile(fItems, pakPath, fBytes, fSha, outError)) {
            std::filesystem::remove_all(scratchDir, ec);
            return false;
        }
        std::string url = ExpandArchToken(doc.GetString("Feature", fid, "PackUrl"), arch);
        if (url.empty() && !univDlBase.empty()) url = univDlBase + "/" + pakName;
        outSummary.featurePacks.push_back({fid, pakName, url, fSha, fBytes});
    }

    // Build `NativeOrSfxManifest` (`#221`)
    const std::string requestedExecLvl = std::string(
        NormalizeRequestedExecutionLevel(doc.GetString("Package", "", "RequestedExecutionLevel", "asInvoker")));

    NativeOrSfxManifest nm{};
    nm.mode                  = isSfx ? "sfx" : "native";
    nm.productName           = outSummary.productName;
    nm.tagline               = ExpandArchToken(doc.GetString("Package", "", "Tagline"), arch);
    nm.websiteLabel          = doc.GetString("Package", "", "WebsiteLabel");
    nm.version               = outSummary.productVersion;
    nm.manufacturer          = outSummary.manufacturer;
    nm.upgradeCode           = outSummary.upgradeCode;
    nm.productCode           = outSummary.productCode;
    nm.architecture          = arch;
    nm.perMachine            = outSummary.perMachine;
    nm.requestedExecLevel    = requestedExecLvl;
    nm.hideFromInstalledApps = doc.GetBool("Package", "", "HideFromInstalledApps", false);
    nm.aboutUrl              = doc.GetString("Package", "", "AboutUrl");
    nm.minWindowsBuild       = outSummary.minWindowsBuild;
    nm.osLaunchMessage       = outSummary.osLaunchMessage;
    nm.silentRebootCode      = doc.GetInt("Requirements", "", "SilentRebootExitCode", 3010);
    nm.defaultSubdir         = outSummary.defaultSubdir;
    nm.startMenuFolder       = ExpandArchToken(doc.GetString("Install", "", "StartMenuFolder", outSummary.manufacturer), arch);
    nm.mainExecutable        = outSummary.mainExeRel;
    nm.wizardLayout          = doc.GetString("UI", "", "WizardLayout", "simple");
    nm.accentPrimaryHex      = doc.GetString("UI", "", "AccentPrimaryHex", "#2563EB");
    nm.accentSecondaryHex    = doc.GetString("UI", "", "AccentSecondaryHex", "#FBBF24");
    nm.installBtnText        = outSummary.installBtnText;
    nm.finishLaunchText      = outSummary.launchChkText;
    nm.offerLaunchOnFinish   = outSummary.offerLaunchOnFinish;
    nm.uncompressedBytes     = uncompTotal;

    nm.postExtractRun    = ExpandArchToken(doc.GetString("SFX", "", "PostExtractRun"), arch);
    nm.postExtractArgs   = ExpandArchToken(doc.GetString("SFX", "", "PostExtractArgs"), arch);
    nm.postExtractOpen   = ExpandArchToken(doc.GetString("SFX", "", "PostExtractOpen"), arch);
    nm.extractToTemp     = doc.GetBool("SFX", "", "ExtractToTemp", false);
    nm.allowChangeFolder = doc.GetBool("SFX", "", "AllowChangeFolder", true);
    nm.silentOverwrite   = doc.GetBool("SFX", "", "SilentOverwrite", true);

    if (!isSfx) {
        for (const auto& flId : doc.GetNamedSectionIds("Folder")) {
            nm.folders.push_back({
                doc.GetString("Folder", flId, "TargetRoot", "INSTALLDIR"),
                ExpandArchToken(doc.GetString("Folder", flId, "TargetSubdir"), arch),
                doc.GetString("Folder", flId, "Feature", "Main"),
                doc.GetBool("Folder", flId, "RemoveOnUninstall", true)
            });
        }
        for (const auto& sid : doc.GetNamedSectionIds("Shortcut")) {
            nm.shortcuts.push_back({
                sid,
                ExpandArchToken(doc.GetString("Shortcut", sid, "Name"), arch),
                ExpandArchToken(doc.GetString("Shortcut", sid, "TargetFile"), arch),
                doc.GetString("Shortcut", sid, "Location", "StartMenu"),
                ExpandArchToken(doc.GetString("Shortcut", sid, "Arguments"), arch),
                ExpandArchToken(doc.GetString("Shortcut", sid, "Description"), arch),
                doc.GetString("Shortcut", sid, "Feature", "Main")
            });
        }
        for (const auto& rid : doc.GetNamedSectionIds("Registry")) {
            nm.registries.push_back({
                doc.GetString("Registry", rid, "Root", outSummary.perMachine ? "HKLM" : "HKCU"),
                ExpandArchToken(doc.GetString("Registry", rid, "Key"), arch),
                ExpandArchToken(doc.GetString("Registry", rid, "Name"), arch),
                ExpandArchToken(doc.GetString("Registry", rid, "Value"), arch),
                doc.GetString("Registry", rid, "Type", "string"),
                doc.GetString("Registry", rid, "Feature", "Main")
            });
        }
        for (const auto& aid : doc.GetNamedSectionIds("FileAssociation")) {
            nm.fileAssocs.push_back({
                ExpandArchToken(doc.GetString("FileAssociation", aid, "Extension"), arch),
                ExpandArchToken(doc.GetString("FileAssociation", aid, "ProgId"), arch),
                ExpandArchToken(doc.GetString("FileAssociation", aid, "Description"), arch),
                ExpandArchToken(doc.GetString("FileAssociation", aid, "TargetFile"), arch),
                ExpandArchToken(doc.GetString("FileAssociation", aid, "Verbs", "open:&Open:\"%1\""), arch),
                doc.GetString("FileAssociation", aid, "ContentType"),
                doc.GetString("FileAssociation", aid, "PerceivedType"),
                doc.GetInt("FileAssociation", aid, "IconIndex", 0),
                doc.GetString("FileAssociation", aid, "Feature", "Main")
            });
        }
    }

    const std::string nmText = SerializeNativeOrSfxManifestIni(nm);
    const std::filesystem::path stageExe = scratchDir / L"stage-out.exe";
    if (!std::filesystem::copy_file(stubTemplate, stageExe, std::filesystem::copy_options::overwrite_existing, ec)) {
        std::filesystem::remove_all(scratchDir, ec);
        outError = "Failed copying SetupWizard.exe stub to scratch staging.";
        return false;
    }
    StripExistingPeCertificates(stageExe);
    const auto existingLangs = QueryExistingPeManifestLangIds(stageExe);

    HANDLE hRes = ::BeginUpdateResourceW(stageExe.wstring().c_str(), FALSE);
    if (!hRes) {
        std::filesystem::remove_all(scratchDir, ec);
        outError = "BeginUpdateResourceW failed while building " + std::string(isSfx ? "SFX" : "Native") + " EXE.";
        return false;
    }

    constexpr WORD kLangNeutral = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    bool ok = true;
    ok = ok && ::UpdateResourceW(
        hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdSfxOrNativeArchive), kLangNeutral,
        pakBytes.data(), static_cast<DWORD>(pakBytes.size()));
    ok = ok && ::UpdateResourceW(
        hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdNativeManifest), kLangNeutral,
        const_cast<char*>(nmText.data()), static_cast<DWORD>(nmText.size()));

    std::string fpStr;
    if (!outSummary.featurePacks.empty()) {
        fpStr = SerializeFeaturePackManifestIni(outSummary.featurePacks);
        ok = ok && ::UpdateResourceW(
            hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdFeaturePackManifest), kLangNeutral,
            const_cast<char*>(fpStr.data()), static_cast<DWORD>(fpStr.size()));
    }

    const std::string origFileName = WideToUtf8(outExePath.filename().wstring());
    const std::string fileDesc     = doc.GetString("Package", "", "Description",
                                       outSummary.productName + (isSfx ? " Self-Extracting Archive" : " Setup"));
    ok = ok && StampIconAndVersionOnExeResource(
        hRes, outSummary.iconFilePath, outSummary.manufacturer, fileDesc,
        outSummary.productVersion, outStem, origFileName, outSummary.productName, outError);
    ok = ok && StampExecutionLevelManifestOnResource(hRes, existingLangs, requestedExecLvl);

    if (!::EndUpdateResourceW(hRes, !ok) || !ok) {
        std::filesystem::remove_all(scratchDir, ec);
        outError = "EndUpdateResourceW failed while stamping '" + origFileName + "'.";
        return false;
    }

    if (!RunAuthenticodeSignTool(doc, stageExe, outError)) {
        std::filesystem::remove_all(scratchDir, ec);
        return false;
    }
    std::filesystem::remove(outExePath, ec);
    if (!std::filesystem::copy_file(stageExe, outExePath, std::filesystem::copy_options::overwrite_existing, ec)) {
        std::filesystem::remove_all(scratchDir, ec);
        outError = "Could not write output executable to '" + WideToUtf8(outExePath.wstring()) + "'.";
        return false;
    }
    std::filesystem::remove_all(scratchDir, ec);
    return true;
}

/// Compiles a Unified Suite Setup (`PackageKind = suite` -> `<OutputName>-setup.exe`) chaining
/// multiple child `.msi` and `Engine = native` packages under ONE UAC prompt and ONE Suite
/// Maintenance entry in Windows *Installed apps* (`RS_SUITE_MANAGED=1` adoption)!
inline bool PackageUnifiedSuiteExecutable(
    const RswManifestDocument& doc,
    std::string_view cliArchOverride,
    const std::filesystem::path& compilerExeDir,
    std::filesystem::path& outSuiteExePath,
    std::string& outError)
{
    const ValidationReport val = ValidateManifestDocument(doc, cliArchOverride);
    if (!val.IsOk()) {
        outError = "Suite manifest validation failed (" + std::to_string(val.ErrorCount()) +
                   " error(s)): " + val.diagnostics.front().message;
        return false;
    }

    const std::string arch = val.resolvedArchitecture;
    const std::filesystem::path baseDir = doc.sourcePath.empty()
        ? std::filesystem::current_path()
        : doc.sourcePath.parent_path();
    auto resolvePath = [&](std::string_view raw) -> std::filesystem::path {
        if (raw.empty()) return {};
        std::string exp = ExpandArchToken(raw, arch);
        std::filesystem::path p(Utf8ToWide(exp));
        return (p.is_absolute() ? p : (baseDir / p)).lexically_normal();
    };

    const std::filesystem::path stubTemplate = LocateSetupBootstrapperTemplate(compilerExeDir, arch);
    if (stubTemplate.empty()) {
        outError = "Could not locate SetupWizard.exe template binary for '" + arch + "'.";
        return false;
    }

    SuiteChainerManifest sm{};
    sm.suiteName          = ExpandArchToken(doc.GetString("Package", "", "Name"), arch);
    sm.tagline            = ExpandArchToken(doc.GetString("Package", "", "Tagline", "Unified Application Suite"), arch);
    sm.websiteLabel       = doc.GetString("Package", "", "WebsiteLabel");
    sm.version            = ExpandArchToken(doc.GetString("Package", "", "Version", "1.0.0"), arch);
    sm.manufacturer       = ExpandArchToken(doc.GetString("Package", "", "Manufacturer", "RaggieSoft"), arch);
    NormalizeMsiGuid(doc.GetString("Package", "", "UpgradeCode"), sm.upgradeCode);
    sm.architecture       = arch;
    sm.perMachine         = EqualsIgnoreCase(doc.GetString("Package", "", "Scope", "machine"), "machine");
    sm.requestedExecLevel = std::string(NormalizeRequestedExecutionLevel(
        doc.GetString("Package", "", "RequestedExecutionLevel", "asInvoker")));
    sm.minWindowsBuild    = doc.GetInt("Requirements", "", "MinWindowsBuild", 22000);
    sm.osLaunchMessage    = doc.GetString("Requirements", "", "OsLaunchMessage", "This suite requires Windows 11 (Build 22000) or later.");
    sm.defaultSubdir      = ExpandArchToken(doc.GetString("Install", "", "DefaultSubdir", sm.manufacturer + "\\" + sm.suiteName), arch);
    sm.accentPrimaryHex   = doc.GetString("UI", "", "AccentPrimaryHex", "#2563EB");
    sm.accentSecondaryHex = doc.GetString("UI", "", "AccentSecondaryHex", "#FBBF24");

    struct EmbeddedSuiteBlob {
        int                       resId = 0;
        std::vector<std::uint8_t> compBytes;
    };
    std::vector<EmbeddedSuiteBlob> childBlobs;

    const auto spIds = doc.GetNamedSectionIds("SuitePackage");
    for (std::size_t i = 0; i < spIds.size(); ++i) {
        const auto& spId   = spIds[i];
        const auto srcPath = resolvePath(doc.GetString("SuitePackage", spId, "Source"));
        std::vector<std::uint8_t> rawBytes;
        if (!ReadBinaryFileBytes(srcPath, rawBytes, outError)) {
            return false;
        }
        std::string shaHex;
        if (!ComputeFileSha256Hex(srcPath, shaHex)) {
            outError = "Failed computing SHA-256 for SuitePackage '" + spId + "'.";
            return false;
        }

        SuiteChildSpec ch{};
        ch.id                = spId;
        ch.name              = ExpandArchToken(doc.GetString("SuitePackage", spId, "Name", spId), arch);
        ch.description       = ExpandArchToken(doc.GetString("SuitePackage", spId, "Description"), arch);
        ch.fileName          = WideToUtf8(srcPath.filename().wstring());
        const std::string rawChUpg = doc.GetString("SuitePackage", spId, "UpgradeCode");
        if (!rawChUpg.empty()) NormalizeMsiGuid(rawChUpg, ch.upgradeCode);
        ch.engine            = AsciiLower(doc.GetString("SuitePackage", spId, "Engine",
                                   EqualsIgnoreCase(WideToUtf8(srcPath.extension().wstring()), ".msi") ? "msi" : "native"));
        ch.installArgs       = ExpandArchToken(doc.GetString("SuitePackage", spId, "InstallArgs"), arch);
        ch.sha256Hex         = shaHex;
        ch.uncompressedBytes = static_cast<std::uint64_t>(rawBytes.size());
        ch.embeddedResId     = kResIdSuiteChildBase + static_cast<int>(i);
        ch.required          = doc.GetBool("SuitePackage", spId, "Required", false);
        ch.selectedByDefault = doc.GetBool("SuitePackage", spId, "SelectedByDefault", true);
        ch.adoptStandalone   = doc.GetBool("SuitePackage", spId, "AdoptStandalone", true);

        sm.children.push_back(std::move(ch));
        childBlobs.push_back({kResIdSuiteChildBase + static_cast<int>(i), CompressWithLzmsIfSmaller(rawBytes)});
    }

    const std::string rawOutName = ExpandArchToken(doc.GetString("Package", "", "OutputName"), arch);
    const std::string outStem    = rawOutName.empty()
        ? (SlugifyName(sm.suiteName) + "-" + arch)
        : rawOutName;
    const std::filesystem::path outDir = resolvePath(doc.GetString("Package", "", "OutputDir", "out"));
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    outSuiteExePath = outDir / Utf8ToWide(outStem + "-setup.exe");

    const auto scratchStage = std::filesystem::temp_directory_path(ec) /
                              (L"rsw-suite-stage-" + std::to_wstring(::GetCurrentProcessId()) + L".exe");
    if (!std::filesystem::copy_file(stubTemplate, scratchStage, std::filesystem::copy_options::overwrite_existing, ec)) {
        outError = "Failed copying SetupWizard.exe stub for Suite packaging.";
        return false;
    }
    StripExistingPeCertificates(scratchStage);
    const auto existingLangs = QueryExistingPeManifestLangIds(scratchStage);

    HANDLE hRes = ::BeginUpdateResourceW(scratchStage.wstring().c_str(), FALSE);
    if (!hRes) {
        std::filesystem::remove(scratchStage, ec);
        outError = "BeginUpdateResourceW failed on Suite setup EXE.";
        return false;
    }

    constexpr WORD kLangNeutral = MAKELANGID(LANG_NEUTRAL, SUBLANG_NEUTRAL);
    const std::string smText = SerializeSuiteChainerManifestIni(sm);
    bool ok = ::UpdateResourceW(
        hRes, RT_RCDATA, MAKEINTRESOURCEW(kResIdSuiteManifest), kLangNeutral,
        const_cast<char*>(smText.data()), static_cast<DWORD>(smText.size()));
    for (const auto& cb : childBlobs) {
        ok = ok && ::UpdateResourceW(
            hRes, RT_RCDATA, MAKEINTRESOURCEW(cb.resId), kLangNeutral,
            const_cast<std::uint8_t*>(cb.compBytes.data()), static_cast<DWORD>(cb.compBytes.size()));
    }

    const auto iconPath        = resolvePath(doc.GetString("UI", "", "IconFile"));
    const std::string origFile = WideToUtf8(outSuiteExePath.filename().wstring());
    const std::string desc     = doc.GetString("Package", "", "Description", sm.suiteName + " Unified Suite Setup");
    ok = ok && StampIconAndVersionOnExeResource(
        hRes, iconPath, sm.manufacturer, desc, sm.version, outStem, origFile, sm.suiteName, outError);
    ok = ok && StampExecutionLevelManifestOnResource(hRes, existingLangs, sm.requestedExecLevel);

    if (!::EndUpdateResourceW(hRes, !ok) || !ok) {
        std::filesystem::remove(scratchStage, ec);
        outError = "EndUpdateResourceW failed while stamping Suite setup EXE.";
        return false;
    }
    if (!RunAuthenticodeSignTool(doc, scratchStage, outError)) {
        std::filesystem::remove(scratchStage, ec);
        return false;
    }
    std::filesystem::remove(outSuiteExePath, ec);
    if (!std::filesystem::copy_file(scratchStage, outSuiteExePath, std::filesystem::copy_options::overwrite_existing, ec)) {
        std::filesystem::remove(scratchStage, ec);
        outError = "Failed copying Suite setup EXE to '" + WideToUtf8(outSuiteExePath.wstring()) + "'.";
        return false;
    }
    std::filesystem::remove(scratchStage, ec);
    return true;
}

} // namespace rsw
