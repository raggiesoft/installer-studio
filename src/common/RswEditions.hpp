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
//     headless command-line compiler (`instbuild.exe --license-file=<file.rswlic>`):
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
//   Zero-Telemetry Cryptographic `.rswlic` Licensing (`RswLicensing.hpp`):
//     - **No `.rswlic` license file present == `Community Edition`** (`RswIdeEdition::Community`).
//     - Commercial tiers (`Essentials`, `Developer`, `Architect`, and All-Access
//       `RaggieSoft Studio` Suite keys) are verified 100% offline in `< 0.2ms` via
//       Windows `System32\bcrypt.dll` (`BCRYPT_ECDSA_P256_ALGORITHM`) across
//       `--license-file=<path.rswlic>`, `RSW_LICENSE_KEY`, `<ExeDir>\`,
//       `%LOCALAPPDATA%\RaggieSoft\Licenses\`, and `%PROGRAMDATA%\RaggieSoft\Licenses\`.
//     - Zero backdoor edition-switcher menus, zero plaintext `edition.json`, zero
//       activation servers, and zero hardware/motherboard fingerprinting!
// ================================================================================

#pragma once

#include "RswLicensing.hpp"
#include "RswPlatform.hpp"
#include "RswVersion.hpp"

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

/// Builds a rich plain-English license & edition status summary for the IDE's `Help -> License & Edition Status...` dialog.
inline std::wstring BuildLicenseStatusSummaryText(const RswLicenseStatus& status) {
    std::wostringstream oss;
    oss << L"Active Edition: " << EditionDisplayName(status.edition) << L"\n";
    if (status.isCommercialLicensed) {
        oss << L"License State : Cryptographically Verified Commercial License (ECDSA P-256)\n"
            << L"Licensed To   : " << Utf8ToWide(status.cert.licensee)
            << (status.cert.email.empty() ? L"" : (L" <" + Utf8ToWide(status.cert.email) + L">")) << L"\n"
            << L"License ID    : " << Utf8ToWide(status.cert.licenseId) << L"\n"
            << L"Product Scope : " << Utf8ToWide(status.cert.product) << L" (" << status.cert.seats << L" seat(s))\n"
            << L"Issued Date   : " << Utf8ToWide(status.cert.issuedDate)
            << (status.cert.maintenanceThru.empty() ? L"" : (L"  •  Maintenance Thru: " + Utf8ToWide(status.cert.maintenanceThru))) << L"\n"
            << L"License File  : " << Utf8ToWide(status.sourceDescription) << L"\n\n"
            << L"Zero-Telemetry Guarantee: Verified 100% offline via Windows System32\\bcrypt.dll with zero activation servers and zero hardware fingerprinting.";
    } else {
        oss << L"License State : No .rswlic commercial license file found — running Community Edition\n\n"
            << L"Community Edition Eligibility & Full Architect Parity:\n"
            << L"  Free for hobbyists, solo creators, students, educators, non-profits, and open-source maintainers.\n"
            << L"  Permanently maintains 100% technical feature parity with Architect Edition with zero watermarks.\n\n"
            << L"Have a Commercial License (.rswlic)?\n"
            << L"  • Drag and drop your .rswlic file directly onto this window, OR\n"
            << L"  • Choose Help -> Activate / Import License File (.rswlic)..., OR\n"
            << L"  • Place installer-studio.rswlic in %PROGRAMDATA%\\RaggieSoft\\Licenses\\ (or set RSW_LICENSE_KEY in CI/CD).";
    }
    return oss.str();
}

/// Builds a rich plain-English comparison summary for the IDE's `Compare All 4 Editions & Capabilities` dialog.
inline std::wstring BuildEditionsComparisonText(RswIdeEdition activeEd) {
    std::wostringstream oss;
    oss << L"Active IDE Edition: " << EditionDisplayName(activeEd) << L"\n\n"
        << L"• Community Edition (Presented Automatically When No .rswlic File Is Present — Free):\n"
        << L"  100% technical feature parity with Architect Edition forever — zero locked views, zero watermarks.\n\n"
        << L"• Essentials Edition (.rswlic Commercial Tier 1 — 1 Workstation):\n"
        << L"  Guided .MSI + Universal Setup.exe (x64 & ARM64), Basic Folder/.ZIP Self-Extractor (%TEMP% wrapper),\n"
        << L"  11 Wizard Styles, Pre-MSI Runtimes, .MSM consumption, Major Upgrades, WCAG 2.2 AA & Code Signing.\n\n"
        << L"• Developer Edition (.rswlic Commercial Tier 2 — Workstation + CI/CD Build Agents):\n"
        << L"  Adds compiling new .MSM Merge Modules, RaggieScript (.rssc) & RaggieScript EXE, RsUpdate.exe\n"
        << L"  (Launcher-compatible JSON updater), Custom C++ & MSI.DLL Dialog Designers, MSI Prism, & CI/CD rights.\n\n"
        << L"• Architect Edition (.rswlic Commercial Tier 3 — Enterprise Suites & Procurement):\n"
        << L"  Adds Unified Multi-Product Suites (.rsis Dual-Bar Progress + ARPSYSTEMCOMPONENT adoption), RsPatch.exe\n"
        << L"  .MSP & .rswpatch binary-delta patches (--plan-update), .MST GPO transforms, Disk Spanning, Loose Media\n"
        << L"  (msiexec /a), .rswpak CDN packs, and automated HTML5 VPAT 2.5 conformance reports (--emit-vpat).";
    return oss.str();
}

} // namespace rsw
