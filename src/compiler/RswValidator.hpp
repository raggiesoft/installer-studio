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
#include "../common/RswManifestAst.hpp"
#include "../common/RswSchema.hpp"

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

/// Runs complete structural, schema, filesystem, and Windows Installer rule validation on `doc`.
inline ValidationReport ValidateManifestDocument(
    const RswManifestDocument& doc,
    std::string_view cliArchOverride = "")
{
    ValidationReport rep;
    rep.manifestPath = doc.sourcePath;

    std::string rawArch = !cliArchOverride.empty()
        ? std::string(cliArchOverride)
        : doc.GetString("Package", "", "Architecture", "arm64");
    rep.resolvedArchitecture = AsciiLower(TrimAscii(rawArch));

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

    // 2. Required sections & required fields check
    for (const auto& sd : kSectionDescriptors) {
        if (sd.cardinality == SectionCardinality::SingletonRequired) {
            if (seenSingletonSections.find(AsciiLower(sd.nameOrPrefix)) == seenSingletonSections.end()) {
                addErr("RSW015", 1, std::string(sd.nameOrPrefix), "",
                       "Missing required section '[" + std::string(sd.nameOrPrefix) + "]'.");
            } else {
                for (const auto& fd : kFieldDescriptors) {
                    if (fd.required && EqualsIgnoreCase(fd.sectionPrefix, sd.nameOrPrefix)) {
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

    return rep;
}

/// Formats a `ValidationReport` as deterministic, machine-readable JSON (`--diagnostics=json`).
inline std::string FormatValidationReportAsJson(const ValidationReport& rep) {
    std::ostringstream oss;
    oss << "{\n"
        << "  \"ok\": " << (rep.IsOk() ? "true" : "false") << ",\n"
        << "  \"manifest\": \"" << JsonEscapeString(WideToUtf8(rep.manifestPath.wstring())) << "\",\n"
        << "  \"architecture\": \"" << JsonEscapeString(rep.resolvedArchitecture) << "\",\n"
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
