// ================================================================================
// FILE: src/compiler/RswMsiCompiler.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   How does an `.msi` file actually work under the hood?
//     A Windows Installer (`.msi`) file is NOT an executable script—it is a
//     structured **relational SQL database** stored inside a Microsoft Compound
//     File (`msi.dll`), containing normalized tables (`Property`, `Directory`,
//     `Component`, `File`, `Feature`, `Shortcut`, `Registry`, `Upgrade`, `Dialog`,
//     `Control`, `InstallExecuteSequence`, etc.) plus binary `_Streams` (`payload.cab`)
//     and an OLE `SummaryInformation` property stream!
//
//     Rather than reverse-engineering undocumented binary OLE sectors or relying
//     on third-party WiX / FireGiant toolsets, `RswMsiCompiler` calls Microsoft's
//     official Windows SDK C API (`msiquery.h` / `msi.lib`) directly:
//       1. `MsiOpenDatabaseW(..., MSIDBOPEN_CREATEDIRECT, &hDb)` creates a fresh
//          Windows Installer 5.0 database.
//       2. `ExecuteSqlDdl` runs backtick-quoted `CREATE TABLE` DDL statements for
//          every relational table AND populates `_Validation` metadata so Microsoft
//          ICE (`darice.cub`) rules pass cleanly.
//       3. `BuildPayloadCabinet` (`RswCabBuilder.hpp`) compresses all harvested
//          files into `#payload.cab` and streams it into `_Streams` (`payload.cab`).
//       4. Populates classic **Windows 2000 "Wizard 97"** internal UI tables so
//          running `Product.msi` directly shows crisp interactive setup dialogs,
//          while `msiexec /i Product.msi /qn` runs 100% silently in Intune/GPO!
//       5. Writes the `SummaryInformation` stream (`x64;1033` or `Arm64;1033`,
//          schema `500`, compressed source wordcount `2`) and commits to disk.
//       6. Immediately re-opens the committed `.msi` in read-only mode to verify
//          relational foreign keys, 64-bit ICE80 rules, and ICE64 `RemoveFolder`!
// ================================================================================

#pragma once

#include "../common/RswCryptoAndPe.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswVersion.hpp"
#include "RswCabBuilder.hpp"
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

/// RAII wrapper around `MSIHANDLE` (`PMSIHANDLE` equivalent without pulling in ATL/MFC).
/// Automatically calls `MsiCloseHandle` on scope exit so zero MSI handles ever leak!
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

struct StreamPathTag {
    std::filesystem::path filePath;
};

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

/// Converts a product name into a clean lowercase `kebab-case` filename slug.
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

/// Converts plain text or reads an existing `.rtf` file into valid ANSI RTF (`{\rtf1\ansi ...}`)
/// required by Windows Installer's `ScrollableText` RichEdit 2.0 control!
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

    // Fallback default MIT/Product RTF agreement if no `.rtf` file was provided
    std::ostringstream rtf;
    rtf << "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0\\fswiss Segoe UI;}}"
        << "\\fs18\\b " << productName << " - End-User Software License Agreement\\b0\\par\\par "
        << "Published by " << manufacturer << ".\\par\\par "
        << "Permission is hereby granted to install and use this software in accordance with "
        << "the accompanying license documentation.\\par}";
    return rtf.str();
}

/// Built-in relational & ICE consistency verifier (`--verify-msi` + automatic post-build gate!).
/// Checks SummaryInformation (`PID_TEMPLATE`, `PID_REVNUMBER`, `PID_PAGECOUNT=500`), ICE80 64-bit
/// component/directory rules, ICE21 feature mappings, ICE64 `RemoveFolder`, and `#payload.cab` stream!
inline bool VerifyCompiledMsiDatabase(const std::filesystem::path& msiPath, std::string& outError) {
    ScopedMsiHandle hDb;
    const std::wstring wMsi = msiPath.wstring();
    UINT rc = ::MsiOpenDatabaseW(wMsi.c_str(), MSIDBOPEN_READONLY, hDb.Put());
    if (rc != ERROR_SUCCESS) {
        outError = "VerifyMsi: MsiOpenDatabaseW failed (" + std::to_string(rc) + ").";
        return false;
    }

    // 1. Verify SummaryInformation stream (`PID_TEMPLATE`, `PID_REVNUMBER`, `PID_PAGECOUNT`)
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

    // 2. Verify core tables contain rows and `#payload.cab` exists in `_Streams`
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

    // 3. ICE80 check: every component in a 64-bit package (`x64;` or `Arm64;`) must carry attribute bit 256 (`0x0100`)
    {
        ScopedMsiHandle hView;
        if (::MsiDatabaseOpenViewW(hDb.Get(), L"SELECT `Component`, `Attributes` FROM `Component`", hView.Put()) == ERROR_SUCCESS &&
            ::MsiViewExecute(hView.Get(), 0) == ERROR_SUCCESS) {
            ScopedMsiHandle hRec;
            while (::MsiViewFetch(hView.Get(), hRec.Put()) == ERROR_SUCCESS) {
                const int attrs = ::MsiRecordGetInteger(hRec.Get(), 2);
                if ((attrs & 256) == 0) {
                    wchar_t compId[128]{};
                    DWORD cch = 128;
                    ::MsiRecordGetStringW(hRec.Get(), 1, compId, &cch);
                    outError = "VerifyMsi (ICE80): 64-bit package Component '" + WideToUtf8(compId) +
                               "' is missing msidbComponentAttributes64bit (256).";
                    return false;
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

    const std::string arch = val.resolvedArchitecture; // `"x64"` or `"arm64"`
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
        outSummary.productCode = GenerateRandomMsiGuid();
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
    // STEP 1: Harvest all `[Payload.*]` files, build directory tree & UUIDv5 components!
    // -------------------------------------------------------------------------
    struct StagedFileRecord {
        std::string           fileKey;        // `"f_0001"`
        std::string           compKey;        // `"c_0001"`
        std::string           compGuid;       // Deterministic SHA-1 UUIDv5 `{GUID}`
        std::string           dirKey;         // `"INSTALLDIR"` or `"d_0001"`
        std::string           featureId;      // `"Main"`
        std::filesystem::path sourcePath;
        std::string           relInstallPath; // Relative to `INSTALLDIR`, `/` separated
        std::string           msiFileName;    // `SHORT~1.EXT|LongFileName.ext`
        int                   fileSize = 0;
        std::string           peVersion;
        std::string           peLanguage;
        int                   sequence = 0;
    };

    struct DirNode {
        std::string key;
        std::string parentKey;
        std::string defaultDir;
    };

    const std::string rootSystemDirKey = outSummary.perMachine ? "ProgramFiles64Folder" : "LocalAppDataFolder";
    std::vector<DirNode> directories;
    directories.push_back({"TARGETDIR", "", "SourceDir"});
    directories.push_back({rootSystemDirKey, "TARGETDIR", "."});

    // Split `Install.DefaultSubdir` (e.g. `RaggieSoft\Hello RaggieSoft`) into nested `Directory` rows
    // ending at uppercase `INSTALLDIR` (required public property for CLI `INSTALLDIR="D:\Custom"` overrides!).
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
    if (installParts.empty()) {
        installParts.push_back(outSummary.productName);
    }

    std::size_t shortDirSeed = 1;
    std::string parentDir = rootSystemDirKey;
    for (std::size_t i = 0; i < installParts.size(); ++i) {
        const bool isLeaf = (i + 1 == installParts.size());
        const std::string dKey = isLeaf ? "INSTALLDIR" : ("INSTALLPARENT_" + std::to_string(i + 1));
        directories.push_back({dKey, parentDir, FormatMsiShortLongName(installParts[i], shortDirSeed++)});
        parentDir = dKey;
    }

    // Map relative subdirectory paths under `INSTALLDIR` -> MSI `Directory` primary keys
    std::map<std::string, std::string> relSubdirToKey;
    relSubdirToKey[""] = "INSTALLDIR";

    auto ensureSubdirKey = [&](std::string relSub) -> std::string {
        for (char& c : relSub) if (c == '\\') c = '/';
        while (!relSub.empty() && relSub.front() == '/') relSub.erase(relSub.begin());
        while (!relSub.empty() && relSub.back() == '/') relSub.pop_back();
        std::string lowerKey = AsciiLower(relSub);
        if (auto it = relSubdirToKey.find(lowerKey); it != relSubdirToKey.end()) {
            return it->second;
        }

        std::string accumLower;
        std::string curParent = "INSTALLDIR";
        std::size_t pos = 0;
        while (pos <= relSub.size()) {
            auto slash = relSub.find('/', pos);
            std::string seg = (slash == std::string::npos) ? relSub.substr(pos) : relSub.substr(pos, slash - pos);
            if (!seg.empty()) {
                if (!accumLower.empty()) accumLower += "/";
                accumLower += AsciiLower(seg);
                auto hit = relSubdirToKey.find(accumLower);
                if (hit != relSubdirToKey.end()) {
                    curParent = hit->second;
                } else {
                    std::string newKey = "d_" + std::to_string(relSubdirToKey.size());
                    directories.push_back({newKey, curParent, FormatMsiShortLongName(seg, shortDirSeed++)});
                    relSubdirToKey[accumLower] = newKey;
                    curParent = newKey;
                }
            }
            if (slash == std::string::npos) break;
            pos = slash + 1;
        }
        return curParent;
    };

    // Collect features (`[Feature.<id>]`, creating default `Main` if omitted)
    struct FeatureRec {
        std::string id;
        std::string title;
        std::string desc;
        int         level = 1;
    };
    std::vector<FeatureRec> features;
    for (const auto& fid : doc.GetNamedSectionIds("Feature")) {
        features.push_back({
            fid,
            doc.GetString("Feature", fid, "Title", fid),
            doc.GetString("Feature", fid, "Description", ""),
            doc.GetInt("Feature", fid, "Level", 1)
        });
    }
    if (features.empty()) {
        features.push_back({"Main", outSummary.productName, "Core application files", 1});
    }
    auto ensureFeatureExists = [&](const std::string& fid) {
        for (const auto& f : features) {
            if (EqualsIgnoreCase(f.id, fid)) return f.id;
        }
        features.push_back({fid, fid, "", 1});
        return fid;
    };

    // Harvest payload files deterministically
    std::vector<StagedFileRecord> stagedFiles;
    std::set<std::string> seenTargetRelPaths;

    auto appendSingleFile = [&](const std::filesystem::path& srcFile,
                                const std::string& targetRelDir,
                                const std::string& featureId) {
        const std::string fileNameUtf8 = WideToUtf8(srcFile.filename().wstring());
        std::string relInstall = targetRelDir.empty() ? fileNameUtf8 : (targetRelDir + "/" + fileNameUtf8);
        for (char& c : relInstall) if (c == '\\') c = '/';
        while (!relInstall.empty() && relInstall.front() == '/') relInstall.erase(relInstall.begin());

        const std::string dedupKey = AsciiLower(relInstall);
        if (!seenTargetRelPaths.insert(dedupKey).second) {
            return; // Avoid duplicate target file collisions
        }

        const int seq = static_cast<int>(stagedFiles.size() + 1);
        char keyBuf[24]{};
        std::snprintf(keyBuf, sizeof(keyBuf), "%04d", seq);
        const std::string fKey = std::string("f_") + keyBuf;
        const std::string cKey = std::string("c_") + keyBuf;

        const auto pe = HarvestPeVersionAndLanguage(srcFile);
        const auto sz = static_cast<int>(std::min<std::uintmax_t>(
            std::filesystem::file_size(srcFile, ec), 0x7FFFFFFFu));

        stagedFiles.push_back({
            fKey,
            cKey,
            ComputeDeterministicComponentGuid(outSummary.upgradeCode, arch, "file:" + relInstall),
            ensureSubdirKey(targetRelDir),
            ensureFeatureExists(featureId),
            srcFile,
            relInstall,
            FormatMsiShortLongName(fileNameUtf8, static_cast<std::size_t>(seq)),
            sz,
            pe.version,
            pe.language,
            seq
        });
    };

    for (const auto& pid : doc.GetNamedSectionIds("Payload")) {
        const auto absSrc     = resolvePath(doc.GetString("Payload", pid, "Source"));
        const std::string sub = ExpandArchToken(doc.GetString("Payload", pid, "TargetSubdir", ""), arch);
        const bool recursive  = doc.GetBool("Payload", pid, "Recursive", true);
        const std::string feat= doc.GetString("Payload", pid, "Feature", features.front().id);

        if (std::filesystem::is_regular_file(absSrc, ec)) {
            appendSingleFile(absSrc, sub, feat);
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
                appendSingleFile(fPath, combinedSub, feat);
            }
        }
    }

    if (stagedFiles.empty()) {
        outError = "No payload files were matched by [Payload.*] sections.";
        return false;
    }

    // -------------------------------------------------------------------------
    // STEP 2: Compress all staged files into temporary `#payload.cab` via `fci.h`
    // -------------------------------------------------------------------------
    const std::filesystem::path scratchDir = std::filesystem::temp_directory_path(ec) /
                                             ("rsw-build-" + outSummary.packageCode.substr(1, 8));
    std::filesystem::create_directories(scratchDir, ec);
    const std::filesystem::path cabPath        = scratchDir / "payload.cab";
    const std::filesystem::path bannerBmpPath  = scratchDir / "banner.bmp";
    const std::filesystem::path welcomeBmpPath = scratchDir / "welcome.bmp";

    std::vector<CabInputEntry> cabInputs;
    cabInputs.reserve(stagedFiles.size());
    for (const auto& sf : stagedFiles) {
        cabInputs.push_back({sf.sourcePath, sf.fileKey});
    }

    const bool useLzx = !EqualsIgnoreCase(doc.GetString("Package", "", "CabinetCompression", "lzx"), "mszip");
    if (!BuildPayloadCabinet(cabInputs, cabPath, useLzx, outError)) {
        std::filesystem::remove_all(scratchDir, ec);
        return false;
    }
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
        "CREATE TABLE `File` (`File` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL LOCALIZABLE, `FileSize` LONG NOT NULL, `Version` CHAR(72), `Language` CHAR(20), `Attributes` SHORT, `Sequence` INTEGER NOT NULL PRIMARY KEY `File`)",
        "CREATE TABLE `Media` (`DiskId` SHORT NOT NULL, `LastSequence` INTEGER NOT NULL, `DiskPrompt` CHAR(64) LOCALIZABLE, `Cabinet` CHAR(255), `VolumeLabel` CHAR(32), `Source` CHAR(72) PRIMARY KEY `DiskId`)",
        "CREATE TABLE `Feature` (`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64) LOCALIZABLE, `Description` CHAR(255) LOCALIZABLE, `Display` SHORT, `Level` SHORT NOT NULL, `Directory_` CHAR(72), `Attributes` SHORT NOT NULL PRIMARY KEY `Feature`)",
        "CREATE TABLE `FeatureComponents` (`Feature_` CHAR(38) NOT NULL, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Feature_`, `Component_`)",
        "CREATE TABLE `Shortcut` (`Shortcut` CHAR(72) NOT NULL, `Directory_` CHAR(72) NOT NULL, `Name` CHAR(128) NOT NULL LOCALIZABLE, `Component_` CHAR(72) NOT NULL, `Target` CHAR(72) NOT NULL, `Arguments` CHAR(255), `Description` CHAR(255) LOCALIZABLE, `Hotkey` SHORT, `Icon_` CHAR(72), `IconIndex` SHORT, `ShowCmd` SHORT, `WkDir` CHAR(72) PRIMARY KEY `Shortcut`)",
        "CREATE TABLE `RemoveFile` (`FileKey` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) LOCALIZABLE, `DirProperty` CHAR(72) NOT NULL, `InstallMode` SHORT NOT NULL PRIMARY KEY `FileKey`)",
        "CREATE TABLE `Registry` (`Registry` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL LOCALIZABLE, `Name` CHAR(255) LOCALIZABLE, `Value` LONGCHAR LOCALIZABLE, `Component_` CHAR(72) NOT NULL PRIMARY KEY `Registry`)",
        "CREATE TABLE `Icon` (`Name` CHAR(72) NOT NULL, `Data` OBJECT NOT NULL PRIMARY KEY `Name`)",
        "CREATE TABLE `Binary` (`Name` CHAR(72) NOT NULL, `Data` OBJECT NOT NULL PRIMARY KEY `Name`)",
        "CREATE TABLE `Upgrade` (`UpgradeCode` CHAR(38) NOT NULL, `VersionMin` CHAR(20), `VersionMax` CHAR(20), `Language` CHAR(255), `Attributes` LONG NOT NULL, `Remove` CHAR(255), `ActionProperty` CHAR(72) NOT NULL PRIMARY KEY `UpgradeCode`, `VersionMin`, `VersionMax`, `Language`, `Attributes`)",
        "CREATE TABLE `RegLocator` (`Signature_` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL, `Name` CHAR(255), `Type` SHORT PRIMARY KEY `Signature_`)",
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

    addProp("ProductCode",     outSummary.productCode);
    addProp("ProductName",     outSummary.productName);
    addProp("ProductVersion",  outSummary.productVersion);
    addProp("ProductLanguage", std::to_string(lcid));
    addProp("Manufacturer",    outSummary.manufacturer);
    addProp("UpgradeCode",     outSummary.upgradeCode);
    addProp("DefaultUIFont",   "DlgFont8");
    addProp("IAgree",          "No");
    addProp("LAUNCHAPP",       outSummary.offerLaunchOnFinish && !outSummary.mainExeRel.empty() ? "1" : "0");
    addProp("SecureCustomProperties", "RSW_OLDERVERSION_DETECTED;RSW_NEWERVERSION_DETECTED;RSW_OSBUILD");

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
    if (!outSummary.iconFilePath.empty() && std::filesystem::exists(outSummary.iconFilePath, ec)) {
        if (!msi_sql::InsertBinaryStreamRow(hDb.Get(), "Icon", "Name", "Data", "AppProductIcon.ico", outSummary.iconFilePath, outError)) {
            std::filesystem::remove_all(scratchDir, ec);
            return false;
        }
        addProp("ARPPRODUCTICON", "AppProductIcon.ico");
    }

    // Major Upgrade rows (`Upgrade` table):
    //   Row 1 (`Attributes = 256` = `msidbUpgradeAttributesLanguage`0 -> `0` / `256`? `0` = migrate features & remove older versions `< ProductVersion`!)
    //   Row 2 (`Attributes = 2` = `msidbUpgradeAttributesOnlyDetect | msidbUpgradeAttributesVersionMinInclusive` off = `1` -> detect newer versions `> ProductVersion`!)
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

    // Windows 11+ OS Build Floor (`HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion -> CurrentBuildNumber`)
    // Type `2` = `msidbLocatorTypeRawValue` (+ `16` = `msidbLocatorType64bit` -> `18` so 64-bit HKLM is read directly!)
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
        {"Installed OR (VersionNT >= 603 AND RSW_OSBUILD >= \"" + std::to_string(outSummary.minWindowsBuild) + "\")",
         outSummary.osLaunchMessage},
        outError);

    // -------------------------------------------------------------------------
    // STEP 5: Populate `Feature`, `Directory`, `Component`, `File`, `FeatureComponents`,
    //         `Shortcut` (+ ICE64 `RemoveFile`), `Registry`, `Media`, & `_Streams`!
    // -------------------------------------------------------------------------
    int featOrder = 2;
    for (const auto& f : features) {
        msi_sql::InsertRow(
            hDb.Get(), "Feature",
            {"Feature", "Feature_Parent", "Title", "Description", "Display", "Level", "Directory_", "Attributes"},
            {f.id, std::monostate{}, f.title, f.desc, featOrder, f.level, std::string("INSTALLDIR"), 0},
            outError);
        featOrder += 2;
    }

    // Check if any `[Shortcut.*]` needs `ProgramMenuFolder` / `ApplicationProgramsFolder` or `DesktopFolder`
    const auto shortcutIds = doc.GetNamedSectionIds("Shortcut");
    bool hasStartMenuShortcut = false;
    bool hasDesktopShortcut   = false;
    for (const auto& sid : shortcutIds) {
        const std::string loc = doc.GetString("Shortcut", sid, "Location", "StartMenu");
        if (EqualsIgnoreCase(loc, "Desktop")) hasDesktopShortcut = true;
        else hasStartMenuShortcut = true;
    }

    if (hasStartMenuShortcut) {
        const std::string smSub = ExpandArchToken(doc.GetString("Install", "", "StartMenuFolder", outSummary.manufacturer), arch);
        directories.push_back({"ProgramMenuFolder", "TARGETDIR", "."});
        directories.push_back({"ApplicationProgramsFolder", "ProgramMenuFolder", FormatMsiShortLongName(smSub, shortDirSeed++)});
    }
    if (hasDesktopShortcut) {
        directories.push_back({"DesktopFolder", "TARGETDIR", "."});
    }

    for (const auto& d : directories) {
        msi_sql::InsertRow(
            hDb.Get(), "Directory",
            {"Directory", "Directory_Parent", "DefaultDir"},
            {d.key, d.parentKey.empty() ? SqlParam{std::monostate{}} : SqlParam{d.parentKey}, d.defaultDir},
            outError);
    }

    // `msidbComponentAttributes64bit` = `256` (`0x0100`). Every component in an `x64` or `Arm64` MSI carries `256`!
    constexpr int kCompAttr64BitFile = 256;
    constexpr int kCompAttr64BitReg  = 256 + 4; // `msidbComponentAttributes64bit | msidbComponentAttributesRegistryKeyPath`

    std::map<std::string, std::string> relPathToFileKey;
    std::map<std::string, std::string> relPathToCompKey;
    std::map<std::string, std::string> relPathToDirKey;

    for (const auto& sf : stagedFiles) {
        msi_sql::InsertRow(
            hDb.Get(), "Component",
            {"Component", "ComponentId", "Directory_", "Attributes", "Condition", "KeyPath"},
            {sf.compKey, sf.compGuid, sf.dirKey, kCompAttr64BitFile, std::monostate{}, sf.fileKey},
            outError);

        // `msidbFileAttributesVital` = `512` (`0x0200`)
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
                512,
                sf.sequence
            },
            outError);

        msi_sql::InsertRow(
            hDb.Get(), "FeatureComponents",
            {"Feature_", "Component_"},
            {sf.featureId, sf.compKey},
            outError);

        std::string lowRel = AsciiLower(sf.relInstallPath);
        relPathToFileKey[lowRel] = sf.fileKey;
        relPathToCompKey[lowRel] = sf.compKey;
        relPathToDirKey[lowRel]  = sf.dirKey;
    }

    std::size_t totalComponents = stagedFiles.size();

    // Populate `[Shortcut.<id>]` rows:
    //   Notice how we satisfy ICE38, ICE43, ICE57, AND ICE64 simultaneously:
    //   Each Shortcut gets a dedicated Registry-KeyPath Component in `HKCU\Software\<Manufacturer>\<Product>\Shortcuts`
    //   plus a `RemoveFile` (`InstallMode = 2`, `FileName = NULL` = `RemoveFolder`!) row cleaning up
    //   `ApplicationProgramsFolder` on uninstall!
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
            outError = "Shortcut '" + sid + "' TargetFile '" + targetRel + "' does not match any [Payload.*] file.";
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
            {scComp, scGuid, scDir, kCompAttr64BitReg, std::monostate{}, regKeyId},
            outError);
        ++totalComponents;

        msi_sql::InsertRow(
            hDb.Get(), "FeatureComponents",
            {"Feature_", "Component_"},
            {featId, scComp},
            outError);

        msi_sql::InsertRow(
            hDb.Get(), "Registry",
            {"Registry", "Root", "Key", "Name", "Value", "Component_"},
            {regKeyId, 1 /* HKCU */, "Software\\" + outSummary.manufacturer + "\\" + outSummary.productName + "\\Shortcuts",
             sid, std::string("#1"), scComp},
            outError);

        if (!isDesktop) {
            msi_sql::InsertRow(
                hDb.Get(), "RemoveFile",
                {"FileKey", "Component_", "FileName", "DirProperty", "InstallMode"},
                {"rf_sc_" + std::to_string(scCounter), scComp, std::monostate{}, std::string("ApplicationProgramsFolder"), 2},
                outError);
        }

        const std::string args = ExpandArchToken(doc.GetString("Shortcut", sid, "Arguments"), arch);
        const std::string desc = ExpandArchToken(doc.GetString("Shortcut", sid, "Description", scName), arch);
        const bool hasIcon = !outSummary.iconFilePath.empty() && std::filesystem::exists(outSummary.iconFilePath, ec);

        msi_sql::InsertRow(
            hDb.Get(), "Shortcut",
            {"Shortcut", "Directory_", "Name", "Component_", "Target", "Arguments", "Description", "Hotkey", "Icon_", "IconIndex", "ShowCmd", "WkDir"},
            {
                scKey,
                scDir,
                FormatMsiShortLongName(scName, 500 + scCounter),
                scComp,
                "[#" + fIt->second + "]",
                args.empty() ? SqlParam{std::monostate{}} : SqlParam{args},
                desc,
                0,
                hasIcon ? SqlParam{std::string("AppProductIcon.ico")} : SqlParam{std::monostate{}},
                0,
                1,
                wkDir
            },
            outError);
    }

    // Populate `[Registry.<id>]` rows (plus standard `InstallLocation` discovery value!)
    std::size_t regCounter = 0;
    for (const auto& rid : doc.GetNamedSectionIds("Registry")) {
        ++regCounter;
        const std::string rootStr = doc.GetString("Registry", rid, "Root", outSummary.perMachine ? "HKLM" : "HKCU");
        const int rootNum = EqualsIgnoreCase(rootStr, "HKCU") ? 1 : (EqualsIgnoreCase(rootStr, "HKCR") ? 0 : 2);
        const std::string keyPath = ExpandArchToken(doc.GetString("Registry", rid, "Key"), arch);
        const std::string valName = ExpandArchToken(doc.GetString("Registry", rid, "Name"), arch);
        const std::string rawVal  = ExpandArchToken(doc.GetString("Registry", rid, "Value"), arch);
        const std::string valType = doc.GetString("Registry", rid, "Type", "string");
        const std::string featId  = ensureFeatureExists(doc.GetString("Registry", rid, "Feature", features.front().id));

        std::string formattedVal = rawVal;
        if (EqualsIgnoreCase(valType, "dword")) {
            formattedVal = "#" + rawVal;
        } else if (EqualsIgnoreCase(valType, "expand_string")) {
            formattedVal = "#%" + rawVal;
        }

        const std::string rComp = "c_reg_" + std::to_string(regCounter);
        const std::string rRow  = "r_usr_" + std::to_string(regCounter);
        const std::string rGuid = ComputeDeterministicComponentGuid(
            outSummary.upgradeCode, arch, "reg:" + rootStr + "\\" + keyPath + "\\" + valName);

        msi_sql::InsertRow(
            hDb.Get(), "Component",
            {"Component", "ComponentId", "Directory_", "Attributes", "Condition", "KeyPath"},
            {rComp, rGuid, std::string("INSTALLDIR"), kCompAttr64BitReg, std::monostate{}, rRow},
            outError);
        ++totalComponents;

        msi_sql::InsertRow(
            hDb.Get(), "FeatureComponents",
            {"Feature_", "Component_"},
            {featId, rComp},
            outError);

        msi_sql::InsertRow(
            hDb.Get(), "Registry",
            {"Registry", "Root", "Key", "Name", "Value", "Component_"},
            {rRow, rootNum, keyPath, (valName == "@") ? SqlParam{std::monostate{}} : SqlParam{valName}, formattedVal, rComp},
            outError);
    }

    outSummary.fileCount      = stagedFiles.size();
    outSummary.componentCount = totalComponents;

    // `Media` table (`#payload.cab` -> `_Streams.Name = 'payload.cab'`)
    msi_sql::InsertRow(
        hDb.Get(), "Media",
        {"DiskId", "LastSequence", "DiskPrompt", "Cabinet", "VolumeLabel", "Source"},
        {1, static_cast<int>(stagedFiles.size()), std::monostate{}, std::string("#payload.cab"), std::monostate{}, std::monostate{}},
        outError);

    if (!msi_sql::InsertBinaryStreamRow(hDb.Get(), "_Streams", "Name", "Data", "payload.cab", cabPath, outError) ||
        !msi_sql::InsertBinaryStreamRow(hDb.Get(), "Binary", "Name", "Data", "RswBannerBmp", bannerBmpPath, outError) ||
        !msi_sql::InsertBinaryStreamRow(hDb.Get(), "Binary", "Name", "Data", "RswWelcomeBmp", welcomeBmpPath, outError)) {
        std::filesystem::remove_all(scratchDir, ec);
        return false;
    }

    // Optional Finish-screen application launch CustomAction (`Type = 226` = `msidbCustomActionTypeExe` 34 + `Async` 192 = 226)
    std::string mainExeFileKey;
    if (!outSummary.mainExeRel.empty()) {
        std::string normMain = outSummary.mainExeRel;
        for (char& c : normMain) if (c == '\\') c = '/';
        auto it = relPathToFileKey.find(AsciiLower(normMain));
        if (it != relPathToFileKey.end()) {
            mainExeFileKey = it->second;
            // Type 18 (`msidbCustomActionTypeFile` 18) + `msidbCustomActionTypeContinue` (64) + `msidbCustomActionTypeAsync` (128) = 210
            msi_sql::InsertRow(
                hDb.Get(), "CustomAction",
                {"Action", "Type", "Source", "Target"},
                {std::string("RswLaunchMainApp"), 210, mainExeFileKey, std::string("")},
                outError);
        }
    }

    // -------------------------------------------------------------------------
    // STEP 6: Populate Classic Windows 2000 "Wizard 97" Internal UI Tables!
    // -------------------------------------------------------------------------
    msi_sql::InsertRow(hDb.Get(), "TextStyle", {"TextStyle", "FaceName", "Size", "Color", "StyleBits"},
                       {std::string("DlgFont8"), std::string("Tahoma"), 8, 0, 0}, outError);
    msi_sql::InsertRow(hDb.Get(), "TextStyle", {"TextStyle", "FaceName", "Size", "Color", "StyleBits"},
                       {std::string("DlgFontBold8"), std::string("Tahoma"), 8, 0, 1}, outError);
    msi_sql::InsertRow(hDb.Get(), "TextStyle", {"TextStyle", "FaceName", "Size", "Color", "StyleBits"},
                       {std::string("VerdanaBold13"), std::string("Verdana"), 12, 0, 1}, outError);

    const bool hasLicensePage = !outSummary.licenseRtfPath.empty();
    const std::string licenseRtfContent = LoadOrSynthesizeLicenseRtf(
        outSummary.licenseRtfPath, outSummary.productName, outSummary.manufacturer);

    const std::string bannerSub = doc.GetString("UI", "", "BannerTitle", "Ready to install [ProductName] on your computer.");
    const std::string welcomeHead = doc.GetString("UI", "", "WelcomeHeadline", "Welcome to the [ProductName] Setup Wizard");

    // Control attribute constants (`msiquery.h` / Windows Installer SDK):
    //   `1`       = Enabled
    //   `2`       = Visible
    //   `3`       = Visible | Enabled
    //   `65539`   = Visible | Enabled | Transparent (`0x00010003` — text paints cleanly over white bitmaps!)
    //   `1048577` = Enabled | FixedSize (`0x00100001`) for `Bitmap` controls (`1048579` = Visible | Enabled | FixedSize)
    //   `7`       = Visible | Enabled | Sunken (`4`)
    constexpr int kVisEn        = 3;
    constexpr int kVisTrans     = 65539;
    constexpr int kBmpFixed     = 1048579;

    auto addDlg = [&](std::string id, int w, int h, int attr, std::string title,
                      std::string first, std::string defBtn, std::string cancelBtn) {
        msi_sql::InsertRow(
            hDb.Get(), "Dialog",
            {"Dialog", "HCentering", "VCentering", "Width", "Height", "Attributes", "Title", "Control_First", "Control_Default", "Control_Cancel"},
            {id, 50, 50, w, h, attr, title, first, defBtn, cancelBtn},
            outError);
    };
    auto addCtrl = [&](std::string dlg, std::string ctrl, std::string type,
                       int x, int y, int w, int h, int attr,
                       SqlParam prop, SqlParam text, SqlParam next) {
        msi_sql::InsertRow(
            hDb.Get(), "Control",
            {"Dialog_", "Control", "Type", "X", "Y", "Width", "Height", "Attributes", "Property", "Text", "Control_Next", "Help"},
            {dlg, ctrl, type, x, y, w, h, attr, prop, text, next, std::monostate{}},
            outError);
    };
    auto addEvt = [&](std::string dlg, std::string ctrl, std::string evt, std::string arg, std::string cond, int ord) {
        msi_sql::InsertRow(
            hDb.Get(), "ControlEvent",
            {"Dialog_", "Control_", "Event", "Argument", "Condition", "Ordering"},
            {dlg, ctrl, evt, arg, cond, ord},
            outError);
    };

    // 1. `WelcomeDlg` (370 x 270)
    addDlg("WelcomeDlg", 370, 270, 3, "[ProductName] Setup", "Next", "Next", "Cancel");
    addCtrl("WelcomeDlg", "Bitmap", "Bitmap", 0, 0, 370, 234, kBmpFixed, std::monostate{}, std::string("RswWelcomeBmp"), std::monostate{});
    addCtrl("WelcomeDlg", "BottomLine", "Line", 0, 234, 370, 0, 1, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("WelcomeDlg", "Title", "Text", 135, 20, 220, 60, kVisTrans, std::monostate{}, "{\\VerdanaBold13}" + welcomeHead, std::monostate{});
    addCtrl("WelcomeDlg", "Description", "Text", 135, 85, 220, 90, kVisTrans, std::monostate{},
            std::string("The Setup Wizard will install [ProductName] v[ProductVersion] on your computer.\r\n\r\nClick Next to continue or Cancel to exit the Setup Wizard."),
            std::monostate{});
    addCtrl("WelcomeDlg", "Back", "PushButton", 180, 243, 56, 17, 2 /* Visible, Disabled on first page */, std::monostate{}, std::string("< &Back"), std::string("Next"));
    addCtrl("WelcomeDlg", "Next", "PushButton", 236, 243, 56, 17, kVisEn, std::monostate{}, std::string("&Next >"), std::string("Cancel"));
    addCtrl("WelcomeDlg", "Cancel", "PushButton", 304, 243, 56, 17, kVisEn, std::monostate{}, std::string("Cancel"), std::string("Back"));
    addEvt("WelcomeDlg", "Next", "NewDialog", hasLicensePage ? "LicenseAgreementDlg" : "InstallDirDlg", "1", 1);
    addEvt("WelcomeDlg", "Cancel", "SpawnDialog", "CancelDlg", "1", 1);

    // 2. `LicenseAgreementDlg` (370 x 270)
    addDlg("LicenseAgreementDlg", 370, 270, 3, "[ProductName] Setup - License Agreement", "AgreementChoice", "Next", "Cancel");
    addCtrl("LicenseAgreementDlg", "BannerBitmap", "Bitmap", 0, 0, 370, 44, kBmpFixed, std::monostate{}, std::string("RswBannerBmp"), std::monostate{});
    addCtrl("LicenseAgreementDlg", "BannerLine", "Line", 0, 44, 370, 0, 1, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("LicenseAgreementDlg", "BottomLine", "Line", 0, 234, 370, 0, 1, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("LicenseAgreementDlg", "Title", "Text", 15, 6, 290, 15, kVisTrans, std::monostate{}, std::string("{\\DlgFontBold8}End-User License Agreement"), std::monostate{});
    addCtrl("LicenseAgreementDlg", "Description", "Text", 20, 23, 290, 15, kVisTrans, std::monostate{}, std::string("Please read the following license agreement carefully."), std::monostate{});
    addCtrl("LicenseAgreementDlg", "AgreementText", "ScrollableText", 20, 55, 330, 130, 7, std::monostate{}, licenseRtfContent, std::string("AgreementChoice"));
    addCtrl("LicenseAgreementDlg", "AgreementChoice", "CheckBox", 20, 195, 330, 18, kVisEn, std::string("IAGREECHECK"), std::string("I &accept the terms in the License Agreement"), std::string("Back"));
    addCtrl("LicenseAgreementDlg", "Back", "PushButton", 180, 243, 56, 17, kVisEn, std::monostate{}, std::string("< &Back"), std::string("Next"));
    addCtrl("LicenseAgreementDlg", "Next", "PushButton", 236, 243, 56, 17, kVisEn, std::monostate{}, std::string("&Next >"), std::string("Cancel"));
    addCtrl("LicenseAgreementDlg", "Cancel", "PushButton", 304, 243, 56, 17, kVisEn, std::monostate{}, std::string("Cancel"), std::string("AgreementChoice"));
    msi_sql::InsertRow(hDb.Get(), "ControlCondition", {"Dialog_", "Control_", "Action", "Condition"},
                       {std::string("LicenseAgreementDlg"), std::string("Next"), std::string("Disable"), std::string("NOT IAGREECHECK")}, outError);
    msi_sql::InsertRow(hDb.Get(), "ControlCondition", {"Dialog_", "Control_", "Action", "Condition"},
                       {std::string("LicenseAgreementDlg"), std::string("Next"), std::string("Enable"), std::string("IAGREECHECK")}, outError);
    addEvt("LicenseAgreementDlg", "Back", "NewDialog", "WelcomeDlg", "1", 1);
    addEvt("LicenseAgreementDlg", "Next", "NewDialog", "InstallDirDlg", "IAGREECHECK", 1);
    addEvt("LicenseAgreementDlg", "Cancel", "SpawnDialog", "CancelDlg", "1", 1);

    // 3. `InstallDirDlg` (370 x 270 — Destination Folder & Ready to Install)
    addDlg("InstallDirDlg", 370, 270, 3, "[ProductName] Setup - Ready to Install", "Install", "Install", "Cancel");
    addCtrl("InstallDirDlg", "BannerBitmap", "Bitmap", 0, 0, 370, 44, kBmpFixed, std::monostate{}, std::string("RswBannerBmp"), std::monostate{});
    addCtrl("InstallDirDlg", "BannerLine", "Line", 0, 44, 370, 0, 1, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("InstallDirDlg", "BottomLine", "Line", 0, 234, 370, 0, 1, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("InstallDirDlg", "Title", "Text", 15, 6, 290, 15, kVisTrans, std::monostate{}, std::string("{\\DlgFontBold8}Destination Folder & Ready to Install"), std::monostate{});
    addCtrl("InstallDirDlg", "Description", "Text", 20, 23, 290, 15, kVisTrans, std::monostate{}, bannerSub, std::monostate{});
    addCtrl("InstallDirDlg", "FolderLabel", "Text", 20, 62, 330, 20, kVisEn, std::monostate{}, std::string("Install [ProductName] to the following folder (you may edit the path below):"), std::string("FolderEdit"));
    addCtrl("InstallDirDlg", "FolderEdit", "PathEdit", 20, 84, 330, 18, kVisEn, std::string("INSTALLDIR"), std::string("[INSTALLDIR]"), std::string("Back"));
    addCtrl("InstallDirDlg", "ReadyNote", "Text", 20, 120, 330, 60, kVisEn, std::monostate{},
            std::string("Click Install to begin the installation. If you want to review or change any settings, click Back."), std::monostate{});
    addCtrl("InstallDirDlg", "Back", "PushButton", 180, 243, 56, 17, kVisEn, std::monostate{}, std::string("< &Back"), std::string("Install"));
    // `msidbControlAttributesElevationShield` = `0x00800000` (`8388608`) when per-machine!
    const int installBtnAttr = outSummary.perMachine ? (kVisEn | 0x00800000) : kVisEn;
    addCtrl("InstallDirDlg", "Install", "PushButton", 236, 243, 56, 17, installBtnAttr, std::monostate{}, outSummary.installBtnText, std::string("Cancel"));
    addCtrl("InstallDirDlg", "Cancel", "PushButton", 304, 243, 56, 17, kVisEn, std::monostate{}, std::string("Cancel"), std::string("FolderLabel"));
    addEvt("InstallDirDlg", "Back", "NewDialog", hasLicensePage ? "LicenseAgreementDlg" : "WelcomeDlg", "1", 1);
    addEvt("InstallDirDlg", "Install", "SetTargetPath", "INSTALLDIR", "1", 1);
    addEvt("InstallDirDlg", "Install", "EndDialog", "Return", "1", 2);
    addEvt("InstallDirDlg", "Cancel", "SpawnDialog", "CancelDlg", "1", 1);

    // 4. `MaintenanceWelcomeDlg` (370 x 270 — Repair or Remove when already installed!)
    addDlg("MaintenanceWelcomeDlg", 370, 270, 3, "[ProductName] Maintenance", "Repair", "Repair", "Cancel");
    addCtrl("MaintenanceWelcomeDlg", "Bitmap", "Bitmap", 0, 0, 370, 234, kBmpFixed, std::monostate{}, std::string("RswWelcomeBmp"), std::monostate{});
    addCtrl("MaintenanceWelcomeDlg", "BottomLine", "Line", 0, 234, 370, 0, 1, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("MaintenanceWelcomeDlg", "Title", "Text", 135, 20, 220, 50, kVisTrans, std::monostate{}, std::string("{\\VerdanaBold13}Modify, Repair, or Remove [ProductName]"), std::monostate{});
    addCtrl("MaintenanceWelcomeDlg", "Description", "Text", 135, 80, 220, 60, kVisTrans, std::monostate{},
            std::string("Choose Repair to verify and restore installed files, or choose Remove to uninstall [ProductName] from your computer."), std::monostate{});
    addCtrl("MaintenanceWelcomeDlg", "Repair", "PushButton", 150, 160, 90, 20, installBtnAttr, std::monostate{}, std::string("&Repair"), std::string("Remove"));
    addCtrl("MaintenanceWelcomeDlg", "Remove", "PushButton", 150, 190, 90, 20, installBtnAttr, std::monostate{}, std::string("Re&move"), std::string("Cancel"));
    addCtrl("MaintenanceWelcomeDlg", "Cancel", "PushButton", 304, 243, 56, 17, kVisEn, std::monostate{}, std::string("Cancel"), std::string("Repair"));
    addEvt("MaintenanceWelcomeDlg", "Repair", "Reinstall", "ALL", "1", 1);
    addEvt("MaintenanceWelcomeDlg", "Repair", "EndDialog", "Return", "1", 2);
    addEvt("MaintenanceWelcomeDlg", "Remove", "Remove", "ALL", "1", 1);
    addEvt("MaintenanceWelcomeDlg", "Remove", "EndDialog", "Return", "1", 2);
    addEvt("MaintenanceWelcomeDlg", "Cancel", "SpawnDialog", "CancelDlg", "1", 1);

    // 5. `ProgressDlg` (`Attributes = 1` = Visible, modeless! Shown during `ExecuteAction`)
    addDlg("ProgressDlg", 370, 270, 1, "[ProductName] Setup", "Cancel", "Cancel", "Cancel");
    addCtrl("ProgressDlg", "BannerBitmap", "Bitmap", 0, 0, 370, 44, kBmpFixed, std::monostate{}, std::string("RswBannerBmp"), std::monostate{});
    addCtrl("ProgressDlg", "BannerLine", "Line", 0, 44, 370, 0, 1, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("ProgressDlg", "BottomLine", "Line", 0, 234, 370, 0, 1, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("ProgressDlg", "Title", "Text", 15, 6, 290, 15, kVisTrans, std::monostate{}, std::string("{\\DlgFontBold8}Installing [ProductName]"), std::monostate{});
    addCtrl("ProgressDlg", "Description", "Text", 20, 23, 290, 15, kVisTrans, std::monostate{}, std::string("Please wait while the Setup Wizard configures [ProductName]."), std::monostate{});
    addCtrl("ProgressDlg", "ActionText", "Text", 20, 75, 330, 18, kVisEn, std::monostate{}, std::string("Preparing installation..."), std::monostate{});
    addCtrl("ProgressDlg", "ProgressBar", "ProgressBar", 20, 100, 330, 16, kVisEn, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("ProgressDlg", "Cancel", "PushButton", 304, 243, 56, 17, kVisEn, std::monostate{}, std::string("Cancel"), std::monostate{});
    addEvt("ProgressDlg", "Cancel", "SpawnDialog", "CancelDlg", "1", 1);
    msi_sql::InsertRow(hDb.Get(), "EventMapping", {"Dialog_", "Control_", "Event", "Attribute"},
                       {std::string("ProgressDlg"), std::string("ActionText"), std::string("ActionText"), std::string("Text")}, outError);
    msi_sql::InsertRow(hDb.Get(), "EventMapping", {"Dialog_", "Control_", "Event", "Attribute"},
                       {std::string("ProgressDlg"), std::string("ProgressBar"), std::string("SetProgress"), std::string("Progress")}, outError);

    // 6. `ExitDialog` (`Sequence = -1` in `InstallUISequence`)
    addDlg("ExitDialog", 370, 270, 3, "[ProductName] Setup", "Finish", "Finish", "Finish");
    addCtrl("ExitDialog", "Bitmap", "Bitmap", 0, 0, 370, 234, kBmpFixed, std::monostate{}, std::string("RswWelcomeBmp"), std::monostate{});
    addCtrl("ExitDialog", "BottomLine", "Line", 0, 234, 370, 0, 1, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("ExitDialog", "Title", "Text", 135, 20, 220, 60, kVisTrans, std::monostate{}, std::string("{\\VerdanaBold13}Completed the [ProductName] Setup Wizard"), std::monostate{});
    addCtrl("ExitDialog", "Description", "Text", 135, 85, 220, 60, kVisTrans, std::monostate{}, std::string("Click the Finish button to exit the Setup Wizard."), std::monostate{});
    if (!mainExeFileKey.empty() && outSummary.offerLaunchOnFinish) {
        addCtrl("ExitDialog", "LaunchCheck", "CheckBox", 135, 160, 220, 18, kVisTrans, std::string("LAUNCHAPP"), outSummary.launchChkText, std::string("Finish"));
        msi_sql::InsertRow(hDb.Get(), "ControlCondition", {"Dialog_", "Control_", "Action", "Condition"},
                           {std::string("ExitDialog"), std::string("LaunchCheck"), std::string("Hide"), std::string("REMOVE=\"ALL\"")}, outError);
        addEvt("ExitDialog", "Finish", "DoAction", "RswLaunchMainApp", "LAUNCHAPP = \"1\" AND NOT REMOVE=\"ALL\"", 1);
    }
    addCtrl("ExitDialog", "Finish", "PushButton", 236, 243, 56, 17, kVisEn, std::monostate{}, std::string("&Finish"), std::monostate{});
    addEvt("ExitDialog", "Finish", "EndDialog", "Return", "1", 2);

    // 7. `CancelDlg`, `UserExit` (`-2`), and `FatalError` (`-3`)
    addDlg("CancelDlg", 260, 85, 3, "[ProductName] Setup", "No", "No", "No");
    addCtrl("CancelDlg", "Text", "Text", 20, 15, 220, 30, kVisEn, std::monostate{}, std::string("Are you sure you want to cancel [ProductName] installation?"), std::monostate{});
    addCtrl("CancelDlg", "Yes", "PushButton", 72, 57, 56, 17, kVisEn, std::monostate{}, std::string("&Yes"), std::string("No"));
    addCtrl("CancelDlg", "No", "PushButton", 132, 57, 56, 17, kVisEn, std::monostate{}, std::string("&No"), std::string("Yes"));
    addEvt("CancelDlg", "Yes", "EndDialog", "Exit", "1", 1);
    addEvt("CancelDlg", "No", "EndDialog", "Return", "1", 1);

    addDlg("UserExit", 370, 270, 3, "[ProductName] Setup", "Finish", "Finish", "Finish");
    addCtrl("UserExit", "Bitmap", "Bitmap", 0, 0, 370, 234, kBmpFixed, std::monostate{}, std::string("RswWelcomeBmp"), std::monostate{});
    addCtrl("UserExit", "BottomLine", "Line", 0, 234, 370, 0, 1, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("UserExit", "Title", "Text", 135, 20, 220, 60, kVisTrans, std::monostate{}, std::string("{\\VerdanaBold13}[ProductName] Setup Was Interrupted"), std::monostate{});
    addCtrl("UserExit", "Description", "Text", 135, 85, 220, 60, kVisTrans, std::monostate{}, std::string("Your system has not been modified. Run Setup again later to complete installation."), std::monostate{});
    addCtrl("UserExit", "Finish", "PushButton", 236, 243, 56, 17, kVisEn, std::monostate{}, std::string("&Finish"), std::monostate{});
    addEvt("UserExit", "Finish", "EndDialog", "Exit", "1", 1);

    addDlg("FatalError", 370, 270, 3, "[ProductName] Setup", "Finish", "Finish", "Finish");
    addCtrl("FatalError", "Bitmap", "Bitmap", 0, 0, 370, 234, kBmpFixed, std::monostate{}, std::string("RswWelcomeBmp"), std::monostate{});
    addCtrl("FatalError", "BottomLine", "Line", 0, 234, 370, 0, 1, std::monostate{}, std::monostate{}, std::monostate{});
    addCtrl("FatalError", "Title", "Text", 135, 20, 220, 60, kVisTrans, std::monostate{}, std::string("{\\VerdanaBold13}[ProductName] Setup Ended Prematurely"), std::monostate{});
    addCtrl("FatalError", "Description", "Text", 135, 85, 220, 60, kVisTrans, std::monostate{}, std::string("[ProductName] Setup ended prematurely because of an error. Your system has not been modified."), std::monostate{});
    addCtrl("FatalError", "Finish", "PushButton", 236, 243, 56, 17, kVisEn, std::monostate{}, std::string("&Finish"), std::monostate{});
    addEvt("FatalError", "Finish", "EndDialog", "Exit", "1", 1);

    // Friendly `ActionText` descriptions streamed to both Wizard 97 `ProgressDlg` and `rswsetup.exe` (`MsiSetExternalUIRecord`)!
    const std::pair<const char*, const char*> actionTexts[] = {
        {"InstallValidate",        "Validating installation..."},
        {"RemoveExistingProducts", "Removing previous version..."},
        {"ProcessComponents",      "Updating component registration..."},
        {"RemoveFiles",            "Removing old files..."},
        {"CreateFolders",          "Creating application folders..."},
        {"InstallFiles",           "Copying new files..."},
        {"CreateShortcuts",        "Creating shortcuts..."},
        {"WriteRegistryValues",    "Writing system registry values..."},
        {"RegisterProduct",        "Registering product..."},
        {"PublishProduct",         "Publishing product information..."}
    };
    for (const auto& [act, txt] : actionTexts) {
        msi_sql::InsertRow(hDb.Get(), "ActionText", {"Action", "Description", "Template"},
                           {std::string(act), std::string(txt), std::monostate{}}, outError);
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

    const SeqEntry execSeq[] = {
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
    for (const auto& s : execSeq) {
        msi_sql::InsertRow(
            hDb.Get(), "InstallExecuteSequence", {"Action", "Condition", "Sequence"},
            {std::string(s.action), s.cond ? SqlParam{std::string(s.cond)} : SqlParam{std::monostate{}}, s.seq},
            outError);
    }

    const SeqEntry adminSeq[] = {
        {"CostInitialize",    nullptr, 800},
        {"FileCost",          nullptr, 900},
        {"CostFinalize",      nullptr, 1000},
        {"InstallValidate",   nullptr, 1400},
        {"InstallInitialize", nullptr, 1500},
        {"InstallAdminPackage", nullptr, 3900},
        {"InstallFiles",      nullptr, 4000},
        {"InstallFinalize",   nullptr, 6600}
    };
    for (const auto& s : adminSeq) {
        msi_sql::InsertRow(
            hDb.Get(), "AdminExecuteSequence", {"Action", "Condition", "Sequence"},
            {std::string(s.action), std::monostate{}, s.seq},
            outError);
    }

    // -------------------------------------------------------------------------
    // STEP 8: Write OLE `SummaryInformation` Stream (`MsiGetSummaryInformationW`) & Commit!
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

        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 1,  VT_I2,       1252, &nowFt, nullptr);                       // PID_CODEPAGE
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 2,  VT_LPSTR,    0,    &nowFt, L"Installation Database");      // PID_TITLE
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 3,  VT_LPSTR,    0,    &nowFt, wSubject.c_str());              // PID_SUBJECT
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 4,  VT_LPSTR,    0,    &nowFt, wAuthor.c_str());               // PID_AUTHOR
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 5,  VT_LPSTR,    0,    &nowFt, L"Installer, MSI, Database");   // PID_KEYWORDS
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 6,  VT_LPSTR,    0,    &nowFt, wComments.c_str());             // PID_COMMENTS
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 7,  VT_LPSTR,    0,    &nowFt, wTemplate.c_str());             // PID_TEMPLATE (`x64;1033` / `Arm64;1033`)
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 9,  VT_LPSTR,    0,    &nowFt, wPkgCode.c_str());              // PID_REVNUMBER (`PackageCode`)
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 12, VT_FILETIME, 0,    &nowFt, nullptr);                       // PID_CREATE_DTM
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 13, VT_FILETIME, 0,    &nowFt, nullptr);                       // PID_LASTSAVE_DTM
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 14, VT_I4,       500,  &nowFt, nullptr);                       // PID_PAGECOUNT (MSI 5.0 schema)
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 15, VT_I4,       2,    &nowFt, nullptr);                       // PID_WORDCOUNT (2 = Compressed CAB source)
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 18, VT_LPSTR,    0,    &nowFt, kGeneratorSigWide.data());      // PID_APPNAME
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 19, VT_I4,       2,    &nowFt, nullptr);                       // PID_SECURITY

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

    // Immediate post-build relational & ICE80 verification!
    return VerifyCompiledMsiDatabase(outSummary.msiPath, outError);
}

} // namespace rsw
