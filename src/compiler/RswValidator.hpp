// ================================================================================
// FILE: src/compiler/RswValidator.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file?
//     This header implements `instbuild.exe --validate [--diagnostics=json] <file.rsip>`,
//     our sub-millisecond dry-run structural and Windows Installer rule linter!
//
//   Why validate BEFORE `cabinet.dll` or `msi.dll` run?
//     Compressing gigabytes of files into a `.cab` archive only to discover at the
//     very end that `Version = 1.0.0.0` has 4 parts (Windows Installer's `ProductVersion`
//     strictly compares only 3 parts: `0..255 . 0..255 . 0..65535`!) or that a button
//     label is missing its `&` keyboard accelerator wastes developer time.
//
//     By checking the parsed `RswManifestDocument` AST directly against our `constexpr`
//     tables (`kSectionDescriptors`, `kFieldDescriptors`, `kExtendedFieldDescriptors`),
//     this validator catches syntax errors, unknown keys, missing files, invalid GUIDs,
//     MSI version overflows, and WCAG 2.2 AA / Section 508 accelerator omissions in
//     less than a millisecond—emitting either human-readable text or structured JSON!
// ================================================================================

#pragma once

#include "../common/RswCryptoAndPe.hpp"
#include "../common/RswEditions.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswRaggieScript.hpp"
#include "../common/RswSchema.hpp"
#include "../common/RswWcagContrast.hpp"
#include "../common/RswWin32UiTheme.hpp"

#include <filesystem>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rsw {

enum class DiagnosticSeverity {
    Error,
    Warning
};

struct RswDiagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string        code;       // e.g. `"RSW101"`
    int                lineNumber = 0;
    std::string        section;
    std::string        key;
    std::string        message;
};

struct ValidationReport {
    std::filesystem::path      manifestPath;
    std::string                resolvedArchitecture;
    RswIdeEdition              activeEdition = RswIdeEdition::Community;
    std::vector<RswDiagnostic> diagnostics;

    [[nodiscard]] std::size_t ErrorCount() const noexcept {
        std::size_t n = 0;
        for (const auto& d : diagnostics) {
            if (d.severity == DiagnosticSeverity::Error) ++n;
        }
        return n;
    }

    [[nodiscard]] std::size_t WarningCount() const noexcept {
        std::size_t n = 0;
        for (const auto& d : diagnostics) {
            if (d.severity == DiagnosticSeverity::Warning) ++n;
        }
        return n;
    }

    [[nodiscard]] bool IsOk() const noexcept {
        return ErrorCount() == 0;
    }
};

inline std::string JsonEscapeString(std::string_view s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char hex[8]{};
                    std::snprintf(hex, sizeof(hex), "\\u%04X", static_cast<unsigned>(c));
                    out += hex;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

/// Checks whether an interactive UI label contains at least one true Win32 `&` mnemonic
/// (ignoring escaped `&&` literal ampersands and trailing `&`).
inline bool HasKeyboardAcceleratorMnemonic(std::string_view label) noexcept {
    for (std::size_t i = 0; i + 1 < label.size(); ++i) {
        if (label[i] == '&') {
            if (label[i + 1] == '&') {
                ++i; // Skip escaped `&&`
                continue;
            }
            if (label[i + 1] != ' ' && label[i + 1] != '\t') {
                return true;
            }
        }
    }
    return false;
}

/// Validates Windows Installer's 3-part `Major.Minor.Build` version format (`<= 255.255.65535`).
inline bool IsValidMsiThreePartVersion(std::string_view ver) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= ver.size()) {
        auto dot = ver.find('.', start);
        if (dot == std::string_view::npos) {
            parts.emplace_back(ver.substr(start));
            break;
        }
        parts.emplace_back(ver.substr(start, dot - start));
        start = dot + 1;
    }
    if (parts.size() != 3) {
        return false;
    }
    int nums[3]{};
    for (int i = 0; i < 3; ++i) {
        if (parts[i].empty()) return false;
        for (char c : parts[i]) {
            if (c < '0' || c > '9') return false;
        }
        try {
            nums[i] = std::stoi(parts[i]);
        } catch (...) {
            return false;
        }
    }
    return (nums[0] >= 0 && nums[0] <= 255) &&
           (nums[1] >= 0 && nums[1] <= 255) &&
           (nums[2] >= 0 && nums[2] <= 65535);
}

inline const SectionDescriptor* FindSectionDescriptor(std::string_view prefix) noexcept {
    for (const auto& sd : kSectionDescriptors) {
        if (EqualsIgnoreCase(sd.nameOrPrefix, prefix)) {
            return &sd;
        }
    }
    return nullptr;
}

inline const FieldDescriptor* FindFieldDescriptor(std::string_view sectionPrefix, std::string_view key) noexcept {
    for (const auto& fd : kFieldDescriptors) {
        if (EqualsIgnoreCase(fd.sectionPrefix, sectionPrefix) && EqualsIgnoreCase(fd.key, key)) {
            return &fd;
        }
    }
    for (const auto& fd : kExtendedFieldDescriptors) {
        if (EqualsIgnoreCase(fd.sectionPrefix, sectionPrefix) && EqualsIgnoreCase(fd.key, key)) {
            return &fd;
        }
    }
    return nullptr;
}

inline bool EnumContainsToken(std::string_view pipeList, std::string_view candidate) {
    std::size_t start = 0;
    while (start <= pipeList.size()) {
        auto bar = pipeList.find('|', start);
        std::string_view item = (bar == std::string_view::npos)
            ? pipeList.substr(start)
            : pipeList.substr(start, bar - start);
        if (EqualsIgnoreCase(item, candidate)) {
            return true;
        }
        if (bar == std::string_view::npos) break;
        start = bar + 1;
    }
    return false;
}

/// Runs complete structural, schema, filesystem, edition, and Windows Installer rule validation on `doc`.
inline ValidationReport ValidateManifestDocument(
    const RswManifestDocument& doc,
    std::string_view cliArchOverride = "",
    RswIdeEdition activeEdition = RswIdeEdition::Community)
{
    ValidationReport rep;
    rep.manifestPath  = doc.sourcePath;
    rep.activeEdition = activeEdition;

    std::string rawArch = !cliArchOverride.empty()
        ? std::string(cliArchOverride)
        : doc.GetString("Package", "", "Architecture", "arm64");
    rep.resolvedArchitecture = AsciiLower(TrimAscii(rawArch));
    if (rep.resolvedArchitecture == "$(arch)") {
        rep.resolvedArchitecture = std::string(QueryHostPlatform().NativeArchSlug());
    }

    auto addErr = [&](std::string code, int line, std::string sec, std::string key, std::string msg) {
        rep.diagnostics.push_back({DiagnosticSeverity::Error, std::move(code), line, std::move(sec), std::move(key), std::move(msg)});
    };
    auto addWarn = [&](std::string code, int line, std::string sec, std::string key, std::string msg) {
        rep.diagnostics.push_back({DiagnosticSeverity::Warning, std::move(code), line, std::move(sec), std::move(key), std::move(msg)});
    };

    if (rep.resolvedArchitecture != "x64" && rep.resolvedArchitecture != "arm64") {
        addErr("RSW001", 1, "Package", "Architecture",
               "Unsupported target architecture '" + rep.resolvedArchitecture + "'. Expected 'x64' or 'arm64'.");
    }

    // 1. Line-level syntax & schema descriptor checks
    std::set<std::string> seenSingletonSections;
    for (const auto& ln : doc.lines) {
        if (ln.kind == AstLineKind::Malformed) {
            addErr("RSW002", ln.lineNumber, ln.sectionPrefix, "", ln.parseError);
            continue;
        }
        if (ln.kind == AstLineKind::SectionHeader) {
            const auto* sd = FindSectionDescriptor(ln.sectionPrefix);
            const std::string fullSec = ln.sectionItemId.empty()
                ? ln.sectionPrefix
                : (ln.sectionPrefix + "." + ln.sectionItemId);
            if (sd == nullptr) {
                addErr("RSW003", ln.lineNumber, fullSec, "",
                       "Unknown section '[" + fullSec + "]'. Run --emit-ai-spec to view all supported sections.");
                continue;
            }
            if (sd->cardinality == SectionCardinality::NamedRepeatable && ln.sectionItemId.empty()) {
                addErr("RSW004", ln.lineNumber, fullSec, "",
                       "Section '[" + ln.sectionPrefix + "]' requires an item identifier, e.g. '[" + ln.sectionPrefix + ".Main]'.");
            } else if (sd->cardinality != SectionCardinality::NamedRepeatable && !ln.sectionItemId.empty()) {
                addErr("RSW005", ln.lineNumber, fullSec, "",
                       "Singleton section '[" + ln.sectionPrefix + "]' must not include a '.<id>' suffix.");
            } else if (sd->cardinality != SectionCardinality::NamedRepeatable) {
                std::string low = AsciiLower(ln.sectionPrefix);
                if (!seenSingletonSections.insert(low).second) {
                    addWarn("RSW006", ln.lineNumber, fullSec, "",
                            "Duplicate singleton section '[" + ln.sectionPrefix + "]'.");
                }
            }
        } else if (ln.kind == AstLineKind::KeyValue) {
            const std::string fullSec = ln.sectionItemId.empty()
                ? ln.sectionPrefix
                : (ln.sectionPrefix + "." + ln.sectionItemId);
            if (FindSectionDescriptor(ln.sectionPrefix) == nullptr) {
                continue; // Already reported on the section header
            }
            const auto* fd = FindFieldDescriptor(ln.sectionPrefix, ln.key);
            if (fd == nullptr) {
                addErr("RSW007", ln.lineNumber, fullSec, ln.key,
                       "Unknown key '" + ln.key + "' in section '[" + fullSec + "]'.");
                continue;
            }

            const std::string expandedVal = ExpandArchToken(ln.value, rep.resolvedArchitecture);
            switch (fd->kind) {
                case FieldValueKind::MsiVersion:
                    if (!IsValidMsiThreePartVersion(expandedVal)) {
                        addErr("RSW008", ln.lineNumber, fullSec, ln.key,
                               "Invalid MSI Version '" + expandedVal + "'. Must be 3-part 'Major.Minor.Build' (max 255.255.65535).");
                    }
                    break;
                case FieldValueKind::Guid: {
                    std::string norm;
                    if (!NormalizeMsiGuid(expandedVal, norm)) {
                        addErr("RSW009", ln.lineNumber, fullSec, ln.key,
                               "Invalid GUID '" + expandedVal + "'. Expected '{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}'.");
                    }
                    break;
                }
                case FieldValueKind::GuidOrAuto: {
                    std::string norm;
                    if (expandedVal != "*" && !NormalizeMsiGuid(expandedVal, norm)) {
                        addErr("RSW010", ln.lineNumber, fullSec, ln.key,
                               "Invalid ProductCode '" + expandedVal + "'. Expected '*' or '{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}'.");
                    }
                    break;
                }
                case FieldValueKind::Integer: {
                    bool okInt = !expandedVal.empty();
                    for (std::size_t i = 0; i < expandedVal.size(); ++i) {
                        char c = expandedVal[i];
                        if (i == 0 && c == '-') continue;
                        if (c < '0' || c > '9') okInt = false;
                    }
                    if (!okInt) {
                        addErr("RSW011", ln.lineNumber, fullSec, ln.key,
                               "Expected integer value for '" + ln.key + "', got '" + expandedVal + "'.");
                    }
                    break;
                }
                case FieldValueKind::Boolean: {
                    std::string b = AsciiLower(expandedVal);
                    if (b != "true" && b != "false" && b != "yes" && b != "no" && b != "1" && b != "0") {
                        addErr("RSW012", ln.lineNumber, fullSec, ln.key,
                               "Expected boolean ('true' or 'false') for '" + ln.key + "', got '" + expandedVal + "'.");
                    }
                    break;
                }
                case FieldValueKind::EnumChoice:
                    if (!EnumContainsToken(fd->allowedValues, expandedVal)) {
                        addErr("RSW013", ln.lineNumber, fullSec, ln.key,
                               "Invalid value '" + expandedVal + "' for '" + ln.key +
                               "'. Allowed values: " + std::string(fd->allowedValues) + ".");
                    }
                    break;
                case FieldValueKind::AcceleratorString:
                    if (!expandedVal.empty() && !HasKeyboardAcceleratorMnemonic(expandedVal)) {
                        addErr("RSW014", ln.lineNumber, fullSec, ln.key,
                               "Accessibility rule: UI label '" + expandedVal +
                               "' must include a '&' keyboard accelerator mnemonic (e.g. '&Install').");
                    }
                    break;
                case FieldValueKind::Path:
                case FieldValueKind::RelativePath:
                case FieldValueKind::String:
                    break;
            }
        }
    }

    // 2. Required sections & required fields check (aware of `PackageKind = sfx` and `PackageKind = suite`!)
    const std::string pkgKindLow = AsciiLower(TrimAscii(doc.GetString("Package", "", "PackageKind", "msi")));
    const bool isSfxPkg   = (pkgKindLow == "sfx");
    const bool isAddonPkg = (pkgKindLow == "addon");
    const bool isSuitePkg = (pkgKindLow == "suite");

    for (const auto& sd : kSectionDescriptors) {
        if (sd.cardinality == SectionCardinality::SingletonRequired) {
            if ((isSfxPkg || isAddonPkg || isSuitePkg) && EqualsIgnoreCase(sd.nameOrPrefix, "Install")) {
                continue; // `[Install]` is optional for SFX archives, Host-Aware Add-Ons (`[HostApp]`), and Suite chainers!
            }
            if (seenSingletonSections.find(AsciiLower(sd.nameOrPrefix)) == seenSingletonSections.end()) {
                addErr("RSW015", 1, std::string(sd.nameOrPrefix), "",
                       "Missing required section '[" + std::string(sd.nameOrPrefix) + "]'.");
            } else {
                for (const auto& fd : kFieldDescriptors) {
                    if (fd.required && EqualsIgnoreCase(fd.sectionPrefix, sd.nameOrPrefix)) {
                        if ((isSfxPkg || isAddonPkg) && EqualsIgnoreCase(sd.nameOrPrefix, "Package") && EqualsIgnoreCase(fd.key, "UpgradeCode")) {
                            continue; // SFX archives and Add-Ons auto-derive deterministic UUIDv5 UpgradeCodes when omitted!
                        }
                        if (doc.GetString(sd.nameOrPrefix, "", fd.key).empty()) {
                            addErr("RSW016", 1, std::string(sd.nameOrPrefix), std::string(fd.key),
                                   "Missing required key '" + std::string(fd.key) + "' in '[" + std::string(sd.nameOrPrefix) + "]'.");
                        }
                    }
                }
            }
        } else if (sd.cardinality == SectionCardinality::NamedRepeatable) {
            auto itemIds = doc.GetNamedSectionIds(sd.nameOrPrefix);
            if (sd.atLeastOneRequired && itemIds.empty()) {
                if (isSuitePkg && EqualsIgnoreCase(sd.nameOrPrefix, "Payload")) {
                    if (doc.GetNamedSectionIds("SuitePackage").empty()) {
                        addErr("RSW017", 1, "SuitePackage", "",
                               "PackageKind = suite must define at least one '[SuitePackage.<id>]' section.");
                    }
                    continue;
                }
                addErr("RSW017", 1, std::string(sd.nameOrPrefix), "",
                       "Manifest must define at least one '[" + std::string(sd.nameOrPrefix) + ".<id>]' section.");
            }
            for (const auto& id : itemIds) {
                const std::string fullSec = std::string(sd.nameOrPrefix) + "." + id;
                auto checkReq = [&](const FieldDescriptor& fd) {
                    if (fd.required && EqualsIgnoreCase(fd.sectionPrefix, sd.nameOrPrefix)) {
                        if (doc.GetString(sd.nameOrPrefix, id, fd.key).empty()) {
                            addErr("RSW018", 1, fullSec, std::string(fd.key),
                                   "Missing required key '" + std::string(fd.key) + "' in '[" + fullSec + "]'.");
                        }
                    }
                };
                for (const auto& fd : kFieldDescriptors) checkReq(fd);
                for (const auto& fd : kExtendedFieldDescriptors) checkReq(fd);
            }
        }
    }

    // 3. Filesystem existence checks on `[Payload.*] Source` and `[UI] {LicenseRtf, IconFile, ArtworkPng}`
    const std::filesystem::path baseDir = doc.sourcePath.empty()
        ? std::filesystem::current_path()
        : doc.sourcePath.parent_path();

    auto resolvePathStd = [&](std::string_view raw) -> std::filesystem::path {
        std::string exp = ExpandArchToken(raw, rep.resolvedArchitecture);
        std::filesystem::path p(Utf8ToWide(exp));
        return (p.is_absolute() ? p : (baseDir / p)).lexically_normal();
    };

    std::error_code ec;
    for (const auto& pid : doc.GetNamedSectionIds("Payload")) {
        std::string rawSrc = doc.GetString("Payload", pid, "Source");
        if (!rawSrc.empty()) {
            auto absSrc = resolvePathStd(rawSrc);
            if (!std::filesystem::exists(absSrc, ec)) {
                addErr("RSW019", 1, "Payload." + pid, "Source",
                       "Payload source path does not exist on disk: " + WideToUtf8(absSrc.wstring()));
            }
        }
    }

    for (const char* uiPathKey : {"LicenseRtf", "IconFile", "ArtworkPng"}) {
        std::string rawUiPath = doc.GetString("UI", "", uiPathKey);
        if (!rawUiPath.empty()) {
            auto absUi = resolvePathStd(rawUiPath);
            if (!std::filesystem::exists(absUi, ec)) {
                addErr("RSW020", 1, "UI", uiPathKey,
                       std::string(uiPathKey) + " file does not exist on disk: " + WideToUtf8(absUi.wstring()));
            }
        }
    }

    // 4. Microsoft Official Windows Installer Component Rules (Single-Directory, ICE18, ICE38, & FileAssociation checks!)
    auto normalizeRelDir = [](std::string s) -> std::string {
        for (char& c : s) if (c == '\\') c = '/';
        while (!s.empty() && s.front() == '/') s.erase(s.begin());
        while (!s.empty() && s.back() == '/') s.pop_back();
        return AsciiLower(s);
    };

    std::set<std::string> definedCompIdsLower;
    for (const auto& cid : doc.GetNamedSectionIds("Component")) {
        definedCompIdsLower.insert(AsciiLower(cid));
        const std::string tRoot  = doc.GetString("Component", cid, "TargetRoot", "INSTALLDIR");
        const std::string kpKind = AsciiLower(doc.GetString("Component", cid, "KeyPathKind", "auto"));
        const std::string kpTarg = doc.GetString("Component", cid, "KeyPathTarget");

        // Microsoft ICE38 Rule: A Component installing into a User Profile folder (`AppDataFolder`,
        // `DesktopFolder`, `ProgramMenuFolder`, etc.) MUST use an `HKCU` Registry value as its KeyPath!
        if (IsPerUserProfileRootDir(tRoot) && (kpKind == "file" || kpKind == "folder")) {
            addErr("RSW032", 1, "Component." + cid, "KeyPathKind",
                   "Microsoft ICE38 Component Rule: Component '" + cid + "' installs to per-user folder '" +
                   tRoot + "' and therefore MUST use KeyPathKind = 'registry' (under HKCU) or 'auto'.");
        }
        if ((kpKind == "file" || kpKind == "registry") && kpTarg.empty()) {
            addErr("RSW033", 1, "Component." + cid, "KeyPathTarget",
                   "Component '" + cid + "' specifies KeyPathKind = '" + kpKind + "' but KeyPathTarget is empty.");
        }
        if (kpKind == "registry" && !kpTarg.empty()) {
            bool regFound = false;
            for (const auto& rid : doc.GetNamedSectionIds("Registry")) {
                if (EqualsIgnoreCase(rid, kpTarg)) {
                    regFound = true;
                    const std::string rRoot = doc.GetString("Registry", rid, "Root", "HKLM");
                    if (IsPerUserProfileRootDir(tRoot) && !EqualsIgnoreCase(rRoot, "HKCU")) {
                        addErr("RSW032", 1, "Registry." + rid, "Root",
                               "Microsoft ICE38 Component Rule: Registry KeyPath '" + rid + "' for per-user Component '" +
                               cid + "' must set Root = HKCU.");
                    }
                    break;
                }
            }
            if (!regFound) {
                addErr("RSW033", 1, "Component." + cid, "KeyPathTarget",
                       "Component '" + cid + "' KeyPathTarget '" + kpTarg + "' does not match any [Registry.<id>] section.");
            }
        }
    }

    // Microsoft Single-Directory Component Rule: Every payload bound to `[Component.<id>]`
    // MUST install to that Component's exact single `(TargetRoot, TargetSubdir)` directory
    // (and cannot recursively span subdirectories under one single Component!).
    for (const auto& pid : doc.GetNamedSectionIds("Payload")) {
        const std::string refComp = TrimAscii(doc.GetString("Payload", pid, "Component"));
        if (refComp.empty()) continue;
        if (definedCompIdsLower.find(AsciiLower(refComp)) == definedCompIdsLower.end()) {
            addErr("RSW030", 1, "Payload." + pid, "Component",
                   "Payload '" + pid + "' references undefined [Component." + refComp + "].");
            continue;
        }
        const std::string cRoot = AsciiLower(doc.GetString("Component", refComp, "TargetRoot", "INSTALLDIR"));
        const std::string cSub  = normalizeRelDir(ExpandArchToken(doc.GetString("Component", refComp, "TargetSubdir"), rep.resolvedArchitecture));
        const std::string pRoot = AsciiLower(doc.GetString("Payload", pid, "TargetRoot", doc.GetString("Component", refComp, "TargetRoot", "INSTALLDIR")));
        const std::string pSub  = normalizeRelDir(ExpandArchToken(doc.GetString("Payload", pid, "TargetSubdir", doc.GetString("Component", refComp, "TargetSubdir")), rep.resolvedArchitecture));
        if (pRoot != cRoot || pSub != cSub) {
            addErr("RSW031", 1, "Payload." + pid, "TargetSubdir",
                   "Microsoft Single-Directory Component Rule: Payload '" + pid + "' target folder does not match [Component." +
                   refComp + "]. A single Windows Installer Component cannot span multiple directories.");
        }
        const std::string rawSrc = doc.GetString("Payload", pid, "Source");
        if (!rawSrc.empty()) {
            const auto absSrc = resolvePathStd(rawSrc);
            if (std::filesystem::is_directory(absSrc, ec) && doc.GetBool("Payload", pid, "Recursive", true)) {
                for (const auto& de : std::filesystem::directory_iterator(absSrc, ec)) {
                    if (de.is_directory(ec)) {
                        addErr("RSW031", 1, "Payload." + pid, "Recursive",
                               "Microsoft Single-Directory Component Rule: Directory Payload '" + pid +
                               "' contains subdirectories and cannot be collapsed into a single explicit [Component." +
                               refComp + "]. Leave 'Component' blank for automatic per-file UUIDv5 components or set Recursive = false.");
                        break;
                    }
                }
            }
        }
    }

    // Validate `[FileAssociation.<id>]` extensions & ProgIds
    for (const auto& aid : doc.GetNamedSectionIds("FileAssociation")) {
        std::string ext = TrimAscii(doc.GetString("FileAssociation", aid, "Extension"));
        if (!ext.empty() && ext.front() == '.') ext.erase(ext.begin());
        if (ext.empty() || ext.find_first_of("\\/ :*?\"<>|") != std::string::npos) {
            addErr("RSW034", 1, "FileAssociation." + aid, "Extension",
                   "Invalid file extension in [FileAssociation." + aid + "]. Expected e.g. '.rsip' or 'rsip'.");
        }
    }

    // Validate `[Prerequisite.<id>]` runtime delivery rules (`embedded` requires `SourceFile`; `download` requires `Url` or `Preset`)
    for (const auto& pid : doc.GetNamedSectionIds("Prerequisite")) {
        const std::string preset = doc.GetString("Prerequisite", pid, "Preset", "Custom");
        const std::string dist   = AsciiLower(doc.GetString("Prerequisite", pid, "Distribution", "download"));
        const std::string rawSrc = doc.GetString("Prerequisite", pid, "SourceFile");
        const std::string urlAny = doc.GetString("Prerequisite", pid, "Url") +
                                   doc.GetString("Prerequisite", pid, "UrlX64") +
                                   doc.GetString("Prerequisite", pid, "UrlArm64");
        if (dist == "embedded") {
            if (rawSrc.empty()) {
                addErr("RSW035", 1, "Prerequisite." + pid, "SourceFile",
                       "[Prerequisite." + pid + "] uses Distribution = embedded, so SourceFile must point to a local .exe or .msi runtime.");
            } else {
                const auto absSrc = resolvePathStd(rawSrc);
                if (!std::filesystem::exists(absSrc, ec)) {
                    addErr("RSW035", 1, "Prerequisite." + pid, "SourceFile",
                           "Prerequisite SourceFile does not exist on disk: " + WideToUtf8(absSrc.wstring()));
                }
            }
        } else if (dist == "download" && EqualsIgnoreCase(preset, "Custom") && urlAny.empty()) {
            addErr("RSW036", 1, "Prerequisite." + pid, "Url",
                   "[Prerequisite." + pid + "] uses Preset = Custom and Distribution = download, so Url (or UrlX64/UrlArm64) is required.");
        }
    }

    // Validate `[MergeModule.<id>]` `.msm` paths on disk
    for (const auto& mid : doc.GetNamedSectionIds("MergeModule")) {
        const std::string rawMsm = doc.GetString("MergeModule", mid, "Source");
        if (!rawMsm.empty()) {
            const auto absMsm = resolvePathStd(rawMsm);
            if (!std::filesystem::exists(absMsm, ec)) {
                addErr("RSW037", 1, "MergeModule." + mid, "Source",
                       "Merge Module (.msm) file does not exist on disk: " + WideToUtf8(absMsm.wstring()));
            }
        }
    }

    // Validate `[HostApp]` rules when `PackageKind = addon` (`RSW067` / `RSW068`)
    if (isAddonPkg) {
        const std::string hName = TrimAscii(doc.GetString("HostApp", "", "Name"));
        const std::string hUpg  = TrimAscii(doc.GetString("HostApp", "", "UpgradeCode"));
        const std::string hReg  = TrimAscii(doc.GetString("HostApp", "", "RegistryKey"));
        const std::string hSub  = TrimAscii(doc.GetString("HostApp", "", "FallbackSubdir"));
        if (hName.empty()) {
            addErr("RSW067", 1, "HostApp", "Name",
                   "PackageKind = addon requires a '[HostApp]' section with a non-empty 'Name = <Host Application Name>'.");
        }
        if (hUpg.empty() && hReg.empty() && hSub.empty()) {
            addErr("RSW068", 1, "HostApp", "Discovery",
                   "[HostApp] must specify at least one discovery rule: 'UpgradeCode', 'RegistryKey', or 'FallbackSubdir'.");
        }
    }

    // Validate `[SuitePackage.<id>]` child `.msi` / `-setup.exe` / Web Workload rules & `[Media]` cabinet bounds
    for (const auto& spId : doc.GetNamedSectionIds("SuitePackage")) {
        const std::string deliv = AsciiLower(TrimAscii(doc.GetString("SuitePackage", spId, "Delivery", "embedded")));
        const std::string rawSp = TrimAscii(doc.GetString("SuitePackage", spId, "Source"));
        const std::string dlUrl = TrimAscii(ExpandArchToken(doc.GetString("SuitePackage", spId, "DownloadUrl"), rep.resolvedArchitecture));
        const std::string shaSpecific = EqualsIgnoreCase(rep.resolvedArchitecture, "x64")
            ? TrimAscii(doc.GetString("SuitePackage", spId, "Sha256X64"))
            : TrimAscii(doc.GetString("SuitePackage", spId, "Sha256Arm64"));
        const std::string shaEff = !shaSpecific.empty() ? shaSpecific : TrimAscii(doc.GetString("SuitePackage", spId, "Sha256"));

        if (deliv == "web") {
            if (dlUrl.empty()) {
                addErr("RSW038", 1, "SuitePackage." + spId, "DownloadUrl",
                       "[SuitePackage." + spId + "] uses Delivery = web, so DownloadUrl is required.");
            }
            if (shaEff.size() != 64 && rawSp.empty()) {
                addErr("RSW038", 1, "SuitePackage." + spId, "Sha256",
                       "[SuitePackage." + spId + "] uses Delivery = web without a local Source file, so a 64-character hex Sha256 (or Sha256X64/Sha256Arm64) is required.");
            }
        } else {
            if (rawSp.empty()) {
                addErr("RSW038", 1, "SuitePackage." + spId, "Source",
                       "[SuitePackage." + spId + "] uses Delivery = " + deliv + ", so Source is required.");
            } else {
                const auto absSp = resolvePathStd(rawSp);
                if (!std::filesystem::exists(absSp, ec)) {
                    addErr("RSW038", 1, "SuitePackage." + spId, "Source",
                           "SuitePackage Source file does not exist on disk: " + WideToUtf8(absSp.wstring()));
                }
            }
        }
    }
    const int maxCabMb = doc.GetInt("Media", "", "MaxCabinetSizeMB", 0);
    const int maxCabKb = doc.GetInt("Media", "", "MaxCabinetSizeKB", 0);
    if (maxCabMb < 0 || maxCabMb > 2047) {
        addErr("RSW039", 1, "Media", "MaxCabinetSizeMB",
               "MaxCabinetSizeMB must be between 0 (single cabinet) and 2047 MB.");
    }
    if (maxCabKb < 0 || (maxCabKb > 0 && maxCabKb < 16)) {
        addErr("RSW039", 1, "Media", "MaxCabinetSizeKB",
               "MaxCabinetSizeKB must be 0 or >= 16 KB.");
    }

    // Validate `[SFX]` Software Installation `%TEMP%` Auto-Cleanup & Companion Process Wait rules
    const bool sfxCleanupTemp = doc.GetBool("SFX", "", "CleanupTempAfterRun", false);
    const bool sfxExtractTemp = doc.GetBool("SFX", "", "ExtractToTemp", false);
    const std::string sfxRun  = TrimAscii(doc.GetString("SFX", "", "PostExtractRun"));
    const std::string sfxWaitMode = AsciiLower(TrimAscii(doc.GetString("SFX", "", "WaitMode", "auto")));
    const std::string sfxWaitProc = TrimAscii(doc.GetString("SFX", "", "WaitForProcess"));
    const int sfxGraceMs = doc.GetInt("SFX", "", "WaitSpawnGraceMs", 1500);

    if (sfxCleanupTemp && !sfxExtractTemp) {
        addErr("RSW061", 1, "SFX", "CleanupTempAfterRun",
               "CleanupTempAfterRun = true requires ExtractToTemp = true so only an isolated %TEMP% staging directory is ever auto-deleted.");
    }
    if (sfxCleanupTemp && sfxRun.empty()) {
        addErr("RSW062", 1, "SFX", "CleanupTempAfterRun",
               "CleanupTempAfterRun = true requires a non-empty [SFX] PostExtractRun installer/executable to run before cleaning %TEMP%.");
    }
    if (sfxCleanupTemp && sfxWaitMode == "none") {
        addErr("RSW063", 1, "SFX", "WaitMode",
               "CleanupTempAfterRun = true cannot use WaitMode = none (temporary files would be deleted before PostExtractRun starts). Use 'auto', 'job_tree', 'process_name', or 'direct'.");
    }
    if (sfxWaitMode == "process_name" && sfxWaitProc.empty()) {
        addErr("RSW064", 1, "SFX", "WaitForProcess",
               "WaitMode = process_name requires [SFX] WaitForProcess (e.g. '_ISDEL.EXE; _INS*._MP').");
    }
    if (sfxGraceMs < 0 || sfxGraceMs > 60000) {
        addErr("RSW065", 1, "SFX", "WaitSpawnGraceMs",
               "WaitSpawnGraceMs must be between 0 and 60000 milliseconds.");
    }

    // 4B. Validate `[Dialog.*]`, `[Control.*]`, `[MsiControl.*]`, `[Environment.*]`, & `[IniFile.*]` (`v0.7.0`)!
    std::set<std::string> knownCppDialogsLower;
    for (const auto& did : doc.GetNamedSectionIds("Dialog")) {
        knownCppDialogsLower.insert(AsciiLower(did));
    }
    for (const auto& cid : doc.GetNamedSectionIds("Control")) {
        const std::string refDlg = TrimAscii(doc.GetString("Control", cid, "Dialog"));
        if (refDlg.empty() || knownCppDialogsLower.count(AsciiLower(refDlg)) == 0) {
            addErr("RSW066", 1, "Control." + cid, "Dialog",
                   "[Control." + cid + "] Dialog = '" + refDlg + "' must reference a declared [Dialog.<id>] section.");
        }
        const std::string tp  = TrimAscii(doc.GetString("Control", cid, "Type", "Text"));
        const std::string txt = TrimAscii(doc.GetString("Control", cid, "Text"));
        if (EqualsIgnoreCase(tp, "CheckBox") && !txt.empty() && txt.find('&') == std::string::npos) {
            addWarn("RSW067", 1, "Control." + cid, "Text",
                    "[Control." + cid + "] CheckBox label '" + txt + "' should include an '&' keyboard mnemonic (e.g. '&Enable Service') for WCAG 2.2 AA accessibility.");
        }
    }

    std::set<std::string> knownMsiDialogsLower{
        "welcomedlg", "licenseagreementdlg", "setuptypedlg", "customizedlg",
        "installdirdlg", "shortcutsdlg", "repairmodedlg", "verifyreadydlg",
        "exitdialog", "maintenancetypedlg", "verifyremovedlg"
    };
    for (const auto& mdid : doc.GetNamedSectionIds("MsiDialog")) {
        knownMsiDialogsLower.insert(AsciiLower(mdid));
    }
    for (const auto& mcid : doc.GetNamedSectionIds("MsiControl")) {
        const std::string refDlg = TrimAscii(doc.GetString("MsiControl", mcid, "Dialog"));
        if (refDlg.empty() || knownMsiDialogsLower.count(AsciiLower(refDlg)) == 0) {
            addErr("RSW066", 1, "MsiControl." + mcid, "Dialog",
                   "[MsiControl." + mcid + "] Dialog = '" + refDlg + "' must reference a built-in MSI dialog or a declared [MsiDialog.<id>] section.");
        }
        const std::string tp  = TrimAscii(doc.GetString("MsiControl", mcid, "Type", "Text"));
        const std::string txt = TrimAscii(doc.GetString("MsiControl", mcid, "Text"));
        if (EqualsIgnoreCase(tp, "CheckBox") && !txt.empty() && txt.find('&') == std::string::npos) {
            addWarn("RSW067", 1, "MsiControl." + mcid, "Text",
                    "[MsiControl." + mcid + "] CheckBox label '" + txt + "' should include an '&' keyboard mnemonic for WCAG 2.2 AA keyboard access.");
        }
    }

    for (const auto& eid : doc.GetNamedSectionIds("Environment")) {
        const std::string nm = TrimAscii(doc.GetString("Environment", eid, "Name"));
        if (nm.empty()) {
            addErr("RSW068", 1, "Environment." + eid, "Name",
                   "[Environment." + eid + "] requires a non-empty Name (e.g. 'PATH' or 'MYAPP_HOME').");
        }
    }
    for (const auto& iid : doc.GetNamedSectionIds("IniFile")) {
        if (TrimAscii(doc.GetString("IniFile", iid, "FileName")).empty() ||
            TrimAscii(doc.GetString("IniFile", iid, "Section")).empty() ||
            TrimAscii(doc.GetString("IniFile", iid, "Key")).empty()) {
            addErr("RSW069", 1, "IniFile." + iid, "FileName",
                   "[IniFile." + iid + "] requires non-empty FileName, Section, and Key fields.");
        }
    }

    // 4C. Validate `[Upgrade]` servicing strategy & optional `BaselineMsi` (`v0.8.0`)!
    const std::string upgStrategy = AsciiLower(TrimAscii(doc.GetString("Upgrade", "", "Strategy", "major")));
    const std::string rawBaseMsi  = TrimAscii(doc.GetString("Upgrade", "", "BaselineMsi"));
    if (!rawBaseMsi.empty()) {
        const auto absBaseMsi = resolvePathStd(rawBaseMsi);
        if (!std::filesystem::exists(absBaseMsi, ec)) {
            addErr("RSW087", 1, "Upgrade", "BaselineMsi",
                   "[Upgrade] BaselineMsi does not exist on disk: " + WideToUtf8(absBaseMsi.wstring()));
        }
    }
    if ((upgStrategy == "minor" || upgStrategy == "small_hotfix") &&
        rawBaseMsi.empty() &&
        TrimAscii(doc.GetString("Package", "", "ProductCode", "*")) == "*") {
        addErr("RSW088", 1, "Upgrade", "BaselineMsi",
               "[Upgrade] Strategy = " + upgStrategy + " requires either an explicit [Package] ProductCode = {GUID} or [Upgrade] BaselineMsi = <old.msi> so the baseline ProductCode is preserved.");
    }

    // 4D. Validate `Engine = raggiescript-msi`, `[RaggieScript]`, & `[CustomAction.*]` (`v0.9.1`)!
    const std::string pkgEngineLow = AsciiLower(TrimAscii(doc.GetString("Package", "", "Engine", "msi")));
    std::string rawRsscScript = TrimAscii(doc.GetString("Package", "", "Script"));
    if (rawRsscScript.empty()) {
        rawRsscScript = TrimAscii(doc.GetString("RaggieScript", "", "Source"));
    }
    const auto customActionIds = doc.GetNamedSectionIds("CustomAction");
    if (pkgEngineLow == "raggiescript-msi" && rawRsscScript.empty()) {
        addErr("RSW071", 1, "Package", "Script",
               "Engine = raggiescript-msi requires [Package] Script = <file.rssc> (or [RaggieScript] Source = <file.rssc>).");
    }
    if (!customActionIds.empty() && rawRsscScript.empty()) {
        addErr("RSW071", 1, "CustomAction." + customActionIds.front(), "Function",
               "[CustomAction.*] sections require a RaggieScript source file via [Package] Script = <file.rssc> (or [RaggieScript] Source = <file.rssc>).");
    }
    if (!rawRsscScript.empty()) {
        const auto absScript = resolvePathStd(rawRsscScript);
        if (!std::filesystem::exists(absScript, ec)) {
            addErr("RSW072", 1, "Package", "Script",
                   "RaggieScript file does not exist on disk: " + WideToUtf8(absScript.wstring()));
        } else {
            rssc::ScriptProgram rsProg{};
            std::vector<rssc::ScriptDiagnostic> rsDiags;
            const std::map<std::string, std::string> rsDefs{{"ARCH", rep.resolvedArchitecture}};
            rssc::CompileRaggieScriptFile(absScript, rsProg, rsDiags, rsDefs);
            for (const auto& rd : rsDiags) {
                if (rd.isError) addErr(rd.code, rd.line, "RaggieScript", "Script", rd.message);
                else            addWarn(rd.code, rd.line, "RaggieScript", "Script", rd.message);
            }
            std::set<std::string> declaredFns;
            for (const auto& fn : rsProg.functions) declaredFns.insert(fn.name);
            for (const auto& caId : customActionIds) {
                const std::string fnName = TrimAscii(doc.GetString("CustomAction", caId, "Function"));
                const std::string rbName = TrimAscii(doc.GetString("CustomAction", caId, "RollbackFunction"));
                if (fnName.empty() || declaredFns.count(fnName) == 0) {
                    addErr("RSW073", 1, "CustomAction." + caId, "Function",
                           "[CustomAction." + caId + "] Function = '" + fnName + "' was not found as a top-level 'fn " + fnName + "(...)' in " + WideToUtf8(absScript.filename().wstring()) + ".");
                }
                if (!rbName.empty() && declaredFns.count(rbName) == 0) {
                    addErr("RSW073", 1, "CustomAction." + caId, "RollbackFunction",
                           "[CustomAction." + caId + "] RollbackFunction = '" + rbName + "' was not found as a top-level 'fn " + rbName + "(...)' in " + WideToUtf8(absScript.filename().wstring()) + ".");
                }
            }
        }
    }

    // 5. WCAG 2.2 AA Brand Accent Contrast Verification (`[UI] AccentPrimaryHex`)
    const std::string rawPrimaryHex = TrimAscii(doc.GetString("UI", "", "AccentPrimaryHex"));
    if (!rawPrimaryHex.empty()) {
        const COLORREF fallback = RGB(37, 99, 235);
        const COLORREF parsed   = ui::ParseHexColor(rawPrimaryHex, fallback);
        double vsWhite = 0.0, vsDarkCard = 0.0;
        if (!wcag::MeetsDualThemeAccentContrast(parsed, vsWhite, vsDarkCard)) {
            const COLORREF suggested = wcag::SnapAccentToAccessibleBand(parsed);
            char ratioBuf[160]{};
            std::snprintf(
                ratioBuf, sizeof(ratioBuf),
                "AccentPrimaryHex '%s' measures %.2f:1 against #FFFFFF button text (min 4.5:1 AA) and %.2f:1 against #15203A Dark Mode cards (min 3.0:1 AA). Nearest hue-preserving accessible shade: %s.",
                rawPrimaryHex.c_str(), vsWhite, vsDarkCard, wcag::FormatHexColor(suggested).c_str());
            addWarn("RSW055", 1, "UI", "AccentPrimaryHex", ratioBuf);
        }
    }

    // 6. Official Four-Edition Capability Enforcement (`Community == 100% Architect Parity`!)
    //    Note: WCAG 2.2 AA accessibility, Authenticode/Azure Signing, `.MSM` consumption,
    //    `PackageKind = sfx` Self-Extractors, Universal `Setup.exe`, and Major Upgrades are
    //    100% unlocked in EVERY edition (never paywalled!).
    if (!EditionHasFullArchitectParity(activeEdition)) {
        const std::string pkgKind = AsciiLower(TrimAscii(doc.GetString("Package", "", "PackageKind", "msi")));
        const std::string engine  = AsciiLower(TrimAscii(doc.GetString("Package", "", "Engine", "msi")));
        const std::string edLabel = WideToUtf8(EditionDisplayName(activeEdition));

        if (pkgKind == "msm" && !EditionSupportsCompilingMergeModules(activeEdition)) {
            addErr("RSW091", 1, "Package", "PackageKind",
                   edLabel + " can consume existing [MergeModule.*] (.msm) files, while compiling new .MSM Merge Modules requires Developer, Architect, or Community Edition.");
        }
        if (pkgKind != "sfx" && pkgKind != "addon" &&
            (engine == "raggiescript" || engine == "raggiescript-msi" || engine == "native" ||
             !rawRsscScript.empty() || !customActionIds.empty()) &&
            !EditionSupportsNativeEngineAndScripting(activeEdition)) {
            addErr("RSW092", 1, "Package", "Engine",
                   "RaggieScript MSI (Engine = raggiescript-msi), RaggieScript EXE (Engine = raggiescript), and RaggieScript (.rssc) Custom Actions require Developer, Architect, or Community Edition (currently running " + edLabel + ").");
        }
        if ((upgStrategy == "minor" || upgStrategy == "small_hotfix") &&
            !EditionSupportsMspPatchesAndMstTransforms(activeEdition)) {
            addErr("RSW097", 1, "Upgrade", "Strategy",
                   "[Upgrade] Strategy = " + upgStrategy + " (.MSP Minor Upgrades & Small Update hotfixes) requires Architect or Community Edition (currently running " + edLabel + "; Essentials and Developer service updates via Major Upgrades).");
        }
        if (!EditionSupportsCustomDialogAndSetupTypeDesign(activeEdition)) {
            if (!doc.GetNamedSectionIds("MsiDialog").empty() ||
                !doc.GetNamedSectionIds("MsiControl").empty() ||
                !doc.GetNamedSectionIds("Dialog").empty() ||
                !doc.GetNamedSectionIds("Control").empty() ||
                !doc.GetNamedSectionIds("SetupType").empty()) {
                addErr("RSW093", 1, "UI", "CustomDesign",
                       "Custom [Dialog.*], [MsiDialog.*], and [SetupType.*] definitions require Developer, Architect, or Community Edition (currently running " + edLabel + "; built-in Express/Custom and Typical/Compact/Complete presets remain available).");
            }
        }
        if ((pkgKind == "suite" || !doc.GetNamedSectionIds("SuitePackage").empty()) &&
            !EditionSupportsUnifiedSuites(activeEdition)) {
            addErr("RSW094", 1, "Package", "PackageKind",
                   "Unified Multi-Product Suites (PackageKind = suite / .rsis) require Architect or Community Edition (currently running " + edLabel + ").");
        }
        if (!EditionSupportsEnterpriseMediaAndPaks(activeEdition)) {
            const std::string mediaComp = AsciiLower(TrimAscii(doc.GetString("Media", "", "Compression", "cab")));
            bool hasExternalPak = false;
            for (const auto& fid : doc.GetNamedSectionIds("Feature")) {
                if (EqualsIgnoreCase(doc.GetString("Feature", fid, "Delivery", "embedded"), "download")) {
                    hasExternalPak = true;
                    break;
                }
            }
            if (maxCabMb > 0 || maxCabKb > 0 || mediaComp == "none" || hasExternalPak) {
                addErr("RSW096", 1, "Media", "EnterpriseMedia",
                       "Multi-volume disk spanning, uncompressed loose source media (msiexec /a), and on-demand .rswpak CDN feature packs require Architect or Community Edition (currently running " + edLabel + ").");
            }
        }
    }

    return rep;
}

/// Formats a `ValidationReport` as deterministic, machine-readable JSON (`--diagnostics=json`).
inline std::string FormatValidationReportAsJson(const ValidationReport& rep) {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"ok\": " << (rep.IsOk() ? "true" : "false") << ",\n"
        << "  \"manifest\": \"" << JsonEscapeString(WideToUtf8(rep.manifestPath.wstring())) << "\",\n"
        << "  \"architecture\": \"" << JsonEscapeString(rep.resolvedArchitecture) << "\",\n"
        << "  \"edition\": \"" << EditionSlug(rep.activeEdition) << "\",\n"
        << "  \"errorCount\": " << rep.ErrorCount() << ",\n"
        << "  \"warningCount\": " << rep.WarningCount() << ",\n"
        << "  \"diagnostics\": [";
    for (std::size_t i = 0; i < rep.diagnostics.size(); ++i) {
        const auto& d = rep.diagnostics[i];
        oss << (i == 0 ? "\n" : ",\n")
            << "    {\n"
            << "      \"severity\": \"" << (d.severity == DiagnosticSeverity::Error ? "error" : "warning") << "\",\n"
            << "      \"code\": \"" << JsonEscapeString(d.code) << "\",\n"
            << "      \"line\": " << d.lineNumber << ",\n"
            << "      \"section\": \"" << JsonEscapeString(d.section) << "\",\n"
            << "      \"key\": \"" << JsonEscapeString(d.key) << "\",\n"
            << "      \"message\": \"" << JsonEscapeString(d.message) << "\"\n"
            << "    }";
    }
    if (!rep.diagnostics.empty()) {
        oss << "\n  ";
    }
    oss << "]\n}\n";
    return oss.str();
}

} // namespace rsw
