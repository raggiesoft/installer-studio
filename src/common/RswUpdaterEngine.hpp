// ================================================================================
// FILE: src/common/RswUpdaterEngine.hpp
// PROJECT: RaggieSoft Updater (`RsUpdate.exe` — Shared DRY `apps-manifest.json` Client)
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is `RsUpdate.exe` and why does it share 100% of its JSON manifest format
//   with **RaggieSoft Launcher**?
//     Instead of running a heavyweight `SYSTEM` background Windows Service (`NT AUTHORITY\SYSTEM`)
//     or maintaining two separate update schemas, RaggieSoft follows the **DRY Principle**:
//       - One single static JSON array (`apps-manifest.json`, hosted on Cloudflare, S3,
//         DigitalOcean Spaces, GitHub Releases, or an enterprise file share) powers BOTH
//         **RaggieSoft Launcher** (`RaggieSoftLauncher.exe`) AND standalone white-label
//         **RaggieSoft Updater** (`RsUpdate.exe`)!
//
//   How does `RsUpdate.exe` choose between a tiny Binary Delta vs. a Full Upgrade?
//     1. Parses `apps-manifest.json` using an embedded RFC 8259 recursive-descent JSON
//        parser (`JsonNode`) and finds the target product by `id` or `upgradeCode`/`innoAppId`.
//     2. Detects the currently installed version (from `--installed-version`, local
//        `UpdateConfig.json`, MSI `MsiEnumRelatedProductsW`, or Windows `Uninstall` Registry).
//     3. If `latestVersion > installedVersion`:
//          - **Delta Fast-Path**: If `patches` contains a matching `.msp` or `.rswpatch`
//            entry for `(fromVersion == installedVersion, arch == hostArch)`, downloads ONLY
//            that kilobyte-scale delta!
//          - **Major Upgrade Fallback**: Otherwise selects `arm64InstallerUrl` (on ARM64),
//            `x64InstallerUrl` (on x64), or `installerDownloadUrl`!
//     4. Verifies `SHA-256` (`bcrypt.dll`) before touching disk, then applies `.rswpatch`
//        via `ApplyRswPatchTransaction`, `.msp` via `MsiApplyPatchW`, `.msi` via
//        `MsiInstallProductW`, or `-setup.exe` / **RaggieScript EXE** cleanly!
// ================================================================================

#pragma once

#include "RswBinaryDelta.hpp"
#include "RswCryptoAndPe.hpp"
#include "RswPlatform.hpp"
#include "RswPrerequisites.hpp"
#include "RswVersion.hpp"

#include <msi.h>
#include <shellapi.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace rsw::updater {

// ================================================================================
// 1. MINIMAL ZERO-DEPENDENCY RFC 8259 JSON PARSER (`apps-manifest.json` + `UpdateConfig.json`)
// ================================================================================
struct JsonNode {
    enum class Kind { Null, Bool, Number, String, Array, Object };
    Kind                             kind    = Kind::Null;
    bool                             boolVal = false;
    double                           numVal  = 0.0;
    std::string                      strVal;
    std::vector<JsonNode>            arrVal;
    std::map<std::string, JsonNode>  objVal;

    [[nodiscard]] std::string GetStr(const std::string& k, const std::string& defV = "") const {
        auto it = objVal.find(k);
        return (it != objVal.end() && it->second.kind == Kind::String) ? it->second.strVal : defV;
    }

    [[nodiscard]] std::uint64_t GetU64(const std::string& k, std::uint64_t defV = 0) const {
        auto it = objVal.find(k);
        if (it == objVal.end()) return defV;
        if (it->second.kind == Kind::Number && it->second.numVal >= 0.0) {
            return static_cast<std::uint64_t>(it->second.numVal);
        }
        if (it->second.kind == Kind::String && !it->second.strVal.empty()) {
            return static_cast<std::uint64_t>(std::strtoull(it->second.strVal.c_str(), nullptr, 10));
        }
        return defV;
    }
};

class MinimalJsonParser {
public:
    explicit MinimalJsonParser(std::string_view src) : s_(src) {}

    bool ParseRoot(JsonNode& out, std::string& outErr) {
        SkipWs();
        // Skip optional UTF-8 BOM if present
        if (pos_ + 3 <= s_.size() &&
            static_cast<unsigned char>(s_[pos_]) == 0xEF &&
            static_cast<unsigned char>(s_[pos_ + 1]) == 0xBB &&
            static_cast<unsigned char>(s_[pos_ + 2]) == 0xBF) {
            pos_ += 3;
            SkipWs();
        }
        if (!ParseValue(out, outErr)) return false;
        SkipWs();
        return true;
    }

private:
    std::string_view s_;
    std::size_t      pos_ = 0;

    void SkipWs() noexcept {
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') ++pos_;
            else break;
        }
    }

    bool ParseValue(JsonNode& out, std::string& err) {
        SkipWs();
        if (pos_ >= s_.size()) {
            err = "Unexpected end of JSON input.";
            return false;
        }
        const char c = s_[pos_];
        if (c == '"') {
            out = {};
            out.kind = JsonNode::Kind::String;
            return ParseString(out.strVal, err);
        }
        if (c == '{') return ParseObject(out, err);
        if (c == '[') return ParseArray(out, err);
        if (c == 't' && s_.substr(pos_, 4) == "true") {
            pos_ += 4;
            out = {};
            out.kind    = JsonNode::Kind::Bool;
            out.boolVal = true;
            return true;
        }
        if (c == 'f' && s_.substr(pos_, 5) == "false") {
            pos_ += 5;
            out = {};
            out.kind    = JsonNode::Kind::Bool;
            out.boolVal = false;
            return true;
        }
        if (c == 'n' && s_.substr(pos_, 4) == "null") {
            pos_ += 4;
            out = {};
            out.kind = JsonNode::Kind::Null;
            return true;
        }
        if (c == '-' || (c >= '0' && c <= '9')) {
            const std::size_t start = pos_;
            if (s_[pos_] == '-') ++pos_;
            while (pos_ < s_.size() && ((s_[pos_] >= '0' && s_[pos_] <= '9') || s_[pos_] == '.' || s_[pos_] == 'e' || s_[pos_] == 'E' || s_[pos_] == '+' || s_[pos_] == '-')) {
                ++pos_;
            }
            out = {};
            out.kind   = JsonNode::Kind::Number;
            out.numVal = std::strtod(std::string(s_.substr(start, pos_ - start)).c_str(), nullptr);
            return true;
        }
        err = "Unexpected token in JSON at byte offset " + std::to_string(pos_) + ".";
        return false;
    }

    bool ParseString(std::string& out, std::string& err) {
        out.clear();
        if (pos_ >= s_.size() || s_[pos_] != '"') {
            err = "Expected '\"' opening JSON string.";
            return false;
        }
        ++pos_;
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (c == '\\') {
                if (pos_ >= s_.size()) { err = "Unterminated JSON escape."; return false; }
                const char esc = s_[pos_++];
                switch (esc) {
                    case '"':  out.push_back('"');  break;
                    case '\\': out.push_back('\\'); break;
                    case '/':  out.push_back('/');  break;
                    case 'b':  out.push_back('\b'); break;
                    case 'f':  out.push_back('\f'); break;
                    case 'n':  out.push_back('\n'); break;
                    case 'r':  out.push_back('\r'); break;
                    case 't':  out.push_back('\t'); break;
                    case 'u':
                        if (pos_ + 4 <= s_.size()) {
                            unsigned cp = static_cast<unsigned>(std::strtoul(std::string(s_.substr(pos_, 4)).c_str(), nullptr, 16));
                            pos_ += 4;
                            if (cp < 0x80u) out.push_back(static_cast<char>(cp));
                            else if (cp < 0x800u) {
                                out.push_back(static_cast<char>(0xC0u | (cp >> 6)));
                                out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                            } else {
                                out.push_back(static_cast<char>(0xE0u | (cp >> 12)));
                                out.push_back(static_cast<char>(0x80u | ((cp >> 6) & 0x3Fu)));
                                out.push_back(static_cast<char>(0x80u | (cp & 0x3Fu)));
                            }
                        }
                        break;
                    default: out.push_back(esc); break;
                }
            } else {
                out.push_back(c);
            }
        }
        err = "Unterminated JSON string literal.";
        return false;
    }

    bool ParseArray(JsonNode& out, std::string& err) {
        out = {};
        out.kind = JsonNode::Kind::Array;
        ++pos_; // skip '['
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            JsonNode elem;
            if (!ParseValue(elem, err)) return false;
            out.arrVal.push_back(std::move(elem));
            SkipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == ']') {
                ++pos_;
                return true;
            }
            err = "Expected ',' or ']' in JSON array.";
            return false;
        }
    }

    bool ParseObject(JsonNode& out, std::string& err) {
        out = {};
        out.kind = JsonNode::Kind::Object;
        ++pos_; // skip '{'
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            SkipWs();
            std::string key;
            if (!ParseString(key, err)) return false;
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') {
                err = "Expected ':' after JSON object key '" + key + "'.";
                return false;
            }
            ++pos_;
            JsonNode val;
            if (!ParseValue(val, err)) return false;
            out.objVal[std::move(key)] = std::move(val);
            SkipWs();
            if (pos_ < s_.size() && s_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < s_.size() && s_[pos_] == '}') {
                ++pos_;
                return true;
            }
            err = "Expected ',' or '}' in JSON object.";
            return false;
        }
    }
};

// ================================================================================
// 2. DRY `apps-manifest.json` SCHEMA STRUCTURES & RESOLUTION ENGINE
// ================================================================================
struct ManifestPatchOption {
    std::string   fromVersion;
    std::string   arch; // `"x64"`, `"arm64"`, or `""` (any)
    std::string   kind; // `"msp"` or `"rswpatch"`
    std::string   url;
    std::string   sha256;
    std::uint64_t sizeBytes = 0;
};

struct ManifestAppEntry {
    std::string                      id;
    std::string                      upgradeCode; // Parsed from `"upgradeCode"` or `"innoAppId"`!
    std::string                      title;
    std::string                      imprint;
    std::string                      category;
    std::string                      description;
    std::string                      latestVersion;
    std::string                      exeName;
    std::string                      defaultSubFolder;
    std::string                      installerFileName;
    std::string                      installerDownloadUrl;
    std::string                      sha256;
    std::uint64_t                    sizeBytes      = 0;
    std::string                      x64InstallerUrl;
    std::string                      x64Sha256;
    std::uint64_t                    x64SizeBytes   = 0;
    std::string                      arm64InstallerUrl;
    std::string                      arm64Sha256;
    std::uint64_t                    arm64SizeBytes = 0;
    std::string                      webUrl;
    std::string                      releaseNotes;
    std::vector<ManifestPatchOption> patches;
};

struct InstalledUpdateConfig {
    std::string           appId;
    std::string           upgradeCode;
    std::string           productName;
    std::string           installedVersion;
    std::string           architecture;
    std::string           channel = "stable";
    std::string           manifestSource; // HTTPS URL, `file:///`, or local filesystem path
    std::filesystem::path installDir;
};

struct UpdateEvaluationResult {
    bool              ok               = false;
    bool              updateAvailable  = false;
    std::string       appId;
    std::string       productTitle;
    std::string       upgradeCode;
    std::string       installedVersion;
    std::string       latestVersion;
    std::string       targetArch;
    std::string       selectedKind;     // `"rswpatch"` | `"msp"` | `"installer"`
    std::string       selectedUrl;
    std::string       selectedSha256;
    std::uint64_t     selectedBytes    = 0;
    std::string       webUrl;
    std::string       description;
    std::string       errorMessage;
};

/// Fetches UTF-8 text from an `https://` URL (`WinHTTP`), `file:///` URI, or local filesystem path!
inline bool FetchManifestTextFromUrlOrFile(
    std::string_view urlOrPath,
    const std::atomic<bool>* cancelFlag,
    std::string& outText,
    std::string& outError)
{
    outText.clear();
    std::string raw = TrimAscii(urlOrPath);
    if (raw.empty()) {
        outError = "No update manifest URL or file path was configured.";
        return false;
    }

    if (_strnicmp(raw.c_str(), "https://", 8) == 0 || _strnicmp(raw.c_str(), "http://", 7) == 0) {
        std::error_code ec;
        const auto tmpJson = std::filesystem::temp_directory_path(ec) /
                             (L"rsupdate-manifest-" + std::to_wstring(::GetCurrentProcessId()) + L".json");
        if (!DownloadFileViaWinHttp(raw, tmpJson, cancelFlag, nullptr, outError)) {
            return false;
        }
        std::vector<std::uint8_t> bytes;
        const bool okRead = ReadBinaryFileBytes(tmpJson, bytes, outError);
        std::filesystem::remove(tmpJson, ec);
        if (!okRead) return false;
        outText.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return true;
    }

    if (_strnicmp(raw.c_str(), "file:///", 8) == 0) {
        raw.erase(0, 8);
        for (char& c : raw) if (c == '/') c = '\\';
    } else if (_strnicmp(raw.c_str(), "file://", 7) == 0) {
        raw.erase(0, 7);
    }

    std::vector<std::uint8_t> bytes;
    if (!ReadBinaryFileBytes(std::filesystem::path(Utf8ToWide(raw)), bytes, outError)) {
        return false;
    }
    outText.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    return true;
}

/// Downloads or copies a selected update payload (`https://`, `file:///`, or local path) to `destFile`
/// and cryptographically verifies its `SHA-256` digest via `bcrypt.dll`!
inline bool AcquireAndVerifyUpdatePayload(
    std::string_view urlOrPath,
    std::string_view expectedSha256,
    const std::filesystem::path& destFile,
    const std::atomic<bool>* cancelFlag,
    const std::function<void(std::uint64_t, std::uint64_t)>& onProgress,
    std::string& outError)
{
    std::error_code ec;
    std::filesystem::create_directories(destFile.parent_path(), ec);
    std::string src = TrimAscii(urlOrPath);
    if (_strnicmp(src.c_str(), "https://", 8) == 0 || _strnicmp(src.c_str(), "http://", 7) == 0) {
        if (!DownloadFileViaWinHttp(src, destFile, cancelFlag, onProgress, outError)) {
            return false;
        }
    } else {
        if (_strnicmp(src.c_str(), "file:///", 8) == 0) {
            src.erase(0, 8);
            for (char& c : src) if (c == '/') c = '\\';
        }
        const std::filesystem::path localSrc(Utf8ToWide(src));
        if (!std::filesystem::is_regular_file(localSrc, ec)) {
            outError = "Update package file not found: " + WideToUtf8(localSrc.wstring());
            return false;
        }
        if (!std::filesystem::copy_file(localSrc, destFile, std::filesystem::copy_options::overwrite_existing, ec)) {
            outError = "Failed staging local update package: " + WideToUtf8(localSrc.wstring());
            return false;
        }
        const auto sz = static_cast<std::uint64_t>(std::filesystem::file_size(destFile, ec));
        if (onProgress) onProgress(sz, sz);
    }

    if (!expectedSha256.empty()) {
        std::string actualSha;
        if (!ComputeFileSha256Hex(destFile, actualSha) || !EqualsIgnoreCase(actualSha, expectedSha256)) {
            std::filesystem::remove(destFile, ec);
            outError = "SHA-256 checksum mismatch on downloaded update package (expected " +
                       std::string(expectedSha256).substr(0, 16) + "..., got " + actualSha.substr(0, 16) + "...).";
            return false;
        }
    }
    return true;
}

/// Parses `apps-manifest.json` (`[ ... ]` or `{ "apps": [ ... ] }`) into `ManifestAppEntry` items.
inline bool ParseAppsManifestJson(
    std::string_view jsonText,
    std::vector<ManifestAppEntry>& outApps,
    std::string& outError)
{
    outApps.clear();
    JsonNode root;
    MinimalJsonParser parser(jsonText);
    if (!parser.ParseRoot(root, outError)) {
        return false;
    }

    const std::vector<JsonNode>* listPtr = nullptr;
    if (root.kind == JsonNode::Kind::Array) {
        listPtr = &root.arrVal;
    } else if (root.kind == JsonNode::Kind::Object) {
        auto it = root.objVal.find("apps");
        if (it != root.objVal.end() && it->second.kind == JsonNode::Kind::Array) {
            listPtr = &it->second.arrVal;
        }
    }
    if (!listPtr) {
        outError = "apps-manifest.json root must be a JSON array '[ ... ]' (or '{ \"apps\": [ ... ] }').";
        return false;
    }

    for (const auto& item : *listPtr) {
        if (item.kind != JsonNode::Kind::Object) continue;
        ManifestAppEntry e{};
        e.id                   = item.GetStr("id");
        e.upgradeCode          = item.GetStr("upgradeCode", item.GetStr("innoAppId"));
        e.title                = item.GetStr("title");
        e.imprint              = item.GetStr("imprint");
        e.category             = item.GetStr("category");
        e.description          = item.GetStr("description");
        e.latestVersion        = item.GetStr("latestVersion");
        e.exeName              = item.GetStr("exeName");
        e.defaultSubFolder     = item.GetStr("defaultSubFolder");
        e.installerFileName    = item.GetStr("installerFileName");
        e.installerDownloadUrl = item.GetStr("installerDownloadUrl");
        e.sha256               = item.GetStr("sha256");
        e.sizeBytes            = item.GetU64("sizeBytes");
        e.x64InstallerUrl      = item.GetStr("x64InstallerUrl");
        e.x64Sha256            = item.GetStr("x64Sha256");
        e.x64SizeBytes         = item.GetU64("x64SizeBytes");
        e.arm64InstallerUrl    = item.GetStr("arm64InstallerUrl");
        e.arm64Sha256          = item.GetStr("arm64Sha256");
        e.arm64SizeBytes       = item.GetU64("arm64SizeBytes");
        e.webUrl               = item.GetStr("webUrl");
        e.releaseNotes         = item.GetStr("releaseNotes");

        auto pit = item.objVal.find("patches");
        if (pit != item.objVal.end() && pit->second.kind == JsonNode::Kind::Array) {
            for (const auto& pn : pit->second.arrVal) {
                if (pn.kind != JsonNode::Kind::Object) continue;
                ManifestPatchOption po{};
                po.fromVersion = pn.GetStr("fromVersion");
                po.arch        = AsciiLower(pn.GetStr("arch"));
                po.kind        = AsciiLower(pn.GetStr("kind", "rswpatch"));
                po.url         = pn.GetStr("url");
                po.sha256      = pn.GetStr("sha256");
                po.sizeBytes   = pn.GetU64("sizeBytes");
                if (!po.url.empty()) {
                    e.patches.push_back(std::move(po));
                }
            }
        }
        outApps.push_back(std::move(e));
    }
    return true;
}

/// Loads `<dir>\UpdateConfig.json` if present next to `RsUpdate.exe` or inside `--install-dir`.
inline void LoadLocalUpdateConfigIfPresent(const std::filesystem::path& configDir, InstalledUpdateConfig& ioCfg) {
    std::error_code ec;
    const auto p = configDir / L"UpdateConfig.json";
    if (!std::filesystem::is_regular_file(p, ec)) return;
    std::vector<std::uint8_t> raw;
    std::string err;
    if (!ReadBinaryFileBytes(p, raw, err)) return;

    JsonNode node;
    MinimalJsonParser parser(std::string_view(reinterpret_cast<const char*>(raw.data()), raw.size()));
    if (!parser.ParseRoot(node, err) || node.kind != JsonNode::Kind::Object) return;

    if (ioCfg.appId.empty())            ioCfg.appId            = node.GetStr("appId");
    if (ioCfg.upgradeCode.empty())      ioCfg.upgradeCode      = node.GetStr("upgradeCode");
    if (ioCfg.productName.empty())      ioCfg.productName      = node.GetStr("productName");
    if (ioCfg.installedVersion.empty()) ioCfg.installedVersion = node.GetStr("installedVersion");
    if (ioCfg.architecture.empty())     ioCfg.architecture     = node.GetStr("architecture");
    if (ioCfg.manifestSource.empty())   ioCfg.manifestSource   = node.GetStr("manifestUrl");
}

/// Auto-detects `installedVersion` (and `installDir`) from MSI (`MsiEnumRelatedProductsW`) or Windows `Uninstall` Registry!
inline void DetectInstalledStateByUpgradeCode(InstalledUpdateConfig& ioCfg) {
    if (ioCfg.upgradeCode.empty()) return;

    // 1. Check virtual registry test store first (`insttests.exe` zero-registry isolation)
    const auto virtRoot = native_detail::GetVirtualRegistryStoreRoot();
    if (!virtRoot.empty()) {
        const std::wstring sub = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + Utf8ToWide(ioCfg.upgradeCode);
        for (const char* hive : {"HKCU", "HKLM"}) {
            std::string ver, loc;
            if (ioCfg.installedVersion.empty() && native_detail::VirtualRegReadValue(virtRoot, hive, sub, L"DisplayVersion", ver)) {
                ioCfg.installedVersion = ver;
            }
            if (ioCfg.installDir.empty() && native_detail::VirtualRegReadValue(virtRoot, hive, sub, L"InstallLocation", loc) && !loc.empty()) {
                ioCfg.installDir = std::filesystem::path(Utf8ToWide(loc));
            }
        }
        return;
    }

    // 2. Check Windows Installer (`MsiEnumRelatedProductsW`)
    wchar_t prodBuf[39]{};
    const std::wstring wUpg = Utf8ToWide(ioCfg.upgradeCode);
    if (::MsiEnumRelatedProductsW(wUpg.c_str(), 0, 0, prodBuf) == ERROR_SUCCESS) {
        wchar_t verBuf[64]{};
        DWORD cch = static_cast<DWORD>(std::size(verBuf));
        if (ioCfg.installedVersion.empty() &&
            ::MsiGetProductInfoW(prodBuf, INSTALLPROPERTY_VERSIONSTRING, verBuf, &cch) == ERROR_SUCCESS) {
            ioCfg.installedVersion = WideToUtf8(verBuf);
        }
        wchar_t locBuf[MAX_PATH * 2]{};
        DWORD cchLoc = static_cast<DWORD>(std::size(locBuf));
        if (ioCfg.installDir.empty() &&
            ::MsiGetProductInfoW(prodBuf, INSTALLPROPERTY_INSTALLLOCATION, locBuf, &cchLoc) == ERROR_SUCCESS &&
            locBuf[0] != L'\0') {
            ioCfg.installDir = std::filesystem::path(locBuf);
        }
    }

    // 3. Check `HKLM` / `HKCU` `Uninstall\<UpgradeCode>` (for **RaggieScript EXE** installs!)
    const std::wstring subKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\" + wUpg;
    for (HKEY hRoot : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER}) {
        HKEY hk = nullptr;
        if (::RegOpenKeyExW(hRoot, subKey.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hk) == ERROR_SUCCESS) {
            wchar_t vBuf[128]{};
            DWORD cb = sizeof(vBuf), tp = 0;
            if (ioCfg.installedVersion.empty() &&
                ::RegQueryValueExW(hk, L"DisplayVersion", nullptr, &tp, reinterpret_cast<LPBYTE>(vBuf), &cb) == ERROR_SUCCESS) {
                ioCfg.installedVersion = WideToUtf8(vBuf);
            }
            wchar_t lBuf[MAX_PATH * 2]{};
            cb = sizeof(lBuf);
            if (ioCfg.installDir.empty() &&
                ::RegQueryValueExW(hk, L"InstallLocation", nullptr, &tp, reinterpret_cast<LPBYTE>(lBuf), &cb) == ERROR_SUCCESS &&
                lBuf[0] != L'\0') {
                ioCfg.installDir = std::filesystem::path(lBuf);
            }
            ::RegCloseKey(hk);
        }
    }
}

/// Evaluates `apps-manifest.json` for `cfg` and selects either a matching binary delta (`.msp`/`.rswpatch`)
/// or the architecture-native full upgrade installer!
inline UpdateEvaluationResult EvaluateAvailableUpdate(
    const InstalledUpdateConfig& cfg,
    const std::atomic<bool>* cancelFlag = nullptr)
{
    UpdateEvaluationResult res{};
    res.appId            = cfg.appId;
    res.upgradeCode      = cfg.upgradeCode;
    res.installedVersion = cfg.installedVersion;
    res.targetArch       = !cfg.architecture.empty()
        ? AsciiLower(cfg.architecture)
        : std::string(QueryHostPlatform().NativeArchSlug());

    std::string jsonText;
    if (!FetchManifestTextFromUrlOrFile(cfg.manifestSource, cancelFlag, jsonText, res.errorMessage)) {
        return res;
    }

    std::vector<ManifestAppEntry> apps;
    if (!ParseAppsManifestJson(jsonText, apps, res.errorMessage)) {
        return res;
    }

    const ManifestAppEntry* matched = nullptr;
    for (const auto& a : apps) {
        if (!cfg.appId.empty() && EqualsIgnoreCase(a.id, cfg.appId)) {
            matched = &a;
            break;
        }
        if (!cfg.upgradeCode.empty() && EqualsIgnoreCase(a.upgradeCode, cfg.upgradeCode)) {
            matched = &a;
            break;
        }
    }
    if (!matched && apps.size() == 1 && cfg.appId.empty() && cfg.upgradeCode.empty()) {
        matched = &apps.front();
    }
    if (!matched) {
        res.errorMessage = "Target application ('" + (cfg.appId.empty() ? cfg.upgradeCode : cfg.appId) +
                           "') was not found in apps-manifest.json.";
        return res;
    }

    res.ok           = true;
    res.appId        = matched->id;
    res.productTitle = matched->title;
    res.upgradeCode  = matched->upgradeCode;
    res.latestVersion= matched->latestVersion;
    res.webUrl       = matched->webUrl;
    res.description  = !matched->releaseNotes.empty() ? matched->releaseNotes : matched->description;

    if (res.installedVersion.empty()) {
        InstalledUpdateConfig probe = cfg;
        probe.upgradeCode = matched->upgradeCode;
        DetectInstalledStateByUpgradeCode(probe);
        res.installedVersion = probe.installedVersion.empty() ? "0.0.0" : probe.installedVersion;
    }

    if (CompareDottedVersions(res.latestVersion, res.installedVersion) <= 0) {
        res.updateAvailable = false;
        return res;
    }

    res.updateAvailable = true;

    // 1. Check for an exact `fromVersion` + `arch` delta patch (`.msp` or `.rswpatch`)!
    for (const auto& p : matched->patches) {
        const bool verOk  = EqualsIgnoreCase(p.fromVersion, res.installedVersion);
        const bool archOk = p.arch.empty() || EqualsIgnoreCase(p.arch, res.targetArch);
        if (verOk && archOk && !p.url.empty()) {
            res.selectedKind   = p.kind;
            res.selectedUrl    = p.url;
            res.selectedSha256 = p.sha256;
            res.selectedBytes  = p.sizeBytes;
            return res;
        }
    }

    // 2. Fallback to architecture-native (`ARM64` or `x64`) or Universal full installer!
    res.selectedKind = "installer";
    if (res.targetArch == "arm64" && !matched->arm64InstallerUrl.empty()) {
        res.selectedUrl    = matched->arm64InstallerUrl;
        res.selectedSha256 = matched->arm64Sha256;
        res.selectedBytes  = matched->arm64SizeBytes;
    } else if (res.targetArch == "x64" && !matched->x64InstallerUrl.empty()) {
        res.selectedUrl    = matched->x64InstallerUrl;
        res.selectedSha256 = matched->x64Sha256;
        res.selectedBytes  = matched->x64SizeBytes;
    } else {
        res.selectedUrl    = matched->installerDownloadUrl;
        res.selectedSha256 = matched->sha256;
        res.selectedBytes  = matched->sizeBytes;
    }
    return res;
}

/// Applies a verified downloaded update package (`.rswpatch`, `.msp`, `.msi`, or `-setup.exe`)!
inline bool ApplyDownloadedUpdatePackage(
    const UpdateEvaluationResult& eval,
    const std::filesystem::path& packagePath,
    const std::filesystem::path& installDir,
    bool silentMode,
    std::string& outError)
{
    const std::string ext = AsciiLower(WideToUtf8(packagePath.extension().wstring()));

    if (eval.selectedKind == "rswpatch" || ext == ".rswpatch") {
        if (installDir.empty()) {
            outError = "Applying a .rswpatch binary delta requires --install-dir (or an installed Uninstall/UpdateConfig path).";
            return false;
        }
        RswPatchApplySummary sum{};
        return ApplyRswPatchTransaction(packagePath, installDir, 0, sum, outError);
    }

    if (eval.selectedKind == "msp" || ext == ".msp") {
        ::MsiSetInternalUI(silentMode ? INSTALLUILEVEL_NONE : INSTALLUILEVEL_BASIC, nullptr);
        const UINT rc = ::MsiApplyPatchW(
            packagePath.wstring().c_str(), nullptr, INSTALLTYPE_DEFAULT, L"REINSTALL=ALL REINSTALLMODE=omus");
        if (rc != ERROR_SUCCESS && rc != ERROR_SUCCESS_REBOOT_REQUIRED) {
            outError = "MsiApplyPatchW returned Windows Installer status " + std::to_string(rc) + ".";
            return false;
        }
        return true;
    }

    if (ext == ".msi") {
        ::MsiSetInternalUI(silentMode ? INSTALLUILEVEL_NONE : INSTALLUILEVEL_BASIC, nullptr);
        std::wstring props;
        if (!installDir.empty()) {
            props = L"INSTALLDIR=\"" + installDir.wstring() + L"\"";
        }
        UINT rc = ::MsiInstallProductW(packagePath.wstring().c_str(), props.c_str());
        if (rc == ERROR_PRODUCT_VERSION /* 1638 — Minor Upgrade with same ProductCode! */) {
            if (!props.empty()) props += L" ";
            props += L"REINSTALL=ALL REINSTALLMODE=vomus";
            rc = ::MsiInstallProductW(packagePath.wstring().c_str(), props.c_str());
        }
        if (rc != ERROR_SUCCESS && rc != ERROR_SUCCESS_REBOOT_REQUIRED) {
            outError = "MsiInstallProductW returned status " + std::to_string(rc) + ".";
            return false;
        }
        return true;
    }

    // Launch `-setup.exe` / **RaggieScript EXE** child installer!
    std::wstring cmd = L"\"" + packagePath.wstring() + L"\"";
    if (silentMode) cmd += L" /s";
    if (!installDir.empty()) {
        cmd += L" INSTALLDIR=\"" + installDir.wstring() + L"\"";
    }
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!::CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
        outError = "Failed launching installer executable (" + std::to_string(::GetLastError()) + ").";
        return false;
    }
    ::WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD exitCode = 1;
    ::GetExitCodeProcess(pi.hProcess, &exitCode);
    ::CloseHandle(pi.hThread);
    ::CloseHandle(pi.hProcess);
    if (exitCode != 0 && exitCode != ERROR_SUCCESS_REBOOT_REQUIRED) {
        outError = "Update installer exited with code " + std::to_string(exitCode) + ".";
        return false;
    }
    return true;
}

/// Formats `UpdateEvaluationResult` as strict comment-free JSON (`--json-out=<file>`).
inline std::string FormatUpdateEvaluationJson(const UpdateEvaluationResult& r, bool applied = false) {
    auto esc = [](std::string_view s) {
        std::string out;
        for (unsigned char c : s) {
            if (c == '"') out += "\\\"";
            else if (c == '\\') out += "\\\\";
            else if (c == '\n') out += "\\n";
            else if (c == '\r') out += "\\r";
            else out += static_cast<char>(c);
        }
        return out;
    };
    std::ostringstream o;
    o << "{\n"
      << "  \"ok\": " << (r.ok ? "true" : "false") << ",\n"
      << "  \"updateAvailable\": " << (r.updateAvailable ? "true" : "false") << ",\n"
      << "  \"applied\": " << (applied ? "true" : "false") << ",\n"
      << "  \"appId\": \"" << esc(r.appId) << "\",\n"
      << "  \"title\": \"" << esc(r.productTitle) << "\",\n"
      << "  \"upgradeCode\": \"" << esc(r.upgradeCode) << "\",\n"
      << "  \"installedVersion\": \"" << esc(r.installedVersion) << "\",\n"
      << "  \"latestVersion\": \"" << esc(r.latestVersion) << "\",\n"
      << "  \"architecture\": \"" << esc(r.targetArch) << "\",\n"
      << "  \"selectedKind\": \"" << esc(r.selectedKind) << "\",\n"
      << "  \"selectedUrl\": \"" << esc(r.selectedUrl) << "\",\n"
      << "  \"selectedSha256\": \"" << esc(r.selectedSha256) << "\",\n"
      << "  \"selectedBytes\": " << r.selectedBytes << ",\n"
      << "  \"error\": \"" << esc(r.errorMessage) << "\"\n"
      << "}\n";
    return o.str();
}

} // namespace rsw::updater
