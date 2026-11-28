// ================================================================================
// FILE: src/compiler/RswUpdatePlanner.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `instbuild.exe --plan-update <old.msi|.exe> <new.msi|.exe>`?
//     Windows Installer (`msi.dll`) supports three distinct update classifications,
//     each governed by strict relational database rules:
//       1. **Major Upgrade** (New `ProductCode` + Higher `ProductVersion` + Same `UpgradeCode`):
//          - Cleanly replaces previous versions via `RemoveExistingProducts`, allowing
//            freely added, renamed, moved, or deleted Components and Features!
//       2. **Minor Upgrade** (Same `ProductCode` + Higher `ProductVersion` — `.MSP` or `REINSTALL=ALL REINSTALLMODE=vomus`):
//          - Fast in-place servicing, EXCEPT Microsoft's Component Rules strictly FORBID:
//              * `RSW081`: Deleting an existing `ComponentId` GUID (`SELMGR` / orphan hazard)
//              * `RSW082`: Changing the `KeyPath` of an existing `ComponentId`
//              * `RSW083`: Moving an existing `ComponentId` to a different `Directory_`
//              * `RSW086`: Removing a `File` from an existing `ComponentId` without changing GUID
//       3. **Small Update Hotfix** (Same `ProductCode` + Same `ProductVersion`, opt-in `--allow-small-update`):
//          - Emergency single-binary hotfix `.MSP` where even `ProductVersion` stays unchanged
//            (`RSW084` warns/blocks accidental Small Updates unless `--allow-small-update` is passed).
//
//     And for **RaggieScript EXE** installers (`Engine = raggiescript` / `.rssc`),
//     `AnalyzeInstallerUpdatePlan` inspects the embedded `RSWPAK1` (`#220`) archives,
//     counts added/modified/retired/unchanged files, and confirms `.rswpatch` delta
//     eligibility via `RsPatch.exe`!
// ================================================================================

#pragma once

#include "../common/RswBinaryDelta.hpp"
#include "../common/RswLzmsCompression.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswPrerequisites.hpp"
#include "../common/RswVersion.hpp"
#include "RswValidator.hpp"

#include <msi.h>
#include <msiquery.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace rsw {

struct UpdatePlanReport {
    std::filesystem::path      baselinePath;
    std::filesystem::path      targetPath;
    std::string                engineKind        = "msi"; // `"msi"` or `"raggiescript_exe"`
    std::string                productName;
    std::string                upgradeCode;
    std::string                baseProductCode;
    std::string                targetProductCode;
    std::string                baseVersion;
    std::string                targetVersion;
    std::string                classification    = "major_upgrade"; // `"major_upgrade"` | `"minor_upgrade"` | `"small_update"` | `"raggiescript_update"` | `"invalid"`
    std::string                recommendedAction = "major_upgrade"; // `"major_upgrade"` | `"minor_msp_or_major"` | `"small_hotfix_msp"` | `"raggiescript_rswpatch"`
    bool                       mspEligible       = false;
    bool                       rswpatchEligible  = false;
    std::size_t                addedFiles        = 0;
    std::size_t                modifiedFiles     = 0;
    std::size_t                removedFiles      = 0;
    std::size_t                unchangedFiles    = 0;
    std::vector<std::string>   changedFileKeys;  // MSI `File.File` keys or relative paths added/modified
    std::vector<RswDiagnostic> diagnostics;

    [[nodiscard]] bool HasBlockingErrors() const noexcept {
        for (const auto& d : diagnostics) {
            if (d.severity == DiagnosticSeverity::Error) return true;
        }
        return false;
    }
};

namespace update_plan_detail {

struct MsiCompRow {
    std::string compKey;
    std::string compGuid;
    std::string dirKey;
    std::string keyPath;
};

struct MsiFileRow {
    std::string fileKey;
    std::string compKey;
    std::string fileName;
    int         fileSize = 0;
    std::string version;
    int         sequence = 0;
};

struct MsiSnapshot {
    std::string productName;
    std::string productCode;
    std::string upgradeCode;
    std::string productVersion;
    std::map<std::string, MsiCompRow> compsByGuidLower;
    std::map<std::string, MsiCompRow> compsByKey;
    std::map<std::string, MsiFileRow> filesByKey;
    std::map<std::string, std::set<std::string>> compKeyToFileNamesLower;
};

inline std::string ReadRecStr(MSIHANDLE hRec, UINT field) {
    wchar_t buf[1024]{};
    DWORD cch = static_cast<DWORD>(std::size(buf));
    if (::MsiRecordGetStringW(hRec, field, buf, &cch) == ERROR_SUCCESS) {
        return WideToUtf8(buf);
    }
    return {};
}

inline bool LoadMsiSnapshot(const std::filesystem::path& msiPath, MsiSnapshot& outSnap, std::string& outError) {
    outSnap = {};
    MSIHANDLE rawDb = 0;
    if (::MsiOpenDatabaseW(msiPath.wstring().c_str(), MSIDBOPEN_READONLY, &rawDb) != ERROR_SUCCESS) {
        outError = "Could not open MSI database '" + WideToUtf8(msiPath.wstring()) + "'.";
        return false;
    }

    auto readProps = [&]() {
        MSIHANDLE hV = 0, hR = 0;
        if (::MsiDatabaseOpenViewW(rawDb, L"SELECT `Property`, `Value` FROM `Property`", &hV) == ERROR_SUCCESS &&
            ::MsiViewExecute(hV, 0) == ERROR_SUCCESS) {
            while (::MsiViewFetch(hV, &hR) == ERROR_SUCCESS) {
                const std::string k = ReadRecStr(hR, 1);
                const std::string v = ReadRecStr(hR, 2);
                if (k == "ProductName")         outSnap.productName    = v;
                else if (k == "ProductCode")    outSnap.productCode    = v;
                else if (k == "UpgradeCode")    outSnap.upgradeCode    = v;
                else if (k == "ProductVersion") outSnap.productVersion = v;
                ::MsiCloseHandle(hR);
                hR = 0;
            }
        }
        if (hV) ::MsiCloseHandle(hV);
    };
    readProps();

    {
        MSIHANDLE hV = 0, hR = 0;
        if (::MsiDatabaseOpenViewW(rawDb, L"SELECT `Component`, `ComponentId`, `Directory_`, `KeyPath` FROM `Component`", &hV) == ERROR_SUCCESS &&
            ::MsiViewExecute(hV, 0) == ERROR_SUCCESS) {
            while (::MsiViewFetch(hV, &hR) == ERROR_SUCCESS) {
                MsiCompRow c{};
                c.compKey  = ReadRecStr(hR, 1);
                c.compGuid = ReadRecStr(hR, 2);
                c.dirKey   = ReadRecStr(hR, 3);
                c.keyPath  = ReadRecStr(hR, 4);
                outSnap.compsByKey[c.compKey] = c;
                if (!c.compGuid.empty()) {
                    outSnap.compsByGuidLower[AsciiLower(c.compGuid)] = c;
                }
                ::MsiCloseHandle(hR);
                hR = 0;
            }
        }
        if (hV) ::MsiCloseHandle(hV);
    }

    {
        MSIHANDLE hV = 0, hR = 0;
        if (::MsiDatabaseOpenViewW(rawDb, L"SELECT `File`, `Component_`, `FileName`, `FileSize`, `Version`, `Sequence` FROM `File`", &hV) == ERROR_SUCCESS &&
            ::MsiViewExecute(hV, 0) == ERROR_SUCCESS) {
            while (::MsiViewFetch(hV, &hR) == ERROR_SUCCESS) {
                MsiFileRow f{};
                f.fileKey  = ReadRecStr(hR, 1);
                f.compKey  = ReadRecStr(hR, 2);
                f.fileName = ReadRecStr(hR, 3);
                f.fileSize = ::MsiRecordGetInteger(hR, 4);
                f.version  = ReadRecStr(hR, 5);
                f.sequence = ::MsiRecordGetInteger(hR, 6);
                outSnap.filesByKey[f.fileKey] = f;
                const auto bar = f.fileName.find('|');
                const std::string longName = (bar == std::string::npos) ? f.fileName : f.fileName.substr(bar + 1);
                outSnap.compKeyToFileNamesLower[f.compKey].insert(AsciiLower(longName));
                ::MsiCloseHandle(hR);
                hR = 0;
            }
        }
        if (hV) ::MsiCloseHandle(hV);
    }

    ::MsiCloseHandle(rawDb);
    return true;
}

/// If `inputPath` is a `-setup.exe` wrapping an embedded `Product.msi` (`#201`), extracts it to `outTempMsi`!
inline bool TryExtractEmbeddedMsiFromSetupExe(
    const std::filesystem::path& exePath,
    const std::filesystem::path& outTempMsi)
{
    HMODULE hMod = ::LoadLibraryExW(
        exePath.wstring().c_str(), nullptr,
        LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_IMAGE_RESOURCE);
    if (!hMod) return false;
    HRSRC hR = ::FindResourceW(hMod, MAKEINTRESOURCEW(kResIdEmbeddedMsi), RT_RCDATA);
    if (!hR) {
        ::FreeLibrary(hMod);
        return false;
    }
    const DWORD sz = ::SizeofResource(hMod, hR);
    HGLOBAL hG = ::LoadResource(hMod, hR);
    const auto* ptr = hG ? static_cast<const std::uint8_t*>(::LockResource(hG)) : nullptr;
    std::vector<std::uint8_t> comp;
    if (ptr && sz > 0) comp.assign(ptr, ptr + sz);
    ::FreeLibrary(hMod);
    if (comp.empty()) return false;

    std::vector<std::uint8_t> raw;
    std::string err;
    if (!DecompressLzmsOrCopyRaw(comp.data(), comp.size(), raw, err) || raw.empty()) return false;
    std::ofstream out(outTempMsi, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(reinterpret_cast<const char*>(raw.data()), static_cast<std::streamsize>(raw.size()));
    return out.good();
}

} // namespace update_plan_detail

/// Analyzes two `.msi` databases or two **RaggieScript EXE** installers and computes update classification,
/// file/component diffs, `.MSP` / `.rswpatch` eligibility, and `RSW080`–`RSW086` diagnostics!
inline bool AnalyzeInstallerUpdatePlan(
    const std::filesystem::path& baselinePath,
    const std::filesystem::path& targetPath,
    bool allowSmallUpdate,
    UpdatePlanReport& outPlan,
    std::string& outError)
{
    using namespace update_plan_detail;
    outPlan = {};
    outPlan.baselinePath = baselinePath;
    outPlan.targetPath   = targetPath;

    std::error_code ec;
    if (!std::filesystem::exists(baselinePath, ec)) {
        outError = "Baseline package does not exist: " + WideToUtf8(baselinePath.wstring());
        return false;
    }
    if (!std::filesystem::exists(targetPath, ec)) {
        outError = "Target package does not exist: " + WideToUtf8(targetPath.wstring());
        return false;
    }

    auto addDiag = [&](DiagnosticSeverity sev, std::string code, std::string key, std::string msg) {
        outPlan.diagnostics.push_back({sev, std::move(code), 1, "Upgrade", std::move(key), std::move(msg)});
    };

    const std::string baseExt = AsciiLower(WideToUtf8(baselinePath.extension().wstring()));
    const std::string targExt = AsciiLower(WideToUtf8(targetPath.extension().wstring()));

    const auto scratchDir = std::filesystem::temp_directory_path(ec) /
                            (L"rsw-plan-" + std::to_wstring(::GetCurrentProcessId()));
    std::filesystem::create_directories(scratchDir, ec);

    std::filesystem::path effBaseMsi = baselinePath;
    std::filesystem::path effTargMsi = targetPath;
    bool isMsiPair = (baseExt == ".msi" && targExt == ".msi");

    if (!isMsiPair && baseExt == ".exe" && targExt == ".exe") {
        const auto tmpB = scratchDir / L"base_extracted.msi";
        const auto tmpT = scratchDir / L"targ_extracted.msi";
        if (TryExtractEmbeddedMsiFromSetupExe(baselinePath, tmpB) &&
            TryExtractEmbeddedMsiFromSetupExe(targetPath, tmpT)) {
            effBaseMsi = tmpB;
            effTargMsi = tmpT;
            isMsiPair  = true;
        }
    }

    // --- PATH A: RAGGIESCRIPT EXE (`#220 RSWPAK1`) DIFF & DELTA PLANNER ---
    if (!isMsiPair) {
        outPlan.engineKind = "raggiescript_exe";
        std::map<std::string, patch_io_detail::SnapshotFile> oldMap, newMap;
        std::string oldName, oldUpg, oldArch, oldVer;
        std::string newName, newUpg, newArch, newVer;
        if (!patch_io_detail::LoadInstallTreeSnapshot(baselinePath, oldMap, oldName, oldUpg, oldArch, oldVer, outError) ||
            !patch_io_detail::LoadInstallTreeSnapshot(targetPath, newMap, newName, newUpg, newArch, newVer, outError)) {
            std::filesystem::remove_all(scratchDir, ec);
            return false;
        }
        std::filesystem::remove_all(scratchDir, ec);

        outPlan.productName       = !newName.empty() ? newName : oldName;
        outPlan.upgradeCode       = !newUpg.empty()  ? newUpg  : oldUpg;
        outPlan.baseVersion       = oldVer;
        outPlan.targetVersion     = newVer;
        outPlan.classification    = "raggiescript_update";
        outPlan.recommendedAction = "raggiescript_rswpatch";

        if (!oldUpg.empty() && !newUpg.empty() && !EqualsIgnoreCase(oldUpg, newUpg)) {
            outPlan.classification = "invalid";
            addDiag(DiagnosticSeverity::Error, "RSW080", "UpgradeCode",
                    "UpgradeCode mismatch between RaggieScript EXE builds (" + oldUpg + " vs " + newUpg + ").");
        }
        const int vCmp = CompareDottedVersions(newVer, oldVer);
        if (!oldVer.empty() && !newVer.empty() && vCmp < 0) {
            outPlan.classification = "invalid";
            addDiag(DiagnosticSeverity::Error, "RSW085", "Version",
                    "Target RaggieScript EXE version (" + newVer + ") is older than baseline (" + oldVer + ").");
        } else if (!oldVer.empty() && !newVer.empty() && vCmp == 0 && !allowSmallUpdate) {
            addDiag(DiagnosticSeverity::Error, "RSW084", "Version",
                    "Target RaggieScript EXE version (" + newVer + ") equals baseline; bump Version or pass --allow-small-update.");
        }

        for (const auto& [k, nf] : newMap) {
            auto it = oldMap.find(k);
            if (it == oldMap.end()) {
                ++outPlan.addedFiles;
                outPlan.changedFileKeys.push_back(nf.relPath);
            } else if (!EqualsIgnoreCase(it->second.sha256, nf.sha256)) {
                ++outPlan.modifiedFiles;
                outPlan.changedFileKeys.push_back(nf.relPath);
            } else {
                ++outPlan.unchangedFiles;
            }
        }
        for (const auto& [k, of] : oldMap) {
            if (newMap.find(k) == newMap.end()) {
                ++outPlan.removedFiles;
            }
        }
        outPlan.rswpatchEligible = !outPlan.HasBlockingErrors();
        return true;
    }

    // --- PATH B: WINDOWS INSTALLER (`.MSI` / `.MSP`) COMPONENT-RULE & UPDATE PLANNER ---
    MsiSnapshot baseSnap, targSnap;
    const bool okLoad = LoadMsiSnapshot(effBaseMsi, baseSnap, outError) &&
                        LoadMsiSnapshot(effTargMsi, targSnap, outError);
    std::filesystem::remove_all(scratchDir, ec);
    if (!okLoad) return false;

    outPlan.engineKind        = "msi";
    outPlan.productName       = !targSnap.productName.empty() ? targSnap.productName : baseSnap.productName;
    outPlan.upgradeCode       = targSnap.upgradeCode;
    outPlan.baseProductCode   = baseSnap.productCode;
    outPlan.targetProductCode = targSnap.productCode;
    outPlan.baseVersion       = baseSnap.productVersion;
    outPlan.targetVersion     = targSnap.productVersion;

    if (!EqualsIgnoreCase(baseSnap.upgradeCode, targSnap.upgradeCode)) {
        outPlan.classification = "invalid";
        addDiag(DiagnosticSeverity::Error, "RSW080", "UpgradeCode",
                "UpgradeCode mismatch between baseline (" + baseSnap.upgradeCode + ") and target (" + targSnap.upgradeCode + ").");
        return true;
    }

    const int verCmp      = CompareDottedVersions(targSnap.productVersion, baseSnap.productVersion);
    const bool sameProdId = EqualsIgnoreCase(baseSnap.productCode, targSnap.productCode);

    if (verCmp < 0) {
        outPlan.classification = "invalid";
        addDiag(DiagnosticSeverity::Error, "RSW085", "Version",
                "Target ProductVersion (" + targSnap.productVersion + ") is lower than baseline (" + baseSnap.productVersion + ").");
        return true;
    }

    if (verCmp == 0) {
        outPlan.classification = "small_update";
        if (!allowSmallUpdate) {
            addDiag(DiagnosticSeverity::Error, "RSW084", "Version",
                    "Small Update detected (ProductVersion unchanged at " + targSnap.productVersion +
                    "). Bump Version for a Minor/Major Upgrade or pass --allow-small-update for an emergency hotfix .MSP.");
        }
    } else if (sameProdId) {
        outPlan.classification = "minor_upgrade";
    } else {
        outPlan.classification = "major_upgrade";
    }

    // Audit Microsoft Component Rules across `baseSnap` -> `targSnap`!
    // Note: For a `major_upgrade` (different `ProductCode`), `RSW081`–`RSW086` are emitted as
    // informational Warnings explaining why `.MSP` patching is ineligible while `major_upgrade` is 100% legal.
    // For a `minor_upgrade` or `small_update` (same `ProductCode`), `RSW081`–`RSW086` are hard Errors!
    const DiagnosticSeverity compRuleSev =
        (outPlan.classification == "major_upgrade") ? DiagnosticSeverity::Warning : DiagnosticSeverity::Error;
    bool compRulesCleanForMsp = true;

    for (const auto& [guidLow, bComp] : baseSnap.compsByGuidLower) {
        auto itT = targSnap.compsByGuidLower.find(guidLow);
        if (itT == targSnap.compsByGuidLower.end()) {
            compRulesCleanForMsp = false;
            addDiag(compRuleSev, "RSW081", "Component",
                    "Baseline Component '" + bComp.compKey + "' (" + bComp.compGuid +
                    ") was removed in target. Minor/Small .MSP patches cannot delete Components; service as a Major Upgrade.");
            continue;
        }
        const auto& tComp = itT->second;
        if (!EqualsIgnoreCase(bComp.dirKey, tComp.dirKey)) {
            compRulesCleanForMsp = false;
            addDiag(compRuleSev, "RSW083", "Component",
                    "Component '" + bComp.compKey + "' (" + bComp.compGuid + ") changed Directory_ from '" +
                    bComp.dirKey + "' to '" + tComp.dirKey + "'. Minor/Small .MSP patches cannot move Components.");
        }

        // Compare KeyPath logical identity (file name or registry/folder key)
        std::string bKpIdentity = bComp.keyPath;
        std::string tKpIdentity = tComp.keyPath;
        if (auto bf = baseSnap.filesByKey.find(bComp.keyPath); bf != baseSnap.filesByKey.end()) {
            const auto bar = bf->second.fileName.find('|');
            bKpIdentity = (bar == std::string::npos) ? bf->second.fileName : bf->second.fileName.substr(bar + 1);
        }
        if (auto tf = targSnap.filesByKey.find(tComp.keyPath); tf != targSnap.filesByKey.end()) {
            const auto bar = tf->second.fileName.find('|');
            tKpIdentity = (bar == std::string::npos) ? tf->second.fileName : tf->second.fileName.substr(bar + 1);
        }
        if (!EqualsIgnoreCase(bKpIdentity, tKpIdentity)) {
            compRulesCleanForMsp = false;
            addDiag(compRuleSev, "RSW082", "KeyPath",
                    "Component '" + bComp.compKey + "' (" + bComp.compGuid + ") changed KeyPath from '" +
                    bKpIdentity + "' to '" + tKpIdentity + "'. Minor/Small .MSP patches cannot alter KeyPaths.");
        }

        const auto& bFiles = baseSnap.compKeyToFileNamesLower[bComp.compKey];
        const auto& tFiles = targSnap.compKeyToFileNamesLower[tComp.compKey];
        for (const auto& fnLow : bFiles) {
            if (tFiles.find(fnLow) == tFiles.end()) {
                compRulesCleanForMsp = false;
                addDiag(compRuleSev, "RSW086", "File",
                        "File '" + fnLow + "' was removed from existing Component '" + bComp.compKey +
                        "' without minting a new Component GUID; service as a Major Upgrade.");
            }
        }
    }

    // Diff `File` table (`addedFiles`, `modifiedFiles`, `unchangedFiles`, `removedFiles`)
    for (const auto& [fk, tf] : targSnap.filesByKey) {
        auto itB = baseSnap.filesByKey.find(fk);
        if (itB == baseSnap.filesByKey.end()) {
            ++outPlan.addedFiles;
            outPlan.changedFileKeys.push_back(fk);
        } else {
            const auto& bf = itB->second;
            if (bf.fileSize != tf.fileSize || bf.version != tf.version || bf.fileName != tf.fileName) {
                ++outPlan.modifiedFiles;
                outPlan.changedFileKeys.push_back(fk);
            } else {
                // Note: If an unversioned data file kept the exact same byte size, include it in
                // `changedFileKeys` if its version is empty so `.MSP` patches never miss same-size text edits!
                if (tf.version.empty()) {
                    ++outPlan.modifiedFiles;
                    outPlan.changedFileKeys.push_back(fk);
                } else {
                    ++outPlan.unchangedFiles;
                }
            }
        }
    }
    for (const auto& [fk, bf] : baseSnap.filesByKey) {
        (void)bf;
        if (targSnap.filesByKey.find(fk) == targSnap.filesByKey.end()) {
            ++outPlan.removedFiles;
        }
    }

    outPlan.mspEligible = compRulesCleanForMsp && !outPlan.HasBlockingErrors();
    if (outPlan.classification == "small_update" && outPlan.mspEligible) {
        outPlan.recommendedAction = "small_hotfix_msp";
    } else if (outPlan.mspEligible) {
        outPlan.recommendedAction = (outPlan.classification == "minor_upgrade") ? "minor_msp" : "minor_msp_or_major";
    } else {
        outPlan.recommendedAction = "major_upgrade";
    }
    return true;
}

/// Formats `UpdatePlanReport` as deterministic JSON (`--diagnostics=json`, zero comments).
inline std::string FormatUpdatePlanAsJson(const UpdatePlanReport& plan) {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"ok\": " << (!plan.HasBlockingErrors() ? "true" : "false") << ",\n"
        << "  \"engine\": \"" << JsonEscapeString(plan.engineKind) << "\",\n"
        << "  \"productName\": \"" << JsonEscapeString(plan.productName) << "\",\n"
        << "  \"upgradeCode\": \"" << JsonEscapeString(plan.upgradeCode) << "\",\n"
        << "  \"baseProductCode\": \"" << JsonEscapeString(plan.baseProductCode) << "\",\n"
        << "  \"targetProductCode\": \"" << JsonEscapeString(plan.targetProductCode) << "\",\n"
        << "  \"baseVersion\": \"" << JsonEscapeString(plan.baseVersion) << "\",\n"
        << "  \"targetVersion\": \"" << JsonEscapeString(plan.targetVersion) << "\",\n"
        << "  \"classification\": \"" << JsonEscapeString(plan.classification) << "\",\n"
        << "  \"recommendedAction\": \"" << JsonEscapeString(plan.recommendedAction) << "\",\n"
        << "  \"mspEligible\": " << (plan.mspEligible ? "true" : "false") << ",\n"
        << "  \"rswpatchEligible\": " << (plan.rswpatchEligible ? "true" : "false") << ",\n"
        << "  \"addedFiles\": " << plan.addedFiles << ",\n"
        << "  \"modifiedFiles\": " << plan.modifiedFiles << ",\n"
        << "  \"removedFiles\": " << plan.removedFiles << ",\n"
        << "  \"unchangedFiles\": " << plan.unchangedFiles << ",\n"
        << "  \"diagnostics\": [";
    for (std::size_t i = 0; i < plan.diagnostics.size(); ++i) {
        const auto& d = plan.diagnostics[i];
        oss << (i == 0 ? "\n" : ",\n")
            << "    {\n"
            << "      \"severity\": \"" << (d.severity == DiagnosticSeverity::Error ? "error" : "warning") << "\",\n"
            << "      \"code\": \"" << JsonEscapeString(d.code) << "\",\n"
            << "      \"key\": \"" << JsonEscapeString(d.key) << "\",\n"
            << "      \"message\": \"" << JsonEscapeString(d.message) << "\"\n"
            << "    }";
    }
    if (!plan.diagnostics.empty()) oss << "\n  ";
    oss << "]\n}\n";
    return oss.str();
}

/// Formats `UpdatePlanReport` as a clean human-readable CLI summary.
inline std::string FormatUpdatePlanAsText(const UpdatePlanReport& plan) {
    std::ostringstream oss;
    oss << "====================================================================\n"
        << "  RaggieSoft Update & Component-Rule Planner (v" << kVersionString << ")\n"
        << "====================================================================\n"
        << "  Product          : " << plan.productName << "\n"
        << "  Engine           : " << (plan.engineKind == "raggiescript_exe" ? "RaggieScript EXE" : "Windows Installer (MSI)") << "\n"
        << "  UpgradeCode      : " << plan.upgradeCode << "\n"
        << "  Version Diff     : " << plan.baseVersion << " -> " << plan.targetVersion << "\n"
        << "  Classification   : " << plan.classification << "\n"
        << "  Recommended Path : " << plan.recommendedAction << "\n"
        << "  .MSP Eligible    : " << (plan.mspEligible ? "Yes (Component Rules clean)" : "No") << "\n"
        << "  .rswpatch Ready  : " << (plan.rswpatchEligible ? "Yes (msdelta.dll binary delta)" : "No") << "\n"
        << "  File Summary     : +" << plan.addedFiles << " added, ~" << plan.modifiedFiles
        << " modified, -" << plan.removedFiles << " removed, =" << plan.unchangedFiles << " unchanged\n";
    for (const auto& d : plan.diagnostics) {
        oss << "  [" << d.code << " " << (d.severity == DiagnosticSeverity::Error ? "ERROR" : "NOTE") << "] "
            << d.message << "\n";
    }
    oss << "====================================================================\n";
    return oss.str();
}

} // namespace rsw
