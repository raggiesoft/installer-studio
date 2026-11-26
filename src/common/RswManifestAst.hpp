// ================================================================================
// FILE: src/common/RswManifestAst.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is an "AST" (Abstract Syntax Tree), and why preserve comments & `@include`s?
//     Most quick INI parsers throw away `;` and `#` comments as soon as they read
//     a file into a `std::map`. That works fine for a one-way compiler, EXCEPT
//     the moment you open the project in a visual GUI IDE (`InstallerStudio.exe`)
//     and click Save, a map-based serializer wipes out all of the developer's
//     hand-written teaching comments, `@include` directives, and key ordering!
//
//     Instead, `RswManifestDocument` stores EVERY line of a `.rsip`, `.rsis`, or
//     `.rsii` file as an ordered sequence of `RswAstLine` records:
//       - `Blank`            : Preserves exact vertical spacing
//       - `Comment`          : Preserves full-line `; ...` and `# ...` comments verbatim
//       - `IncludeDirective` : `@include "shared/company-signing.rsii"` (preserved verbatim!)
//       - `SectionHeader`    : `[Package]` or `[Payload.Core]` (+ optional trailing comment)
//       - `KeyValue`         : `Name = Hello RaggieSoft ; inline comment` (preserving exact
//                              indentation, `=` spacing, quotes if used, and trailing comment!)
//
//     Result:
//       1. `Serialize()` on an unmodified AST reproduces the input text **100% byte-for-byte**!
//       2. Calling `SetValue("Package", "", "Version", "1.0.1")` updates ONLY that
//          value while leaving every single comment, `@include`, and neighboring line untouched!
// ================================================================================

#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rsw {

enum class AstLineKind {
    Blank,
    Comment,
    IncludeDirective,
    SectionHeader,
    KeyValue,
    Malformed
};

struct RswAstLine {
    AstLineKind kind          = AstLineKind::Blank;
    int         lineNumber    = 0;     // 1-based source line number for pinpoint error diagnostics
    std::string rawLine;               // Exact original line (without `\r\n`) for lossless round-trip
    bool        mutated       = false; // `false` -> `Serialize()` emits `rawLine` verbatim!

    // Populated for `IncludeDirective` (`@include "path.rsii"`)
    std::string includePath;

    // Populated for `SectionHeader` (`[Prefix]` or `[Prefix.ItemId]`) and `KeyValue`
    std::string sectionPrefix;         // e.g. `"Package"` or `"Payload"`
    std::string sectionItemId;         // e.g. `""` for `[Package]`, `"Core"` for `[Payload.Core]`

    // Populated for `KeyValue`
    std::string leadingIndent;
    std::string key;
    std::string separator     = " = ";
    std::string value;                 // Unquoted, trimmed logical value
    bool        wasQuoted     = false;
    std::string trailingComment;       // Preserves inline `; comment` at end of a `Key = Value` line!

    // Populated for `Malformed`
    std::string parseError;
};

inline std::string AsciiLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

inline std::string TrimAscii(std::string_view s) {
    std::size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t')) {
        ++start;
    }
    std::size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t')) {
        --end;
    }
    return std::string(s.substr(start, end - start));
}

inline bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        char ca = (a[i] >= 'A' && a[i] <= 'Z') ? static_cast<char>(a[i] - 'A' + 'a') : a[i];
        char cb = (b[i] >= 'A' && b[i] <= 'Z') ? static_cast<char>(b[i] - 'A' + 'a') : b[i];
        if (ca != cb) {
            return false;
        }
    }
    return true;
}

/// Replaces every occurrence of `$(TOKEN)` (case-insensitively) with `replacement`.
inline std::string ExpandNamedToken(std::string_view raw, std::string_view tokenName, std::string_view replacement) {
    std::string out(raw);
    const std::string needle = "$(" + AsciiLower(tokenName) + ")";
    std::size_t pos = 0;
    while (pos < out.size()) {
        std::string lowerSlice = AsciiLower(out);
        auto hit = lowerSlice.find(needle, pos);
        if (hit == std::string::npos) {
            break;
        }
        out.replace(hit, needle.size(), replacement);
        pos = hit + replacement.size();
    }
    return out;
}

/// Replaces every occurrence of `$(ARCH)` (case-insensitively) with `archSlug` (`"x64"` or `"arm64"`).
inline std::string ExpandArchToken(std::string_view raw, std::string_view archSlug) {
    return ExpandNamedToken(raw, "arch", archSlug);
}

class RswManifestDocument {
public:
    bool                     hasUtf8Bom      = false;
    bool                     useCrlf         = true;
    bool                     endsWithNewline = true;
    std::filesystem::path    sourcePath;
    std::vector<RswAstLine>  lines;

    /// Parses a `.rsip`, `.rsis`, or `.rsii` manifest string into a lossless, comment-preserving AST.
    static RswManifestDocument ParseFromString(
        std::string_view content,
        const std::filesystem::path& originPath = {})
    {
        RswManifestDocument doc;
        doc.sourcePath = originPath;

        // Detect and strip optional UTF-8 BOM (`\xEF\xBB\xBF`), remembering it for round-trip save.
        if (content.size() >= 3 &&
            static_cast<unsigned char>(content[0]) == 0xEF &&
            static_cast<unsigned char>(content[1]) == 0xBB &&
            static_cast<unsigned char>(content[2]) == 0xBF) {
            doc.hasUtf8Bom = true;
            content.remove_prefix(3);
        }

        doc.useCrlf = (content.find("\r\n") != std::string_view::npos);
        doc.endsWithNewline = !content.empty() && (content.back() == '\n');

        std::string activePrefix;
        std::string activeItemId;

        std::size_t cursor = 0;
        int lineNo = 0;
        while (cursor < content.size()) {
            ++lineNo;
            std::size_t nlPos = content.find('\n', cursor);
            std::string_view rawSlice = (nlPos == std::string_view::npos)
                ? content.substr(cursor)
                : content.substr(cursor, nlPos - cursor);
            while (!rawSlice.empty() && rawSlice.back() == '\r') {
                rawSlice.remove_suffix(1);
            }

            doc.lines.push_back(ParseSingleLine(rawSlice, lineNo, activePrefix, activeItemId));

            if (nlPos == std::string_view::npos) {
                break;
            }
            cursor = nlPos + 1;
        }

        return doc;
    }

    static RswManifestDocument LoadFromFile(const std::filesystem::path& path, std::string& outError) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            outError = "Could not open manifest file: " + path.string();
            return {};
        }
        std::ostringstream oss;
        oss << in.rdbuf();
        return ParseFromString(oss.str(), path);
    }

    /// Serializes the AST back to plain text, preserving 100% of untouched lines, `@include`s, and comments!
    [[nodiscard]] std::string Serialize() const {
        std::string out;
        if (hasUtf8Bom) {
            out += "\xEF\xBB\xBF";
        }
        const std::string_view eol = useCrlf ? "\r\n" : "\n";
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const auto& ln = lines[i];
            if (!ln.mutated) {
                out += ln.rawLine;
            } else if (ln.kind == AstLineKind::IncludeDirective) {
                out += "@include \"";
                out += ln.includePath;
                out += "\"";
                if (!ln.trailingComment.empty()) {
                    out += " ";
                    out += ln.trailingComment;
                }
            } else if (ln.kind == AstLineKind::KeyValue) {
                out += ln.leadingIndent;
                out += ln.key;
                out += ln.separator;
                if (ln.wasQuoted || (!ln.value.empty() && (ln.value.front() == ' ' || ln.value.back() == ' ')) ||
                    ln.value.find(';') != std::string::npos || ln.value.find('#') != std::string::npos) {
                    out += '"';
                    out += ln.value;
                    out += '"';
                } else {
                    out += ln.value;
                }
                if (!ln.trailingComment.empty()) {
                    out += " ";
                    out += ln.trailingComment;
                }
            } else if (ln.kind == AstLineKind::SectionHeader) {
                out += "[";
                out += ln.sectionPrefix;
                if (!ln.sectionItemId.empty()) {
                    out += ".";
                    out += ln.sectionItemId;
                }
                out += "]";
                if (!ln.trailingComment.empty()) {
                    out += " ";
                    out += ln.trailingComment;
                }
            } else {
                out += ln.rawLine;
            }

            if (i + 1 < lines.size() || endsWithNewline) {
                out += eol;
            }
        }
        return out;
    }

    /// Updates an existing key in-place (preserving its inline comment & formatting) or appends it under its section!
    void SetValue(
        std::string_view sectionPrefix,
        std::string_view sectionItemId,
        std::string_view key,
        std::string_view newValue)
    {
        std::size_t lastLineInSection = lines.size();
        bool foundSection = false;

        for (std::size_t i = 0; i < lines.size(); ++i) {
            auto& ln = lines[i];
            if (ln.kind == AstLineKind::SectionHeader) {
                if (EqualsIgnoreCase(ln.sectionPrefix, sectionPrefix) &&
                    EqualsIgnoreCase(ln.sectionItemId, sectionItemId)) {
                    foundSection = true;
                    lastLineInSection = i;
                }
            } else if (ln.kind == AstLineKind::KeyValue &&
                       EqualsIgnoreCase(ln.sectionPrefix, sectionPrefix) &&
                       EqualsIgnoreCase(ln.sectionItemId, sectionItemId)) {
                lastLineInSection = i;
                if (EqualsIgnoreCase(ln.key, key)) {
                    ln.value = std::string(newValue);
                    ln.mutated = true;
                    return;
                }
            }
        }

        if (!foundSection) {
            RswAstLine sec{};
            sec.kind = AstLineKind::SectionHeader;
            sec.sectionPrefix = std::string(sectionPrefix);
            sec.sectionItemId = std::string(sectionItemId);
            sec.mutated = true;
            lines.push_back(std::move(sec));
            lastLineInSection = lines.size() - 1;
        }

        RswAstLine kv{};
        kv.kind = AstLineKind::KeyValue;
        kv.sectionPrefix = std::string(sectionPrefix);
        kv.sectionItemId = std::string(sectionItemId);
        kv.key = std::string(key);
        kv.value = std::string(newValue);
        kv.mutated = true;
        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(lastLineInSection + 1), std::move(kv));
    }

    void SetString(
        std::string_view sectionPrefix,
        std::string_view sectionItemId,
        std::string_view key,
        std::string_view newValue)
    {
        SetValue(sectionPrefix, sectionItemId, key, newValue);
    }

    /// Checks whether a specific `(sectionPrefix, sectionItemId, key)` exists in this document.
    [[nodiscard]] bool HasKey(
        std::string_view sectionPrefix,
        std::string_view sectionItemId,
        std::string_view key) const noexcept
    {
        for (const auto& ln : lines) {
            if (ln.kind == AstLineKind::KeyValue &&
                EqualsIgnoreCase(ln.sectionPrefix, sectionPrefix) &&
                EqualsIgnoreCase(ln.sectionItemId, sectionItemId) &&
                EqualsIgnoreCase(ln.key, key)) {
                return true;
            }
        }
        return false;
    }

    /// Looks up a scalar string value (last matching definition wins when `@include`s are merged).
    [[nodiscard]] std::string GetString(
        std::string_view sectionPrefix,
        std::string_view sectionItemId,
        std::string_view key,
        std::string_view defaultVal = "") const
    {
        for (std::size_t i = lines.size(); i > 0; --i) {
            const auto& ln = lines[i - 1];
            if (ln.kind == AstLineKind::KeyValue &&
                EqualsIgnoreCase(ln.sectionPrefix, sectionPrefix) &&
                EqualsIgnoreCase(ln.sectionItemId, sectionItemId) &&
                EqualsIgnoreCase(ln.key, key)) {
                return ln.value;
            }
        }
        return std::string(defaultVal);
    }

    [[nodiscard]] bool GetBool(
        std::string_view sectionPrefix,
        std::string_view sectionItemId,
        std::string_view key,
        bool defaultVal = false) const
    {
        std::string raw = AsciiLower(GetString(sectionPrefix, sectionItemId, key, ""));
        if (raw.empty()) return defaultVal;
        return (raw == "true" || raw == "yes" || raw == "1");
    }

    [[nodiscard]] int GetInt(
        std::string_view sectionPrefix,
        std::string_view sectionItemId,
        std::string_view key,
        int defaultVal = 0) const
    {
        std::string raw = GetString(sectionPrefix, sectionItemId, key, "");
        if (raw.empty()) return defaultVal;
        try {
            return std::stoi(raw);
        } catch (...) {
            return defaultVal;
        }
    }

    [[nodiscard]] std::vector<std::string> GetNamedSectionIds(std::string_view sectionPrefix) const {
        std::vector<std::string> ids;
        for (const auto& ln : lines) {
            if (ln.kind == AstLineKind::SectionHeader &&
                EqualsIgnoreCase(ln.sectionPrefix, sectionPrefix) &&
                !ln.sectionItemId.empty()) {
                bool exists = false;
                for (const auto& existing : ids) {
                    if (EqualsIgnoreCase(existing, ln.sectionItemId)) {
                        exists = true;
                        break;
                    }
                }
                if (!exists) {
                    ids.push_back(ln.sectionItemId);
                }
            }
        }
        return ids;
    }

private:
    static RswAstLine ParseSingleLine(
        std::string_view raw,
        int lineNo,
        std::string& activePrefix,
        std::string& activeItemId)
    {
        RswAstLine node;
        node.lineNumber = lineNo;
        node.rawLine = std::string(raw);

        std::size_t firstNonWs = 0;
        while (firstNonWs < raw.size() && (raw[firstNonWs] == ' ' || raw[firstNonWs] == '\t')) {
            ++firstNonWs;
        }

        if (firstNonWs == raw.size()) {
            node.kind = AstLineKind::Blank;
            return node;
        }

        const char lead = raw[firstNonWs];
        if (lead == ';' || lead == '#') {
            node.kind = AstLineKind::Comment;
            return node;
        }

        // `@include "shared/publisher.rsii"` directive (shared `.rsii` header support!)
        if (lead == '@') {
            std::string_view dirView = raw.substr(firstNonWs);
            if (dirView.size() > 8 && EqualsIgnoreCase(dirView.substr(0, 8), "@include") &&
                (dirView[8] == ' ' || dirView[8] == '\t' || dirView[8] == '"')) {
                std::string arg = TrimAscii(dirView.substr(8));
                std::string trail;
                if (!arg.empty() && (arg.front() == '"' || arg.front() == '\'')) {
                    const char qc = arg.front();
                    auto closeQ = arg.find(qc, 1);
                    if (closeQ == std::string::npos) {
                        node.kind = AstLineKind::Malformed;
                        node.parseError = "Unterminated quote in @include directive.";
                        return node;
                    }
                    trail = TrimAscii(std::string_view(arg).substr(closeQ + 1));
                    arg = arg.substr(1, closeQ - 1);
                }
                if (arg.empty()) {
                    node.kind = AstLineKind::Malformed;
                    node.parseError = "@include directive requires a relative or absolute .rsii file path.";
                    return node;
                }
                node.kind = AstLineKind::IncludeDirective;
                node.includePath = arg;
                node.trailingComment = trail;
                return node;
            }
            node.kind = AstLineKind::Malformed;
            node.parseError = "Unknown '@' directive (expected '@include \"path.rsii\"').";
            return node;
        }

        // `[Section]` or `[Prefix.ItemId]`
        if (lead == '[') {
            auto closeBracket = raw.find(']', firstNonWs + 1);
            if (closeBracket == std::string_view::npos) {
                node.kind = AstLineKind::Malformed;
                node.parseError = "Missing closing ']' on section header.";
                return node;
            }

            std::string inside = TrimAscii(raw.substr(firstNonWs + 1, closeBracket - firstNonWs - 1));
            std::string rest   = TrimAscii(raw.substr(closeBracket + 1));
            if (!rest.empty() && rest[0] != ';' && rest[0] != '#') {
                node.kind = AstLineKind::Malformed;
                node.parseError = "Unexpected characters after ']' on section header.";
                return node;
            }

            node.kind = AstLineKind::SectionHeader;
            node.trailingComment = rest;

            auto dotPos = inside.find('.');
            if (dotPos == std::string::npos) {
                activePrefix = inside;
                activeItemId.clear();
            } else {
                activePrefix = TrimAscii(std::string_view(inside).substr(0, dotPos));
                activeItemId = TrimAscii(std::string_view(inside).substr(dotPos + 1));
            }
            node.sectionPrefix = activePrefix;
            node.sectionItemId = activeItemId;
            return node;
        }

        // `Key = Value`
        auto eqPos = raw.find('=', firstNonWs);
        if (eqPos == std::string_view::npos) {
            node.kind = AstLineKind::Malformed;
            node.parseError = "Expected 'Key = Value' assignment, '@include', or '[Section]' header.";
            return node;
        }

        if (activePrefix.empty()) {
            node.kind = AstLineKind::Malformed;
            node.parseError = "Key-value assignment appears before any '[Section]' header.";
            return node;
        }

        node.kind          = AstLineKind::KeyValue;
        node.sectionPrefix = activePrefix;
        node.sectionItemId = activeItemId;
        node.leadingIndent = std::string(raw.substr(0, firstNonWs));
        node.key           = TrimAscii(raw.substr(firstNonWs, eqPos - firstNonWs));

        std::size_t keyEnd = firstNonWs + node.key.size();
        std::size_t valStart = eqPos + 1;
        while (valStart < raw.size() && (raw[valStart] == ' ' || raw[valStart] == '\t')) {
            ++valStart;
        }
        node.separator = std::string(raw.substr(keyEnd, valStart - keyEnd));

        std::string_view rhs = raw.substr(valStart);
        if (!rhs.empty() && (rhs.front() == '"' || rhs.front() == '\'')) {
            const char quoteChar = rhs.front();
            std::size_t endQ = 1;
            std::string unescaped;
            while (endQ < rhs.size() && rhs[endQ] != quoteChar) {
                if (rhs[endQ] == '\\' && endQ + 1 < rhs.size() && rhs[endQ + 1] == quoteChar) {
                    unescaped += quoteChar;
                    endQ += 2;
                } else {
                    unescaped += rhs[endQ++];
                }
            }
            if (endQ >= rhs.size()) {
                node.kind = AstLineKind::Malformed;
                node.parseError = "Unterminated quoted string value.";
                return node;
            }
            node.wasQuoted = true;
            node.value = std::move(unescaped);
            node.trailingComment = TrimAscii(rhs.substr(endQ + 1));
        } else {
            // Unquoted value: an inline `;` preceded by whitespace starts a trailing comment!
            // Note: `#` is reserved for full-line comments so `#RRGGBB` hex colors (like `#2563EB`)
            // and URL `#anchors` work cleanly without requiring quotes!
            std::size_t commentPos = std::string_view::npos;
            for (std::size_t i = 0; i < rhs.size(); ++i) {
                if (rhs[i] == ';' && (i == 0 || rhs[i - 1] == ' ' || rhs[i - 1] == '\t')) {
                    commentPos = i;
                    break;
                }
            }
            if (commentPos != std::string_view::npos) {
                node.value = TrimAscii(rhs.substr(0, commentPos));
                node.trailingComment = TrimAscii(rhs.substr(commentPos));
            } else {
                node.value = TrimAscii(rhs);
            }
        }

        return node;
    }
};

inline RswManifestDocument ParseManifestText(
    std::string_view content,
    const std::filesystem::path& originPath = {})
{
    return RswManifestDocument::ParseFromString(content, originPath);
}

inline bool LoadManifestFile(
    const std::filesystem::path& path,
    RswManifestDocument& outDoc,
    std::string& outError)
{
    outError.clear();
    outDoc = RswManifestDocument::LoadFromFile(path, outError);
    return outError.empty();
}

inline bool SaveManifestFile(
    const std::filesystem::path& path,
    const RswManifestDocument& doc,
    std::string& outError)
{
    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
    }
    std::ofstream out(path, std::ios::binary);
    if (!out) {
        outError = "Could not open file for writing: " + path.string();
        return false;
    }
    const std::string text = doc.Serialize();
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!out.good()) {
        outError = "Failed writing manifest bytes to: " + path.string();
        return false;
    }
    return true;
}

/// Resolves `@include "*.rsii"` directives and optional `$(VAR)` suite/CLI tokens into an
/// effective compile-time `RswManifestDocument` without altering the on-disk `.rsip` file!
/// Included lines are spliced in where `@include` appears so local keys declared later in
/// the `.rsip` file naturally override defaults inherited from `.rsii` (`GetString` reads bottom-up).
inline bool ResolveManifestIncludesAndVars(
    const RswManifestDocument& rootDoc,
    const std::map<std::string, std::string>& variables,
    RswManifestDocument& outResolved,
    std::string& outError,
    int depth = 0)
{
    if (depth > 8) {
        outError = "Maximum @include nesting depth (8) exceeded — check for circular .rsii includes.";
        return false;
    }

    outResolved = rootDoc;
    outResolved.lines.clear();

    const std::filesystem::path baseDir = rootDoc.sourcePath.has_parent_path()
        ? rootDoc.sourcePath.parent_path()
        : std::filesystem::current_path();

    for (const auto& ln : rootDoc.lines) {
        if (ln.kind == AstLineKind::IncludeDirective) {
            std::string expandedInc = ln.includePath;
            for (const auto& [k, v] : variables) {
                expandedInc = ExpandNamedToken(expandedInc, k, v);
            }
            std::filesystem::path incPath(expandedInc);
            if (incPath.is_relative()) {
                incPath = (baseDir / incPath).lexically_normal();
            }
            RswManifestDocument childRaw;
            if (!LoadManifestFile(incPath, childRaw, outError)) {
                return false;
            }
            RswManifestDocument childResolved;
            if (!ResolveManifestIncludesAndVars(childRaw, variables, childResolved, outError, depth + 1)) {
                return false;
            }
            // Rebasing relative file paths in `[UI]` / `[Payload.*]` / `[Signing]` inside `.rsii`
            // relative to the `.rsip` project directory if needed, or preserve directly:
            for (auto childLn : childResolved.lines) {
                outResolved.lines.push_back(std::move(childLn));
            }
            continue;
        }

        RswAstLine copyLn = ln;
        if (copyLn.kind == AstLineKind::KeyValue && !variables.empty()) {
            for (const auto& [k, v] : variables) {
                copyLn.value = ExpandNamedToken(copyLn.value, k, v);
            }
        }
        outResolved.lines.push_back(std::move(copyLn));
    }

    // Deduplicate repeatable `SectionHeader` entries while preserving last-wins `KeyValue` overrides
    // so `RswValidator`'s singleton section count (`[Package]` == 1) stays 100% satisfied when `.rsii`
    // and `.rsip` both contribute keys to `[Package]`, `[UI]`, or `[Signing]`!
    std::vector<RswAstLine> normalized;
    std::vector<std::pair<std::string, std::string>> seenSections;
    for (const auto& ln : outResolved.lines) {
        if (ln.kind == AstLineKind::SectionHeader) {
            bool already = false;
            for (const auto& [sp, si] : seenSections) {
                if (EqualsIgnoreCase(sp, ln.sectionPrefix) && EqualsIgnoreCase(si, ln.sectionItemId)) {
                    already = true;
                    break;
                }
            }
            if (already) {
                continue;
            }
            seenSections.emplace_back(ln.sectionPrefix, ln.sectionItemId);
        }
        normalized.push_back(ln);
    }
    outResolved.lines = std::move(normalized);

    // Automatically expand built-in `$(VERSION)` across all `KeyValue` entries (e.g. `OutputName`,
    // `UniversalOutputName`, `OutputDir`, and `UniversalDownloadBaseUrl`) using `[Package] Version`!
    const std::string resolvedPkgVer = TrimAscii(outResolved.GetString("Package", "", "Version"));
    if (!resolvedPkgVer.empty() && AsciiLower(resolvedPkgVer).find("$(version)") == std::string::npos) {
        for (auto& ln : outResolved.lines) {
            if (ln.kind == AstLineKind::KeyValue) {
                ln.value = ExpandNamedToken(ln.value, "VERSION", resolvedPkgVer);
            }
        }
    }
    return true;
}

} // namespace rsw
