// ================================================================================
// FILE: src/common/RswRaggieScriptVm.hpp
// PROJECT: RaggieSoft Installer Studio & RaggieScript Engine ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com/raggiescript  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What does this C++20 header do?
//     While `RswRaggieScript.hpp` compiles plain-text `.rssc` source files into AST
//     trees and `RSWBC1` binary bytecode streams (verifying `RSW701` phase isolation
//     and `RSW702` capability sandboxes at compile time), THIS header (`RswRaggieScriptVm.hpp`)
//     is the **Zero-Dependency Native Win32 Execution Engine & Standard Library**!
//
//   Five Major Architectural Highlights Inside This File:
//     1. **Rich Runtime Value & Type System (`rsw::rssc::Value`)**:
//        - Supports `null`, `bool`, 64-bit `int`, `float`, UTF-8 `string`,
//          OS-normalized `path` (supporting `path(sys.programFiles) / "RaggieSoft"`!),
//          4-part `version` (`SemVer`, supporting `<, <=, >, >=, ==, !=`), dynamic
//          `array` (`[...]`), object `map` (`{ key: val }`), and `ref` out-parameters!
//     2. **Atomic Transactional Rollback Ledger (`ScriptRollbackLedger`)**:
//        - Every mutating call inside `install { ... }` (`fs.extract`, `fs.writeText`,
//          `fs.copy`, `fs.mkdir`, `reg.write*`, `ini.write`, `shortcut.create`,
//          `shell.registerFileType`, `env.appendPath`, `svc.install`, `task.*`,
//          `firewall.*`, and `arp.register`) is recorded in a live `.rsw-rollback.jnl`
//          ledger backed by `.rswbak` file backups and previous-value snapshots!
//        - If an installation throws an error, hits a simulated or real disk fault,
//          or is canceled mid-flight, `RollbackLifo()` undoes every partial file,
//          directory, shortcut, registry value, INI entry, PATH edit, and service
//          in strict reverse (`LIFO`) order!
//        - On commit, `arp.register(...)` (or automatic `package` registration)
//          writes `<InstallDir>\Uninstall.exe` + `<InstallDir>\Uninstall.rswm` so
//          Windows *Installed apps* removes 100% of installed state cleanly!
//     3. **Stateless 64-Bit, `WOW6432Node`, & Dual-View Registry API (`reg.*`)**:
//        - Eliminates global `64-bit mode` state bugs! Every `reg.*` call accepts an
//          explicit stateless `view` (`"native64"` default, `"wow32"`, or `"both"`),
//          and transparently honors `RSW_TEST_REG_STORE_DIR` during sandboxed tests.
//     4. **Three-Token Process Launcher (`proc.run` -> `"current"`, `"elevated"`, `"desktop"`)**:
//        - `"elevated"` launches runtime prerequisites via `ShellExecuteExW(L"runas")`
//          and captures their exact Win32 exit code.
//        - `"desktop"` duplicates the interactive user's medium-integrity `explorer.exe`
//          shell token (`GetShellWindow` -> `DuplicateTokenEx` -> `CreateProcessWithTokenW`)
//          so launching an app at the end of an elevated installer NEVER breaks
//          Explorer drag-and-drop or pollutes `%LOCALAPPDATA%` with Admin-owned files!
//     5. **Zero-`MessageBox` Native Windows 11 `TaskDialogIndirect` UI (`ui.*`)**:
//        - Renders modern `comctl32.dll` v6 Task Dialogs with Command Links, UAC
//          elevation shields (`TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE`), collapsible
//          technical details drawers, and verification checkboxes—while auto-resolving
//          cleanly without blocking in `/s` silent mode!
// ================================================================================

#pragma once

#include "RswCryptoAndPe.hpp"
#include "RswLzmsCompression.hpp"
#include "RswNativeAndSuiteEngine.hpp"
#include "RswPlatform.hpp"
#include "RswPrerequisites.hpp"
#include "RswRaggieScript.hpp"
#include "RswVersion.hpp"
#include "RswZipAndPak.hpp"

#include <aclapi.h>
#include <commctrl.h>
#include <msi.h>
#include <regex>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <tlhelp32.h>
#include <winioctl.h>
#include <winsvc.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "msi.lib")

namespace rsw::rssc {

// ================================================================================
// 1. RUNTIME VALUE MODEL (`Value`) & LEXICAL SCOPE CHAIN (`Scope`)
// ================================================================================
enum class ValKind : std::uint8_t {
    Null = 0,
    Bool,
    Int,
    Float,
    Str,
    Path,
    Ver,
    Array,
    Map,
    Ref
};

struct Scope;

struct Value {
    ValKind                                            kind     = ValKind::Null;
    bool                                               boolVal  = false;
    std::int64_t                                       intVal   = 0;
    double                                             floatVal = 0.0;
    std::string                                        strVal;
    SemVer                                             verVal{};
    std::shared_ptr<std::vector<Value>>                arrVal;
    std::shared_ptr<std::map<std::string, Value>>      mapVal;
    Scope*                                             refScope = nullptr;
    std::string                                        refName;

    static Value Null() noexcept { return Value{}; }
    static Value FromBool(bool b) noexcept { Value v; v.kind = ValKind::Bool; v.boolVal = b; return v; }
    static Value FromInt(std::int64_t n) noexcept { Value v; v.kind = ValKind::Int; v.intVal = n; return v; }
    static Value FromFloat(double d) noexcept { Value v; v.kind = ValKind::Float; v.floatVal = d; return v; }
    static Value FromStr(std::string s) { Value v; v.kind = ValKind::Str; v.strVal = std::move(s); return v; }
    static Value FromPath(const std::filesystem::path& p) {
        Value v;
        v.kind   = ValKind::Path;
        v.strVal = WideToUtf8(p.lexically_normal().wstring());
        return v;
    }
    static Value FromPathUtf8(std::string_view s) {
        return FromPath(std::filesystem::path(Utf8ToWide(s)));
    }
    static Value FromVer(SemVer sv) noexcept { Value v; v.kind = ValKind::Ver; v.verVal = sv; return v; }
    static Value FromArray(std::vector<Value> items = {}) {
        Value v;
        v.kind   = ValKind::Array;
        v.arrVal = std::make_shared<std::vector<Value>>(std::move(items));
        return v;
    }
    static Value FromMap(std::map<std::string, Value> kv = {}) {
        Value v;
        v.kind   = ValKind::Map;
        v.mapVal = std::make_shared<std::map<std::string, Value>>(std::move(kv));
        return v;
    }
    static Value FromRef(Scope* sc, std::string name) {
        Value v;
        v.kind     = ValKind::Ref;
        v.refScope = sc;
        v.refName  = std::move(name);
        return v;
    }

    std::string_view TypeName() const noexcept {
        switch (kind) {
            case ValKind::Null:  return "null";
            case ValKind::Bool:  return "bool";
            case ValKind::Int:   return "int";
            case ValKind::Float: return "float";
            case ValKind::Str:   return "string";
            case ValKind::Path:  return "path";
            case ValKind::Ver:   return "version";
            case ValKind::Array: return "array";
            case ValKind::Map:   return "map";
            case ValKind::Ref:   return "ref";
        }
        return "null";
    }

    bool AsBool() const noexcept {
        switch (kind) {
            case ValKind::Null:  return false;
            case ValKind::Bool:  return boolVal;
            case ValKind::Int:   return intVal != 0;
            case ValKind::Float: return floatVal != 0.0;
            case ValKind::Str:
            case ValKind::Path:  return !strVal.empty() && !EqualsIgnoreCase(strVal, "false") && strVal != "0";
            case ValKind::Ver:   return verVal.Compare(SemVer{}) != 0;
            case ValKind::Array: return arrVal && !arrVal->empty();
            case ValKind::Map:   return mapVal && !mapVal->empty();
            case ValKind::Ref:   return refScope != nullptr;
        }
        return false;
    }

    std::int64_t AsInt() const noexcept {
        switch (kind) {
            case ValKind::Bool:  return boolVal ? 1 : 0;
            case ValKind::Int:   return intVal;
            case ValKind::Float: return static_cast<std::int64_t>(floatVal);
            case ValKind::Str:
            case ValKind::Path:  return std::strtoll(strVal.c_str(), nullptr, 0);
            case ValKind::Ver:   return verVal.parts[0];
            default:             return 0;
        }
    }

    double AsFloat() const noexcept {
        switch (kind) {
            case ValKind::Bool:  return boolVal ? 1.0 : 0.0;
            case ValKind::Int:   return static_cast<double>(intVal);
            case ValKind::Float: return floatVal;
            case ValKind::Str:
            case ValKind::Path:  return std::strtod(strVal.c_str(), nullptr);
            default:             return 0.0;
        }
    }

    SemVer AsVersion() const noexcept {
        if (kind == ValKind::Ver) return verVal;
        if (kind == ValKind::Int) {
            SemVer v{};
            v.parts[0] = static_cast<int>(intVal);
            return v;
        }
        return SemVer::Parse(strVal);
    }

    std::string AsString() const {
        switch (kind) {
            case ValKind::Null:  return "null";
            case ValKind::Bool:  return boolVal ? "true" : "false";
            case ValKind::Int:   return std::to_string(intVal);
            case ValKind::Float: {
                std::ostringstream oss;
                oss << floatVal;
                return oss.str();
            }
            case ValKind::Str:
            case ValKind::Path:  return strVal;
            case ValKind::Ver:   return verVal.ToString();
            case ValKind::Array: {
                std::ostringstream oss;
                oss << "[";
                if (arrVal) {
                    for (std::size_t i = 0; i < arrVal->size(); ++i) {
                        if (i > 0) oss << ", ";
                        oss << (*arrVal)[i].AsString();
                    }
                }
                oss << "]";
                return oss.str();
            }
            case ValKind::Map: {
                std::ostringstream oss;
                oss << "{";
                if (mapVal) {
                    bool first = true;
                    for (const auto& [k, v] : *mapVal) {
                        if (!first) oss << ", ";
                        first = false;
                        oss << k << ": " << v.AsString();
                    }
                }
                oss << "}";
                return oss.str();
            }
            case ValKind::Ref:
                return "<ref:" + refName + ">";
        }
        return {};
    }

    std::filesystem::path AsPath() const {
        return std::filesystem::path(Utf8ToWide(AsString())).lexically_normal();
    }
};

struct Scope {
    struct Slot {
        Value value;
        bool  isConst = false;
    };
    Scope*                      parent = nullptr;
    std::map<std::string, Slot> vars;

    explicit Scope(Scope* p = nullptr) : parent(p) {}

    void Declare(const std::string& name, Value v, bool isConst = false) {
        vars[name] = Slot{std::move(v), isConst};
    }

    bool TryGet(const std::string& name, Value& outVal) const {
        auto it = vars.find(name);
        if (it != vars.end()) {
            outVal = it->second.value;
            return true;
        }
        return parent ? parent->TryGet(name, outVal) : false;
    }

    bool Assign(const std::string& name, Value v, std::string& outErr) {
        auto it = vars.find(name);
        if (it != vars.end()) {
            if (it->second.isConst) {
                outErr = "Cannot reassign 'const' binding '" + name + "'.";
                return false;
            }
            it->second.value = std::move(v);
            return true;
        }
        if (parent) {
            return parent->Assign(name, std::move(v), outErr);
        }
        outErr = "Assignment to undeclared variable '" + name + "'. Declare it with 'let " + name + " = ...;' first.";
        return false;
    }
};

inline Value DerefIfNeeded(const Value& v) {
    if (v.kind == ValKind::Ref && v.refScope) {
        Value actual;
        if (v.refScope->TryGet(v.refName, actual)) return actual;
    }
    return v;
}

inline bool WriteThroughRef(const Value& maybeRef, Value newVal, std::string& outErr) {
    if (maybeRef.kind != ValKind::Ref || !maybeRef.refScope) return false;
    return maybeRef.refScope->Assign(maybeRef.refName, std::move(newVal), outErr);
}

// ================================================================================
// 2. SIMPLE JSON PARSER & SERIALIZER (`json.parse`, `json.stringify`, `fs.readJson`)
// ================================================================================
namespace json_detail {

struct JsonReader {
    std::string_view s;
    std::size_t      pos = 0;

    void SkipWs() noexcept {
        while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' || s[pos] == '\r' || s[pos] == '\n')) ++pos;
    }

    bool ParseStringRaw(std::string& out) {
        SkipWs();
        if (pos >= s.size() || s[pos] != '"') return false;
        ++pos;
        out.clear();
        while (pos < s.size()) {
            const char c = s[pos++];
            if (c == '"') return true;
            if (c == '\\' && pos < s.size()) {
                const char esc = s[pos++];
                switch (esc) {
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case '\\': out.push_back('\\'); break;
                    case '"':  out.push_back('"');  break;
                    case '/':  out.push_back('/');  break;
                    default:   out.push_back(esc);  break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    Value ParseValue() {
        SkipWs();
        if (pos >= s.size()) return Value::Null();
        const char c = s[pos];
        if (c == '"') {
            std::string str;
            return ParseStringRaw(str) ? Value::FromStr(std::move(str)) : Value::Null();
        }
        if (c == '{') {
            ++pos;
            std::map<std::string, Value> mp;
            SkipWs();
            if (pos < s.size() && s[pos] == '}') { ++pos; return Value::FromMap(std::move(mp)); }
            while (pos < s.size()) {
                std::string key;
                if (!ParseStringRaw(key)) break;
                SkipWs();
                if (pos < s.size() && s[pos] == ':') ++pos;
                mp[std::move(key)] = ParseValue();
                SkipWs();
                if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
                if (pos < s.size() && s[pos] == '}') { ++pos; break; }
                break;
            }
            return Value::FromMap(std::move(mp));
        }
        if (c == '[') {
            ++pos;
            std::vector<Value> arr;
            SkipWs();
            if (pos < s.size() && s[pos] == ']') { ++pos; return Value::FromArray(std::move(arr)); }
            while (pos < s.size()) {
                arr.push_back(ParseValue());
                SkipWs();
                if (pos < s.size() && s[pos] == ',') { ++pos; continue; }
                if (pos < s.size() && s[pos] == ']') { ++pos; break; }
                break;
            }
            return Value::FromArray(std::move(arr));
        }
        if (s.substr(pos, 4) == "true")  { pos += 4; return Value::FromBool(true); }
        if (s.substr(pos, 5) == "false") { pos += 5; return Value::FromBool(false); }
        if (s.substr(pos, 4) == "null")  { pos += 4; return Value::Null(); }

        // Number
        const std::size_t start = pos;
        bool isFloat = false;
        if (pos < s.size() && (s[pos] == '-' || s[pos] == '+')) ++pos;
        while (pos < s.size()) {
            const char nc = s[pos];
            if (nc >= '0' && nc <= '9') { ++pos; }
            else if (nc == '.' || nc == 'e' || nc == 'E' || nc == '+' || nc == '-') { isFloat = true; ++pos; }
            else break;
        }
        const std::string numTxt(s.substr(start, pos - start));
        if (numTxt.empty()) return Value::Null();
        return isFloat
            ? Value::FromFloat(std::strtod(numTxt.c_str(), nullptr))
            : Value::FromInt(std::strtoll(numTxt.c_str(), nullptr, 10));
    }
};

inline void EscapeJsonStr(std::ostringstream& oss, std::string_view sv) {
    oss << '"';
    for (unsigned char c : sv) {
        switch (c) {
            case '"':  oss << "\\\""; break;
            case '\\': oss << "\\\\"; break;
            case '\n': oss << "\\n";  break;
            case '\r': oss << "\\r";  break;
            case '\t': oss << "\\t";  break;
            default:
                if (c < 0x20) {
                    char hex[8]{};
                    std::snprintf(hex, sizeof(hex), "\\u%04x", static_cast<unsigned int>(c));
                    oss << hex;
                } else {
                    oss << static_cast<char>(c);
                }
                break;
        }
    }
    oss << '"';
}

inline void StringifyJsonRec(std::ostringstream& oss, const Value& rawV, int indent, int depth) {
    const Value v = DerefIfNeeded(rawV);
    const auto pad = [&](int d) {
        if (indent > 0) oss << std::string(static_cast<std::size_t>(d * indent), ' ');
    };
    switch (v.kind) {
        case ValKind::Null:  oss << "null"; break;
        case ValKind::Bool:  oss << (v.boolVal ? "true" : "false"); break;
        case ValKind::Int:   oss << v.intVal; break;
        case ValKind::Float: oss << v.floatVal; break;
        case ValKind::Str:
        case ValKind::Path:  EscapeJsonStr(oss, v.strVal); break;
        case ValKind::Ver:   EscapeJsonStr(oss, v.verVal.ToString()); break;
        case ValKind::Array: {
            oss << "[";
            if (v.arrVal && !v.arrVal->empty()) {
                for (std::size_t i = 0; i < v.arrVal->size(); ++i) {
                    if (i > 0) oss << ",";
                    if (indent > 0) { oss << "\n"; pad(depth + 1); }
                    StringifyJsonRec(oss, (*v.arrVal)[i], indent, depth + 1);
                }
                if (indent > 0) { oss << "\n"; pad(depth); }
            }
            oss << "]";
            break;
        }
        case ValKind::Map: {
            oss << "{";
            if (v.mapVal && !v.mapVal->empty()) {
                bool first = true;
                for (const auto& [k, val] : *v.mapVal) {
                    if (!first) oss << ",";
                    first = false;
                    if (indent > 0) { oss << "\n"; pad(depth + 1); }
                    EscapeJsonStr(oss, k);
                    oss << (indent > 0 ? ": " : ":");
                    StringifyJsonRec(oss, val, indent, depth + 1);
                }
                if (indent > 0) { oss << "\n"; pad(depth); }
            }
            oss << "}";
            break;
        }
        case ValKind::Ref:
            oss << "null";
            break;
    }
}

} // namespace json_detail

// ================================================================================
// 3. ATOMIC TRANSACTIONAL ROLLBACK LEDGER (`ScriptRollbackLedger`)
// ================================================================================
struct ScriptRollbackLedger {
    bool active = false;
    std::filesystem::path journalFilePath;
    int mutatingOpCount = 0;
    int faultAfterOps   = 0; // Non-zero in fault-injection tests to verify LIFO rollback!

    struct RegSnap {
        HKEY         hRoot = HKEY_CURRENT_USER;
        std::string  rootCode;
        std::wstring subKey;
        std::wstring valName;
        REGSAM       wowFlag    = KEY_WOW64_64KEY;
        bool         createdKey = false;
        bool         hadOldVal  = false;
        DWORD        oldType    = REG_SZ;
        std::vector<BYTE> oldBytes;
        std::string  virtOldUtf8;
    };

    struct IniSnap {
        std::filesystem::path file;
        std::wstring          section;
        std::wstring          key;
        bool                  hadOld = false;
        std::wstring          oldVal;
    };

    std::vector<std::filesystem::path>                                    createdDirs;
    std::vector<std::filesystem::path>                                    newlyWrittenFiles;
    std::vector<std::pair<std::filesystem::path, std::filesystem::path>>  backedUpFiles;
    std::vector<std::filesystem::path>                                    createdShortcuts;
    std::vector<RegSnap>                                                  regUndoStack;
    std::vector<std::pair<std::string, std::wstring>>                     uninstallRegKeys;
    std::vector<IniSnap>                                                  iniUndoStack;
    std::vector<std::wstring>                                             createdServices;
    std::vector<std::wstring>                                             createdTasks;
    std::vector<std::wstring>                                             createdFirewallRules;

    void Begin(const std::filesystem::path& targetDir, int simulateFaultAfter = 0) {
        active          = true;
        mutatingOpCount = 0;
        faultAfterOps   = simulateFaultAfter;
        createdDirs.clear();
        newlyWrittenFiles.clear();
        backedUpFiles.clear();
        createdShortcuts.clear();
        regUndoStack.clear();
        uninstallRegKeys.clear();
        iniUndoStack.clear();
        createdServices.clear();
        createdTasks.clear();
        createdFirewallRules.clear();

        if (!targetDir.empty()) {
            std::error_code ec;
            EnsureDirectoryTracked(targetDir);
            journalFilePath = targetDir / L".rsw-rollback.jnl";
            std::ofstream jnl(journalFilePath, std::ios::binary | std::ios::trunc);
            if (jnl) {
                jnl << "RSW_RAGGIESCRIPT_ROLLBACK_JOURNAL_V1\nTargetDir="
                    << WideToUtf8(targetDir.wstring()) << "\n";
            }
        }
    }

    bool CheckFaultTrigger(std::string& outErr) {
        ++mutatingOpCount;
        if (active && faultAfterOps > 0 && mutatingOpCount >= faultAfterOps) {
            outErr = "Simulated transactional fault after " + std::to_string(mutatingOpCount) +
                     " mutating RaggieScript operation(s).";
            return false;
        }
        return true;
    }

    void AppendJournalLine(std::string_view opTag, std::string_view targetDesc) {
        if (!active || journalFilePath.empty()) return;
        std::ofstream jnl(journalFilePath, std::ios::binary | std::ios::app);
        if (jnl) {
            jnl << opTag << "|" << targetDesc << "\n";
        }
    }

    bool EnsureDirectoryTracked(const std::filesystem::path& dirPath) {
        if (dirPath.empty()) return true;
        std::error_code ec;
        std::vector<std::filesystem::path> missing;
        std::filesystem::path cur = dirPath.lexically_normal();
        while (!cur.empty() && !std::filesystem::exists(cur, ec)) {
            missing.push_back(cur);
            const auto par = cur.parent_path();
            if (par == cur) break;
            cur = par;
        }
        std::reverse(missing.begin(), missing.end());
        for (const auto& d : missing) {
            if (!std::filesystem::create_directory(d, ec) && !std::filesystem::exists(d, ec)) {
                return false;
            }
            if (active) {
                createdDirs.push_back(d);
                AppendJournalLine("MKDIR", WideToUtf8(d.wstring()));
            }
        }
        return true;
    }

    bool PrepareFileWriteBackup(const std::filesystem::path& dstPath) {
        const auto norm = dstPath.lexically_normal();
        if (!EnsureDirectoryTracked(norm.parent_path())) return false;
        if (!active) return true;

        std::error_code ec;
        if (std::filesystem::is_regular_file(norm, ec)) {
            const auto bak = std::filesystem::path(
                norm.wstring() + L".rswbak." + std::to_wstring(::GetCurrentProcessId()) +
                L"." + std::to_wstring(backedUpFiles.size() + 1));
            std::filesystem::remove(bak, ec);
            ec.clear();
            std::filesystem::rename(norm, bak, ec);
            if (!ec) {
                backedUpFiles.push_back({norm, bak});
                AppendJournalLine("BACKUP_FILE", WideToUtf8(norm.wstring()));
            }
        } else {
            newlyWrittenFiles.push_back(norm);
            AppendJournalLine("WRITE_FILE", WideToUtf8(norm.wstring()));
        }
        return true;
    }

    void Commit() noexcept {
        std::error_code ec;
        for (const auto& [origP, bakP] : backedUpFiles) {
            (void)origP;
            std::filesystem::remove(bakP, ec);
        }
        backedUpFiles.clear();
        if (!journalFilePath.empty()) {
            std::filesystem::remove(journalFilePath, ec);
        }
        active = false;
    }

    void RollbackLifo() noexcept {
        if (!active) return;
        std::error_code ec;

        // 1. Remove created firewall rules
        for (auto it = createdFirewallRules.rbegin(); it != createdFirewallRules.rend(); ++it) {
            std::wstring cmd = L"netsh.exe advfirewall firewall delete rule name=\"" + *it + L"\"";
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                ::WaitForSingleObject(pi.hProcess, 10000);
                ::CloseHandle(pi.hThread);
                ::CloseHandle(pi.hProcess);
            }
        }

        // 2. Remove created scheduled tasks
        for (auto it = createdTasks.rbegin(); it != createdTasks.rend(); ++it) {
            std::wstring cmd = L"schtasks.exe /Delete /TN \"" + *it + L"\" /F";
            STARTUPINFOW si{sizeof(si)};
            PROCESS_INFORMATION pi{};
            if (::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
                ::WaitForSingleObject(pi.hProcess, 10000);
                ::CloseHandle(pi.hThread);
                ::CloseHandle(pi.hProcess);
            }
        }

        // 3. Stop & delete newly installed Windows Services
        if (!createdServices.empty()) {
            if (SC_HANDLE hScm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS)) {
                for (auto it = createdServices.rbegin(); it != createdServices.rend(); ++it) {
                    if (SC_HANDLE hSvc = ::OpenServiceW(hScm, it->c_str(), SERVICE_STOP | DELETE)) {
                        SERVICE_STATUS ss{};
                        ::ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
                        ::DeleteService(hSvc);
                        ::CloseServiceHandle(hSvc);
                    }
                }
                ::CloseServiceHandle(hScm);
            }
        }

        // 4. Restore INI mutations
        for (auto it = iniUndoStack.rbegin(); it != iniUndoStack.rend(); ++it) {
            ::WritePrivateProfileStringW(
                it->section.c_str(),
                it->key.c_str(),
                it->hadOld ? it->oldVal.c_str() : nullptr,
                it->file.wstring().c_str());
        }

        // 5. Undo Registry mutations (restoring previous values or deleting newly created keys/values)
        const std::filesystem::path virtReg = native_detail::GetVirtualRegistryStoreRoot();
        for (auto it = regUndoStack.rbegin(); it != regUndoStack.rend(); ++it) {
            if (!virtReg.empty()) {
                if (it->createdKey) {
                    native_detail::VirtualRegDeleteTree(virtReg, it->rootCode, it->subKey);
                } else if (it->hadOldVal) {
                    bool dummy = false;
                    native_detail::VirtualRegWriteValue(virtReg, it->rootCode, it->subKey, it->valName, it->virtOldUtf8, dummy);
                } else {
                    native_detail::VirtualRegDeleteValue(virtReg, it->rootCode, it->subKey, it->valName);
                }
                continue;
            }
            if (it->createdKey) {
                ::RegDeleteKeyExW(it->hRoot, it->subKey.c_str(), it->wowFlag, 0);
                ::RegDeleteTreeW(it->hRoot, it->subKey.c_str());
            } else {
                HKEY hk = nullptr;
                if (::RegOpenKeyExW(it->hRoot, it->subKey.c_str(), 0, KEY_SET_VALUE | it->wowFlag, &hk) == ERROR_SUCCESS) {
                    if (it->hadOldVal) {
                        ::RegSetValueExW(
                            hk,
                            it->valName.empty() ? nullptr : it->valName.c_str(),
                            0,
                            it->oldType,
                            it->oldBytes.data(),
                            static_cast<DWORD>(it->oldBytes.size()));
                    } else {
                        ::RegDeleteValueW(hk, it->valName.empty() ? nullptr : it->valName.c_str());
                    }
                    ::RegCloseKey(hk);
                }
            }
        }

        // 6. Remove created `.lnk` shortcuts
        for (auto it = createdShortcuts.rbegin(); it != createdShortcuts.rend(); ++it) {
            std::filesystem::remove(*it, ec);
        }

        // 7. Delete newly written files & restore `.rswbak` overwritten files!
        if (!journalFilePath.empty()) {
            std::filesystem::remove(journalFilePath, ec);
        }
        for (auto it = newlyWrittenFiles.rbegin(); it != newlyWrittenFiles.rend(); ++it) {
            std::filesystem::remove(*it, ec);
        }
        for (auto it = backedUpFiles.rbegin(); it != backedUpFiles.rend(); ++it) {
            std::filesystem::remove(it->first, ec);
            std::filesystem::rename(it->second, it->first, ec);
        }

        // 8. Remove newly created empty directories
        for (auto it = createdDirs.rbegin(); it != createdDirs.rend(); ++it) {
            std::filesystem::remove(*it, ec);
        }
        active = false;
    }
};

// ================================================================================
// 4. WIN32 HOST HELPERS (DE-ELEVATED DESKTOP TOKEN & `TaskDialogIndirect`)
// ================================================================================
namespace host_detail {

inline std::filesystem::path GetKnownFolderOrEnv(REFKNOWNFOLDERID rfid, const wchar_t* fallbackEnv) {
    PWSTR psz = nullptr;
    if (SUCCEEDED(::SHGetKnownFolderPath(rfid, 0, nullptr, &psz)) && psz) {
        std::filesystem::path p(psz);
        ::CoTaskMemFree(psz);
        return p.lexically_normal();
    }
    if (fallbackEnv) {
        wchar_t buf[MAX_PATH * 4]{};
        if (::GetEnvironmentVariableW(fallbackEnv, buf, static_cast<DWORD>(std::size(buf))) > 0) {
            return std::filesystem::path(buf).lexically_normal();
        }
    }
    return {};
}

/// Launches a child process under `"current"`, `"elevated"` (`runas`), or `"desktop"`
/// (medium-integrity `explorer.exe` shell token duplication so apps launched from an
/// elevated setup wizard run as the interactive standard user!).
inline bool RunProcessWithTokenMode(
    const std::filesystem::path& exePath,
    const std::string& argsUtf8,
    const std::filesystem::path& workDir,
    bool wait,
    bool hidden,
    std::string_view tokenMode,
    std::int64_t& outExitCode,
    std::string& outError)
{
    outExitCode = 0;
    const std::wstring wExe  = exePath.wstring();
    const std::wstring wArgs = Utf8ToWide(argsUtf8);
    const std::wstring wDir  = workDir.empty() ? exePath.parent_path().wstring() : workDir.wstring();

    // Mode 1: `"elevated"` (`ShellExecuteExW` with `runas` verb)
    if (EqualsIgnoreCase(tokenMode, "elevated") && !QueryProcessPrivilegeState().HasEffectiveAdminRights()) {
        SHELLEXECUTEINFOW sei{};
        sei.cbSize       = sizeof(sei);
        sei.fMask        = SEE_MASK_NOCLOSEPROCESS | (hidden ? SEE_MASK_FLAG_NO_UI : 0);
        sei.lpVerb       = L"runas";
        sei.lpFile       = wExe.c_str();
        sei.lpParameters = wArgs.empty() ? nullptr : wArgs.c_str();
        sei.lpDirectory  = wDir.empty() ? nullptr : wDir.c_str();
        sei.nShow        = hidden ? SW_HIDE : SW_SHOWNORMAL;
        if (!::ShellExecuteExW(&sei)) {
            const DWORD gle = ::GetLastError();
            outExitCode = static_cast<std::int64_t>(gle);
            outError    = "Elevated launch canceled or denied (Win32 " + std::to_string(gle) + ").";
            return false;
        }
        if (sei.hProcess) {
            if (wait) {
                ::WaitForSingleObject(sei.hProcess, INFINITE);
                DWORD code = 0;
                ::GetExitCodeProcess(sei.hProcess, &code);
                outExitCode = static_cast<std::int64_t>(code);
            }
            ::CloseHandle(sei.hProcess);
        }
        return true;
    }

    // Mode 2: `"desktop"` (De-elevated Explorer shell token via `GetShellWindow` -> `CreateProcessWithTokenW`)
    if (EqualsIgnoreCase(tokenMode, "desktop") && QueryProcessPrivilegeState().isTokenElevated) {
        HWND hShellWnd = ::GetShellWindow();
        DWORD shellPid = 0;
        if (hShellWnd) ::GetWindowThreadProcessId(hShellWnd, &shellPid);
        if (shellPid != 0 && shellPid != ::GetCurrentProcessId()) {
            if (HANDLE hShellProc = ::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, shellPid)) {
                HANDLE hShellTok = nullptr;
                if (::OpenProcessToken(hShellProc, TOKEN_DUPLICATE, &hShellTok)) {
                    HANDLE hPrimaryTok = nullptr;
                    if (::DuplicateTokenEx(
                            hShellTok,
                            TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY | TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_ADJUST_SESSIONID,
                            nullptr,
                            SecurityImpersonation,
                            TokenPrimary,
                            &hPrimaryTok)) {
                        std::wstring cmdLine = L"\"" + wExe + L"\"";
                        if (!wArgs.empty()) cmdLine += L" " + wArgs;
                        STARTUPINFOW si{};
                        si.cb          = sizeof(si);
                        si.dwFlags     = STARTF_USESHOWWINDOW;
                        si.wShowWindow = static_cast<WORD>(hidden ? SW_HIDE : SW_SHOWNORMAL);
                        PROCESS_INFORMATION pi{};
                        const BOOL launched = ::CreateProcessWithTokenW(
                            hPrimaryTok,
                            0,
                            nullptr,
                            cmdLine.data(),
                            hidden ? CREATE_NO_WINDOW : 0,
                            nullptr,
                            wDir.empty() ? nullptr : wDir.c_str(),
                            &si,
                            &pi);
                        ::CloseHandle(hPrimaryTok);
                        ::CloseHandle(hShellTok);
                        ::CloseHandle(hShellProc);
                        if (launched) {
                            if (wait) {
                                ::WaitForSingleObject(pi.hProcess, INFINITE);
                                DWORD code = 0;
                                ::GetExitCodeProcess(pi.hProcess, &code);
                                outExitCode = static_cast<std::int64_t>(code);
                            }
                            ::CloseHandle(pi.hThread);
                            ::CloseHandle(pi.hProcess);
                            return true;
                        }
                    } else {
                        ::CloseHandle(hShellTok);
                    }
                }
                ::CloseHandle(hShellProc);
            }
        }
    }

    // Mode 3: `"current"` (or fallback when already non-elevated)
    std::wstring cmdLine = L"\"" + wExe + L"\"";
    if (!wArgs.empty()) cmdLine += L" " + wArgs;
    STARTUPINFOW si{};
    si.cb          = sizeof(si);
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = static_cast<WORD>(hidden ? SW_HIDE : SW_SHOWNORMAL);
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(
            nullptr,
            cmdLine.data(),
            nullptr,
            nullptr,
            FALSE,
            hidden ? CREATE_NO_WINDOW : 0,
            nullptr,
            wDir.empty() ? nullptr : wDir.c_str(),
            &si,
            &pi)) {
        const DWORD gle = ::GetLastError();
        outExitCode = static_cast<std::int64_t>(gle);
        outError    = "CreateProcessW failed for '" + WideToUtf8(wExe) + "' (Win32 " + std::to_string(gle) + ").";
        return false;
    }
    if (wait) {
        ::WaitForSingleObject(pi.hProcess, INFINITE);
        DWORD code = 0;
        ::GetExitCodeProcess(pi.hProcess, &code);
        outExitCode = static_cast<std::int64_t>(code);
    }
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    return true;
}

/// Callback used by `ui.taskDialog(...)` to apply UAC elevation shield icons onto individual
/// Command Link buttons (`TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE`) as soon as the dialog opens!
struct TaskDialogShieldContext {
    std::vector<int> shieldButtonIds;
};

inline HRESULT CALLBACK TaskDialogCallbackProc(
    HWND hwnd,
    UINT msg,
    WPARAM,
    LPARAM,
    LONG_PTR lpRefData) noexcept
{
    if (msg == TDN_CREATED && lpRefData != 0) {
        const auto* ctx = reinterpret_cast<const TaskDialogShieldContext*>(lpRefData);
        for (int btnId : ctx->shieldButtonIds) {
            ::SendMessageW(hwnd, TDM_SET_BUTTON_ELEVATION_REQUIRED_STATE, static_cast<WPARAM>(btnId), TRUE);
        }
    }
    return S_OK;
}

/// In-memory HTTPS/HTTP GET text helper via stock Windows `WinHTTP` (`net.getText(url)`).
inline bool FetchUrlTextViaWinHttp(const std::string& urlUtf8, std::string& outBody, std::string& outErr) {
    outBody.clear();
    const std::wstring wUrl = Utf8ToWide(urlUtf8);
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t hostBuf[256]{};
    wchar_t pathBuf[2048]{};
    uc.lpszHostName     = hostBuf;
    uc.dwHostNameLength = static_cast<DWORD>(std::size(hostBuf));
    uc.lpszUrlPath      = pathBuf;
    uc.dwUrlPathLength  = static_cast<DWORD>(std::size(pathBuf));
    if (!::WinHttpCrackUrl(wUrl.c_str(), static_cast<DWORD>(wUrl.size()), 0, &uc)) {
        outErr = "Invalid URL passed to net.getText(): " + urlUtf8;
        return false;
    }
    HINTERNET hSess = ::WinHttpOpen(
        L"RaggieScript/0.6 (Windows NT 10.0; Win64)",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) {
        outErr = "WinHttpOpen failed.";
        return false;
    }
    DWORD redir = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    ::WinHttpSetOption(hSess, WINHTTP_OPTION_REDIRECT_POLICY, &redir, sizeof(redir));
    ::WinHttpSetTimeouts(hSess, 10000, 10000, 20000, 30000);

    HINTERNET hConn = ::WinHttpConnect(hSess, hostBuf, uc.nPort, 0);
    const DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = hConn
        ? ::WinHttpOpenRequest(hConn, L"GET", pathBuf, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags)
        : nullptr;

    bool ok = false;
    if (hReq &&
        ::WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        ::WinHttpReceiveResponse(hReq, nullptr)) {
        DWORD status = 0, cb = sizeof(status);
        ::WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                              WINHTTP_HEADER_NAME_BY_INDEX, &status, &cb, WINHTTP_NO_HEADER_INDEX);
        if (status >= 200 && status < 300) {
            ok = true;
            char chunk[16384];
            for (;;) {
                DWORD avail = 0, got = 0;
                if (!::WinHttpQueryDataAvailable(hReq, &avail) || avail == 0) break;
                const DWORD want = std::min<DWORD>(avail, static_cast<DWORD>(sizeof(chunk)));
                if (!::WinHttpReadData(hReq, chunk, want, &got) || got == 0) break;
                outBody.append(chunk, got);
            }
        } else {
            outErr = "HTTP status " + std::to_string(status) + " from " + urlUtf8;
        }
    } else {
        outErr = "WinHTTP connection failed for " + urlUtf8;
    }
    if (hReq)  ::WinHttpCloseHandle(hReq);
    if (hConn) ::WinHttpCloseHandle(hConn);
    ::WinHttpCloseHandle(hSess);
    return ok;
}

/// Verifies Windows Authenticode digital signature via `WinVerifyTrust` (`crypto.verifyAuthenticode`).
inline bool VerifyAuthenticodeSignature(const std::filesystem::path& filePath) noexcept {
    const std::wstring wPath = filePath.wstring();
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct       = sizeof(fileInfo);
    fileInfo.pcwszFilePath  = wPath.c_str();

    GUID policyGuid = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA wtd{};
    wtd.cbStruct            = sizeof(wtd);
    wtd.dwUIChoice          = WTD_UI_NONE;
    wtd.fdwRevocationChecks = WTD_REVOKE_NONE;
    wtd.dwUnionChoice       = WTD_CHOICE_FILE;
    wtd.pFile               = &fileInfo;
    wtd.dwStateAction       = WTD_STATEACTION_VERIFY;

    const LONG status = ::WinVerifyTrust(nullptr, &policyGuid, &wtd);
    wtd.dwStateAction = WTD_STATEACTION_CLOSE;
    ::WinVerifyTrust(nullptr, &policyGuid, &wtd);
    return status == ERROR_SUCCESS;
}

} // namespace host_detail

// ================================================================================
// 5. VIRTUAL MACHINE CONFIGURATION, EXECUTION RESULT, & INTERPRETER (`RaggieScriptVm`)
// ================================================================================
struct VmExecutionOptions {
    bool                                                  silentMode        = false;
    bool                                                  passiveMode       = false;
    bool                                                  maintenanceMode   = false;
    std::string                                           maintenanceAction = "install"; // `"install"` | `"repair"` | `"uninstall"`
    std::string                                           repairModeArg     = "QuickRepair";
    bool                                                  purgeUserDataArg  = false;
    HWND                                                  parentHwnd        = nullptr;
    std::filesystem::path                                 runningExePath;
    std::filesystem::path                                 supportDirPath;
    std::filesystem::path                                 logFilePath;
    const std::uint8_t*                                   embeddedPakBytes  = nullptr;
    std::size_t                                           embeddedPakSize   = 0;
    std::map<std::string, std::string>                    cliProperties;       // `INSTALLDIR=...` (mutable override)
    std::set<std::string>                                 lockedCliProperties; // `INSTALLDIR:...` (immutable policy lock!)
    std::vector<std::string>                              rawCliSwitches;   // `/s`, `/rsupdate`, etc.
    int                                                   simulateFaultAfterOps = 0;
    const std::atomic<bool>*                              cancelFlag        = nullptr;
    std::function<void(int permille, int compPermille, const std::wstring& status)> onProgress;
    std::function<void(const std::string& line)>          onStdout;
};

struct VmExecutionResult {
    bool                  ok            = true;
    int                   exitCode      = 0;
    bool                  rebootNeeded  = false;
    bool                  rolledBack    = false;
    std::string           errorMessage;
    int                   errorLine     = 0;
    std::filesystem::path resolvedInstallDir;
    std::vector<std::string> consoleLines;
};

class RaggieScriptVm {
public:
    RaggieScriptVm(const ScriptProgram& prog, VmExecutionOptions opts = {})
        : prog_(prog), opts_(std::move(opts)), globalScope_(nullptr)
    {
        if (opts_.runningExePath.empty()) {
            wchar_t selfBuf[MAX_PATH * 4]{};
            ::GetModuleFileNameW(nullptr, selfBuf, static_cast<DWORD>(std::size(selfBuf)));
            opts_.runningExePath = std::filesystem::path(selfBuf).lexically_normal();
        }
        if (opts_.simulateFaultAfterOps == 0) {
            wchar_t envBuf[64]{};
            if (::GetEnvironmentVariableW(L"RSW_SCRIPT_FAULT_AFTER_OPS", envBuf, 64) > 0) {
                opts_.simulateFaultAfterOps = _wtoi(envBuf);
            }
        }
        PopulateBuiltInGlobals();
    }

    Scope& GlobalScope() noexcept { return globalScope_; }
    const Scope& GlobalScope() const noexcept { return globalScope_; }

    /// Runs a standalone `.rssc` micro-app or full installer lifecycle (`topStatements` -> `wizard` -> `install` / `repair` / `uninstall`).
    VmExecutionResult ExecuteFullLifecycle() {
        VmExecutionResult res{};

        // 1. Evaluate `param` declarations (applying CLI `KEY=VALUE` soft overrides AND `KEY:VALUE` immutable locks!)
        for (const auto& p : prog_.params) {
            Value val = p.defaultExpr ? EvalExpr(p.defaultExpr, globalScope_) : Value::Null();
            if (Failed()) return BuildFailureResult(res);

            bool isLockedByCli = false;
            for (const auto& [cliK, cliV] : opts_.cliProperties) {
                if (EqualsIgnoreCase(cliK, p.name)) {
                    const std::string tl = AsciiLower(p.typeName);
                    if (tl == "bool")         val = Value::FromBool(EqualsIgnoreCase(cliV, "true") || cliV == "1" || EqualsIgnoreCase(cliV, "yes"));
                    else if (tl == "int")     val = Value::FromInt(std::strtoll(cliV.c_str(), nullptr, 0));
                    else if (tl == "version") val = Value::FromVer(SemVer::Parse(cliV));
                    else if (tl == "path")    val = Value::FromPathUtf8(cliV);
                    else                      val = Value::FromStr(cliV);
                    for (const auto& lk : opts_.lockedCliProperties) {
                        if (EqualsIgnoreCase(lk, cliK)) isLockedByCli = true;
                    }
                    break;
                }
            }
            globalScope_.Declare(p.name, std::move(val), isLockedByCli);
        }

        // Ensure standard `installDir` variable exists if `package` header is present and author didn't declare one
        Value existingDir;
        if (prog_.hasPackageHeader && !globalScope_.TryGet("installDir", existingDir)) {
            const bool perMach = EqualsIgnoreCase(prog_.package.scope, "machine");
            const auto base = perMach
                ? host_detail::GetKnownFolderOrEnv(FOLDERID_ProgramFilesX64, L"ProgramW6432")
                : host_detail::GetKnownFolderOrEnv(FOLDERID_LocalAppData, L"LOCALAPPDATA");
            const auto defDir = base / Utf8ToWide(prog_.package.publisher) / Utf8ToWide(prog_.package.name);
            globalScope_.Declare("installDir", Value::FromPath(defDir), false);
        }

        // 2. Execute top-level statements (pre-checks, OS/CPU gates, or standalone micro-app script!)
        for (const auto& st : prog_.topStatements) {
            ExecStmt(st, globalScope_);
            if (flow_ == FlowKind::ExitSetup) return FinalizeNormalResult(res);
            if (Failed()) return BuildFailureResult(res);
        }

        // Dispatch Maintenance Repair or Uninstall when requested!
        if (EqualsIgnoreCase(opts_.maintenanceAction, "repair") && prog_.repairBlock) {
            Scope repScope(&globalScope_);
            repScope.Declare(prog_.repairParamName, Value::FromStr(opts_.repairModeArg), false);
            ExecStmt(prog_.repairBlock, repScope);
            if (Failed()) return BuildFailureResult(res);
            return FinalizeNormalResult(res);
        }
        if (EqualsIgnoreCase(opts_.maintenanceAction, "uninstall") && prog_.uninstallBlock) {
            Scope unScope(&globalScope_);
            unScope.Declare(prog_.uninstallParamName, Value::FromBool(opts_.purgeUserDataArg), false);
            ExecStmt(prog_.uninstallBlock, unScope);
            if (Failed()) return BuildFailureResult(res);
            return FinalizeNormalResult(res);
        }

        // 3. Execute side-effect-free `wizard { ... }` phase when NOT in `/s` silent mode!
        if (!opts_.silentMode && prog_.wizardBlock) {
            inWizardPhase_ = true;
            ExecuteWizardBlock(prog_.wizardBlock);
            inWizardPhase_ = false;
            if (flow_ == FlowKind::ExitSetup) return FinalizeNormalResult(res);
            if (Failed()) return BuildFailureResult(res);
        }

        // 4. Execute transactional, rollback-journaled `install { ... }` phase!
        if (prog_.installBlock) {
            std::filesystem::path targetInstallDir;
            Value dirVal;
            if (globalScope_.TryGet("installDir", dirVal)) {
                targetInstallDir = dirVal.AsPath();
            }
            ledger_.Begin(targetInstallDir, opts_.simulateFaultAfterOps);

            Scope instScope(&globalScope_);
            ExecStmt(prog_.installBlock, instScope);

            if (opts_.cancelFlag && opts_.cancelFlag->load() && !Failed()) {
                RaiseError(prog_.installBlock->line, "Installation canceled by user.");
            }

            if (Failed()) {
                ledger_.RollbackLifo();
                res.rolledBack = true;
                return BuildFailureResult(res);
            }

            // If a `package` header was declared (not `app` or `sfx`) and `arp.register` wasn't called manually,
            // register `Uninstall.exe` + `Uninstall.rswm` automatically unless `hideFromArp` was requested without uninstall!
            if (prog_.hasPackageHeader && EqualsIgnoreCase(prog_.package.kind, "package") &&
                !arpRegisteredDuringInstall_ && !targetInstallDir.empty()) {
                std::string arpErr;
                if (!RegisterArpAndUninstallMeta(
                        prog_.package.id,
                        prog_.package.name,
                        prog_.package.version,
                        prog_.package.publisher,
                        targetInstallDir,
                        prog_.package.mainExe,
                        prog_.package.websiteUrl,
                        EqualsIgnoreCase(prog_.package.scope, "machine"),
                        prog_.package.hideFromArp,
                        arpErr)) {
                    RaiseError(prog_.installBlock->line, arpErr);
                    ledger_.RollbackLifo();
                    res.rolledBack = true;
                    return BuildFailureResult(res);
                }
            }

            ledger_.Commit();
        }

        return FinalizeNormalResult(res);
    }

    /// Fires a Showcase Studio canvas or hotspot event (`on hotspot("BtnInstall").click { ... }`).
    bool TriggerEventHandler(std::string_view targetKind, std::string_view targetId, std::string_view eventName) {
        for (const auto& ev : prog_.eventHandlers) {
            if (EqualsIgnoreCase(ev.targetKind, targetKind) &&
                (ev.targetId.empty() || EqualsIgnoreCase(ev.targetId, targetId)) &&
                EqualsIgnoreCase(ev.eventName, eventName)) {
                Scope evScope(&globalScope_);
                ExecStmt(ev.body, evScope);
                return !Failed();
            }
        }
        return true;
    }

private:
    enum class FlowKind {
        Normal = 0,
        Return,
        Break,
        Continue,
        ExitSetup,
        WizardNext,
        WizardBack,
        Error
    };

    const ScriptProgram&      prog_;
    VmExecutionOptions        opts_;
    Scope                     globalScope_;
    ScriptRollbackLedger      ledger_;
    FlowKind                  flow_                       = FlowKind::Normal;
    Value                     returnVal_                  = Value::Null();
    int                       exitCode_                   = 0;
    bool                      rebootNeeded_               = false;
    bool                      inWizardPhase_              = false;
    bool                      arpRegisteredDuringInstall_ = false;
    std::string               errorMsg_;
    int                       errorLine_                  = 0;
    std::vector<std::string>  consoleLines_;
    std::map<std::string, Value> showcaseState_;

    bool Failed() const noexcept { return flow_ == FlowKind::Error; }

    void RaiseError(int line, std::string msg) {
        if (flow_ != FlowKind::Error) {
            flow_      = FlowKind::Error;
            errorLine_ = line;
            errorMsg_  = std::move(msg);
            if (exitCode_ == 0) exitCode_ = 1603;
        }
    }

    VmExecutionResult FinalizeNormalResult(VmExecutionResult& res) {
        res.ok           = true;
        res.rebootNeeded = rebootNeeded_;
        res.exitCode     = exitCode_;
        if (res.exitCode == 0 && rebootNeeded_ && opts_.silentMode) {
            res.exitCode = 9001; // RaggieSoft canonical silent reboot exit code!
        }
        Value dirVal;
        if (globalScope_.TryGet("installDir", dirVal)) {
            res.resolvedInstallDir = dirVal.AsPath();
        }
        res.consoleLines = consoleLines_;
        return res;
    }

    VmExecutionResult BuildFailureResult(VmExecutionResult& res) {
        res.ok           = false;
        res.exitCode     = (exitCode_ != 0) ? exitCode_ : 1603;
        res.rebootNeeded = rebootNeeded_;
        res.errorMessage = errorMsg_;
        res.errorLine    = errorLine_;
        res.consoleLines = consoleLines_;
        return res;
    }

    void PopulateBuiltInGlobals() {
        // Canonical RaggieSoft exit code constants & strongly-named enum maps!
        globalScope_.Declare("EXIT_OK",                 Value::FromInt(0),    true);
        globalScope_.Declare("EXIT_ELEVATION_REQUIRED", Value::FromInt(740),  true);
        globalScope_.Declare("EXIT_REBOOT_NEEDED",      Value::FromInt(9001), true);
        globalScope_.Declare("EXIT_UNSUPPORTED_OS",     Value::FromInt(9007), true);
        globalScope_.Declare("EXIT_PREREQ_MISSING",     Value::FromInt(9008), true);
        globalScope_.Declare("EXIT_DOWNLOAD_FAILED",    Value::FromInt(9010), true);

        globalScope_.Declare("Scope", Value::FromMap({
            {"Machine", Value::FromStr("machine")},
            {"User",    Value::FromStr("user")}
        }), true);
        globalScope_.Declare("Uac", Value::FromMap({
            {"AsInvoker",            Value::FromStr("asInvoker")},
            {"HighestAvailable",     Value::FromStr("highestAvailable")},
            {"RequireAdministrator", Value::FromStr("requireAdministrator")}
        }), true);
        globalScope_.Declare("Token", Value::FromMap({
            {"Current",  Value::FromStr("current")},
            {"Elevated", Value::FromStr("elevated")},
            {"Desktop",  Value::FromStr("desktop")}
        }), true);
        globalScope_.Declare("RegView", Value::FromMap({
            {"Native64", Value::FromStr("native64")},
            {"Wow32",    Value::FromStr("wow32")},
            {"Both",     Value::FromStr("both")}
        }), true);
        globalScope_.Declare("Icon", Value::FromMap({
            {"Info",    Value::FromStr("info")},
            {"Warning", Value::FromStr("warning")},
            {"Error",   Value::FromStr("error")},
            {"Shield",  Value::FromStr("shield")}
        }), true);

        showcaseState_["currentPage"] = Value::FromStr("Main");
        showcaseState_["audioMuted"]  = Value::FromBool(!prog_.package.autoPlayAudio);
    }

    // ----------------------------------------------------------------------------
    // Wizard Step History Navigator (`wizard { step Welcome { ... } step Folder when (...) { ... } }`)
    //   Supports arbitrary `if/else` conditions and genuine `wizard.back()` step-stack navigation!
    // ----------------------------------------------------------------------------
    void ExecuteWizardBlock(const StmtPtr& wizBlk) {
        if (!wizBlk) return;
        // Collect `WizardStep` children vs. plain statements
        bool hasExplicitSteps = false;
        for (const auto& ch : wizBlk->children) {
            if (ch && ch->kind == StmtKind::WizardStep) {
                hasExplicitSteps = true;
                break;
            }
        }
        if (!hasExplicitSteps) {
            Scope wizScope(&globalScope_);
            ExecStmt(wizBlk, wizScope);
            return;
        }

        std::vector<std::size_t> history;
        std::size_t idx = 0;
        while (idx < wizBlk->children.size() && flow_ == FlowKind::Normal) {
            const auto& st = wizBlk->children[idx];
            if (!st) { ++idx; continue; }
            if (st->kind != StmtKind::WizardStep) {
                ExecStmt(st, globalScope_);
                if (flow_ != FlowKind::Normal) break;
                ++idx;
                continue;
            }

            // Evaluate optional `when (cond)` guard on `step Name when (cond) { ... }`
            if (st->expr) {
                const Value condVal = EvalExpr(st->expr, globalScope_);
                if (Failed()) return;
                if (!condVal.AsBool()) {
                    ++idx;
                    continue;
                }
            }

            Scope stepScope(&globalScope_);
            ExecStmt(st->body, stepScope);
            if (flow_ == FlowKind::WizardBack) {
                flow_ = FlowKind::Normal;
                if (!history.empty()) {
                    idx = history.back();
                    history.pop_back();
                }
                continue;
            }
            if (flow_ == FlowKind::WizardNext) {
                flow_ = FlowKind::Normal;
            }
            if (flow_ != FlowKind::Normal) break;

            history.push_back(idx);
            ++idx;
        }
    }

    // ----------------------------------------------------------------------------
    // Statement Interpreter
    // ----------------------------------------------------------------------------
    void ExecStmt(const StmtPtr& s, Scope& scope) {
        if (!s || flow_ != FlowKind::Normal) return;
        if (opts_.cancelFlag && opts_.cancelFlag->load()) {
            RaiseError(s->line, "Canceled by user.");
            return;
        }

        switch (s->kind) {
            case StmtKind::Block: {
                Scope blkScope(&scope);
                for (const auto& ch : s->children) {
                    ExecStmt(ch, blkScope);
                    if (flow_ != FlowKind::Normal) break;
                }
                break;
            }
            case StmtKind::VarDecl: {
                Value initVal = s->expr ? DerefIfNeeded(EvalExpr(s->expr, scope)) : Value::Null();
                if (Failed()) return;
                if (EqualsIgnoreCase(s->secondName, "path") && initVal.kind == ValKind::Str) {
                    initVal = Value::FromPathUtf8(initVal.strVal);
                } else if (EqualsIgnoreCase(s->secondName, "version") && initVal.kind == ValKind::Str) {
                    initVal = Value::FromVer(SemVer::Parse(initVal.strVal));
                }
                scope.Declare(s->name, std::move(initVal), s->isConst);
                break;
            }
            case StmtKind::Assign: {
                Value rhs = DerefIfNeeded(EvalExpr(s->expr, scope));
                if (Failed()) return;
                AssignLValue(s->target, s->name, std::move(rhs), scope, s->line);
                break;
            }
            case StmtKind::ExprStmt: {
                (void)EvalExpr(s->expr, scope);
                break;
            }
            case StmtKind::If: {
                const Value c = DerefIfNeeded(EvalExpr(s->expr, scope));
                if (Failed()) return;
                if (c.AsBool()) ExecStmt(s->body, scope);
                else if (s->elseBody) ExecStmt(s->elseBody, scope);
                break;
            }
            case StmtKind::While: {
                while (flow_ == FlowKind::Normal) {
                    const Value c = DerefIfNeeded(EvalExpr(s->expr, scope));
                    if (Failed() || !c.AsBool()) break;
                    ExecStmt(s->body, scope);
                    if (flow_ == FlowKind::Break)    { flow_ = FlowKind::Normal; break; }
                    if (flow_ == FlowKind::Continue) { flow_ = FlowKind::Normal; continue; }
                }
                break;
            }
            case StmtKind::RepeatUntil: {
                do {
                    ExecStmt(s->body, scope);
                    if (flow_ == FlowKind::Break)    { flow_ = FlowKind::Normal; break; }
                    if (flow_ == FlowKind::Continue) { flow_ = FlowKind::Normal; }
                    if (flow_ != FlowKind::Normal) break;
                    const Value c = DerefIfNeeded(EvalExpr(s->expr, scope));
                    if (Failed() || c.AsBool()) break;
                } while (flow_ == FlowKind::Normal);
                break;
            }
            case StmtKind::ForIn: {
                const Value iter = DerefIfNeeded(EvalExpr(s->expr, scope));
                if (Failed()) return;
                if (iter.kind == ValKind::Array && iter.arrVal) {
                    const auto snapshot = *iter.arrVal;
                    for (std::size_t i = 0; i < snapshot.size() && flow_ == FlowKind::Normal; ++i) {
                        Scope loopScope(&scope);
                        if (s->secondName.empty()) {
                            loopScope.Declare(s->name, snapshot[i], false);
                        } else {
                            loopScope.Declare(s->name, Value::FromInt(static_cast<std::int64_t>(i)), false);
                            loopScope.Declare(s->secondName, snapshot[i], false);
                        }
                        ExecStmt(s->body, loopScope);
                        if (flow_ == FlowKind::Break)    { flow_ = FlowKind::Normal; break; }
                        if (flow_ == FlowKind::Continue) { flow_ = FlowKind::Normal; continue; }
                    }
                } else if (iter.kind == ValKind::Map && iter.mapVal) {
                    const auto snapshot = *iter.mapVal;
                    for (const auto& [k, v] : snapshot) {
                        if (flow_ != FlowKind::Normal) break;
                        Scope loopScope(&scope);
                        loopScope.Declare(s->name, Value::FromStr(k), false);
                        if (!s->secondName.empty()) {
                            loopScope.Declare(s->secondName, v, false);
                        }
                        ExecStmt(s->body, loopScope);
                        if (flow_ == FlowKind::Break)    { flow_ = FlowKind::Normal; break; }
                        if (flow_ == FlowKind::Continue) { flow_ = FlowKind::Normal; continue; }
                    }
                }
                break;
            }
            case StmtKind::Break:    flow_ = FlowKind::Break; break;
            case StmtKind::Continue: flow_ = FlowKind::Continue; break;
            case StmtKind::Return: {
                returnVal_ = s->expr ? DerefIfNeeded(EvalExpr(s->expr, scope)) : Value::Null();
                if (!Failed()) flow_ = FlowKind::Return;
                break;
            }
            case StmtKind::Throw: {
                const Value v = s->expr ? DerefIfNeeded(EvalExpr(s->expr, scope)) : Value::FromStr("Script error");
                if (!Failed()) RaiseError(s->line, v.AsString());
                break;
            }
            case StmtKind::ExitSetup: {
                const Value v = s->expr ? DerefIfNeeded(EvalExpr(s->expr, scope)) : Value::FromInt(0);
                if (!Failed()) {
                    exitCode_ = static_cast<int>(v.AsInt());
                    flow_     = FlowKind::ExitSetup;
                }
                break;
            }
            case StmtKind::TryCatch: {
                const int savedExit = exitCode_;
                ExecStmt(s->body, scope);
                if (flow_ == FlowKind::Error) {
                    const std::string caughtMsg = errorMsg_;
                    flow_      = FlowKind::Normal;
                    errorMsg_.clear();
                    errorLine_ = 0;
                    exitCode_  = savedExit;
                    Scope catchScope(&scope);
                    catchScope.Declare(s->name.empty() ? "err" : s->name, Value::FromStr(caughtMsg), false);
                    ExecStmt(s->elseBody, catchScope);
                }
                break;
            }
            case StmtKind::WizardStep: {
                if (s->expr) {
                    const Value c = DerefIfNeeded(EvalExpr(s->expr, scope));
                    if (Failed() || !c.AsBool()) break;
                }
                Scope stepScope(&scope);
                ExecStmt(s->body, stepScope);
                break;
            }
        }
    }

    void AssignLValue(const ExprPtr& target, const std::string& op, Value rhs, Scope& scope, int line) {
        if (!target) return;
        auto applyCompound = [&](const Value& cur) -> Value {
            if (op == "=") return rhs;
            if (op == "+=") return EvalBinaryOp("+", cur, rhs, line);
            if (op == "-=") return EvalBinaryOp("-", cur, rhs, line);
            return rhs;
        };

        if (target->kind == ExprKind::Ident) {
            Value cur;
            if (!scope.TryGet(target->strVal, cur)) {
                RaiseError(line, "Undeclared variable '" + target->strVal + "'.");
                return;
            }
            // If `cur` is itself a `ref` parameter inside a user `fn`, write through it!
            if (cur.kind == ValKind::Ref && cur.refScope) {
                Value refCur = DerefIfNeeded(cur);
                Value next   = applyCompound(refCur);
                std::string err;
                if (!WriteThroughRef(cur, std::move(next), err)) RaiseError(line, err);
                return;
            }
            Value next = applyCompound(cur);
            std::string err;
            if (!scope.Assign(target->strVal, std::move(next), err)) {
                RaiseError(line, err);
            }
            return;
        }

        if (target->kind == ExprKind::Member && target->lhs) {
            Value obj = DerefIfNeeded(EvalExpr(target->lhs, scope));
            if (Failed()) return;
            if (obj.kind == ValKind::Map && obj.mapVal) {
                Value cur = obj.mapVal->count(target->strVal) ? (*obj.mapVal)[target->strVal] : Value::Null();
                (*obj.mapVal)[target->strVal] = applyCompound(cur);
                return;
            }
        }

        if (target->kind == ExprKind::Index && target->lhs && target->rhs) {
            Value obj = DerefIfNeeded(EvalExpr(target->lhs, scope));
            Value idx = DerefIfNeeded(EvalExpr(target->rhs, scope));
            if (Failed()) return;
            if (obj.kind == ValKind::Array && obj.arrVal) {
                const auto i = idx.AsInt();
                if (i >= 0 && static_cast<std::size_t>(i) < obj.arrVal->size()) {
                    (*obj.arrVal)[static_cast<std::size_t>(i)] = applyCompound((*obj.arrVal)[static_cast<std::size_t>(i)]);
                    return;
                }
                RaiseError(line, "Array index out of bounds: " + std::to_string(i));
                return;
            }
            if (obj.kind == ValKind::Map && obj.mapVal) {
                const std::string k = idx.AsString();
                Value cur = obj.mapVal->count(k) ? (*obj.mapVal)[k] : Value::Null();
                (*obj.mapVal)[k] = applyCompound(cur);
                return;
            }
        }

        RaiseError(line, "Invalid assignment target.");
    }

    // ----------------------------------------------------------------------------
    // Expression Evaluator
    // ----------------------------------------------------------------------------
    Value EvalExpr(const ExprPtr& e, Scope& scope) {
        if (!e || Failed()) return Value::Null();

        switch (e->kind) {
            case ExprKind::NullLit:  return Value::Null();
            case ExprKind::BoolLit:  return Value::FromBool(e->boolVal);
            case ExprKind::IntLit:   return Value::FromInt(e->intVal);
            case ExprKind::FloatLit: return Value::FromFloat(e->floatVal);
            case ExprKind::StrLit:   return Value::FromStr(e->strVal);
            case ExprKind::VerLit:   return Value::FromVer(SemVer::Parse(e->strVal));
            case ExprKind::RefVar:   return Value::FromRef(&scope, e->strVal);

            case ExprKind::Ident: {
                Value v;
                if (scope.TryGet(e->strVal, v)) return DerefIfNeeded(v);
                RaiseError(e->line, "Unknown identifier '" + e->strVal + "'.");
                return Value::Null();
            }

            case ExprKind::ArrayLit: {
                std::vector<Value> items;
                items.reserve(e->items.size());
                for (const auto& it : e->items) {
                    items.push_back(DerefIfNeeded(EvalExpr(it, scope)));
                    if (Failed()) return Value::Null();
                }
                return Value::FromArray(std::move(items));
            }

            case ExprKind::MapLit: {
                std::map<std::string, Value> mp;
                for (const auto& [k, valEx] : e->mapEntries) {
                    mp[k] = DerefIfNeeded(EvalExpr(valEx, scope));
                    if (Failed()) return Value::Null();
                }
                return Value::FromMap(std::move(mp));
            }

            case ExprKind::Unary: {
                const Value v = DerefIfNeeded(EvalExpr(e->lhs, scope));
                if (Failed()) return Value::Null();
                if (e->strVal == "!") return Value::FromBool(!v.AsBool());
                if (e->strVal == "-") {
                    return (v.kind == ValKind::Float) ? Value::FromFloat(-v.floatVal) : Value::FromInt(-v.AsInt());
                }
                return v;
            }

            case ExprKind::Binary: {
                // Short-circuit `&&` and `||`!
                if (e->strVal == "&&") {
                    const Value l = DerefIfNeeded(EvalExpr(e->lhs, scope));
                    if (Failed() || !l.AsBool()) return Value::FromBool(false);
                    const Value r = DerefIfNeeded(EvalExpr(e->rhs, scope));
                    return Value::FromBool(!Failed() && r.AsBool());
                }
                if (e->strVal == "||") {
                    const Value l = DerefIfNeeded(EvalExpr(e->lhs, scope));
                    if (Failed()) return Value::Null();
                    if (l.AsBool()) return Value::FromBool(true);
                    const Value r = DerefIfNeeded(EvalExpr(e->rhs, scope));
                    return Value::FromBool(!Failed() && r.AsBool());
                }
                const Value l = DerefIfNeeded(EvalExpr(e->lhs, scope));
                const Value r = DerefIfNeeded(EvalExpr(e->rhs, scope));
                if (Failed()) return Value::Null();
                return EvalBinaryOp(e->strVal, l, r, e->line);
            }

            case ExprKind::Ternary: {
                const Value c = DerefIfNeeded(EvalExpr(e->cond, scope));
                if (Failed()) return Value::Null();
                return DerefIfNeeded(EvalExpr(c.AsBool() ? e->lhs : e->rhs, scope));
            }

            case ExprKind::Member: {
                // Built-in `sys.<property>` known folder & platform properties (`sys.programFiles`, `sys.localAppData`, etc.)
                if (e->lhs && e->lhs->kind == ExprKind::Ident && e->lhs->strVal == "sys") {
                    Value propVal;
                    if (TryReadSysProperty(e->strVal, propVal)) return propVal;
                }
                const Value obj = DerefIfNeeded(EvalExpr(e->lhs, scope));
                if (Failed()) return Value::Null();
                if (obj.kind == ValKind::Map && obj.mapVal) {
                    auto it = obj.mapVal->find(e->strVal);
                    return (it != obj.mapVal->end()) ? it->second : Value::Null();
                }
                if (obj.kind == ValKind::Array && obj.arrVal && (e->strVal == "length" || e->strVal == "count")) {
                    return Value::FromInt(static_cast<std::int64_t>(obj.arrVal->size()));
                }
                if ((obj.kind == ValKind::Str || obj.kind == ValKind::Path) && e->strVal == "length") {
                    return Value::FromInt(static_cast<std::int64_t>(obj.strVal.size()));
                }
                if (obj.kind == ValKind::Ver) {
                    if (e->strVal == "major") return Value::FromInt(obj.verVal.parts[0]);
                    if (e->strVal == "minor") return Value::FromInt(obj.verVal.parts[1]);
                    if (e->strVal == "build" || e->strVal == "patch") return Value::FromInt(obj.verVal.parts[2]);
                    if (e->strVal == "revision") return Value::FromInt(obj.verVal.parts[3]);
                }
                return Value::Null();
            }

            case ExprKind::Index: {
                const Value obj = DerefIfNeeded(EvalExpr(e->lhs, scope));
                const Value idx = DerefIfNeeded(EvalExpr(e->rhs, scope));
                if (Failed()) return Value::Null();
                if (obj.kind == ValKind::Array && obj.arrVal) {
                    const auto i = idx.AsInt();
                    if (i >= 0 && static_cast<std::size_t>(i) < obj.arrVal->size()) {
                        return (*obj.arrVal)[static_cast<std::size_t>(i)];
                    }
                    return Value::Null();
                }
                if (obj.kind == ValKind::Map && obj.mapVal) {
                    auto it = obj.mapVal->find(idx.AsString());
                    return (it != obj.mapVal->end()) ? it->second : Value::Null();
                }
                return Value::Null();
            }

            case ExprKind::Call:
                return EvalCallExpr(e, scope);
        }
        return Value::Null();
    }

    Value EvalBinaryOp(const std::string& op, const Value& l, const Value& r, int line) {
        // First-class `Path / "Subdir"` joining!
        if (op == "/" && (l.kind == ValKind::Path || r.kind == ValKind::Path)) {
            return Value::FromPath(l.AsPath() / Utf8ToWide(r.AsString()));
        }

        // Array concatenation (`arrA + arrB`)
        if (op == "+" && l.kind == ValKind::Array && r.kind == ValKind::Array && l.arrVal && r.arrVal) {
            std::vector<Value> merged = *l.arrVal;
            merged.insert(merged.end(), r.arrVal->begin(), r.arrVal->end());
            return Value::FromArray(std::move(merged));
        }

        // String concatenation (`"a" + x` or `x + "b"`)
        if (op == "+" && (l.kind == ValKind::Str || r.kind == ValKind::Str)) {
            return Value::FromStr(l.AsString() + r.AsString());
        }

        // First-class Semantic Version (`SemVer`) comparisons (`v"10.0.22000" <= sys.osVersion()`)!
        if (l.kind == ValKind::Ver || r.kind == ValKind::Ver) {
            const int cmp = l.AsVersion().Compare(r.AsVersion());
            if (op == "==") return Value::FromBool(cmp == 0);
            if (op == "!=") return Value::FromBool(cmp != 0);
            if (op == "<")  return Value::FromBool(cmp <  0);
            if (op == "<=") return Value::FromBool(cmp <= 0);
            if (op == ">")  return Value::FromBool(cmp >  0);
            if (op == ">=") return Value::FromBool(cmp >= 0);
        }

        // Equality for Null / Bool / Str / Path
        if (op == "==" || op == "!=") {
            bool eq = false;
            if (l.kind == ValKind::Null || r.kind == ValKind::Null) {
                eq = (l.kind == ValKind::Null && r.kind == ValKind::Null);
            } else if ((l.kind == ValKind::Str || l.kind == ValKind::Path) &&
                       (r.kind == ValKind::Str || r.kind == ValKind::Path)) {
                eq = (l.strVal == r.strVal);
            } else if (l.kind == ValKind::Bool && r.kind == ValKind::Bool) {
                eq = (l.boolVal == r.boolVal);
            } else if (l.kind == ValKind::Float || r.kind == ValKind::Float) {
                eq = (l.AsFloat() == r.AsFloat());
            } else {
                eq = (l.AsInt() == r.AsInt());
            }
            return Value::FromBool(op == "==" ? eq : !eq);
        }

        // Relational `<, <=, >, >=` on strings vs numbers
        if (op == "<" || op == "<=" || op == ">" || op == ">=") {
            if ((l.kind == ValKind::Str || l.kind == ValKind::Path) &&
                (r.kind == ValKind::Str || r.kind == ValKind::Path)) {
                const int cmp = l.strVal.compare(r.strVal);
                if (op == "<")  return Value::FromBool(cmp <  0);
                if (op == "<=") return Value::FromBool(cmp <= 0);
                if (op == ">")  return Value::FromBool(cmp >  0);
                return Value::FromBool(cmp >= 0);
            }
            if (l.kind == ValKind::Float || r.kind == ValKind::Float) {
                const double a = l.AsFloat(), b = r.AsFloat();
                if (op == "<")  return Value::FromBool(a <  b);
                if (op == "<=") return Value::FromBool(a <= b);
                if (op == ">")  return Value::FromBool(a >  b);
                return Value::FromBool(a >= b);
            }
            const std::int64_t a = l.AsInt(), b = r.AsInt();
            if (op == "<")  return Value::FromBool(a <  b);
            if (op == "<=") return Value::FromBool(a <= b);
            if (op == ">")  return Value::FromBool(a >  b);
            return Value::FromBool(a >= b);
        }

        // Arithmetic `+`, `-`, `*`, `/`, `%`
        if (l.kind == ValKind::Float || r.kind == ValKind::Float) {
            const double a = l.AsFloat(), b = r.AsFloat();
            if (op == "+") return Value::FromFloat(a + b);
            if (op == "-") return Value::FromFloat(a - b);
            if (op == "*") return Value::FromFloat(a * b);
            if (op == "/") {
                if (b == 0.0) { RaiseError(line, "Division by zero."); return Value::Null(); }
                return Value::FromFloat(a / b);
            }
        }
        const std::int64_t a = l.AsInt(), b = r.AsInt();
        if (op == "+") return Value::FromInt(a + b);
        if (op == "-") return Value::FromInt(a - b);
        if (op == "*") return Value::FromInt(a * b);
        if (op == "/") {
            if (b == 0) { RaiseError(line, "Integer division by zero."); return Value::Null(); }
            return Value::FromInt(a / b);
        }
        if (op == "%") {
            if (b == 0) { RaiseError(line, "Modulo by zero."); return Value::Null(); }
            return Value::FromInt(a % b);
        }
        return Value::Null();
    }

    // ----------------------------------------------------------------------------
    // Call Argument Helper (`EvalCallArgs`)
    // ----------------------------------------------------------------------------
    struct EvaluatedArgs {
        std::vector<Value>           pos;   // All evaluated arguments in order (keeping `Ref` intact!)
        std::map<std::string, Value> named; // Named `key: value` arguments (lowercased key -> `Value`)

        Value At(std::size_t idx, Value defVal = Value::Null()) const {
            return (idx < pos.size()) ? DerefIfNeeded(pos[idx]) : defVal;
        }
        Value NamedOrPos(std::string_view key, std::size_t idx, Value defVal = Value::Null()) const {
            auto it = named.find(AsciiLower(key));
            if (it != named.end()) return DerefIfNeeded(it->second);
            return (idx < pos.size()) ? DerefIfNeeded(pos[idx]) : defVal;
        }
        Value RawNamedOrPos(std::string_view key, std::size_t idx) const {
            auto it = named.find(AsciiLower(key));
            if (it != named.end()) return it->second;
            return (idx < pos.size()) ? pos[idx] : Value::Null();
        }
    };

    EvaluatedArgs EvalCallArgs(const std::vector<CallArg>& rawArgs, Scope& scope) {
        EvaluatedArgs out;
        for (const auto& ca : rawArgs) {
            Value v = EvalExpr(ca.value, scope);
            if (Failed()) return out;
            if (!ca.name.empty()) {
                out.named[AsciiLower(ca.name)] = v;
            }
            out.pos.push_back(std::move(v));
        }
        return out;
    }

    // ============================================================================
    // 6. BUILT-IN FUNCTIONS & 15 WIN32 STANDARD / HOST NAMESPACES
    // ============================================================================
    bool TryReadSysProperty(std::string_view prop, Value& out) const {
        if (prop == "programFiles")            { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_ProgramFilesX64, L"ProgramW6432")); return true; }
        if (prop == "programFilesX86")         { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_ProgramFilesX86, L"ProgramFiles(x86)")); return true; }
        if (prop == "commonFiles")             { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_ProgramFilesCommonX64, L"CommonProgramW6432")); return true; }
        if (prop == "commonFilesX86")          { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_ProgramFilesCommonX86, L"CommonProgramFiles(x86)")); return true; }
        if (prop == "localAppData")            { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_LocalAppData, L"LOCALAPPDATA")); return true; }
        if (prop == "roamingAppData")          { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_RoamingAppData, L"APPDATA")); return true; }
        if (prop == "programData")             { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_ProgramData, L"ProgramData")); return true; }
        if (prop == "desktopDir")              { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_Desktop, nullptr)); return true; }
        if (prop == "commonDesktopDir")        { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_PublicDesktop, nullptr)); return true; }
        if (prop == "startMenuPrograms")       { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_Programs, nullptr)); return true; }
        if (prop == "commonStartMenuPrograms") { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_CommonPrograms, nullptr)); return true; }
        if (prop == "windowsDir")              { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_Windows, L"SystemRoot")); return true; }
        if (prop == "system32Dir")             { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_System, nullptr)); return true; }
        if (prop == "sysWow64Dir")             { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_SystemX86, nullptr)); return true; }
        if (prop == "documentsDir")            { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_Documents, nullptr)); return true; }
        if (prop == "publicDocumentsDir")      { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_PublicDocuments, nullptr)); return true; }
        if (prop == "fontsDir")                { out = Value::FromPath(host_detail::GetKnownFolderOrEnv(FOLDERID_Fonts, nullptr)); return true; }
        if (prop == "tempDir") {
            std::error_code ec;
            out = Value::FromPath(std::filesystem::temp_directory_path(ec));
            return true;
        }
        if (prop == "exePath")    { out = Value::FromPath(opts_.runningExePath); return true; }
        if (prop == "exeDir")     { out = Value::FromPath(opts_.runningExePath.parent_path()); return true; }
        if (prop == "supportDir") {
            const auto sup = opts_.supportDirPath.empty()
                ? (opts_.runningExePath.parent_path() / L"_support")
                : opts_.supportDirPath;
            out = Value::FromPath(sup);
            return true;
        }
        return false;
    }

    Value EvalCallExpr(const ExprPtr& e, Scope& scope) {
        auto args = EvalCallArgs(e->callArgs, scope);
        if (Failed()) return Value::Null();

        // 1. Array / Map instance helper methods (`arr.push(x)`, `arr.contains(x)`, `map.has(k)`, `map.keys()`)
        if (e->lhs->kind == ExprKind::Member && e->lhs->lhs) {
            if (e->lhs->lhs->kind == ExprKind::Ident) {
                const std::string& ns = e->lhs->lhs->strVal;
                if (IsBuiltInNamespace(ns)) {
                    return InvokeNamespaceMethod(ns, e->lhs->strVal, args, e->line);
                }
            }
            // Evaluate receiver object (`arr.push(...)`, `map.keys()`, `str.trim()`, etc.)
            Value recv = DerefIfNeeded(EvalExpr(e->lhs->lhs, scope));
            if (Failed()) return Value::Null();
            const std::string& meth = e->lhs->strVal;
            if (recv.kind == ValKind::Array && recv.arrVal) {
                if (meth == "push" || meth == "add") {
                    recv.arrVal->push_back(args.At(0));
                    return Value::FromInt(static_cast<std::int64_t>(recv.arrVal->size()));
                }
                if (meth == "pop" && !recv.arrVal->empty()) {
                    Value last = recv.arrVal->back();
                    recv.arrVal->pop_back();
                    return last;
                }
                if (meth == "contains") {
                    const std::string needle = args.At(0).AsString();
                    for (const auto& it : *recv.arrVal) {
                        if (it.AsString() == needle) return Value::FromBool(true);
                    }
                    return Value::FromBool(false);
                }
                if (meth == "join") {
                    const std::string sep = args.At(0, Value::FromStr(", ")).AsString();
                    std::ostringstream oss;
                    for (std::size_t i = 0; i < recv.arrVal->size(); ++i) {
                        if (i > 0) oss << sep;
                        oss << (*recv.arrVal)[i].AsString();
                    }
                    return Value::FromStr(oss.str());
                }
            }
            if (recv.kind == ValKind::Map && recv.mapVal) {
                if (meth == "has" || meth == "containsKey") {
                    return Value::FromBool(recv.mapVal->count(args.At(0).AsString()) > 0);
                }
                if (meth == "keys") {
                    std::vector<Value> ks;
                    for (const auto& [k, _] : *recv.mapVal) ks.push_back(Value::FromStr(k));
                    return Value::FromArray(std::move(ks));
                }
                if (meth == "remove") {
                    return Value::FromBool(recv.mapVal->erase(args.At(0).AsString()) > 0);
                }
            }
        }

        // 2. Global `std` functions & user-defined `fn` declarations!
        if (e->lhs->kind == ExprKind::Ident) {
            const std::string& fnName = e->lhs->strVal;
            Value stdRes;
            if (TryInvokeGlobalBuiltIn(fnName, args, e->line, stdRes)) {
                return stdRes;
            }
            for (const auto& fn : prog_.functions) {
                if (fn.name == fnName) {
                    Scope fnScope(&globalScope_);
                    for (std::size_t i = 0; i < fn.params.size(); ++i) {
                        // Preserve `Ref` if caller passed `ref myVar`!
                        Value argVal = (i < args.pos.size()) ? args.pos[i] : Value::Null();
                        fnScope.Declare(fn.params[i], std::move(argVal), false);
                    }
                    const FlowKind prevFlow = flow_;
                    ExecStmt(fn.body, fnScope);
                    if (flow_ == FlowKind::Return) {
                        Value ret = std::move(returnVal_);
                        returnVal_ = Value::Null();
                        flow_      = prevFlow;
                        return ret;
                    }
                    return Value::Null();
                }
            }
            RaiseError(e->line, "Unknown function '" + fnName + "()'.");
            return Value::Null();
        }

        RaiseError(e->line, "Expression is not a callable function.");
        return Value::Null();
    }

    static bool IsBuiltInNamespace(std::string_view ns) noexcept {
        return ns == "sys" || ns == "fs" || ns == "reg" || ns == "ini" || ns == "xml" ||
               ns == "json" || ns == "str" || ns == "regex" || ns == "archive" || ns == "crypto" ||
               ns == "net" || ns == "proc" || ns == "msi" || ns == "prereq" || ns == "shortcut" ||
               ns == "shell" || ns == "env" || ns == "arp" || ns == "svc" || ns == "task" ||
               ns == "firewall" || ns == "acl" || ns == "ui" || ns == "wizard" || ns == "showcase" ||
               ns == "log";
    }

    bool TryInvokeGlobalBuiltIn(const std::string& name, const EvaluatedArgs& args, int line, Value& out) {
        if (name == "str")    { out = Value::FromStr(args.At(0).AsString()); return true; }
        if (name == "int")    { out = Value::FromInt(args.At(0).AsInt()); return true; }
        if (name == "float")  { out = Value::FromFloat(args.At(0).AsFloat()); return true; }
        if (name == "bool")   { out = Value::FromBool(args.At(0).AsBool()); return true; }
        if (name == "ver")    { out = Value::FromVer(args.At(0).AsVersion()); return true; }
        if (name == "typeof") { out = Value::FromStr(std::string(args.At(0).TypeName())); return true; }
        if (name == "path") {
            std::filesystem::path p;
            for (std::size_t i = 0; i < args.pos.size(); ++i) {
                const std::string seg = args.At(i).AsString();
                if (i == 0) p = Utf8ToWide(seg);
                else p /= Utf8ToWide(seg);
            }
            out = Value::FromPath(p);
            return true;
        }
        if (name == "len") {
            const Value v = args.At(0);
            if (v.kind == ValKind::Array && v.arrVal) out = Value::FromInt(static_cast<std::int64_t>(v.arrVal->size()));
            else if (v.kind == ValKind::Map && v.mapVal) out = Value::FromInt(static_cast<std::int64_t>(v.mapVal->size()));
            else out = Value::FromInt(static_cast<std::int64_t>(v.AsString().size()));
            return true;
        }
        if (name == "print" || name == "log") {
            std::ostringstream oss;
            for (std::size_t i = 0; i < args.pos.size(); ++i) {
                if (i > 0) oss << " ";
                oss << args.At(i).AsString();
            }
            const std::string ln = oss.str();
            AppendLogLine("INFO", ln);
            out = Value::Null();
            return true;
        }
        if (name == "assert") {
            if (!args.At(0).AsBool()) {
                const std::string msg = args.At(1, Value::FromStr("Assertion failed")).AsString();
                RaiseError(line, msg);
            }
            out = Value::FromBool(!Failed());
            return true;
        }
        if (name == "sleep") {
            const auto ms = std::clamp<std::int64_t>(args.At(0).AsInt(), 0, 60000);
            if (ms > 0) ::Sleep(static_cast<DWORD>(ms));
            out = Value::Null();
            return true;
        }
        if (name == "coalesce") {
            for (std::size_t i = 0; i < args.pos.size(); ++i) {
                Value v = args.At(i);
                if (v.kind != ValKind::Null && !(v.kind == ValKind::Str && v.strVal.empty())) {
                    out = v;
                    return true;
                }
            }
            out = Value::Null();
            return true;
        }
        if (name == "range") {
            const std::int64_t start = (args.pos.size() >= 2) ? args.At(0).AsInt() : 0;
            const std::int64_t end   = (args.pos.size() >= 2) ? args.At(1).AsInt() : args.At(0).AsInt();
            std::vector<Value> items;
            for (std::int64_t k = start; k < end && items.size() < 100000; ++k) {
                items.push_back(Value::FromInt(k));
            }
            out = Value::FromArray(std::move(items));
            return true;
        }
        return false;
    }

    Value InvokeNamespaceMethod(const std::string& ns, const std::string& m, const EvaluatedArgs& args, int line) {
        // Runtime safety defense-in-depth enforcing `RSW701` (Wizard Phase Isolation) even on dynamic calls!
        if (inWizardPhase_ && detail::IsMutatingHostCall(ns, m)) {
            RaiseError(line, "RSW701 Phase isolation violation: '" + ns + "." + m + "()' cannot mutate system state inside wizard { ... }.");
            return Value::Null();
        }

        if (ns == "wizard") {
            if (m == "back") { flow_ = FlowKind::WizardBack; return Value::Null(); }
            if (m == "next") { flow_ = FlowKind::WizardNext; return Value::Null(); }
            if (m == "cancel") {
                exitCode_ = ERROR_INSTALL_USEREXIT;
                flow_     = FlowKind::ExitSetup;
                return Value::Null();
            }
        }

        if (ns == "sys")      return CallSys(m, args, line);
        if (ns == "fs")       return CallFs(m, args, line);
        if (ns == "reg")      return CallReg(m, args, line);
        if (ns == "ini")      return CallIni(m, args, line);
        if (ns == "xml")      return CallXml(m, args, line);
        if (ns == "json")     return CallJson(m, args);
        if (ns == "str")      return CallStr(m, args);
        if (ns == "regex")    return CallRegex(m, args, line);
        if (ns == "archive")  return CallArchive(m, args, line);
        if (ns == "crypto")   return CallCrypto(m, args);
        if (ns == "net")      return CallNet(m, args, line);
        if (ns == "proc")     return CallProc(m, args, line);
        if (ns == "msi")      return CallMsi(m, args, line);
        if (ns == "prereq")   return CallPrereq(m, args, line);
        if (ns == "shortcut") return CallShortcut(m, args, line);
        if (ns == "shell")    return CallShell(m, args, line);
        if (ns == "env")      return CallEnv(m, args, line);
        if (ns == "arp")      return CallArp(m, args, line);
        if (ns == "svc")      return CallSvc(m, args, line);
        if (ns == "task")     return CallTask(m, args, line);
        if (ns == "firewall") return CallFirewall(m, args, line);
        if (ns == "acl")      return CallAcl(m, args, line);
        if (ns == "ui")       return CallUi(m, args);
        if (ns == "showcase") return CallShowcase(m, args);
        if (ns == "log") {
            std::string lvl = "INFO";
            if (m == "warn" || m == "warning") lvl = "WARN";
            else if (m == "error") lvl = "ERROR";
            AppendLogLine(lvl, args.At(0).AsString());
            return Value::Null();
        }

        RaiseError(line, "Unknown host call '" + ns + "." + m + "()'.");
        return Value::Null();
    }

    void AppendLogLine(std::string_view level, const std::string& msg) {
        consoleLines_.push_back(msg);
        if (opts_.onStdout) opts_.onStdout(msg);
        if (!opts_.logFilePath.empty()) {
            std::error_code ec;
            std::filesystem::create_directories(opts_.logFilePath.parent_path(), ec);
            std::ofstream lf(opts_.logFilePath, std::ios::binary | std::ios::app);
            if (lf) {
                SYSTEMTIME st{};
                ::GetLocalTime(&st);
                char ts[32]{};
                std::snprintf(ts, sizeof(ts), "%04u-%02u-%02u %02u:%02u:%02u",
                              st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
                lf << "[" << ts << "][" << level << "] " << msg << "\n";
            }
        }
    }

    // ----------------------------------------------------------------------------
    // Namespace 1: `sys.*`
    // ----------------------------------------------------------------------------
    Value CallSys(const std::string& m, const EvaluatedArgs& args, int line) {
        const auto hp   = QueryHostPlatform();
        const auto priv = QueryProcessPrivilegeState();

        if (m == "arch")                  return Value::FromStr(std::string(hp.NativeArchSlug()));
        if (m == "compiledArch")          return Value::FromStr(std::string(hp.CompiledArchSlug()));
        if (m == "isArm64")               return Value::FromBool(hp.nativeMachine == IMAGE_FILE_MACHINE_ARM64);
        if (m == "isX64")                 return Value::FromBool(hp.nativeMachine == IMAGE_FILE_MACHINE_AMD64);
        if (m == "isEmulatedUnderPrism")  return Value::FromBool(hp.IsEmulatedProcess());
        if (m == "osBuild")               return Value::FromInt(static_cast<std::int64_t>(hp.buildNumber));
        if (m == "osVersion") {
            SemVer sv{};
            sv.parts[0] = static_cast<int>(hp.majorVersion);
            sv.parts[1] = static_cast<int>(hp.minorVersion);
            sv.parts[2] = static_cast<int>(hp.buildNumber);
            return Value::FromVer(sv);
        }
        if (m == "isWindows11")           return Value::FromBool(hp.MeetsMinimumWindowsBuild(22000));
        if (m == "isServer") {
            std::string instType;
            ReadRegValueStringView(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                                   L"InstallationType", KEY_WOW64_64KEY, instType);
            return Value::FromBool(instType.find("Server") != std::string::npos);
        }
        if (m == "isAdmin")               return Value::FromBool(priv.HasEffectiveAdminRights());
        if (m == "uacEnabled")            return Value::FromBool(priv.isUacEnabled);
        if (m == "canPromptUac")          return Value::FromBool(priv.CanPromptForUacElevation());
        if (m == "isSilent")              return Value::FromBool(opts_.silentMode);
        if (m == "isPassive")             return Value::FromBool(opts_.passiveMode);
        if (m == "isMaintenance")         return Value::FromBool(opts_.maintenanceMode);
        if (m == "cmdLine")               return Value::FromStr(WideToUtf8(::GetCommandLineW()));
        if (m == "cliArg") {
            const std::string key = args.At(0).AsString();
            for (const auto& [k, v] : opts_.cliProperties) {
                if (EqualsIgnoreCase(k, key)) return Value::FromStr(v);
            }
            return args.At(1, Value::FromStr(""));
        }
        if (m == "hasSwitch") {
            const std::string sw = args.At(0).AsString();
            for (const auto& s : opts_.rawCliSwitches) {
                if (EqualsIgnoreCase(s, sw)) return Value::FromBool(true);
            }
            return Value::FromBool(false);
        }
        if (m == "ramMB") {
            MEMORYSTATUSEX ms{};
            ms.dwLength = sizeof(ms);
            ::GlobalMemoryStatusEx(&ms);
            return Value::FromInt(static_cast<std::int64_t>(ms.ullTotalPhys / (1024ull * 1024ull)));
        }
        if (m == "cpuCores") {
            SYSTEM_INFO si{};
            ::GetNativeSystemInfo(&si);
            return Value::FromInt(static_cast<std::int64_t>(si.dwNumberOfProcessors));
        }
        if (m == "freeDiskBytes" || m == "freeDiskMB") {
            std::filesystem::path probe = args.pos.empty()
                ? std::filesystem::path(L"C:\\")
                : args.At(0).AsPath();
            std::error_code ec;
            while (!probe.empty() && !std::filesystem::exists(probe, ec)) {
                const auto par = probe.parent_path();
                if (par == probe) break;
                probe = par;
            }
            ULARGE_INTEGER avail{}, total{}, freeTot{};
            ::GetDiskFreeSpaceExW(probe.empty() ? L"C:\\" : probe.wstring().c_str(), &avail, &total, &freeTot);
            const std::uint64_t bytes = avail.QuadPart;
            return Value::FromInt(static_cast<std::int64_t>(m == "freeDiskMB" ? (bytes / (1024ull * 1024ull)) : bytes));
        }
        if (m == "screenWidth")           return Value::FromInt(::GetSystemMetrics(SM_CXSCREEN));
        if (m == "screenHeight")          return Value::FromInt(::GetSystemMetrics(SM_CYSCREEN));
        if (m == "dpi")                   return Value::FromInt(::GetDpiForSystem());
        if (m == "isHighContrast") {
            HIGHCONTRASTW hc{sizeof(hc)};
            ::SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(hc), &hc, 0);
            return Value::FromBool((hc.dwFlags & HCF_HIGHCONTRASTON) != 0);
        }
        if (m == "isDarkMode") {
            std::string v;
            if (ReadRegValueStringView(HKEY_CURRENT_USER,
                    L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                    L"AppsUseLightTheme", 0, v)) {
                return Value::FromBool(v == "0");
            }
            return Value::FromBool(false);
        }
        if (m == "locale") {
            wchar_t locBuf[LOCALE_NAME_MAX_LENGTH]{};
            ::GetUserDefaultLocaleName(locBuf, LOCALE_NAME_MAX_LENGTH);
            return Value::FromStr(WideToUtf8(locBuf));
        }
        if (m == "computerName") {
            wchar_t buf[MAX_COMPUTERNAME_LENGTH + 4]{};
            DWORD sz = static_cast<DWORD>(std::size(buf));
            ::GetComputerNameW(buf, &sz);
            return Value::FromStr(WideToUtf8(buf));
        }
        if (m == "userName") {
            wchar_t buf[256]{};
            DWORD sz = static_cast<DWORD>(std::size(buf));
            ::GetUserNameW(buf, &sz);
            return Value::FromStr(WideToUtf8(buf));
        }
        if (m == "userSid") {
            HANDLE hTok = nullptr;
            std::string sidStr;
            if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hTok)) {
                DWORD cb = 0;
                ::GetTokenInformation(hTok, TokenUser, nullptr, 0, &cb);
                std::vector<BYTE> buf(cb);
                if (cb > 0 && ::GetTokenInformation(hTok, TokenUser, buf.data(), cb, &cb)) {
                    LPWSTR wSid = nullptr;
                    if (::ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &wSid) && wSid) {
                        sidStr = WideToUtf8(wSid);
                        ::LocalFree(wSid);
                    }
                }
                ::CloseHandle(hTok);
            }
            return Value::FromStr(std::move(sidStr));
        }
        if (m == "newGuid")               return Value::FromStr(GenerateRandomMsiGuid());
        if (m == "uuid5")                 return Value::FromStr(ComputeDeterministicComponentGuid(args.At(0).AsString(), hp.NativeArchSlug(), args.At(1).AsString()));
        if (m == "editionId") {
            std::string ed;
            ReadRegValueStringView(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                                   L"EditionID", KEY_WOW64_64KEY, ed);
            return Value::FromStr(ed);
        }
        if (m == "machineGuid") {
            std::string mg;
            ReadRegValueStringView(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography",
                                   L"MachineGuid", KEY_WOW64_64KEY, mg);
            return Value::FromStr(mg);
        }
        if (m == "rebootLatch")           { rebootNeeded_ = args.At(0, Value::FromBool(true)).AsBool(); return Value::FromBool(rebootNeeded_); }
        if (m == "needsReboot")           return Value::FromBool(rebootNeeded_);
        if (m == "scheduleResumeAfterReboot") {
            // Writes `HKCU\Software\Microsoft\Windows\CurrentVersion\RunOnce\<key>` to resume setup post-reboot!
            const std::string keyName = args.At(0, Value::FromStr("RaggieSoftSetupResume")).AsString();
            const std::string cmd     = args.At(1).AsString();
            EvaluatedArgs regArgs;
            regArgs.pos = {Value::FromStr("HKCU"), Value::FromStr("Software\\Microsoft\\Windows\\CurrentVersion\\RunOnce"),
                           Value::FromStr(keyName), Value::FromStr(cmd)};
            return CallReg("writeString", regArgs, line);
        }

        RaiseError(line, "Unknown sys.* method 'sys." + m + "()'.");
        return Value::Null();
    }

    // ----------------------------------------------------------------------------
    // Namespace 2: `fs.*`
    // ----------------------------------------------------------------------------
    Value CallFs(const std::string& m, const EvaluatedArgs& args, int line) {
        std::error_code ec;
        if (m == "exists")  return Value::FromBool(std::filesystem::exists(args.At(0).AsPath(), ec));
        if (m == "isDir")   return Value::FromBool(std::filesystem::is_directory(args.At(0).AsPath(), ec));
        if (m == "isFile")  return Value::FromBool(std::filesystem::is_regular_file(args.At(0).AsPath(), ec));
        if (m == "size") {
            const auto sz = std::filesystem::file_size(args.At(0).AsPath(), ec);
            return Value::FromInt(ec ? 0 : static_cast<std::int64_t>(sz));
        }
        if (m == "isLocked") {
            const auto p = args.At(0).AsPath();
            if (!std::filesystem::exists(p, ec)) return Value::FromBool(false);
            HANDLE h = ::CreateFileW(p.wstring().c_str(), GENERIC_READ | GENERIC_WRITE, 0 /* exclusive */,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h == INVALID_HANDLE_VALUE) {
                const DWORD gle = ::GetLastError();
                return Value::FromBool(gle == ERROR_SHARING_VIOLATION || gle == ERROR_LOCK_VIOLATION);
            }
            ::CloseHandle(h);
            return Value::FromBool(false);
        }
        if (m == "fileVersion") {
            const auto pe = HarvestPeVersionAndLanguage(args.At(0).AsPath());
            return Value::FromVer(SemVer::Parse(pe.version));
        }
        if (m == "join") {
            std::filesystem::path p;
            for (std::size_t i = 0; i < args.pos.size(); ++i) {
                if (i == 0) p = args.At(i).AsPath();
                else p /= Utf8ToWide(args.At(i).AsString());
            }
            return Value::FromPath(p);
        }
        if (m == "parent")   return Value::FromPath(args.At(0).AsPath().parent_path());
        if (m == "fileName") return Value::FromStr(WideToUtf8(args.At(0).AsPath().filename().wstring()));
        if (m == "stem")     return Value::FromStr(WideToUtf8(args.At(0).AsPath().stem().wstring()));
        if (m == "ext")      return Value::FromStr(WideToUtf8(args.At(0).AsPath().extension().wstring()));
        if (m == "tempDir") {
            const std::string pfx = args.At(0, Value::FromStr("rssc")).AsString();
            const auto td = std::filesystem::temp_directory_path(ec) /
                            Utf8ToWide(pfx + "-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(::GetTickCount64() & 0xFFFFu));
            std::filesystem::create_directories(td, ec);
            return Value::FromPath(td);
        }
        if (m == "tempFile") {
            const std::string pfx = args.At(0, Value::FromStr("rssc")).AsString();
            const std::string ext = args.At(1, Value::FromStr(".tmp")).AsString();
            const auto tf = std::filesystem::temp_directory_path(ec) /
                            Utf8ToWide(pfx + "-" + std::to_string(::GetCurrentProcessId()) + "-" + std::to_string(::GetTickCount64() & 0xFFFFFFu) + ext);
            return Value::FromPath(tf);
        }
        if (m == "findFiles") {
            const auto dir = args.At(0).AsPath();
            const std::string pat = args.At(1, Value::FromStr("*")).AsString();
            const bool rec = args.At(2, Value::FromBool(true)).AsBool();
            std::vector<Value> found;
            if (std::filesystem::is_directory(dir, ec)) {
                auto matchPat = [&](const std::filesystem::path& fp) {
                    if (pat.empty() || pat == "*" || pat == "*.*") return true;
                    if (pat.size() >= 2 && pat.front() == '*' && pat[1] == '.') {
                        return EqualsIgnoreCase(WideToUtf8(fp.extension().wstring()), pat.substr(1));
                    }
                    return WideToUtf8(fp.filename().wstring()).find(pat) != std::string::npos;
                };
                if (rec) {
                    for (const auto& de : std::filesystem::recursive_directory_iterator(dir, ec)) {
                        if (de.is_regular_file(ec) && matchPat(de.path())) found.push_back(Value::FromPath(de.path()));
                    }
                } else {
                    for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
                        if (de.is_regular_file(ec) && matchPat(de.path())) found.push_back(Value::FromPath(de.path()));
                    }
                }
            }
            return Value::FromArray(std::move(found));
        }
        if (m == "readText") {
            std::ifstream in(args.At(0).AsPath(), std::ios::binary);
            if (!in) return Value::FromStr("");
            std::ostringstream ss;
            ss << in.rdbuf();
            return Value::FromStr(ss.str());
        }
        if (m == "readLines") {
            std::ifstream in(args.At(0).AsPath(), std::ios::binary);
            std::vector<Value> lines;
            std::string ln;
            while (std::getline(in, ln)) {
                if (!ln.empty() && ln.back() == '\r') ln.pop_back();
                lines.push_back(Value::FromStr(std::move(ln)));
            }
            return Value::FromArray(std::move(lines));
        }
        if (m == "readJson") {
            std::ifstream in(args.At(0).AsPath(), std::ios::binary);
            if (!in) return Value::Null();
            std::ostringstream ss;
            ss << in.rdbuf();
            const std::string txt = ss.str();
            json_detail::JsonReader jr{txt, 0};
            return jr.ParseValue();
        }
        if (m == "mkdir") {
            std::string fErr;
            const auto d = args.At(0).AsPath();
            if (!ledger_.EnsureDirectoryTracked(d)) {
                RaiseError(line, "Could not create directory: " + WideToUtf8(d.wstring()));
                return Value::FromBool(false);
            }
            if (!ledger_.CheckFaultTrigger(fErr)) { RaiseError(line, fErr); return Value::FromBool(false); }
            return Value::FromBool(true);
        }
        if (m == "writeText" || m == "appendText" || m == "writeJson") {
            const auto dst = args.At(0).AsPath();
            std::string content;
            if (m == "writeJson") {
                std::ostringstream jss;
                json_detail::StringifyJsonRec(jss, args.At(1), static_cast<int>(args.At(2, Value::FromInt(2)).AsInt()), 0);
                content = jss.str();
            } else {
                content = args.At(1).AsString();
            }

            if (m == "appendText" && std::filesystem::is_regular_file(dst, ec)) {
                std::ifstream prev(dst, std::ios::binary);
                std::ostringstream pss;
                pss << prev.rdbuf();
                content = pss.str() + content;
            }
            if (!ledger_.PrepareFileWriteBackup(dst)) {
                RaiseError(line, "Failed preparing parent directory for: " + WideToUtf8(dst.wstring()));
                return Value::FromBool(false);
            }
            std::ofstream out(dst, std::ios::binary | std::ios::trunc);
            if (!out) {
                RaiseError(line, "Could not write file: " + WideToUtf8(dst.wstring()));
                return Value::FromBool(false);
            }
            out.write(content.data(), static_cast<std::streamsize>(content.size()));
            out.close();
            std::string fErr;
            if (!ledger_.CheckFaultTrigger(fErr)) { RaiseError(line, fErr); return Value::FromBool(false); }
            return Value::FromBool(true);
        }
        if (m == "copy") {
            const auto src = args.At(0).AsPath();
            const auto dst = args.At(1).AsPath();
            const bool ovr = args.NamedOrPos("overwrite", 2, Value::FromBool(true)).AsBool();
            if (!ovr && std::filesystem::exists(dst, ec)) return Value::FromBool(true);
            if (!ledger_.PrepareFileWriteBackup(dst)) {
                RaiseError(line, "Failed preparing destination directory for copy: " + WideToUtf8(dst.wstring()));
                return Value::FromBool(false);
            }
            if (!std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec)) {
                RaiseError(line, "fs.copy failed from '" + WideToUtf8(src.wstring()) + "' to '" + WideToUtf8(dst.wstring()) + "'.");
                return Value::FromBool(false);
            }
            std::string fErr;
            if (!ledger_.CheckFaultTrigger(fErr)) { RaiseError(line, fErr); return Value::FromBool(false); }
            return Value::FromBool(true);
        }
        if (m == "copyTree") {
            const auto srcDir = args.At(0).AsPath();
            const auto dstDir = args.At(1).AsPath();
            if (!std::filesystem::is_directory(srcDir, ec)) {
                RaiseError(line, "fs.copyTree source directory does not exist: " + WideToUtf8(srcDir.wstring()));
                return Value::FromBool(false);
            }
            for (const auto& de : std::filesystem::recursive_directory_iterator(srcDir, ec)) {
                const auto rel = std::filesystem::relative(de.path(), srcDir, ec);
                const auto target = (dstDir / rel).lexically_normal();
                if (de.is_directory(ec)) {
                    ledger_.EnsureDirectoryTracked(target);
                } else if (de.is_regular_file(ec)) {
                    ledger_.PrepareFileWriteBackup(target);
                    if (!std::filesystem::copy_file(de.path(), target, std::filesystem::copy_options::overwrite_existing, ec)) {
                        RaiseError(line, "fs.copyTree failed copying: " + WideToUtf8(target.wstring()));
                        return Value::FromBool(false);
                    }
                    std::string fErr;
                    if (!ledger_.CheckFaultTrigger(fErr)) { RaiseError(line, fErr); return Value::FromBool(false); }
                }
            }
            return Value::FromBool(true);
        }
        if (m == "move") {
            const auto src = args.At(0).AsPath();
            const auto dst = args.At(1).AsPath();
            ledger_.PrepareFileWriteBackup(dst);
            std::filesystem::rename(src, dst, ec);
            if (ec) {
                std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
                if (!ec) std::filesystem::remove(src, ec);
            }
            return Value::FromBool(!ec);
        }
        if (m == "remove") {
            const auto p = args.At(0).AsPath();
            if (std::filesystem::is_regular_file(p, ec)) {
                if (ledger_.active) {
                    const auto bak = std::filesystem::path(p.wstring() + L".rswbak.rm." + std::to_wstring(::GetCurrentProcessId()));
                    std::filesystem::rename(p, bak, ec);
                    if (!ec) ledger_.backedUpFiles.push_back({p, bak});
                    else std::filesystem::remove(p, ec);
                } else {
                    std::filesystem::remove(p, ec);
                }
            }
            return Value::FromBool(true);
        }
        if (m == "removeTree" || m == "removeTreeIfExists") {
            const auto p = args.At(0).AsPath();
            if (std::filesystem::exists(p, ec)) {
                std::filesystem::remove_all(p, ec);
            }
            return Value::FromBool(!ec);
        }
        if (m == "setAttributes") {
            const auto p = args.At(0).AsPath();
            const bool hid = args.NamedOrPos("hidden",   1, Value::FromBool(false)).AsBool();
            const bool ro  = args.NamedOrPos("readOnly", 2, Value::FromBool(false)).AsBool();
            const bool sys = args.NamedOrPos("system",   3, Value::FromBool(false)).AsBool();
            DWORD attr = FILE_ATTRIBUTE_NORMAL;
            if (hid || ro || sys) {
                attr = 0;
                if (hid) attr |= FILE_ATTRIBUTE_HIDDEN;
                if (ro)  attr |= FILE_ATTRIBUTE_READONLY;
                if (sys) attr |= FILE_ATTRIBUTE_SYSTEM;
            }
            return Value::FromBool(::SetFileAttributesW(p.wstring().c_str(), attr) != FALSE);
        }
        if (m == "setNtfsCompression") {
            const auto p = args.At(0).AsPath();
            const bool enable = args.At(1, Value::FromBool(true)).AsBool();
            HANDLE h = ::CreateFileW(
                p.wstring().c_str(), GENERIC_READ | GENERIC_WRITE,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
            if (h == INVALID_HANDLE_VALUE) return Value::FromBool(false);
            USHORT fmt = enable ? COMPRESSION_FORMAT_DEFAULT : COMPRESSION_FORMAT_NONE;
            DWORD bytesRet = 0;
            const BOOL ok = ::DeviceIoControl(h, FSCTL_SET_COMPRESSION, &fmt, sizeof(fmt), nullptr, 0, &bytesRet, nullptr);
            ::CloseHandle(h);
            return Value::FromBool(ok != FALSE);
        }
        if (m == "extract") {
            // Unpacks embedded `RSWPAK1` payload (`#220`) matching `payloadId` (or `"*"` for all)!
            const std::string wantId = args.At(0, Value::FromStr("*")).AsString();
            std::filesystem::path dstBase;
            if (args.pos.size() >= 2 || args.named.count("to") || args.named.count("dest")) {
                dstBase = args.NamedOrPos("to", 1, args.NamedOrPos("dest", 1)).AsPath();
            } else {
                Value dirV;
                if (globalScope_.TryGet("installDir", dirV)) dstBase = dirV.AsPath();
            }
            if (dstBase.empty()) {
                RaiseError(line, "fs.extract requires an installDir or destination folder.");
                return Value::FromInt(0);
            }

            // If running directly from `.rssc` source (`raggiescript run` before packing `#220`),
            // seamlessly fall back to copying matching `payload <id> { from "..." }` declarations from disk!
            if (!opts_.embeddedPakBytes || opts_.embeddedPakSize == 0) {
                std::int64_t copiedCount = 0;
                const std::filesystem::path srcBase = prog_.sourcePath.empty()
                    ? std::filesystem::current_path()
                    : std::filesystem::path(Utf8ToWide(prog_.sourcePath)).parent_path();
                for (const auto& pl : prog_.payloads) {
                    if (wantId != "*" && !EqualsIgnoreCase(pl.id, wantId) && !EqualsIgnoreCase(pl.featureId, wantId)) {
                        continue;
                    }
                    const std::filesystem::path rawSrc(Utf8ToWide(ExpandArchToken(pl.sourceGlob, QueryHostPlatform().NativeArchSlug())));
                    const std::filesystem::path absSrc = (rawSrc.is_absolute() ? rawSrc : (srcBase / rawSrc)).lexically_normal();
                    if (std::filesystem::is_regular_file(absSrc, ec)) {
                        const auto target = (dstBase / absSrc.filename()).lexically_normal();
                        ledger_.PrepareFileWriteBackup(target);
                        if (!std::filesystem::copy_file(absSrc, target, std::filesystem::copy_options::overwrite_existing, ec)) {
                            RaiseError(line, "fs.extract source copy failed: " + WideToUtf8(absSrc.wstring()));
                            return Value::FromInt(copiedCount);
                        }
                        ++copiedCount;
                        std::string fErr;
                        if (!ledger_.CheckFaultTrigger(fErr)) { RaiseError(line, fErr); return Value::FromInt(copiedCount); }
                    } else if (std::filesystem::is_directory(absSrc, ec)) {
                        for (const auto& de : std::filesystem::recursive_directory_iterator(absSrc, ec)) {
                            if (!de.is_regular_file(ec)) continue;
                            const auto rel = std::filesystem::relative(de.path(), absSrc, ec);
                            const auto target = (dstBase / rel).lexically_normal();
                            ledger_.PrepareFileWriteBackup(target);
                            if (!std::filesystem::copy_file(de.path(), target, std::filesystem::copy_options::overwrite_existing, ec)) {
                                RaiseError(line, "fs.extract directory copy failed: " + WideToUtf8(target.wstring()));
                                return Value::FromInt(copiedCount);
                            }
                            ++copiedCount;
                            std::string fErr;
                            if (!ledger_.CheckFaultTrigger(fErr)) { RaiseError(line, fErr); return Value::FromInt(copiedCount); }
                        }
                    }
                }
                return Value::FromInt(copiedCount);
            }

            std::vector<RswPakFileMetadata> metas;
            std::vector<std::uint8_t> pakPayload;
            std::string pakErr;
            if (!UnpackRswPakBytesInMemory(opts_.embeddedPakBytes, opts_.embeddedPakSize, metas, pakPayload, pakErr)) {
                RaiseError(line, "fs.extract failed unpacking embedded RSWPAK1 payload: " + pakErr);
                return Value::FromInt(0);
            }
            std::int64_t written = 0;
            const std::size_t tot = std::max<std::size_t>(1, metas.size());
            for (std::size_t i = 0; i < metas.size(); ++i) {
                const auto& meta = metas[i];
                // Skip `_support/` internal files unless explicitly requested
                if (meta.featureId == "__support__" && wantId != "__support__") continue;
                if (wantId != "*" && !EqualsIgnoreCase(meta.featureId, wantId)) continue;

                std::string rel = meta.archiveRelPath;
                if (rel.rfind("INSTALLDIR/", 0) == 0) rel.erase(0, 11);
                const auto dstFile = (dstBase / Utf8ToWide(rel)).lexically_normal();
                if (!ledger_.PrepareFileWriteBackup(dstFile)) {
                    RaiseError(line, "fs.extract could not create directory for: " + WideToUtf8(dstFile.wstring()));
                    return Value::FromInt(written);
                }
                std::ofstream out(dstFile, std::ios::binary | std::ios::trunc);
                if (!out) {
                    RaiseError(line, "fs.extract could not write file: " + WideToUtf8(dstFile.wstring()));
                    return Value::FromInt(written);
                }
                if (meta.payloadSize > 0) {
                    out.write(reinterpret_cast<const char*>(pakPayload.data() + meta.payloadOffset),
                              static_cast<std::streamsize>(meta.payloadSize));
                }
                out.close();
                ++written;
                if (opts_.onProgress) {
                    const int perm = static_cast<int>(((i + 1) * 850u) / tot);
                    opts_.onProgress(perm, perm, L"Installing: " + dstFile.filename().wstring());
                }
                std::string fErr;
                if (!ledger_.CheckFaultTrigger(fErr)) { RaiseError(line, fErr); return Value::FromInt(written); }
            }
            return Value::FromInt(written);
        }

        RaiseError(line, "Unknown fs.* method 'fs." + m + "()'.");
        return Value::Null();
    }

    // ----------------------------------------------------------------------------
    // Namespace 3: `reg.*` (Stateless `native64` / `wow32` / `both` Registry Views!)
    // ----------------------------------------------------------------------------
    static HKEY ParseHive(std::string_view h, std::string& outCanonicalRoot) noexcept {
        if (EqualsIgnoreCase(h, "HKCU") || EqualsIgnoreCase(h, "HKEY_CURRENT_USER")) {
            outCanonicalRoot = "HKCU";
            return HKEY_CURRENT_USER;
        }
        if (EqualsIgnoreCase(h, "HKCR") || EqualsIgnoreCase(h, "HKEY_CLASSES_ROOT")) {
            outCanonicalRoot = "HKCR";
            return HKEY_CLASSES_ROOT;
        }
        if (EqualsIgnoreCase(h, "HKU") || EqualsIgnoreCase(h, "HKEY_USERS")) {
            outCanonicalRoot = "HKU";
            return HKEY_USERS;
        }
        outCanonicalRoot = "HKLM";
        return HKEY_LOCAL_MACHINE;
    }

    static std::vector<REGSAM> ParseRegViews(std::string_view viewName) {
        if (EqualsIgnoreCase(viewName, "wow32") || EqualsIgnoreCase(viewName, "32")) {
            return {KEY_WOW64_32KEY};
        }
        if (EqualsIgnoreCase(viewName, "both") || EqualsIgnoreCase(viewName, "any")) {
            return {KEY_WOW64_64KEY, KEY_WOW64_32KEY};
        }
        return {KEY_WOW64_64KEY};
    }

    Value CallReg(const std::string& m, const EvaluatedArgs& args, int line) {
        std::string rootCode;
        const HKEY hRoot = ParseHive(args.At(0).AsString(), rootCode);
        const std::wstring subKey = Utf8ToWide(args.At(1).AsString());
        const std::filesystem::path virtReg = native_detail::GetVirtualRegistryStoreRoot();

        if (m == "exists") {
            const std::wstring valName = Utf8ToWide(args.NamedOrPos("value", 2, Value::FromStr("")).AsString());
            const std::string viewStr  = args.NamedOrPos("view", 3, Value::FromStr("native64")).AsString();
            if (!virtReg.empty()) {
                std::error_code ec;
                const auto dir = native_detail::ResolveVirtualRegKeyDir(virtReg, rootCode, subKey);
                if (valName.empty()) return Value::FromBool(std::filesystem::is_directory(dir, ec));
                std::string dummy;
                return Value::FromBool(native_detail::VirtualRegReadValue(virtReg, rootCode, subKey, valName, dummy));
            }
            for (REGSAM wf : ParseRegViews(viewStr)) {
                HKEY hk = nullptr;
                if (::RegOpenKeyExW(hRoot, subKey.c_str(), 0, KEY_READ | wf, &hk) == ERROR_SUCCESS) {
                    if (valName.empty()) { ::RegCloseKey(hk); return Value::FromBool(true); }
                    const LSTATUS st = ::RegQueryValueExW(hk, valName.c_str(), nullptr, nullptr, nullptr, nullptr);
                    ::RegCloseKey(hk);
                    if (st == ERROR_SUCCESS) return Value::FromBool(true);
                }
            }
            return Value::FromBool(false);
        }

        if (m == "readString" || m == "readDword") {
            const std::wstring valName = Utf8ToWide(args.At(2, Value::FromStr("")).AsString());
            const Value defVal         = (m == "readDword")
                ? args.NamedOrPos("default", 3, Value::FromInt(0))
                : args.NamedOrPos("default", 3, Value::FromStr(""));
            const std::string viewStr  = args.NamedOrPos("view", 4, Value::FromStr("native64")).AsString();

            if (!virtReg.empty()) {
                std::string v;
                if (native_detail::VirtualRegReadValue(virtReg, rootCode, subKey, valName, v)) {
                    return (m == "readDword") ? Value::FromInt(std::strtoll(v.c_str(), nullptr, 0)) : Value::FromStr(v);
                }
                return defVal;
            }
            for (REGSAM wf : ParseRegViews(viewStr)) {
                HKEY hk = nullptr;
                if (::RegOpenKeyExW(hRoot, subKey.c_str(), 0, KEY_READ | wf, &hk) != ERROR_SUCCESS) continue;
                DWORD dwType = 0, cb = 0;
                if (::RegQueryValueExW(hk, valName.empty() ? nullptr : valName.c_str(), nullptr, &dwType, nullptr, &cb) == ERROR_SUCCESS && cb > 0) {
                    std::vector<BYTE> buf(cb + sizeof(wchar_t), 0);
                    if (::RegQueryValueExW(hk, valName.empty() ? nullptr : valName.c_str(), nullptr, &dwType, buf.data(), &cb) == ERROR_SUCCESS) {
                        ::RegCloseKey(hk);
                        if (dwType == REG_DWORD && cb >= sizeof(DWORD)) {
                            DWORD d = 0;
                            std::memcpy(&d, buf.data(), sizeof(d));
                            return (m == "readDword") ? Value::FromInt(d) : Value::FromStr(std::to_string(d));
                        }
                        if (dwType == REG_SZ || dwType == REG_EXPAND_SZ) {
                            const std::string s = WideToUtf8(reinterpret_cast<const wchar_t*>(buf.data()));
                            return (m == "readDword") ? Value::FromInt(std::strtoll(s.c_str(), nullptr, 0)) : Value::FromStr(s);
                        }
                    }
                }
                ::RegCloseKey(hk);
            }
            return defVal;
        }

        if (m == "listSubkeys" || m == "listValues") {
            const std::string viewStr = args.NamedOrPos("view", 2, Value::FromStr("native64")).AsString();
            std::vector<Value> out;
            std::set<std::string> seen;
            if (!virtReg.empty()) {
                std::error_code ec;
                const auto dir = native_detail::ResolveVirtualRegKeyDir(virtReg, rootCode, subKey);
                if (std::filesystem::is_directory(dir, ec)) {
                    for (const auto& de : std::filesystem::directory_iterator(dir, ec)) {
                        if (m == "listSubkeys" && de.is_directory(ec)) {
                            out.push_back(Value::FromStr(WideToUtf8(de.path().filename().wstring())));
                        } else if (m == "listValues" && de.is_regular_file(ec) && de.path().extension() == L".regval") {
                            out.push_back(Value::FromStr(WideToUtf8(de.path().stem().wstring())));
                        }
                    }
                }
                return Value::FromArray(std::move(out));
            }
            for (REGSAM wf : ParseRegViews(viewStr)) {
                HKEY hk = nullptr;
                if (::RegOpenKeyExW(hRoot, subKey.c_str(), 0, KEY_READ | wf, &hk) != ERROR_SUCCESS) continue;
                wchar_t nameBuf[512]{};
                for (DWORD i = 0;; ++i) {
                    DWORD cch = static_cast<DWORD>(std::size(nameBuf));
                    const LSTATUS st = (m == "listSubkeys")
                        ? ::RegEnumKeyExW(hk, i, nameBuf, &cch, nullptr, nullptr, nullptr, nullptr)
                        : ::RegEnumValueW(hk, i, nameBuf, &cch, nullptr, nullptr, nullptr, nullptr);
                    if (st != ERROR_SUCCESS) break;
                    const std::string s = WideToUtf8(std::wstring_view(nameBuf, cch));
                    if (seen.insert(s).second) out.push_back(Value::FromStr(s));
                }
                ::RegCloseKey(hk);
            }
            return Value::FromArray(std::move(out));
        }

        if (m == "writeString" || m == "writeExpandString" || m == "writeDword") {
            const std::wstring valName = Utf8ToWide(args.At(2).AsString());
            const Value valObj         = args.At(3);
            const std::string viewStr  = args.NamedOrPos("view", 4, Value::FromStr("native64")).AsString();
            const REGSAM wf            = EqualsIgnoreCase(viewStr, "wow32") ? KEY_WOW64_32KEY : KEY_WOW64_64KEY;

            if (!virtReg.empty()) {
                ScriptRollbackLedger::RegSnap snap{};
                snap.hRoot    = hRoot;
                snap.rootCode = rootCode;
                snap.subKey   = subKey;
                snap.valName  = valName;
                snap.hadOldVal = native_detail::VirtualRegReadValue(virtReg, rootCode, subKey, valName, snap.virtOldUtf8);
                bool createdNew = false;
                if (!native_detail::VirtualRegWriteValue(virtReg, rootCode, subKey, valName, valObj.AsString(), createdNew)) {
                    RaiseError(line, "Virtual registry write failed.");
                    return Value::FromBool(false);
                }
                snap.createdKey = createdNew;
                if (ledger_.active) {
                    ledger_.regUndoStack.push_back(std::move(snap));
                    ledger_.uninstallRegKeys.push_back({rootCode, subKey});
                }
                std::string fErr;
                if (!ledger_.CheckFaultTrigger(fErr)) { RaiseError(line, fErr); return Value::FromBool(false); }
                return Value::FromBool(true);
            }

            ScriptRollbackLedger::RegSnap snap{};
            snap.hRoot    = hRoot;
            snap.rootCode = rootCode;
            snap.subKey   = subKey;
            snap.valName  = valName;
            snap.wowFlag  = wf;

            // Capture previous value if the key/value already exists!
            HKEY hkOld = nullptr;
            if (::RegOpenKeyExW(hRoot, subKey.c_str(), 0, KEY_READ | wf, &hkOld) == ERROR_SUCCESS) {
                DWORD t = 0, cb = 0;
                if (::RegQueryValueExW(hkOld, valName.empty() ? nullptr : valName.c_str(), nullptr, &t, nullptr, &cb) == ERROR_SUCCESS) {
                    snap.hadOldVal = true;
                    snap.oldType   = t;
                    snap.oldBytes.resize(cb);
                    if (cb > 0) {
                        ::RegQueryValueExW(hkOld, valName.empty() ? nullptr : valName.c_str(), nullptr, &t, snap.oldBytes.data(), &cb);
                    }
                }
                ::RegCloseKey(hkOld);
            }

            HKEY hk = nullptr;
            DWORD disp = 0;
            if (::RegCreateKeyExW(hRoot, subKey.c_str(), 0, nullptr, 0, KEY_WRITE | wf, nullptr, &hk, &disp) != ERROR_SUCCESS) {
                RaiseError(line, "Registry write denied under " + rootCode + "\\" + WideToUtf8(subKey));
                return Value::FromBool(false);
            }
            snap.createdKey = (disp == REG_CREATED_NEW_KEY);
            LSTATUS st = ERROR_SUCCESS;
            if (m == "writeDword") {
                const DWORD dw = static_cast<DWORD>(valObj.AsInt());
                st = ::RegSetValueExW(hk, valName.empty() ? nullptr : valName.c_str(), 0, REG_DWORD,
                                      reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
            } else {
                const std::wstring ws = Utf8ToWide(valObj.AsString());
                const DWORD rt = (m == "writeExpandString") ? REG_EXPAND_SZ : REG_SZ;
                st = ::RegSetValueExW(hk, valName.empty() ? nullptr : valName.c_str(), 0, rt,
                                      reinterpret_cast<const BYTE*>(ws.c_str()),
                                      static_cast<DWORD>((ws.size() + 1) * sizeof(wchar_t)));
            }
            ::RegCloseKey(hk);
            if (st != ERROR_SUCCESS) {
                RaiseError(line, "RegSetValueExW failed under " + rootCode + "\\" + WideToUtf8(subKey));
                return Value::FromBool(false);
            }
            if (ledger_.active) {
                ledger_.regUndoStack.push_back(std::move(snap));
                ledger_.uninstallRegKeys.push_back({rootCode, subKey});
            }
            std::string fErr;
            if (!ledger_.CheckFaultTrigger(fErr)) { RaiseError(line, fErr); return Value::FromBool(false); }
            return Value::FromBool(true);
        }

        if (m == "deleteValue") {
            const std::wstring valName = Utf8ToWide(args.At(2).AsString());
            const std::string viewStr  = args.NamedOrPos("view", 3, Value::FromStr("native64")).AsString();
            if (!virtReg.empty()) {
                native_detail::VirtualRegDeleteValue(virtReg, rootCode, subKey, valName);
                return Value::FromBool(true);
            }
            for (REGSAM wf : ParseRegViews(viewStr)) {
                HKEY hk = nullptr;
                if (::RegOpenKeyExW(hRoot, subKey.c_str(), 0, KEY_SET_VALUE | wf, &hk) == ERROR_SUCCESS) {
                    ::RegDeleteValueW(hk, valName.empty() ? nullptr : valName.c_str());
                    ::RegCloseKey(hk);
                }
            }
            return Value::FromBool(true);
        }

        if (m == "deleteKeyTree") {
            if (!virtReg.empty()) {
                native_detail::VirtualRegDeleteTree(virtReg, rootCode, subKey);
                return Value::FromBool(true);
            }
            ::RegDeleteTreeW(hRoot, subKey.c_str());
            return Value::FromBool(true);
        }

        RaiseError(line, "Unknown reg.* method 'reg." + m + "()'.");
        return Value::Null();
    }

    // ----------------------------------------------------------------------------
    // Namespace 4: `ini.*`, `xml.*`, & `json.*`
    // ----------------------------------------------------------------------------
    Value CallIni(const std::string& m, const EvaluatedArgs& args, int line) {
        const auto filePath = std::filesystem::weakly_canonical(args.At(0).AsPath());
        const std::wstring wFile = filePath.empty() ? args.At(0).AsPath().wstring() : filePath.wstring();
        if (m == "sections") {
            wchar_t buf[8192]{};
            const DWORD n = ::GetPrivateProfileSectionNamesW(buf, static_cast<DWORD>(std::size(buf)), wFile.c_str());
            std::vector<Value> secs;
            std::size_t pos = 0;
            while (pos < n && buf[pos] != L'\0') {
                std::wstring s(&buf[pos]);
                pos += s.size() + 1;
                secs.push_back(Value::FromStr(WideToUtf8(s)));
            }
            return Value::FromArray(std::move(secs));
        }

        const std::wstring wSec = Utf8ToWide(args.At(1).AsString());
        const std::wstring wKey = Utf8ToWide(args.At(2).AsString());
        if (m == "read") {
            const std::wstring wDef = Utf8ToWide(args.At(3, Value::FromStr("")).AsString());
            wchar_t buf[4096]{};
            ::GetPrivateProfileStringW(wSec.c_str(), wKey.c_str(), wDef.c_str(), buf, static_cast<DWORD>(std::size(buf)), wFile.c_str());
            return Value::FromStr(WideToUtf8(buf));
        }
        if (m == "write" || m == "deleteKey") {
            ledger_.EnsureDirectoryTracked(std::filesystem::path(wFile).parent_path());
            if (ledger_.active) {
                wchar_t sentinelBuf[4096]{};
                const wchar_t* kSentinel = L"__RSSC_MISSING_INI_KEY__";
                ::GetPrivateProfileStringW(wSec.c_str(), wKey.c_str(), kSentinel, sentinelBuf, static_cast<DWORD>(std::size(sentinelBuf)), wFile.c_str());
                const bool hadOld = (std::wcscmp(sentinelBuf, kSentinel) != 0);
                ledger_.iniUndoStack.push_back({std::filesystem::path(wFile), wSec, wKey, hadOld, hadOld ? sentinelBuf : L""});
            }
            const std::wstring wVal = Utf8ToWide(args.At(3).AsString());
            const BOOL ok = ::WritePrivateProfileStringW(
                wSec.c_str(), wKey.c_str(), (m == "deleteKey") ? nullptr : wVal.c_str(), wFile.c_str());
            std::string fErr;
            if (!ledger_.CheckFaultTrigger(fErr)) { RaiseError(line, fErr); return Value::FromBool(false); }
            return Value::FromBool(ok != FALSE);
        }
        RaiseError(line, "Unknown ini.* method 'ini." + m + "()'.");
        return Value::Null();
    }

    Value CallXml(const std::string& m, const EvaluatedArgs& args, int line) {
        const auto filePath = args.At(0).AsPath();
        const std::string tag = args.At(1).AsString();
        std::ifstream in(filePath, std::ios::binary);
        std::ostringstream ss;
        if (in) ss << in.rdbuf();
        std::string xml = ss.str();

        if (m == "readText") {
            const std::string defVal = args.At(2, Value::FromStr("")).AsString();
            const std::string openTag = "<" + tag;
            const auto p1 = xml.find(openTag);
            const auto gt = (p1 != std::string::npos) ? xml.find('>', p1) : std::string::npos;
            const auto cl = (gt != std::string::npos) ? xml.find("</" + tag + ">", gt + 1) : std::string::npos;
            if (gt != std::string::npos && cl != std::string::npos) {
                return Value::FromStr(xml.substr(gt + 1, cl - gt - 1));
            }
            return Value::FromStr(defVal);
        }
        if (m == "readAttr") {
            const std::string attr   = args.At(2).AsString();
            const std::string defVal = args.At(3, Value::FromStr("")).AsString();
            const auto p1 = xml.find("<" + tag);
            const auto gt = (p1 != std::string::npos) ? xml.find('>', p1) : std::string::npos;
            if (p1 != std::string::npos && gt != std::string::npos) {
                const std::string head = xml.substr(p1, gt - p1);
                const auto ap = head.find(attr + "=\"");
                if (ap != std::string::npos) {
                    const auto vs = ap + attr.size() + 2;
                    const auto ve = head.find('"', vs);
                    if (ve != std::string::npos) return Value::FromStr(head.substr(vs, ve - vs));
                }
            }
            return Value::FromStr(defVal);
        }
        if (m == "replaceAttr") {
            const std::string attr   = args.At(2).AsString();
            const std::string newVal = args.At(3).AsString();
            const auto p1 = xml.find("<" + tag);
            const auto gt = (p1 != std::string::npos) ? xml.find('>', p1) : std::string::npos;
            if (p1 == std::string::npos || gt == std::string::npos) return Value::FromBool(false);
            std::string head = xml.substr(p1, gt - p1);
            const auto ap = head.find(attr + "=\"");
            if (ap != std::string::npos) {
                const auto vs = ap + attr.size() + 2;
                const auto ve = head.find('"', vs);
                if (ve != std::string::npos) head.replace(vs, ve - vs, newVal);
            } else {
                head += " " + attr + "=\"" + newVal + "\"";
            }
            xml.replace(p1, gt - p1, head);
            ledger_.PrepareFileWriteBackup(filePath);
            std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
            out.write(xml.data(), static_cast<std::streamsize>(xml.size()));
            return Value::FromBool(out.good());
        }
        if (m == "setElementText") {
            const std::string newTxt = args.At(2).AsString();
            const auto p1 = xml.find("<" + tag);
            const auto gt = (p1 != std::string::npos) ? xml.find('>', p1) : std::string::npos;
            const auto cl = (gt != std::string::npos) ? xml.find("</" + tag + ">", gt + 1) : std::string::npos;
            if (gt == std::string::npos || cl == std::string::npos) return Value::FromBool(false);
            xml.replace(gt + 1, cl - gt - 1, newTxt);
            ledger_.PrepareFileWriteBackup(filePath);
            std::ofstream out(filePath, std::ios::binary | std::ios::trunc);
            out.write(xml.data(), static_cast<std::streamsize>(xml.size()));
            return Value::FromBool(out.good());
        }
        RaiseError(line, "Unknown xml.* method 'xml." + m + "()'.");
        return Value::Null();
    }

    Value CallJson(const std::string& m, const EvaluatedArgs& args) {
        if (m == "parse") {
            const std::string txt = args.At(0).AsString();
            json_detail::JsonReader jr{txt, 0};
            return jr.ParseValue();
        }
        if (m == "stringify") {
            std::ostringstream oss;
            json_detail::StringifyJsonRec(oss, args.At(0), static_cast<int>(args.At(1, Value::FromInt(2)).AsInt()), 0);
            return Value::FromStr(oss.str());
        }
        return Value::Null();
    }

    // ----------------------------------------------------------------------------
    // Namespace 5: `str.*` & `regex.*`
    // ----------------------------------------------------------------------------
    Value CallStr(const std::string& m, const EvaluatedArgs& args) {
        if (m == "join") {
            const Value arr = args.At(0);
            const std::string sep = args.At(1, Value::FromStr("")).AsString();
            std::ostringstream oss;
            if (arr.kind == ValKind::Array && arr.arrVal) {
                for (std::size_t i = 0; i < arr.arrVal->size(); ++i) {
                    if (i > 0) oss << sep;
                    oss << (*arr.arrVal)[i].AsString();
                }
            }
            return Value::FromStr(oss.str());
        }
        const std::string s = args.At(0).AsString();
        if (m == "len")   return Value::FromInt(static_cast<std::int64_t>(s.size()));
        if (m == "lower") return Value::FromStr(AsciiLower(s));
        if (m == "upper") {
            std::string u = s;
            for (char& c : u) if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
            return Value::FromStr(std::move(u));
        }
        if (m == "trim")  return Value::FromStr(TrimAscii(s));
        if (m == "contains") {
            const std::string sub = args.At(1).AsString();
            const bool ic = args.At(2, Value::FromBool(false)).AsBool();
            return Value::FromBool(ic ? (AsciiLower(s).find(AsciiLower(sub)) != std::string::npos)
                                      : (s.find(sub) != std::string::npos));
        }
        if (m == "startsWith") {
            const std::string pfx = args.At(1).AsString();
            const bool ic = args.At(2, Value::FromBool(false)).AsBool();
            if (s.size() < pfx.size()) return Value::FromBool(false);
            return Value::FromBool(ic ? EqualsIgnoreCase(std::string_view(s).substr(0, pfx.size()), pfx)
                                      : (s.compare(0, pfx.size(), pfx) == 0));
        }
        if (m == "endsWith") {
            const std::string sfx = args.At(1).AsString();
            const bool ic = args.At(2, Value::FromBool(false)).AsBool();
            if (s.size() < sfx.size()) return Value::FromBool(false);
            return Value::FromBool(ic ? EqualsIgnoreCase(std::string_view(s).substr(s.size() - sfx.size()), sfx)
                                      : (s.compare(s.size() - sfx.size(), sfx.size(), sfx) == 0));
        }
        if (m == "indexOf") {
            const auto pos = s.find(args.At(1).AsString());
            return Value::FromInt(pos == std::string::npos ? -1 : static_cast<std::int64_t>(pos));
        }
        if (m == "replace") {
            std::string out = s;
            const std::string from = args.At(1).AsString();
            const std::string to   = args.At(2).AsString();
            if (!from.empty()) {
                std::size_t pos = 0;
                while ((pos = out.find(from, pos)) != std::string::npos) {
                    out.replace(pos, from.size(), to);
                    pos += to.size();
                }
            }
            return Value::FromStr(std::move(out));
        }
        if (m == "split") {
            const std::string delim = args.At(1, Value::FromStr(",")).AsString();
            std::vector<Value> parts;
            if (delim.empty()) {
                parts.push_back(Value::FromStr(s));
            } else {
                std::size_t pos = 0;
                while (pos <= s.size()) {
                    const auto next = s.find(delim, pos);
                    if (next == std::string::npos) {
                        parts.push_back(Value::FromStr(s.substr(pos)));
                        break;
                    }
                    parts.push_back(Value::FromStr(s.substr(pos, next - pos)));
                    pos = next + delim.size();
                }
            }
            return Value::FromArray(std::move(parts));
        }
        if (m == "slice") {
            const auto start = static_cast<std::size_t>(std::max<std::int64_t>(0, args.At(1).AsInt()));
            if (start >= s.size()) return Value::FromStr("");
            if (args.pos.size() >= 3) {
                const auto len = static_cast<std::size_t>(std::max<std::int64_t>(0, args.At(2).AsInt()));
                return Value::FromStr(s.substr(start, len));
            }
            return Value::FromStr(s.substr(start));
        }
        return Value::FromStr(s);
    }

    Value CallRegex(const std::string& m, const EvaluatedArgs& args, int line) {
        try {
            const std::string text = args.At(0).AsString();
            const std::string pat  = args.At(1).AsString();
            auto flags = std::regex::ECMAScript;
            if (args.NamedOrPos("ignoreCase", 2, Value::FromBool(false)).AsBool()) {
                flags |= std::regex::icase;
            }
            const std::regex re(pat, flags);
            if (m == "isMatch") {
                return Value::FromBool(std::regex_search(text, re));
            }
            if (m == "match") {
                std::smatch sm;
                std::vector<Value> groups;
                if (std::regex_search(text, sm, re)) {
                    for (std::size_t i = 0; i < sm.size(); ++i) {
                        groups.push_back(Value::FromStr(sm[i].str()));
                    }
                }
                return Value::FromArray(std::move(groups));
            }
            if (m == "replace") {
                const std::string repl = args.At(2).AsString();
                return Value::FromStr(std::regex_replace(text, re, repl));
            }
        } catch (const std::exception& ex) {
            RaiseError(line, std::string("Regex error: ") + ex.what());
        }
        return Value::Null();
    }

    // ----------------------------------------------------------------------------
    // Namespace 6: `archive.*` & `crypto.*`
    // ----------------------------------------------------------------------------
    Value CallArchive(const std::string& m, const EvaluatedArgs& args, int line) {
        std::string err;
        std::error_code ec;
        if (m == "extractZip") {
            const auto zipP = args.At(0).AsPath();
            const auto dstP = args.At(1).AsPath();
            ledger_.EnsureDirectoryTracked(dstP);
            std::vector<std::filesystem::path> extracted;
            if (!ExtractZipArchiveToDirectory(zipP, dstP, extracted, err)) {
                RaiseError(line, err);
                return Value::FromInt(0);
            }
            if (ledger_.active) {
                for (const auto& f : extracted) ledger_.newlyWrittenFiles.push_back(f);
            }
            return Value::FromInt(static_cast<std::int64_t>(extracted.size()));
        }
        if (m == "createPak") {
            const auto srcDir = args.At(0).AsPath();
            const auto outPak = args.At(1).AsPath();
            std::vector<RswPakInputItem> items;
            for (const auto& de : std::filesystem::recursive_directory_iterator(srcDir, ec)) {
                if (!de.is_regular_file(ec)) continue;
                const auto rel = std::filesystem::relative(de.path(), srcDir, ec);
                items.push_back({de.path(), WideToUtf8(rel.generic_wstring()), "Main"});
            }
            std::uint64_t rawBytes = 0;
            std::string shaHex;
            if (!WriteRswPakFile(items, outPak, rawBytes, shaHex, err)) {
                RaiseError(line, err);
                return Value::FromStr("");
            }
            return Value::FromStr(shaHex);
        }
        if (m == "extractPak") {
            const auto pakPath = args.At(0).AsPath();
            const auto dstDir  = args.At(1).AsPath();
            std::vector<std::uint8_t> raw;
            std::vector<RswPakFileMetadata> metas;
            std::vector<std::uint8_t> payload;
            if (!ReadBinaryFileBytes(pakPath, raw, err) ||
                !UnpackRswPakBytesInMemory(raw.data(), raw.size(), metas, payload, err)) {
                RaiseError(line, err);
                return Value::FromInt(0);
            }
            for (const auto& meta : metas) {
                const auto dst = (dstDir / Utf8ToWide(meta.archiveRelPath)).lexically_normal();
                ledger_.PrepareFileWriteBackup(dst);
                std::ofstream out(dst, std::ios::binary | std::ios::trunc);
                if (meta.payloadSize > 0) {
                    out.write(reinterpret_cast<const char*>(payload.data() + meta.payloadOffset),
                              static_cast<std::streamsize>(meta.payloadSize));
                }
            }
            return Value::FromInt(static_cast<std::int64_t>(metas.size()));
        }
        RaiseError(line, "Unknown archive.* method 'archive." + m + "()'.");
        return Value::Null();
    }

    Value CallCrypto(const std::string& m, const EvaluatedArgs& args) {
        if (m == "sha256File") {
            std::string hex;
            ComputeFileSha256Hex(args.At(0).AsPath(), hex);
            return Value::FromStr(hex);
        }
        if (m == "sha256Text") {
            const std::string s = args.At(0).AsString();
            return Value::FromStr(bc_detail::ComputeBufferSha256HexLower(
                reinterpret_cast<const std::uint8_t*>(s.data()), s.size()));
        }
        if (m == "crc32Text") {
            const std::string s = args.At(0).AsString();
            return Value::FromInt(static_cast<std::int64_t>(
                ComputeCrc32(reinterpret_cast<const std::uint8_t*>(s.data()), s.size())));
        }
        if (m == "verifyAuthenticode") {
            return Value::FromBool(host_detail::VerifyAuthenticodeSignature(args.At(0).AsPath()));
        }
        return Value::Null();
    }

    // ----------------------------------------------------------------------------
    // Namespace 7: `net.*` (`WinHTTP` TLS 1.2/1.3)
    // ----------------------------------------------------------------------------
    Value CallNet(const std::string& m, const EvaluatedArgs& args, int line) {
        if (m == "download") {
            const std::string url    = args.At(0).AsString();
            const auto dest          = args.At(1).AsPath();
            const std::string expSha = args.NamedOrPos("sha256", 2, Value::FromStr("")).AsString();
            std::string err;
            if (!DownloadFileViaWinHttp(url, dest, opts_.cancelFlag, nullptr, err)) {
                exitCode_ = 9010;
                RaiseError(line, err);
                return Value::FromBool(false);
            }
            if (!expSha.empty()) {
                std::string actual;
                if (!ComputeFileSha256Hex(dest, actual) || !EqualsIgnoreCase(actual, expSha)) {
                    std::error_code ec;
                    std::filesystem::remove(dest, ec);
                    exitCode_ = 9010;
                    RaiseError(line, "SHA-256 verification failed for downloaded file: " + WideToUtf8(dest.filename().wstring()));
                    return Value::FromBool(false);
                }
            }
            if (ledger_.active) ledger_.newlyWrittenFiles.push_back(dest);
            return Value::FromBool(true);
        }
        if (m == "getText") {
            std::string body, err;
            if (!host_detail::FetchUrlTextViaWinHttp(args.At(0).AsString(), body, err)) {
                RaiseError(line, err);
                return Value::FromStr("");
            }
            return Value::FromStr(std::move(body));
        }
        RaiseError(line, "Unknown net.* method 'net." + m + "()'.");
        return Value::Null();
    }

    // ----------------------------------------------------------------------------
    // Namespace 8: `proc.*` (Toolhelp32 Inspection + 3-Token Launcher!)
    // ----------------------------------------------------------------------------
    Value CallProc(const std::string& m, const EvaluatedArgs& args, int line) {
        if (m == "isRunning" || m == "listRunning" || m == "terminate") {
            const std::wstring targetLeaf = Utf8ToWide(args.At(0, Value::FromStr("")).AsString());
            std::vector<Value> running;
            bool matched = false;
            HANDLE hSnap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
            if (hSnap != INVALID_HANDLE_VALUE) {
                PROCESSENTRY32W pe{};
                pe.dwSize = sizeof(pe);
                if (::Process32FirstW(hSnap, &pe)) {
                    do {
                        if (m == "listRunning") {
                            running.push_back(Value::FromStr(WideToUtf8(pe.szExeFile)));
                        } else if (_wcsicmp(pe.szExeFile, targetLeaf.c_str()) == 0 &&
                                   pe.th32ProcessID != ::GetCurrentProcessId()) {
                            matched = true;
                            if (m == "terminate") {
                                if (HANDLE hProc = ::OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, pe.th32ProcessID)) {
                                    ::TerminateProcess(hProc, 0);
                                    ::WaitForSingleObject(hProc, 3000);
                                    ::CloseHandle(hProc);
                                }
                            }
                        }
                    } while (::Process32NextW(hSnap, &pe));
                }
                ::CloseHandle(hSnap);
            }
            if (m == "listRunning") return Value::FromArray(std::move(running));
            return Value::FromBool(matched);
        }

        if (m == "open") {
            const std::wstring target = Utf8ToWide(args.At(0).AsString());
            const auto rc = reinterpret_cast<INT_PTR>(
                ::ShellExecuteW(opts_.parentHwnd, L"open", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
            return Value::FromBool(rc > 32);
        }

        if (m == "run") {
            const auto exePath        = args.At(0).AsPath();
            const std::string cmdArgs = args.NamedOrPos("args", 1, Value::FromStr("")).AsString();
            const bool wait           = args.NamedOrPos("wait", 2, Value::FromBool(true)).AsBool();
            const bool hidden         = args.NamedOrPos("hidden", 3, Value::FromBool(wait)).AsBool();
            const std::string tokMode = args.NamedOrPos("token", 4, Value::FromStr("current")).AsString();
            const auto workDir        = args.NamedOrPos("workDir", 5, Value::FromStr("")).AsPath();

            std::int64_t code = 0;
            std::string err;
            if (!host_detail::RunProcessWithTokenMode(exePath, cmdArgs, workDir, wait, hidden, tokMode, code, err)) {
                if (args.NamedOrPos("check", 6, Value::FromBool(false)).AsBool()) {
                    RaiseError(line, err);
                }
                return Value::FromInt(code != 0 ? code : -1);
            }
            if (code == 3010 || code == 1641) rebootNeeded_ = true;
            return Value::FromInt(code);
        }

        if (m == "callDllInt") {
            const std::wstring dllName = Utf8ToWide(args.At(0).AsString());
            const std::string fnName   = args.At(1).AsString();
            HMODULE hDll = ::LoadLibraryW(dllName.c_str());
            if (!hDll) return Value::FromInt(-1);
            std::int64_t ret = 0;
            if (args.pos.size() >= 3) {
                using Fn1 = int(WINAPI*)(std::intptr_t);
                if (auto fn = reinterpret_cast<Fn1>(::GetProcAddress(hDll, fnName.c_str()))) {
                    ret = fn(static_cast<std::intptr_t>(args.At(2).AsInt()));
                }
            } else {
                using Fn0 = int(WINAPI*)();
                if (auto fn = reinterpret_cast<Fn0>(::GetProcAddress(hDll, fnName.c_str()))) {
                    ret = fn();
                }
            }
            ::FreeLibrary(hDll);
            return Value::FromInt(ret);
        }

        RaiseError(line, "Unknown proc.* method 'proc." + m + "()'.");
        return Value::Null();
    }

    // ----------------------------------------------------------------------------
    // Namespace 9: `msi.*` & `prereq.*`
    // ----------------------------------------------------------------------------
    Value CallMsi(const std::string& m, const EvaluatedArgs& args, int line) {
        if (m == "isUpgradeInstalled") {
            const std::wstring upg = Utf8ToWide(args.At(0).AsString());
            wchar_t prodBuf[39]{};
            if (::MsiEnumRelatedProductsW(upg.c_str(), 0, 0, prodBuf) == ERROR_SUCCESS) {
                wchar_t verBuf[64]{}, locBuf[MAX_PATH * 2]{};
                DWORD cchVer = 64, cchLoc = static_cast<DWORD>(std::size(locBuf));
                ::MsiGetProductInfoW(prodBuf, INSTALLPROPERTY_VERSIONSTRING, verBuf, &cchVer);
                ::MsiGetProductInfoW(prodBuf, INSTALLPROPERTY_INSTALLLOCATION, locBuf, &cchLoc);
                return Value::FromMap({
                    {"installed",       Value::FromBool(true)},
                    {"productCode",     Value::FromStr(WideToUtf8(prodBuf))},
                    {"version",         Value::FromVer(SemVer::Parse(WideToUtf8(verBuf)))},
                    {"installLocation", Value::FromStr(WideToUtf8(locBuf))}
                });
            }
            return Value::FromMap({{"installed", Value::FromBool(false)}});
        }
        if (m == "isProductInstalled") {
            const INSTALLSTATE st = ::MsiQueryProductStateW(Utf8ToWide(args.At(0).AsString()).c_str());
            return Value::FromBool(st == INSTALLSTATE_DEFAULT || st == INSTALLSTATE_LOCAL);
        }
        if (m == "isNativeInstalled") {
            const std::string id  = args.At(0).AsString();
            const std::string key = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + id;
            EvaluatedArgs rArgs;
            rArgs.pos = {Value::FromStr("HKLM"), Value::FromStr(key), Value::FromStr("DisplayVersion")};
            if (CallReg("exists", rArgs, line).AsBool()) return Value::FromBool(true);
            rArgs.pos[0] = Value::FromStr("HKCU");
            return CallReg("exists", rArgs, line);
        }
        if (m == "runLegacyUninstaller") {
            // Cleanly executes legacy third-party uninstallers (supporting optional `%TEMP%` staging
            // so self-deleting uninstallers can delete their original folder without file locks!).
            const auto origExe   = args.At(0).AsPath();
            const std::string ua = args.NamedOrPos("args", 1, Value::FromStr("/S")).AsString();
            const bool stageTemp = args.NamedOrPos("copyToTemp", 2, Value::FromBool(false)).AsBool();
            std::error_code ec;
            if (!std::filesystem::exists(origExe, ec)) return Value::FromInt(0);
            std::filesystem::path runExe = origExe;
            if (stageTemp) {
                runExe = std::filesystem::temp_directory_path(ec) /
                         (L"rsw-legacy-uninst-" + std::to_wstring(::GetCurrentProcessId()) + L".exe");
                std::filesystem::copy_file(origExe, runExe, std::filesystem::copy_options::overwrite_existing, ec);
            }
            std::int64_t code = 0;
            std::string err;
            host_detail::RunProcessWithTokenMode(runExe, ua, origExe.parent_path(), true, true, "current", code, err);
            if (stageTemp) std::filesystem::remove(runExe, ec);
            return Value::FromInt(code);
        }
        if (m == "install" || m == "uninstall" || m == "applyPatch") {
            while (IsSystemMsiExecuteMutexBusy()) {
                if (opts_.cancelFlag && opts_.cancelFlag->load()) return Value::FromInt(ERROR_INSTALL_USEREXIT);
                ::Sleep(250);
            }
            const bool silent = args.NamedOrPos("silent", 2, Value::FromBool(true)).AsBool();
            ::MsiSetInternalUI(silent ? INSTALLUILEVEL_NONE : INSTALLUILEVEL_BASIC, nullptr);
            UINT rc = 0;
            if (m == "install") {
                rc = ::MsiInstallProductW(
                    args.At(0).AsPath().wstring().c_str(),
                    Utf8ToWide(args.NamedOrPos("properties", 1, Value::FromStr("")).AsString()).c_str());
            } else if (m == "uninstall") {
                rc = ::MsiConfigureProductExW(
                    Utf8ToWide(args.At(0).AsString()).c_str(), 0, INSTALLSTATE_ABSENT, L"");
            } else {
                rc = ::MsiApplyPatchW(
                    args.At(0).AsPath().wstring().c_str(), nullptr, INSTALLTYPE_DEFAULT,
                    Utf8ToWide(args.NamedOrPos("properties", 1, Value::FromStr("")).AsString()).c_str());
            }
            if (rc == ERROR_SUCCESS_REBOOT_REQUIRED) {
                rebootNeeded_ = true;
                rc = ERROR_SUCCESS;
            }
            return Value::FromInt(rc);
        }
        RaiseError(line, "Unknown msi.* method 'msi." + m + "()'.");
        return Value::Null();
    }

    Value CallPrereq(const std::string& m, const EvaluatedArgs& args, int line) {
        const std::string defArch(QueryHostPlatform().NativeArchSlug());
        if (m == "isDotNetDesktopInstalled") {
            const int maj = static_cast<int>(args.At(0, Value::FromInt(11)).AsInt());
            const std::string a = args.At(1, Value::FromStr(defArch)).AsString();
            return Value::FromBool(IsDotNetSharedFxInstalled("Microsoft.WindowsDesktop.App", maj, a));
        }
        if (m == "isAspNetInstalled") {
            const int maj = static_cast<int>(args.At(0, Value::FromInt(11)).AsInt());
            const std::string a = args.At(1, Value::FromStr(defArch)).AsString();
            return Value::FromBool(IsDotNetSharedFxInstalled("Microsoft.AspNetCore.App", maj, a));
        }
        if (m == "isDotNetRuntimeInstalled") {
            const int maj = static_cast<int>(args.At(0, Value::FromInt(11)).AsInt());
            const std::string a = args.At(1, Value::FromStr(defArch)).AsString();
            return Value::FromBool(IsDotNetSharedFxInstalled("Microsoft.NETCore.App", maj, a));
        }
        if (m == "isWebView2Installed") {
            RuntimePrerequisiteSpec sp{};
            sp.preset = "EdgeWebView2";
            return Value::FromBool(IsPrerequisiteSatisfied(sp, defArch));
        }
        if (m == "isVcRedist2022Installed") {
            RuntimePrerequisiteSpec sp{};
            sp.preset = "VcRedist2022";
            return Value::FromBool(IsPrerequisiteSatisfied(sp, args.At(0, Value::FromStr(defArch)).AsString()));
        }
        if (m == "isSatisfied") {
            RuntimePrerequisiteSpec sp{};
            sp.preset = args.At(0).AsString();
            return Value::FromBool(IsPrerequisiteSatisfied(sp, args.At(1, Value::FromStr(defArch)).AsString()));
        }
        if (m == "installPreset") {
            const std::string presetName = args.At(0).AsString();
            const std::string arch       = args.At(1, Value::FromStr(defArch)).AsString();
            const auto* cur              = FindCuratedRuntimePreset(presetName);
            if (!cur) {
                RaiseError(line, "Unknown runtime preset '" + presetName + "'.");
                return Value::FromBool(false);
            }
            RuntimePrerequisiteSpec sp{};
            sp.id           = presetName;
            sp.preset       = presetName;
            sp.displayName  = std::string(cur->displayName);
            sp.fileName     = ExpandArchToken(cur->fileNamePattern, arch);
            sp.downloadUrl  = std::string(EqualsIgnoreCase(arch, "arm64") ? cur->urlArm64 : cur->urlX64);
            sp.silentArgs   = std::string(cur->silentArgs);
            sp.successCodes = ParseExitCodeList(cur->successCodes);
            sp.rebootCodes  = ParseExitCodeList(cur->rebootCodes);
            if (IsPrerequisiteSatisfied(sp, arch)) return Value::FromBool(true);

            std::error_code ec;
            const auto dlFile = std::filesystem::temp_directory_path(ec) / Utf8ToWide(sp.fileName);
            std::string err;
            if (!DownloadFileViaWinHttp(sp.downloadUrl, dlFile, opts_.cancelFlag, nullptr, err)) {
                exitCode_ = 9010;
                RaiseError(line, err);
                return Value::FromBool(false);
            }
            std::uint32_t rawCode = 0;
            const auto outc = ExecuteStagedPrerequisiteInstaller(sp, dlFile, rawCode, err);
            std::filesystem::remove(dlFile, ec);
            if (outc == PrereqExitOutcome::SucceededRebootRequested) {
                rebootNeeded_ = true;
                return Value::FromBool(true);
            }
            if (outc == PrereqExitOutcome::Failed) {
                RaiseError(line, err);
                return Value::FromBool(false);
            }
            return Value::FromBool(true);
        }
        RaiseError(line, "Unknown prereq.* method 'prereq." + m + "()'.");
        return Value::Null();
    }

    // ----------------------------------------------------------------------------
    // Namespace 10: `shortcut.*`, `shell.*`, `env.*`, & `arp.*`
    // ----------------------------------------------------------------------------
    Value CallShortcut(const std::string& m, const EvaluatedArgs& args, int line) {
        if (m == "create") {
            const std::string loc  = args.NamedOrPos("location",    0, Value::FromStr("StartMenu")).AsString();
            const std::string name = args.NamedOrPos("name",        1, Value::FromStr(prog_.package.name)).AsString();
            const auto targetExe   = args.NamedOrPos("target",      2).AsPath();
            const std::string scAr = args.NamedOrPos("args",        3, Value::FromStr("")).AsString();
            const std::string desc = args.NamedOrPos("description", 4, Value::FromStr("")).AsString();
            const auto iconPath    = args.NamedOrPos("icon",        5, Value::FromPath(targetExe)).AsPath();
            const int iconIdx      = static_cast<int>(args.NamedOrPos("iconIndex", 6, Value::FromInt(0)).AsInt());
            const bool perMachine  = EqualsIgnoreCase(prog_.package.scope, "machine");

            std::filesystem::path lnkPath;
            if (EqualsIgnoreCase(WideToUtf8(std::filesystem::path(Utf8ToWide(loc)).extension().wstring()), ".lnk")) {
                lnkPath = std::filesystem::path(Utf8ToWide(loc));
            } else if (EqualsIgnoreCase(loc, "Desktop")) {
                const auto desk = host_detail::GetKnownFolderOrEnv(perMachine ? FOLDERID_PublicDesktop : FOLDERID_Desktop, nullptr);
                lnkPath = desk / Utf8ToWide(name + ".lnk");
            } else if (EqualsIgnoreCase(loc, "StartMenu")) {
                auto sm = host_detail::GetKnownFolderOrEnv(perMachine ? FOLDERID_CommonPrograms : FOLDERID_Programs, nullptr);
                if (!prog_.package.publisher.empty()) sm /= Utf8ToWide(prog_.package.publisher);
                lnkPath = sm / Utf8ToWide(name + ".lnk");
            } else {
                lnkPath = std::filesystem::path(Utf8ToWide(loc)) / Utf8ToWide(name + ".lnk");
            }

            ledger_.EnsureDirectoryTracked(lnkPath.parent_path());
            std::string scErr;
            if (!native_detail::CreateWindowsShellShortcut(lnkPath, targetExe, scAr, desc, iconPath, iconIdx, scErr)) {
                RaiseError(line, scErr);
                return Value::FromPath({});
            }
            if (ledger_.active) ledger_.createdShortcuts.push_back(lnkPath);
            std::string fErr;
            if (!ledger_.CheckFaultTrigger(fErr)) { RaiseError(line, fErr); return Value::FromPath({}); }
            return Value::FromPath(lnkPath);
        }
        if (m == "remove") {
            std::error_code ec;
            std::filesystem::remove(args.At(0).AsPath(), ec);
            return Value::FromBool(!ec);
        }
        RaiseError(line, "Unknown shortcut.* method 'shortcut." + m + "()'.");
        return Value::Null();
    }

    Value CallShell(const std::string& m, const EvaluatedArgs& args, int line) {
        const std::string hive = EqualsIgnoreCase(prog_.package.scope, "machine") ? "HKLM" : "HKCU";
        if (m == "registerFileType") {
            std::string ext = TrimAscii(args.NamedOrPos("ext", 0).AsString());
            if (!ext.empty() && ext.front() != '.') ext.insert(ext.begin(), '.');
            const std::string progId = args.NamedOrPos("progId", 1).AsString();
            const std::string desc   = args.NamedOrPos("description", 2, Value::FromStr(progId)).AsString();
            const std::string exeStr = WideToUtf8(args.NamedOrPos("target", 3).AsPath().wstring());
            const int iconIdx        = static_cast<int>(args.NamedOrPos("iconIndex", 4, Value::FromInt(0)).AsInt());

            auto writeCls = [&](const std::string& sub, const std::string& valName, const std::string& val) {
                EvaluatedArgs ra;
                ra.pos = {Value::FromStr(hive), Value::FromStr("Software\\Classes\\" + sub), Value::FromStr(valName), Value::FromStr(val)};
                return CallReg("writeString", ra, line).AsBool();
            };
            const bool ok =
                writeCls(ext, "", progId) &&
                writeCls(progId, "", desc) &&
                writeCls(progId + "\\DefaultIcon", "", exeStr + "," + std::to_string(iconIdx)) &&
                writeCls(progId + "\\shell\\open\\command", "", "\"" + exeStr + "\" \"%1\"");
            if (native_detail::GetVirtualRegistryStoreRoot().empty()) {
                ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            }
            return Value::FromBool(ok);
        }
        if (m == "registerProtocol") {
            const std::string scheme = TrimAscii(args.NamedOrPos("scheme", 0).AsString());
            const std::string desc   = args.NamedOrPos("description", 1, Value::FromStr("URL:" + scheme + " Protocol")).AsString();
            const std::string exeStr = WideToUtf8(args.NamedOrPos("target", 2).AsPath().wstring());
            auto writeCls = [&](const std::string& sub, const std::string& valName, const std::string& val) {
                EvaluatedArgs ra;
                ra.pos = {Value::FromStr(hive), Value::FromStr("Software\\Classes\\" + sub), Value::FromStr(valName), Value::FromStr(val)};
                return CallReg("writeString", ra, line).AsBool();
            };
            const bool ok =
                writeCls(scheme, "", desc) &&
                writeCls(scheme, "URL Protocol", "") &&
                writeCls(scheme + "\\shell\\open\\command", "", "\"" + exeStr + "\" \"%1\"");
            return Value::FromBool(ok);
        }
        if (m == "refresh") {
            ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
            return Value::FromBool(true);
        }
        RaiseError(line, "Unknown shell.* method 'shell." + m + "()'.");
        return Value::Null();
    }

    Value CallEnv(const std::string& m, const EvaluatedArgs& args, int line) {
        const std::string scopeStr = args.NamedOrPos("scope", (m == "get" || m == "remove" || m == "appendPath") ? 1 : 2,
                                                     Value::FromStr("user")).AsString();
        const bool isMachine = EqualsIgnoreCase(scopeStr, "machine");
        const std::string hive = isMachine ? "HKLM" : "HKCU";
        const std::string envKey = isMachine
            ? "SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment"
            : "Environment";

        auto broadcastEnv = []() {
            if (native_detail::GetVirtualRegistryStoreRoot().empty()) {
                DWORD_PTR res = 0;
                ::SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
                                      reinterpret_cast<LPARAM>(L"Environment"),
                                      SMTO_ABORTIFHUNG, 1500, &res);
            }
        };

        if (m == "get") {
            const std::string varName = args.At(0).AsString();
            EvaluatedArgs ra;
            ra.pos = {Value::FromStr(hive), Value::FromStr(envKey), Value::FromStr(varName), Value::FromStr("")};
            const Value regV = CallReg("readString", ra, line);
            if (!regV.AsString().empty()) return regV;
            wchar_t buf[8192]{};
            ::GetEnvironmentVariableW(Utf8ToWide(varName).c_str(), buf, static_cast<DWORD>(std::size(buf)));
            return Value::FromStr(WideToUtf8(buf));
        }
        if (m == "set") {
            EvaluatedArgs ra;
            ra.pos = {Value::FromStr(hive), Value::FromStr(envKey), args.At(0), args.At(1)};
            const Value ok = CallReg("writeExpandString", ra, line);
            broadcastEnv();
            return ok;
        }
        if (m == "remove") {
            EvaluatedArgs ra;
            ra.pos = {Value::FromStr(hive), Value::FromStr(envKey), args.At(0)};
            const Value ok = CallReg("deleteValue", ra, line);
            broadcastEnv();
            return ok;
        }
        if (m == "appendPath") {
            // Safe non-clobbering semicolon append (`Placement=1` safety rule: NEVER overwrite `PATH`!)
            const std::string dirToAdd = WideToUtf8(args.At(0).AsPath().wstring());
            EvaluatedArgs rRead;
            rRead.pos = {Value::FromStr(hive), Value::FromStr(envKey), Value::FromStr("Path"), Value::FromStr("")};
            std::string curPath = CallReg("readString", rRead, line).AsString();

            // Case-insensitive duplicate check across `;`-separated segments
            std::size_t pos = 0;
            while (pos <= curPath.size()) {
                const auto sc = curPath.find(';', pos);
                std::string seg = TrimAscii(sc == std::string::npos ? curPath.substr(pos) : curPath.substr(pos, sc - pos));
                while (!seg.empty() && seg.back() == '\\') seg.pop_back();
                std::string normNew = dirToAdd;
                while (!normNew.empty() && normNew.back() == '\\') normNew.pop_back();
                if (!seg.empty() && EqualsIgnoreCase(seg, normNew)) {
                    return Value::FromBool(true); // Already present on PATH!
                }
                if (sc == std::string::npos) break;
                pos = sc + 1;
            }

            if (!curPath.empty() && curPath.back() != ';') curPath.push_back(';');
            curPath += dirToAdd;

            EvaluatedArgs rWrite;
            rWrite.pos = {Value::FromStr(hive), Value::FromStr(envKey), Value::FromStr("Path"), Value::FromStr(curPath)};
            const Value ok = CallReg("writeExpandString", rWrite, line);
            broadcastEnv();
            return ok;
        }
        RaiseError(line, "Unknown env.* method 'env." + m + "()'.");
        return Value::Null();
    }

    bool RegisterArpAndUninstallMeta(
        const std::string& rawId,
        const std::string& dispName,
        const std::string& dispVer,
        const std::string& publisher,
        const std::filesystem::path& installDir,
        const std::string& mainExeRel,
        const std::string& aboutUrl,
        bool perMachine,
        bool systemComponent,
        std::string& outErr)
    {
        std::error_code ec;
        ledger_.EnsureDirectoryTracked(installDir);

        // Copy running setup binary to `<InstallDir>\Uninstall.exe`
        const auto uninstExe = installDir / L"Uninstall.exe";
        if (!opts_.runningExePath.empty() && std::filesystem::exists(opts_.runningExePath, ec) &&
            _wcsicmp(opts_.runningExePath.wstring().c_str(), uninstExe.wstring().c_str()) != 0) {
            std::filesystem::copy_file(opts_.runningExePath, uninstExe, std::filesystem::copy_options::overwrite_existing, ec);
            if (ledger_.active) ledger_.newlyWrittenFiles.push_back(uninstExe);
        }

        auto slugifyLocal = [](std::string_view src) {
            std::string out;
            for (char c : src) {
                if ((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')) out.push_back(c);
                else if (c >= 'A' && c <= 'Z') out.push_back(static_cast<char>(c - 'A' + 'a'));
                else if (!out.empty() && out.back() != '-') out.push_back('-');
            }
            while (!out.empty() && out.back() == '-') out.pop_back();
            return out.empty() ? std::string("app") : out;
        };
        const std::string keyId = rawId.empty() ? ("RaggieSoft." + slugifyLocal(dispName)) : rawId;
        const std::string uninstSubKey = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + keyId;
        const std::string hive = perMachine ? "HKLM" : "HKCU";

        // Emit `<InstallDir>\Uninstall.rswm` compatible with `SetupWizard.exe --native-uninstall`!
        const auto metaPath = installDir / L"Uninstall.rswm";
        {
            std::ofstream um(metaPath, std::ios::binary | std::ios::trunc);
            if (!um) {
                outErr = "Failed writing Uninstall.rswm in '" + WideToUtf8(installDir.wstring()) + "'.";
                return false;
            }
            um << "[NativeUninstall]\n"
               << "ProductName=" << native_detail::EscapeSingleLine(dispName) << "\n"
               << "Version=" << native_detail::EscapeSingleLine(dispVer) << "\n"
               << "Manufacturer=" << native_detail::EscapeSingleLine(publisher) << "\n"
               << "PerMachine=" << (perMachine ? "true" : "false") << "\n"
               << "UninstallRegKey=" << uninstSubKey << "\n"
               << "InstallDir=" << WideToUtf8(installDir.wstring()) << "\n";
            um << "FileCount=" << ledger_.newlyWrittenFiles.size() << "\n";
            for (std::size_t i = 0; i < ledger_.newlyWrittenFiles.size(); ++i) {
                um << "File_" << i << "=" << WideToUtf8(ledger_.newlyWrittenFiles[i].wstring()) << "\n";
            }
            um << "ShortcutCount=" << ledger_.createdShortcuts.size() << "\n";
            for (std::size_t i = 0; i < ledger_.createdShortcuts.size(); ++i) {
                um << "Shortcut_" << i << "=" << WideToUtf8(ledger_.createdShortcuts[i].wstring()) << "\n";
            }
            um << "DirCount=" << ledger_.createdDirs.size() << "\n";
            for (std::size_t i = 0; i < ledger_.createdDirs.size(); ++i) {
                um << "Dir_" << i << "=" << WideToUtf8(ledger_.createdDirs[i].wstring()) << "\n";
            }
            um << "RegKeyCount=" << ledger_.uninstallRegKeys.size() << "\n";
            for (std::size_t i = 0; i < ledger_.uninstallRegKeys.size(); ++i) {
                um << "RegRoot_" << i << "=" << ledger_.uninstallRegKeys[i].first << "\n"
                   << "RegKey_"  << i << "=" << WideToUtf8(ledger_.uninstallRegKeys[i].second) << "\n";
            }
        }
        if (ledger_.active) ledger_.newlyWrittenFiles.push_back(metaPath);

        const std::string uninstCmd = "\"" + WideToUtf8(uninstExe.wstring()) + "\" --native-uninstall";
        const std::string quietCmd  = "\"" + WideToUtf8(uninstExe.wstring()) + "\" --native-uninstall /s";
        const std::string iconExe   = !mainExeRel.empty()
            ? WideToUtf8((installDir / Utf8ToWide(mainExeRel)).wstring())
            : WideToUtf8(uninstExe.wstring());

        auto setStr = [&](const char* vn, const std::string& val) {
            EvaluatedArgs ra;
            ra.pos = {Value::FromStr(hive), Value::FromStr(uninstSubKey), Value::FromStr(vn), Value::FromStr(val)};
            return CallReg("writeString", ra, 0).AsBool();
        };
        auto setDw = [&](const char* vn, std::int64_t val) {
            EvaluatedArgs ra;
            ra.pos = {Value::FromStr(hive), Value::FromStr(uninstSubKey), Value::FromStr(vn), Value::FromInt(val)};
            return CallReg("writeDword", ra, 0).AsBool();
        };

        if (!setStr("DisplayName", dispName) ||
            !setStr("DisplayVersion", dispVer) ||
            !setStr("Publisher", publisher) ||
            !setStr("InstallLocation", WideToUtf8(installDir.wstring())) ||
            !setStr("DisplayIcon", iconExe) ||
            !setStr("UninstallString", uninstCmd) ||
            !setStr("QuietUninstallString", quietCmd) ||
            !setDw("NoModify", prog_.repairBlock ? 0 : 1) ||
            !setDw("NoRepair", prog_.repairBlock ? 0 : 1)) {
            outErr = "Failed registering Installed Apps entry under " + hive + "\\" + uninstSubKey;
            return false;
        }
        if (!aboutUrl.empty()) setStr("URLInfoAbout", aboutUrl);
        if (systemComponent)   setDw("SystemComponent", 1);

        arpRegisteredDuringInstall_ = true;
        return true;
    }

    Value CallArp(const std::string& m, const EvaluatedArgs& args, int line) {
        if (m == "register") {
            // Supports either `arp.register({ id: "...", ... })` map OR named parameters!
            auto getField = [&](std::string_view key, std::size_t idx, const std::string& defVal) -> std::string {
                if (!args.pos.empty() && args.pos[0].kind == ValKind::Map && args.pos[0].mapVal) {
                    for (const auto& [mk, mv] : *args.pos[0].mapVal) {
                        if (EqualsIgnoreCase(mk, key)) return mv.AsString();
                    }
                    return defVal;
                }
                return args.NamedOrPos(key, idx, Value::FromStr(defVal)).AsString();
            };

            Value defDirV;
            globalScope_.TryGet("installDir", defDirV);
            const std::string id        = getField("id", 0, prog_.package.id);
            const std::string name      = getField("name", 1, prog_.package.name);
            const std::string ver       = getField("version", 2, prog_.package.version);
            const std::string pub       = getField("publisher", 3, prog_.package.publisher);
            const auto instDir          = std::filesystem::path(Utf8ToWide(getField("installDir", 4, defDirV.AsString())));
            const std::string mainExe   = getField("mainExe", 5, prog_.package.mainExe);
            const std::string aboutUrl  = getField("aboutUrl", 6, prog_.package.websiteUrl);
            const bool perMach          = EqualsIgnoreCase(getField("scope", 7, prog_.package.scope), "machine");
            const bool sysComp          = EqualsIgnoreCase(getField("systemComponent", 8, prog_.package.hideFromArp ? "true" : "false"), "true");

            std::string err;
            if (!RegisterArpAndUninstallMeta(id, name, ver, pub, instDir, mainExe, aboutUrl, perMach, sysComp, err)) {
                RaiseError(line, err);
                return Value::FromBool(false);
            }
            return Value::FromBool(true);
        }
        if (m == "setSystemComponent") {
            const std::string id      = args.At(0).AsString();
            const bool hidden         = args.At(1, Value::FromBool(true)).AsBool();
            const bool perMach        = args.At(2, Value::FromBool(EqualsIgnoreCase(prog_.package.scope, "machine"))).AsBool();
            const std::string hive    = perMach ? "HKLM" : "HKCU";
            const std::string subKey  = "Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + id;
            EvaluatedArgs ra;
            if (hidden) {
                ra.pos = {Value::FromStr(hive), Value::FromStr(subKey), Value::FromStr("SystemComponent"), Value::FromInt(1)};
                return CallReg("writeDword", ra, line);
            }
            ra.pos = {Value::FromStr(hive), Value::FromStr(subKey), Value::FromStr("SystemComponent")};
            return CallReg("deleteValue", ra, line);
        }
        if (m == "unregister") {
            const std::string id   = args.At(0).AsString();
            const bool perMach     = args.At(1, Value::FromBool(EqualsIgnoreCase(prog_.package.scope, "machine"))).AsBool();
            const std::string hive = perMach ? "HKLM" : "HKCU";
            EvaluatedArgs ra;
            ra.pos = {Value::FromStr(hive), Value::FromStr("Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + id)};
            return CallReg("deleteKeyTree", ra, line);
        }
        RaiseError(line, "Unknown arp.* method 'arp." + m + "()'.");
        return Value::Null();
    }

    // ----------------------------------------------------------------------------
    // Namespace 11: `svc.*`, `task.*`, `firewall.*`, & `acl.*`
    // ----------------------------------------------------------------------------
    Value CallSvc(const std::string& m, const EvaluatedArgs& args, int line) {
        const std::wstring svcName = Utf8ToWide(args.At(0).AsString());
        if (m == "exists" || m == "status") {
            SC_HANDLE hScm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
            if (!hScm) return (m == "exists") ? Value::FromBool(false) : Value::FromStr("unavailable");
            SC_HANDLE hSvc = ::OpenServiceW(hScm, svcName.c_str(), SERVICE_QUERY_STATUS);
            if (!hSvc) {
                ::CloseServiceHandle(hScm);
                return (m == "exists") ? Value::FromBool(false) : Value::FromStr("missing");
            }
            SERVICE_STATUS_PROCESS ssp{};
            DWORD cb = 0;
            ::QueryServiceStatusEx(hSvc, SC_STATUS_PROCESS_INFO, reinterpret_cast<LPBYTE>(&ssp), sizeof(ssp), &cb);
            ::CloseServiceHandle(hSvc);
            ::CloseServiceHandle(hScm);
            if (m == "exists") return Value::FromBool(true);
            switch (ssp.dwCurrentState) {
                case SERVICE_RUNNING: return Value::FromStr("running");
                case SERVICE_STOPPED: return Value::FromStr("stopped");
                case SERVICE_PAUSED:  return Value::FromStr("paused");
                default:              return Value::FromStr("pending");
            }
        }
        SC_HANDLE hScm = ::OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ALL_ACCESS);
        if (!hScm) {
            RaiseError(line, "OpenSCManagerW requires Administrator privileges.");
            return Value::FromBool(false);
        }
        if (m == "install") {
            const std::wstring disp = Utf8ToWide(args.NamedOrPos("displayName", 1, args.At(0)).AsString());
            const std::wstring bin  = args.NamedOrPos("binPath", 2).AsPath().wstring();
            const std::string stStr = args.NamedOrPos("startType", 3, Value::FromStr("auto")).AsString();
            const std::wstring desc = Utf8ToWide(args.NamedOrPos("description", 4, Value::FromStr("")).AsString());
            DWORD startFlag = SERVICE_AUTO_START;
            if (EqualsIgnoreCase(stStr, "demand") || EqualsIgnoreCase(stStr, "manual")) startFlag = SERVICE_DEMAND_START;
            else if (EqualsIgnoreCase(stStr, "disabled")) startFlag = SERVICE_DISABLED;

            SC_HANDLE hSvc = ::CreateServiceW(
                hScm, svcName.c_str(), disp.c_str(), SERVICE_ALL_ACCESS,
                SERVICE_WIN32_OWN_PROCESS, startFlag, SERVICE_ERROR_NORMAL,
                bin.c_str(), nullptr, nullptr, nullptr, nullptr, nullptr);
            if (!hSvc) {
                ::CloseServiceHandle(hScm);
                RaiseError(line, "CreateServiceW failed for '" + WideToUtf8(svcName) + "'.");
                return Value::FromBool(false);
            }
            if (!desc.empty()) {
                SERVICE_DESCRIPTIONW sd{const_cast<LPWSTR>(desc.c_str())};
                ::ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_DESCRIPTION, &sd);
            }
            ::CloseServiceHandle(hSvc);
            ::CloseServiceHandle(hScm);
            if (ledger_.active) ledger_.createdServices.push_back(svcName);
            return Value::FromBool(true);
        }
        if (m == "configureFailureReset") {
            const DWORD resetSec = static_cast<DWORD>(args.NamedOrPos("resetSeconds", 1, Value::FromInt(86400)).AsInt());
            const DWORD delayMs  = static_cast<DWORD>(args.NamedOrPos("restartDelayMs", 2, Value::FromInt(5000)).AsInt());
            SC_HANDLE hSvc = ::OpenServiceW(hScm, svcName.c_str(), SERVICE_CHANGE_CONFIG | SERVICE_START);
            bool ok = false;
            if (hSvc) {
                SC_ACTION actions[3] = {
                    {SC_ACTION_RESTART, delayMs},
                    {SC_ACTION_RESTART, delayMs * 2},
                    {SC_ACTION_NONE,    0}
                };
                SERVICE_FAILURE_ACTIONSW sfa{};
                sfa.dwResetPeriod = resetSec;
                sfa.cActions      = 3;
                sfa.lpsaActions   = actions;
                ok = (::ChangeServiceConfig2W(hSvc, SERVICE_CONFIG_FAILURE_ACTIONS, &sfa) != FALSE);
                ::CloseServiceHandle(hSvc);
            }
            ::CloseServiceHandle(hScm);
            return Value::FromBool(ok);
        }
        if (m == "start" || m == "stop" || m == "remove") {
            SC_HANDLE hSvc = ::OpenServiceW(hScm, svcName.c_str(), SERVICE_START | SERVICE_STOP | DELETE | SERVICE_QUERY_STATUS);
            bool ok = false;
            if (hSvc) {
                if (m == "start") {
                    ok = (::StartServiceW(hSvc, 0, nullptr) != FALSE || ::GetLastError() == ERROR_SERVICE_ALREADY_RUNNING);
                } else if (m == "stop") {
                    SERVICE_STATUS ss{};
                    ok = (::ControlService(hSvc, SERVICE_CONTROL_STOP, &ss) != FALSE || ::GetLastError() == ERROR_SERVICE_NOT_ACTIVE);
                } else {
                    SERVICE_STATUS ss{};
                    ::ControlService(hSvc, SERVICE_CONTROL_STOP, &ss);
                    ok = (::DeleteService(hSvc) != FALSE);
                }
                ::CloseServiceHandle(hSvc);
            }
            ::CloseServiceHandle(hScm);
            return Value::FromBool(ok);
        }
        ::CloseServiceHandle(hScm);
        RaiseError(line, "Unknown svc.* method 'svc." + m + "()'.");
        return Value::Null();
    }

    Value CallTask(const std::string& m, const EvaluatedArgs& args, int line) {
        const std::string taskName = args.At(0).AsString();
        const auto sys32 = host_detail::GetKnownFolderOrEnv(FOLDERID_System, nullptr);
        const auto schtasks = sys32 / L"schtasks.exe";
        std::int64_t code = 0;
        std::string err;

        if (m == "exists") {
            host_detail::RunProcessWithTokenMode(schtasks, "/Query /TN \"" + taskName + "\"", {}, true, true, "current", code, err);
            return Value::FromBool(code == 0);
        }
        if (m == "createWeekly") {
            const std::string exe  = WideToUtf8(args.NamedOrPos("exe", 1).AsPath().wstring());
            const std::string tArg = args.NamedOrPos("args", 2, Value::FromStr("")).AsString();
            const std::string day  = args.NamedOrPos("day", 3, Value::FromStr("MON")).AsString();
            const std::string time = args.NamedOrPos("time", 4, Value::FromStr("12:00")).AsString();
            const std::string tr   = tArg.empty() ? ("\\\"" + exe + "\\\"") : ("\\\"" + exe + "\\\" " + tArg);
            const std::string cmd  = "/Create /SC WEEKLY /D " + day + " /TN \"" + taskName + "\" /TR \"" + tr + "\" /ST " + time + " /F";
            host_detail::RunProcessWithTokenMode(schtasks, cmd, {}, true, true, "current", code, err);
            if (code == 0 && ledger_.active) ledger_.createdTasks.push_back(Utf8ToWide(taskName));
            return Value::FromBool(code == 0);
        }
        if (m == "remove") {
            host_detail::RunProcessWithTokenMode(schtasks, "/Delete /TN \"" + taskName + "\" /F", {}, true, true, "current", code, err);
            return Value::FromBool(code == 0);
        }
        RaiseError(line, "Unknown task.* method 'task." + m + "()'.");
        return Value::Null();
    }

    Value CallFirewall(const std::string& m, const EvaluatedArgs& args, int line) {
        const std::string ruleName = args.At(0).AsString();
        const auto netsh = host_detail::GetKnownFolderOrEnv(FOLDERID_System, nullptr) / L"netsh.exe";
        std::int64_t code = 0;
        std::string err;

        if (m == "allowProgram") {
            const std::string exe = WideToUtf8(args.NamedOrPos("exe", 1).AsPath().wstring());
            const std::string dir = args.NamedOrPos("dir", 2, Value::FromStr("in")).AsString();
            const std::string cmd = "advfirewall firewall add rule name=\"" + ruleName + "\" dir=" + dir +
                                    " action=allow program=\"" + exe + "\" enable=yes";
            host_detail::RunProcessWithTokenMode(netsh, cmd, {}, true, true, "current", code, err);
            if (code == 0 && ledger_.active) ledger_.createdFirewallRules.push_back(Utf8ToWide(ruleName));
            return Value::FromBool(code == 0);
        }
        if (m == "removeRule") {
            const std::string cmd = "advfirewall firewall delete rule name=\"" + ruleName + "\"";
            host_detail::RunProcessWithTokenMode(netsh, cmd, {}, true, true, "current", code, err);
            return Value::FromBool(code == 0);
        }
        RaiseError(line, "Unknown firewall.* method 'firewall." + m + "()'.");
        return Value::Null();
    }

    Value CallAcl(const std::string& m, const EvaluatedArgs& args, int line) {
        if (m == "grantModify") {
            const std::string target = WideToUtf8(args.At(0).AsPath().wstring());
            const std::string trustee = args.At(1, Value::FromStr("*S-1-5-32-545")).AsString(); // Builtin Users SID default
            const auto icacls = host_detail::GetKnownFolderOrEnv(FOLDERID_System, nullptr) / L"icacls.exe";
            std::int64_t code = 0;
            std::string err;
            host_detail::RunProcessWithTokenMode(
                icacls, "\"" + target + "\" /grant \"" + trustee + "\":(OI)(CI)M /T /C /Q",
                {}, true, true, "current", code, err);
            return Value::FromBool(code == 0);
        }
        RaiseError(line, "Unknown acl.* method 'acl." + m + "()'.");
        return Value::Null();
    }

    // ----------------------------------------------------------------------------
    // Namespace 12: `ui.*` & `showcase.*` (Native `TaskDialogIndirect` — Zero `MessageBox`!)
    // ----------------------------------------------------------------------------
    Value CallUi(const std::string& m, const EvaluatedArgs& args) {
        if (m == "progress") {
            const int pct     = std::clamp<int>(static_cast<int>(args.At(0).AsInt()), 0, 100);
            const std::wstring status = Utf8ToWide(args.At(1, Value::FromStr("")).AsString());
            const int compPct = std::clamp<int>(static_cast<int>(args.At(2, Value::FromInt(pct)).AsInt()), 0, 100);
            if (opts_.onProgress) {
                opts_.onProgress(pct * 10, compPct * 10, status);
            }
            return Value::Null();
        }

        // Non-blocking silent/headless fallback so automated builds and `/s` installs never hang!
        wchar_t headlessBtnBuf[32]{};
        const bool hasHeadlessOverride =
            (::GetEnvironmentVariableW(L"RSW_HEADLESS_UI_DEFAULT_BTN", headlessBtnBuf, 32) > 0);
        if (opts_.silentMode || hasHeadlessOverride) {
            const int defBtn = hasHeadlessOverride ? _wtoi(headlessBtnBuf) : IDOK;
            if (m == "confirm") return Value::FromBool(true);
            if (m == "pickFolder" || m == "pickFile") return args.At(1, Value::FromStr(""));
            if (m == "taskDialog") {
                Value refChoice = args.RawNamedOrPos("choice", 7);
                std::string dummyErr;
                WriteThroughRef(refChoice, Value::FromInt(defBtn), dummyErr);
                return Value::FromMap({
                    {"button",   Value::FromInt(defBtn)},
                    {"verified", Value::FromBool(false)}
                });
            }
            return Value::FromInt(defBtn);
        }

        if (m == "info" || m == "warn" || m == "error") {
            const std::wstring head = Utf8ToWide(args.NamedOrPos("heading", 0).AsString());
            const std::wstring body = Utf8ToWide(args.NamedOrPos("body", 1, Value::FromStr("")).AsString());
            const std::wstring det  = Utf8ToWide(args.NamedOrPos("details", 2, Value::FromStr("")).AsString());
            const std::wstring tit  = Utf8ToWide(args.NamedOrPos("title", 3, Value::FromStr(prog_.package.name + " Setup")).AsString());

            TASKDIALOGCONFIG tdc{};
            tdc.cbSize             = sizeof(tdc);
            tdc.hwndParent         = opts_.parentHwnd;
            tdc.dwCommonButtons    = TDCBF_OK_BUTTON;
            tdc.pszWindowTitle     = tit.c_str();
            tdc.pszMainInstruction = head.c_str();
            tdc.pszContent         = body.c_str();
            tdc.pszMainIcon        = (m == "error") ? TD_ERROR_ICON : ((m == "warn") ? TD_WARNING_ICON : TD_INFORMATION_ICON);
            if (!det.empty()) {
                tdc.pszCollapsedControlText = L"Show technical details";
                tdc.pszExpandedControlText  = L"Hide technical details";
                tdc.pszExpandedInformation  = det.c_str();
            }
            int pressed = IDOK;
            ::TaskDialogIndirect(&tdc, &pressed, nullptr, nullptr);
            return Value::FromInt(pressed);
        }

        if (m == "confirm") {
            const std::wstring head    = Utf8ToWide(args.NamedOrPos("heading", 0).AsString());
            const std::wstring body    = Utf8ToWide(args.NamedOrPos("body", 1, Value::FromStr("")).AsString());
            const std::wstring yesText = Utf8ToWide(args.NamedOrPos("confirmText", 2, Value::FromStr("&Continue")).AsString());
            const bool shield          = args.NamedOrPos("shield", 3, Value::FromBool(false)).AsBool();
            const std::wstring tit     = Utf8ToWide(prog_.package.name + " Setup");

            TASKDIALOG_BUTTON btns[2] = {
                {IDYES, yesText.c_str()},
                {IDNO,  L"Cancel"}
            };
            host_detail::TaskDialogShieldContext sctx{};
            if (shield) sctx.shieldButtonIds.push_back(IDYES);

            TASKDIALOGCONFIG tdc{};
            tdc.cbSize             = sizeof(tdc);
            tdc.hwndParent         = opts_.parentHwnd;
            tdc.pszWindowTitle     = tit.c_str();
            tdc.pszMainInstruction = head.c_str();
            tdc.pszContent         = body.c_str();
            tdc.pszMainIcon        = shield ? TD_SHIELD_ICON : TD_INFORMATION_ICON;
            tdc.pButtons           = btns;
            tdc.cButtons           = 2;
            tdc.nDefaultButton     = IDYES;
            tdc.pfCallback         = &host_detail::TaskDialogCallbackProc;
            tdc.lpCallbackData     = reinterpret_cast<LONG_PTR>(&sctx);

            int pressed = IDNO;
            ::TaskDialogIndirect(&tdc, &pressed, nullptr, nullptr);
            return Value::FromBool(pressed == IDYES);
        }

        if (m == "taskDialog") {
            const std::wstring title       = Utf8ToWide(args.NamedOrPos("title", 0, Value::FromStr(prog_.package.name + " Setup")).AsString());
            const std::wstring heading     = Utf8ToWide(args.NamedOrPos("heading", 1).AsString());
            const std::wstring body        = Utf8ToWide(args.NamedOrPos("body", 2, Value::FromStr("")).AsString());
            const std::string  iconStr     = args.NamedOrPos("icon", 3, Value::FromStr("info")).AsString();
            const std::wstring detTitle    = Utf8ToWide(args.NamedOrPos("detailsTitle", 4, Value::FromStr("Technical details")).AsString());
            const std::wstring detBody     = Utf8ToWide(args.NamedOrPos("detailsBody", 5, Value::FromStr("")).AsString());
            const std::wstring verifyTxt   = Utf8ToWide(args.NamedOrPos("verifyText", 6, Value::FromStr("")).AsString());
            const std::wstring footerTxt   = Utf8ToWide(args.NamedOrPos("footer", 8, Value::FromStr("")).AsString());
            const Value buttonsVal         = args.NamedOrPos("buttons", 9);
            const bool commandLinks        = args.NamedOrPos("commandLinks", 10, Value::FromBool(true)).AsBool();

            std::vector<std::wstring> btnLabels;
            std::vector<TASKDIALOG_BUTTON> tdBtns;
            host_detail::TaskDialogShieldContext sctx{};

            if (buttonsVal.kind == ValKind::Array && buttonsVal.arrVal && !buttonsVal.arrVal->empty()) {
                btnLabels.reserve(buttonsVal.arrVal->size());
                for (std::size_t i = 0; i < buttonsVal.arrVal->size(); ++i) {
                    const Value& bv = (*buttonsVal.arrVal)[i];
                    int bid = static_cast<int>(100 + i);
                    std::wstring blab;
                    if (bv.kind == ValKind::Map && bv.mapVal) {
                        if (bv.mapVal->count("id"))   bid  = static_cast<int>((*bv.mapVal)["id"].AsInt());
                        if (bv.mapVal->count("text")) blab = Utf8ToWide((*bv.mapVal)["text"].AsString());
                        if (bv.mapVal->count("subtitle") && !(*bv.mapVal)["subtitle"].AsString().empty()) {
                            blab += L"\n" + Utf8ToWide((*bv.mapVal)["subtitle"].AsString());
                        }
                        if (bv.mapVal->count("shield") && (*bv.mapVal)["shield"].AsBool()) {
                            sctx.shieldButtonIds.push_back(bid);
                        }
                    } else {
                        blab = Utf8ToWide(bv.AsString());
                    }
                    btnLabels.push_back(std::move(blab));
                    tdBtns.push_back({bid, btnLabels.back().c_str()});
                }
            }

            TASKDIALOGCONFIG tdc{};
            tdc.cbSize             = sizeof(tdc);
            tdc.hwndParent         = opts_.parentHwnd;
            tdc.dwFlags            = TDF_ALLOW_DIALOG_CANCELLATION | ((!tdBtns.empty() && commandLinks) ? TDF_USE_COMMAND_LINKS : 0);
            tdc.pszWindowTitle     = title.c_str();
            tdc.pszMainInstruction = heading.c_str();
            tdc.pszContent         = body.c_str();
            if (EqualsIgnoreCase(iconStr, "error"))        tdc.pszMainIcon = TD_ERROR_ICON;
            else if (EqualsIgnoreCase(iconStr, "warning")) tdc.pszMainIcon = TD_WARNING_ICON;
            else if (EqualsIgnoreCase(iconStr, "shield"))  tdc.pszMainIcon = TD_SHIELD_ICON;
            else                                           tdc.pszMainIcon = TD_INFORMATION_ICON;

            if (!tdBtns.empty()) {
                tdc.pButtons = tdBtns.data();
                tdc.cButtons = static_cast<UINT>(tdBtns.size());
            } else {
                tdc.dwCommonButtons = TDCBF_OK_BUTTON;
            }
            if (!detBody.empty()) {
                tdc.pszCollapsedControlText = detTitle.c_str();
                tdc.pszExpandedInformation  = detBody.c_str();
            }
            if (!verifyTxt.empty()) tdc.pszVerificationText = verifyTxt.c_str();
            if (!footerTxt.empty()) tdc.pszFooter           = footerTxt.c_str();
            tdc.pfCallback     = &host_detail::TaskDialogCallbackProc;
            tdc.lpCallbackData = reinterpret_cast<LONG_PTR>(&sctx);

            int pressed = IDCANCEL;
            BOOL verified = FALSE;
            ::TaskDialogIndirect(&tdc, &pressed, nullptr, &verified);

            std::string refErr;
            WriteThroughRef(args.RawNamedOrPos("choice", 7), Value::FromInt(pressed), refErr);
            WriteThroughRef(args.RawNamedOrPos("verifiedOut", 11), Value::FromBool(verified != FALSE), refErr);
            return Value::FromMap({
                {"button",   Value::FromInt(pressed)},
                {"verified", Value::FromBool(verified != FALSE)}
            });
        }

        if (m == "pickFolder" || m == "pickFile") {
            const std::wstring title = Utf8ToWide(args.NamedOrPos("title", 0, Value::FromStr("Select Location")).AsString());
            const std::wstring initD = Utf8ToWide(args.NamedOrPos("initialDir", 1, Value::FromStr("")).AsString());
            const HRESULT hrCo = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
            const bool needUn  = (hrCo == S_OK || hrCo == S_FALSE);

            IFileOpenDialog* pDlg = nullptr;
            std::filesystem::path chosen;
            if (SUCCEEDED(::CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&pDlg))) && pDlg) {
                DWORD fos = 0;
                pDlg->GetOptions(&fos);
                fos |= FOS_FORCEFILESYSTEM | ((m == "pickFolder") ? FOS_PICKFOLDERS : FOS_FILEMUSTEXIST);
                pDlg->SetOptions(fos);
                pDlg->SetTitle(title.c_str());
                if (!initD.empty()) {
                    IShellItem* pInit = nullptr;
                    if (SUCCEEDED(::SHCreateItemFromParsingName(initD.c_str(), nullptr, IID_PPV_ARGS(&pInit))) && pInit) {
                        pDlg->SetFolder(pInit);
                        pInit->Release();
                    }
                }
                if (SUCCEEDED(pDlg->Show(opts_.parentHwnd))) {
                    IShellItem* pRes = nullptr;
                    if (SUCCEEDED(pDlg->GetResult(&pRes)) && pRes) {
                        PWSTR psz = nullptr;
                        if (SUCCEEDED(pRes->GetDisplayName(SIGDN_FILESYSPATH, &psz)) && psz) {
                            chosen = psz;
                            ::CoTaskMemFree(psz);
                        }
                        pRes->Release();
                    }
                }
                pDlg->Release();
            }
            if (needUn) ::CoUninitialize();
            return chosen.empty() ? Value::FromStr("") : Value::FromPath(chosen);
        }

        return Value::Null();
    }

    Value CallShowcase(const std::string& m, const EvaluatedArgs& args) {
        if (m == "navigate") {
            showcaseState_["currentPage"] = Value::FromStr(args.At(0).AsString());
            return showcaseState_["currentPage"];
        }
        if (m == "setHotspotEnabled") {
            showcaseState_["hotspot_enabled_" + args.At(0).AsString()] = Value::FromBool(args.At(1, Value::FromBool(true)).AsBool());
            return Value::FromBool(true);
        }
        if (m == "setHotspotBadge") {
            showcaseState_["hotspot_badge_" + args.At(0).AsString()] = Value::FromStr(args.At(1).AsString());
            return Value::FromBool(true);
        }
        if (m == "playAudio") {
            // WCAG 2.2 AA rule: never force-unmute audio unless user explicitly unmuted or opted in!
            showcaseState_["activeTrack"] = Value::FromStr(args.At(0).AsString());
            return Value::FromBool(!showcaseState_["audioMuted"].AsBool());
        }
        if (m == "stopAudio") {
            showcaseState_["activeTrack"] = Value::FromStr("");
            return Value::FromBool(true);
        }
        if (m == "getState") {
            return Value::FromMap(showcaseState_);
        }
        return Value::Null();
    }
};

} // namespace rsw::rssc
