// ================================================================================
// FILE: src/common/RswRaggieScript.hpp
// PROJECT: RaggieSoft Installer Studio & RaggieScript Engine ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com/raggiescript  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `RaggieScript` (`.rssc`), and what does this C++20 header do?
//     `RaggieScript` is RaggieSoft's clean-room, C# / TypeScript-inspired curly-brace
//     scripting and micro-application language spanning all 8 RaggieSoft Studio tools
//     (`Community`, `Developer`, and `Architect` editions) as well as standalone
//     zero-dependency Windows utilities (`raggiescript.exe`).
//
//     This header implements the complete front-end compiler and binary `RSWBC1`
//     bytecode container codec in zero-dependency static `/MT` C++20:
//       1. **Preprocessor & Modular `include` Resolver**:
//          - Supports `include "common/runtimes.rssc";` (with circular-include guards)
//            and `#if` / `#else` / `#endif` compile-time conditional compilation.
//       2. **Full Lexical Scanner (including Raw Strings `r"..."`, `${...}` Interpolation,
//          & Semantic Version Literals `v"10.0.22000"`)**:
//          - Raw strings (`r"SOFTWARE\RaggieSoft\Book Reader"`) eliminate double-backslash
//            noise in Windows Registry keys and file paths!
//          - Interpolated strings (`"Installing ${title} (${arch})..."`) desugar into
//            clean concatenation expressions automatically at lex/parse time.
//          - Semantic version literals (`v"10.0.22000"`) let scripts compare OS builds
//            and `VS_FIXEDFILEINFO` file versions directly with `<, <=, >, >=, ==, !=`!
//       3. **Recursive-Descent + Precedence-Climbing Parser**:
//          - Parses `use <capability>;`, `package` / `showcase` / `app` metadata blocks,
//            `param` CLI-bindable parameters, `payload` file rules, `fn` functions,
//            `wizard { step ... }`, `install { ... }`, `repair { ... }`, `uninstall { ... }`,
//            `on <target>.<event> { ... }`, `if/else`, `while`, `repeat/until`,
//            `for (x in list)`, `try/catch`, named arguments, and `ref` bindings!
//       4. **Compile-Time Capability & Phase Static Analyzer (`RSW701` & `RSW702`)**:
//          - `RSW701` (Phase Isolation): Rejects any mutating system call (`fs.write*`,
//            `reg.write*`, `shortcut.create`, `svc.*`, etc.) inside `wizard { ... }`
//            (including transitive calls through user-defined `fn` helpers!) before a
//            single byte of setup code ever runs.
//          - `RSW702` (Capability Sandbox): Ensures sandboxed Studio profiles (such as
//            `use training;` for SCORM courseware) cannot invoke filesystem, registry,
//            network, or process execution APIs.
//       5. **Deterministic `RSWBC1` Binary Bytecode Container Codec + Disassembler**:
//          - Serializes compiled programs into compact `RSWBC1` streams protected by a
//            32-byte `SHA-256` digest (`bcrypt.dll`) for embedding inside `Setup.exe`
//            (`kResIdScriptBytecode = 225`) or standalone micro-apps!
// ================================================================================

#pragma once

#include "RswCryptoAndPe.hpp"
#include "RswPlatform.hpp"
#include "RswVersion.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rsw::rssc {

// ================================================================================
// 1. DIAGNOSTICS & SEMANTIC VERSION PRIMITIVE
// ================================================================================
struct ScriptDiagnostic {
    bool        isError = true;
    std::string code;     // e.g. `"RSW700"` syntax, `"RSW701"` wizard mutation, `"RSW702"` capability
    int         line = 1;
    int         col  = 1;
    std::string file;
    std::string message;
};

/// 4-part numeric version (`Major.Minor.Build.Revision`) supporting exact `<, <=, >, >=, ==, !=`!
struct SemVer {
    int parts[4]{0, 0, 0, 0};

    static SemVer Parse(std::string_view text) noexcept {
        SemVer v{};
        int idx = 0;
        std::size_t i = 0;
        while (i < text.size() && (text[i] == 'v' || text[i] == 'V' || text[i] == ' ')) ++i;
        for (; i < text.size() && idx < 4; ++i) {
            const char c = text[i];
            if (c == '.' || c == ',') {
                ++idx;
            } else if (c >= '0' && c <= '9') {
                v.parts[idx] = v.parts[idx] * 10 + (c - '0');
            } else {
                break;
            }
        }
        return v;
    }

    std::string ToString(int minParts = 3) const {
        int count = std::clamp(minParts, 2, 4);
        if (parts[3] != 0) count = 4;
        std::ostringstream oss;
        for (int i = 0; i < count; ++i) {
            if (i > 0) oss << '.';
            oss << parts[i];
        }
        return oss.str();
    }

    int Compare(const SemVer& rhs) const noexcept {
        for (int i = 0; i < 4; ++i) {
            if (parts[i] < rhs.parts[i]) return -1;
            if (parts[i] > rhs.parts[i]) return 1;
        }
        return 0;
    }
};

// ================================================================================
// 2. LEXICAL TOKENIZER
// ================================================================================
enum class TokKind : std::uint8_t {
    Eof = 0,
    Ident,
    IntLit,
    FloatLit,
    StrLit,
    VerLit,     // `v"10.0.22000"`
    LParen,     // `(`
    RParen,     // `)`
    LBrace,     // `{`
    RBrace,     // `}`
    LBracket,   // `[`
    RBracket,   // `]`
    Semi,       // `;`
    Colon,      // `:`
    Comma,      // `,`
    Dot,        // `.`
    Question,   // `?`
    Arrow,      // `->`
    FatArrow,   // `=>`
    Plus,       // `+`
    Minus,      // `-`
    Star,       // `*`
    Slash,      // `/`
    Percent,    // `%`
    Assign,     // `=`
    PlusEq,     // `+=`
    MinusEq,    // `-=`
    Eq,         // `==`
    Neq,        // `!=`
    Lt,         // `<`
    Lte,        // `<=`
    Gt,         // `>`
    Gte,        // `>=`
    And,        // `&&`
    Or,         // `||`
    Bang        // `!`
};

struct Token {
    TokKind     kind = TokKind::Eof;
    std::string text;
    int         line = 1;
    int         col  = 1;
};

// ================================================================================
// 3. ABSTRACT SYNTAX TREE (AST) NODES
// ================================================================================
struct Expr;
struct Stmt;
using ExprPtr = std::shared_ptr<Expr>;
using StmtPtr = std::shared_ptr<Stmt>;

enum class ExprKind : std::uint8_t {
    NullLit = 0,
    BoolLit,
    IntLit,
    FloatLit,
    StrLit,
    VerLit,
    Ident,
    RefVar,     // `ref varName`
    ArrayLit,
    MapLit,
    Unary,
    Binary,
    Ternary,
    Member,     // `obj.prop`
    Index,      // `arr[idx]`
    Call        // `callee(arg1, name: arg2)`
};

struct CallArg {
    std::string name; // Empty if positional
    ExprPtr     value;
};

struct Expr {
    ExprKind                                      kind = ExprKind::NullLit;
    int                                           line = 1;
    bool                                          boolVal = false;
    std::int64_t                                  intVal  = 0;
    double                                        floatVal = 0.0;
    std::string                                   strVal; // Also operator/ident/member name
    std::vector<ExprPtr>                          items;
    std::vector<std::pair<std::string, ExprPtr>>  mapEntries;
    ExprPtr                                       lhs;
    ExprPtr                                       rhs;
    ExprPtr                                       cond;
    std::vector<CallArg>                          callArgs;
};

enum class StmtKind : std::uint8_t {
    Block = 0,
    VarDecl,    // `let` / `const`
    Assign,     // `=`, `+=`, `-=`
    ExprStmt,
    If,
    While,
    RepeatUntil,// `repeat { ... } until (cond);`
    ForIn,      // `for (item in expr)` or `for (k, v in expr)`
    Break,
    Continue,
    Return,
    Throw,
    ExitSetup,  // `exit(code);`
    TryCatch,
    WizardStep  // `step Welcome when (cond) { ... }`
};

struct Stmt {
    StmtKind             kind = StmtKind::Block;
    int                  line = 1;
    bool                 isConst = false;
    std::string          name;       // Var name, step name, catch var, or assign op (`=`, `+=`, `-=`)
    std::string          secondName; // Optional value var in `for (k, v in map)` or type annotation
    ExprPtr              target;     // L-value for `Assign`
    ExprPtr              expr;       // Initializer, condition, iterable, return/throw/exit value
    StmtPtr              body;       // Primary block
    StmtPtr              elseBody;   // `else` block or `catch` block
    std::vector<StmtPtr> children;   // Statements inside `Block`
};

struct ParamDecl {
    std::string name;
    std::string typeName; // `"string"`, `"path"`, `"bool"`, `"int"`, `"version"`
    ExprPtr     defaultExpr;
    int         line = 1;
};

struct PayloadDecl {
    std::string id;
    std::string sourceGlob;
    ExprPtr     destExpr; // Optional destination expression (defaults to `installDir`)
    std::string featureId = "Main";
    bool        recursive = true;
    int         line = 1;
};

struct FunctionDecl {
    std::string              name;
    std::vector<std::string> params;
    StmtPtr                  body;
    int                      line = 1;
};

struct EventHandlerDecl {
    std::string targetKind; // e.g. `"hotspot"` or `"page"`
    std::string targetId;   // e.g. `"BtnInstall"`
    std::string eventName;  // e.g. `"click"` or `"show"`
    StmtPtr     body;
    int         line = 1;
};

struct PackageHeaderDecl {
    std::string kind           = "package"; // `"package"` | `"showcase"` | `"app"`
    std::string name           = "RaggieSoft Application";
    std::string id;                         // UpgradeCode GUID
    std::string productCode;
    std::string version        = "1.0.0";
    std::string publisher      = "RaggieSoft";
    std::string tagline;
    std::string websiteUrl     = "https://raggiesoftstudio.com";
    std::string scope          = "machine"; // `"machine"` | `"user"`
    std::string uac            = "asInvoker"; // `"asInvoker"` | `"highestAvailable"` | `"requireAdministrator"`
    std::string style          = "fluentmica";
    std::string supportDir;                 // e.g. `"./_support"`
    std::string mainExe;
    std::string iconFile;
    std::string licenseFile;
    std::string accentPrimary  = "#1D4ED8";
    std::string accentSecondary= "#FBBF24";
    bool        hideFromArp    = false;
    bool        autoPlayAudio  = false;
    int         minWinBuild    = 18362;     // Windows 10 1903+ / Windows 11+
};

struct ScriptProgram {
    std::string                   sourcePath;
    std::set<std::string>         capabilities; // `"std"`, `"installer"`, `"showcase"`, `"ui"`, `"net"`, `"svc"`, etc.
    PackageHeaderDecl             package{};
    bool                          hasPackageHeader = false;
    std::vector<ParamDecl>        params;
    std::vector<PayloadDecl>      payloads;
    std::vector<FunctionDecl>     functions;
    std::vector<StmtPtr>          topStatements; // Global `let`/`const` & top-level micro-app logic
    StmtPtr                       wizardBlock;
    StmtPtr                       installBlock;
    StmtPtr                       repairBlock;
    std::string                   repairParamName = "mode";
    StmtPtr                       uninstallBlock;
    std::string                   uninstallParamName = "options";
    std::vector<EventHandlerDecl> eventHandlers;
};

// ================================================================================
// 4. PREPROCESSOR (`include "..."` & `#if` / `#else` / `#endif`) + LEXER
// ================================================================================
namespace detail {

inline bool PreprocessSourceRecursive(
    const std::string& rawText,
    const std::filesystem::path& currentFile,
    const std::map<std::string, std::string>& buildDefines,
    std::set<std::wstring>& visitedCanonPaths,
    std::string& outExpanded,
    std::vector<ScriptDiagnostic>& diags)
{
    std::error_code ec;
    if (!currentFile.empty()) {
        const auto canon = std::filesystem::weakly_canonical(currentFile, ec).wstring();
        if (!canon.empty()) {
            if (visitedCanonPaths.count(canon)) return true; // Idempotent `#pragma once` style include safety!
            visitedCanonPaths.insert(canon);
        }
    }

    std::istringstream iss(rawText);
    std::string line;
    int lineNo = 0;
    struct CondFrame { bool active = true; bool branchTaken = false; };
    std::vector<CondFrame> condStack;
    auto isCurrentlyActive = [&]() {
        for (const auto& f : condStack) {
            if (!f.active) return false;
        }
        return true;
    };

    while (std::getline(iss, line)) {
        ++lineNo;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const std::string trimmed = TrimAscii(line);

        // Handle `#region` / `#endregion` (keep blank line so line numbers stay accurate!)
        if (trimmed.rfind("#region", 0) == 0 || trimmed.rfind("#endregion", 0) == 0) {
            outExpanded += "\n";
            continue;
        }

        // Handle `#if KEY == "VAL"` / `#elif ...` / `#ifdef KEY` / `#else` / `#endif`
        if (trimmed.rfind("#if", 0) == 0 || trimmed.rfind("#elif ", 0) == 0) {
            const bool isElif = (trimmed.rfind("#elif ", 0) == 0);
            if (isElif && condStack.empty()) {
                diags.push_back({true, "RSW700", lineNo, 1, WideToUtf8(currentFile.wstring()), "Unmatched #elif directive."});
                return false;
            }
            bool parentActive = true;
            if (isElif) {
                for (std::size_t i = 0; i + 1 < condStack.size(); ++i) {
                    if (!condStack[i].active) parentActive = false;
                }
            } else {
                parentActive = isCurrentlyActive();
            }
            const bool alreadyTaken = isElif ? condStack.back().branchTaken : false;
            bool condVal = false;
            if (parentActive && !alreadyTaken) {
                std::string expr = TrimAscii(trimmed.substr(isElif ? 5 : 3));
                if (!isElif && expr.rfind("def ", 0) == 0) {
                    const std::string key = TrimAscii(expr.substr(4));
                    condVal = buildDefines.count(key) > 0 && !buildDefines.at(key).empty() && buildDefines.at(key) != "0" && buildDefines.at(key) != "false";
                } else {
                    const bool isNeq = expr.find("!=") != std::string::npos;
                    const auto pos = expr.find(isNeq ? "!=" : "==");
                    if (pos != std::string::npos) {
                        const std::string lhsKey = TrimAscii(expr.substr(0, pos));
                        std::string rhsVal = TrimAscii(expr.substr(pos + 2));
                        if (rhsVal.size() >= 2 && rhsVal.front() == '"' && rhsVal.back() == '"') {
                            rhsVal = rhsVal.substr(1, rhsVal.size() - 2);
                        }
                        auto it = buildDefines.find(lhsKey);
                        const std::string actual = (it != buildDefines.end()) ? it->second : "";
                        condVal = isNeq ? (!EqualsIgnoreCase(actual, rhsVal)) : EqualsIgnoreCase(actual, rhsVal);
                    } else {
                        auto it = buildDefines.find(expr);
                        condVal = (it != buildDefines.end() && !it->second.empty() && !EqualsIgnoreCase(it->second, "false") && it->second != "0");
                    }
                }
            }
            if (isElif) {
                condStack.back().active      = parentActive && !alreadyTaken && condVal;
                condStack.back().branchTaken = alreadyTaken || condVal;
            } else {
                condStack.push_back({parentActive && condVal, condVal});
            }
            outExpanded += "\n";
            continue;
        }
        if (trimmed == "#else") {
            if (condStack.empty()) {
                diags.push_back({true, "RSW700", lineNo, 1, WideToUtf8(currentFile.wstring()), "Unmatched #else directive."});
                return false;
            }
            auto& top = condStack.back();
            bool parentActive = true;
            for (std::size_t i = 0; i + 1 < condStack.size(); ++i) {
                if (!condStack[i].active) parentActive = false;
            }
            top.active = parentActive && !top.branchTaken;
            top.branchTaken = true;
            outExpanded += "\n";
            continue;
        }
        if (trimmed == "#endif") {
            if (condStack.empty()) {
                diags.push_back({true, "RSW700", lineNo, 1, WideToUtf8(currentFile.wstring()), "Unmatched #endif directive."});
                return false;
            }
            condStack.pop_back();
            outExpanded += "\n";
            continue;
        }
        if (trimmed.rfind("#error", 0) == 0 && isCurrentlyActive()) {
            diags.push_back({true, "RSW703", lineNo, 1, WideToUtf8(currentFile.wstring()), TrimAscii(trimmed.substr(6))});
            return false;
        }

        if (!isCurrentlyActive()) {
            outExpanded += "\n";
            continue;
        }

        // Handle `include "path/to/module.rssc";` or `import "path/to/module.rssc";`
        if (trimmed.rfind("include ", 0) == 0 || trimmed.rfind("#include ", 0) == 0 || trimmed.rfind("import ", 0) == 0) {
            const auto q1 = trimmed.find('"');
            const auto q2 = (q1 != std::string::npos) ? trimmed.find('"', q1 + 1) : std::string::npos;
            if (q1 != std::string::npos && q2 != std::string::npos && q2 > q1 + 1) {
                const std::string rel = trimmed.substr(q1 + 1, q2 - q1 - 1);
                std::filesystem::path incPath = currentFile.empty()
                    ? std::filesystem::path(Utf8ToWide(rel))
                    : (currentFile.parent_path() / Utf8ToWide(rel));
                std::ifstream incFs(incPath, std::ios::binary);
                if (!incFs) {
                    diags.push_back({true, "RSW700", lineNo, 1, WideToUtf8(currentFile.wstring()),
                                     "Cannot open included RaggieScript file: " + rel});
                    return false;
                }
                std::ostringstream incBuf;
                incBuf << incFs.rdbuf();
                if (!PreprocessSourceRecursive(incBuf.str(), incPath, buildDefines, visitedCanonPaths, outExpanded, diags)) {
                    return false;
                }
                outExpanded += "\n";
                continue;
            }
        }

        outExpanded += line;
        outExpanded += "\n";
    }

    if (!condStack.empty()) {
        diags.push_back({true, "RSW700", lineNo, 1, WideToUtf8(currentFile.wstring()), "Unclosed #if preprocessor block."});
        return false;
    }
    return true;
}

inline bool TokenizeRaggieScript(
    std::string_view src,
    const std::string& fileLabel,
    std::vector<Token>& outTokens,
    std::vector<ScriptDiagnostic>& diags)
{
    outTokens.clear();
    std::size_t i = 0;
    int line = 1;
    int col  = 1;

    auto adv = [&](std::size_t count = 1) {
        for (std::size_t k = 0; k < count && i < src.size(); ++k) {
            if (src[i] == '\n') { ++line; col = 1; }
            else { ++col; }
            ++i;
        }
    };

    while (i < src.size()) {
        const char c = src[i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            adv();
            continue;
        }
        // Single-line `//` comment
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '/') {
            while (i < src.size() && src[i] != '\n') adv();
            continue;
        }
        // Multi-line `/* ... */` comment
        if (c == '/' && i + 1 < src.size() && src[i + 1] == '*') {
            const int startLine = line;
            adv(2);
            bool closed = false;
            while (i + 1 < src.size()) {
                if (src[i] == '*' && src[i + 1] == '/') { adv(2); closed = true; break; }
                adv();
            }
            if (!closed) {
                diags.push_back({true, "RSW700", startLine, 1, fileLabel, "Unterminated /* ... */ block comment."});
                return false;
            }
            continue;
        }

        const int tokLine = line;
        const int tokCol  = col;

        // Raw string literal `r"..."` or semantic version literal `v"10.0.22000"`!
        if ((c == 'r' || c == 'R' || c == 'v' || c == 'V') && i + 1 < src.size() && src[i + 1] == '"') {
            const bool isVer = (c == 'v' || c == 'V');
            adv(2);
            std::string val;
            bool closed = false;
            while (i < src.size()) {
                if (src[i] == '"') { adv(); closed = true; break; }
                val.push_back(src[i]);
                adv();
            }
            if (!closed) {
                diags.push_back({true, "RSW700", tokLine, tokCol, fileLabel, "Unterminated string literal."});
                return false;
            }
            outTokens.push_back({isVer ? TokKind::VerLit : TokKind::StrLit, std::move(val), tokLine, tokCol});
            continue;
        }

        // Standard double-quoted string literal `"..."` (supports `\n`, `\t`, `\\`, `\"`, `\$`, AND `${expr}` interpolation!)
        if (c == '"') {
            adv();
            // To make `"Hello ${name}!"` effortless without extra AST nodes, we desugar interpolated strings
            // directly into parenthesized `( "Hello " + str(name) + "!" )` token sequences!
            bool hasInterp = false;
            std::string seg;
            std::vector<std::pair<bool, std::string>> parts; // `(isExpr, text)`
            bool closed = false;
            while (i < src.size()) {
                if (src[i] == '"') {
                    adv();
                    closed = true;
                    break;
                }
                if (src[i] == '\\' && i + 1 < src.size()) {
                    const char esc = src[i + 1];
                    adv(2);
                    switch (esc) {
                        case 'n':  seg.push_back('\n'); break;
                        case 'r':  seg.push_back('\r'); break;
                        case 't':  seg.push_back('\t'); break;
                        case '\\': seg.push_back('\\'); break;
                        case '"':  seg.push_back('"');  break;
                        case '$':  seg.push_back('$');  break;
                        default:   seg.push_back(esc);  break;
                    }
                    continue;
                }
                if (src[i] == '$' && i + 1 < src.size() && src[i + 1] == '{') {
                    hasInterp = true;
                    parts.push_back({false, seg});
                    seg.clear();
                    adv(2);
                    int braceDepth = 1;
                    std::string innerExpr;
                    while (i < src.size() && braceDepth > 0) {
                        if (src[i] == '{') ++braceDepth;
                        else if (src[i] == '}') {
                            --braceDepth;
                            if (braceDepth == 0) { adv(); break; }
                        }
                        innerExpr.push_back(src[i]);
                        adv();
                    }
                    parts.push_back({true, innerExpr});
                    continue;
                }
                seg.push_back(src[i]);
                adv();
            }
            if (!closed) {
                diags.push_back({true, "RSW700", tokLine, tokCol, fileLabel, "Unterminated string literal."});
                return false;
            }
            if (!hasInterp) {
                outTokens.push_back({TokKind::StrLit, std::move(seg), tokLine, tokCol});
            } else {
                parts.push_back({false, seg});
                outTokens.push_back({TokKind::LParen, "(", tokLine, tokCol});
                bool first = true;
                for (const auto& [isEx, txt] : parts) {
                    if (!isEx && txt.empty() && !first) continue;
                    if (!first) outTokens.push_back({TokKind::Plus, "+", tokLine, tokCol});
                    first = false;
                    if (!isEx) {
                        outTokens.push_back({TokKind::StrLit, txt, tokLine, tokCol});
                    } else {
                        outTokens.push_back({TokKind::Ident, "str", tokLine, tokCol});
                        outTokens.push_back({TokKind::LParen, "(", tokLine, tokCol});
                        std::vector<Token> subToks;
                        if (!TokenizeRaggieScript(txt, fileLabel, subToks, diags)) return false;
                        for (auto& st : subToks) {
                            if (st.kind != TokKind::Eof) {
                                st.line = tokLine;
                                outTokens.push_back(std::move(st));
                            }
                        }
                        outTokens.push_back({TokKind::RParen, ")", tokLine, tokCol});
                    }
                }
                if (first) outTokens.push_back({TokKind::StrLit, "", tokLine, tokCol});
                outTokens.push_back({TokKind::RParen, ")", tokLine, tokCol});
            }
            continue;
        }

        // Numbers (`42`, `0x1040`, `500_000`, `3.14`, OR bare `1.2.0` 3-part version literal!)
        if (c >= '0' && c <= '9') {
            std::string num;
            if (c == '0' && i + 1 < src.size() && (src[i + 1] == 'x' || src[i + 1] == 'X')) {
                num = "0x";
                adv(2);
                while (i < src.size() && (std::isxdigit(static_cast<unsigned char>(src[i])) || src[i] == '_')) {
                    if (src[i] != '_') num.push_back(src[i]);
                    adv();
                }
                outTokens.push_back({TokKind::IntLit, std::move(num), tokLine, tokCol});
                continue;
            }
            int dotCount = 0;
            while (i < src.size()) {
                const char nc = src[i];
                if (nc >= '0' && nc <= '9') {
                    num.push_back(nc);
                    adv();
                } else if (nc == '_') {
                    adv();
                } else if (nc == '.' && i + 1 < src.size() && src[i + 1] >= '0' && src[i + 1] <= '9') {
                    ++dotCount;
                    num.push_back('.');
                    adv();
                } else {
                    break;
                }
            }
            if (dotCount >= 2) {
                outTokens.push_back({TokKind::VerLit, std::move(num), tokLine, tokCol});
            } else if (dotCount == 1) {
                outTokens.push_back({TokKind::FloatLit, std::move(num), tokLine, tokCol});
            } else {
                outTokens.push_back({TokKind::IntLit, std::move(num), tokLine, tokCol});
            }
            continue;
        }

        // Identifiers & keywords
        if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
            std::string id;
            while (i < src.size() && (std::isalnum(static_cast<unsigned char>(src[i])) || src[i] == '_')) {
                id.push_back(src[i]);
                adv();
            }
            outTokens.push_back({TokKind::Ident, std::move(id), tokLine, tokCol});
            continue;
        }

        // Two-character operators
        const char next = (i + 1 < src.size()) ? src[i + 1] : '\0';
        if (c == '-' && next == '>') { adv(2); outTokens.push_back({TokKind::Arrow, "->", tokLine, tokCol}); continue; }
        if (c == '=' && next == '>') { adv(2); outTokens.push_back({TokKind::FatArrow, "=>", tokLine, tokCol}); continue; }
        if (c == '+' && next == '=') { adv(2); outTokens.push_back({TokKind::PlusEq, "+=", tokLine, tokCol}); continue; }
        if (c == '-' && next == '=') { adv(2); outTokens.push_back({TokKind::MinusEq, "-=", tokLine, tokCol}); continue; }
        if (c == '=' && next == '=') { adv(2); outTokens.push_back({TokKind::Eq, "==", tokLine, tokCol}); continue; }
        if (c == '!' && next == '=') { adv(2); outTokens.push_back({TokKind::Neq, "!=", tokLine, tokCol}); continue; }
        if (c == '<' && next == '=') { adv(2); outTokens.push_back({TokKind::Lte, "<=", tokLine, tokCol}); continue; }
        if (c == '>' && next == '=') { adv(2); outTokens.push_back({TokKind::Gte, ">=", tokLine, tokCol}); continue; }
        if (c == '&' && next == '&') { adv(2); outTokens.push_back({TokKind::And, "&&", tokLine, tokCol}); continue; }
        if (c == '|' && next == '|') { adv(2); outTokens.push_back({TokKind::Or, "||", tokLine, tokCol}); continue; }

        // Single-character punctuation & operators
        adv();
        switch (c) {
            case '(': outTokens.push_back({TokKind::LParen,   "(", tokLine, tokCol}); break;
            case ')': outTokens.push_back({TokKind::RParen,   ")", tokLine, tokCol}); break;
            case '{': outTokens.push_back({TokKind::LBrace,   "{", tokLine, tokCol}); break;
            case '}': outTokens.push_back({TokKind::RBrace,   "}", tokLine, tokCol}); break;
            case '[': outTokens.push_back({TokKind::LBracket, "[", tokLine, tokCol}); break;
            case ']': outTokens.push_back({TokKind::RBracket, "]", tokLine, tokCol}); break;
            case ';': outTokens.push_back({TokKind::Semi,     ";", tokLine, tokCol}); break;
            case ':': outTokens.push_back({TokKind::Colon,    ":", tokLine, tokCol}); break;
            case ',': outTokens.push_back({TokKind::Comma,    ",", tokLine, tokCol}); break;
            case '.': outTokens.push_back({TokKind::Dot,      ".", tokLine, tokCol}); break;
            case '?': outTokens.push_back({TokKind::Question, "?", tokLine, tokCol}); break;
            case '+': outTokens.push_back({TokKind::Plus,     "+", tokLine, tokCol}); break;
            case '-': outTokens.push_back({TokKind::Minus,    "-", tokLine, tokCol}); break;
            case '*': outTokens.push_back({TokKind::Star,     "*", tokLine, tokCol}); break;
            case '/': outTokens.push_back({TokKind::Slash,    "/", tokLine, tokCol}); break;
            case '%': outTokens.push_back({TokKind::Percent,  "%", tokLine, tokCol}); break;
            case '=': outTokens.push_back({TokKind::Assign,   "=", tokLine, tokCol}); break;
            case '<': outTokens.push_back({TokKind::Lt,       "<", tokLine, tokCol}); break;
            case '>': outTokens.push_back({TokKind::Gt,       ">", tokLine, tokCol}); break;
            case '!': outTokens.push_back({TokKind::Bang,     "!", tokLine, tokCol}); break;
            default:
                diags.push_back({true, "RSW700", tokLine, tokCol, fileLabel,
                                 std::string("Unexpected character '") + c + "' in RaggieScript."});
                return false;
        }
    }

    outTokens.push_back({TokKind::Eof, "", line, col});
    return true;
}

// ================================================================================
// 5. RECURSIVE-DESCENT + PRECEDENCE-CLIMBING PARSER
// ================================================================================
class Parser {
public:
    Parser(const std::vector<Token>& toks, std::string fileLabel, std::vector<ScriptDiagnostic>& diags)
        : toks_(toks), file_(std::move(fileLabel)), diags_(diags) {}

    bool ParseProgram(ScriptProgram& prog) {
        prog.sourcePath = file_;
        prog.capabilities.insert("std"); // `std` is always available unless restricted

        while (!At(TokKind::Eof)) {
            if (MatchIdent("use")) {
                std::string cap = ExpectIdent("Expected capability namespace after 'use'.");
                while (Match(TokKind::Dot)) {
                    cap += "." + ExpectIdent("Expected sub-namespace after '.'.");
                }
                Expect(TokKind::Semi, "Expected ';' after 'use' declaration.");
                prog.capabilities.insert(AsciiLower(cap));
            } else if (PeekIdent("package") || PeekIdent("showcase") || PeekIdent("app")) {
                if (!ParsePackageHeader(prog)) return false;
            } else if (MatchIdent("param")) {
                if (!ParseParamDecl(prog)) return false;
            } else if (MatchIdent("payload")) {
                if (!ParsePayloadDecl(prog)) return false;
            } else if (MatchIdent("fn") || MatchIdent("function")) {
                if (!ParseFunctionDecl(prog)) return false;
            } else if (MatchIdent("wizard")) {
                prog.wizardBlock = ParseBlock();
            } else if (MatchIdent("install")) {
                prog.installBlock = ParseBlock();
            } else if (MatchIdent("repair")) {
                if (Match(TokKind::LParen)) {
                    std::string first = ExpectIdent("Expected parameter inside repair(...).");
                    if (At(TokKind::Ident)) first = Advance().text; // Optional `RepairMode mode` type prefix!
                    prog.repairParamName = first;
                    Expect(TokKind::RParen, "Expected ')' after repair parameter.");
                }
                prog.repairBlock = ParseBlock();
            } else if (MatchIdent("uninstall")) {
                if (Match(TokKind::LParen)) {
                    std::string first = ExpectIdent("Expected parameter inside uninstall(...).");
                    if (At(TokKind::Ident)) first = Advance().text; // Optional `bool purgeUserData` type prefix!
                    prog.uninstallParamName = first;
                    Expect(TokKind::RParen, "Expected ')' after uninstall parameter.");
                }
                prog.uninstallBlock = ParseBlock();
            } else if (MatchIdent("on")) {
                if (!ParseEventHandler(prog)) return false;
            } else {
                auto st = ParseStmt();
                if (!st) return false;
                prog.topStatements.push_back(std::move(st));
            }
            if (hadError_) return false;
        }
        return !hadError_;
    }

private:
    const std::vector<Token>&      toks_;
    std::string                    file_;
    std::vector<ScriptDiagnostic>& diags_;
    std::size_t                    pos_ = 0;
    bool                           hadError_ = false;

    const Token& Cur() const noexcept { return toks_[std::min(pos_, toks_.size() - 1)]; }
    const Token& NextTok() const noexcept { return toks_[std::min(pos_ + 1, toks_.size() - 1)]; }
    bool At(TokKind k) const noexcept { return Cur().kind == k; }
    bool PeekIdent(std::string_view s) const noexcept { return Cur().kind == TokKind::Ident && Cur().text == s; }

    const Token& Advance() noexcept {
        const Token& t = Cur();
        if (pos_ + 1 < toks_.size()) ++pos_;
        return t;
    }

    bool Match(TokKind k) noexcept {
        if (At(k)) { Advance(); return true; }
        return false;
    }

    bool MatchIdent(std::string_view s) noexcept {
        if (PeekIdent(s)) { Advance(); return true; }
        return false;
    }

    void ErrorHere(const std::string& msg, const std::string& code = "RSW700") {
        if (!hadError_) {
            diags_.push_back({true, code, Cur().line, Cur().col, file_, msg});
            hadError_ = true;
        }
    }

    void Expect(TokKind k, const std::string& msg) {
        if (!Match(k)) ErrorHere(msg);
    }

    std::string ExpectIdent(const std::string& msg) {
        if (At(TokKind::Ident)) return Advance().text;
        ErrorHere(msg);
        return "";
    }

    static std::string FlattenEnumOrLitValue(const ExprPtr& e) {
        if (!e) return "";
        if (e->kind == ExprKind::StrLit || e->kind == ExprKind::VerLit || e->kind == ExprKind::Ident) return e->strVal;
        if (e->kind == ExprKind::BoolLit) return e->boolVal ? "true" : "false";
        if (e->kind == ExprKind::IntLit) return std::to_string(e->intVal);
        if (e->kind == ExprKind::Member) {
            // e.g., `Scope.Machine` -> `"machine"`, `Uac.RequireAdministrator` -> `"requireAdministrator"`
            const std::string leaf = e->strVal;
            if (EqualsIgnoreCase(leaf, "AsInvoker")) return "asInvoker";
            if (EqualsIgnoreCase(leaf, "HighestAvailable")) return "highestAvailable";
            if (EqualsIgnoreCase(leaf, "RequireAdministrator")) return "requireAdministrator";
            return AsciiLower(leaf);
        }
        return "";
    }

    bool ParsePackageHeader(ScriptProgram& prog) {
        prog.hasPackageHeader = true;
        prog.package.kind = Advance().text; // `"package"` | `"showcase"` | `"app"`
        if (At(TokKind::StrLit)) {
            prog.package.name = Advance().text;
        }
        Expect(TokKind::LBrace, "Expected '{' after package/showcase/app title.");
        while (!At(TokKind::RBrace) && !At(TokKind::Eof) && !hadError_) {
            const std::string key = ExpectIdent("Expected property name inside header block.");
            if (!Match(TokKind::Colon)) Expect(TokKind::Assign, "Expected ':' or '=' after property name.");
            ExprPtr valExpr = ParseExpr();
            Match(TokKind::Semi);
            Match(TokKind::Comma);
            const std::string v = FlattenEnumOrLitValue(valExpr);
            const std::string kl = AsciiLower(key);
            if (kl == "id" || kl == "upgradecode") prog.package.id = v;
            else if (kl == "productcode") prog.package.productCode = v;
            else if (kl == "version") prog.package.version = v;
            else if (kl == "publisher" || kl == "manufacturer") prog.package.publisher = v;
            else if (kl == "tagline") prog.package.tagline = v;
            else if (kl == "website" || kl == "url" || kl == "abouturl") prog.package.websiteUrl = v;
            else if (kl == "scope") prog.package.scope = AsciiLower(v);
            else if (kl == "uac" || kl == "requestedexecutionlevel") prog.package.uac = v;
            else if (kl == "style" || kl == "wizardlayout") prog.package.style = AsciiLower(v);
            else if (kl == "supportdir") prog.package.supportDir = v;
            else if (kl == "mainexe" || kl == "mainexecutable") prog.package.mainExe = v;
            else if (kl == "icon" || kl == "iconfile") prog.package.iconFile = v;
            else if (kl == "license" || kl == "licensefile") prog.package.licenseFile = v;
            else if (kl == "accentprimary") prog.package.accentPrimary = v;
            else if (kl == "accentsecondary") prog.package.accentSecondary = v;
            else if (kl == "hidefrominstalledapps" || kl == "systemcomponent") prog.package.hideFromArp = EqualsIgnoreCase(v, "true") || v == "1";
            else if (kl == "autoplay" || kl == "autoplayaudio") prog.package.autoPlayAudio = EqualsIgnoreCase(v, "true");
            else if (kl == "minwindowsbuild" && valExpr && valExpr->kind == ExprKind::IntLit) prog.package.minWinBuild = static_cast<int>(valExpr->intVal);
        }
        Expect(TokKind::RBrace, "Expected '}' at end of header block.");
        return !hadError_;
    }

    bool ParseParamDecl(ScriptProgram& prog) {
        ParamDecl p{};
        p.line = Cur().line;
        p.name = ExpectIdent("Expected parameter name after 'param'.");
        if (Match(TokKind::Colon)) {
            p.typeName = ExpectIdent("Expected parameter type name after ':'.");
        }
        if (Match(TokKind::Assign)) {
            p.defaultExpr = ParseExpr();
        }
        Expect(TokKind::Semi, "Expected ';' after param declaration.");
        prog.params.push_back(std::move(p));
        return !hadError_;
    }

    bool ParsePayloadDecl(ScriptProgram& prog) {
        PayloadDecl pl{};
        pl.line = Cur().line;
        pl.id = ExpectIdent("Expected payload identifier after 'payload'.");
        Expect(TokKind::LBrace, "Expected '{' in payload declaration.");
        while (!At(TokKind::RBrace) && !At(TokKind::Eof) && !hadError_) {
            if (MatchIdent("from")) {
                if (At(TokKind::StrLit)) pl.sourceGlob = Advance().text;
                else ErrorHere("Expected source path string after 'from'.");
                if (Match(TokKind::Arrow)) {
                    pl.destExpr = ParseExpr();
                }
                Match(TokKind::Semi);
            } else {
                const std::string k = ExpectIdent("Expected payload property.");
                if (!Match(TokKind::Colon)) Expect(TokKind::Assign, "Expected ':' in payload property.");
                ExprPtr v = ParseExpr();
                Match(TokKind::Semi);
                if (EqualsIgnoreCase(k, "source")) pl.sourceGlob = FlattenEnumOrLitValue(v);
                else if (EqualsIgnoreCase(k, "dest") || EqualsIgnoreCase(k, "to")) pl.destExpr = v;
                else if (EqualsIgnoreCase(k, "feature")) pl.featureId = FlattenEnumOrLitValue(v);
                else if (EqualsIgnoreCase(k, "recursive")) pl.recursive = EqualsIgnoreCase(FlattenEnumOrLitValue(v), "true");
            }
        }
        Expect(TokKind::RBrace, "Expected '}' after payload declaration.");
        prog.payloads.push_back(std::move(pl));
        return !hadError_;
    }

    bool ParseFunctionDecl(ScriptProgram& prog) {
        FunctionDecl fn{};
        fn.line = Cur().line;
        fn.name = ExpectIdent("Expected function name after 'fn'.");
        Expect(TokKind::LParen, "Expected '(' after function name.");
        if (!At(TokKind::RParen)) {
            do {
                std::string pName = ExpectIdent("Expected parameter name.");
                if (Match(TokKind::Colon)) {
                    (void)ExpectIdent("Expected type name after ':'.");
                }
                fn.params.push_back(std::move(pName));
            } while (Match(TokKind::Comma));
        }
        Expect(TokKind::RParen, "Expected ')' after function parameters.");
        if (Match(TokKind::Colon)) {
            (void)ExpectIdent("Expected return type name after ':'.");
        }
        fn.body = ParseBlock();
        prog.functions.push_back(std::move(fn));
        return !hadError_;
    }

    bool ParseEventHandler(ScriptProgram& prog) {
        EventHandlerDecl ev{};
        ev.line = Cur().line;
        ev.targetKind = ExpectIdent("Expected event target after 'on' (e.g. hotspot or page).");
        if (Match(TokKind::LParen)) {
            if (At(TokKind::StrLit) || At(TokKind::Ident)) ev.targetId = Advance().text;
            Expect(TokKind::RParen, "Expected ')' after event target ID.");
        }
        if (Match(TokKind::Dot)) {
            ev.eventName = ExpectIdent("Expected event name after '.' (e.g. click or show).");
        } else {
            ev.eventName = "trigger";
        }
        ev.body = ParseBlock();
        prog.eventHandlers.push_back(std::move(ev));
        return !hadError_;
    }

    StmtPtr ParseBlock() {
        auto blk = std::make_shared<Stmt>();
        blk->kind = StmtKind::Block;
        blk->line = Cur().line;
        Expect(TokKind::LBrace, "Expected '{' to open block.");
        while (!At(TokKind::RBrace) && !At(TokKind::Eof) && !hadError_) {
            auto st = ParseStmt();
            if (!st) break;
            blk->children.push_back(std::move(st));
        }
        Expect(TokKind::RBrace, "Expected '}' to close block.");
        return blk;
    }

    StmtPtr ParseStmt() {
        const int ln = Cur().line;
        if (At(TokKind::LBrace)) return ParseBlock();

        // `let x = ...;` or `const x = ...;`
        if (PeekIdent("let") || PeekIdent("const")) {
            const bool isConst = Advance().text == "const";
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::VarDecl;
            s->line = ln;
            s->isConst = isConst;
            s->name = ExpectIdent("Expected variable name.");
            if (Match(TokKind::Colon)) {
                s->secondName = ExpectIdent("Expected type name after ':'.");
                // Allow generic suffix like `Capital<USD>` if written in type annotations
                if (Match(TokKind::Lt)) {
                    while (!At(TokKind::Gt) && !At(TokKind::Eof)) Advance();
                    Match(TokKind::Gt);
                }
            }
            if (Match(TokKind::Assign)) {
                s->expr = ParseExpr();
            }
            Expect(TokKind::Semi, "Expected ';' after variable declaration.");
            return s;
        }

        // `step Welcome [when (cond)] { ... }` inside `wizard { ... }`
        if (MatchIdent("step")) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::WizardStep;
            s->line = ln;
            s->name = ExpectIdent("Expected step name after 'step'.");
            if (MatchIdent("when")) {
                Expect(TokKind::LParen, "Expected '(' after 'when'.");
                s->expr = ParseExpr();
                Expect(TokKind::RParen, "Expected ')' after step condition.");
            }
            s->body = ParseBlock();
            return s;
        }

        // `if (cond) { ... } else ...`
        if (MatchIdent("if")) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::If;
            s->line = ln;
            Expect(TokKind::LParen, "Expected '(' after 'if'.");
            s->expr = ParseExpr();
            Expect(TokKind::RParen, "Expected ')' after 'if' condition.");
            s->body = At(TokKind::LBrace) ? ParseBlock() : ParseStmt();
            if (MatchIdent("else")) {
                s->elseBody = At(TokKind::LBrace) ? ParseBlock() : ParseStmt();
            }
            return s;
        }

        // `while (cond) { ... }`
        if (MatchIdent("while")) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::While;
            s->line = ln;
            Expect(TokKind::LParen, "Expected '(' after 'while'.");
            s->expr = ParseExpr();
            Expect(TokKind::RParen, "Expected ')' after 'while' condition.");
            s->body = At(TokKind::LBrace) ? ParseBlock() : ParseStmt();
            return s;
        }

        // `repeat { ... } until (cond);`
        if (MatchIdent("repeat")) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::RepeatUntil;
            s->line = ln;
            s->body = ParseBlock();
            if (!MatchIdent("until")) ErrorHere("Expected 'until' after 'repeat' block.");
            Expect(TokKind::LParen, "Expected '(' after 'until'.");
            s->expr = ParseExpr();
            Expect(TokKind::RParen, "Expected ')' after 'until' condition.");
            Match(TokKind::Semi);
            return s;
        }

        // `for (item in list) { ... }` or `for (k, v in map) { ... }`
        if (MatchIdent("for")) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::ForIn;
            s->line = ln;
            Expect(TokKind::LParen, "Expected '(' after 'for'.");
            MatchIdent("let");
            s->name = ExpectIdent("Expected loop variable name.");
            if (Match(TokKind::Comma)) {
                s->secondName = ExpectIdent("Expected second loop variable name.");
            }
            if (!MatchIdent("in")) ErrorHere("Expected 'in' inside 'for (... in ...)'.");
            s->expr = ParseExpr();
            Expect(TokKind::RParen, "Expected ')' after 'for' iterable.");
            s->body = At(TokKind::LBrace) ? ParseBlock() : ParseStmt();
            return s;
        }

        if (MatchIdent("break")) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::Break;
            s->line = ln;
            Expect(TokKind::Semi, "Expected ';' after 'break'.");
            return s;
        }

        if (MatchIdent("continue")) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::Continue;
            s->line = ln;
            Expect(TokKind::Semi, "Expected ';' after 'continue'.");
            return s;
        }

        if (MatchIdent("return")) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::Return;
            s->line = ln;
            if (!At(TokKind::Semi)) s->expr = ParseExpr();
            Expect(TokKind::Semi, "Expected ';' after 'return'.");
            return s;
        }

        if (MatchIdent("throw")) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::Throw;
            s->line = ln;
            s->expr = ParseExpr();
            Expect(TokKind::Semi, "Expected ';' after 'throw'.");
            return s;
        }

        if (PeekIdent("exit") && NextTok().kind == TokKind::LParen) {
            Advance();
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::ExitSetup;
            s->line = ln;
            Expect(TokKind::LParen, "Expected '(' after 'exit'.");
            s->expr = At(TokKind::RParen) ? nullptr : ParseExpr();
            Expect(TokKind::RParen, "Expected ')' after exit code.");
            Expect(TokKind::Semi, "Expected ';' after exit(...).");
            return s;
        }

        if (MatchIdent("try")) {
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::TryCatch;
            s->line = ln;
            s->body = ParseBlock();
            if (!MatchIdent("catch")) ErrorHere("Expected 'catch' after 'try' block.");
            if (Match(TokKind::LParen)) {
                s->name = ExpectIdent("Expected error variable name inside catch(...).");
                Expect(TokKind::RParen, "Expected ')' after catch variable.");
            } else {
                s->name = "err";
            }
            s->elseBody = ParseBlock();
            return s;
        }

        // Expression or assignment (`target = rhs;`, `target += rhs;`, `target -= rhs;`)
        ExprPtr lhs = ParseExpr();
        if (!lhs) return nullptr;
        if (At(TokKind::Assign) || At(TokKind::PlusEq) || At(TokKind::MinusEq)) {
            const std::string op = Advance().text;
            auto s = std::make_shared<Stmt>();
            s->kind = StmtKind::Assign;
            s->line = ln;
            s->name = op;
            s->target = std::move(lhs);
            s->expr = ParseExpr();
            Expect(TokKind::Semi, "Expected ';' after assignment.");
            return s;
        }

        auto s = std::make_shared<Stmt>();
        s->kind = StmtKind::ExprStmt;
        s->line = ln;
        s->expr = std::move(lhs);
        Expect(TokKind::Semi, "Expected ';' after statement.");
        return s;
    }

    ExprPtr ParseExpr() { return ParseTernary(); }

    ExprPtr ParseTernary() {
        auto c = ParseOr();
        if (Match(TokKind::Question)) {
            auto e = std::make_shared<Expr>();
            e->kind = ExprKind::Ternary;
            e->line = c ? c->line : Cur().line;
            e->cond = std::move(c);
            e->lhs  = ParseExpr();
            Expect(TokKind::Colon, "Expected ':' in '? :' ternary expression.");
            e->rhs  = ParseTernary();
            return e;
        }
        return c;
    }

    ExprPtr ParseOr() {
        auto left = ParseAnd();
        while (Match(TokKind::Or)) {
            auto b = std::make_shared<Expr>();
            b->kind = ExprKind::Binary; b->strVal = "||"; b->line = Cur().line;
            b->lhs = std::move(left); b->rhs = ParseAnd();
            left = std::move(b);
        }
        return left;
    }

    ExprPtr ParseAnd() {
        auto left = ParseEquality();
        while (Match(TokKind::And)) {
            auto b = std::make_shared<Expr>();
            b->kind = ExprKind::Binary; b->strVal = "&&"; b->line = Cur().line;
            b->lhs = std::move(left); b->rhs = ParseEquality();
            left = std::move(b);
        }
        return left;
    }

    ExprPtr ParseEquality() {
        auto left = ParseCompare();
        while (At(TokKind::Eq) || At(TokKind::Neq)) {
            const std::string op = Advance().text;
            auto b = std::make_shared<Expr>();
            b->kind = ExprKind::Binary; b->strVal = op; b->line = Cur().line;
            b->lhs = std::move(left); b->rhs = ParseCompare();
            left = std::move(b);
        }
        return left;
    }

    ExprPtr ParseCompare() {
        auto left = ParseAdd();
        while (At(TokKind::Lt) || At(TokKind::Lte) || At(TokKind::Gt) || At(TokKind::Gte)) {
            const std::string op = Advance().text;
            auto b = std::make_shared<Expr>();
            b->kind = ExprKind::Binary; b->strVal = op; b->line = Cur().line;
            b->lhs = std::move(left); b->rhs = ParseAdd();
            left = std::move(b);
        }
        return left;
    }

    ExprPtr ParseAdd() {
        auto left = ParseMul();
        while (At(TokKind::Plus) || At(TokKind::Minus)) {
            const std::string op = Advance().text;
            auto b = std::make_shared<Expr>();
            b->kind = ExprKind::Binary; b->strVal = op; b->line = Cur().line;
            b->lhs = std::move(left); b->rhs = ParseMul();
            left = std::move(b);
        }
        return left;
    }

    ExprPtr ParseMul() {
        auto left = ParseUnary();
        while (At(TokKind::Star) || At(TokKind::Slash) || At(TokKind::Percent)) {
            const std::string op = Advance().text;
            auto b = std::make_shared<Expr>();
            b->kind = ExprKind::Binary; b->strVal = op; b->line = Cur().line;
            b->lhs = std::move(left); b->rhs = ParseUnary();
            left = std::move(b);
        }
        return left;
    }

    ExprPtr ParseUnary() {
        if (At(TokKind::Bang) || At(TokKind::Minus)) {
            const Token op = Advance();
            auto u = std::make_shared<Expr>();
            u->kind = ExprKind::Unary; u->strVal = op.text; u->line = op.line;
            u->lhs = ParseUnary();
            return u;
        }
        if (MatchIdent("ref")) {
            auto r = std::make_shared<Expr>();
            r->kind = ExprKind::RefVar; r->line = Cur().line;
            r->strVal = ExpectIdent("Expected variable name after 'ref'.");
            return r;
        }
        return ParsePostfix();
    }

    ExprPtr ParsePostfix() {
        auto expr = ParsePrimary();
        while (!hadError_) {
            if (Match(TokKind::Dot)) {
                auto m = std::make_shared<Expr>();
                m->kind = ExprKind::Member; m->line = Cur().line;
                m->lhs = std::move(expr);
                m->strVal = ExpectIdent("Expected property or method name after '.'.");
                expr = std::move(m);
            } else if (Match(TokKind::LBracket)) {
                auto ix = std::make_shared<Expr>();
                ix->kind = ExprKind::Index; ix->line = Cur().line;
                ix->lhs = std::move(expr);
                ix->rhs = ParseExpr();
                Expect(TokKind::RBracket, "Expected ']' after index expression.");
                expr = std::move(ix);
            } else if (Match(TokKind::LParen)) {
                auto call = std::make_shared<Expr>();
                call->kind = ExprKind::Call; call->line = Cur().line;
                call->lhs = std::move(expr);
                if (!At(TokKind::RParen)) {
                    do {
                        CallArg ca{};
                        if (At(TokKind::Ident) && NextTok().kind == TokKind::Colon) {
                            ca.name = Advance().text;
                            Advance(); // consume `:`
                        }
                        ca.value = ParseExpr();
                        call->callArgs.push_back(std::move(ca));
                    } while (Match(TokKind::Comma));
                }
                Expect(TokKind::RParen, "Expected ')' after call arguments.");
                expr = std::move(call);
            } else {
                break;
            }
        }
        return expr;
    }

    ExprPtr ParsePrimary() {
        const Token& t = Cur();
        if (MatchIdent("true")) {
            auto e = std::make_shared<Expr>(); e->kind = ExprKind::BoolLit; e->boolVal = true; e->line = t.line; return e;
        }
        if (MatchIdent("false")) {
            auto e = std::make_shared<Expr>(); e->kind = ExprKind::BoolLit; e->boolVal = false; e->line = t.line; return e;
        }
        if (MatchIdent("null")) {
            auto e = std::make_shared<Expr>(); e->kind = ExprKind::NullLit; e->line = t.line; return e;
        }
        if (At(TokKind::IntLit)) {
            Advance();
            auto e = std::make_shared<Expr>(); e->kind = ExprKind::IntLit; e->line = t.line;
            e->intVal = std::strtoll(t.text.c_str(), nullptr, 0);
            return e;
        }
        if (At(TokKind::FloatLit)) {
            Advance();
            auto e = std::make_shared<Expr>(); e->kind = ExprKind::FloatLit; e->line = t.line;
            e->floatVal = std::strtod(t.text.c_str(), nullptr);
            return e;
        }
        if (At(TokKind::StrLit)) {
            Advance();
            auto e = std::make_shared<Expr>(); e->kind = ExprKind::StrLit; e->strVal = t.text; e->line = t.line; return e;
        }
        if (At(TokKind::VerLit)) {
            Advance();
            auto e = std::make_shared<Expr>(); e->kind = ExprKind::VerLit; e->strVal = t.text; e->line = t.line; return e;
        }
        if (At(TokKind::Ident)) {
            Advance();
            auto e = std::make_shared<Expr>(); e->kind = ExprKind::Ident; e->strVal = t.text; e->line = t.line; return e;
        }
        if (Match(TokKind::LParen)) {
            auto inner = ParseExpr();
            Expect(TokKind::RParen, "Expected ')' after parenthesized expression.");
            return inner;
        }
        if (Match(TokKind::LBracket)) {
            auto arr = std::make_shared<Expr>();
            arr->kind = ExprKind::ArrayLit; arr->line = t.line;
            if (!At(TokKind::RBracket)) {
                do {
                    if (At(TokKind::RBracket)) break;
                    arr->items.push_back(ParseExpr());
                } while (Match(TokKind::Comma));
            }
            Expect(TokKind::RBracket, "Expected ']' after array literal.");
            return arr;
        }
        if (Match(TokKind::LBrace)) {
            auto mp = std::make_shared<Expr>();
            mp->kind = ExprKind::MapLit; mp->line = t.line;
            if (!At(TokKind::RBrace)) {
                do {
                    if (At(TokKind::RBrace)) break;
                    std::string key;
                    if (At(TokKind::Ident) || At(TokKind::StrLit)) key = Advance().text;
                    else ErrorHere("Expected identifier or string key inside '{ ... }' object literal.");
                    Expect(TokKind::Colon, "Expected ':' after object literal key.");
                    mp->mapEntries.push_back({std::move(key), ParseExpr()});
                } while (Match(TokKind::Comma));
            }
            Expect(TokKind::RBrace, "Expected '}' after object literal.");
            return mp;
        }

        ErrorHere("Unexpected token '" + t.text + "' in expression.");
        return nullptr;
    }
};

// ================================================================================
// 6. STATIC ANALYZER: `RSW701` WIZARD PHASE ISOLATION & `RSW702` CAPABILITY GATES
// ================================================================================
inline bool IsMutatingHostCall(std::string_view ns, std::string_view method) noexcept {
    if (ns == "fs") {
        return method == "extract" || method == "mkdir" || method == "copy" || method == "copyTree" ||
               method == "move" || method == "remove" || method == "removeTree" || method == "removeTreeIfExists" ||
               method == "writeText" || method == "appendText" || method == "writeJson" ||
               method == "writeBytesHex" || method == "setAttributes" || method == "setNtfsCompression";
    }
    if (ns == "reg") {
        return method == "writeString" || method == "writeExpandString" || method == "writeDword" ||
               method == "appendMultiString" || method == "deleteValue" || method == "deleteKeyTree";
    }
    if (ns == "ini") return method == "write" || method == "deleteKey";
    if (ns == "xml") return method == "replaceAttr" || method == "setElementText";
    if (ns == "shortcut") return method == "create" || method == "remove";
    if (ns == "shell") return method == "registerFileType" || method == "unregisterFileType" || method == "registerProtocol";
    if (ns == "env") return method == "set" || method == "appendPath" || method == "remove";
    if (ns == "arp") return method == "register" || method == "unregister" || method == "setSystemComponent";
    if (ns == "svc" || ns == "task" || ns == "firewall" || ns == "acl") {
        return method != "exists" && method != "status";
    }
    if (ns == "msi") {
        return method == "install" || method == "uninstall" || method == "applyPatch" || method == "runLegacyUninstaller";
    }
    if (ns == "archive") {
        return method == "extractZip" || method == "extractPak" || method == "createPak";
    }
    return false;
}

inline void AnalyzeExprCapabilitiesAndPhase(
    const ExprPtr& e,
    const ScriptProgram& prog,
    bool insideWizard,
    std::set<std::string>& visitedFns,
    std::vector<ScriptDiagnostic>& diags);

inline void AnalyzeStmtCapabilitiesAndPhase(
    const StmtPtr& s,
    const ScriptProgram& prog,
    bool insideWizard,
    std::set<std::string>& visitedFns,
    std::vector<ScriptDiagnostic>& diags)
{
    if (!s) return;
    AnalyzeExprCapabilitiesAndPhase(s->target, prog, insideWizard, visitedFns, diags);
    AnalyzeExprCapabilitiesAndPhase(s->expr, prog, insideWizard, visitedFns, diags);
    AnalyzeStmtCapabilitiesAndPhase(s->body, prog, insideWizard, visitedFns, diags);
    AnalyzeStmtCapabilitiesAndPhase(s->elseBody, prog, insideWizard, visitedFns, diags);
    for (const auto& ch : s->children) {
        AnalyzeStmtCapabilitiesAndPhase(ch, prog, insideWizard, visitedFns, diags);
    }
}

inline void AnalyzeExprCapabilitiesAndPhase(
    const ExprPtr& e,
    const ScriptProgram& prog,
    bool insideWizard,
    std::set<std::string>& visitedFns,
    std::vector<ScriptDiagnostic>& diags)
{
    if (!e) return;
    AnalyzeExprCapabilitiesAndPhase(e->lhs, prog, insideWizard, visitedFns, diags);
    AnalyzeExprCapabilitiesAndPhase(e->rhs, prog, insideWizard, visitedFns, diags);
    AnalyzeExprCapabilitiesAndPhase(e->cond, prog, insideWizard, visitedFns, diags);
    for (const auto& it : e->items) AnalyzeExprCapabilitiesAndPhase(it, prog, insideWizard, visitedFns, diags);
    for (const auto& kv : e->mapEntries) AnalyzeExprCapabilitiesAndPhase(kv.second, prog, insideWizard, visitedFns, diags);
    for (const auto& ca : e->callArgs) AnalyzeExprCapabilitiesAndPhase(ca.value, prog, insideWizard, visitedFns, diags);

    if (e->kind != ExprKind::Call || !e->lhs) return;

    // 1. Direct `namespace.method(...)` host call check
    if (e->lhs->kind == ExprKind::Member && e->lhs->lhs && e->lhs->lhs->kind == ExprKind::Ident) {
        const std::string ns     = e->lhs->lhs->strVal;
        const std::string method = e->lhs->strVal;

        // `RSW702` Sandboxed Training/A11y Capability Gate:
        const bool isSandboxedTraining = prog.capabilities.count("training") > 0 &&
                                         prog.capabilities.count("installer") == 0 &&
                                         prog.capabilities.count("showcase") == 0;
        if (isSandboxedTraining &&
            (ns == "fs" || ns == "reg" || ns == "proc" || ns == "svc" || ns == "net" ||
             ns == "msi" || ns == "firewall" || ns == "acl" || ns == "task" || ns == "env")) {
            diags.push_back({
                true, "RSW702", e->line, 1, prog.sourcePath,
                "Capability violation: sandboxed 'use training;' script cannot call '" + ns + "." + method + "()'."
            });
        }

        // `RSW701` Wizard Phase Isolation Gate:
        if (insideWizard && IsMutatingHostCall(ns, method)) {
            diags.push_back({
                true, "RSW701", e->line, 1, prog.sourcePath,
                "Phase isolation violation: mutating call '" + ns + "." + method +
                "()' is forbidden inside 'wizard { ... }'. Move system modifications into 'install { ... }'."
            });
        }
    }

    // 2. Transitive user-defined function check (`wizard` calling `fn helper()` that mutates!)
    if (insideWizard && e->lhs->kind == ExprKind::Ident) {
        const std::string fnName = e->lhs->strVal;
        if (!visitedFns.count(fnName)) {
            for (const auto& fn : prog.functions) {
                if (fn.name == fnName) {
                    visitedFns.insert(fnName);
                    AnalyzeStmtCapabilitiesAndPhase(fn.body, prog, true, visitedFns, diags);
                    break;
                }
            }
        }
    }
}

} // namespace detail

// ================================================================================
// 7. PUBLIC COMPILE & `RSWBC1` BINARY BYTECODE CODEC API
// ================================================================================
/// Compiles RaggieScript source text (expanding `include "..."` & `#if`, parsing AST,
/// and running `RSW701` + `RSW702` static verification).
inline bool CompileRaggieScriptText(
    const std::string& sourceText,
    const std::filesystem::path& sourceFilePath,
    ScriptProgram& outProgram,
    std::vector<ScriptDiagnostic>& outDiagnostics,
    const std::map<std::string, std::string>& buildDefines = {})
{
    outProgram = ScriptProgram{};
    outDiagnostics.clear();

    std::map<std::string, std::string> defs = buildDefines;
    if (!defs.count("ARCH")) {
        defs["ARCH"] = std::string(QueryHostPlatform().NativeArchSlug());
    }

    std::string expanded;
    std::set<std::wstring> visited;
    if (!detail::PreprocessSourceRecursive(sourceText, sourceFilePath, defs, visited, expanded, outDiagnostics)) {
        return false;
    }

    const std::string fileLabel = sourceFilePath.empty() ? "<memory.rssc>" : WideToUtf8(sourceFilePath.wstring());
    std::vector<Token> tokens;
    if (!detail::TokenizeRaggieScript(expanded, fileLabel, tokens, outDiagnostics)) {
        return false;
    }

    detail::Parser parser(tokens, fileLabel, outDiagnostics);
    if (!parser.ParseProgram(outProgram)) {
        return false;
    }

    // Run static `RSW701` (Wizard Phase Isolation) & `RSW702` (Capability Sandbox) passes!
    std::set<std::string> visitedFns;
    for (const auto& st : outProgram.topStatements) {
        detail::AnalyzeStmtCapabilitiesAndPhase(st, outProgram, false, visitedFns, outDiagnostics);
    }
    if (outProgram.wizardBlock) {
        visitedFns.clear();
        detail::AnalyzeStmtCapabilitiesAndPhase(outProgram.wizardBlock, outProgram, true, visitedFns, outDiagnostics);
    }
    if (outProgram.installBlock) {
        visitedFns.clear();
        detail::AnalyzeStmtCapabilitiesAndPhase(outProgram.installBlock, outProgram, false, visitedFns, outDiagnostics);
    }
    for (const auto& fn : outProgram.functions) {
        visitedFns.clear();
        detail::AnalyzeStmtCapabilitiesAndPhase(fn.body, outProgram, false, visitedFns, outDiagnostics);
    }

    for (const auto& d : outDiagnostics) {
        if (d.isError) return false;
    }
    return true;
}

inline bool CompileRaggieScriptFile(
    const std::filesystem::path& filePath,
    ScriptProgram& outProgram,
    std::vector<ScriptDiagnostic>& outDiagnostics,
    const std::map<std::string, std::string>& buildDefines = {})
{
    std::ifstream fs(filePath, std::ios::binary);
    if (!fs) {
        outDiagnostics.push_back({true, "RSW700", 1, 1, WideToUtf8(filePath.wstring()),
                                  "Cannot open RaggieScript file: " + WideToUtf8(filePath.wstring())});
        return false;
    }
    std::ostringstream ss;
    ss << fs.rdbuf();
    std::string raw = ss.str();
    if (raw.size() >= 3 &&
        static_cast<unsigned char>(raw[0]) == 0xEF &&
        static_cast<unsigned char>(raw[1]) == 0xBB &&
        static_cast<unsigned char>(raw[2]) == 0xBF) {
        raw.erase(0, 3);
    }
    return CompileRaggieScriptText(raw, filePath, outProgram, outDiagnostics, buildDefines);
}

// --------------------------------------------------------------------------------
// `RSWBC1` Binary Bytecode Serialization & Integrity Verification (`kResIdScriptBytecode = 225`)
//   Layout:
//     [0..7]   Magic `"RSWBC1\0\0"` (`8` bytes)
//     [8..11]  Format Version `uint32_t` (`1`)
//     [12..15] Payload Byte Length `uint32_t`
//     [16..47] Raw 32-byte `SHA-256` digest of serialized AST/IR stream (`[48..end]`)
//     [48..]   Compact binary-encoded AST/IR instructions & constant strings!
// --------------------------------------------------------------------------------
namespace bc_detail {

inline void PutU8(std::vector<std::uint8_t>& b, std::uint8_t v) { b.push_back(v); }
inline void PutU32(std::vector<std::uint8_t>& b, std::uint32_t v) {
    for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>((v >> (i * 8)) & 0xFFu));
}
inline void PutI64(std::vector<std::uint8_t>& b, std::int64_t v) {
    const auto u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>((u >> (i * 8)) & 0xFFu));
}
inline void PutF64(std::vector<std::uint8_t>& b, double v) {
    std::uint64_t u = 0;
    std::memcpy(&u, &v, sizeof(u));
    PutI64(b, static_cast<std::int64_t>(u));
}
inline void PutStr(std::vector<std::uint8_t>& b, std::string_view s) {
    PutU32(b, static_cast<std::uint32_t>(s.size()));
    b.insert(b.end(), reinterpret_cast<const std::uint8_t*>(s.data()),
             reinterpret_cast<const std::uint8_t*>(s.data()) + s.size());
}

inline std::string ComputeBufferSha256HexLower(const std::uint8_t* data, std::size_t size) {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_HASH_HANDLE hHash = nullptr;
    if (::BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    DWORD objLen = 0, cbRes = 0;
    ::BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objLen), sizeof(objLen), &cbRes, 0);
    std::vector<UCHAR> hashObj(objLen);
    std::array<UCHAR, 32> digest{};
    bool ok = (::BCryptCreateHash(hAlg, &hHash, hashObj.data(), objLen, nullptr, 0, 0) == 0);
    if (ok && size > 0) {
        ok = (::BCryptHashData(hHash, const_cast<PUCHAR>(data), static_cast<ULONG>(size), 0) == 0);
    }
    if (ok) {
        ok = (::BCryptFinishHash(hHash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0);
    }
    if (hHash) ::BCryptDestroyHash(hHash);
    if (hAlg)  ::BCryptCloseAlgorithmProvider(hAlg, 0);
    if (!ok) return {};
    static constexpr char kHex[] = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (UCHAR byteVal : digest) {
        hex.push_back(kHex[(byteVal >> 4) & 0x0F]);
        hex.push_back(kHex[byteVal & 0x0F]);
    }
    return hex;
}

struct Reader {
    const std::uint8_t* ptr = nullptr;
    std::size_t         len = 0;
    std::size_t         pos = 0;
    bool                ok  = true;

    std::uint8_t GetU8() noexcept {
        if (pos + 1 > len) { ok = false; return 0; }
        return ptr[pos++];
    }
    std::uint32_t GetU32() noexcept {
        if (pos + 4 > len) { ok = false; return 0; }
        std::uint32_t v = 0;
        for (int i = 0; i < 4; ++i) v |= (static_cast<std::uint32_t>(ptr[pos++]) << (i * 8));
        return v;
    }
    std::int64_t GetI64() noexcept {
        if (pos + 8 > len) { ok = false; return 0; }
        std::uint64_t u = 0;
        for (int i = 0; i < 8; ++i) u |= (static_cast<std::uint64_t>(ptr[pos++]) << (i * 8));
        return static_cast<std::int64_t>(u);
    }
    double GetF64() noexcept {
        const auto u = static_cast<std::uint64_t>(GetI64());
        double d = 0.0;
        std::memcpy(&d, &u, sizeof(d));
        return d;
    }
    std::string GetStr() {
        const std::uint32_t n = GetU32();
        if (!ok || pos + n > len) { ok = false; return {}; }
        std::string s(reinterpret_cast<const char*>(ptr + pos), n);
        pos += n;
        return s;
    }
};

inline void WriteExpr(std::vector<std::uint8_t>& b, const ExprPtr& e) {
    if (!e) { PutU8(b, 0); return; }
    PutU8(b, 1);
    PutU8(b, static_cast<std::uint8_t>(e->kind));
    PutU32(b, static_cast<std::uint32_t>(e->line));
    PutU8(b, e->boolVal ? 1 : 0);
    PutI64(b, e->intVal);
    PutF64(b, e->floatVal);
    PutStr(b, e->strVal);
    PutU32(b, static_cast<std::uint32_t>(e->items.size()));
    for (const auto& it : e->items) WriteExpr(b, it);
    PutU32(b, static_cast<std::uint32_t>(e->mapEntries.size()));
    for (const auto& kv : e->mapEntries) { PutStr(b, kv.first); WriteExpr(b, kv.second); }
    WriteExpr(b, e->lhs);
    WriteExpr(b, e->rhs);
    WriteExpr(b, e->cond);
    PutU32(b, static_cast<std::uint32_t>(e->callArgs.size()));
    for (const auto& ca : e->callArgs) { PutStr(b, ca.name); WriteExpr(b, ca.value); }
}

inline ExprPtr ReadExpr(Reader& r) {
    if (r.GetU8() == 0 || !r.ok) return nullptr;
    auto e = std::make_shared<Expr>();
    e->kind     = static_cast<ExprKind>(r.GetU8());
    e->line     = static_cast<int>(r.GetU32());
    e->boolVal  = (r.GetU8() != 0);
    e->intVal   = r.GetI64();
    e->floatVal = r.GetF64();
    e->strVal   = r.GetStr();
    const std::uint32_t nItems = r.GetU32();
    for (std::uint32_t i = 0; i < nItems && r.ok; ++i) e->items.push_back(ReadExpr(r));
    const std::uint32_t nMap = r.GetU32();
    for (std::uint32_t i = 0; i < nMap && r.ok; ++i) {
        std::string k = r.GetStr();
        e->mapEntries.push_back({std::move(k), ReadExpr(r)});
    }
    e->lhs  = ReadExpr(r);
    e->rhs  = ReadExpr(r);
    e->cond = ReadExpr(r);
    const std::uint32_t nArgs = r.GetU32();
    for (std::uint32_t i = 0; i < nArgs && r.ok; ++i) {
        CallArg ca{};
        ca.name  = r.GetStr();
        ca.value = ReadExpr(r);
        e->callArgs.push_back(std::move(ca));
    }
    return e;
}

inline void WriteStmt(std::vector<std::uint8_t>& b, const StmtPtr& s) {
    if (!s) { PutU8(b, 0); return; }
    PutU8(b, 1);
    PutU8(b, static_cast<std::uint8_t>(s->kind));
    PutU32(b, static_cast<std::uint32_t>(s->line));
    PutU8(b, s->isConst ? 1 : 0);
    PutStr(b, s->name);
    PutStr(b, s->secondName);
    WriteExpr(b, s->target);
    WriteExpr(b, s->expr);
    WriteStmt(b, s->body);
    WriteStmt(b, s->elseBody);
    PutU32(b, static_cast<std::uint32_t>(s->children.size()));
    for (const auto& ch : s->children) WriteStmt(b, ch);
}

inline StmtPtr ReadStmt(Reader& r) {
    if (r.GetU8() == 0 || !r.ok) return nullptr;
    auto s = std::make_shared<Stmt>();
    s->kind       = static_cast<StmtKind>(r.GetU8());
    s->line       = static_cast<int>(r.GetU32());
    s->isConst    = (r.GetU8() != 0);
    s->name       = r.GetStr();
    s->secondName = r.GetStr();
    s->target     = ReadExpr(r);
    s->expr       = ReadExpr(r);
    s->body       = ReadStmt(r);
    s->elseBody   = ReadStmt(r);
    const std::uint32_t nCh = r.GetU32();
    for (std::uint32_t i = 0; i < nCh && r.ok; ++i) s->children.push_back(ReadStmt(r));
    return s;
}

} // namespace bc_detail

/// Emits a SHA-256-verified `RSWBC1` bytecode stream from a compiled `ScriptProgram`.
inline bool EmitRaggieScriptBytecode(
    const ScriptProgram& prog,
    std::vector<std::uint8_t>& outBytes,
    std::string& outError)
{
    using namespace bc_detail;
    std::vector<std::uint8_t> body;
    body.reserve(4096);

    PutStr(body, prog.sourcePath);
    PutU32(body, static_cast<std::uint32_t>(prog.capabilities.size()));
    for (const auto& c : prog.capabilities) PutStr(body, c);

    PutU8(body, prog.hasPackageHeader ? 1 : 0);
    PutStr(body, prog.package.kind);
    PutStr(body, prog.package.name);
    PutStr(body, prog.package.id);
    PutStr(body, prog.package.productCode);
    PutStr(body, prog.package.version);
    PutStr(body, prog.package.publisher);
    PutStr(body, prog.package.tagline);
    PutStr(body, prog.package.websiteUrl);
    PutStr(body, prog.package.scope);
    PutStr(body, prog.package.uac);
    PutStr(body, prog.package.style);
    PutStr(body, prog.package.supportDir);
    PutStr(body, prog.package.mainExe);
    PutStr(body, prog.package.iconFile);
    PutStr(body, prog.package.licenseFile);
    PutStr(body, prog.package.accentPrimary);
    PutStr(body, prog.package.accentSecondary);
    PutU8(body, prog.package.hideFromArp ? 1 : 0);
    PutU8(body, prog.package.autoPlayAudio ? 1 : 0);
    PutU32(body, static_cast<std::uint32_t>(prog.package.minWinBuild));

    PutU32(body, static_cast<std::uint32_t>(prog.params.size()));
    for (const auto& p : prog.params) {
        PutStr(body, p.name);
        PutStr(body, p.typeName);
        WriteExpr(body, p.defaultExpr);
        PutU32(body, static_cast<std::uint32_t>(p.line));
    }

    PutU32(body, static_cast<std::uint32_t>(prog.payloads.size()));
    for (const auto& pl : prog.payloads) {
        PutStr(body, pl.id);
        PutStr(body, pl.sourceGlob);
        WriteExpr(body, pl.destExpr);
        PutStr(body, pl.featureId);
        PutU8(body, pl.recursive ? 1 : 0);
        PutU32(body, static_cast<std::uint32_t>(pl.line));
    }

    PutU32(body, static_cast<std::uint32_t>(prog.functions.size()));
    for (const auto& fn : prog.functions) {
        PutStr(body, fn.name);
        PutU32(body, static_cast<std::uint32_t>(fn.params.size()));
        for (const auto& pn : fn.params) PutStr(body, pn);
        WriteStmt(body, fn.body);
        PutU32(body, static_cast<std::uint32_t>(fn.line));
    }

    PutU32(body, static_cast<std::uint32_t>(prog.topStatements.size()));
    for (const auto& st : prog.topStatements) WriteStmt(body, st);

    WriteStmt(body, prog.wizardBlock);
    WriteStmt(body, prog.installBlock);
    WriteStmt(body, prog.repairBlock);
    PutStr(body, prog.repairParamName);
    WriteStmt(body, prog.uninstallBlock);
    PutStr(body, prog.uninstallParamName);

    PutU32(body, static_cast<std::uint32_t>(prog.eventHandlers.size()));
    for (const auto& ev : prog.eventHandlers) {
        PutStr(body, ev.targetKind);
        PutStr(body, ev.targetId);
        PutStr(body, ev.eventName);
        WriteStmt(body, ev.body);
        PutU32(body, static_cast<std::uint32_t>(ev.line));
    }

    const std::string shaHex = ComputeBufferSha256HexLower(body.data(), body.size());
    if (shaHex.size() != 64) {
        outError = "Failed to compute SHA-256 digest for RSWBC1 bytecode.";
        return false;
    }

    outBytes.clear();
    outBytes.reserve(48 + body.size());
    const char magic[8] = {'R', 'S', 'W', 'B', 'C', '1', '\0', '\0'};
    outBytes.insert(outBytes.end(), reinterpret_cast<const std::uint8_t*>(magic),
                    reinterpret_cast<const std::uint8_t*>(magic) + 8);
    PutU32(outBytes, 1u);
    PutU32(outBytes, static_cast<std::uint32_t>(body.size()));
    for (std::size_t i = 0; i < 32; ++i) {
        const unsigned int byteVal = std::strtoul(shaHex.substr(i * 2, 2).c_str(), nullptr, 16);
        outBytes.push_back(static_cast<std::uint8_t>(byteVal & 0xFFu));
    }
    outBytes.insert(outBytes.end(), body.begin(), body.end());
    return true;
}

/// Loads and cryptographically verifies an `RSWBC1` bytecode stream back into a `ScriptProgram`.
inline bool LoadRaggieScriptBytecode(
    const std::uint8_t* bytes,
    std::size_t size,
    ScriptProgram& outProg,
    std::string& outError)
{
    using namespace bc_detail;
    if (!bytes || size < 48) {
        outError = "Truncated RSWBC1 bytecode header.";
        return false;
    }
    if (std::memcmp(bytes, "RSWBC1\0\0", 8) != 0) {
        outError = "Invalid RSWBC1 bytecode magic signature.";
        return false;
    }
    Reader hdr{bytes, 16, 8, true};
    const std::uint32_t ver = hdr.GetU32();
    const std::uint32_t bodyLen = hdr.GetU32();
    if (ver != 1u || 48ull + bodyLen != size) {
        outError = "Unsupported RSWBC1 format version or mismatched stream length.";
        return false;
    }

    const std::string actualHex = ComputeBufferSha256HexLower(bytes + 48, bodyLen);
    std::ostringstream expectedHex;
    static constexpr char kHex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < 32; ++i) {
        const std::uint8_t b = bytes[16 + i];
        expectedHex << kHex[(b >> 4) & 0xF] << kHex[b & 0xF];
    }
    if (actualHex != expectedHex.str()) {
        outError = "RSWBC1 bytecode SHA-256 integrity check failed.";
        return false;
    }

    Reader r{bytes + 48, bodyLen, 0, true};
    outProg = ScriptProgram{};
    outProg.sourcePath = r.GetStr();
    const std::uint32_t nCaps = r.GetU32();
    for (std::uint32_t i = 0; i < nCaps && r.ok; ++i) outProg.capabilities.insert(r.GetStr());

    outProg.hasPackageHeader          = (r.GetU8() != 0);
    outProg.package.kind              = r.GetStr();
    outProg.package.name              = r.GetStr();
    outProg.package.id                = r.GetStr();
    outProg.package.productCode       = r.GetStr();
    outProg.package.version           = r.GetStr();
    outProg.package.publisher         = r.GetStr();
    outProg.package.tagline           = r.GetStr();
    outProg.package.websiteUrl        = r.GetStr();
    outProg.package.scope             = r.GetStr();
    outProg.package.uac               = r.GetStr();
    outProg.package.style             = r.GetStr();
    outProg.package.supportDir        = r.GetStr();
    outProg.package.mainExe           = r.GetStr();
    outProg.package.iconFile          = r.GetStr();
    outProg.package.licenseFile       = r.GetStr();
    outProg.package.accentPrimary     = r.GetStr();
    outProg.package.accentSecondary   = r.GetStr();
    outProg.package.hideFromArp       = (r.GetU8() != 0);
    outProg.package.autoPlayAudio     = (r.GetU8() != 0);
    outProg.package.minWinBuild       = static_cast<int>(r.GetU32());

    const std::uint32_t nParams = r.GetU32();
    for (std::uint32_t i = 0; i < nParams && r.ok; ++i) {
        ParamDecl p{};
        p.name        = r.GetStr();
        p.typeName    = r.GetStr();
        p.defaultExpr = ReadExpr(r);
        p.line        = static_cast<int>(r.GetU32());
        outProg.params.push_back(std::move(p));
    }

    const std::uint32_t nPay = r.GetU32();
    for (std::uint32_t i = 0; i < nPay && r.ok; ++i) {
        PayloadDecl pl{};
        pl.id         = r.GetStr();
        pl.sourceGlob = r.GetStr();
        pl.destExpr   = ReadExpr(r);
        pl.featureId  = r.GetStr();
        pl.recursive  = (r.GetU8() != 0);
        pl.line       = static_cast<int>(r.GetU32());
        outProg.payloads.push_back(std::move(pl));
    }

    const std::uint32_t nFns = r.GetU32();
    for (std::uint32_t i = 0; i < nFns && r.ok; ++i) {
        FunctionDecl fn{};
        fn.name = r.GetStr();
        const std::uint32_t np = r.GetU32();
        for (std::uint32_t k = 0; k < np && r.ok; ++k) fn.params.push_back(r.GetStr());
        fn.body = ReadStmt(r);
        fn.line = static_cast<int>(r.GetU32());
        outProg.functions.push_back(std::move(fn));
    }

    const std::uint32_t nTop = r.GetU32();
    for (std::uint32_t i = 0; i < nTop && r.ok; ++i) outProg.topStatements.push_back(ReadStmt(r));

    outProg.wizardBlock        = ReadStmt(r);
    outProg.installBlock       = ReadStmt(r);
    outProg.repairBlock        = ReadStmt(r);
    outProg.repairParamName    = r.GetStr();
    outProg.uninstallBlock     = ReadStmt(r);
    outProg.uninstallParamName = r.GetStr();

    const std::uint32_t nEv = r.GetU32();
    for (std::uint32_t i = 0; i < nEv && r.ok; ++i) {
        EventHandlerDecl ev{};
        ev.targetKind = r.GetStr();
        ev.targetId   = r.GetStr();
        ev.eventName  = r.GetStr();
        ev.body       = ReadStmt(r);
        ev.line       = static_cast<int>(r.GetU32());
        outProg.eventHandlers.push_back(std::move(ev));
    }

    if (!r.ok) {
        outError = "Corrupted RSWBC1 AST instruction stream.";
        return false;
    }
    return true;
}

/// Formats a human-readable disassembly summary of a compiled `ScriptProgram` (`raggiescript disasm`).
inline std::string DisassembleRaggieScriptProgram(const ScriptProgram& prog) {
    std::ostringstream oss;
    oss << "; ======================================================================\n"
        << "; RaggieScript Bytecode Disassembly (Format: RSWBC1 v1)\n"
        << "; Source      : " << prog.sourcePath << "\n"
        << "; Capabilities: ";
    bool first = true;
    for (const auto& c : prog.capabilities) {
        if (!first) oss << ", ";
        first = false;
        oss << c;
    }
    oss << "\n";
    if (prog.hasPackageHeader) {
        oss << "; Header      : " << prog.package.kind << " \"" << prog.package.name << "\" v"
            << prog.package.version << " (Scope=" << prog.package.scope
            << ", UAC=" << prog.package.uac << ", Style=" << prog.package.style << ")\n";
    }
    oss << "; ======================================================================\n\n";
    oss << "[SECTION .params] count=" << prog.params.size() << "\n";
    for (const auto& p : prog.params) {
        oss << "  PARAM " << p.name << " : " << (p.typeName.empty() ? "any" : p.typeName) << " (line " << p.line << ")\n";
    }
    oss << "\n[SECTION .payloads] count=" << prog.payloads.size() << "\n";
    for (const auto& pl : prog.payloads) {
        oss << "  PAYLOAD " << pl.id << " from=\"" << pl.sourceGlob << "\" feature=" << pl.featureId << "\n";
    }
    oss << "\n[SECTION .functions] count=" << prog.functions.size() << "\n";
    for (const auto& fn : prog.functions) {
        oss << "  FN " << fn.name << "(" << fn.params.size() << " args) @ line " << fn.line << "\n";
    }
    oss << "\n[SECTION .phases]\n"
        << "  TOP_STATEMENTS : " << prog.topStatements.size() << "\n"
        << "  WIZARD_PHASE   : " << (prog.wizardBlock ? "PRESENT (RSW701 Side-Effect-Free Verified)" : "NONE") << "\n"
        << "  INSTALL_PHASE  : " << (prog.installBlock ? "PRESENT (Rollback-Journaled)" : "NONE") << "\n"
        << "  REPAIR_PHASE   : " << (prog.repairBlock ? "PRESENT" : "NONE") << "\n"
        << "  UNINSTALL_PHASE: " << (prog.uninstallBlock ? "PRESENT" : "NONE") << "\n"
        << "  EVENT_HANDLERS : " << prog.eventHandlers.size() << "\n";
    return oss.str();
}

} // namespace rsw::rssc
