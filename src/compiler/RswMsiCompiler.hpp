// ================================================================================
// FILE: src/compiler/RswMsiCompiler.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   How does an `.msi` file actually work under the hood?
//     A Windows Installer (`.msi`) file is a structured **relational SQL database**
//     stored inside a Microsoft Compound File (`msi.dll`), containing normalized
//     tables (`Property`, `Directory`, `Component`, `CreateFolder`, `File`, `Font`,
//     `Feature`, `FeatureComponents`, `Shortcut`, `Registry`, `ProgId`, `Extension`,
//     `Verb`, `MIME`, `Upgrade`, `Dialog`, `Control`, `RadioButton`, `ControlEvent`,
//     `InstallExecuteSequence`, etc.) plus compressed `#payload.cab` `_Streams`!
//
//   What Microsoft Official Windows Installer Rules does `RswMsiCompiler` enforce?
//     1. **Single-Directory Component Rule**: Every resource in a Component lives
//        in one single `Directory_` row.
//     2. **ICE80 64-Bit Rule**: Every Component in an `x64` (`x64;1033`) or `ARM64`
//        (`Arm64;1033`) package carries `msidbComponentAttributes64bit` (`256`).
//     3. **ICE18 Empty Folder Rule**: Components with `KeyPath = NULL` (`folder`)
//        automatically emit a matching `(Directory_, Component_)` row in `CreateFolder`.
//     4. **ICE38 & ICE64 Per-User Profile Rule**: Components targeting `AppDataFolder`,
//        `LocalAppDataFolder`, `ProgramMenuFolder`, or `DesktopFolder` automatically
//        use an `HKCU` Registry KeyPath (`Attributes |= 4`) and emit `RemoveFile`
//        (`InstallMode = 2`) rows for clean folder removal on uninstall!
//     5. **Clean vs. Advertised File Associations (`[FileAssociation.<id>]`)**:
//        Supports both modern zero-popup `HKMU\Software\Classes` (`Root = -1`)
//        registration AND classic MSI `ProgId` / `Extension` / `Verb` / `MIME` tables!
// ================================================================================

#pragma once

#include "../common/RswCryptoAndPe.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswVersion.hpp"
#include "RswCabBuilder.hpp"
#include "RswMsiDialogBuilder.hpp"
#include "RswMsmMerger.hpp"
#include "RswValidator.hpp"
#include "RswWizard97Art.hpp"

#include <msi.h>
#include <msiquery.h>
#include <wtypes.h>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <variant>
#include <vector>

#pragma comment(lib, "msi.lib")

namespace rsw {

/// RAII wrapper around `MSIHANDLE` (`PMSIHANDLE` equivalent without ATL/MFC).
class ScopedMsiHandle {
public:
    ScopedMsiHandle() noexcept = default;
    explicit ScopedMsiHandle(MSIHANDLE h) noexcept : h_(h) {}
    ~ScopedMsiHandle() { Reset(); }

    ScopedMsiHandle(const ScopedMsiHandle&) = delete;
    ScopedMsiHandle& operator=(const ScopedMsiHandle&) = delete;

    ScopedMsiHandle(ScopedMsiHandle&& other) noexcept : h_(other.h_) { other.h_ = 0; }
    ScopedMsiHandle& operator=(ScopedMsiHandle&& other) noexcept {
        if (this != &other) {
            Reset();
            h_ = other.h_;
            other.h_ = 0;
        }
        return *this;
    }

    void Reset(MSIHANDLE newH = 0) noexcept {
        if (h_ != 0) {
            ::MsiCloseHandle(h_);
        }
        h_ = newH;
    }

    [[nodiscard]] MSIHANDLE Get() const noexcept { return h_; }
    MSIHANDLE* Put() noexcept { Reset(); return &h_; }
    explicit operator bool() const noexcept { return h_ != 0; }

private:
    MSIHANDLE h_ = 0;
};

using SqlParam = std::variant<std::monostate, int, std::string>;

struct CompiledMsiSummary {
    std::filesystem::path msiPath;
    std::string           productName;
    std::string           productVersion;
    std::string           manufacturer;
    std::string           productCode;
    std::string           upgradeCode;
    std::string           packageCode;
    std::string           architecture;   // `"x64"` or `"arm64"`
    std::string           defaultSubdir;  // e.g. `"RaggieSoft\\Hello RaggieSoft"`
    std::string           mainExeRel;     // e.g. `"HelloRaggieSoft.exe"`
    std::string           installBtnText;
    std::string           launchChkText;
    bool                  offerLaunchOnFinish = true;
    bool                  perMachine          = true;
    int                   minWindowsBuild     = 22000;
    std::string           osLaunchMessage;
    std::filesystem::path licenseRtfPath;
    std::filesystem::path iconFilePath;
    std::size_t           fileCount           = 0;
    std::size_t           componentCount      = 0;
};

namespace msi_sql {

inline bool ExecuteDdl(MSIHANDLE hDb, std::string_view sql, std::string& outError) {
    const std::wstring wSql = Utf8ToWide(sql);
    ScopedMsiHandle hView;
    UINT rc = ::MsiDatabaseOpenViewW(hDb, wSql.c_str(), hView.Put());
    if (rc != ERROR_SUCCESS) {
        outError = "MsiDatabaseOpenViewW DDL failed (" + std::to_string(rc) + "): " + std::string(sql);
        return false;
    }
    rc = ::MsiViewExecute(hView.Get(), 0);
    if (rc != ERROR_SUCCESS) {
        outError = "MsiViewExecute DDL failed (" + std::to_string(rc) + "): " + std::string(sql);
        return false;
    }
    return true;
}

inline bool InsertRow(
    MSIHANDLE hDb,
    std::string_view table,
    const std::vector<std::string_view>& cols,
    const std::vector<SqlParam>& values,
    std::string& outError)
{
    std::ostringstream sql;
    sql << "INSERT INTO `" << table << "` (";
    for (std::size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << "`" << cols[i] << "`";
    }
    sql << ") VALUES (";
    for (std::size_t i = 0; i < cols.size(); ++i) {
        if (i > 0) sql << ", ";
        sql << "?";
    }
    sql << ")";

    const std::wstring wSql = Utf8ToWide(sql.str());
    ScopedMsiHandle hView;
    UINT rc = ::MsiDatabaseOpenViewW(hDb, wSql.c_str(), hView.Put());
    if (rc != ERROR_SUCCESS) {
        outError = "OpenView INSERT failed on table '" + std::string(table) + "' (" + std::to_string(rc) + ").";
        return false;
    }

    ScopedMsiHandle hRec(::MsiCreateRecord(static_cast<UINT>(values.size())));
    for (std::size_t i = 0; i < values.size(); ++i) {
        const UINT fieldIdx = static_cast<UINT>(i + 1);
        const auto& v = values[i];
        if (std::holds_alternative<std::monostate>(v)) {
            ::MsiRecordSetInteger(hRec.Get(), fieldIdx, MSI_NULL_INTEGER);
        } else if (std::holds_alternative<int>(v)) {
            ::MsiRecordSetInteger(hRec.Get(), fieldIdx, std::get<int>(v));
        } else {
            const std::wstring ws = Utf8ToWide(std::get<std::string>(v));
            ::MsiRecordSetStringW(hRec.Get(), fieldIdx, ws.c_str());
        }
    }

    rc = ::MsiViewExecute(hView.Get(), hRec.Get());
    if (rc != ERROR_SUCCESS) {
        outError = "Execute INSERT failed on table '" + std::string(table) + "' (" + std::to_string(rc) + ").";
        return false;
    }
    return true;
}

inline bool InsertBinaryStreamRow(
    MSIHANDLE hDb,
    std::string_view tableName,
    std::string_view nameCol,
    std::string_view dataCol,
    std::string_view streamKey,
    const std::filesystem::path& fileOnDisk,
    std::string& outError)
{
    std::ostringstream sql;
    sql << "INSERT INTO `" << tableName << "` (`" << nameCol << "`, `" << dataCol << "`) VALUES (?, ?)";
    const std::wstring wSql = Utf8ToWide(sql.str());

    ScopedMsiHandle hView;
    UINT rc = ::MsiDatabaseOpenViewW(hDb, wSql.c_str(), hView.Put());
    if (rc != ERROR_SUCCESS) {
        outError = "OpenView stream INSERT failed on '" + std::string(tableName) + "' (" + std::to_string(rc) + ").";
        return false;
    }

    ScopedMsiHandle hRec(::MsiCreateRecord(2));
    const std::wstring wKey  = Utf8ToWide(streamKey);
    const std::wstring wPath = fileOnDisk.wstring();
    ::MsiRecordSetStringW(hRec.Get(), 1, wKey.c_str());
    rc = ::MsiRecordSetStreamW(hRec.Get(), 2, wPath.c_str());
    if (rc != ERROR_SUCCESS) {
        outError = "MsiRecordSetStreamW failed for '" + WideToUtf8(wPath) + "' (" + std::to_string(rc) + ").";
        return false;
    }

    rc = ::MsiViewExecute(hView.Get(), hRec.Get());
    if (rc != ERROR_SUCCESS) {
        outError = "Execute stream INSERT failed on '" + std::string(tableName) + "' (" + std::to_string(rc) + ").";
        return false;
    }
    return true;
}

} // namespace msi_sql

inline std::string SlugifyName(std::string_view raw) {
    std::string out;
    bool prevDash = false;
    for (char c : raw) {
        if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
            prevDash = false;
        } else if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) {
            out += c;
            prevDash = false;
        } else if (!out.empty() && !prevDash) {
            out += '-';
            prevDash = true;
        }
    }
    while (!out.empty() && out.back() == '-') out.pop_back();
    return out.empty() ? "raggiesoft-package" : out;
}

inline std::string LoadOrSynthesizeLicenseRtf(
    const std::filesystem::path& rtfPath,
    std::string_view productName,
    std::string_view manufacturer)
{
    if (!rtfPath.empty() && std::filesystem::exists(rtfPath)) {
        std::ifstream in(rtfPath, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        std::string raw = ss.str();
        if (raw.rfind("{\\rtf", 0) == 0) {
            return raw;
        }
    }

    std::ostringstream rtf;
    rtf << "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Segoe UI;}}"
        << "\\fs18\\b " << productName << " - End-User Software License Agreement\\b0\\par\\par "
        << "Published by " << manufacturer << ".\\par\\par "
        << "Permission is hereby granted to install and use this software in accordance with "
        << "the accompanying license documentation.\\par}";
    return rtf.str();
}

/// Relational & ICE consistency verifier (`--verify-msi` + automatic post-build gate!).
inline bool VerifyCompiledMsiDatabase(const std::filesystem::path& msiPath, std::string& outError) {
    ScopedMsiHandle hDb;
    const std::wstring wMsi = msiPath.wstring();
    UINT rc = ::MsiOpenDatabaseW(wMsi.c_str(), MSIDBOPEN_READONLY, hDb.Put());
    if (rc != ERROR_SUCCESS) {
        outError = "VerifyMsi: MsiOpenDatabaseW failed (" + std::to_string(rc) + ").";
        return false;
    }

    ScopedMsiHandle hSum;
    if (::MsiGetSummaryInformationW(hDb.Get(), nullptr, 0, hSum.Put()) != ERROR_SUCCESS) {
        outError = "VerifyMsi: Missing SummaryInformation stream.";
        return false;
    }

    auto readSumStr = [&](UINT pid) -> std::string {
        UINT dataType = 0;
        INT iVal = 0;
        FILETIME ft{};
        wchar_t buf[256]{};
        DWORD cch = static_cast<DWORD>(std::size(buf));
        if (::MsiSummaryInfoGetPropertyW(hSum.Get(), pid, &dataType, &iVal, &ft, buf, &cch) == ERROR_SUCCESS) {
            return WideToUtf8(buf);
        }
        return {};
    };

    const std::string tmpl = readSumStr(7); // PID_TEMPLATE
    const std::string pkg  = readSumStr(9); // PID_REVNUMBER
    if (tmpl != "x64;1033" && tmpl != "Arm64;1033" && tmpl.rfind("x64;", 0) != 0 && tmpl.rfind("Arm64;", 0) != 0) {
        outError = "VerifyMsi (ICE80): Unexpected SummaryInformation PID_TEMPLATE '" + tmpl + "'.";
        return false;
    }
    std::string normPkg;
    if (!NormalizeMsiGuid(pkg, normPkg)) {
        outError = "VerifyMsi: Invalid SummaryInformation PackageCode '" + pkg + "'.";
        return false;
    }

    auto countRows = [&](std::wstring_view query) -> int {
        ScopedMsiHandle hView;
        if (::MsiDatabaseOpenViewW(hDb.Get(), query.data(), hView.Put()) != ERROR_SUCCESS) return -1;
        if (::MsiViewExecute(hView.Get(), 0) != ERROR_SUCCESS) return -1;
        int n = 0;
        ScopedMsiHandle hRec;
        while (::MsiViewFetch(hView.Get(), hRec.Put()) == ERROR_SUCCESS) {
            ++n;
        }
        return n;
    };

    if (countRows(L"SELECT `File` FROM `File`") <= 0) {
        outError = "VerifyMsi: File table is empty.";
        return false;
    }
    if (countRows(L"SELECT `Name` FROM `_Streams` WHERE `Name` = 'payload.cab'") != 1) {
        outError = "VerifyMsi: Embedded 'payload.cab' stream not found in _Streams table.";
        return false;
    }
    if (countRows(L"SELECT `Dialog` FROM `Dialog` WHERE `Dialog` = 'WelcomeDlg'") != 1 ||
        countRows(L"SELECT `Dialog` FROM `Dialog` WHERE `Dialog` = 'ProgressDlg'") != 1 ||
        countRows(L"SELECT `Dialog` FROM `Dialog` WHERE `Dialog` = 'ExitDialog'") != 1) {
        outError = "VerifyMsi: Missing required Wizard 97 dialogs in Dialog table.";
        return false;
    }

    // ICE80 + ICE18 check: every component must carry `msidbComponentAttributes64bit` (`256`),
    // and any component with a NULL `KeyPath` must have a matching `CreateFolder` row!
    {
        ScopedMsiHandle hView;
        if (::MsiDatabaseOpenViewW(hDb.Get(), L"SELECT `Component`, `Attributes`, `KeyPath` FROM `Component`", hView.Put()) == ERROR_SUCCESS &&
            ::MsiViewExecute(hView.Get(), 0) == ERROR_SUCCESS) {
            ScopedMsiHandle hRec;
            while (::MsiViewFetch(hView.Get(), hRec.Put()) == ERROR_SUCCESS) {
                wchar_t compId[128]{};
                DWORD cch = 128;
                ::MsiRecordGetStringW(hRec.Get(), 1, compId, &cch);
                const int attrs = ::MsiRecordGetInteger(hRec.Get(), 2);
                if ((attrs & 256) == 0) {
                    outError = "VerifyMsi (ICE80): 64-bit package Component '" + WideToUtf8(compId) +
                               "' is missing msidbComponentAttributes64bit (256).";
                    return false;
                }
                wchar_t kpBuf[128]{};
                DWORD kpCch = 128;
                ::MsiRecordGetStringW(hRec.Get(), 3, kpBuf, &kpCch);
                if (kpBuf[0] == L'\0') {
                    const std::wstring q = L"SELECT `Directory_` FROM `CreateFolder` WHERE `Component_` = '" + std::wstring(compId) + L"'";
                    if (countRows(q) <= 0) {
                        outError = "VerifyMsi (ICE18): Component '" + WideToUtf8(compId) +
                                   "' has NULL KeyPath without a matching CreateFolder row.";
                        return false;
                    }
                }
            }
        }
    }

    return true;
}

/// Compiles a validated `RswManifestDocument` into a standalone Enterprise `.msi` (`Product.msi`)!
inline bool CompileManifestToMsi(
    const RswManifestDocument& doc,
    std::string_view cliArchOverride,
    CompiledMsiSummary& outSummary,
    std::string& outError)
{
    const ValidationReport val = ValidateManifestDocument(doc, cliArchOverride);
    if (!val.IsOk()) {
        outError = "Manifest validation failed (" + std::to_string(val.ErrorCount()) +
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

    outSummary = CompiledMsiSummary{};
    outSummary.architecture        = arch;
    outSummary.productName         = ExpandArchToken(doc.GetString("Package", "", "Name"), arch);
    outSummary.productVersion      = ExpandArchToken(doc.GetString("Package", "", "Version"), arch);
    outSummary.manufacturer        = ExpandArchToken(doc.GetString("Package", "", "Manufacturer"), arch);
    outSummary.perMachine          = EqualsIgnoreCase(doc.GetString("Package", "", "Scope", "machine"), "machine");
    outSummary.minWindowsBuild     = doc.GetInt("Requirements", "", "MinWindowsBuild", 22000);
    outSummary.osLaunchMessage     = doc.GetString(
        "Requirements", "", "OsLaunchMessage", "This application requires Windows 11 (Build 22000) or later.");
    outSummary.defaultSubdir       = ExpandArchToken(doc.GetString("Install", "", "DefaultSubdir"), arch);
    outSummary.mainExeRel          = ExpandArchToken(doc.GetString("Install", "", "MainExecutable"), arch);
    outSummary.installBtnText      = doc.GetString("UI", "", "InstallButtonText", "&Install");
    outSummary.launchChkText       = doc.GetString("UI", "", "FinishLaunchCheckbox", "&Launch application when setup finishes");
    outSummary.offerLaunchOnFinish = doc.GetBool("UI", "", "OfferLaunchOnFinish", true);
    outSummary.licenseRtfPath      = resolvePath(doc.GetString("UI", "", "LicenseRtf"));
    outSummary.iconFilePath        = resolvePath(doc.GetString("UI", "", "IconFile"));

    NormalizeMsiGuid(doc.GetString("Package", "", "UpgradeCode"), outSummary.upgradeCode);
    const std::string rawProdCode = ExpandArchToken(doc.GetString("Package", "", "ProductCode", "*"), arch);
    if (rawProdCode == "*" || rawProdCode.empty()) {
        // Deterministic UUIDv5 keyed by `(UpgradeCode, Architecture, ProductVersion)`:
        //   - Rebuilding the SAME version (`0.5.0`, `arm64`) keeps a stable `ProductCode` so
        //     `msiexec /x <file>.msi` and `winget` manifests always match the installed product!
        //   - Bumping `Version` (`0.5.0` -> `0.5.1` or `1.0.0` -> `1.1.0`) automatically yields a
        //     fresh `ProductCode` so Major Upgrades work seamlessly out of the box!
        outSummary.productCode = ComputeDeterministicComponentGuid(
            outSummary.upgradeCode, arch, "productcode:" + outSummary.productVersion);
    } else {
        NormalizeMsiGuid(rawProdCode, outSummary.productCode);
    }
    outSummary.packageCode = GenerateRandomMsiGuid();

    const int lcid = doc.GetInt("Package", "", "Language", 1033);
    const std::string rawOutName = ExpandArchToken(doc.GetString("Package", "", "OutputName"), arch);
    const std::string outStem = rawOutName.empty()
        ? (SlugifyName(outSummary.productName) + "-" + arch)
        : rawOutName;
    const std::filesystem::path outDir = resolvePath(doc.GetString("Package", "", "OutputDir", "out"));
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    outSummary.msiPath = outDir / (outStem + ".msi");

    // -------------------------------------------------------------------------
    // STEP 1: Build Destination `Directory` Tree Across All Standard MSI Roots!
    // -------------------------------------------------------------------------
    struct DirNode {
        std::string key;
        std::string parentKey;
        std::string defaultDir;
        bool        isUserProfileSubfolder = false;
    };

    const std::string rootSystemDirKey = outSummary.perMachine ? "ProgramFiles64Folder" : "LocalAppDataFolder";
    std::vector<DirNode> directories;
    std::set<std::string> registeredDirKeys;

    auto pushDirOnce = [&](std::string key, std::string parent, std::string defDir, bool userProfileSub = false) {
        if (registeredDirKeys.insert(key).second) {
            directories.push_back({std::move(key), std::move(parent), std::move(defDir), userProfileSub});
        }
    };

    pushDirOnce("TARGETDIR", "", "SourceDir");
    pushDirOnce(rootSystemDirKey, "TARGETDIR", ".");

    std::size_t shortDirSeed = 1;
    std::vector<std::string> installParts;
    {
        std::string normSub = outSummary.defaultSubdir;
        for (char& c : normSub) if (c == '/') c = '\\';
        std::size_t s = 0;
        while (s <= normSub.size()) {
            auto sep = normSub.find('\\', s);
            std::string seg = TrimAscii(sep == std::string::npos ? normSub.substr(s) : normSub.substr(s, sep - s));
            if (!seg.empty()) installParts.push_back(std::move(seg));
            if (sep == std::string::npos) break;
            s = sep + 1;
        }
    }
    if (installParts.empty()) installParts.push_back(outSummary.productName);

    std::string parentDir = rootSystemDirKey;
    for (std::size_t i = 0; i < installParts.size(); ++i) {
        const bool isLeaf = (i + 1 == installParts.size());
        const std::string dKey = isLeaf ? "INSTALLDIR" : ("INSTALLPARENT_" + std::to_string(i + 1));
        pushDirOnce(dKey, parentDir, FormatMsiShortLongName(installParts[i], shortDirSeed++), !outSummary.perMachine);
        parentDir = dKey;
    }

    auto ensureSystemRootRegistered = [&](const std::string& rootName) -> std::string {
        if (rootName.empty() || EqualsIgnoreCase(rootName, "INSTALLDIR")) {
            return "INSTALLDIR";
        }
        if (EqualsIgnoreCase(rootName, "ApplicationProgramsFolder")) {
            pushDirOnce("ProgramMenuFolder", "TARGETDIR", ".");
            const std::string smSub = ExpandArchToken(doc.GetString("Install", "", "StartMenuFolder", outSummary.manufacturer), arch);
            pushDirOnce("ApplicationProgramsFolder", "ProgramMenuFolder", FormatMsiShortLongName(smSub, shortDirSeed++), true);
            return "ApplicationProgramsFolder";
        }
        // Standard Windows Installer built-in system folder properties (`CommonFiles64Folder`,
        // `CommonAppDataFolder`, `AppDataFolder`, `LocalAppDataFolder`, `DesktopFolder`, `System64Folder`, `FontsFolder`)
        const char* canonicalRoots[] = {
            "CommonFiles64Folder", "CommonAppDataFolder", "AppDataFolder",
            "LocalAppDataFolder", "ProgramMenuFolder", "DesktopFolder",
            "System64Folder", "FontsFolder"
        };
        for (const char* cr : canonicalRoots) {
            if (EqualsIgnoreCase(rootName, cr)) {
                pushDirOnce(cr, "TARGETDIR", ".");
                return cr;
            }
        }
        return "INSTALLDIR";
    };

    std::map<std::string, std::string> rootAndRelSubToDirKey;
    auto ensureTargetDirKey = [&](const std::string& rawRoot, std::string relSub) -> std::string {
        const std::string rootKey = ensureSystemRootRegistered(rawRoot);
        for (char& c : relSub) if (c == '\\') c = '/';
        while (!relSub.empty() && relSub.front() == '/') relSub.erase(relSub.begin());
        while (!relSub.empty() && relSub.back() == '/') relSub.pop_back();
        if (relSub.empty()) return rootKey;

        const std::string cacheKey = AsciiLower(rootKey + "::" + relSub);
        if (auto it = rootAndRelSubToDirKey.find(cacheKey); it != rootAndRelSubToDirKey.end()) {
            return it->second;
        }

        const bool isUserSub = IsPerUserProfileRootDir(rootKey) || (rootKey == "INSTALLDIR" && !outSummary.perMachine);
        std::string accum;
        std::string curParent = rootKey;
        std::size_t pos = 0;
        while (pos <= relSub.size()) {
            auto slash = relSub.find('/', pos);
            std::string seg = (slash == std::string::npos) ? relSub.substr(pos) : relSub.substr(pos, slash - pos);
            if (!seg.empty()) {
                if (!accum.empty()) accum += "/";
                accum += AsciiLower(seg);
                const std::string stepCache = AsciiLower(rootKey + "::" + accum);
                auto hit = rootAndRelSubToDirKey.find(stepCache);
                if (hit != rootAndRelSubToDirKey.end()) {
                    curParent = hit->second;
                } else {
                    std::string newKey = "d_" + std::to_string(rootAndRelSubToDirKey.size() + 1);
                    pushDirOnce(newKey, curParent, FormatMsiShortLongName(seg, shortDirSeed++), isUserSub);
                    rootAndRelSubToDirKey[stepCache] = newKey;
                    curParent = newKey;
                }
            }
            if (slash == std::string::npos) break;
            pos = slash + 1;
        }
        return curParent;
    };

    // Collect `[Feature.<id>]` hierarchy (supporting `Parent` sub-features, `DisallowAbsent`, & `AllowAdvertise`!)
    struct FeatureRec {
        std::string id;
        std::string parentId;
        std::string title;
        std::string desc;
        int         level = 1;
        int         attrs = 8; // Default `msidbFeatureAttributesDisallowAdvertise` (`8`) unless `AllowAdvertise = true`
    };
    std::vector<FeatureRec> features;
    for (const auto& fid : doc.GetNamedSectionIds("Feature")) {
        int fAttrs = 0;
        if (!doc.GetBool("Feature", fid, "AllowAdvertise", false)) fAttrs |= 8;  // `msidbFeatureAttributesDisallowAdvertise`
        if (doc.GetBool("Feature", fid, "DisallowAbsent", false))  fAttrs |= 16; // `msidbFeatureAttributesUIDisallowAbsent`
        const std::string incStr = TrimAscii(doc.GetString("Feature", fid, "Inclusion"));
        const std::string rawLvl = TrimAscii(doc.GetString("Feature", fid, "Level"));
        const int resolvedLvl    = ResolveFeatureMsiLevel(incStr, doc.GetInt("Feature", fid, "Level", 1), !rawLvl.empty());
        features.push_back({
            fid,
            TrimAscii(doc.GetString("Feature", fid, "Parent")),
            doc.GetString("Feature", fid, "Title", fid),
            doc.GetString("Feature", fid, "Description", ""),
            resolvedLvl,
            fAttrs
        });
    }
    if (features.empty()) {
        features.push_back({"Main", "", outSummary.productName, "Core application files", 1, 8});
    }
    auto ensureFeatureExists = [&](const std::string& fid) {
        if (fid.empty()) return features.front().id;
        for (const auto& f : features) {
            if (EqualsIgnoreCase(f.id, fid)) return f.id;
        }
        features.push_back({fid, "", fid, "", 1, 8});
        return fid;
    };

    // Collect explicit `[Component.<id>]` definitions
    struct ExplicitCompState {
        std::string id;
        std::string msiCompKey;
        std::string guid;
        std::string featureId;
        std::string targetRoot;
        std::string targetSubdir;
        std::string dirKey;
        std::string keyPathKind;   // `"auto"`, `"file"`, `"registry"`, `"folder"`
        std::string keyPathTarget;
        std::string condition;
        int         extraAttr = 0; // Permanent (`16`), NeverOverwrite (`128`), SharedDllRefCount (`8`)
        std::vector<std::string> ownedFileKeys;
        std::vector<std::pair<std::string, std::string>> ownedFileNameAndKeys;
        std::vector<std::pair<std::string, std::string>> ownedRegIdAndKeys;
        bool        emitted = false;
    };
    std::map<std::string, ExplicitCompState> explicitComps;
    for (const auto& cid : doc.GetNamedSectionIds("Component")) {
        ExplicitCompState cs{};
        cs.id            = cid;
        cs.msiCompKey    = "c_exp_" + SlugifyName(cid);
        cs.featureId     = ensureFeatureExists(doc.GetString("Component", cid, "Feature", features.front().id));
        cs.targetRoot    = doc.GetString("Component", cid, "TargetRoot", "INSTALLDIR");
        cs.targetSubdir  = ExpandArchToken(doc.GetString("Component", cid, "TargetSubdir"), arch);
        cs.dirKey        = ensureTargetDirKey(cs.targetRoot, cs.targetSubdir);
        cs.keyPathKind   = AsciiLower(doc.GetString("Component", cid, "KeyPathKind", "auto"));
        cs.keyPathTarget = ExpandArchToken(doc.GetString("Component", cid, "KeyPathTarget"), arch);
        cs.condition     = ExpandArchToken(doc.GetString("Component", cid, "Condition"), arch);
        if (doc.GetBool("Component", cid, "Permanent", false))         cs.extraAttr |= 16;
        if (doc.GetBool("Component", cid, "NeverOverwrite", false))    cs.extraAttr |= 128;
        if (doc.GetBool("Component", cid, "SharedDllRefCount", false)) cs.extraAttr |= 8;

        const std::string rawGuid = doc.GetString("Component", cid, "Guid", "*");
        if (rawGuid == "*" || rawGuid.empty()) {
            cs.guid = ComputeDeterministicComponentGuid(outSummary.upgradeCode, arch, "component:" + AsciiLower(cid));
        } else {
            NormalizeMsiGuid(rawGuid, cs.guid);
        }
        explicitComps[AsciiLower(cid)] = std::move(cs);
    }

    // Harvest `[Payload.<id>]` files
    struct StagedFileRecord {
        std::string           fileKey;
        std::string           compKey;
        std::string           compGuid;
        std::string           explicitCompLower; // Non-empty when bound to `[Component.<id>]`!
        std::string           targetRoot;
        std::string           dirKey;
        std::string           featureId;
        std::filesystem::path sourcePath;
        std::string           fileNameOnly;
        std::string           relInstallPath;    // Relative to `TargetRoot`, `/` separated
        std::string           msiFileName;
        int                   fileSize = 0;
        std::string           peVersion;
        std::string           peLanguage;
        int                   sequence = 0;
    };

    std::vector<StagedFileRecord> stagedFiles;
    std::set<std::string> seenRootRelPaths;
    bool hasFontFiles = false;

    auto appendSingleFile = [&](const std::filesystem::path& srcFile,
                                const std::string& targetRoot,
                                const std::string& targetRelDir,
                                const std::string& featureId,
                                const std::string& explicitCompName) {
        const std::string fileNameUtf8 = WideToUtf8(srcFile.filename().wstring());
        std::string relInstall = targetRelDir.empty() ? fileNameUtf8 : (targetRelDir + "/" + fileNameUtf8);
        for (char& c : relInstall) if (c == '\\') c = '/';
        while (!relInstall.empty() && relInstall.front() == '/') relInstall.erase(relInstall.begin());

        const std::string dedupKey = AsciiLower(targetRoot + "::" + relInstall);
        if (!seenRootRelPaths.insert(dedupKey).second) {
            return;
        }

        const int seq = static_cast<int>(stagedFiles.size() + 1);
        char keyBuf[24]{};
        std::snprintf(keyBuf, sizeof(keyBuf), "%04d", seq);
        const std::string fKey = std::string("f_") + keyBuf;

        std::string expLower = AsciiLower(TrimAscii(explicitCompName));
        std::string cKey     = std::string("c_") + keyBuf;
        std::string cGuid    = ComputeDeterministicComponentGuid(
            outSummary.upgradeCode, arch, "file:" + AsciiLower(targetRoot) + ":" + relInstall);
        std::string dKey     = ensureTargetDirKey(targetRoot, targetRelDir);
        std::string fId      = ensureFeatureExists(featureId);

        if (!expLower.empty()) {
            auto it = explicitComps.find(expLower);
            if (it != explicitComps.end()) {
                cKey  = it->second.msiCompKey;
                cGuid = it->second.guid;
                dKey  = it->second.dirKey;
                fId   = it->second.featureId;
                it->second.ownedFileKeys.push_back(fKey);
                it->second.ownedFileNameAndKeys.push_back({fileNameUtf8, fKey});
            }
        }

        if (EqualsIgnoreCase(targetRoot, "FontsFolder")) {
            const std::string extLow = AsciiLower(WideToUtf8(srcFile.extension().wstring()));
            if (extLow == ".ttf" || extLow == ".otf" || extLow == ".ttc") {
                hasFontFiles = true;
            }
        }

        const auto pe = HarvestPeVersionAndLanguage(srcFile);
        const auto sz = static_cast<int>(std::min<std::uintmax_t>(
            std::filesystem::file_size(srcFile, ec), 0x7FFFFFFFu));

        stagedFiles.push_back({
            fKey, cKey, cGuid, expLower, targetRoot, dKey, fId, srcFile, fileNameUtf8, relInstall,
            FormatMsiShortLongName(fileNameUtf8, static_cast<std::size_t>(seq)),
            sz, pe.version, pe.language, seq
        });
    };

    for (const auto& pid : doc.GetNamedSectionIds("Payload")) {
        const auto absSrc       = resolvePath(doc.GetString("Payload", pid, "Source"));
        const std::string refCmp= TrimAscii(doc.GetString("Payload", pid, "Component"));
        const std::string defRt = !refCmp.empty() ? doc.GetString("Component", refCmp, "TargetRoot", "INSTALLDIR") : "INSTALLDIR";
        const std::string defSb = !refCmp.empty() ? doc.GetString("Component", refCmp, "TargetSubdir", "") : "";
        const std::string tRoot = doc.GetString("Payload", pid, "TargetRoot", defRt);
        const std::string sub   = ExpandArchToken(doc.GetString("Payload", pid, "TargetSubdir", defSb), arch);
        const bool recursive    = doc.GetBool("Payload", pid, "Recursive", true);
        const std::string feat  = doc.GetString("Payload", pid, "Feature", features.front().id);

        if (std::filesystem::is_regular_file(absSrc, ec)) {
            appendSingleFile(absSrc, tRoot, sub, feat, refCmp);
        } else if (std::filesystem::is_directory(absSrc, ec)) {
            std::vector<std::filesystem::path> found;
            if (recursive) {
                for (const auto& de : std::filesystem::recursive_directory_iterator(absSrc, ec)) {
                    if (de.is_regular_file(ec)) found.push_back(de.path());
                }
            } else {
                for (const auto& de : std::filesystem::directory_iterator(absSrc, ec)) {
                    if (de.is_regular_file(ec)) found.push_back(de.path());
                }
            }
            std::sort(found.begin(), found.end());
            for (const auto& fPath : found) {
                auto relParent = std::filesystem::relative(fPath.parent_path(), absSrc, ec);
                std::string relParentStr = (relParent == ".") ? "" : WideToUtf8(relParent.generic_wstring());
                std::string combinedSub = sub.empty()
                    ? relParentStr
                    : (relParentStr.empty() ? sub : (sub + "/" + relParentStr));
                appendSingleFile(fPath, tRoot, combinedSub, feat, refCmp);
            }
        }
    }

    const auto mergeModuleIds = doc.GetNamedSectionIds("MergeModule");
    for (const auto& mid : mergeModuleIds) {
        ensureSystemRootRegistered(doc.GetString("MergeModule", mid, "TargetRoot", "INSTALLDIR"));
        ensureFeatureExists(doc.GetString("MergeModule", mid, "Feature", features.front().id));
    }

    if (stagedFiles.empty() && mergeModuleIds.empty()) {
        outError = "No payload files were matched by [Payload.*] or [MergeModule.*] sections.";
        return false;
    }

    // Pre-register `[Folder.<id>]` & `[Shortcut.<id>]` directories so `Directory` table is 100% complete!
    struct FolderRec {
        std::string id;
        std::string dirKey;
        std::string featureId;
        std::string explicitCompLower;
        bool        removeOnUninstall = true;
    };
    std::vector<FolderRec> explicitFolders;
    for (const auto& flId : doc.GetNamedSectionIds("Folder")) {
        const std::string fRoot = doc.GetString("Folder", flId, "TargetRoot", "INSTALLDIR");
        const std::string fSub  = ExpandArchToken(doc.GetString("Folder", flId, "TargetSubdir"), arch);
        const std::string dKey  = ensureTargetDirKey(fRoot, fSub);
        const std::string feat  = ensureFeatureExists(doc.GetString("Folder", flId, "Feature", features.front().id));
        const std::string cLow  = AsciiLower(TrimAscii(doc.GetString("Folder", flId, "Component")));
        const bool rem          = doc.GetBool("Folder", flId, "RemoveOnUninstall", true);
        explicitFolders.push_back({flId, dKey, feat, cLow, rem});
    }

    const auto shortcutIds = doc.GetNamedSectionIds("Shortcut");
    for (const auto& sid : shortcutIds) {
        const std::string loc = doc.GetString("Shortcut", sid, "Location", "StartMenu");
        if (EqualsIgnoreCase(loc, "Desktop")) ensureSystemRootRegistered("DesktopFolder");
        else ensureSystemRootRegistered("ApplicationProgramsFolder");
    }

    // Check if any `[FileAssociation.<id>]` sets `Advertise = true` (requiring `ProgId`, `Extension`, `Verb`, `MIME` tables)
    const auto fileAssocIds = doc.GetNamedSectionIds("FileAssociation");
    bool hasAdvertisedAssoc = false;
    for (const auto& aid : fileAssocIds) {
        if (doc.GetBool("FileAssociation", aid, "Advertise", false)) {
            hasAdvertisedAssoc = true;
            break;
        }
    }

    // -------------------------------------------------------------------------
    // STEP 2: Prepare scratch workspace & queue staged files for `#payload.cab`
    //         (Final `BuildPayloadCabinet` runs after `[MergeModule.*]` merging!)
    // -------------------------------------------------------------------------
    const std::filesystem::path scratchDir = std::filesystem::temp_directory_path(ec) /
                                             ("rsw-build-" + outSummary.packageCode.substr(1, 8));
    std::filesystem::create_directories(scratchDir, ec);
    const std::filesystem::path cabPath        = scratchDir / "payload.cab";
    const std::filesystem::path bannerBmpPath  = scratchDir / "banner.bmp";
    const std::filesystem::path welcomeBmpPath = scratchDir / "welcome.bmp";

    std::vector<CabInputEntry> cabInputs;
    cabInputs.reserve(stagedFiles.size() + 16);
    for (const auto& sf : stagedFiles) {
        cabInputs.push_back({sf.sourcePath, sf.fileKey});
    }

    const bool useLzx = !EqualsIgnoreCase(doc.GetString("Package", "", "CabinetCompression", "lzx"), "mszip");
    WriteDefaultWizard97BannerBmp(bannerBmpPath);
    WriteDefaultWizard97WelcomeBmp(welcomeBmpPath);

    // -------------------------------------------------------------------------
    // STEP 3: Create relational MSI database (`MSIDBOPEN_CREATEDIRECT`) & DDL
    // -------------------------------------------------------------------------
    std::filesystem::remove(outSummary.msiPath, ec);
    ScopedMsiHandle hDb;
    const std::wstring wMsiPath = outSummary.msiPath.wstring();
    UINT rc = ::MsiOpenDatabaseW(wMsiPath.c_str(), MSIDBOPEN_CREATEDIRECT, hDb.Put());
    if (rc != ERROR_SUCCESS) {
        std::filesystem::remove_all(scratchDir, ec);
        outError = "MsiOpenDatabaseW(MSIDBOPEN_CREATEDIRECT) failed (" + std::to_string(rc) + ").";
        return false;
    }

    const char* ddlStatements[] = {
        "CREATE TABLE `_Validation` (`Table` CHAR(32) NOT NULL, `Column` CHAR(32) NOT NULL, `Nullable` CHAR(4) NOT NULL, "
        "`MinValue` LONG, `MaxValue` LONG, `KeyTable` CHAR(255), `KeyColumn` SHORT, `Category` CHAR(32), `Set` CHAR(255), "
        "`Description` CHAR(255) PRIMARY KEY `Table`, `Column`)",
        "CREATE TABLE `Property` (`Property` CHAR(72) NOT NULL, `Value` LONGCHAR NOT NULL LOCALIZABLE PRIMARY KEY `Property`)",
        "CREATE TABLE `Directory` (`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL LOCALIZABLE PRIMARY KEY `Directory`)",
        "CREATE TABLE `Component` (`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, `Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72) PRIMARY KEY `Component`)",
        "CREATE TABLE `CreateFolder` (`Directory_` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Directory_`, `Component_`)",
        "CREATE TABLE `File` (`File` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL LOCALIZABLE, `FileSize` LONG NOT NULL, `Version` CHAR(72), `Language` CHAR(20), `Attributes` SHORT, `Sequence` INTEGER NOT NULL PRIMARY KEY `File`)",
        "CREATE TABLE `Font` (`File_` CHAR(72) NOT NULL, `FontTitle` CHAR(128) PRIMARY KEY `File_`)",
        "CREATE TABLE `Media` (`DiskId` SHORT NOT NULL, `LastSequence` INTEGER NOT NULL, `DiskPrompt` CHAR(64) LOCALIZABLE, `Cabinet` CHAR(255), `VolumeLabel` CHAR(32), `Source` CHAR(72) PRIMARY KEY `DiskId`)",
        "CREATE TABLE `Feature` (`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64) LOCALIZABLE, `Description` CHAR(255) LOCALIZABLE, `Display` SHORT, `Level` SHORT NOT NULL, `Directory_` CHAR(72), `Attributes` SHORT NOT NULL PRIMARY KEY `Feature`)",
        "CREATE TABLE `FeatureComponents` (`Feature_` CHAR(38) NOT NULL, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Feature_`, `Component_`)",
        "CREATE TABLE `Shortcut` (`Shortcut` CHAR(72) NOT NULL, `Directory_` CHAR(72) NOT NULL, `Name` CHAR(128) NOT NULL LOCALIZABLE, `Component_` CHAR(72) NOT NULL, `Target` CHAR(72) NOT NULL, `Arguments` CHAR(255), `Description` CHAR(255) LOCALIZABLE, `Hotkey` SHORT, `Icon_` CHAR(72), `IconIndex` SHORT, `ShowCmd` SHORT, `WkDir` CHAR(72) PRIMARY KEY `Shortcut`)",
        "CREATE TABLE `RemoveFile` (`FileKey` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) LOCALIZABLE, `DirProperty` CHAR(72) NOT NULL, `InstallMode` SHORT NOT NULL PRIMARY KEY `FileKey`)",
        "CREATE TABLE `Registry` (`Registry` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL LOCALIZABLE, `Name` CHAR(255) LOCALIZABLE, `Value` LONGCHAR LOCALIZABLE, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Registry`)",
        "CREATE TABLE `ProgId` (`ProgId` CHAR(255) NOT NULL, `ProgId_Parent` CHAR(255), `Class_` CHAR(38), `Description` CHAR(255) LOCALIZABLE, `Icon_` CHAR(72), `IconIndex` SHORT PRIMARY KEY `ProgId`)",
        "CREATE TABLE `Extension` (`Extension` CHAR(255) NOT NULL, `Component_` CHAR(72) NOT NULL, `ProgId_` CHAR(255), `MIME_` CHAR(64), `Feature_` CHAR(38) NOT NULL PRIMARY KEY `Extension`, `Component_`)",
        "CREATE TABLE `Verb` (`Extension_` CHAR(255) NOT NULL, `Verb` CHAR(32) NOT NULL, `Sequence` SHORT, `Command` CHAR(64) LOCALIZABLE, `Argument` CHAR(255) LOCALIZABLE PRIMARY KEY `Extension_`, `Verb`)",
        "CREATE TABLE `MIME` (`ContentType` CHAR(64) NOT NULL, `Extension_` CHAR(255) NOT NULL, `CLSID` CHAR(38) PRIMARY KEY `ContentType`)",
        "CREATE TABLE `Icon` (`Name` CHAR(72) NOT NULL, `Data` OBJECT NOT NULL PRIMARY KEY `Name`)",
        "CREATE TABLE `Binary` (`Name` CHAR(72) NOT NULL, `Data` OBJECT NOT NULL PRIMARY KEY `Name`)",
        "CREATE TABLE `Upgrade` (`UpgradeCode` CHAR(38) NOT NULL, `VersionMin` CHAR(20), `VersionMax` CHAR(20), `Language` CHAR(255), `Attributes` LONG NOT NULL, `Remove` CHAR(255), `ActionProperty` CHAR(72) NOT NULL PRIMARY KEY `UpgradeCode`, `VersionMin`, `VersionMax`, `Language`, `Attributes`)",
        // CRITICAL `AppSearch` Table Set (`Signature`, `RegLocator`, `CompLocator`, `DrLocator`, `IniLocator`):
        // Whenever `AppSearch` contains rows (even registry-only `RegLocator` searches!), Windows Installer
        // queries the `Signature` table FIRST (`SELECT FileName... FROM Signature WHERE Signature = ?`).
        // Omitting `Signature` causes `msiexec` to abort with `Error 2228: Unknown table 'Signature'` (`1603`)!
        "CREATE TABLE `Signature` (`Signature` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL, `MinVersion` CHAR(20), `MaxVersion` CHAR(20), `MinSize` LONG, `MaxSize` LONG, `MinDate` LONG, `MaxDate` LONG, `Languages` CHAR(255) PRIMARY KEY `Signature`)",
        "CREATE TABLE `RegLocator` (`Signature_` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL, `Name` CHAR(255), `Type` SHORT PRIMARY KEY `Signature_`)",
        "CREATE TABLE `CompLocator` (`Signature_` CHAR(72) NOT NULL, `ComponentId` CHAR(38) NOT NULL, `Type` SHORT PRIMARY KEY `Signature_`)",
        "CREATE TABLE `DrLocator` (`Signature_` CHAR(72) NOT NULL, `Parent` CHAR(72), `Path` CHAR(255), `Depth` SHORT PRIMARY KEY `Signature_`, `Parent`, `Path`)",
        "CREATE TABLE `IniLocator` (`Signature_` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL, `Section` CHAR(96) NOT NULL, `Key` CHAR(96) NOT NULL, `Field` SHORT, `Type` SHORT PRIMARY KEY `Signature_`)",
        "CREATE TABLE `AppSearch` (`Property` CHAR(72) NOT NULL, `Signature_` CHAR(72) NOT NULL PRIMARY KEY `Property`, `Signature_`)",
        "CREATE TABLE `LaunchCondition` (`Condition` CHAR(255) NOT NULL, `Description` CHAR(255) NOT NULL LOCALIZABLE PRIMARY KEY `Condition`)",
        "CREATE TABLE `CustomAction` (`Action` CHAR(72) NOT NULL, `Type` SHORT NOT NULL, `Source` CHAR(72), `Target` CHAR(255) PRIMARY KEY `Action`)",
        "CREATE TABLE `InstallExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)",
        "CREATE TABLE `AdminExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)",
        "CREATE TABLE `AdvtExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)",
        "CREATE TABLE `InstallUISequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)",
        "CREATE TABLE `TextStyle` (`TextStyle` CHAR(72) NOT NULL, `FaceName` CHAR(32) NOT NULL, `Size` SHORT NOT NULL, `Color` LONG, `StyleBits` SHORT PRIMARY KEY `TextStyle`)",
        "CREATE TABLE `Dialog` (`Dialog` CHAR(72) NOT NULL, `HCentering` SHORT NOT NULL, `VCentering` SHORT NOT NULL, `Width` SHORT NOT NULL, `Height` SHORT NOT NULL, `Attributes` LONG, `Title` CHAR(128) LOCALIZABLE, `Control_First` CHAR(50) NOT NULL, `Control_Default` CHAR(50), `Control_Cancel` CHAR(50) PRIMARY KEY `Dialog`)",
        "CREATE TABLE `Control` (`Dialog_` CHAR(72) NOT NULL, `Control` CHAR(50) NOT NULL, `Type` CHAR(20) NOT NULL, `X` SHORT NOT NULL, `Y` SHORT NOT NULL, `Width` SHORT NOT NULL, `Height` SHORT NOT NULL, `Attributes` LONG, `Property` CHAR(50), `Text` LONGCHAR LOCALIZABLE, `Control_Next` CHAR(50), `Help` CHAR(50) LOCALIZABLE PRIMARY KEY `Dialog_`, `Control`)",
        "CREATE TABLE `RadioButton` (`Property` CHAR(72) NOT NULL, `Order` SHORT NOT NULL, `Value` CHAR(64) NOT NULL, `X` SHORT NOT NULL, `Y` SHORT NOT NULL, `Width` SHORT NOT NULL, `Height` SHORT NOT NULL, `Text` CHAR(255) LOCALIZABLE, `Help` CHAR(50) LOCALIZABLE PRIMARY KEY `Property`, `Order`)",
        "CREATE TABLE `ComboBox` (`Property` CHAR(72) NOT NULL, `Order` SHORT NOT NULL, `Value` CHAR(64) NOT NULL, `Text` CHAR(128) LOCALIZABLE PRIMARY KEY `Property`, `Order`)",
        "CREATE TABLE `ListBox` (`Property` CHAR(72) NOT NULL, `Order` SHORT NOT NULL, `Value` CHAR(64) NOT NULL, `Text` CHAR(128) LOCALIZABLE PRIMARY KEY `Property`, `Order`)",
        "CREATE TABLE `ControlEvent` (`Dialog_` CHAR(72) NOT NULL, `Control_` CHAR(50) NOT NULL, `Event` CHAR(50) NOT NULL, `Argument` CHAR(255) NOT NULL, `Condition` CHAR(255), `Ordering` SHORT PRIMARY KEY `Dialog_`, `Control_`, `Event`, `Argument`, `Condition`)",
        "CREATE TABLE `ControlCondition` (`Dialog_` CHAR(72) NOT NULL, `Control_` CHAR(50) NOT NULL, `Action` CHAR(50) NOT NULL, `Condition` CHAR(255) NOT NULL PRIMARY KEY `Dialog_`, `Control_`, `Action`, `Condition`)",
        "CREATE TABLE `EventMapping` (`Dialog_` CHAR(72) NOT NULL, `Control_` CHAR(50) NOT NULL, `Event` CHAR(50) NOT NULL, `Attribute` CHAR(50) NOT NULL PRIMARY KEY `Dialog_`, `Control_`, `Event`)",
        "CREATE TABLE `UIText` (`Key` CHAR(72) NOT NULL, `Text` CHAR(255) LOCALIZABLE PRIMARY KEY `Key`)",
        "CREATE TABLE `ActionText` (`Action` CHAR(72) NOT NULL, `Description` CHAR(64) LOCALIZABLE, `Template` CHAR(128) LOCALIZABLE PRIMARY KEY `Action`)",
        "CREATE TABLE `Error` (`Error` SHORT NOT NULL, `Message` LONGCHAR LOCALIZABLE PRIMARY KEY `Error`)"
    };

    for (const char* ddl : ddlStatements) {
        if (!msi_sql::ExecuteDdl(hDb.Get(), ddl, outError)) {
            std::filesystem::remove_all(scratchDir, ec);
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // STEP 4: Populate `Property`, `Upgrade`, `RegLocator`, `AppSearch`, `LaunchCondition`
    // -------------------------------------------------------------------------
    auto addProp = [&](std::string_view k, std::string v) {
        return msi_sql::InsertRow(hDb.Get(), "Property", {"Property", "Value"}, {std::string(k), std::move(v)}, outError);
    };

    std::set<std::string> secureCustomProps = {
        "RSW_OLDERVERSION_DETECTED", "RSW_NEWERVERSION_DETECTED", "RSW_OSBUILD",
        "INSTALLDIR", "STARTMENUFOLDER", "DESKTOP_SHORTCUT", "REGISTER_ASSOC",
        "REMOVE_CACHE", "REMOVE_PREFS"
    };

    addProp("ProductCode",     outSummary.productCode);
    addProp("ProductName",     outSummary.productName);
    addProp("ProductVersion",  outSummary.productVersion);
    addProp("ProductLanguage", std::to_string(lcid));
    addProp("Manufacturer",    outSummary.manufacturer);
    addProp("UpgradeCode",     outSummary.upgradeCode);
    addProp("DefaultUIFont",   "DlgFont8");
    addProp("INSTALLLEVEL",    "100");
    addProp("IAgree",          "No");
    addProp("LAUNCHAPP",       outSummary.offerLaunchOnFinish && !outSummary.mainExeRel.empty() ? "1" : "0");

    if (outSummary.perMachine) {
        addProp("ALLUSERS", "1");
    }
    if (doc.GetBool("Package", "", "HideFromInstalledApps", false)) {
        addProp("ARPSYSTEMCOMPONENT", "1");
    }
    const std::string aboutUrl = doc.GetString("Package", "", "AboutUrl");
    if (!aboutUrl.empty()) {
        addProp("ARPHELPLINK",     aboutUrl);
        addProp("ARPURLINFOABOUT", aboutUrl);
    }
    const bool hasProductIcon = !outSummary.iconFilePath.empty() && std::filesystem::exists(outSummary.iconFilePath, ec);
    if (hasProductIcon) {
        if (!msi_sql::InsertBinaryStreamRow(hDb.Get(), "Icon", "Name", "Data", "AppProductIcon.ico", outSummary.iconFilePath, outError)) {
            std::filesystem::remove_all(scratchDir, ec);
            return false;
        }
        addProp("ARPPRODUCTICON", "AppProductIcon.ico");
    }

    msi_sql::InsertRow(
        hDb.Get(), "Upgrade",
        {"UpgradeCode", "VersionMin", "VersionMax", "Language", "Attributes", "Remove", "ActionProperty"},
        {outSummary.upgradeCode, "0.0.0", outSummary.productVersion, std::monostate{}, 256, std::monostate{}, std::string("RSW_OLDERVERSION_DETECTED")},
        outError);

    if (doc.GetBool("Requirements", "", "BlockDowngrades", true)) {
        msi_sql::InsertRow(
            hDb.Get(), "Upgrade",
            {"UpgradeCode", "VersionMin", "VersionMax", "Language", "Attributes", "Remove", "ActionProperty"},
            {outSummary.upgradeCode, outSummary.productVersion, std::monostate{}, std::monostate{}, 2, std::monostate{}, std::string("RSW_NEWERVERSION_DETECTED")},
            outError);
        msi_sql::InsertRow(
            hDb.Get(), "LaunchCondition",
            {"Condition", "Description"},
            {std::string("NOT RSW_NEWERVERSION_DETECTED"), std::string("A newer version of [ProductName] is already installed on this computer.")},
            outError);
    }

    // Read BOTH `CurrentMajorVersionNumber` (`#10` on Windows 10/11, absent on
    // Windows 7/8/8.1) and `CurrentBuildNumber` (`22000`+ on Windows 11) directly
    // from 64-bit `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion` via `AppSearch`
    // (`Type = 18` = `msidbLocatorTypeRawValue | msidbLocatorType64bit`).
    // Why avoid `VersionNT >= 603`? Unmanifested Win32 hosts (e.g., legacy SCCM/MDT
    // wrappers or console test runners without a Windows 10 `<supportedOS>` manifest)
    // have `GetVersionExW` shimmed to Windows 8 (`VersionNT = 602`), whereas these
    // registry values always report the true underlying OS major version and build!
    msi_sql::InsertRow(
        hDb.Get(), "RegLocator",
        {"Signature_", "Root", "Key", "Name", "Type"},
        {std::string("SigWinNtCurrentMajor"), 2, std::string("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"), std::string("CurrentMajorVersionNumber"), 18},
        outError);
    msi_sql::InsertRow(
        hDb.Get(), "AppSearch",
        {"Property", "Signature_"},
        {std::string("RSW_OSMAJOR"), std::string("SigWinNtCurrentMajor")},
        outError);
    msi_sql::InsertRow(
        hDb.Get(), "RegLocator",
        {"Signature_", "Root", "Key", "Name", "Type"},
        {std::string("SigWinNtCurrentBuild"), 2, std::string("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"), std::string("CurrentBuildNumber"), 18},
        outError);
    msi_sql::InsertRow(
        hDb.Get(), "AppSearch",
        {"Property", "Signature_"},
        {std::string("RSW_OSBUILD"), std::string("SigWinNtCurrentBuild")},
        outError);
    msi_sql::InsertRow(
        hDb.Get(), "LaunchCondition",
        {"Condition", "Description"},
        {"Installed OR (RSW_OSMAJOR AND RSW_OSBUILD >= \"" + std::to_string(outSummary.minWindowsBuild) + "\")",
         outSummary.osLaunchMessage},
        outError);

    // -------------------------------------------------------------------------
    // STEP 5: Populate `Feature`, `Directory`, `Component`, `CreateFolder`,
    //         `File`, `Font`, `Registry`, `Shortcut`, & `[FileAssociation.*]`!
    // -------------------------------------------------------------------------
    int featOrder = 2;
    for (const auto& f : features) {
        msi_sql::InsertRow(
            hDb.Get(), "Feature",
            {"Feature", "Feature_Parent", "Title", "Description", "Display", "Level", "Directory_", "Attributes"},
            {f.id, f.parentId.empty() ? SqlParam{std::monostate{}} : SqlParam{f.parentId},
             f.title, f.desc, featOrder, f.level, std::string("INSTALLDIR"), f.attrs},
            outError);
        featOrder += 2;
    }

    for (const auto& d : directories) {
        msi_sql::InsertRow(
            hDb.Get(), "Directory",
            {"Directory", "Directory_Parent", "DefaultDir"},
            {d.key, d.parentKey.empty() ? SqlParam{std::monostate{}} : SqlParam{d.parentKey}, d.defaultDir},
            outError);
    }

    constexpr int kCompAttr64BitFile = 256;
    constexpr int kCompAttr64BitReg  = 256 + 4; // `msidbComponentAttributes64bit | msidbComponentAttributesRegistryKeyPath`

    // Pre-scan `[Registry.<id>]` rows bound to an explicit `[Component.<id>]` so `KeyPathKind = registry` resolves cleanly!
    struct PendingRegRow {
        std::string regSectionId;
        std::string msiRegKey;
        int         rootNum = 2;
        std::string keyPath;
        std::string valName;
        std::string formattedVal;
        std::string featureId;
        std::string explicitCompLower;
    };
    std::vector<PendingRegRow> pendingRegs;
    std::size_t regCounter = 0;
    for (const auto& rid : doc.GetNamedSectionIds("Registry")) {
        ++regCounter;
        const std::string rootStr = doc.GetString("Registry", rid, "Root", outSummary.perMachine ? "HKLM" : "HKCU");
        int rootNum = 2; // HKLM
        if (EqualsIgnoreCase(rootStr, "HKCU")) rootNum = 1;
        else if (EqualsIgnoreCase(rootStr, "HKCR")) rootNum = 0;
        else if (EqualsIgnoreCase(rootStr, "HKU"))  rootNum = 3;
        else if (EqualsIgnoreCase(rootStr, "HKMU")) rootNum = -1;

        const std::string keyP    = ExpandArchToken(doc.GetString("Registry", rid, "Key"), arch);
        const std::string valName = ExpandArchToken(doc.GetString("Registry", rid, "Name"), arch);
        const std::string rawVal  = ExpandArchToken(doc.GetString("Registry", rid, "Value"), arch);
        const std::string valType = doc.GetString("Registry", rid, "Type", "string");
        const std::string featId  = ensureFeatureExists(doc.GetString("Registry", rid, "Feature", features.front().id));
        const std::string expComp = AsciiLower(TrimAscii(doc.GetString("Registry", rid, "Component")));

        std::string formattedVal = rawVal;
        if (EqualsIgnoreCase(valType, "dword")) formattedVal = "#" + rawVal;
        else if (EqualsIgnoreCase(valType, "expand_string")) formattedVal = "#%" + rawVal;

        const std::string rRow = "r_usr_" + std::to_string(regCounter);
        if (!expComp.empty()) {
            auto it = explicitComps.find(expComp);
            if (it != explicitComps.end()) {
                it->second.ownedRegIdAndKeys.push_back({rid, rRow});
            }
        }
        pendingRegs.push_back({rid, rRow, rootNum, keyP, valName, formattedVal, featId, expComp});
    }

    std::size_t totalComponents = 0;
    std::size_t removeFileSeq   = 0;

    auto emitUserDirCleanupIfNeeded = [&](const std::string& dirKey, const std::string& compKey) {
        for (const auto& dn : directories) {
            if (dn.key == dirKey && dn.isUserProfileSubfolder) {
                ++removeFileSeq;
                msi_sql::InsertRow(
                    hDb.Get(), "RemoveFile",
                    {"FileKey", "Component_", "FileName", "DirProperty", "InstallMode"},
                    {"rf_dir_" + std::to_string(removeFileSeq), compKey, std::monostate{}, dirKey, 2},
                    outError);
                break;
            }
        }
    };

    // 5A. Emit all explicit `[Component.<id>]` rows (enforcing Microsoft File / Registry / Folder KeyPath & ICE18/ICE38!)
    std::size_t autoHkcuKpCounter = 0;
    for (auto& [cidLow, cs] : explicitComps) {
        const bool isUserDir = IsPerUserProfileRootDir(cs.targetRoot);
        int attrs = kCompAttr64BitFile | cs.extraAttr;
        SqlParam keyPathVal = std::monostate{};
        bool needsCreateFolder = false;

        if (cs.keyPathKind == "file") {
            std::string chosenFileKey;
            for (const auto& [fn, fk] : cs.ownedFileNameAndKeys) {
                if (EqualsIgnoreCase(fn, cs.keyPathTarget)) { chosenFileKey = fk; break; }
            }
            if (chosenFileKey.empty() && !cs.ownedFileKeys.empty()) {
                chosenFileKey = cs.ownedFileKeys.front();
            }
            if (chosenFileKey.empty()) {
                std::filesystem::remove_all(scratchDir, ec);
                outError = "Component '" + cs.id + "' specifies KeyPathKind = 'file' ('" + cs.keyPathTarget +
                           "'), but no matching [Payload.*] file is assigned to Component = " + cs.id + ".";
                return false;
            }
            keyPathVal = chosenFileKey;
        } else if (cs.keyPathKind == "registry") {
            attrs |= 4; // `msidbComponentAttributesRegistryKeyPath`
            std::string chosenRegKey;
            for (const auto& [rid, rk] : cs.ownedRegIdAndKeys) {
                if (EqualsIgnoreCase(rid, cs.keyPathTarget)) { chosenRegKey = rk; break; }
            }
            if (chosenRegKey.empty() && !cs.ownedRegIdAndKeys.empty()) {
                chosenRegKey = cs.ownedRegIdAndKeys.front().second;
            }
            if (chosenRegKey.empty()) {
                std::filesystem::remove_all(scratchDir, ec);
                outError = "Component '" + cs.id + "' specifies KeyPathKind = 'registry', but [Registry." +
                           cs.keyPathTarget + "] is not assigned to Component = " + cs.id + ".";
                return false;
            }
            keyPathVal = chosenRegKey;
        } else if (cs.keyPathKind == "folder") {
            // Microsoft ICE18 Rule: NULL KeyPath + `CreateFolder` row!
            keyPathVal = std::monostate{};
            needsCreateFolder = true;
        } else {
            // `auto` KeyPath resolution following Microsoft ICE18 & ICE38 rules!
            if (isUserDir) {
                attrs |= 4;
                if (!cs.ownedRegIdAndKeys.empty()) {
                    keyPathVal = cs.ownedRegIdAndKeys.front().second;
                } else {
                    ++autoHkcuKpCounter;
                    const std::string autoRk = "r_kp_exp_" + std::to_string(autoHkcuKpCounter);
                    msi_sql::InsertRow(
                        hDb.Get(), "Registry",
                        {"Registry", "Root", "Key", "Name", "Value", "Component_"},
                        {autoRk, 1 /* HKCU */, "Software\\" + outSummary.manufacturer + "\\" + outSummary.productName + "\\Components",
                         cs.id, std::string("#1"), cs.msiCompKey},
                        outError);
                    keyPathVal = autoRk;
                }
            } else if (!cs.ownedFileKeys.empty()) {
                keyPathVal = cs.ownedFileKeys.front();
            } else if (!cs.ownedRegIdAndKeys.empty()) {
                attrs |= 4;
                keyPathVal = cs.ownedRegIdAndKeys.front().second;
            } else {
                keyPathVal = std::monostate{};
                needsCreateFolder = true;
            }
        }

        msi_sql::InsertRow(
            hDb.Get(), "Component",
            {"Component", "ComponentId", "Directory_", "Attributes", "Condition", "KeyPath"},
            {cs.msiCompKey, cs.guid, cs.dirKey, attrs,
             cs.condition.empty() ? SqlParam{std::monostate{}} : SqlParam{cs.condition},
             keyPathVal},
            outError);
        ++totalComponents;
        cs.emitted = true;

        msi_sql::InsertRow(
            hDb.Get(), "FeatureComponents",
            {"Feature_", "Component_"},
            {cs.featureId, cs.msiCompKey},
            outError);

        if (needsCreateFolder) {
            msi_sql::InsertRow(
                hDb.Get(), "CreateFolder",
                {"Directory_", "Component_"},
                {cs.dirKey, cs.msiCompKey},
                outError);
        }
        emitUserDirCleanupIfNeeded(cs.dirKey, cs.msiCompKey);
    }

    // 5B. Emit `[File]` rows (+ automatic per-file Components for `[Payload.*]` with no explicit `Component`)
    std::map<std::string, std::string> relPathToFileKey;
    std::map<std::string, std::string> relPathToCompKey;
    std::map<std::string, std::string> relPathToDirKey;

    for (const auto& sf : stagedFiles) {
        if (sf.explicitCompLower.empty()) {
            const bool isUserDir = IsPerUserProfileRootDir(sf.targetRoot);
            if (isUserDir) {
                // Microsoft ICE38 Rule: auto-component in user-profile folder gets HKCU Registry KeyPath!
                ++autoHkcuKpCounter;
                const std::string autoRk = "r_kp_usr_" + std::to_string(autoHkcuKpCounter);
                msi_sql::InsertRow(
                    hDb.Get(), "Component",
                    {"Component", "ComponentId", "Directory_", "Attributes", "Condition", "KeyPath"},
                    {sf.compKey, sf.compGuid, sf.dirKey, kCompAttr64BitReg, std::monostate{}, autoRk},
                    outError);
                msi_sql::InsertRow(
                    hDb.Get(), "Registry",
                    {"Registry", "Root", "Key", "Name", "Value", "Component_"},
                    {autoRk, 1 /* HKCU */, "Software\\" + outSummary.manufacturer + "\\" + outSummary.productName + "\\UserFiles",
                     sf.fileKey, std::string("#1"), sf.compKey},
                    outError);
                emitUserDirCleanupIfNeeded(sf.dirKey, sf.compKey);
            } else {
                msi_sql::InsertRow(
                    hDb.Get(), "Component",
                    {"Component", "ComponentId", "Directory_", "Attributes", "Condition", "KeyPath"},
                    {sf.compKey, sf.compGuid, sf.dirKey, kCompAttr64BitFile, std::monostate{}, sf.fileKey},
                    outError);
            }
            ++totalComponents;

            msi_sql::InsertRow(
                hDb.Get(), "FeatureComponents",
                {"Feature_", "Component_"},
                {sf.featureId, sf.compKey},
                outError);
        }

        msi_sql::InsertRow(
            hDb.Get(), "File",
            {"File", "Component_", "FileName", "FileSize", "Version", "Language", "Attributes", "Sequence"},
            {
                sf.fileKey,
                sf.compKey,
                sf.msiFileName,
                sf.fileSize,
                sf.peVersion.empty() ? SqlParam{std::monostate{}} : SqlParam{sf.peVersion},
                sf.peLanguage.empty() ? SqlParam{std::monostate{}} : SqlParam{sf.peLanguage},
                512 /* Vital */,
                sf.sequence
            },
            outError);

        if (EqualsIgnoreCase(sf.targetRoot, "FontsFolder")) {
            const std::string extLow = AsciiLower(WideToUtf8(sf.sourcePath.extension().wstring()));
            if (extLow == ".ttf" || extLow == ".otf" || extLow == ".ttc") {
                msi_sql::InsertRow(hDb.Get(), "Font", {"File_", "FontTitle"}, {sf.fileKey, std::monostate{}}, outError);
            }
        }

        if (EqualsIgnoreCase(sf.targetRoot, "INSTALLDIR")) {
            const std::string lowRel = AsciiLower(sf.relInstallPath);
            relPathToFileKey[lowRel] = sf.fileKey;
            relPathToCompKey[lowRel] = sf.compKey;
            relPathToDirKey[lowRel]  = sf.dirKey;
        }
    }

    // 5C. Emit `[Folder.<id>]` `CreateFolder` + `RemoveFile` rows!
    std::size_t folderCompCounter = 0;
    for (const auto& fl : explicitFolders) {
        std::string fCompKey;
        if (!fl.explicitCompLower.empty() && explicitComps.count(fl.explicitCompLower)) {
            fCompKey = explicitComps[fl.explicitCompLower].msiCompKey;
            msi_sql::InsertRow(hDb.Get(), "CreateFolder", {"Directory_", "Component_"}, {fl.dirKey, fCompKey}, outError);
        } else {
            ++folderCompCounter;
            fCompKey = "c_fld_" + std::to_string(folderCompCounter);
            const std::string fGuid = ComputeDeterministicComponentGuid(outSummary.upgradeCode, arch, "folder:" + AsciiLower(fl.id));

            bool isUserSub = false;
            for (const auto& dn : directories) {
                if (dn.key == fl.dirKey && dn.isUserProfileSubfolder) { isUserSub = true; break; }
            }
            if (isUserSub) {
                ++autoHkcuKpCounter;
                const std::string rk = "r_kp_fld_" + std::to_string(autoHkcuKpCounter);
                msi_sql::InsertRow(
                    hDb.Get(), "Component",
                    {"Component", "ComponentId", "Directory_", "Attributes", "Condition", "KeyPath"},
                    {fCompKey, fGuid, fl.dirKey, kCompAttr64BitReg, std::monostate{}, rk},
                    outError);
                msi_sql::InsertRow(
                    hDb.Get(), "Registry",
                    {"Registry", "Root", "Key", "Name", "Value", "Component_"},
                    {rk, 1, "Software\\" + outSummary.manufacturer + "\\" + outSummary.productName + "\\Folders",
                     fl.id, std::string("#1"), fCompKey},
                    outError);
            } else {
                msi_sql::InsertRow(
                    hDb.Get(), "Component",
                    {"Component", "ComponentId", "Directory_", "Attributes", "Condition", "KeyPath"},
                    {fCompKey, fGuid, fl.dirKey, kCompAttr64BitFile, std::monostate{}, std::monostate{}},
                    outError);
            }
            ++totalComponents;
            msi_sql::InsertRow(hDb.Get(), "FeatureComponents", {"Feature_", "Component_"}, {fl.featureId, fCompKey}, outError);
            msi_sql::InsertRow(hDb.Get(), "CreateFolder", {"Directory_", "Component_"}, {fl.dirKey, fCompKey}, outError);
        }

        if (fl.removeOnUninstall) {
            ++removeFileSeq;
            msi_sql::InsertRow(
                hDb.Get(), "RemoveFile",
                {"FileKey", "Component_", "FileName", "DirProperty", "InstallMode"},
                {"rf_fld_" + std::to_string(removeFileSeq), fCompKey, std::monostate{}, fl.dirKey, 2},
                outError);
        }
    }

    // 5D. Emit `[Shortcut.<id>]` rows (conditioning Desktop shortcuts on `DESKTOP_SHORTCUT <> "0"`!)
    std::size_t scCounter = 0;
    for (const auto& sid : shortcutIds) {
        ++scCounter;
        const std::string scName = ExpandArchToken(doc.GetString("Shortcut", sid, "Name"), arch);
        std::string targetRel    = ExpandArchToken(doc.GetString("Shortcut", sid, "TargetFile"), arch);
        for (char& c : targetRel) if (c == '\\') c = '/';
        while (!targetRel.empty() && targetRel.front() == '/') targetRel.erase(targetRel.begin());

        auto fIt = relPathToFileKey.find(AsciiLower(targetRel));
        if (fIt == relPathToFileKey.end()) {
            std::filesystem::remove_all(scratchDir, ec);
            outError = "Shortcut '" + sid + "' TargetFile '" + targetRel + "' does not match any [Payload.*] file under INSTALLDIR.";
            return false;
        }

        const std::string loc     = doc.GetString("Shortcut", sid, "Location", "StartMenu");
        const bool isDesktop      = EqualsIgnoreCase(loc, "Desktop");
        const std::string scDir   = isDesktop ? "DesktopFolder" : "ApplicationProgramsFolder";
        const std::string featId  = ensureFeatureExists(doc.GetString("Shortcut", sid, "Feature", features.front().id));
        const std::string wkDir   = relPathToDirKey[AsciiLower(targetRel)];
        const std::string scKey   = "sc_" + std::to_string(scCounter);
        const std::string scComp  = "c_sc_" + std::to_string(scCounter);
        const std::string regKeyId= "reg_sc_" + std::to_string(scCounter);
        const std::string scGuid  = ComputeDeterministicComponentGuid(outSummary.upgradeCode, arch, "shortcut:" + sid);

        msi_sql::InsertRow(
            hDb.Get(), "Component",
            {"Component", "ComponentId", "Directory_", "Attributes", "Condition", "KeyPath"},
            {scComp, scGuid, scDir, kCompAttr64BitReg,
             isDesktop ? SqlParam{std::string("DESKTOP_SHORTCUT <> \"0\"")} : SqlParam{std::monostate{}},
             regKeyId},
            outError);
        ++totalComponents;

        msi_sql::InsertRow(hDb.Get(), "FeatureComponents", {"Feature_", "Component_"}, {featId, scComp}, outError);
        msi_sql::InsertRow(
            hDb.Get(), "Registry",
            {"Registry", "Root", "Key", "Name", "Value", "Component_"},
            {regKeyId, 1 /* HKCU */, "Software\\" + outSummary.manufacturer + "\\" + outSummary.productName + "\\Shortcuts",
             sid, std::string("#1"), scComp},
            outError);

        if (!isDesktop) {
            ++removeFileSeq;
            msi_sql::InsertRow(
                hDb.Get(), "RemoveFile",
                {"FileKey", "Component_", "FileName", "DirProperty", "InstallMode"},
                {"rf_sc_" + std::to_string(scCounter), scComp, std::monostate{}, std::string("ApplicationProgramsFolder"), 2},
                outError);
        }

        const std::string args = ExpandArchToken(doc.GetString("Shortcut", sid, "Arguments"), arch);
        const std::string desc = ExpandArchToken(doc.GetString("Shortcut", sid, "Description", scName), arch);

        msi_sql::InsertRow(
            hDb.Get(), "Shortcut",
            {"Shortcut", "Directory_", "Name", "Component_", "Target", "Arguments", "Description", "Hotkey", "Icon_", "IconIndex", "ShowCmd", "WkDir"},
            {
                scKey, scDir, FormatMsiShortLongName(scName, 500 + scCounter), scComp,
                "[#" + fIt->second + "]",
                args.empty() ? SqlParam{std::monostate{}} : SqlParam{args},
                desc, 0,
                hasProductIcon ? SqlParam{std::string("AppProductIcon.ico")} : SqlParam{std::monostate{}},
                0, 1, wkDir
            },
            outError);
    }

    // 5E. Emit `[Registry.<id>]` rows
    for (const auto& pr : pendingRegs) {
        std::string rComp;
        if (!pr.explicitCompLower.empty() && explicitComps.count(pr.explicitCompLower)) {
            rComp = explicitComps[pr.explicitCompLower].msiCompKey;
        } else {
            rComp = "c_" + pr.msiRegKey;
            const std::string rGuid = ComputeDeterministicComponentGuid(
                outSummary.upgradeCode, arch, "reg:" + std::to_string(pr.rootNum) + "\\" + pr.keyPath + "\\" + pr.valName);
            msi_sql::InsertRow(
                hDb.Get(), "Component",
                {"Component", "ComponentId", "Directory_", "Attributes", "Condition", "KeyPath"},
                {rComp, rGuid, std::string("INSTALLDIR"), kCompAttr64BitReg, std::monostate{}, pr.msiRegKey},
                outError);
            ++totalComponents;
            msi_sql::InsertRow(hDb.Get(), "FeatureComponents", {"Feature_", "Component_"}, {pr.featureId, rComp}, outError);
        }

        msi_sql::InsertRow(
            hDb.Get(), "Registry",
            {"Registry", "Root", "Key", "Name", "Value", "Component_"},
            {pr.msiRegKey, pr.rootNum, pr.keyPath,
             (pr.valName == "@") ? SqlParam{std::monostate{}} : SqlParam{pr.valName},
             pr.formattedVal, rComp},
            outError);
    }

    // 5F. Emit `[FileAssociation.<id>]` (Both Clean `HKMU\Software\Classes` AND Native `ProgId`/`Extension`/`Verb`/`MIME`!)
    std::size_t assocCounter = 0;
    std::set<std::string> emittedProgIds;
    std::set<std::string> emittedMimes;
    for (const auto& aid : fileAssocIds) {
        ++assocCounter;
        std::string ext = TrimAscii(ExpandArchToken(doc.GetString("FileAssociation", aid, "Extension"), arch));
        if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());

        const std::string progId   = TrimAscii(ExpandArchToken(doc.GetString("FileAssociation", aid, "ProgId"), arch));
        const std::string desc     = ExpandArchToken(doc.GetString("FileAssociation", aid, "Description", progId), arch);
        std::string targetRel      = ExpandArchToken(doc.GetString("FileAssociation", aid, "TargetFile"), arch);
        for (char& c : targetRel) if (c == '\\') c = '/';
        while (!targetRel.empty() && targetRel.front() == '/') targetRel.erase(targetRel.begin());

        auto fIt = relPathToFileKey.find(AsciiLower(targetRel));
        if (fIt == relPathToFileKey.end()) {
            std::filesystem::remove_all(scratchDir, ec);
            outError = "FileAssociation '" + aid + "' TargetFile '" + targetRel + "' does not match any [Payload.*] file under INSTALLDIR.";
            return false;
        }
        const std::string targetFileKey = fIt->second;
        const std::string targetCompKey = relPathToCompKey[AsciiLower(targetRel)];

        const std::string verbsRaw = ExpandArchToken(doc.GetString("FileAssociation", aid, "Verbs", "open:&Open:\"%1\""), arch);
        const std::string mimeType = TrimAscii(doc.GetString("FileAssociation", aid, "ContentType"));
        const std::string percType = TrimAscii(doc.GetString("FileAssociation", aid, "PerceivedType"));
        const int iconIdx          = doc.GetInt("FileAssociation", aid, "IconIndex", 0);
        const bool advertise       = doc.GetBool("FileAssociation", aid, "Advertise", false);
        const std::string featId   = ensureFeatureExists(doc.GetString("FileAssociation", aid, "Feature", features.front().id));

        // Parse pipe-separated `verbId:MenuCaption:Arguments` items
        struct ParsedVerb { std::string id; std::string caption; std::string args; };
        std::vector<ParsedVerb> verbs;
        {
            std::size_t pos = 0;
            while (pos <= verbsRaw.size()) {
                auto bar = verbsRaw.find('|', pos);
                std::string seg = TrimAscii(bar == std::string::npos ? verbsRaw.substr(pos) : verbsRaw.substr(pos, bar - pos));
                if (!seg.empty()) {
                    auto c1 = seg.find(':');
                    auto c2 = (c1 == std::string::npos) ? std::string::npos : seg.find(':', c1 + 1);
                    if (c1 == std::string::npos) {
                        verbs.push_back({seg, "&Open", "\"%1\""});
                    } else if (c2 == std::string::npos) {
                        verbs.push_back({TrimAscii(seg.substr(0, c1)), TrimAscii(seg.substr(c1 + 1)), "\"%1\""});
                    } else {
                        verbs.push_back({TrimAscii(seg.substr(0, c1)), TrimAscii(seg.substr(c1 + 1, c2 - c1 - 1)), TrimAscii(seg.substr(c2 + 1))});
                    }
                }
                if (bar == std::string::npos) break;
                pos = bar + 1;
            }
            if (verbs.empty()) verbs.push_back({"open", "&Open", "\"%1\""});
        }

        if (advertise) {
            // Native Windows Installer `ProgId`, `Extension`, `Verb`, and `MIME` SQL tables!
            if (emittedProgIds.insert(AsciiLower(progId)).second) {
                msi_sql::InsertRow(
                    hDb.Get(), "ProgId",
                    {"ProgId", "ProgId_Parent", "Class_", "Description", "Icon_", "IconIndex"},
                    {progId, std::monostate{}, std::monostate{}, desc,
                     hasProductIcon ? SqlParam{std::string("AppProductIcon.ico")} : SqlParam{std::monostate{}},
                     hasProductIcon ? SqlParam{iconIdx} : SqlParam{std::monostate{}}},
                    outError);
            }
            msi_sql::InsertRow(
                hDb.Get(), "Extension",
                {"Extension", "Component_", "ProgId_", "MIME_", "Feature_"},
                {ext, targetCompKey, progId,
                 mimeType.empty() ? SqlParam{std::monostate{}} : SqlParam{mimeType},
                 featId},
                outError);
            int vSeq = 1;
            for (const auto& vb : verbs) {
                msi_sql::InsertRow(
                    hDb.Get(), "Verb",
                    {"Extension_", "Verb", "Sequence", "Command", "Argument"},
                    {ext, vb.id, vSeq++, vb.caption, vb.args},
                    outError);
            }
            if (!mimeType.empty() && emittedMimes.insert(AsciiLower(mimeType)).second) {
                msi_sql::InsertRow(
                    hDb.Get(), "MIME",
                    {"ContentType", "Extension_", "CLSID"},
                    {mimeType, ext, std::monostate{}},
                    outError);
            }
        } else {
            // Clean Non-Advertised `HKMU\Software\Classes` (`Root = -1`) Registration!
            // Works identically for Per-Machine (`HKLM\Software\Classes`) AND Per-User (`HKCU\Software\Classes`),
            // honors `REGISTER_ASSOC <> "0"`, and NEVER triggers MSI self-repair popups on double-click!
            const std::string faComp = "c_fa_" + std::to_string(assocCounter);
            const std::string faGuid = ComputeDeterministicComponentGuid(outSummary.upgradeCode, arch, "fileassoc:." + AsciiLower(ext));
            const std::string kpReg  = "r_fa_" + std::to_string(assocCounter) + "_kp";

            msi_sql::InsertRow(
                hDb.Get(), "Component",
                {"Component", "ComponentId", "Directory_", "Attributes", "Condition", "KeyPath"},
                {faComp, faGuid, std::string("INSTALLDIR"), kCompAttr64BitReg, std::string("REGISTER_ASSOC <> \"0\""), kpReg},
                outError);
            ++totalComponents;
            msi_sql::InsertRow(hDb.Get(), "FeatureComponents", {"Feature_", "Component_"}, {featId, faComp}, outError);

            int subIdx = 0;
            auto addClassesReg = [&](const std::string& rowId, const std::string& relClassesKey, SqlParam name, const std::string& val) {
                msi_sql::InsertRow(
                    hDb.Get(), "Registry",
                    {"Registry", "Root", "Key", "Name", "Value", "Component_"},
                    {rowId, -1 /* HKMU */, "Software\\Classes\\" + relClassesKey, std::move(name), val, faComp},
                    outError);
            };

            const std::string pfx = "r_fa_" + std::to_string(assocCounter) + "_";
            addClassesReg(kpReg, "." + ext, std::monostate{}, progId);
            addClassesReg(pfx + std::to_string(++subIdx), "." + ext + "\\OpenWithProgids", progId, "");
            if (!mimeType.empty()) {
                addClassesReg(pfx + std::to_string(++subIdx), "." + ext, std::string("Content Type"), mimeType);
                addClassesReg(pfx + std::to_string(++subIdx), "MIME\\Database\\Content Type\\" + mimeType, std::string("Extension"), "." + ext);
            }
            if (!percType.empty()) {
                addClassesReg(pfx + std::to_string(++subIdx), "." + ext, std::string("PerceivedType"), percType);
            }
            addClassesReg(pfx + std::to_string(++subIdx), progId, std::monostate{}, desc);
            addClassesReg(pfx + std::to_string(++subIdx), progId + "\\DefaultIcon", std::monostate{},
                          "[#" + targetFileKey + "]," + std::to_string(iconIdx));

            for (const auto& vb : verbs) {
                addClassesReg(pfx + std::to_string(++subIdx), progId + "\\shell\\" + vb.id, std::monostate{}, vb.caption);
                addClassesReg(pfx + std::to_string(++subIdx), progId + "\\shell\\" + vb.id + "\\command", std::monostate{},
                              "\"[#" + targetFileKey + "]\" " + vb.args);
            }
        }
    }

    // Merge any `[MergeModule.<id>]` (`.msm`) databases into `hDb` & `cabInputs` BEFORE packing `#payload.cab`!
    std::size_t totalMergedFiles = 0;
    int nextCabSeq = static_cast<int>(stagedFiles.size()) + 1;
    for (const auto& mid : mergeModuleIds) {
        const auto msmPath  = resolvePath(doc.GetString("MergeModule", mid, "Source"));
        const auto msmFeat  = ensureFeatureExists(doc.GetString("MergeModule", mid, "Feature", features.front().id));
        const auto msmRoot  = ensureSystemRootRegistered(doc.GetString("MergeModule", mid, "TargetRoot", "INSTALLDIR"));
        if (!MergeMsmIntoMsiDatabase(
                hDb.Get(), msmPath, msmFeat, msmRoot, scratchDir,
                registeredDirKeys, cabInputs, nextCabSeq,
                totalComponents, totalMergedFiles, outError)) {
            std::filesystem::remove_all(scratchDir, ec);
            return false;
        }
    }

    outSummary.fileCount      = stagedFiles.size() + totalMergedFiles;
    outSummary.componentCount = totalComponents;

    // Pack all `[Payload.*]` + merged `[MergeModule.*]` files into unified `LZX:21` `#payload.cab`!
    if (!cabInputs.empty() && !BuildPayloadCabinet(cabInputs, cabPath, useLzx, outError)) {
        std::filesystem::remove_all(scratchDir, ec);
        return false;
    }

    // `Media` & `_Streams` (`payload.cab`)
    msi_sql::InsertRow(
        hDb.Get(), "Media",
        {"DiskId", "LastSequence", "DiskPrompt", "Cabinet", "VolumeLabel", "Source"},
        {1, static_cast<int>(cabInputs.size()), std::monostate{}, std::string("#payload.cab"), std::monostate{}, std::monostate{}},
        outError);

    if ((!cabInputs.empty() && !msi_sql::InsertBinaryStreamRow(hDb.Get(), "_Streams", "Name", "Data", "payload.cab", cabPath, outError)) ||
        !msi_sql::InsertBinaryStreamRow(hDb.Get(), "Binary", "Name", "Data", "RswBannerBmp", bannerBmpPath, outError) ||
        !msi_sql::InsertBinaryStreamRow(hDb.Get(), "Binary", "Name", "Data", "RswWelcomeBmp", welcomeBmpPath, outError)) {
        std::filesystem::remove_all(scratchDir, ec);
        return false;
    }

    if (registeredDirKeys.count("ApplicationProgramsFolder")) {
        // Type 35 (`msidbCustomActionTypeDirectory | msidbCustomActionTypeTextData`) Custom Action:
        // Applies custom `STARTMENUFOLDER` chosen in `SetupWizard.exe` (`Shortcuts & Integration` page)
        // directly onto `ApplicationProgramsFolder` after `CostFinalize`!
        msi_sql::InsertRow(
            hDb.Get(), "CustomAction",
            {"Action", "Type", "Source", "Target"},
            {std::string("RswApplyStartMenuFolder"), 35, std::string("ApplicationProgramsFolder"), std::string("[ProgramMenuFolder][STARTMENUFOLDER]")},
            outError);
    }

    std::string mainExeFileKey;
    if (!outSummary.mainExeRel.empty()) {
        std::string normMain = outSummary.mainExeRel;
        for (char& c : normMain) if (c == '\\') c = '/';
        auto it = relPathToFileKey.find(AsciiLower(normMain));
        if (it != relPathToFileKey.end()) {
            mainExeFileKey = it->second;
            msi_sql::InsertRow(
                hDb.Get(), "CustomAction",
                {"Action", "Type", "Source", "Target"},
                {std::string("RswLaunchMainApp"), 210, mainExeFileKey, std::string("")},
                outError);
        }
    }

    // -------------------------------------------------------------------------
    // STEP 6: Populate Internal `MSI.DLL` UI Tables (`RswMsiDialogBuilder.hpp`)!
    // -------------------------------------------------------------------------
    MsiUiBuildOptions uiOpt{};
    uiOpt.perMachine          = outSummary.perMachine;
    uiOpt.hasLicensePage      = !outSummary.licenseRtfPath.empty();
    uiOpt.offerLaunchOnFinish = outSummary.offerLaunchOnFinish;
    uiOpt.mainExeFileKey      = mainExeFileKey;
    uiOpt.licenseRtfContent   = LoadOrSynthesizeLicenseRtf(outSummary.licenseRtfPath, outSummary.productName, outSummary.manufacturer);
    uiOpt.welcomeHeadline     = doc.GetString("UI", "", "WelcomeHeadline", "Welcome to the [ProductName] Setup Wizard");
    uiOpt.bannerSubtitle      = doc.GetString("UI", "", "BannerTitle", "Ready to install [ProductName] on your computer.");
    uiOpt.installBtnText      = outSummary.installBtnText;
    uiOpt.launchChkText       = outSummary.launchChkText;
    uiOpt.expressDesc         = doc.GetString("UI", "", "ExpressDescription",
                                  "One-click setup into the default installation folder with recommended shortcuts.");
    uiOpt.compactDesc         = doc.GetString("UI", "", "CompactDescription",
                                  "Installs essential core application files with minimal disk footprint.");
    uiOpt.customDesc          = doc.GetString("UI", "", "CustomDescription",
                                  "Choose features, destination folder, and shortcuts step by step.");

    PopulateMsiInternalUiTables(
        hDb.Get(), doc, uiOpt,
        [](MSIHANDLE db, std::string_view tbl, const std::vector<std::string_view>& cols,
           const std::vector<SqlParam>& vals, std::string& err) {
            return msi_sql::InsertRow(db, tbl, cols, vals, err);
        },
        secureCustomProps, outError);

    // Finalize `SecureCustomProperties` containing all built-in + custom `[MsiControl.*]` properties!
    {
        std::string joinedProps;
        for (const auto& sp : secureCustomProps) {
            if (!joinedProps.empty()) joinedProps += ";";
            joinedProps += sp;
        }
        addProp("SecureCustomProperties", std::move(joinedProps));
    }

    // Populate `ActionText` with BOTH plain `Description` (never containing raw `[1]`!)
    // AND rich `Template` patterns (`File: [1]\r\nDirectory: [9]\r\nSize: [6] bytes`) formatted
    // live by `MSI.DLL`'s `ActionData` `EventMapping` on `ProgressDlg` AND by `MsiFormatRecordW`
    // inside `SetupWizard.exe`!
    struct ActionTextRow { const char* action; const char* desc; const char* tmpl; };
    const ActionTextRow actionTexts[] = {
        {"AppSearch",               "Searching for installed applications...",    "Property: [1]"},
        {"LaunchConditions",        "Evaluating launch conditions...",            nullptr},
        {"CostInitialize",          "Computing disk space requirements...",       nullptr},
        {"FileCost",                "Computing disk space requirements...",       nullptr},
        {"CostFinalize",            "Computing disk space requirements...",       nullptr},
        {"InstallValidate",         "Validating installation...",                 nullptr},
        {"RemoveExistingProducts",  "Removing previous version...",               "Application: [1]"},
        {"ProcessComponents",       "Updating component registration...",         nullptr},
        {"UnpublishFeatures",       "Unpublishing product features...",           "Feature: [1]"},
        {"RemoveRegistryValues",    "Removing system registry values...",         "Key: [1]\r\nName: [2]"},
        {"RemoveShortcuts",         "Removing shortcuts...",                      "Shortcut: [1]"},
        {"RemoveFiles",             "Removing files...",                          "File: [1]\r\nDirectory: [9]"},
        {"RemoveFolders",           "Removing folders...",                        "Folder: [1]"},
        {"CreateFolders",           "Creating application folders...",            "Folder: [1]"},
        {"MoveFiles",               "Moving files...",                            "File: [1]\r\nDirectory: [9]\r\nSize: [6] bytes"},
        {"InstallFiles",            "Copying new files...",                       "File: [1]\r\nDirectory: [9]\r\nSize: [6] bytes"},
        {"DuplicateFiles",          "Creating duplicate files...",                "File: [1]\r\nDirectory: [9]\r\nSize: [6] bytes"},
        {"PatchFiles",              "Patching files...",                          "File: [1]\r\nDirectory: [2]\r\nSize: [3] bytes"},
        {"RegisterFonts",           "Registering fonts...",                       "Font: [1]"},
        {"UnregisterFonts",         "Unregistering fonts...",                     "Font: [1]"},
        {"CreateShortcuts",         "Creating shortcuts...",                      "Shortcut: [1]"},
        {"WriteRegistryValues",     "Writing system registry values...",          "Key: [1]\r\nName: [2]"},
        {"RegisterProgIdInfo",      "Registering document types...",              "ProgId: [1]"},
        {"UnregisterProgIdInfo",    "Unregistering document types...",            "ProgId: [1]"},
        {"RegisterExtensionInfo",   "Registering file extensions...",             "Extension: [1]"},
        {"UnregisterExtensionInfo", "Unregistering file extensions...",           "Extension: [1]"},
        {"RegisterMIMEInfo",        "Registering MIME content types...",          "MIME Type: [1]"},
        {"UnregisterMIMEInfo",      "Unregistering MIME content types...",        "MIME Type: [1]"},
        {"RegisterUser",            "Registering user information...",            "[1]"},
        {"RegisterProduct",         "Registering product...",                     "[1]"},
        {"PublishFeatures",         "Publishing product features...",             "Feature: [1]"},
        {"PublishProduct",          "Publishing product information...",          nullptr},
        {"InstallAdminPackage",     "Copying network installation files...",      "File: [1]\r\nDirectory: [9]\r\nSize: [6] bytes"},
        {"GenerateScript",          "Generating script operations for action:",   "[1]"},
        {"Rollback",                "Rolling back action:",                       "[1]"},
        {"RollbackCleanup",         "Removing backup files...",                   "File: [1]"}
    };
    for (const auto& at : actionTexts) {
        msi_sql::InsertRow(
            hDb.Get(), "ActionText", {"Action", "Description", "Template"},
            {std::string(at.action), std::string(at.desc),
             at.tmpl ? SqlParam{std::string(at.tmpl)} : SqlParam{std::monostate{}}},
            outError);
    }

    // -------------------------------------------------------------------------
    // STEP 7: Populate `InstallUISequence`, `InstallExecuteSequence`, & `AdminExecuteSequence`
    // -------------------------------------------------------------------------
    struct SeqEntry { const char* action; const char* cond; int seq; };
    const SeqEntry uiSeq[] = {
        {"FindRelatedProducts",    nullptr, 100},
        {"AppSearch",              nullptr, 200},
        {"LaunchConditions",       nullptr, 400},
        {"ValidateProductID",      nullptr, 700},
        {"CostInitialize",         nullptr, 800},
        {"FileCost",               nullptr, 900},
        {"CostFinalize",           nullptr, 1000},
        {"MigrateFeatureStates",   nullptr, 1200},
        {"WelcomeDlg",             "NOT Installed", 1290},
        {"MaintenanceWelcomeDlg",  "Installed AND NOT RESUME AND NOT Preselected", 1295},
        {"ProgressDlg",            nullptr, 1298},
        {"ExecuteAction",          nullptr, 1300},
        {"ExitDialog",             nullptr, -1},
        {"UserExit",               nullptr, -2},
        {"FatalError",             nullptr, -3}
    };
    for (const auto& s : uiSeq) {
        msi_sql::InsertRow(
            hDb.Get(), "InstallUISequence", {"Action", "Condition", "Sequence"},
            {std::string(s.action), s.cond ? SqlParam{std::string(s.cond)} : SqlParam{std::monostate{}}, s.seq},
            outError);
    }

    std::vector<SeqEntry> execSeq = {
        {"FindRelatedProducts",    nullptr, 100},
        {"AppSearch",              nullptr, 200},
        {"LaunchConditions",       nullptr, 400},
        {"ValidateProductID",      nullptr, 700},
        {"CostInitialize",         nullptr, 800},
        {"FileCost",               nullptr, 900},
        {"CostFinalize",           nullptr, 1000},
        {"InstallValidate",        nullptr, 1400},
        {"InstallInitialize",      nullptr, 1500},
        {"RemoveExistingProducts", nullptr, 1525},
        {"ProcessComponents",      nullptr, 1600},
        {"UnpublishFeatures",      nullptr, 1800},
        {"RemoveRegistryValues",   nullptr, 2600},
        {"RemoveShortcuts",        nullptr, 3200},
        {"RemoveFiles",            nullptr, 3500},
        {"RemoveFolders",          nullptr, 3600},
        {"CreateFolders",          nullptr, 3700},
        {"InstallFiles",           nullptr, 4000},
        {"CreateShortcuts",        nullptr, 4500},
        {"WriteRegistryValues",    nullptr, 5000},
        {"RegisterUser",           nullptr, 6000},
        {"RegisterProduct",        nullptr, 6100},
        {"PublishFeatures",        nullptr, 6300},
        {"PublishProduct",         nullptr, 6400},
        {"InstallFinalize",        nullptr, 6600}
    };
    if (registeredDirKeys.count("ApplicationProgramsFolder")) {
        execSeq.push_back({"RswApplyStartMenuFolder", "STARTMENUFOLDER AND NOT Installed", 1050});
    }
    if (hasFontFiles) {
        execSeq.push_back({"UnregisterFonts", nullptr, 3400});
        execSeq.push_back({"RegisterFonts",   nullptr, 5300});
    }
    if (hasAdvertisedAssoc) {
        execSeq.push_back({"UnregisterMIMEInfo",      nullptr, 2700});
        execSeq.push_back({"UnregisterExtensionInfo", nullptr, 2800});
        execSeq.push_back({"UnregisterProgIdInfo",    nullptr, 2900});
        execSeq.push_back({"RegisterProgIdInfo",      nullptr, 4700});
        execSeq.push_back({"RegisterExtensionInfo",   nullptr, 4800});
        execSeq.push_back({"RegisterMIMEInfo",        nullptr, 4900});
    }
    for (const auto& s : execSeq) {
        msi_sql::InsertRow(
            hDb.Get(), "InstallExecuteSequence", {"Action", "Condition", "Sequence"},
            {std::string(s.action), s.cond ? SqlParam{std::string(s.cond)} : SqlParam{std::monostate{}}, s.seq},
            outError);
    }

    const SeqEntry adminSeq[] = {
        {"CostInitialize",      nullptr, 800},
        {"FileCost",            nullptr, 900},
        {"CostFinalize",        nullptr, 1000},
        {"InstallValidate",     nullptr, 1400},
        {"InstallInitialize",   nullptr, 1500},
        {"InstallAdminPackage", nullptr, 3900},
        {"InstallFiles",        nullptr, 4000},
        {"InstallFinalize",     nullptr, 6600}
    };
    for (const auto& s : adminSeq) {
        msi_sql::InsertRow(
            hDb.Get(), "AdminExecuteSequence", {"Action", "Condition", "Sequence"},
            {std::string(s.action), std::monostate{}, s.seq},
            outError);
    }

    // -------------------------------------------------------------------------
    // STEP 8: Write OLE `SummaryInformation` Stream & Commit!
    // -------------------------------------------------------------------------
    {
        ScopedMsiHandle hSum;
        rc = ::MsiGetSummaryInformationW(hDb.Get(), nullptr, 20, hSum.Put());
        if (rc != ERROR_SUCCESS) {
            std::filesystem::remove_all(scratchDir, ec);
            outError = "MsiGetSummaryInformationW failed (" + std::to_string(rc) + ").";
            return false;
        }

        const std::wstring wTemplate = (arch == "arm64")
            ? (L"Arm64;" + std::to_wstring(lcid))
            : (L"x64;" + std::to_wstring(lcid));
        const std::wstring wSubject  = Utf8ToWide(outSummary.productName);
        const std::wstring wAuthor   = Utf8ToWide(outSummary.manufacturer);
        const std::wstring wComments = Utf8ToWide(
            doc.GetString("Package", "", "Description", "This installer database contains the logic and data required to install " + outSummary.productName + "."));
        const std::wstring wPkgCode  = Utf8ToWide(outSummary.packageCode);

        FILETIME nowFt{};
        ::GetSystemTimeAsFileTime(&nowFt);

        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 1,  VT_I2,       1252, &nowFt, nullptr);
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 2,  VT_LPSTR,    0,    &nowFt, L"Installation Database");
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 3,  VT_LPSTR,    0,    &nowFt, wSubject.c_str());
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 4,  VT_LPSTR,    0,    &nowFt, wAuthor.c_str());
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 5,  VT_LPSTR,    0,    &nowFt, L"Installer, MSI, Database");
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 6,  VT_LPSTR,    0,    &nowFt, wComments.c_str());
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 7,  VT_LPSTR,    0,    &nowFt, wTemplate.c_str());
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 9,  VT_LPSTR,    0,    &nowFt, wPkgCode.c_str());
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 12, VT_FILETIME, 0,    &nowFt, nullptr);
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 13, VT_FILETIME, 0,    &nowFt, nullptr);
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 14, VT_I4,       500,  &nowFt, nullptr);
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 15, VT_I4,       2,    &nowFt, nullptr);
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 18, VT_LPSTR,    0,    &nowFt, kGeneratorSigWide.data());
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 19, VT_I4,       2,    &nowFt, nullptr);

        if (::MsiSummaryInfoPersist(hSum.Get()) != ERROR_SUCCESS) {
            std::filesystem::remove_all(scratchDir, ec);
            outError = "MsiSummaryInfoPersist failed.";
            return false;
        }
    }

    rc = ::MsiDatabaseCommit(hDb.Get());
    hDb.Reset();
    std::filesystem::remove_all(scratchDir, ec);

    if (rc != ERROR_SUCCESS) {
        outError = "MsiDatabaseCommit failed (" + std::to_string(rc) + ").";
        return false;
    }

    return VerifyCompiledMsiDatabase(outSummary.msiPath, outError);
}

} // namespace rsw
