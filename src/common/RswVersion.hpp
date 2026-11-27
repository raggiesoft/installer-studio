// ================================================================================
// FILE: src/common/RswVersion.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file?
//     This header holds the canonical version numbers, file extensions, IDE
//     Edition tiers, and embedded resource IDs shared across the entire
//     **RaggieSoft Installer Studio** native Win32 C++20 packaging suite:
//       - `InstallerStudio.exe` : 12-view Tree-First Visual IDE + 6 Guided AeroWizards
//       - `instbuild.exe`       : Headless CLI compiler, JSON linter, AI spec & VPAT emitter
//       - `SetupWizard.exe`     : White-label WCAG 2.2 AA runtime wizard ("RaggieSoft Setup Wizard")
//       - `msiprism.exe`        : Native 64-bit/ARM64 MSI, MSM, MST & MSP SQL inspector ("RaggieSoft MSI Prism")
//       - `RaggieSoftHelp.exe`  : Zero-dependency native C++20 offline Help Viewer (`.rshm.json` + Named Pipe F1 IPC)
//       - `insttests.exe`       : Automated C++20 verification suite
//
//   Official 4-Edition Architecture (`Community`, `Essentials`, `Developer`, `Architect`):
//     RaggieSoft Installer Studio defines four named product editions (documented in
//     `EDITIONS_AND_RAGGIESCRIPT.md` and `https://raggiesoftstudio.com/installer-studio/editions`):
//       1. `Community`  — Free for hobbyists, solo creators, open-source maintainers,
//                         students, educators, and non-profits; permanently maintains
//                         **100% technical feature parity with Architect Edition**
//                         (`Community == Architect` — zero locked views, zero watermarks!).
//       2. `Essentials` — Commercial Tier 1: Guided no-code `.MSI` + `Setup.exe` (`x64`,
//                         `ARM64`, Universal), Basic `.ZIP`/Folder Self-Extractor (`sfx`),
//                         100% WCAG 2.2 AA runtime accessibility & Authenticode/Azure Signing,
//                         Pre-MSI Runtimes, **consumes** `.MSM` Merge Modules (does not compile
//                         new `.MSM`s), and performs **Major Upgrades Only** (`1.0.0 -> 1.0.1`
//                         mints a fresh `ProductCode`; does not create `.MSP` patches).
//       3. `Developer`  — Commercial Tier 2: Adds `.MSM` Merge Module compilation,
//                         `RaggieScript` (`.rssc`) zero-MSI native scripting & full
//                         `Engine = native`, From-Scratch C++ & `MSI.DLL` Dialog Designers,
//                         `.slnx` / VB6 `.vbp` importers, `msiprism.exe`, `RsUpdate.exe`,
//                         `--emit-winget`, and commercial CI/CD build agent rights.
//       4. `Architect`  — Commercial Tier 3: Adds Multi-Product `.rsis` Unified Suites
//                         (`Showcase Studio` front-door + dynamic `ARPSYSTEMCOMPONENT`),
//                         `.MSP` binary-delta patches (`RsPatch.exe`) & `.MST` transforms,
//                         Multi-Volume Disk Spanning & `msiexec /a`, `.rswpak` CDN packs,
//                         Automated HTML5 VPAT 2.5 (`--emit-vpat`), and `.wsb` Repackager.
//
//   Community Edition White-Label Attribution Rule:
//     `kGeneratorSigWide` (`"RaggieSoft Installer Studio (instbuild.exe 0.6.0)"`) is
//     written ONLY to the non-visible MSI `SummaryInformation` `PID_APPNAME`
//     (Creating Application) diagnostic field—never stamped as a visible watermark
//     onto customer dialogs or `Setup.exe` `VERSIONINFO`!
// ================================================================================

#pragma once

#include <string_view>

namespace rsw {

// Semantic Versioning (`0.7.0`)
inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 7;
inline constexpr int kVersionPatch = 0;

inline constexpr std::string_view  kVersionString      = "0.7.0";
inline constexpr std::wstring_view kVersionStringWide  = L"0.7.0";

// Suite, Runtime Wizard, Help Viewer, & MSI Prism Product Identity
inline constexpr std::string_view  kSuiteTitle         = "RaggieSoft Installer Studio";
inline constexpr std::string_view  kProductTitle       = "RaggieSoft Installer Studio";
inline constexpr std::string_view  kWizardTitle        = "RaggieSoft Setup Wizard";
inline constexpr std::string_view  kPrismTitle         = "RaggieSoft MSI Prism";
inline constexpr std::string_view  kHelpTitle          = "RaggieSoft Help Viewer";
inline constexpr std::string_view  kLensTitle          = "RaggieSoft MSI Prism";
inline constexpr std::string_view  kProductShortCode   = "RSIP";
inline constexpr std::string_view  kProductTagline     = "Install Without Limits";
inline constexpr std::string_view  kCanonicalExt       = ".rsip";
inline constexpr std::string_view  kSuiteExt           = ".rsis";
inline constexpr std::string_view  kIncludeExt         = ".rsii";
inline constexpr std::string_view  kScriptExt          = ".rssc";
inline constexpr std::string_view  kPakExt             = ".rswpak";
inline constexpr std::string_view  kGeneratorSignature = "RaggieSoft Installer Studio (instbuild.exe 0.7.0)";
inline constexpr std::wstring_view kGeneratorSigWide   = L"RaggieSoft Installer Studio (instbuild.exe 0.7.0)";
inline constexpr std::string_view  kManifestSchemaVer  = "0.7";

/// Official IDE Product Editions (`Community` permanently maintains 100% feature parity with `Architect`!).
enum class RswIdeEdition : int {
    Community  = 0, // Free for hobbyists, solo creators, EDU & OSS — 100% Architect feature parity!
    Essentials = 1, // Commercial Tier 1: Guided MSI + Setup.exe, SFX, Runtimes, consume .MSM, Major Upgrades only
    Developer  = 2, // Commercial Tier 2: + Compile .MSM, RaggieScript (.rssc), Dialog Designers, Importers & CI/CD
    Architect  = 3  // Commercial Tier 3: + Unified .rsis Suites, .MSP/.MST deltas, Spanning/Admin & Auto-VPAT 2.5
};

/// Returns the human-readable display badge for each official product edition.
inline constexpr std::wstring_view EditionDisplayName(RswIdeEdition ed) noexcept {
    switch (ed) {
        case RswIdeEdition::Community:  return L"Community Edition";
        case RswIdeEdition::Essentials: return L"Essentials Edition";
        case RswIdeEdition::Developer:  return L"Developer Edition";
        case RswIdeEdition::Architect:  return L"Architect Edition";
    }
    return L"Community Edition";
}

/// Returns a plain-English summary of each edition's exact capabilities and licensing scope.
inline constexpr std::wstring_view EditionSummary(RswIdeEdition ed) noexcept {
    switch (ed) {
        case RswIdeEdition::Community:
            return L"Free for hobbyists, solo creators, educators & open source — 100% technical feature parity with Architect Edition (zero locked views, zero watermarks).";
        case RswIdeEdition::Essentials:
            return L"Commercial Tier 1 — Guided .MSI + Universal Setup.exe, Basic .ZIP/Folder SFX (%TEMP% wrapper), WCAG 2.2 AA UI, Signing, Pre-MSI Runtimes, .MSM consumption, & Major Upgrades.";
        case RswIdeEdition::Developer:
            return L"Commercial Tier 2 — Everything in Essentials + compile .MSM Merge Modules, RaggieScript (.rssc) zero-MSI native scripting, Custom Dialog Designers, VS/VB6 Importers, msiprism, & CI/CD agents.";
        case RswIdeEdition::Architect:
            return L"Commercial Tier 3 — Everything in Developer + Multi-Product .rsis Suites (Showcase front-door), .MSP binary-delta patches & .MST transforms, Disk Spanning/msiexec /a, & HTML5 VPAT 2.5.";
    }
    return L"100% Feature Parity with Architect Edition.";
}

/// Enforces our core pledge: Community Edition permanently enjoys 100% Architect capability!
inline constexpr bool EditionHasFullArchitectParity(RswIdeEdition ed) noexcept {
    return ed == RswIdeEdition::Community || ed == RswIdeEdition::Architect;
}

/// Returns true when an edition can compile new `.MSM` Merge Modules (`Community`, `Developer`, `Architect`).
/// Note: ALL editions (including `Essentials`) can consume existing `.MSM` Merge Modules via `[MergeModule.*]`!
inline constexpr bool EditionCanCompileMergeModules(RswIdeEdition ed) noexcept {
    return ed != RswIdeEdition::Essentials;
}

/// Returns true when an edition unlocks `RaggieScript` (`.rssc`), custom dialog designers, and CI/CD agents.
inline constexpr bool EditionIncludesRaggieScript(RswIdeEdition ed) noexcept {
    return ed != RswIdeEdition::Essentials;
}

/// Returns true when an edition includes `.MSP` binary-delta patching, `.rsis` Suites, and `--emit-vpat`.
inline constexpr bool EditionIncludesMspAndSuites(RswIdeEdition ed) noexcept {
    return ed == RswIdeEdition::Community || ed == RswIdeEdition::Architect;
}

inline constexpr int kResIdScriptBytecode      = 225; // Compiled `RSWBC1` RaggieScript (`.rssc`) bytecode stream (`v0.7.0`)

// Embedded Win32 RCDATA resource IDs inside `SetupWizard.exe` -> `<Product>-setup.exe`
inline constexpr int kResIdEmbeddedMsi         = 201; // Full compiled `Product.msi` stream
inline constexpr int kResIdBrandingIni         = 202; // UTF-8 `[Branding]` manifest
inline constexpr int kResIdLicenseRtf          = 203; // RTF / plain-text software license
inline constexpr int kResIdArtworkPng          = 204; // Optional cover / 16:9 banner PNG
inline constexpr int kResIdPrereqManifest      = 205; // UTF-8 serialized `[Prerequisite.*]` runtime table
inline constexpr int kResIdUniversalManifest   = 210; // UTF-8 `[Universal]` CPU-dispatch manifest (`Mode=offline|web`)
inline constexpr int kResIdUniversalSetupX64   = 211; // LZMS-compressed native `<Product>-x64-setup.exe` (Offline Universal)
inline constexpr int kResIdUniversalSetupArm64 = 212; // LZMS-compressed native `<Product>-arm64-setup.exe` (Offline Universal)
inline constexpr int kResIdFeaturePackManifest = 215; // UTF-8 `[FeaturePacks]` table for external `.rswpak` on-demand blocks
inline constexpr int kResIdSfxOrNativeArchive  = 220; // Solid `RSWPAK1` LZMS payload stream (`PackageKind = sfx` & `Engine = native`)
inline constexpr int kResIdNativeManifest      = 221; // UTF-8 `[NativeInstall]` / `[SFX]` execution & shortcut/registry manifest
inline constexpr int kResIdSuiteManifest       = 230; // UTF-8 `[SuiteChainer]` multi-package manifest (`PackageKind = suite` / `.rsis`)
inline constexpr int kResIdPrereqPayloadBase   = 300; // `300 + idx`: Embedded offline runtime binaries!
inline constexpr int kResIdSuiteChildBase      = 400; // `400 + idx`: Embedded LZMS child `.msi` or `-setup.exe` binaries in Suite Setup!

} // namespace rsw
