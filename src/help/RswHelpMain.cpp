// ================================================================================
// FILE: src/help/RswHelpMain.cpp
// PROJECT: RaggieSoft Installer Studio — RaggieSoftHelp.exe ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   Why did we convert `RaggieSoftHelp.exe` into a 100% Native Win32 C++20 binary?
//     The whole point of **RaggieSoft Installer Studio** is to have **ZERO external
//     runtime dependencies**—running cold on any stock Windows 11 `x64` or `ARM64`
//     workstation without needing `.NET` or `WebView2` pre-installed!
//
//   How does `RaggieSoftHelp.exe` stay 100% plug-compatible with RaggieSoft's
//   modular `.rshm.json` Help Architecture while needing zero external runtimes?
//     1. **Exact 5-Stage `module.rshm.json` Discovery Contract**:
//        Scans `--module-dir`, `<ExeDir>\HelpModules\*`, `<ExeDir>\..\HelpModules\*`,
//        `%CommonProgramFiles%\RaggieSoft\HelpModules\*`,
//        `%LOCALAPPDATA%\RaggieSoft\HelpModules\*`, and `HKLM` / `HKCU`
//        `Software\RaggieSoft\HelpModules` registry string values—so every
//        installed RaggieSoft product automatically plugs into one shared Help hub!
//     2. **Instant Single-Instance `F1` Named Pipe IPC**:
//        Listens on `\\.\pipe\RaggieSoft.HelpViewer.IPC.<SanitizedUserName>`.
//        Pressing `F1` inside `InstallerStudio.exe` forwards JSON
//        `{"ModuleId":"...","TopicId":"...","AnchorId":"..."}` to an already-open
//        `RaggieSoftHelp.exe` window in under 5 milliseconds instead of spawning
//        duplicate windows!
//     3. **In-Process HTML5-to-RichText (`RTF`) Typography Engine (`Msftedit.dll`)**:
//        Converts semantic HTML5 topic files (`<h1>`, `<h2>`, `<h3>`, `<p>`, `<ul>`,
//        `<ol>`, `<li>`, `<pre>`, `<code>`, `<table>`, `<blockquote>`) into themed
//        RTF streamed directly into Windows' built-in `System32\Msftedit.dll`
//        (`MSFTEDIT_CLASS` RichEdit 4.1) with >= 15.8:1 WCAG AAA contrast in Dark
//        Mode, Light Mode, and Windows High Contrast!
//     4. **Dark-Mode-Safe Win32 Checkboxes & `SysListView32` Headers**:
//        Uses `AttachAccessibleCheckboxSubclass` on `"Search &all installed modules"`
//        and `AttachDarkAwareListViewSubclass` on the `Index` and `Search` grids so
//        there is NEVER black-on-black checkbox text or blinding white ListView
//        header strips in Dark Mode!
// ================================================================================

#include "../common/RswGlassRenderer.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswPlatform.hpp"
#include "../common/RswVersion.hpp"

#include <richedit.h>
#include <shellapi.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#pragma comment(lib, "shell32.lib")

namespace {

constexpr UINT WM_RSW_HELP_IPC_NAV = WM_APP + 50;

// Accessible Win32 Child Control IDs
enum HelpCtrlId : int {
    IDC_HLP_MOD_COMBO     = 5001,
    IDC_HLP_BACK_BTN      = 5002,
    IDC_HLP_FWD_BTN       = 5003,
    IDC_HLP_HOME_BTN      = 5004,
    IDC_HLP_WEBDOCS_BTN   = 5005,

    IDC_HLP_TAB_CONTENTS  = 5010,
    IDC_HLP_TAB_INDEX     = 5011,
    IDC_HLP_TAB_SEARCH    = 5012,

    IDC_HLP_TOC_TREE      = 5020,

    IDC_HLP_IDX_FILTER    = 5030,
    IDC_HLP_IDX_LIST      = 5031,

    IDC_HLP_SRCH_QUERY    = 5040,
    IDC_HLP_SRCH_BTN      = 5041,
    IDC_HLP_SRCH_ALL_CHK  = 5042,
    IDC_HLP_SRCH_LIST     = 5043,

    IDC_HLP_READER_RICH   = 5050
};

enum class LeftNavPane : int {
    Contents = 0,
    Index    = 1,
    Search   = 2
};

// ================================================================================
// 1. ZERO-DEPENDENCY UTF-8 JSON PARSER FOR `module.rshm.json` & IPC MESSAGES
// ================================================================================
// A compact, standards-compliant recursive-descent JSON reader so `RaggieSoftHelp`
// parses `module.rshm.json` manifests and Named Pipe IPC payloads with zero third-party
// header dependencies!
struct JsonValue {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind                             kind   = Kind::Null;
    bool                             bVal   = false;
    double                           nVal   = 0.0;
    std::string                      sVal;
    std::vector<JsonValue>           arrVal;
    std::map<std::string, JsonValue> objVal;

    std::string GetStr(const char* key, const char* defVal = "") const {
        if (kind != Kind::Object) return defVal;
        for (const auto& [k, v] : objVal) {
            if (rsw::EqualsIgnoreCase(k, key) && v.kind == Kind::String) return v.sVal;
        }
        return defVal;
    }

    const std::vector<JsonValue>* GetArr(const char* key) const {
        if (kind != Kind::Object) return nullptr;
        for (const auto& [k, v] : objVal) {
            if (rsw::EqualsIgnoreCase(k, key) && v.kind == Kind::Array) return &v.arrVal;
        }
        return nullptr;
    }
};

class MiniJsonParser {
public:
    explicit MiniJsonParser(std::string_view src) noexcept : src_(src) {}

    bool ParseRoot(JsonValue& out) {
        SkipWs();
        // Skip optional UTF-8 BOM (`EF BB BF`) if present at byte 0
        if (pos_ + 3 <= src_.size() &&
            static_cast<unsigned char>(src_[pos_]) == 0xEF &&
            static_cast<unsigned char>(src_[pos_ + 1]) == 0xBB &&
            static_cast<unsigned char>(src_[pos_ + 2]) == 0xBF) {
            pos_ += 3;
            SkipWs();
        }
        return ParseValue(out);
    }

private:
    std::string_view src_;
    std::size_t      pos_ = 0;

    void SkipWs() noexcept {
        while (pos_ < src_.size()) {
            const char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++pos_;
            else break;
        }
    }

    bool ParseValue(JsonValue& out) {
        SkipWs();
        if (pos_ >= src_.size()) return false;
        const char c = src_[pos_];
        if (c == '{') return ParseObject(out);
        if (c == '[') return ParseArray(out);
        if (c == '"') {
            out = JsonValue{};
            out.kind = JsonValue::Kind::String;
            return ParseString(out.sVal);
        }
        if (c == 't' && src_.substr(pos_, 4) == "true") {
            pos_ += 4;
            out = JsonValue{};
            out.kind = JsonValue::Kind::Bool;
            out.bVal = true;
            return true;
        }
        if (c == 'f' && src_.substr(pos_, 5) == "false") {
            pos_ += 5;
            out = JsonValue{};
            out.kind = JsonValue::Kind::Bool;
            out.bVal = false;
            return true;
        }
        if (c == 'n' && src_.substr(pos_, 4) == "null") {
            pos_ += 4;
            out = JsonValue{};
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = pos_;
            if (src_[pos_] == '-') ++pos_;
            while (pos_ < src_.size() && ((src_[pos_] >= '0' && src_[pos_] <= '9') ||
                   src_[pos_] == '.' || src_[pos_] == 'e' || src_[pos_] == 'E' ||
                   src_[pos_] == '+' || src_[pos_] == '-')) {
                ++pos_;
            }
            out = JsonValue{};
            out.kind = JsonValue::Kind::Number;
            out.nVal = std::strtod(std::string(src_.substr(start, pos_ - start)).c_str(), nullptr);
            return true;
        }
        return false;
    }

    bool ParseString(std::string& out) {
        out.clear();
        if (pos_ >= src_.size() || src_[pos_] != '"') return false;
        ++pos_;
        while (pos_ < src_.size()) {
            const char c = src_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= src_.size()) return false;
                const char esc = src_[pos_++];
                switch (esc) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u': {
                        if (pos_ + 4 > src_.size()) return false;
                        unsigned int cp = 0;
                        for (int k = 0; k < 4; ++k) {
                            const char h = src_[pos_++];
                            cp <<= 4;
                            if (h >= '0' && h <= '9') cp |= static_cast<unsigned int>(h - '0');
                            else if (h >= 'a' && h <= 'f') cp |= static_cast<unsigned int>(10 + h - 'a');
                            else if (h >= 'A' && h <= 'F') cp |= static_cast<unsigned int>(10 + h - 'A');
                        }
                        wchar_t wc[2] = {static_cast<wchar_t>(cp), L'\0'};
                        out += rsw::WideToUtf8(wc);
                        break;
                    }
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        return false;
    }

    bool ParseArray(JsonValue& out) {
        out = JsonValue{};
        out.kind = JsonValue::Kind::Array;
        ++pos_; // skip '['
        SkipWs();
        if (pos_ < src_.size() && src_[pos_] == ']') { ++pos_; return true; }
        while (pos_ < src_.size()) {
            JsonValue elem;
            if (!ParseValue(elem)) return false;
            out.arrVal.push_back(std::move(elem));
            SkipWs();
            if (pos_ < src_.size() && src_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < src_.size() && src_[pos_] == ']') { ++pos_; return true; }
            return false;
        }
        return false;
    }

    bool ParseObject(JsonValue& out) {
        out = JsonValue{};
        out.kind = JsonValue::Kind::Object;
        ++pos_; // skip '{'
        SkipWs();
        if (pos_ < src_.size() && src_[pos_] == '}') { ++pos_; return true; }
        while (pos_ < src_.size()) {
            SkipWs();
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (pos_ >= src_.size() || src_[pos_] != ':') return false;
            ++pos_;
            JsonValue val;
            if (!ParseValue(val)) return false;
            out.objVal[std::move(key)] = std::move(val);
            SkipWs();
            if (pos_ < src_.size() && src_[pos_] == ',') { ++pos_; continue; }
            if (pos_ < src_.size() && src_[pos_] == '}') { ++pos_; return true; }
            return false;
        }
        return false;
    }
};

// ================================================================================
// 2. PLUGGABLE `.rshm.json` HELP MODULE DATA MODEL & 5-STAGE DISCOVERY SERVICE
// ================================================================================
struct HelpTopicEntry {
    std::string              id;
    std::string              fileName;
    std::string              category;
    std::string              title;
    std::string              summary;
    std::vector<std::string> keywords;
    std::string              cachedPlainTextLower; // Pre-indexed plain text for instant full-text search!
};

struct HelpIndexEntry {
    std::string entryId;
    std::string displayText;
    std::string category;
    std::string badge;
    std::string subtitle;
    std::string topicId;
    std::string anchorId;
};

struct LoadedHelpModule {
    std::filesystem::path       moduleDir;
    std::string                 moduleId;
    std::string                 productName;
    std::string                 productVersion;
    std::string                 publisher;
    std::string                 description;
    std::string                 defaultTopicId;
    std::string                 onlineDocsUrl;
    std::vector<HelpTopicEntry> topics;
    std::vector<HelpIndexEntry> indexEntries;
};

std::string LowerUtf8(std::string_view sv) {
    std::string out(sv);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return out;
}

std::string ReadUtf8FileToString(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

/// Decodes standard HTML5 named & numeric entities into UTF-8 text.
std::string DecodeHtmlEntitiesUtf8(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '&') {
            const auto semi = raw.find(';', i + 1);
            if (semi != std::string_view::npos && semi - i <= 12) {
                const std::string_view ent = raw.substr(i + 1, semi - i - 1);
                std::string repl;
                if (ent == "amp")        repl = "&";
                else if (ent == "lt")    repl = "<";
                else if (ent == "gt")    repl = ">";
                else if (ent == "quot")  repl = "\"";
                else if (ent == "apos" || ent == "#39") repl = "'";
                else if (ent == "nbsp")  repl = " ";
                else if (ent == "rarr")  repl = "->";
                else if (ent == "larr")  repl = "<-";
                else if (ent == "mdash") repl = "\xE2\x80\x94";
                else if (ent == "ndash") repl = "\xE2\x80\x93";
                else if (ent == "bull")  repl = "\xE2\x80\xA2";
                else if (ent == "ge")    repl = ">=";
                else if (ent == "le")    repl = "<=";
                else if (ent == "times") repl = "x";
                else if (ent == "copy")  repl = "(c)";
                else if (!ent.empty() && ent[0] == '#') {
                    unsigned int cp = 0;
                    if (ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X')) {
                        cp = static_cast<unsigned int>(std::strtoul(std::string(ent.substr(2)).c_str(), nullptr, 16));
                    } else {
                        cp = static_cast<unsigned int>(std::strtoul(std::string(ent.substr(1)).c_str(), nullptr, 10));
                    }
                    if (cp > 0 && cp <= 0xFFFFu) {
                        wchar_t wc[2] = {static_cast<wchar_t>(cp), L'\0'};
                        repl = rsw::WideToUtf8(wc);
                    }
                }
                if (!repl.empty()) {
                    out += repl;
                    i = semi;
                    continue;
                }
            }
        }
        out.push_back(raw[i]);
    }
    return out;
}

/// Strips HTML tags to produce clean lowercase text for instant full-text topic search.
std::string StripHtmlTagsToSearchableLower(std::string_view html) {
    std::string plain;
    plain.reserve(html.size());
    bool inTag = false;
    bool inSkipBlock = false;
    for (std::size_t i = 0; i < html.size(); ++i) {
        if (!inTag && html[i] == '<') {
            inTag = true;
            const auto rem = LowerUtf8(html.substr(i, 9));
            if (rem.rfind("<style", 0) == 0 || rem.rfind("<script", 0) == 0) inSkipBlock = true;
            if (rem.rfind("</style", 0) == 0 || rem.rfind("</script", 0) == 0) inSkipBlock = false;
            plain.push_back(' ');
            continue;
        }
        if (inTag) {
            if (html[i] == '>') inTag = false;
            continue;
        }
        if (!inSkipBlock) plain.push_back(html[i]);
    }
    return LowerUtf8(DecodeHtmlEntitiesUtf8(plain));
}

bool TryLoadHelpModuleDir(const std::filesystem::path& dirPath, LoadedHelpModule& outMod) {
    std::error_code ec;
    const auto manifestFile = dirPath / L"module.rshm.json";
    if (!std::filesystem::exists(manifestFile, ec)) return false;

    const std::string rawJson = ReadUtf8FileToString(manifestFile);
    JsonValue root;
    MiniJsonParser parser(rawJson);
    if (!parser.ParseRoot(root) || root.kind != JsonValue::Kind::Object) return false;

    LoadedHelpModule m{};
    m.moduleDir      = std::filesystem::weakly_canonical(dirPath, ec);
    if (m.moduleDir.empty()) m.moduleDir = dirPath;
    m.moduleId       = root.GetStr("moduleId");
    m.productName    = root.GetStr("productName", "RaggieSoft Documentation");
    m.productVersion = root.GetStr("productVersion", "0.4.0");
    m.publisher      = root.GetStr("publisher", "RaggieSoft");
    m.description    = root.GetStr("description");
    m.defaultTopicId = root.GetStr("defaultTopicId");
    m.onlineDocsUrl  = root.GetStr("onlineDocsUrl");
    if (m.moduleId.empty()) return false;

    if (const auto* tArr = root.GetArr("topics")) {
        for (const auto& tj : *tArr) {
            HelpTopicEntry t{};
            t.id       = tj.GetStr("id");
            t.fileName = tj.GetStr("fileName");
            t.category = tj.GetStr("category", "General");
            t.title    = tj.GetStr("title", t.id.c_str());
            t.summary  = tj.GetStr("summary");
            if (const auto* kwArr = tj.GetArr("keywords")) {
                for (const auto& kw : *kwArr) {
                    if (kw.kind == JsonValue::Kind::String) t.keywords.push_back(kw.sVal);
                }
            }
            if (!t.id.empty() && !t.fileName.empty()) {
                std::filesystem::path relFile(rsw::Utf8ToWide(t.fileName));
                const auto fullHtml = m.moduleDir / relFile;
                const std::string htmlBytes = ReadUtf8FileToString(fullHtml);
                t.cachedPlainTextLower = LowerUtf8(t.title + " " + t.category + " " + t.summary) + " " +
                                         StripHtmlTagsToSearchableLower(htmlBytes);
                for (const auto& kw : t.keywords) {
                    t.cachedPlainTextLower += " " + LowerUtf8(kw);
                }
                m.topics.push_back(std::move(t));
            }
        }
    }
    if (m.topics.empty()) return false;
    if (m.defaultTopicId.empty()) m.defaultTopicId = m.topics.front().id;

    if (const auto* idxArr = root.GetArr("indexEntries")) {
        for (const auto& ij : *idxArr) {
            HelpIndexEntry ie{};
            ie.entryId     = ij.GetStr("entryId");
            ie.displayText = ij.GetStr("displayText");
            ie.category    = ij.GetStr("category");
            ie.badge       = ij.GetStr("badge");
            ie.subtitle    = ij.GetStr("subtitle");
            ie.topicId     = ij.GetStr("topicId");
            ie.anchorId    = ij.GetStr("anchorId");
            if (!ie.displayText.empty() && !ie.topicId.empty()) {
                m.indexEntries.push_back(std::move(ie));
            }
        }
    }
    // Auto-populate Index entries from topic keywords if `indexEntries` was omitted
    if (m.indexEntries.empty()) {
        for (const auto& t : m.topics) {
            m.indexEntries.push_back({t.id, t.title, t.category, "Topic", t.summary, t.id, ""});
            for (const auto& kw : t.keywords) {
                m.indexEntries.push_back({t.id + ":" + kw, kw, t.category, "Keyword", t.title, t.id, ""});
            }
        }
    }
    std::sort(m.indexEntries.begin(), m.indexEntries.end(), [](const HelpIndexEntry& a, const HelpIndexEntry& b) {
        return LowerUtf8(a.displayText) < LowerUtf8(b.displayText);
    });

    outMod = std::move(m);
    return true;
}

/// Discovers all installed `.rshm.json` modules across all 5 standard RaggieSoft Help locations!
std::vector<LoadedHelpModule> DiscoverAllHelpModules(
    const std::filesystem::path& exeDir,
    const std::vector<std::filesystem::path>& cliExtraDirs)
{
    std::vector<LoadedHelpModule> found;
    std::set<std::string> seenIds;

    auto tryCandidate = [&](const std::filesystem::path& dir) {
        if (dir.empty()) return;
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec)) return;

        LoadedHelpModule directMod;
        if (TryLoadHelpModuleDir(dir, directMod)) {
            const std::string key = LowerUtf8(directMod.moduleId);
            if (seenIds.insert(key).second) found.push_back(std::move(directMod));
            return;
        }
        for (const auto& sub : std::filesystem::directory_iterator(dir, ec)) {
            if (!sub.is_directory(ec)) continue;
            LoadedHelpModule subMod;
            if (TryLoadHelpModuleDir(sub.path(), subMod)) {
                const std::string key = LowerUtf8(subMod.moduleId);
                if (seenIds.insert(key).second) found.push_back(std::move(subMod));
            }
        }
    };

    // Priority 1: Explicit `--module-dir` paths
    for (const auto& d : cliExtraDirs) tryCandidate(d);

    // Priority 2: Portable / IDE side-by-side HelpModules folders (`bin\<arch>\HelpModules` & repo `help\`)
    tryCandidate(exeDir / L"HelpModules");
    tryCandidate(exeDir / L".." / L"HelpModules");
    tryCandidate(exeDir / L".." / L".." / L"help");

    // Priority 3 & 4: `%CommonProgramFiles%\RaggieSoft\HelpModules` & `%LOCALAPPDATA%\RaggieSoft\HelpModules`
    auto envDir = [](const wchar_t* name) -> std::filesystem::path {
        wchar_t buf[MAX_PATH * 2]{};
        const DWORD n = ::GetEnvironmentVariableW(name, buf, static_cast<DWORD>(std::size(buf)));
        return (n > 0) ? std::filesystem::path(buf) : std::filesystem::path{};
    };
    const auto commonPf = envDir(L"CommonProgramW6432");
    if (!commonPf.empty()) tryCandidate(commonPf / L"RaggieSoft" / L"HelpModules");
    const auto localApp = envDir(L"LOCALAPPDATA");
    if (!localApp.empty()) tryCandidate(localApp / L"RaggieSoft" / L"HelpModules");

    // Priority 5: `HKCU` & `HKLM` `Software\RaggieSoft\HelpModules` string values
    for (HKEY root : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) {
        HKEY hKey = nullptr;
        if (::RegOpenKeyExW(root, L"Software\\RaggieSoft\\HelpModules", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
            for (DWORD idx = 0; ; ++idx) {
                wchar_t valName[256]{};
                wchar_t valData[MAX_PATH * 2]{};
                DWORD cchName = static_cast<DWORD>(std::size(valName));
                DWORD cbData  = sizeof(valData);
                DWORD type    = 0;
                if (::RegEnumValueW(hKey, idx, valName, &cchName, nullptr, &type,
                                    reinterpret_cast<LPBYTE>(valData), &cbData) != ERROR_SUCCESS) {
                    break;
                }
                if (type == REG_SZ && valData[0] != L'\0') {
                    tryCandidate(std::filesystem::path(valData));
                }
            }
            ::RegCloseKey(hKey);
        }
    }

    return found;
}

// ================================================================================
// 3. IN-PROCESS HTML5 -> RTF TYPOGRAPHY CONVERTER FOR `Msftedit.dll` RICHEDIT 4.1
// ================================================================================
// Converts structured HTML5 help articles into crisp, high-contrast RTF with:
//   - Color 1 (`\cf1`): Primary body text (`#F8FAFC` Dark / `#0F172A` Light = >= 15.8:1!)
//   - Color 2 (`\cf2`): `<h1>` / `<h2>` headings (`#7DD3FC` Dark / `#1E40AF` Light)
//   - Color 3 (`\cf3`): `<h3>` & table headers (`#FBBF24` Warm Gold Dark / `#0F172A` Light)
//   - Color 4 (`\cf4`): Inline `<code>` & `<pre>` blocks (`#BAE6FD` Dark / `#1E3A8A` Light)
//   - Color 5 (`\cf5`): Secondary / callout border captions (`#CBD5E1` Dark / `#334155` Light)
struct ConvertedRtfTopic {
    std::string                   rtfBytes;
    std::map<std::string, LONG>   anchorCharOffsets;
};

void AppendRtfEscapedUtf8(std::ostringstream& rtf, std::string_view utf8Text, LONG& plainCharCounter) {
    const std::wstring wide = rsw::Utf8ToWide(utf8Text);
    for (wchar_t wc : wide) {
        if (wc == L'\\')      { rtf << "\\\\"; ++plainCharCounter; }
        else if (wc == L'{')  { rtf << "\\{";  ++plainCharCounter; }
        else if (wc == L'}')  { rtf << "\\}";  ++plainCharCounter; }
        else if (wc == L'\n') { rtf << "\\line "; ++plainCharCounter; }
        else if (wc == L'\r') { continue; }
        else if (wc >= 32 && wc <= 126) {
            rtf << static_cast<char>(wc);
            ++plainCharCounter;
        } else {
            // RTF signed 16-bit Unicode escape (`\uN?`)
            rtf << "\\u" << static_cast<int>(static_cast<short>(wc)) << "?";
            ++plainCharCounter;
        }
    }
}

std::string CollapseHtmlWhitespace(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool prevSpace = false;
    for (char c : s) {
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!prevSpace) { out.push_back(' '); prevSpace = true; }
        } else {
            out.push_back(c);
            prevSpace = false;
        }
    }
    return out;
}

std::string ExtractIdAttrFromTag(std::string_view tagContent) {
    const std::string low = LowerUtf8(tagContent);
    auto p = low.find("id=");
    if (p == std::string::npos) return {};
    p += 3;
    if (p >= tagContent.size()) return {};
    const char quote = tagContent[p];
    if (quote == '"' || quote == '\'') {
        const auto endQ = tagContent.find(quote, p + 1);
        if (endQ != std::string_view::npos) {
            return std::string(tagContent.substr(p + 1, endQ - p - 1));
        }
    }
    return {};
}

ConvertedRtfTopic ConvertHtmlTopicToThemedRtf(std::string_view html, const rsw::ui::RswTheme& theme) {
    ConvertedRtfTopic result{};
    const COLORREF cBody = theme.textPrimary;
    const COLORREF cH1   = theme.isHighContrast ? theme.textPrimary   : (theme.isDarkMode ? RGB(125, 211, 252) : RGB(30, 64, 175));
    const COLORREF cH3   = theme.isHighContrast ? theme.textPrimary   : (theme.isDarkMode ? RGB(251, 191, 36)  : RGB(15, 23, 42));
    const COLORREF cCode = theme.isHighContrast ? theme.textPrimary   : (theme.isDarkMode ? RGB(186, 230, 253) : RGB(30, 58, 138));
    const COLORREF cSub  = theme.textSecondary;
    const COLORREF cCodeBg = theme.isHighContrast ? theme.cardBg      : (theme.isDarkMode ? RGB(11, 19, 43)    : RGB(238, 242, 249));

    auto rtfCol = [](COLORREF c) {
        std::ostringstream o;
        o << "\\red" << static_cast<int>(GetRValue(c))
          << "\\green" << static_cast<int>(GetGValue(c))
          << "\\blue" << static_cast<int>(GetBValue(c)) << ";";
        return o.str();
    };

    std::ostringstream rtf;
    rtf << "{\\rtf1\\ansi\\deff0"
        << "{\\fonttbl{\\f0\\fnil\\fcharset0 Segoe UI;}{\\f1\\fmodern\\fcharset0 Cascadia Mono;}}"
        << "{\\colortbl ;"
        << rtfCol(cBody)   // \cf1
        << rtfCol(cH1)     // \cf2
        << rtfCol(cH3)     // \cf3
        << rtfCol(cCode)   // \cf4
        << rtfCol(cSub)    // \cf5
        << rtfCol(cCodeBg) // \cb6 / \highlight6
        << "}"
        << "\\viewkind4\\uc1\\pard\\li180\\ri180\\sa140\\sl276\\slmult1\\f0\\fs21\\cf1 ";

    // Strip `<head>...</head>` and `<style>...</style>` first so only visible body content renders!
    std::string bodyHtml(html);
    auto stripSection = [&](const char* openTag, const char* closeTag) {
        for (;;) {
            const std::string low = LowerUtf8(bodyHtml);
            const auto a = low.find(openTag);
            if (a == std::string::npos) break;
            const auto b = low.find(closeTag, a);
            if (b == std::string::npos) { bodyHtml.erase(a); break; }
            bodyHtml.erase(a, (b - a) + std::strlen(closeTag));
        }
    };
    stripSection("<head", "</head>");
    stripSection("<style", "</style>");
    stripSection("<script", "</script>");

    bool inPre        = false;
    bool inTableRow   = false;
    bool firstCell    = true;
    bool atBlockStart = true;
    int  orderedIndex = 0;
    bool inOrdered    = false;
    LONG plainChars   = 0;

    std::size_t pos = 0;
    while (pos < bodyHtml.size()) {
        if (bodyHtml[pos] == '<') {
            // HTML comments (`<!-- ... -->`) may contain `<Id>` brackets inside them—always scan to `-->`!
            if (pos + 4 <= bodyHtml.size() && std::string_view(bodyHtml).substr(pos, 4) == "<!--") {
                const auto endCmt = bodyHtml.find("-->", pos + 4);
                pos = (endCmt == std::string::npos) ? bodyHtml.size() : (endCmt + 3);
                continue;
            }
            const auto gt = bodyHtml.find('>', pos + 1);
            if (gt == std::string::npos) break;
            const std::string_view rawTag = std::string_view(bodyHtml).substr(pos + 1, gt - pos - 1);
            pos = gt + 1;

            if (!rawTag.empty() && rawTag.front() == '!') continue; // doctype

            const std::string anchor = ExtractIdAttrFromTag(rawTag);
            if (!anchor.empty()) {
                result.anchorCharOffsets[LowerUtf8(anchor)] = plainChars;
            }

            std::string tagLow = LowerUtf8(rawTag);
            const auto sp = tagLow.find_first_of(" \t\r\n/");
            std::string name = (sp == std::string::npos) ? tagLow : tagLow.substr(0, sp);
            if (!rawTag.empty() && rawTag.front() == '/') {
                name = "/" + LowerUtf8(rawTag.substr(1));
                const auto sp2 = name.find_first_of(" \t\r\n>");
                if (sp2 != std::string::npos) name.resize(sp2);
            }

            if (name == "h1" || name == "/h1" || name == "h2" || name == "/h2" ||
                name == "h3" || name == "/h3" || name == "h4" || name == "/h4" ||
                name == "p"  || name == "/p"  || name == "br" || name == "hr"  ||
                name == "li" || name == "/li" || name == "tr" || name == "/tr" ||
                name == "pre"|| name == "/pre"|| name == "blockquote" || name == "/blockquote") {
                atBlockStart = true;
            }

            if (name == "h1") {
                rtf << "\\par\\pard\\li180\\ri180\\sb180\\sa120\\b\\f0\\fs34\\cf2 ";
                ++plainChars;
            } else if (name == "/h1") {
                rtf << "\\b0\\fs21\\cf1\\par\\pard\\li180\\ri180\\sa140\\sl276\\slmult1 ";
                ++plainChars;
            } else if (name == "h2") {
                rtf << "\\par\\pard\\li180\\ri180\\sb220\\sa100\\b\\f0\\fs27\\cf2 ";
                ++plainChars;
            } else if (name == "/h2") {
                rtf << "\\b0\\fs21\\cf1\\par\\pard\\li180\\ri180\\sa140\\sl276\\slmult1 ";
                ++plainChars;
            } else if (name == "h3" || name == "h4") {
                rtf << "\\par\\pard\\li180\\ri180\\sb160\\sa80\\b\\f0\\fs23\\cf3 ";
                ++plainChars;
            } else if (name == "/h3" || name == "/h4") {
                rtf << "\\b0\\fs21\\cf1\\par\\pard\\li180\\ri180\\sa140\\sl276\\slmult1 ";
                ++plainChars;
            } else if (name == "p") {
                rtf << "\\pard\\li180\\ri180\\sa140\\sl276\\slmult1\\f0\\fs21\\cf1 ";
            } else if (name == "/p") {
                rtf << "\\par ";
                ++plainChars;
            } else if (name == "br") {
                rtf << "\\line ";
                ++plainChars;
            } else if (name == "hr") {
                rtf << "\\par\\pard\\li180\\ri180\\sa120\\cf5 ------------------------------------------------------------------------\\cf1\\par ";
                plainChars += 73;
            } else if (name == "strong" || name == "b") {
                rtf << "\\b ";
            } else if (name == "/strong" || name == "/b") {
                rtf << "\\b0 ";
            } else if (name == "em" || name == "i") {
                rtf << "\\i ";
            } else if (name == "/em" || name == "/i") {
                rtf << "\\i0 ";
            } else if (name == "code") {
                if (!inPre) rtf << "\\f1\\fs19\\cf4 ";
            } else if (name == "/code") {
                if (!inPre) rtf << "\\f0\\fs21\\cf1 ";
            } else if (name == "pre") {
                inPre = true;
                rtf << "\\par\\pard\\li320\\ri180\\sb80\\sa140\\sl240\\slmult1\\f1\\fs19\\cf4 ";
                ++plainChars;
            } else if (name == "/pre") {
                inPre = false;
                rtf << "\\f0\\fs21\\cf1\\par\\pard\\li180\\ri180\\sa140\\sl276\\slmult1 ";
                ++plainChars;
            } else if (name == "blockquote") {
                rtf << "\\par\\pard\\li420\\ri240\\sb80\\sa140\\cf5\\i ";
                ++plainChars;
            } else if (name == "/blockquote") {
                rtf << "\\i0\\cf1\\par\\pard\\li180\\ri180\\sa140\\sl276\\slmult1 ";
                ++plainChars;
            } else if (name == "ul") {
                inOrdered = false;
            } else if (name == "ol") {
                inOrdered = true;
                orderedIndex = 0;
            } else if (name == "/ul" || name == "/ol") {
                inOrdered = false;
                rtf << "\\pard\\li180\\ri180\\sa140\\sl276\\slmult1 ";
            } else if (name == "li") {
                rtf << "\\pard\\fi-240\\li520\\ri180\\sa80\\sl260\\slmult1\\cf3\\b ";
                if (inOrdered) {
                    ++orderedIndex;
                    const std::string num = std::to_string(orderedIndex) + ". ";
                    AppendRtfEscapedUtf8(rtf, num, plainChars);
                } else {
                    AppendRtfEscapedUtf8(rtf, "\xE2\x80\xA2  ", plainChars);
                }
                rtf << "\\b0\\cf1 ";
            } else if (name == "/li") {
                rtf << "\\par ";
                ++plainChars;
            } else if (name == "tr") {
                inTableRow = true;
                firstCell  = true;
                rtf << "\\pard\\li240\\ri180\\sa70\\sl250\\slmult1 ";
            } else if (name == "/tr") {
                if (inTableRow) {
                    rtf << "\\par ";
                    ++plainChars;
                    inTableRow = false;
                }
            } else if (name == "th") {
                if (!firstCell) { AppendRtfEscapedUtf8(rtf, "   |   ", plainChars); }
                firstCell = false;
                rtf << "\\b\\cf3 ";
            } else if (name == "/th") {
                rtf << "\\b0\\cf1 ";
            } else if (name == "td") {
                if (!firstCell) { AppendRtfEscapedUtf8(rtf, "   \xE2\x80\x94   ", plainChars); }
                firstCell = false;
            }
        } else {
            const auto nextLt = bodyHtml.find('<', pos);
            const std::string_view chunk = (nextLt == std::string::npos)
                ? std::string_view(bodyHtml).substr(pos)
                : std::string_view(bodyHtml).substr(pos, nextLt - pos);
            pos = (nextLt == std::string::npos) ? bodyHtml.size() : nextLt;

            std::string decoded = DecodeHtmlEntitiesUtf8(chunk);
            if (!inPre) {
                decoded = CollapseHtmlWhitespace(decoded);
                if (atBlockStart && !decoded.empty() && decoded.front() == ' ') {
                    decoded.erase(0, 1);
                }
                if (decoded.empty()) continue;
            } else {
                // Trim single leading newline right after `<pre>`
                if (!decoded.empty() && decoded.front() == '\r') decoded.erase(0, 1);
                if (!decoded.empty() && decoded.front() == '\n') decoded.erase(0, 1);
            }
            if (!decoded.empty()) {
                AppendRtfEscapedUtf8(rtf, decoded, plainChars);
                atBlockStart = false;
            }
        }
    }

    rtf << "\\par}";
    result.rtfBytes = rtf.str();
    return result;
}

// ================================================================================
// 4. NAMED PIPE SINGLE-INSTANCE `F1` IPC (`\\.\pipe\RaggieSoft.HelpViewer.IPC.<User>`)
// ================================================================================
std::wstring BuildUserNamedPipePath() {
    wchar_t userBuf[256]{};
    DWORD cch = static_cast<DWORD>(std::size(userBuf));
    std::wstring cleanUser = L"Default";
    if (::GetUserNameW(userBuf, &cch) && userBuf[0] != L'\0') {
        cleanUser.clear();
        for (std::size_t i = 0; userBuf[i] != L'\0'; ++i) {
            const wchar_t ch = userBuf[i];
            if (std::iswalnum(ch) || ch == L'_' || ch == L'-') cleanUser.push_back(ch);
            else cleanUser.push_back(L'_');
        }
    }
    return L"\\\\.\\pipe\\RaggieSoft.HelpViewer.IPC." + cleanUser;
}

bool TryForwardRequestToRunningInstance(
    const std::string& moduleId,
    const std::string& topicId,
    const std::string& anchorId)
{
    const std::wstring pipePath = BuildUserNamedPipePath();
    if (!::WaitNamedPipeW(pipePath.c_str(), 80)) {
        return false;
    }
    ::AllowSetForegroundWindow(ASFW_ANY);
    HANDLE hPipe = ::CreateFileW(pipePath.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hPipe == INVALID_HANDLE_VALUE) return false;

    auto esc = [](const std::string& s) {
        std::string o;
        for (char c : s) {
            if (c == '"' || c == '\\') o.push_back('\\');
            o.push_back(c);
        }
        return o;
    };
    const std::string payload =
        "{\"ModuleId\":\"" + esc(moduleId) + "\",\"TopicId\":\"" + esc(topicId) + "\",\"AnchorId\":\"" + esc(anchorId) + "\"}\n";
    DWORD written = 0;
    const BOOL ok = ::WriteFile(hPipe, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
    ::FlushFileBuffers(hPipe);
    ::CloseHandle(hPipe);
    return ok && (written == payload.size());
}

struct HelpHistoryLoc {
    std::size_t moduleIdx = 0;
    std::string topicId;
    std::string anchorId;
};

struct SearchHitItem {
    std::size_t moduleIdx = 0;
    std::string topicId;
    std::string title;
    std::string productAndCategory;
    int         score = 0;
};

struct RtfMemCookie {
    const char* data = nullptr;
    std::size_t size = 0;
    std::size_t pos  = 0;
};

DWORD CALLBACK HelpRichStreamInCb(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG* pcb) {
    auto* c = reinterpret_cast<RtfMemCookie*>(dwCookie);
    if (!c || c->pos >= c->size) { *pcb = 0; return 0; }
    const std::size_t n = std::min<std::size_t>(c->size - c->pos, static_cast<std::size_t>(cb));
    std::memcpy(pbBuff, c->data + c->pos, n);
    c->pos += n;
    *pcb = static_cast<LONG>(n);
    return 0;
}

// ================================================================================
// 5. MAIN WIN32 C++20 HELP VIEWER WINDOW (`RaggieSoftHelpWindow`)
// ================================================================================
class RaggieSoftHelpWindow {
public:
    rsw::ui::RswTheme               theme{};
    HWND                            hWnd          = nullptr;
    HMODULE                         hRichEditDll  = nullptr;
    HFONT                           hFontTitle    = nullptr;
    HFONT                           hFontUi       = nullptr;
    HFONT                           hFontBold     = nullptr;
    HIMAGELIST                      hTreeIcons    = nullptr;

    std::vector<LoadedHelpModule>   modules;
    std::size_t                     activeModIdx  = 0;
    std::string                     activeTopicId;
    LeftNavPane                     activePane    = LeftNavPane::Contents;

    std::vector<HelpHistoryLoc>     history;
    int                             historyCursor = -1;
    bool                            suppressHistoryPush = false;

    std::vector<std::string>        tocNodeTopicIds;
    std::map<std::string, HTREEITEM>topicToTreeHandle;
    std::vector<HelpIndexEntry>     filteredIndexRows;
    std::vector<SearchHitItem>      searchResults;

    // Toolbar & Pane Controls
    HWND hModCombo = nullptr, hBtnBack = nullptr, hBtnFwd = nullptr, hBtnHome = nullptr, hBtnWebDocs = nullptr;
    HWND hTabContents = nullptr, hTabIndex = nullptr, hTabSearch = nullptr;
    HWND hTocTree = nullptr;
    HWND hIdxFilter = nullptr, hIdxList = nullptr;
    HWND hSrchQuery = nullptr, hSrchBtn = nullptr, hSrchAllChk = nullptr, hSrchList = nullptr;
    HWND hReaderRich = nullptr;

    // Single-instance Named Pipe IPC state
    std::atomic<bool>               stopIpc{false};
    std::thread                     ipcThread;
    std::mutex                      ipcMutex;
    std::vector<std::string>        ipcInbox;

    ~RaggieSoftHelpWindow() {
        StopIpcServer();
        for (HFONT* f : {&hFontTitle, &hFontUi, &hFontBold}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
        if (hTreeIcons) { ::ImageList_Destroy(hTreeIcons); hTreeIcons = nullptr; }
        if (hRichEditDll) { ::FreeLibrary(hRichEditDll); hRichEditDll = nullptr; }
    }

    void StartIpcServer() {
        stopIpc.store(false);
        ipcThread = std::thread([this]() {
            const std::wstring pipeName = BuildUserNamedPipePath();
            while (!stopIpc.load()) {
                HANDLE hPipe = ::CreateNamedPipeW(
                    pipeName.c_str(),
                    PIPE_ACCESS_INBOUND,
                    PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                    4, 0, 4096, 100, nullptr);
                if (hPipe == INVALID_HANDLE_VALUE) break;

                const BOOL connected = ::ConnectNamedPipe(hPipe, nullptr)
                    ? TRUE
                    : (::GetLastError() == ERROR_PIPE_CONNECTED);
                if (connected && !stopIpc.load()) {
                    std::string msg;
                    char buf[1024];
                    DWORD n = 0;
                    while (::ReadFile(hPipe, buf, sizeof(buf), &n, nullptr) && n > 0) {
                        msg.append(buf, n);
                        if (msg.find('\n') != std::string::npos) break;
                    }
                    if (!msg.empty() && hWnd && ::IsWindow(hWnd)) {
                        {
                            std::lock_guard<std::mutex> lk(ipcMutex);
                            ipcInbox.push_back(std::move(msg));
                        }
                        ::PostMessageW(hWnd, WM_RSW_HELP_IPC_NAV, 0, 0);
                    }
                }
                ::DisconnectNamedPipe(hPipe);
                ::CloseHandle(hPipe);
            }
        });
    }

    void StopIpcServer() {
        if (!ipcThread.joinable()) return;
        stopIpc.store(true);
        // Unblock `ConnectNamedPipe` with a quick dummy local connection
        const std::wstring pipeName = BuildUserNamedPipePath();
        HANDLE hWake = ::CreateFileW(pipeName.c_str(), GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (hWake != INVALID_HANDLE_VALUE) ::CloseHandle(hWake);
        ipcThread.join();
    }

    void CreateControls() {
        HINSTANCE hInst = ::GetModuleHandleW(nullptr);
        hRichEditDll = ::LoadLibraryW(L"Msftedit.dll");

        auto mkBtn = [&](const wchar_t* txt, int id, DWORD style = BS_PUSHBUTTON) {
            HWND h = ::CreateWindowExW(
                0, L"BUTTON", txt, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style,
                0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), hInst, nullptr);
            const DWORD lowType = style & 0x0Fu;
            if (lowType == BS_PUSHBUTTON || lowType == BS_DEFPUSHBUTTON) {
                rsw::ui::AttachAccessiblePushButtonSubclass(h, &theme, id == IDC_HLP_SRCH_BTN, false);
            }
            return h;
        };

        hModCombo = ::CreateWindowExW(
            0, L"COMBOBOX", nullptr, WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL,
            0, 0, 260, 220, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HLP_MOD_COMBO)), hInst, nullptr);
        for (const auto& m : modules) {
            const std::wstring label = rsw::Utf8ToWide(m.productName + " (v" + m.productVersion + ")");
            ::SendMessageW(hModCombo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        ::SendMessageW(hModCombo, CB_SETCURSEL, static_cast<WPARAM>(activeModIdx), 0);
        rsw::ui::ApplyDarkComboBoxTheme(hModCombo, theme);

        hBtnBack    = mkBtn(L"< &Back",          IDC_HLP_BACK_BTN);
        hBtnFwd     = mkBtn(L"&Forward >",       IDC_HLP_FWD_BTN);
        hBtnHome    = mkBtn(L"&Home",            IDC_HLP_HOME_BTN);
        hBtnWebDocs = mkBtn(L"Open &Web Docs",   IDC_HLP_WEBDOCS_BTN);

        hTabContents = mkBtn(L"&Contents", IDC_HLP_TAB_CONTENTS);
        hTabIndex    = mkBtn(L"&Index",    IDC_HLP_TAB_INDEX);
        hTabSearch   = mkBtn(L"&Search",   IDC_HLP_TAB_SEARCH);

        // 1. Contents Tree (`SysTreeView32` with 32-bit ARGB vector icons!)
        hTocTree = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_TREEVIEWW, L"Help Table of Contents",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | TVS_HASBUTTONS | TVS_HASLINES | TVS_LINESATROOT | TVS_SHOWSELALWAYS | TVS_SHAREIMAGELISTS,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HLP_TOC_TREE)), hInst, nullptr);

        // 2. Index Filter & Dark-Header `SysListView32`
        hIdxFilter = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HLP_IDX_FILTER)), hInst, nullptr);
        ::SendMessageW(hIdxFilter, 0x1501 /*EM_SETCUEBANNER*/, TRUE, reinterpret_cast<LPARAM>(L"Filter index keywords..."));

        hIdxList = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"Help Keyword Index",
            WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HLP_IDX_LIST)), hInst, nullptr);
        ListView_SetExtendedListViewStyle(hIdxList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        // 3. Full-Text Search Controls (`AttachAccessibleCheckboxSubclass` + `AttachDarkAwareListViewSubclass`!)
        hSrchQuery = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, L"EDIT", L"",
            WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HLP_SRCH_QUERY)), hInst, nullptr);
        ::SendMessageW(hSrchQuery, 0x1501 /*EM_SETCUEBANNER*/, TRUE, reinterpret_cast<LPARAM>(L"Search offline topics..."));
        hSrchBtn    = mkBtn(L"&Find", IDC_HLP_SRCH_BTN, BS_DEFPUSHBUTTON);
        hSrchAllChk = mkBtn(L"Search &all installed modules", IDC_HLP_SRCH_ALL_CHK, BS_AUTOCHECKBOX);
        ::SendMessageW(hSrchAllChk, BM_SETCHECK, BST_CHECKED, 0);

        hSrchList = ::CreateWindowExW(
            WS_EX_CLIENTEDGE, WC_LISTVIEWW, L"Help Search Results",
            WS_CHILD | WS_TABSTOP | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HLP_SRCH_LIST)), hInst, nullptr);
        ListView_SetExtendedListViewStyle(hSrchList, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);

        // 4. Right-Hand High-Contrast RichEdit 4.1 Topic Reader (`Msftedit.dll`)
        hReaderRich = ::CreateWindowExW(
            WS_EX_CLIENTEDGE,
            hRichEditDll ? MSFTEDIT_CLASS : L"EDIT",
            L"",
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
            0, 0, 10, 10, hWnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_HLP_READER_RICH)), hInst, nullptr);

        // Wire Dark Mode Checkbox & ListView Header Subclasses so there is ZERO black-on-dark text!
        rsw::ui::AttachAccessibleCheckboxSubclass(hSrchAllChk, &theme, false);
        rsw::ui::AttachDarkAwareListViewSubclass(hIdxList,     &theme);
        rsw::ui::AttachDarkAwareListViewSubclass(hSrchList,    &theme);
        for (HWND sc : {hTocTree, hReaderRich}) {
            rsw::ui::ApplyDarkScrollbarTheme(sc, theme);
        }

        InitListColumns();
    }

    void InitListColumns() {
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };
        auto addCol = [&](HWND lv, int idx, const wchar_t* label, int w) {
            LVCOLUMNW col{};
            col.mask     = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            col.pszText  = const_cast<LPWSTR>(label);
            col.cx       = S(w);
            col.iSubItem = idx;
            ListView_InsertColumn(lv, idx, &col);
        };
        addCol(hIdxList,  0, L"Keyword / Entry", 195);
        addCol(hIdxList,  1, L"Category",        115);

        addCol(hSrchList, 0, L"Matching Topic",  195);
        addCol(hSrchList, 1, L"Module / Section",115);
    }

    void LayoutChildren() {
        const UINT dpi = ::GetDpiForWindow(hWnd);
        for (HFONT* f : {&hFontTitle, &hFontUi, &hFontBold}) {
            if (*f) { ::DeleteObject(*f); *f = nullptr; }
        }
        hFontTitle = rsw::ui::CreateScaledFont(L"Segoe UI", 13, FW_BOLD,     dpi, theme.textScalePercent);
        hFontUi    = rsw::ui::CreateScaledFont(L"Segoe UI",  9, FW_NORMAL,   dpi, theme.textScalePercent);
        hFontBold  = rsw::ui::CreateScaledFont(L"Segoe UI",  9, FW_SEMIBOLD, dpi, theme.textScalePercent);

        for (HWND h : {hModCombo, hBtnBack, hBtnFwd, hBtnHome, hBtnWebDocs, hTocTree, hIdxFilter, hIdxList, hSrchQuery, hSrchBtn, hSrchList}) {
            if (h) ::SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(hFontUi), TRUE);
        }
        for (HWND hb : {hTabContents, hTabIndex, hTabSearch}) {
            if (hb) ::SendMessageW(hb, WM_SETFONT, reinterpret_cast<WPARAM>(hFontBold), TRUE);
        }

        HIMAGELIST newIcons = rsw::ui::CreateStudioTreeImageList(dpi, theme);
        TreeView_SetImageList(hTocTree, newIcons, TVSIL_NORMAL);
        if (hTreeIcons) ::ImageList_Destroy(hTreeIcons);
        hTreeIcons = newIcons;
        rsw::ui::ApplyDarkTreeViewTheme(hTocTree, theme, dpi);
        if (!theme.isHighContrast) {
            for (HWND lv : {hIdxList, hSrchList}) {
                ListView_SetBkColor(lv, theme.cardBg);
                ListView_SetTextBkColor(lv, theme.cardBg);
                ListView_SetTextColor(lv, theme.textPrimary);
            }
        }

        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(640, rc.right - rc.left);
        const int H = std::max<int>(440, rc.bottom - rc.top);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        // Toolbar row (`y = 62`)
        const int tbY = S(62);
        ::MoveWindow(hModCombo,   S(14),  tbY + S(2), S(300), S(220), TRUE);
        ::MoveWindow(hBtnBack,    S(324), tbY,        S(82),  S(30),  TRUE);
        ::MoveWindow(hBtnFwd,     S(412), tbY,        S(92),  S(30),  TRUE);
        ::MoveWindow(hBtnHome,    S(510), tbY,        S(76),  S(30),  TRUE);
        ::MoveWindow(hBtnWebDocs, W - S(156), tbY,    S(142), S(30),  TRUE);

        // Left pane (`w = 336`) vs Right reader pane
        const int paneTop = S(102);
        const int paneH   = std::max(S(240), H - paneTop - S(14));
        const int leftW   = S(336);
        const int tabW    = (leftW - S(8)) / 3;

        ::MoveWindow(hTabContents, S(14),                      paneTop, tabW, S(28), TRUE);
        ::MoveWindow(hTabIndex,    S(14) + tabW + S(4),        paneTop, tabW, S(28), TRUE);
        ::MoveWindow(hTabSearch,   S(14) + (tabW + S(4)) * 2,  paneTop, leftW - (tabW + S(4)) * 2, S(28), TRUE);

        const int navBodyY = paneTop + S(34);
        const int navBodyH = paneH - S(34);
        ::MoveWindow(hTocTree, S(14), navBodyY, leftW, navBodyH, TRUE);

        ::MoveWindow(hIdxFilter, S(14), navBodyY,         leftW, S(26), TRUE);
        ::MoveWindow(hIdxList,   S(14), navBodyY + S(32), leftW, std::max(S(80), navBodyH - S(32)), TRUE);

        ::MoveWindow(hSrchQuery,  S(14), navBodyY,         leftW - S(68), S(26), TRUE);
        ::MoveWindow(hSrchBtn,    S(14) + leftW - S(62), navBodyY - S(1), S(62), S(28), TRUE);
        ::MoveWindow(hSrchAllChk, S(14), navBodyY + S(32), leftW, S(26), TRUE);
        ::MoveWindow(hSrchList,   S(14), navBodyY + S(64), leftW, std::max(S(80), navBodyH - S(64)), TRUE);

        const int readerX = S(26) + leftW;
        ::MoveWindow(hReaderRich, readerX, paneTop, std::max(S(260), W - readerX - S(14)), paneH, TRUE);

        UpdateLeftPaneVisibility();
        ::InvalidateRect(hWnd, nullptr, TRUE);
    }

    void UpdateLeftPaneVisibility() {
        const bool isToc  = (activePane == LeftNavPane::Contents);
        const bool isIdx  = (activePane == LeftNavPane::Index);
        const bool isSrch = (activePane == LeftNavPane::Search);

        ::ShowWindow(hTocTree,    isToc  ? SW_SHOW : SW_HIDE);
        ::ShowWindow(hIdxFilter,  isIdx  ? SW_SHOW : SW_HIDE);
        ::ShowWindow(hIdxList,    isIdx  ? SW_SHOW : SW_HIDE);
        ::ShowWindow(hSrchQuery,  isSrch ? SW_SHOW : SW_HIDE);
        ::ShowWindow(hSrchBtn,    isSrch ? SW_SHOW : SW_HIDE);
        ::ShowWindow(hSrchAllChk, isSrch ? SW_SHOW : SW_HIDE);
        ::ShowWindow(hSrchList,   isSrch ? SW_SHOW : SW_HIDE);

        ::SetWindowTextW(hTabContents, isToc  ? L"[ &Contents ]" : L"&Contents");
        ::SetWindowTextW(hTabIndex,    isIdx  ? L"[ &Index ]"    : L"&Index");
        ::SetWindowTextW(hTabSearch,   isSrch ? L"[ &Search ]"   : L"&Search");
    }

    void PopulateActiveModuleTrees() {
        TreeView_DeleteAllItems(hTocTree);
        tocNodeTopicIds.clear();
        topicToTreeHandle.clear();
        if (activeModIdx >= modules.size()) return;

        const auto& mod = modules[activeModIdx];
        std::map<std::string, HTREEITEM> catHandles;
        std::vector<std::string> catOrder;

        for (const auto& t : mod.topics) {
            const std::string catKey = LowerUtf8(t.category);
            if (!catHandles.count(catKey)) {
                std::wstring wCat = rsw::Utf8ToWide(t.category);
                const std::size_t metaIdx = tocNodeTopicIds.size();
                tocNodeTopicIds.emplace_back(""); // Category header node
                TVINSERTSTRUCTW cIns{};
                cIns.hParent      = TVI_ROOT;
                cIns.hInsertAfter = TVI_LAST;
                cIns.item.mask    = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM | TVIF_STATE;
                cIns.item.pszText = wCat.data();
                cIns.item.iImage  = static_cast<int>(rsw::ui::StudioTreeIcon::CategoryFolder);
                cIns.item.iSelectedImage = static_cast<int>(rsw::ui::StudioTreeIcon::CategoryFolder);
                cIns.item.lParam  = static_cast<LPARAM>(metaIdx);
                cIns.item.stateMask = TVIS_BOLD | TVIS_EXPANDED;
                cIns.item.state   = TVIS_BOLD | TVIS_EXPANDED;
                catHandles[catKey] = TreeView_InsertItem(hTocTree, &cIns);
                catOrder.push_back(catKey);
            }

            std::wstring wTitle = rsw::Utf8ToWide(t.title);
            const std::size_t metaIdx = tocNodeTopicIds.size();
            tocNodeTopicIds.push_back(t.id);
            TVINSERTSTRUCTW tIns{};
            tIns.hParent      = catHandles[catKey];
            tIns.hInsertAfter = TVI_LAST;
            tIns.item.mask    = TVIF_TEXT | TVIF_IMAGE | TVIF_SELECTEDIMAGE | TVIF_PARAM;
            tIns.item.pszText = wTitle.data();
            tIns.item.iImage  = static_cast<int>(rsw::ui::StudioTreeIcon::FileLeaf);
            tIns.item.iSelectedImage = static_cast<int>(rsw::ui::StudioTreeIcon::FileLeaf);
            tIns.item.lParam  = static_cast<LPARAM>(metaIdx);
            HTREEITEM hItem = TreeView_InsertItem(hTocTree, &tIns);
            topicToTreeHandle[LowerUtf8(t.id)] = hItem;
        }
        for (const auto& ck : catOrder) {
            TreeView_Expand(hTocTree, catHandles[ck], TVE_EXPAND);
        }

        RefreshIndexFilter();
    }

    void RefreshIndexFilter() {
        ListView_DeleteAllItems(hIdxList);
        filteredIndexRows.clear();
        if (activeModIdx >= modules.size()) return;

        wchar_t qBuf[256]{};
        ::GetWindowTextW(hIdxFilter, qBuf, static_cast<int>(std::size(qBuf)));
        const std::string needle = LowerUtf8(rsw::WideToUtf8(qBuf));

        int row = 0;
        for (const auto& ie : modules[activeModIdx].indexEntries) {
            if (!needle.empty()) {
                const std::string hay = LowerUtf8(ie.displayText + " " + ie.category + " " + ie.subtitle);
                if (hay.find(needle) == std::string::npos) continue;
            }
            filteredIndexRows.push_back(ie);
            std::wstring w0 = rsw::Utf8ToWide(ie.displayText);
            std::wstring w1 = rsw::Utf8ToWide(ie.category);
            LVITEMW it{LVIF_TEXT, row, 0, 0, 0, w0.data(), 0, 0, 0, 0};
            ListView_InsertItem(hIdxList, &it);
            ListView_SetItemText(hIdxList, row, 1, w1.data());
            ++row;
        }
    }

    void ExecuteFullTextSearch() {
        ListView_DeleteAllItems(hSrchList);
        searchResults.clear();
        if (modules.empty()) return;

        wchar_t qBuf[256]{};
        ::GetWindowTextW(hSrchQuery, qBuf, static_cast<int>(std::size(qBuf)));
        const std::string query = LowerUtf8(rsw::WideToUtf8(qBuf));

        std::vector<std::string> terms;
        {
            std::istringstream iss(query);
            std::string tok;
            while (iss >> tok) terms.push_back(tok);
        }
        const bool allMods = (::SendMessageW(hSrchAllChk, BM_GETCHECK, 0, 0) == BST_CHECKED);

        for (std::size_t mIdx = 0; mIdx < modules.size(); ++mIdx) {
            if (!allMods && mIdx != activeModIdx) continue;
            const auto& mod = modules[mIdx];
            for (const auto& t : mod.topics) {
                int score = 0;
                if (terms.empty()) {
                    score = 1;
                } else {
                    bool matchedAll = true;
                    const std::string titleLow = LowerUtf8(t.title);
                    for (const auto& term : terms) {
                        if (t.cachedPlainTextLower.find(term) == std::string::npos) {
                            matchedAll = false;
                            break;
                        }
                        score += (titleLow.find(term) != std::string::npos) ? 20 : 5;
                    }
                    if (!matchedAll) continue;
                }
                searchResults.push_back({mIdx, t.id, t.title, mod.productName + " • " + t.category, score});
            }
        }

        std::sort(searchResults.begin(), searchResults.end(), [](const SearchHitItem& a, const SearchHitItem& b) {
            if (a.score != b.score) return a.score > b.score;
            return a.title < b.title;
        });

        for (int r = 0; r < static_cast<int>(searchResults.size()); ++r) {
            std::wstring w0 = rsw::Utf8ToWide(searchResults[r].title);
            std::wstring w1 = rsw::Utf8ToWide(searchResults[r].productAndCategory);
            LVITEMW it{LVIF_TEXT, r, 0, 0, 0, w0.data(), 0, 0, 0, 0};
            ListView_InsertItem(hSrchList, &it);
            ListView_SetItemText(hSrchList, r, 1, w1.data());
        }
    }

    void NavigateToTopic(std::size_t modIdx, const std::string& topicId, const std::string& anchorId = "", bool recordHistory = true) {
        if (modules.empty()) return;
        modIdx = std::min(modIdx, modules.size() - 1);
        if (modIdx != activeModIdx) {
            activeModIdx = modIdx;
            ::SendMessageW(hModCombo, CB_SETCURSEL, static_cast<WPARAM>(activeModIdx), 0);
            PopulateActiveModuleTrees();
        }

        const auto& mod = modules[activeModIdx];
        const HelpTopicEntry* target = nullptr;
        for (const auto& t : mod.topics) {
            if (rsw::EqualsIgnoreCase(t.id, topicId)) { target = &t; break; }
        }
        if (!target) {
            for (const auto& t : mod.topics) {
                if (rsw::EqualsIgnoreCase(t.id, mod.defaultTopicId)) { target = &t; break; }
            }
        }
        if (!target && !mod.topics.empty()) target = &mod.topics.front();
        if (!target) return;

        activeTopicId = target->id;
        const std::filesystem::path htmlPath = mod.moduleDir / std::filesystem::path(rsw::Utf8ToWide(target->fileName));
        const std::string html = ReadUtf8FileToString(htmlPath);
        const auto rtfDoc = ConvertHtmlTopicToThemedRtf(html, theme);

        if (hRichEditDll) {
            ::SendMessageW(hReaderRich, EM_SETBKGNDCOLOR, 0, static_cast<LPARAM>(theme.cardBg));
            RtfMemCookie ck{rtfDoc.rtfBytes.data(), rtfDoc.rtfBytes.size(), 0};
            EDITSTREAM es{};
            es.dwCookie    = reinterpret_cast<DWORD_PTR>(&ck);
            es.pfnCallback = &HelpRichStreamInCb;
            ::SendMessageW(hReaderRich, EM_STREAMIN, SF_RTF, reinterpret_cast<LPARAM>(&es));

            if (!anchorId.empty()) {
                auto aIt = rtfDoc.anchorCharOffsets.find(LowerUtf8(anchorId));
                if (aIt != rtfDoc.anchorCharOffsets.end()) {
                    CHARRANGE cr{aIt->second, aIt->second};
                    ::SendMessageW(hReaderRich, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&cr));
                    ::SendMessageW(hReaderRich, EM_SCROLLCARET, 0, 0);
                }
            } else {
                CHARRANGE top{0, 0};
                ::SendMessageW(hReaderRich, EM_EXSETSEL, 0, reinterpret_cast<LPARAM>(&top));
                ::SendMessageW(hReaderRich, WM_VSCROLL, SB_TOP, 0);
            }
        }

        // Sync TOC selection without recursive history loop
        suppressHistoryPush = true;
        auto tvIt = topicToTreeHandle.find(LowerUtf8(target->id));
        if (tvIt != topicToTreeHandle.end()) {
            TreeView_SelectItem(hTocTree, tvIt->second);
        }
        suppressHistoryPush = false;

        if (recordHistory) {
            if (historyCursor + 1 < static_cast<int>(history.size())) {
                history.erase(history.begin() + historyCursor + 1, history.end());
            }
            history.push_back({activeModIdx, activeTopicId, anchorId});
            historyCursor = static_cast<int>(history.size()) - 1;
        }
        ::EnableWindow(hBtnBack, (historyCursor > 0) ? TRUE : FALSE);
        ::EnableWindow(hBtnFwd,  (historyCursor + 1 < static_cast<int>(history.size())) ? TRUE : FALSE);
        ::EnableWindow(hBtnWebDocs, !mod.onlineDocsUrl.empty() ? TRUE : FALSE);

        const std::wstring winCaption = L"RaggieSoft Help Viewer — " + rsw::Utf8ToWide(mod.productName + " • " + target->title);
        ::SetWindowTextW(hWnd, winCaption.c_str());
        ::InvalidateRect(hWnd, nullptr, FALSE);
    }

    void NavigateByIdStrings(const std::string& moduleId, const std::string& topicId, const std::string& anchorId) {
        std::size_t idx = activeModIdx;
        if (!moduleId.empty()) {
            for (std::size_t i = 0; i < modules.size(); ++i) {
                if (rsw::EqualsIgnoreCase(modules[i].moduleId, moduleId)) { idx = i; break; }
            }
        }
        NavigateToTopic(idx, topicId, anchorId, true);
    }

    void PaintBanner(HDC hdcOut) {
        RECT rc{};
        ::GetClientRect(hWnd, &rc);
        const int W = std::max<int>(1, rc.right - rc.left);
        const int H = std::max<int>(1, rc.bottom - rc.top);
        const UINT dpi = ::GetDpiForWindow(hWnd);
        auto S = [&](int px) { return rsw::ui::ScalePx(px, dpi, theme.textScalePercent); };

        HDC hdc = ::CreateCompatibleDC(hdcOut);
        HBITMAP bmp = ::CreateCompatibleBitmap(hdcOut, W, H);
        HGDIOBJ oldB = ::SelectObject(hdc, bmp);

        ::FillRect(hdc, &rc, theme.hbrWindow);
        RECT bannerRc{0, 0, W, S(52)};
        rsw::ui::FillHorizontalGradient(hdc, bannerRc, theme.bannerNavyLeft, theme.bannerNavyRight);

        {
            Gdiplus::Graphics g(hdc);
            g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
            rsw::ui::DrawBrandSquircleBadge(g, static_cast<float>(S(14)), static_cast<float>(S(11)), static_cast<float>(S(30)), theme);
            rsw::ui::DrawMultiStopAccentBar(g, 0.0f, static_cast<float>(S(52)), static_cast<float>(W), static_cast<float>(S(3)), theme);
        }

        ::SetBkMode(hdc, TRANSPARENT);
        HGDIOBJ oldF = ::SelectObject(hdc, hFontTitle);
        ::SetTextColor(hdc, theme.bannerTitleText);
        std::wstring bannerTitle = L"RaggieSoft Help Viewer  \x2022  Zero-Dependency C++20 Offline Documentation";
        if (activeModIdx < modules.size()) {
            bannerTitle = L"RaggieSoft Help Viewer  \x2022  " + rsw::Utf8ToWide(modules[activeModIdx].productName) +
                          L" (v" + rsw::Utf8ToWide(modules[activeModIdx].productVersion) + L")";
        }
        RECT tRc{S(52), S(8), W - S(16), S(46)};
        ::DrawTextW(hdc, bannerTitle.c_str(), -1, &tRc, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        ::SelectObject(hdc, oldF);

        ::BitBlt(hdcOut, 0, 0, W, H, hdc, 0, 0, SRCCOPY);
        ::SelectObject(hdc, oldB);
        ::DeleteObject(bmp);
        ::DeleteDC(hdc);
    }

    static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
        RaggieSoftHelpWindow* self = nullptr;
        if (msg == WM_NCCREATE) {
            auto* cs = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<RaggieSoftHelpWindow*>(cs->lpCreateParams);
            ::SetWindowLongPtrW(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
            self->hWnd = hWnd;
        } else {
            self = reinterpret_cast<RaggieSoftHelpWindow*>(::GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        }
        if (!self) return ::DefWindowProcW(hWnd, msg, wParam, lParam);

        switch (msg) {
            case WM_CREATE:
                self->CreateControls();
                rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                self->PopulateActiveModuleTrees();
                self->LayoutChildren();
                return 0;

            case WM_SIZE:
                self->LayoutChildren();
                return 0;

            case WM_DPICHANGED: {
                const auto* r = reinterpret_cast<const RECT*>(lParam);
                if (r) ::SetWindowPos(hWnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
                self->LayoutChildren();
                return 0;
            }

            case WM_SETTINGCHANGE:
            case WM_SYSCOLORCHANGE:
            case WM_THEMECHANGED: {
                if (rsw::ui::IsLiveThemeBroadcast(msg, wParam, lParam)) {
                    self->theme.Refresh(RGB(37, 99, 235), RGB(251, 191, 36));
                    rsw::ui::ApplyWindowChromeAndA11y(hWnd, self->theme);
                    rsw::ui::ApplyDarkComboBoxTheme(self->hModCombo, self->theme);
                    for (HWND sc : {self->hTocTree, self->hIdxList, self->hSrchList, self->hReaderRich}) {
                        rsw::ui::ApplyDarkScrollbarTheme(sc, self->theme);
                    }
                    self->NavigateToTopic(self->activeModIdx, self->activeTopicId, "", false);
                    self->LayoutChildren();
                    ::RedrawWindow(hWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_ALLCHILDREN | RDW_UPDATENOW);
                    return 0;
                }
                break;
            }

            case WM_ERASEBKGND:
                return 1;

            case WM_PAINT: {
                PAINTSTRUCT ps{};
                HDC hdc = ::BeginPaint(hWnd, &ps);
                self->PaintBanner(hdc);
                ::EndPaint(hWnd, &ps);
                return 0;
            }

            case WM_CTLCOLOREDIT:
            case WM_CTLCOLORSTATIC: {
                HDC dc = reinterpret_cast<HDC>(wParam);
                ::SetTextColor(dc, self->theme.textPrimary);
                ::SetBkColor(dc, self->theme.cardBg);
                return reinterpret_cast<LRESULT>(self->theme.hbrCard);
            }

            case WM_RSW_HELP_IPC_NAV: {
                std::vector<std::string> batch;
                {
                    std::lock_guard<std::mutex> lk(self->ipcMutex);
                    batch.swap(self->ipcInbox);
                }
                for (const auto& raw : batch) {
                    JsonValue jv;
                    MiniJsonParser p(raw);
                    if (p.ParseRoot(jv)) {
                        self->NavigateByIdStrings(jv.GetStr("ModuleId"), jv.GetStr("TopicId"), jv.GetStr("AnchorId"));
                    }
                }
                if (::IsIconic(hWnd)) ::ShowWindow(hWnd, SW_RESTORE);
                ::SetForegroundWindow(hWnd);
                return 0;
            }

            case WM_NOTIFY: {
                const auto* nm = reinterpret_cast<const NMHDR*>(lParam);
                if (!nm) break;
                if (nm->idFrom == IDC_HLP_TOC_TREE && nm->code == TVN_SELCHANGEDW && !self->suppressHistoryPush) {
                    const auto* nmtv = reinterpret_cast<const NMTREEVIEWW*>(lParam);
                    const std::size_t idx = static_cast<std::size_t>(nmtv->itemNew.lParam);
                    if (idx < self->tocNodeTopicIds.size() && !self->tocNodeTopicIds[idx].empty()) {
                        self->NavigateToTopic(self->activeModIdx, self->tocNodeTopicIds[idx], "", true);
                    }
                    return 0;
                }
                if (nm->idFrom == IDC_HLP_IDX_LIST && (nm->code == LVN_ITEMCHANGED || nm->code == NM_DBLCLK)) {
                    const int sel = ListView_GetNextItem(self->hIdxList, -1, LVNI_SELECTED);
                    if (sel >= 0 && static_cast<std::size_t>(sel) < self->filteredIndexRows.size()) {
                        const auto& ie = self->filteredIndexRows[static_cast<std::size_t>(sel)];
                        self->NavigateToTopic(self->activeModIdx, ie.topicId, ie.anchorId, true);
                    }
                    return 0;
                }
                if (nm->idFrom == IDC_HLP_SRCH_LIST && (nm->code == LVN_ITEMCHANGED || nm->code == NM_DBLCLK)) {
                    const int sel = ListView_GetNextItem(self->hSrchList, -1, LVNI_SELECTED);
                    if (sel >= 0 && static_cast<std::size_t>(sel) < self->searchResults.size()) {
                        const auto& hit = self->searchResults[static_cast<std::size_t>(sel)];
                        self->NavigateToTopic(hit.moduleIdx, hit.topicId, "", true);
                    }
                    return 0;
                }
                break;
            }

            case WM_COMMAND: {
                const int id   = LOWORD(wParam);
                const int code = HIWORD(wParam);
                if (id == IDC_HLP_MOD_COMBO && code == CBN_SELCHANGE) {
                    const LRESULT sel = ::SendMessageW(self->hModCombo, CB_GETCURSEL, 0, 0);
                    if (sel != CB_ERR && static_cast<std::size_t>(sel) < self->modules.size()) {
                        self->NavigateToTopic(static_cast<std::size_t>(sel), "", "", true);
                    }
                    return 0;
                }
                if (id == IDC_HLP_IDX_FILTER && code == EN_CHANGE) {
                    self->RefreshIndexFilter();
                    return 0;
                }
                switch (id) {
                    case IDC_HLP_TAB_CONTENTS:
                        self->activePane = LeftNavPane::Contents;
                        self->UpdateLeftPaneVisibility();
                        return 0;
                    case IDC_HLP_TAB_INDEX:
                        self->activePane = LeftNavPane::Index;
                        self->UpdateLeftPaneVisibility();
                        self->RefreshIndexFilter();
                        return 0;
                    case IDC_HLP_TAB_SEARCH:
                        self->activePane = LeftNavPane::Search;
                        self->UpdateLeftPaneVisibility();
                        if (self->searchResults.empty()) self->ExecuteFullTextSearch();
                        return 0;
                    case IDC_HLP_SRCH_BTN:
                    case IDC_HLP_SRCH_ALL_CHK:
                        self->ExecuteFullTextSearch();
                        return 0;
                    case IDC_HLP_BACK_BTN:
                        if (self->historyCursor > 0) {
                            --self->historyCursor;
                            const auto loc = self->history[static_cast<std::size_t>(self->historyCursor)];
                            self->NavigateToTopic(loc.moduleIdx, loc.topicId, loc.anchorId, false);
                        }
                        return 0;
                    case IDC_HLP_FWD_BTN:
                        if (self->historyCursor + 1 < static_cast<int>(self->history.size())) {
                            ++self->historyCursor;
                            const auto loc = self->history[static_cast<std::size_t>(self->historyCursor)];
                            self->NavigateToTopic(loc.moduleIdx, loc.topicId, loc.anchorId, false);
                        }
                        return 0;
                    case IDC_HLP_HOME_BTN:
                        if (self->activeModIdx < self->modules.size()) {
                            self->NavigateToTopic(self->activeModIdx, self->modules[self->activeModIdx].defaultTopicId, "", true);
                        }
                        return 0;
                    case IDC_HLP_WEBDOCS_BTN:
                        if (self->activeModIdx < self->modules.size() && !self->modules[self->activeModIdx].onlineDocsUrl.empty()) {
                            const std::wstring url = rsw::Utf8ToWide(self->modules[self->activeModIdx].onlineDocsUrl);
                            ::ShellExecuteW(hWnd, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                        }
                        return 0;
                }
                break;
            }

            case WM_DESTROY:
                self->StopIpcServer();
                ::PostQuitMessage(0);
                return 0;
        }
        return ::DefWindowProcW(hWnd, msg, wParam, lParam);
    }
};

} // namespace

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow) {
    int argc = 0;
    LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
    std::vector<std::wstring> args;
    if (argv) {
        for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
        ::LocalFree(argv);
    }

    std::string reqModuleId = "raggiesoft.installer-studio";
    std::string reqTopicId;
    std::string reqAnchorId;
    std::vector<std::filesystem::path> extraModuleDirs;
    std::filesystem::path exportShotDir;
    bool headlessSmoke = false;

    for (std::size_t i = 0; i < args.size(); ++i) {
        const auto& a = args[i];
        if (a == L"--headless-smoke-test") {
            headlessSmoke = true;
        } else if (a == L"--module" && i + 1 < args.size()) {
            reqModuleId = rsw::WideToUtf8(args[++i]);
        } else if (a == L"--topic" && i + 1 < args.size()) {
            reqTopicId = rsw::WideToUtf8(args[++i]);
        } else if (a == L"--anchor" && i + 1 < args.size()) {
            reqAnchorId = rsw::WideToUtf8(args[++i]);
        } else if (a == L"--module-dir" && i + 1 < args.size()) {
            extraModuleDirs.emplace_back(args[++i]);
        } else if (a == L"--export-screenshots" && i + 1 < args.size()) {
            exportShotDir = args[++i];
            rsw::ui::ForcedThemeOverride() = 1;
        }
    }

    wchar_t exeBuf[MAX_PATH * 4]{};
    ::GetModuleFileNameW(nullptr, exeBuf, static_cast<DWORD>(std::size(exeBuf)));
    const std::filesystem::path exeDir = std::filesystem::path(exeBuf).parent_path();

    // Headless CI & `insttests.exe` verification mode
    if (headlessSmoke) {
        rsw::ui::RswTheme testTheme{};
        testTheme.Refresh();
        const auto mods = DiscoverAllHelpModules(exeDir, extraModuleDirs);
        for (const auto& m : mods) {
            if (rsw::EqualsIgnoreCase(m.moduleId, "raggiesoft.installer-studio")) {
                if (m.topics.size() < 8) return 3;
                for (const auto& t : m.topics) {
                    const std::string html = ReadUtf8FileToString(m.moduleDir / std::filesystem::path(rsw::Utf8ToWide(t.fileName)));
                    const auto rtf = ConvertHtmlTopicToThemedRtf(html, testTheme);
                    if (rtf.rtfBytes.size() < 120) return 4;
                }
                return 0;
            }
        }
        return 2;
    }

    // Reuse an already-open `RaggieSoftHelp.exe` window via Named Pipe IPC (< 5ms `F1` navigation!)
    if (exportShotDir.empty() && TryForwardRequestToRunningInstance(reqModuleId, reqTopicId, reqAnchorId)) {
        return 0;
    }

    ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    rsw::ui::ScopedGdiPlusSession gdiPlusSession;
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES | ICC_TREEVIEW_CLASSES | ICC_LISTVIEW_CLASSES};
    ::InitCommonControlsEx(&icc);

    RaggieSoftHelpWindow viewer{};
    viewer.theme.Refresh(RGB(37, 99, 235), RGB(251, 191, 36));
    viewer.modules = DiscoverAllHelpModules(exeDir, extraModuleDirs);

    for (std::size_t i = 0; i < viewer.modules.size(); ++i) {
        if (rsw::EqualsIgnoreCase(viewer.modules[i].moduleId, reqModuleId)) {
            viewer.activeModIdx = i;
            break;
        }
    }

    constexpr const wchar_t* kCls = L"RaggieSoftHelpViewerNativeWindow";
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &RaggieSoftHelpWindow::WndProc;
    wc.hInstance     = hInstance;
    wc.hCursor       = ::LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon         = ::LoadIconW(nullptr, IDI_APPLICATION);
    wc.lpszClassName = kCls;
    ::RegisterClassExW(&wc);

    const UINT dpi = ::GetDpiForSystem();
    const int winW = rsw::ui::ScalePx(1160, dpi, viewer.theme.textScalePercent);
    const int winH = rsw::ui::ScalePx(740,  dpi, viewer.theme.textScalePercent);
    const int posX = std::max(0, (::GetSystemMetrics(SM_CXSCREEN) - winW) / 2);
    const int posY = std::max(0, (::GetSystemMetrics(SM_CYSCREEN) - winH) / 2);

    HWND hWnd = ::CreateWindowExW(
        0, kCls, L"RaggieSoft Help Viewer",
        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
        posX, posY, winW, winH,
        nullptr, nullptr, hInstance, &viewer);
    if (!hWnd) {
        ::CoUninitialize();
        return 1;
    }

    viewer.NavigateToTopic(viewer.activeModIdx, reqTopicId, reqAnchorId, true);
    if (exportShotDir.empty()) {
        viewer.StartIpcServer();
    }

    ::ShowWindow(hWnd, nCmdShow);
    ::UpdateWindow(hWnd);

    if (!exportShotDir.empty()) {
        viewer.NavigateToTopic(viewer.activeModIdx, "selection-tree-and-dialogs", "", false);
        const bool ok1 = rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"helpviewer-contents.png");

        viewer.activePane = LeftNavPane::Search;
        viewer.UpdateLeftPaneVisibility();
        ::SetWindowTextW(viewer.hSrchQuery, L"Prerequisite");
        viewer.ExecuteFullTextSearch();
        viewer.NavigateToTopic(viewer.activeModIdx, "runtimes-and-merge-modules", "", false);
        const bool ok2 = rsw::ui::CaptureWindowToPngFile(hWnd, exportShotDir / L"helpviewer-search.png");

        ::DestroyWindow(hWnd);
        ::CoUninitialize();
        return (ok1 && ok2) ? 0 : 2;
    }

    MSG msg{};
    while (::GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!::IsDialogMessageW(hWnd, &msg)) {
            ::TranslateMessage(&msg);
            ::DispatchMessageW(&msg);
        }
    }

    ::CoUninitialize();
    return 0;
}
