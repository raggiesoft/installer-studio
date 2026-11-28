// ================================================================================
// FILE: src/common/RswEditions.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com/installer-studio/editions
// DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file?
//     This header is the single source of truth for **RaggieSoft Installer Studio's**
//     official Four-Edition capability model (`Community`, `Essentials`, `Developer`,
//     and `Architect`) across both the visual IDE (`InstallerStudio.exe`) and the
//     headless command-line compiler (`instbuild.exe --edition=<tier>`):
//
//       1. `Community Edition` (Free for hobbyists, solo creators, students,
//          educators, non-profits, and open source):
//          Permanently maintains **100% technical feature parity with Architect
//          Edition (`Community == Architect`)**—zero locked views, zero disabled
//          CLI flags, and zero watermarks on customer setups!
//       2. `Essentials Edition` (Commercial Tier 1 — 1 Workstation Seat):
//          Guided no-code `.MSI` + Universal `Setup.exe` (`x64` & `ARM64`), Basic
//          Folder/`.ZIP` Self-Extractor (`PackageKind = sfx` + `%TEMP%` wrapper),
//          100% WCAG 2.2 AA runtime accessibility & Dual-Layer Authenticode / Azure
//          Artifact Signing (NEVER paywalled!), Pre-MSI runtimes, **consumes**
//          existing `.MSM` Merge Modules, and services updates via clean **Major
//          Upgrades** (`1.0.0 -> 1.0.1` fresh `ProductCode`).
//       3. `Developer Edition` (Commercial Tier 2 — Workstation + CI/CD Agents):
//          Adds compiling new `.MSM` Merge Modules, `RaggieScript` (`.rssc`) &
//          **RaggieScript EXE** (`Engine = raggiescript`), `RsUpdate.exe` (shared
//          Launcher JSON updater), custom `[MsiDialog.*]` / `[Dialog.*]` &
//          `[SetupType.*]` designers, `msiprism.exe` SQL table editor, and CI/CD
//          build agent rights.
//       4. `Architect Edition` (Commercial Tier 3 — Enterprise Suites & Compliance):
//          Adds Unified Multi-Product Suites (`PackageKind = suite` & `.rsis`),
//          `.MSP` & `.rswpatch` binary-delta patches (`RsPatch.exe`, `--plan-update`)
//          & `.MST` GPO transforms, Multi-Volume Disk Spanning, Uncompressed Loose
//          Media (`msiexec /a`), `.rswpak` CDN feature packs, and automated HTML5
//          VPAT 2.5 conformance reports (`--emit-vpat`).
//
//   Zero-Telemetry Offline Persistence:
//     Selected/activated edition state is stored locally in:
//       `%LOCALAPPDATA%\RaggieSoft\Installer Studio\edition.json`
//     (and overridable per command via `--edition=<tier>` or `RSW_EDITION=<tier>`),
//     with zero network phone-home or cloud account lock-in.
// ================================================================================

#pragma once

#include "RswPlatform.hpp"
#include "RswVersion.hpp"

#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

namespace rsw {

/// Returns the canonical lowercase CLI/JSON slug for an Installer Studio edition.
inline constexpr std::string_view EditionSlug(RswIdeEdition ed) noexcept {
    switch (ed) {
        case RswIdeEdition::Community:  return "community";
        case RswIdeEdition::Essentials: return "essentials";
        case RswIdeEdition::Developer:  return "developer";
        case RswIdeEdition::Architect:  return "architect";
    }
    return "community";
}

/// Parses `"community"`, `"essentials"`, `"developer"`, or `"architect"` (case-insensitive).
inline bool ParseEditionToken(std::string_view raw, RswIdeEdition& outEd) noexcept {
    const std::string clean = AsciiLower(TrimAscii(raw));
    if (clean == "community" || clean == "free") {
        outEd = RswIdeEdition::Community;
        return true;
    }
    if (clean == "essentials" || clean == "essential" || clean == "tier1") {
        outEd = RswIdeEdition::Essentials;
        return true;
    }
    if (clean == "developer" || clean == "dev" || clean == "tier2") {
        outEd = RswIdeEdition::Developer;
        return true;
    }
    if (clean == "architect" || clean == "enterprise" || clean == "tier3") {
        outEd = RswIdeEdition::Architect;
        return true;
    }
    return false;
}

// --------------------------------------------------------------------------------
// GRANULAR EDITION CAPABILITY MATRIX PREDICATES
//   Note: Runtime WCAG 2.2 AA Accessibility, Dual-Layer Authenticode / Azure
//   Signing, `.MSM` consumption, `sfx` Self-Extractors, Universal `Setup.exe`,
//   and Major Upgrades are 100% included in EVERY edition (never paywalled!).
// --------------------------------------------------------------------------------

/// Baseline capabilities unlocked in ALL 4 editions (`Community`, `Essentials`, `Developer`, `Architect`).
inline constexpr bool EditionSupportsGuidedMsiAndUniversalExe(RswIdeEdition) noexcept { return true; }
inline constexpr bool EditionSupportsSelfExtractor(RswIdeEdition) noexcept            { return true; }
inline constexpr bool EditionSupportsConsumingMergeModules(RswIdeEdition) noexcept    { return true; }
inline constexpr bool EditionSupportsCodeSigning(RswIdeEdition) noexcept              { return true; }
inline constexpr bool EditionSupportsRuntimeAccessibility(RswIdeEdition) noexcept     { return true; }
inline constexpr bool EditionSupportsMajorUpgrades(RswIdeEdition) noexcept            { return true; }

/// Developer+ capabilities (`Community`, `Developer`, `Architect`; excluded in `Essentials`).
inline constexpr bool EditionSupportsCompilingMergeModules(RswIdeEdition ed) noexcept {
    return ed != RswIdeEdition::Essentials;
}
inline constexpr bool EditionSupportsNativeEngineAndScripting(RswIdeEdition ed) noexcept {
    return ed != RswIdeEdition::Essentials;
}
inline constexpr bool EditionSupportsCustomDialogAndSetupTypeDesign(RswIdeEdition ed) noexcept {
    return ed != RswIdeEdition::Essentials;
}
inline constexpr bool EditionSupportsMsiPrismEditor(RswIdeEdition ed) noexcept {
    return ed != RswIdeEdition::Essentials;
}
inline constexpr bool EditionSupportsRsUpdater(RswIdeEdition ed) noexcept {
    return ed != RswIdeEdition::Essentials;
}

/// Architect / Community-Parity capabilities (`Community` and `Architect`; excluded in `Essentials` & `Developer`).
inline constexpr bool EditionSupportsUnifiedSuites(RswIdeEdition ed) noexcept {
    return ed == RswIdeEdition::Community || ed == RswIdeEdition::Architect;
}
inline constexpr bool EditionSupportsMspPatchesAndMstTransforms(RswIdeEdition ed) noexcept {
    return ed == RswIdeEdition::Community || ed == RswIdeEdition::Architect;
}
inline constexpr bool EditionSupportsEnterpriseMediaAndPaks(RswIdeEdition ed) noexcept {
    return ed == RswIdeEdition::Community || ed == RswIdeEdition::Architect;
}
inline constexpr bool EditionSupportsAutoVpatExport(RswIdeEdition ed) noexcept {
    return ed == RswIdeEdition::Community || ed == RswIdeEdition::Architect;
}

/// Resolves `%LOCALAPPDATA%\RaggieSoft\Installer Studio\edition.json` for offline persistence.
inline std::filesystem::path GetEditionConfigFilePath() {
    PWSTR rawLocalAppData = nullptr;
    std::filesystem::path baseDir;
    if (SUCCEEDED(::SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &rawLocalAppData)) && rawLocalAppData) {
        baseDir = rawLocalAppData;
        ::CoTaskMemFree(rawLocalAppData);
    } else {
        wchar_t envBuf[MAX_PATH * 2]{};
        if (::GetEnvironmentVariableW(L"LOCALAPPDATA", envBuf, static_cast<DWORD>(std::size(envBuf))) > 0) {
            baseDir = envBuf;
        } else {
            std::error_code ec;
            baseDir = std::filesystem::temp_directory_path(ec);
        }
    }
    return baseDir / L"RaggieSoft" / L"Installer Studio" / L"edition.json";
}

/// Loads the locally persisted edition from `edition.json`, defaulting cleanly to `Community Edition`.
inline RswIdeEdition LoadPersistedEdition() noexcept {
    std::error_code ec;
    const auto cfgPath = GetEditionConfigFilePath();
    if (!std::filesystem::exists(cfgPath, ec)) {
        return RswIdeEdition::Community;
    }
    std::ifstream in(cfgPath, std::ios::binary);
    if (!in) return RswIdeEdition::Community;
    std::ostringstream ss;
    ss << in.rdbuf();
    const std::string txt = ss.str();

    // Lightweight zero-allocation scan for `"edition": "<slug>"` (valid strict JSON, zero comments!)
    const auto keyPos = txt.find("\"edition\"");
    if (keyPos == std::string::npos) return RswIdeEdition::Community;
    const auto colonPos = txt.find(':', keyPos + 9);
    if (colonPos == std::string::npos) return RswIdeEdition::Community;
    const auto q1 = txt.find('"', colonPos + 1);
    if (q1 == std::string::npos) return RswIdeEdition::Community;
    const auto q2 = txt.find('"', q1 + 1);
    if (q2 == std::string::npos) return RswIdeEdition::Community;

    RswIdeEdition parsed = RswIdeEdition::Community;
    if (ParseEditionToken(std::string_view(txt).substr(q1 + 1, q2 - q1 - 1), parsed)) {
        return parsed;
    }
    return RswIdeEdition::Community;
}

/// Saves the active edition to `%LOCALAPPDATA%\RaggieSoft\Installer Studio\edition.json` (strict JSON, zero comments).
inline bool SavePersistedEdition(RswIdeEdition ed) noexcept {
    std::error_code ec;
    const auto cfgPath = GetEditionConfigFilePath();
    std::filesystem::create_directories(cfgPath.parent_path(), ec);
    std::ofstream out(cfgPath, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "{\n"
        << "  \"product\": \"RaggieSoft Installer Studio\",\n"
        << "  \"edition\": \"" << EditionSlug(ed) << "\",\n"
        << "  \"architectParity\": " << (EditionHasFullArchitectParity(ed) ? "true" : "false") << "\n"
        << "}\n";
    return out.good();
}

/// Resolves the effective edition for CLI invocations:
///   1. Explicit `--edition=<slug>` flag (highest priority)
///   2. `RSW_EDITION` environment variable (CI/CD & automated test harnesses)
///   3. Locally saved `edition.json` (or `Community` default)
inline bool ResolveEffectiveCliEdition(
    std::string_view cliEditionFlag,
    RswIdeEdition& outEdition,
    std::string& outError)
{
    if (!cliEditionFlag.empty()) {
        if (!ParseEditionToken(cliEditionFlag, outEdition)) {
            outError = "Unknown --edition='" + std::string(cliEditionFlag) +
                       "'. Expected 'community', 'essentials', 'developer', or 'architect'.";
            return false;
        }
        return true;
    }
    wchar_t envBuf[64]{};
    const DWORD n = ::GetEnvironmentVariableW(L"RSW_EDITION", envBuf, static_cast<DWORD>(std::size(envBuf)));
    if (n > 0 && n < std::size(envBuf)) {
        const std::string envStr = WideToUtf8(envBuf);
        if (!ParseEditionToken(envStr, outEdition)) {
            outError = "Invalid RSW_EDITION='" + envStr +
                       "'. Expected 'community', 'essentials', 'developer', or 'architect'.";
            return false;
        }
        return true;
    }
    outEdition = LoadPersistedEdition();
    return true;
}

/// Builds a rich plain-English comparison summary for the IDE's `Compare Editions & Capabilities` dialog.
inline std::wstring BuildEditionsComparisonText(RswIdeEdition activeEd) {
    std::wostringstream oss;
    oss << L"Active IDE Edition: " << EditionDisplayName(activeEd) << L"\n\n"
        << L"• Community Edition (Free — Hobbyist, Solo, EDU, Non-Profit & OSS):\n"
        << L"  100% technical feature parity with Architect Edition forever — zero locked views, zero watermarks.\n\n"
        << L"• Essentials Edition (Commercial Tier 1 — 1 Workstation):\n"
        << L"  Guided .MSI + Universal Setup.exe (x64 & ARM64), Basic Folder/.ZIP Self-Extractor (%TEMP% wrapper),\n"
        << L"  11 Wizard Styles, Pre-MSI Runtimes, .MSM consumption, Major Upgrades, WCAG 2.2 AA & Code Signing.\n\n"
        << L"• Developer Edition (Commercial Tier 2 — Workstation + CI/CD Build Agents):\n"
        << L"  Adds compiling new .MSM Merge Modules, RaggieScript (.rssc) & RaggieScript EXE, RsUpdate.exe\n"
        << L"  (Launcher-compatible JSON updater), Custom C++ & MSI.DLL Dialog Designers, MSI Prism, & CI/CD rights.\n\n"
        << L"• Architect Edition (Commercial Tier 3 — Enterprise Suites & Procurement):\n"
        << L"  Adds Unified Multi-Product Suites (.rsis Dual-Bar Progress + ARPSYSTEMCOMPONENT adoption), RsPatch.exe\n"
        << L"  .MSP & .rswpatch binary-delta patches (--plan-update), .MST GPO transforms, Disk Spanning, Loose Media\n"
        << L"  (msiexec /a), .rswpak CDN packs, and automated HTML5 VPAT 2.5 conformance reports (--emit-vpat).";
    return oss.str();
}

} // namespace rsw
