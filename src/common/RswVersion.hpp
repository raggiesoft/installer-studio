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
//       - `InstallerStudio.exe` : 12-view Tree-First Visual IDE + 5 Guided AeroWizards
//       - `instbuild.exe`       : Headless CLI compiler, JSON linter, AI spec & VPAT emitter
//       - `SetupWizard.exe`     : White-label WCAG 2.2 AA runtime wizard ("RaggieSoft Setup Wizard")
//       - `msiprism.exe`        : Native 64-bit/ARM64 MSI, MSM, MST & MSP SQL inspector ("RaggieSoft MSI Prism")
//       - `RaggieSoftHelp.exe`  : Zero-dependency native C++20 offline Help Viewer (`.rshm.json` + Named Pipe F1 IPC)
//       - `insttests.exe`       : Automated C++20 verification suite
//
//   IDE Edition Architecture (`Community`, `Express`, `Professional`, `Premier`):
//     RaggieSoft Installer Studio defines four named product editions:
//       1. `Community`    — Free Community Edition with **100% technical feature
//                           parity with Premier** (zero locked views, zero disabled
//                           compilers, zero watermarks!).
//       2. `Express`      — Streamlined commercial license tier placeholder.
//       3. `Professional` — Mid-tier commercial license tier placeholder.
//       4. `Premier`      — Enterprise commercial license tier placeholder.
//     Keeping `Community == Premier` in capability ensures independent developers,
//     educators, open-source authors, and accessibility advocates never hit paywalls
//     for core MSI, Prerequisite, Native, or Scripting features!
//
//   Community Edition White-Label Attribution Rule:
//     `kGeneratorSigWide` (`"RaggieSoft Installer Studio (instbuild.exe 0.5.1)"`) is
//     written ONLY to the non-visible MSI `SummaryInformation` `PID_APPNAME`
//     (Creating Application) diagnostic field—never stamped as a visible watermark
//     onto customer dialogs or `Setup.exe` `VERSIONINFO`!
// ================================================================================

#pragma once

#include <string_view>

namespace rsw {

// Semantic Versioning (`0.6.0`)
inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 6;
inline constexpr int kVersionPatch = 0;

inline constexpr std::string_view  kVersionString      = "0.6.0";
inline constexpr std::wstring_view kVersionStringWide  = L"0.6.0";

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
inline constexpr std::string_view  kGeneratorSignature = "RaggieSoft Installer Studio (instbuild.exe 0.6.0)";
inline constexpr std::wstring_view kGeneratorSigWide   = L"RaggieSoft Installer Studio (instbuild.exe 0.6.0)";
inline constexpr std::string_view  kManifestSchemaVer  = "0.6";

/// IDE Product Editions (`Community` permanently maintains 100% feature parity with `Premier`!).
enum class RswIdeEdition : int {
    Community    = 0,
    Express      = 1,
    Professional = 2,
    Premier      = 3
};

/// Returns the consumer-friendly display badge for an IDE edition.
inline constexpr std::wstring_view EditionDisplayName(RswIdeEdition ed) noexcept {
    switch (ed) {
        case RswIdeEdition::Community:    return L"Community Edition";
        case RswIdeEdition::Express:      return L"Express Edition";
        case RswIdeEdition::Professional: return L"Professional Edition";
        case RswIdeEdition::Premier:      return L"Premier Edition";
    }
    return L"Community Edition";
}

/// Returns a plain-English summary of each edition's positioning.
inline constexpr std::wstring_view EditionSummary(RswIdeEdition ed) noexcept {
    switch (ed) {
        case RswIdeEdition::Community:
            return L"100% Feature Parity with Premier — Every MSI, Prerequisite, AeroWizard, VPAT 2.5, & CLI builder unlocked with zero watermarks.";
        case RswIdeEdition::Express:
            return L"Placeholder commercial license tier for rapid single-project packaging.";
        case RswIdeEdition::Professional:
            return L"Placeholder commercial license tier for multi-architecture studio teams.";
        case RswIdeEdition::Premier:
            return L"Placeholder enterprise commercial license tier with full suite parity.";
    }
    return L"100% Feature Parity with Premier.";
}

/// Enforces our core pledge: Community Edition always enjoys full Premier capability!
inline constexpr bool EditionHasFullPremierParity(RswIdeEdition ed) noexcept {
    return ed == RswIdeEdition::Community || ed == RswIdeEdition::Premier;
}

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
