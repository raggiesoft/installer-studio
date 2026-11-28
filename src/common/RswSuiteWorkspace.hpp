// ================================================================================
// FILE: src/common/RswSuiteWorkspace.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   How do `.rsip` (Project) and `.rsis` (Suite) fit together?
//     Think of how modern .NET 10 pairs `.csproj` with `.slnx`:
//       - **Single-App Simplicity (`.rsip`)**: If you are an indie developer or
//         open-source maintainer shipping one application, you ONLY need a single
//         `.rsip` file (`instbuild my-app.rsip`). Zero workspace ceremony!
//       - **Enterprise & Multi-Package Suites (`.rsis`)**: When a studio or
//         enterprise team manages dozens of packages—such as shared `.msm` Merge
//         Modules that must build first (`BuildOrder = 10`), followed by Desktop
//         IDEs (`BuildOrder = 20`), Book packs (`BuildOrder = 30`), and Album
//         packs (`BuildOrder = 40`)—a plain-text `.rsis` Suite file groups all
//         member `.rsip` projects, shared `[Variable]` tokens (`$(PUBLISHER)`),
//         and dependency build order in one Git-friendly file!
// ================================================================================

#pragma once

#include "RswManifestAst.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rsw {

struct RswSuiteProjectEntry {
    std::string           id;            // `<Id>` from `[Project.<Id>]`
    std::filesystem::path resolvedPath;  // Absolute/resolved path to member `.rsip`
    std::string           rawRelPath;    // Original relative path written in `.rsis`
    std::string           category;      // Optional grouping (`CoreApps`, `Books`, `Albums`, `MergeModules`)
    int                   buildOrder        = 100;
    int                   declIndex         = 0;
    bool                  enabled           = true;
    bool                  includeInSuite    = true;  // Bundled into unified Suite Setup when `BuildSuiteInstaller = true`
    bool                  requiredInSuite   = false; // Locked as required in the Suite checklist
    bool                  selectedByDefault = true;  // Pre-checked by default in the Suite checklist
    bool                  rollbackBoundary  = false; // Savepoint boundary when `RollbackPolicy = rollback_new`
    bool                  forwardInstallDir = false; // Passes `SUITE_INSTALLDIR="<dir>"` to child setup
};

struct RswSuiteWorkspace {
    std::filesystem::path                suiteFilePath;
    std::string                          name;
    std::string                          description;
    std::string                          tagline;
    std::string                          version;
    std::string                          manufacturer            = "RaggieSoft";
    std::string                          upgradeCode;
    std::string                          scope                   = "machine";
    std::string                          requestedExecutionLevel = "asInvoker";
    std::string                          rollbackPolicy          = "keep_completed";
    std::string                          suiteDelivery           = "auto"; // `auto` | `embedded` | `side-by-side`
    std::string                          outputName;
    std::string                          outputDir               = "out";
    std::string                          defaultSubdir;
    std::string                          accentPrimaryHex        = "#2563EB";
    std::string                          accentSecondaryHex      = "#FBBF24";
    bool                                 buildSuiteInstaller     = false; // Emits unified `<OutputName>-setup.exe` chainer!
    bool                                 buildUniversal          = false; // Emits `x64 + ARM64` combined Universal Suite EXE!
    bool                                 showComponentProgressBar = true; // Secondary `Current Component` bar beneath Overall!
    std::vector<std::string>             defaultArchitectures;
    std::map<std::string, std::string>   variables;
    std::vector<RswSuiteProjectEntry>    projects;
};

/// Returns `true` if `path` has the `.rsis` (RaggieSoft Installer Suite) file extension.
inline bool IsSuiteFilePath(const std::filesystem::path& path) {
    return EqualsIgnoreCase(path.extension().string(), ".rsis");
}

/// Parses a `.rsis` `RswManifestDocument` into a sorted `RswSuiteWorkspace`.
inline bool ParseSuiteWorkspace(
    const RswManifestDocument& doc,
    RswSuiteWorkspace& outSuite,
    std::string& outError)
{
    outSuite = {};
    outSuite.suiteFilePath            = doc.sourcePath;
    outSuite.name                     = doc.GetString("Suite", "", "Name", "Untitled Installer Suite");
    outSuite.description              = doc.GetString("Suite", "", "Description", "");
    outSuite.tagline                  = doc.GetString("Suite", "", "Tagline", outSuite.description);
    outSuite.version                  = doc.GetString("Suite", "", "Version", "1.0.0");
    outSuite.manufacturer             = doc.GetString("Suite", "", "Manufacturer", "RaggieSoft");
    outSuite.upgradeCode              = doc.GetString("Suite", "", "UpgradeCode", "");
    outSuite.scope                    = doc.GetString("Suite", "", "Scope", "machine");
    outSuite.requestedExecutionLevel  = doc.GetString("Suite", "", "RequestedExecutionLevel", "asInvoker");
    outSuite.rollbackPolicy           = AsciiLower(doc.GetString("Suite", "", "RollbackPolicy", "keep_completed"));
    outSuite.suiteDelivery            = AsciiLower(doc.GetString("Suite", "", "SuiteDelivery", "auto"));
    outSuite.outputName               = doc.GetString("Suite", "", "OutputName", "");
    outSuite.outputDir                = doc.GetString("Suite", "", "OutputDir", "out");
    outSuite.defaultSubdir            = doc.GetString("Suite", "", "DefaultSubdir", "");
    outSuite.accentPrimaryHex         = doc.GetString("Suite", "", "AccentPrimaryHex", "#2563EB");
    outSuite.accentSecondaryHex       = doc.GetString("Suite", "", "AccentSecondaryHex", "#FBBF24");
    outSuite.buildSuiteInstaller      = doc.GetBool("Suite", "", "BuildSuiteInstaller", false);
    outSuite.buildUniversal           = doc.GetBool("Suite", "", "BuildUniversal", false);
    outSuite.showComponentProgressBar = doc.GetBool("Suite", "", "ShowComponentProgressBar", true);

    // Parse pipe- or comma-separated `Architectures = arm64|x64`
    const std::string rawArchs = doc.GetString("Suite", "", "Architectures", "arm64|x64");
    std::string cur;
    for (char c : rawArchs) {
        if (c == '|' || c == ',' || c == ' ') {
            if (!cur.empty()) {
                outSuite.defaultArchitectures.push_back(AsciiLower(cur));
                cur.clear();
            }
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) {
        outSuite.defaultArchitectures.push_back(AsciiLower(cur));
    }
    if (outSuite.defaultArchitectures.empty()) {
        outSuite.defaultArchitectures.push_back("arm64");
    }

    // Collect `[Variable]` key-value pairs (`$(KEY)` passed to every member `.rsip`)
    for (const auto& ln : doc.lines) {
        if (ln.kind == AstLineKind::KeyValue &&
            EqualsIgnoreCase(ln.sectionPrefix, "Variable") &&
            ln.sectionItemId.empty()) {
            outSuite.variables[ln.key] = ln.value;
        }
    }

    const std::filesystem::path baseDir = doc.sourcePath.has_parent_path()
        ? doc.sourcePath.parent_path()
        : std::filesystem::current_path();

    int declIdx = 0;
    for (const auto& pid : doc.GetNamedSectionIds("Project")) {
        RswSuiteProjectEntry pe{};
        pe.id                = pid;
        pe.rawRelPath        = doc.GetString("Project", pid, "Path", "");
        pe.category          = doc.GetString("Project", pid, "Category", "Default");
        pe.buildOrder        = doc.GetInt("Project", pid, "BuildOrder", 100);
        pe.enabled           = doc.GetBool("Project", pid, "Enabled", true);
        pe.includeInSuite    = doc.GetBool("Project", pid, "IncludeInSuite", true);
        pe.requiredInSuite   = doc.GetBool("Project", pid, "Required", false);
        pe.selectedByDefault = doc.GetBool("Project", pid, "SelectedByDefault", true);
        pe.rollbackBoundary  = doc.GetBool("Project", pid, "RollbackBoundary", false);
        pe.forwardInstallDir = doc.GetBool("Project", pid, "ForwardInstallDir", false);
        pe.declIndex         = declIdx++;

        if (pe.rawRelPath.empty()) {
            outError = "[Project." + pid + "] in suite is missing required 'Path = <project.rsip>'.";
            return false;
        }

        std::filesystem::path p(pe.rawRelPath);
        pe.resolvedPath = (p.is_relative() ? (baseDir / p) : p).lexically_normal();
        outSuite.projects.push_back(std::move(pe));
    }

    if (outSuite.projects.empty()) {
        outError = "Suite file contains zero '[Project.<Id>]' entries.";
        return false;
    }

    // Deterministic dependency sort: lower `BuildOrder` builds first (e.g. `.msm` = 10, `.msi` = 20)!
    std::sort(outSuite.projects.begin(), outSuite.projects.end(),
              [](const RswSuiteProjectEntry& a, const RswSuiteProjectEntry& b) {
                  if (a.buildOrder != b.buildOrder) return a.buildOrder < b.buildOrder;
                  return a.declIndex < b.declIndex;
              });

    return true;
}

} // namespace rsw
