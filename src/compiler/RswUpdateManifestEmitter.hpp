// ================================================================================
// FILE: src/compiler/RswUpdateManifestEmitter.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What does `instbuild.exe --emit-update-manifest <project.rsip> <apps-manifest.json>` do?
//     Following the **DRY (Don't Repeat Yourself) Principle**, **RaggieSoft Updater**
//     (`RsUpdate.exe`), **RaggieSoft Launcher** (`RaggieSoftLauncher.exe`), and
//     **RaggieSoft Launcher Studio** ALL consume the exact same static-CDN JSON array
//     schema (`apps-manifest.json`):
//
//       [
//         {
//           "id": "installer-studio",
//           "innoAppId": "{D9C0EF36-990C-519C-A9C7-D7E19FDD5627}",
//           "upgradeCode": "{D9C0EF36-990C-519C-A9C7-D7E19FDD5627}",
//           "title": "RaggieSoft Installer Studio",
//           "imprint": "RaggieSoft Studio",
//           "category": "Studio",
//           "description": "...",
//           "latestVersion": "0.8.0",
//           "exeName": "InstallerStudio.exe",
//           "defaultSubFolder": "RaggieSoft\\Installer Studio",
//           "installerFileName": "raggiesoft-installer-studio-v0.8.0-offline-setup.exe",
//           "installerDownloadUrl": "https://...",
//           "sha256": "...",
//           "sizeBytes": 1234567,
//           "x64InstallerUrl": "https://...",
//           "x64Sha256": "...",
//           "x64SizeBytes": 123456,
//           "arm64InstallerUrl": "https://...",
//           "arm64Sha256": "...",
//           "arm64SizeBytes": 123456,
//           "webUrl": "https://raggiesoftstudio.com/installer-studio",
//           "patches": []
//         }
//       ]
//
//     This emitter inspects a `.rsip` manifest and its compiled output directory,
//     calculates `SHA-256` (`bcrypt.dll`) and byte sizes for Universal, `x64`, `ARM64`,
//     and any `.msp` / `.rswpatch` delta files present alongside the build, and writes
//     valid comment-free JSON ready for Cloudflare / S3 / DigitalOcean Spaces / GitHub Releases!
// ================================================================================

#pragma once

#include "../common/RswCryptoAndPe.hpp"
#include "../common/RswManifestAst.hpp"
#include "../common/RswPlatform.hpp"
#include "RswValidator.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace rsw {

/// Emits a single-entry (or multi-artifact) `apps-manifest.json` file for `doc` + `artifactDir`.
inline bool EmitLauncherCompatibleUpdateManifest(
    const RswManifestDocument& doc,
    const std::filesystem::path& artifactDir,
    const std::filesystem::path& outManifestJsonPath,
    std::string_view baseUrlOverride,
    std::string& outError)
{
    std::error_code ec;
    const std::string name    = doc.GetString("Package", "", "Name", "Application");
    const std::string version = doc.GetString("Package", "", "Version", "1.0.0");
    const std::string pub     = doc.GetString("Package", "", "Manufacturer", "RaggieSoft");
    const std::string desc    = doc.GetString("Package", "", "Description", doc.GetString("Package", "", "Tagline", name));
    const std::string webUrl  = doc.GetString("Package", "", "AboutUrl", "https://raggiesoftstudio.com");
    const std::string subDir  = doc.GetString("Install", "", "DefaultSubdir", pub + "\\" + name);
    const std::string mainExe = WideToUtf8(std::filesystem::path(Utf8ToWide(doc.GetString("Install", "", "MainExecutable", "App.exe"))).filename().wstring());

    std::string upgGuid;
    NormalizeMsiGuid(doc.GetString("Package", "", "UpgradeCode"), upgGuid);

    std::string appId = SlugifyName(name);
    if (appId.rfind("raggiesoft-", 0) == 0 && appId.size() > 11) {
        appId = appId.substr(11);
    }

    std::string baseUrl = !baseUrlOverride.empty()
        ? std::string(baseUrlOverride)
        : ExpandNamedToken(doc.GetString("Package", "", "UniversalDownloadBaseUrl"), "VERSION", version);
    while (!baseUrl.empty() && baseUrl.back() == '/') baseUrl.pop_back();

    auto makeUrl = [&](const std::filesystem::path& p) -> std::string {
        const std::string fn = WideToUtf8(p.filename().wstring());
        if (!baseUrl.empty()) return baseUrl + "/" + fn;
        std::string local = WideToUtf8(std::filesystem::absolute(p, ec).generic_wstring());
        return "file:///" + local;
    };

    struct FoundBin {
        std::filesystem::path path;
        std::string           sha256;
        std::uint64_t         sizeBytes = 0;
    };
    auto inspectBin = [&](const std::filesystem::path& p, FoundBin& out) -> bool {
        if (!std::filesystem::is_regular_file(p, ec)) return false;
        out.path      = p;
        out.sizeBytes = static_cast<std::uint64_t>(std::filesystem::file_size(p, ec));
        ComputeFileSha256Hex(p, out.sha256);
        out.sha256 = AsciiLower(out.sha256);
        return !out.sha256.empty();
    };

    FoundBin primaryBin{}, x64Bin{}, arm64Bin{};
    struct PatchBin {
        std::string   fromVer;
        std::string   arch;
        std::string   kind;
        FoundBin      bin;
    };
    std::vector<PatchBin> patches;

    if (std::filesystem::is_directory(artifactDir, ec)) {
        for (const auto& de : std::filesystem::directory_iterator(artifactDir, ec)) {
            if (!de.is_regular_file(ec)) continue;
            const std::string fnLow = AsciiLower(WideToUtf8(de.path().filename().wstring()));
            FoundBin fb{};
            if (fnLow.size() > 4 && fnLow.substr(fnLow.size() - 4) == ".exe") {
                if (!inspectBin(de.path(), fb)) continue;
                if (fnLow.find("offline-setup.exe") != std::string::npos) {
                    primaryBin = fb;
                } else if (fnLow.find("-x64-setup.exe") != std::string::npos || fnLow.find("-x64.exe") != std::string::npos) {
                    x64Bin = fb;
                } else if (fnLow.find("-arm64-setup.exe") != std::string::npos || fnLow.find("-arm64.exe") != std::string::npos) {
                    arm64Bin = fb;
                } else if (primaryBin.path.empty() && fnLow.find("-setup.exe") != std::string::npos) {
                    primaryBin = fb;
                }
            } else if ((fnLow.size() > 4 && fnLow.substr(fnLow.size() - 4) == ".msp") ||
                       (fnLow.size() > 9 && fnLow.substr(fnLow.size() - 9) == ".rswpatch")) {
                if (!inspectBin(de.path(), fb)) continue;
                PatchBin pb{};
                pb.bin  = fb;
                pb.kind = (fnLow.substr(fnLow.size() - 4) == ".msp") ? "msp" : "rswpatch";
                pb.arch = (fnLow.find("arm64") != std::string::npos) ? "arm64" : "x64";
                // Parse optional `<stem>-from-<ver>-to-<ver>` convention if present
                const auto fromPos = fnLow.find("from-");
                if (fromPos != std::string::npos) {
                    const std::size_t s = fromPos + 5;
                    std::size_t e = s;
                    while (e < fnLow.size() && ((fnLow[e] >= '0' && fnLow[e] <= '9') || fnLow[e] == '.')) ++e;
                    if (e > s) pb.fromVer = fnLow.substr(s, e - s);
                }
                patches.push_back(std::move(pb));
            }
        }
    }

    if (primaryBin.path.empty()) {
        primaryBin = !arm64Bin.path.empty() ? arm64Bin : x64Bin;
    }

    const std::string primaryFn  = primaryBin.path.empty() ? (SlugifyName(name) + "-v" + version + "-setup.exe") : WideToUtf8(primaryBin.path.filename().wstring());
    const std::string primaryUrl = primaryBin.path.empty() ? (baseUrl.empty() ? "" : (baseUrl + "/" + primaryFn)) : makeUrl(primaryBin.path);

    std::ostringstream json;
    json << "[\n"
         << "  {\n"
         << "    \"id\": \"" << JsonEscapeString(appId) << "\",\n"
         << "    \"innoAppId\": \"" << JsonEscapeString(upgGuid) << "\",\n"
         << "    \"upgradeCode\": \"" << JsonEscapeString(upgGuid) << "\",\n"
         << "    \"title\": \"" << JsonEscapeString(name) << "\",\n"
         << "    \"imprint\": \"" << JsonEscapeString(pub) << "\",\n"
         << "    \"category\": \"Apps\",\n"
         << "    \"description\": \"" << JsonEscapeString(desc) << "\",\n"
         << "    \"latestVersion\": \"" << JsonEscapeString(version) << "\",\n"
         << "    \"exeName\": \"" << JsonEscapeString(mainExe) << "\",\n"
         << "    \"defaultSubFolder\": \"" << JsonEscapeString(subDir) << "\",\n"
         << "    \"installerFileName\": \"" << JsonEscapeString(primaryFn) << "\",\n"
         << "    \"installerDownloadUrl\": \"" << JsonEscapeString(primaryUrl) << "\",\n"
         << "    \"sha256\": \"" << JsonEscapeString(primaryBin.sha256) << "\",\n"
         << "    \"sizeBytes\": " << primaryBin.sizeBytes << ",\n";

    if (!x64Bin.path.empty()) {
        json << "    \"x64InstallerUrl\": \"" << JsonEscapeString(makeUrl(x64Bin.path)) << "\",\n"
             << "    \"x64Sha256\": \"" << JsonEscapeString(x64Bin.sha256) << "\",\n"
             << "    \"x64SizeBytes\": " << x64Bin.sizeBytes << ",\n";
    }
    if (!arm64Bin.path.empty()) {
        json << "    \"arm64InstallerUrl\": \"" << JsonEscapeString(makeUrl(arm64Bin.path)) << "\",\n"
             << "    \"arm64Sha256\": \"" << JsonEscapeString(arm64Bin.sha256) << "\",\n"
             << "    \"arm64SizeBytes\": " << arm64Bin.sizeBytes << ",\n";
    }

    json << "    \"webUrl\": \"" << JsonEscapeString(webUrl) << "\",\n"
         << "    \"patches\": [";
    for (std::size_t i = 0; i < patches.size(); ++i) {
        const auto& p = patches[i];
        json << (i == 0 ? "\n" : ",\n")
             << "      {\n"
             << "        \"fromVersion\": \"" << JsonEscapeString(p.fromVer) << "\",\n"
             << "        \"arch\": \"" << JsonEscapeString(p.arch) << "\",\n"
             << "        \"kind\": \"" << JsonEscapeString(p.kind) << "\",\n"
             << "        \"url\": \"" << JsonEscapeString(makeUrl(p.bin.path)) << "\",\n"
             << "        \"sha256\": \"" << JsonEscapeString(p.bin.sha256) << "\",\n"
             << "        \"sizeBytes\": " << p.bin.sizeBytes << "\n"
             << "      }";
    }
    if (!patches.empty()) json << "\n    ";
    json << "]\n"
         << "  }\n"
         << "]\n";

    if (outManifestJsonPath.has_parent_path()) {
        std::filesystem::create_directories(outManifestJsonPath.parent_path(), ec);
    }
    std::ofstream out(outManifestJsonPath, std::ios::binary | std::ios::trunc);
    if (!out) {
        outError = "Could not write update manifest JSON to: " + WideToUtf8(outManifestJsonPath.wstring());
        return false;
    }
    const std::string text = json.str();
    out.write(text.data(), static_cast<std::streamsize>(text.size()));
    return out.good();
}

} // namespace rsw
