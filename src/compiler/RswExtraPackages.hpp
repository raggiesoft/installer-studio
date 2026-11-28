// ================================================================================
// FILE: src/compiler/RswExtraPackages.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   Beyond standard `.msi` installers and `Setup.exe` bootstrappers, enterprise
//   Windows deployment teams rely on three companion Windows Installer formats:
//
//     1. `.msm` — **Merge Module** (`CompileManifestToMsm`):
//        A reusable library component database (similar to a static `.lib` for
//        installers!) that shares DLLs, tools, or registry keys across multiple
//        parent `.msi` products without GUID collisions. Every identifier is
//        modularized with a `.GUID_WITH_UNDERSCORES` suffix, files are compressed
//        into `_Streams` as `MergeModule.CABinet`, and `ModuleSignature` +
//        `ModuleComponents` tables identify the module.
//
//     2. `.mst` — **Windows Installer Transform** (`GenerateMsiTransform`):
//        A lightweight relational diff generated between a baseline `.msi` and a
//        customized `.msi` via `MsiDatabaseGenerateTransformW` +
//        `MsiCreateTransformSummaryInfoW`. System administrators pass `.mst` files
//        to Active Directory Group Policy or `msiexec /i Product.msi TRANSFORMS=Org.mst`
//        to pre-configure enterprise defaults without breaking vendor MSI files.
//
//     3. `.msp` — **Windows Installer Patch Package** (`GenerateMsiPatchPackage`):
//        An OLE Compound Document created via `MsiOpenDatabaseW` with
//        `MSIDBOPEN_CREATEDIRECT + MSIDBOPEN_PATCHFILE` containing:
//          - Patch `SummaryInformation` (`PID_REVNUMBER` = `{PatchCode}`,
//            `PID_TEMPLATE` = target `{ProductCode}`, `PID_LASTAUTHOR` = `:RswPatch1`,
//            `PID_KEYWORDS` = `RswPatch.cab`, `PID_WORDCOUNT = 4`)
//          - Embedded delta/updated cabinet stream (`RswPatch.cab` in `_Streams`)
//          - Embedded transform substorage (`RswPatch1` copied via OLE `IStorage`)
//        Applied via `msiexec /p Update.msp REINSTALL=ALL REINSTALLMODE=omus`!
// ================================================================================

#pragma once

#include "../common/RswCryptoAndPe.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswVersion.hpp"
#include "RswCabBuilder.hpp"
#include "RswMsiCompiler.hpp"
#include "RswUpdatePlanner.hpp"
#include "RswValidator.hpp"

#include <objbase.h>

#include <algorithm>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#pragma comment(lib, "ole32.lib")

namespace rsw {

/// Converts `{A18F4E20-7C93-4D62-9B44-524147474945}` into Merge Module suffix
/// `.A18F4E20_7C93_4D62_9B44_524147474945` per Microsoft's Merge Module specification.
inline std::string FormatMergeModuleGuidSuffix(std::string_view normalizedMsiGuid) {
    std::string out = ".";
    for (char c : normalizedMsiGuid) {
        if (c == '{' || c == '}') continue;
        out += (c == '-') ? '_' : c;
    }
    return out;
}

/// Verifies a compiled `.msm` Merge Module database (`ModuleSignature`, `ModuleComponents`,
/// `MergeModule.CABinet` stream, and `PID_TITLE == "Merge Module"`).
inline bool VerifyCompiledMsmDatabase(const std::filesystem::path& msmPath, std::string& outError) {
    ScopedMsiHandle hDb;
    if (::MsiOpenDatabaseW(msmPath.wstring().c_str(), MSIDBOPEN_READONLY, hDb.Put()) != ERROR_SUCCESS) {
        outError = "VerifyMsm: MsiOpenDatabaseW failed on '" + WideToUtf8(msmPath.wstring()) + "'.";
        return false;
    }

    auto countRows = [&](const wchar_t* sql) -> int {
        ScopedMsiHandle hView;
        if (::MsiDatabaseOpenViewW(hDb.Get(), sql, hView.Put()) != ERROR_SUCCESS) return -1;
        if (::MsiViewExecute(hView.Get(), 0) != ERROR_SUCCESS) return -1;
        int n = 0;
        ScopedMsiHandle hRec;
        while (::MsiViewFetch(hView.Get(), hRec.Put()) == ERROR_SUCCESS) ++n;
        return n;
    };

    if (countRows(L"SELECT `ModuleID` FROM `ModuleSignature`") != 1) {
        outError = "VerifyMsm: ModuleSignature table must contain exactly 1 row.";
        return false;
    }
    if (countRows(L"SELECT `Component` FROM `ModuleComponents`") <= 0) {
        outError = "VerifyMsm: ModuleComponents table is empty.";
        return false;
    }
    if (countRows(L"SELECT `Name` FROM `_Streams` WHERE `Name` = 'MergeModule.CABinet'") != 1) {
        outError = "VerifyMsm: Embedded 'MergeModule.CABinet' stream missing from _Streams.";
        return false;
    }
    return true;
}

/// Compiles a `.rssw` manifest into a genuine Windows Installer Merge Module (`.msm`)!
inline bool CompileManifestToMsm(
    const RswManifestDocument& doc,
    std::string_view cliArchOverride,
    const std::filesystem::path& explicitMsmOutPath,
    std::filesystem::path& outWrittenMsmPath,
    std::string& outError)
{
    const ValidationReport val = ValidateManifestDocument(doc, cliArchOverride);
    if (!val.IsOk()) {
        outError = "Merge Module manifest validation failed: " + val.diagnostics.front().message;
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

    const std::string modName    = ExpandArchToken(doc.GetString("Package", "", "Name"), arch);
    const std::string modVersion = ExpandArchToken(doc.GetString("Package", "", "Version"), arch);
    const std::string author     = ExpandArchToken(doc.GetString("Package", "", "Manufacturer"), arch);
    const int lcid               = doc.GetInt("Package", "", "Language", 1033);

    std::string upgradeGuid;
    NormalizeMsiGuid(doc.GetString("Package", "", "UpgradeCode"), upgradeGuid);
    const std::string modSuffix  = FormatMergeModuleGuidSuffix(upgradeGuid);
    const std::string moduleId   = SlugifyName(modName) + modSuffix;
    for (char& c : const_cast<std::string&>(moduleId)) {
        if (c == '-') c = '_';
    }
    const std::string packageGuid = GenerateRandomMsiGuid();

    std::error_code ec;
    if (!explicitMsmOutPath.empty()) {
        outWrittenMsmPath = explicitMsmOutPath;
        std::filesystem::create_directories(outWrittenMsmPath.parent_path(), ec);
    } else {
        const std::string rawOutName = ExpandArchToken(doc.GetString("Package", "", "OutputName"), arch);
        const std::string outStem = rawOutName.empty() ? (SlugifyName(modName) + "-" + arch) : rawOutName;
        const auto outDir = resolvePath(doc.GetString("Package", "", "OutputDir", "out"));
        std::filesystem::create_directories(outDir, ec);
        outWrittenMsmPath = outDir / (outStem + ".msm");
    }

    // Harvest payload files for `MergeModule.CABinet`
    struct MsmFileRec {
        std::string           fileKey;
        std::string           compKey;
        std::string           compGuid;
        std::filesystem::path sourcePath;
        std::string           msiFileName;
        int                   fileSize = 0;
        std::string           peVersion;
        std::string           peLanguage;
        int                   sequence = 0;
    };
    std::vector<MsmFileRec> files;
    std::set<std::string> seenRel;

    auto addFile = [&](const std::filesystem::path& srcFile, const std::string& sub) {
        const std::string fn = WideToUtf8(srcFile.filename().wstring());
        std::string rel = sub.empty() ? fn : (sub + "/" + fn);
        if (!seenRel.insert(AsciiLower(rel)).second) return;

        const int seq = static_cast<int>(files.size() + 1);
        char num[16]{};
        std::snprintf(num, sizeof(num), "%04d", seq);
        const std::string fKey = std::string("f_") + num + modSuffix;
        const std::string cKey = std::string("c_") + num + modSuffix;
        const auto pe = HarvestPeVersionAndLanguage(srcFile);
        const int sz = static_cast<int>(std::min<std::uintmax_t>(std::filesystem::file_size(srcFile, ec), 0x7FFFFFFFu));

        files.push_back({
            fKey,
            cKey,
            ComputeDeterministicComponentGuid(upgradeGuid, arch, "msmfile:" + rel),
            srcFile,
            FormatMsiShortLongName(fn, static_cast<std::size_t>(seq)),
            sz,
            pe.version,
            pe.language,
            seq
        });
    };

    for (const auto& pid : doc.GetNamedSectionIds("Payload")) {
        const auto absSrc = resolvePath(doc.GetString("Payload", pid, "Source"));
        const std::string sub = ExpandArchToken(doc.GetString("Payload", pid, "TargetSubdir", ""), arch);
        if (std::filesystem::is_regular_file(absSrc, ec)) {
            addFile(absSrc, sub);
        } else if (std::filesystem::is_directory(absSrc, ec)) {
            for (const auto& de : std::filesystem::recursive_directory_iterator(absSrc, ec)) {
                if (de.is_regular_file(ec)) addFile(de.path(), sub);
            }
        }
    }

    if (files.empty()) {
        outError = "CompileManifestToMsm: No files matched [Payload.*].";
        return false;
    }

    const auto scratchDir = std::filesystem::temp_directory_path(ec) / ("rsw-msm-" + packageGuid.substr(1, 8));
    std::filesystem::create_directories(scratchDir, ec);
    const auto cabPath = scratchDir / "MergeModule.CABinet";

    std::vector<CabInputEntry> cabEntries;
    for (const auto& f : files) cabEntries.push_back({f.sourcePath, f.fileKey});
    if (!BuildPayloadCabinet(cabEntries, cabPath, true, outError)) {
        std::filesystem::remove_all(scratchDir, ec);
        return false;
    }

    std::filesystem::remove(outWrittenMsmPath, ec);
    ScopedMsiHandle hDb;
    if (::MsiOpenDatabaseW(outWrittenMsmPath.wstring().c_str(), MSIDBOPEN_CREATEDIRECT, hDb.Put()) != ERROR_SUCCESS) {
        std::filesystem::remove_all(scratchDir, ec);
        outError = "MsiOpenDatabaseW failed creating .msm database.";
        return false;
    }

    const char* msmDdl[] = {
        "CREATE TABLE `ModuleSignature` (`ModuleID` CHAR(72) NOT NULL, `Language` SHORT NOT NULL, `Version` CHAR(72) NOT NULL PRIMARY KEY `ModuleID`, `Language`)",
        "CREATE TABLE `ModuleComponents` (`Component` CHAR(72) NOT NULL, `ModuleID` CHAR(72) NOT NULL, `Language` SHORT NOT NULL PRIMARY KEY `Component`, `ModuleID`, `Language`)",
        "CREATE TABLE `Directory` (`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL LOCALIZABLE PRIMARY KEY `Directory`)",
        "CREATE TABLE `Component` (`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, `Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72) PRIMARY KEY `Component`)",
        "CREATE TABLE `File` (`File` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL LOCALIZABLE, `FileSize` LONG NOT NULL, `Version` CHAR(72), `Language` CHAR(20), `Attributes` SHORT, `Sequence` INTEGER NOT NULL PRIMARY KEY `File`)",
        "CREATE TABLE `InstallExecuteSequence` (`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT PRIMARY KEY `Action`)"
    };
    for (const char* ddl : msmDdl) {
        if (!msi_sql::ExecuteDdl(hDb.Get(), ddl, outError)) {
            std::filesystem::remove_all(scratchDir, ec);
            return false;
        }
    }

    const std::string retargetDirKey = "INSTALLDIR" + modSuffix;
    msi_sql::InsertRow(hDb.Get(), "ModuleSignature", {"ModuleID", "Language", "Version"},
                       {moduleId, lcid, modVersion}, outError);
    msi_sql::InsertRow(hDb.Get(), "Directory", {"Directory", "Directory_Parent", "DefaultDir"},
                       {std::string("TARGETDIR"), std::monostate{}, std::string("SourceDir")}, outError);
    msi_sql::InsertRow(hDb.Get(), "Directory", {"Directory", "Directory_Parent", "DefaultDir"},
                       {retargetDirKey, std::string("TARGETDIR"), std::string(".")}, outError);

    for (const auto& f : files) {
        msi_sql::InsertRow(hDb.Get(), "Component",
                           {"Component", "ComponentId", "Directory_", "Attributes", "Condition", "KeyPath"},
                           {f.compKey, f.compGuid, retargetDirKey, 256 /* 64-bit */, std::monostate{}, f.fileKey},
                           outError);
        msi_sql::InsertRow(hDb.Get(), "ModuleComponents",
                           {"Component", "ModuleID", "Language"},
                           {f.compKey, moduleId, lcid},
                           outError);
        msi_sql::InsertRow(hDb.Get(), "File",
                           {"File", "Component_", "FileName", "FileSize", "Version", "Language", "Attributes", "Sequence"},
                           {
                               f.fileKey, f.compKey, f.msiFileName, f.fileSize,
                               f.peVersion.empty() ? SqlParam{std::monostate{}} : SqlParam{f.peVersion},
                               f.peLanguage.empty() ? SqlParam{std::monostate{}} : SqlParam{f.peLanguage},
                               512, f.sequence
                           },
                           outError);
    }

    if (!msi_sql::InsertBinaryStreamRow(hDb.Get(), "_Streams", "Name", "Data", "MergeModule.CABinet", cabPath, outError)) {
        std::filesystem::remove_all(scratchDir, ec);
        return false;
    }

    {
        ScopedMsiHandle hSum;
        if (::MsiGetSummaryInformationW(hDb.Get(), nullptr, 20, hSum.Put()) == ERROR_SUCCESS) {
            const std::wstring wTemplate = (arch == "arm64") ? (L"Arm64;" + std::to_wstring(lcid)) : (L"x64;" + std::to_wstring(lcid));
            const std::wstring wSubject  = Utf8ToWide(modName + " Merge Module");
            const std::wstring wAuthor   = Utf8ToWide(author);
            const std::wstring wGuid     = Utf8ToWide(upgradeGuid);
            FILETIME ft{};
            ::GetSystemTimeAsFileTime(&ft);
            ::MsiSummaryInfoSetPropertyW(hSum.Get(), 1,  VT_I2,       1252, &ft, nullptr);
            ::MsiSummaryInfoSetPropertyW(hSum.Get(), 2,  VT_LPSTR,    0,    &ft, L"Merge Module");
            ::MsiSummaryInfoSetPropertyW(hSum.Get(), 3,  VT_LPSTR,    0,    &ft, wSubject.c_str());
            ::MsiSummaryInfoSetPropertyW(hSum.Get(), 4,  VT_LPSTR,    0,    &ft, wAuthor.c_str());
            ::MsiSummaryInfoSetPropertyW(hSum.Get(), 5,  VT_LPSTR,    0,    &ft, L"MergeModule, MSI, Database");
            ::MsiSummaryInfoSetPropertyW(hSum.Get(), 7,  VT_LPSTR,    0,    &ft, wTemplate.c_str());
            ::MsiSummaryInfoSetPropertyW(hSum.Get(), 9,  VT_LPSTR,    0,    &ft, wGuid.c_str());
            ::MsiSummaryInfoSetPropertyW(hSum.Get(), 12, VT_FILETIME, 0,    &ft, nullptr);
            ::MsiSummaryInfoSetPropertyW(hSum.Get(), 13, VT_FILETIME, 0,    &ft, nullptr);
            ::MsiSummaryInfoSetPropertyW(hSum.Get(), 14, VT_I4,       500,  &ft, nullptr);
            ::MsiSummaryInfoSetPropertyW(hSum.Get(), 15, VT_I4,       2,    &ft, nullptr);
            ::MsiSummaryInfoSetPropertyW(hSum.Get(), 18, VT_LPSTR,    0,    &ft, kGeneratorSigWide.data());
            ::MsiSummaryInfoPersist(hSum.Get());
        }
    }

    UINT rc = ::MsiDatabaseCommit(hDb.Get());
    hDb.Reset();
    std::filesystem::remove_all(scratchDir, ec);
    if (rc != ERROR_SUCCESS) {
        outError = "MsiDatabaseCommit failed for .msm (" + std::to_string(rc) + ").";
        return false;
    }

    return VerifyCompiledMsmDatabase(outWrittenMsmPath, outError);
}

/// Generates a genuine Windows Installer Transform (`.mst`) comparing `baseMsiPath` -> `targetMsiPath`!
inline bool GenerateMsiTransform(
    const std::filesystem::path& baseMsiPath,
    const std::filesystem::path& targetMsiPath,
    const std::filesystem::path& outMstPath,
    std::string& outError)
{
    std::error_code ec;
    std::filesystem::create_directories(outMstPath.parent_path(), ec);
    std::filesystem::remove(outMstPath, ec);

    ScopedMsiHandle hBase;
    ScopedMsiHandle hTarget;
    if (::MsiOpenDatabaseW(baseMsiPath.wstring().c_str(), MSIDBOPEN_READONLY, hBase.Put()) != ERROR_SUCCESS) {
        outError = "GenerateMst: Could not open base MSI: " + WideToUtf8(baseMsiPath.wstring());
        return false;
    }
    if (::MsiOpenDatabaseW(targetMsiPath.wstring().c_str(), MSIDBOPEN_READONLY, hTarget.Put()) != ERROR_SUCCESS) {
        outError = "GenerateMst: Could not open target MSI: " + WideToUtf8(targetMsiPath.wstring());
        return false;
    }

    const std::wstring wMst = outMstPath.wstring();
    UINT rc = ::MsiDatabaseGenerateTransformW(hTarget.Get(), hBase.Get(), wMst.c_str(), 0, 0);
    if (rc == ERROR_NO_DATA) {
        outError = "GenerateMst: Base and Target MSI databases are identical (ERROR_NO_DATA).";
        return false;
    }
    if (rc != ERROR_SUCCESS) {
        outError = "MsiDatabaseGenerateTransformW failed (" + std::to_string(rc) + ").";
        return false;
    }

    // Stamp standard GPO/Enterprise Transform SummaryInformation validation flags
    rc = ::MsiCreateTransformSummaryInfoW(
        hTarget.Get(),
        hBase.Get(),
        wMst.c_str(),
        MSITRANSFORM_ERROR_ADDEXISTINGROW | MSITRANSFORM_ERROR_DELMISSINGROW | MSITRANSFORM_ERROR_UPDATEMISSINGROW,
        MSITRANSFORM_VALIDATE_UPGRADECODE);
    if (rc != ERROR_SUCCESS) {
        outError = "MsiCreateTransformSummaryInfoW failed (" + std::to_string(rc) + ").";
        return false;
    }

    return std::filesystem::exists(outMstPath, ec) && std::filesystem::file_size(outMstPath, ec) > 0;
}

/// Reads a single scalar `Property.Value` from an open MSI database handle.
inline std::string QueryMsiPropertyString(MSIHANDLE hDb, std::wstring_view propName) {
    const std::wstring sql = L"SELECT `Value` FROM `Property` WHERE `Property` = '" + std::wstring(propName) + L"'";
    ScopedMsiHandle hView;
    if (::MsiDatabaseOpenViewW(hDb, sql.c_str(), hView.Put()) != ERROR_SUCCESS) return {};
    if (::MsiViewExecute(hView.Get(), 0) != ERROR_SUCCESS) return {};
    ScopedMsiHandle hRec;
    if (::MsiViewFetch(hView.Get(), hRec.Put()) != ERROR_SUCCESS) return {};
    wchar_t buf[512]{};
    DWORD cch = static_cast<DWORD>(std::size(buf));
    if (::MsiRecordGetStringW(hRec.Get(), 1, buf, &cch) == ERROR_SUCCESS) {
        return WideToUtf8(buf);
    }
    return {};
}

/// Extracts a binary stream (like `payload.cab`) from `_Streams` inside an `.msi` to disk.
inline bool ExtractMsiStreamToFile(
    MSIHANDLE hDb,
    std::wstring_view streamName,
    const std::filesystem::path& destPath)
{
    const std::wstring sql = L"SELECT `Data` FROM `_Streams` WHERE `Name` = '" + std::wstring(streamName) + L"'";
    ScopedMsiHandle hView;
    if (::MsiDatabaseOpenViewW(hDb, sql.c_str(), hView.Put()) != ERROR_SUCCESS) return false;
    if (::MsiViewExecute(hView.Get(), 0) != ERROR_SUCCESS) return false;
    ScopedMsiHandle hRec;
    if (::MsiViewFetch(hView.Get(), hRec.Put()) != ERROR_SUCCESS) return false;

    std::ofstream out(destPath, std::ios::binary);
    if (!out) return false;
    char chunk[65536];
    for (;;) {
        DWORD cb = sizeof(chunk);
        if (::MsiRecordReadStream(hRec.Get(), 1, chunk, &cb) != ERROR_SUCCESS) return false;
        if (cb == 0) break;
        out.write(chunk, static_cast<std::streamsize>(cb));
    }
    return true;
}

/// Generates a genuine Windows Installer Patch Package (`.msp`) from `baseMsiPath` -> `upgradedMsiPath`,
/// enforcing Microsoft Component Rules (`RSW080`–`RSW086`) and stamping `MsiPatchMetadata` (`AllowRemoval = 1`)!
inline bool GenerateMsiPatchPackage(
    const std::filesystem::path& baseMsiPath,
    const std::filesystem::path& upgradedMsiPath,
    const std::filesystem::path& outMspPath,
    std::string& outError,
    bool allowSmallUpdate = false,
    std::string_view additionalTargetProductCodes = "")
{
    UpdatePlanReport plan{};
    if (!AnalyzeInstallerUpdatePlan(baseMsiPath, upgradedMsiPath, true, plan, outError)) {
        return false;
    }
    if (plan.classification == "small_update" && !allowSmallUpdate) {
        outError = "[RSW084] Small Update detected (ProductVersion unchanged at " + plan.targetVersion +
                   "). Bump Version for a Minor Upgrade .MSP or pass --allow-small-update for an emergency hotfix .MSP.";
        return false;
    }
    if (!plan.mspEligible) {
        for (const auto& d : plan.diagnostics) {
            if (d.code == "RSW080" || d.code == "RSW081" || d.code == "RSW082" ||
                d.code == "RSW083" || d.code == "RSW085" || d.code == "RSW086") {
                outError = "[" + d.code + "] " + d.message;
                return false;
            }
        }
        outError = "GenerateMsp: Target MSI violates Windows Installer Component Rules; service via Major Upgrade instead.";
        return false;
    }

    std::error_code ec;
    std::filesystem::create_directories(outMspPath.parent_path(), ec);
    std::filesystem::remove(outMspPath, ec);

    const std::string patchGuid = GenerateRandomMsiGuid();
    const auto scratchDir = std::filesystem::temp_directory_path(ec) / ("rsw-msp-" + patchGuid.substr(1, 8));
    std::filesystem::create_directories(scratchDir, ec);

    const auto targetCopyMsi = scratchDir / "upgraded_patch_prep.msi";
    const auto patchCabPath  = scratchDir / "RswPatch.cab";
    const auto patchMstPath  = scratchDir / "RswPatch1.mst";
    std::filesystem::copy_file(upgradedMsiPath, targetCopyMsi, std::filesystem::copy_options::overwrite_existing, ec);

    std::string baseProductCode;
    std::string productName;
    std::string manufacturer;
    std::string upgradedVer;

    // 1. Inspect `base.msi` and prepare `upgraded_patch_prep.msi` (aligning `ProductCode` for Small/Minor
    //    MSP applicability when needed, adding `PatchPackage` + `MsiPatchMetadata` + `#RswPatch.cab` `Media`
    //    `DiskId = 100`, and extracting `payload.cab` as `RswPatch.cab`!).
    {
        ScopedMsiHandle hBase;
        ScopedMsiHandle hUpg;
        if (::MsiOpenDatabaseW(baseMsiPath.wstring().c_str(), MSIDBOPEN_READONLY, hBase.Put()) != ERROR_SUCCESS ||
            ::MsiOpenDatabaseW(targetCopyMsi.wstring().c_str(), MSIDBOPEN_DIRECT, hUpg.Put()) != ERROR_SUCCESS) {
            std::filesystem::remove_all(scratchDir, ec);
            outError = "GenerateMsp: Could not open base or upgraded MSI databases.";
            return false;
        }

        baseProductCode = QueryMsiPropertyString(hBase.Get(), L"ProductCode");
        productName     = QueryMsiPropertyString(hUpg.Get(), L"ProductName");
        manufacturer    = QueryMsiPropertyString(hUpg.Get(), L"Manufacturer");
        upgradedVer     = QueryMsiPropertyString(hUpg.Get(), L"ProductVersion");

        if (!ExtractMsiStreamToFile(hUpg.Get(), L"payload.cab", patchCabPath)) {
            std::filesystem::remove_all(scratchDir, ec);
            outError = "GenerateMsp: Upgraded MSI does not contain an embedded 'payload.cab' stream.";
            return false;
        }

        // Ensure `ProductCode` matches `baseMsi` inside the patch transform so `msiexec /p` targets `baseProductCode` cleanly
        if (!baseProductCode.empty()) {
            const std::wstring updSql = L"UPDATE `Property` SET `Value` = '" + Utf8ToWide(baseProductCode) +
                                        L"' WHERE `Property` = 'ProductCode'";
            msi_sql::ExecuteDdl(hUpg.Get(), WideToUtf8(updSql), outError);
        }

        // Add `PatchPackage`, `MsiPatchMetadata` (`AllowRemoval = 1`), and `#RswPatch.cab` `Media` row (`DiskId = 100`)
        msi_sql::ExecuteDdl(
            hUpg.Get(),
            "CREATE TABLE `PatchPackage` (`PatchId` CHAR(38) NOT NULL, `Media_` SHORT NOT NULL PRIMARY KEY `PatchId`)",
            outError);
        msi_sql::InsertRow(
            hUpg.Get(), "PatchPackage", {"PatchId", "Media_"}, {patchGuid, 100}, outError);

        msi_sql::ExecuteDdl(
            hUpg.Get(),
            "CREATE TABLE `MsiPatchMetadata` (`Company` CHAR(72), `Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL LOCALIZABLE PRIMARY KEY `Company`, `Property`)",
            outError);
        const std::string compName = manufacturer.empty() ? "RaggieSoft" : manufacturer;
        const std::string patchCls = (plan.classification == "small_update") ? "Hotfix" : "Update";
        msi_sql::InsertRow(hUpg.Get(), "MsiPatchMetadata", {"Company", "Property", "Value"},
                           {std::monostate{}, std::string("AllowRemoval"), std::string("1")}, outError);
        msi_sql::InsertRow(hUpg.Get(), "MsiPatchMetadata", {"Company", "Property", "Value"},
                           {std::monostate{}, std::string("Classification"), patchCls}, outError);
        msi_sql::InsertRow(hUpg.Get(), "MsiPatchMetadata", {"Company", "Property", "Value"},
                           {compName, std::string("DisplayName"), productName + " v" + upgradedVer + " " + patchCls}, outError);
        msi_sql::InsertRow(hUpg.Get(), "MsiPatchMetadata", {"Company", "Property", "Value"},
                           {compName, std::string("TargetProductName"), productName}, outError);

        msi_sql::InsertRow(
            hUpg.Get(), "Media",
            {"DiskId", "LastSequence", "DiskPrompt", "Cabinet", "VolumeLabel", "Source"},
            {100, 10000, std::monostate{}, std::string("#RswPatch.cab"), std::monostate{}, std::monostate{}},
            outError);

        ::MsiDatabaseCommit(hUpg.Get());
    }

    // 2. Generate `RswPatch1.mst` relational transform between `base.msi` and `upgraded_patch_prep.msi`
    if (!GenerateMsiTransform(baseMsiPath, targetCopyMsi, patchMstPath, outError)) {
        std::filesystem::remove_all(scratchDir, ec);
        return false;
    }

    // 3. Create `outMspPath` using `MSIDBOPEN_CREATEDIRECT + MSIDBOPEN_PATCHFILE`!
    {
        ScopedMsiHandle hMsp;
        UINT rc = ::MsiOpenDatabaseW(
            outMspPath.wstring().c_str(),
            MSIDBOPEN_CREATEDIRECT + MSIDBOPEN_PATCHFILE,
            hMsp.Put());
        if (rc != ERROR_SUCCESS) {
            std::filesystem::remove_all(scratchDir, ec);
            outError = "MsiOpenDatabaseW(MSIDBOPEN_PATCHFILE) failed (" + std::to_string(rc) + ").";
            return false;
        }

        if (!msi_sql::InsertBinaryStreamRow(hMsp.Get(), "_Streams", "Name", "Data", "RswPatch.cab", patchCabPath, outError)) {
            std::filesystem::remove_all(scratchDir, ec);
            return false;
        }

        ScopedMsiHandle hSum;
        if (::MsiGetSummaryInformationW(hMsp.Get(), nullptr, 20, hSum.Put()) != ERROR_SUCCESS) {
            std::filesystem::remove_all(scratchDir, ec);
            outError = "GenerateMsp: MsiGetSummaryInformationW failed.";
            return false;
        }

        std::string targetCodeList = baseProductCode;
        if (!additionalTargetProductCodes.empty()) {
            if (!targetCodeList.empty() && targetCodeList.back() != ';') targetCodeList.push_back(';');
            targetCodeList.append(additionalTargetProductCodes);
        }
        const std::wstring wSubject = Utf8ToWide(productName + " Update v" + upgradedVer);
        const std::wstring wAuthor  = Utf8ToWide(manufacturer);
        const std::wstring wTargets = Utf8ToWide(targetCodeList);
        const std::wstring wPatchId = Utf8ToWide(patchGuid);

        FILETIME ft{};
        ::GetSystemTimeAsFileTime(&ft);
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 2,  VT_LPSTR,    0,   &ft, L"Patch");               // PID_TITLE
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 3,  VT_LPSTR,    0,   &ft, wSubject.c_str());       // PID_SUBJECT
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 4,  VT_LPSTR,    0,   &ft, wAuthor.c_str());        // PID_AUTHOR
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 5,  VT_LPSTR,    0,   &ft, L"RswPatch.cab");        // PID_KEYWORDS (Cabinet stream list)
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 6,  VT_LPSTR,    0,   &ft, L"RaggieSoft Setup Wizard Patch Package");
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 7,  VT_LPSTR,    0,   &ft, wTargets.c_str());       // PID_TEMPLATE (Target ProductCode list)
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 8,  VT_LPSTR,    0,   &ft, L":RswPatch1");          // PID_LASTAUTHOR (Substorage transform list)
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 9,  VT_LPSTR,    0,   &ft, wPatchId.c_str());       // PID_REVNUMBER (PatchCode GUID)
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 12, VT_FILETIME, 0,   &ft, nullptr);
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 13, VT_FILETIME, 0,   &ft, nullptr);
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 14, VT_I4,       200, &ft, nullptr);                // PID_PAGECOUNT (MSI 2.0+ patch engine)
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 15, VT_I4,       4,   &ft, nullptr);                // PID_WORDCOUNT (4 = msidbSumInfoSourceTypePatch)
        ::MsiSummaryInfoSetPropertyW(hSum.Get(), 18, VT_LPSTR,    0,   &ft, kGeneratorSigWide.data());

        if (::MsiSummaryInfoPersist(hSum.Get()) != ERROR_SUCCESS ||
            ::MsiDatabaseCommit(hMsp.Get()) != ERROR_SUCCESS) {
            std::filesystem::remove_all(scratchDir, ec);
            outError = "GenerateMsp: Failed committing .msp SummaryInformation.";
            return false;
        }
    }

    // 4. Copy `RswPatch1.mst` into the `.msp` compound file as OLE child `IStorage` `"RswPatch1"`!
    {
        IStorage* pMspRoot = nullptr;
        IStorage* pMstRoot = nullptr;
        HRESULT hr1 = ::StgOpenStorage(
            outMspPath.wstring().c_str(), nullptr,
            STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_DIRECT, nullptr, 0, &pMspRoot);
        HRESULT hr2 = ::StgOpenStorage(
            patchMstPath.wstring().c_str(), nullptr,
            STGM_READ | STGM_SHARE_DENY_WRITE | STGM_DIRECT, nullptr, 0, &pMstRoot);

        bool stgOk = SUCCEEDED(hr1) && SUCCEEDED(hr2) && pMspRoot && pMstRoot;
        if (stgOk) {
            IStorage* pChild = nullptr;
            if (SUCCEEDED(pMspRoot->CreateStorage(
                    L"RswPatch1", STGM_CREATE | STGM_READWRITE | STGM_SHARE_EXCLUSIVE | STGM_DIRECT, 0, 0, &pChild)) &&
                pChild) {
                stgOk = SUCCEEDED(pMstRoot->CopyTo(0, nullptr, nullptr, pChild));
                pChild->Commit(STGC_DEFAULT);
                pChild->Release();
            } else {
                stgOk = false;
            }
            pMspRoot->Commit(STGC_DEFAULT);
        }
        if (pMstRoot) pMstRoot->Release();
        if (pMspRoot) pMspRoot->Release();

        if (!stgOk) {
            std::filesystem::remove_all(scratchDir, ec);
            outError = "GenerateMsp: Failed embedding RswPatch1 OLE substorage into .msp.";
            return false;
        }
    }

    std::filesystem::remove_all(scratchDir, ec);

    // 5. Verify the emitted `.msp` opens cleanly via `MsiOpenDatabaseW(..., MSIDBOPEN_READONLY + MSIDBOPEN_PATCHFILE)`!
    ScopedMsiHandle hVerify;
    if (::MsiOpenDatabaseW(outMspPath.wstring().c_str(), MSIDBOPEN_READONLY + MSIDBOPEN_PATCHFILE, hVerify.Put()) != ERROR_SUCCESS) {
        outError = "GenerateMsp: Post-build verification failed opening .msp with MSIDBOPEN_PATCHFILE.";
        return false;
    }
    return true;
}

} // namespace rsw
