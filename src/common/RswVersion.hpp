// ================================================================================
// FILE: src/common/RswVersion.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file?
//     This header holds the canonical version numbers, file extensions, and
//     product identity constants shared across the entire **RaggieSoft Installer
//     Studio** native Win32 C++20 packaging suite:
//       - `InstallerStudio.exe` : 7-view Visual IDE for `.rsip` projects & `.rsis` suites
//       - `instbuild.exe`       : Headless CLI compiler, JSON linter, AI spec & VPAT emitter
//       - `SetupWizard.exe`     : White-label WCAG 2.2 AA runtime wizard ("RaggieSoft Setup Wizard")
//       - `msiprism.exe`        : Native 64-bit/ARM64 MSI, MSM, MST & MSP SQL inspector ("RaggieSoft MSI Prism")
//       - `insttests.exe`       : Automated C++20 verification suite
//
//   Hybrid Project Architecture (`.rsip` + `.rsis` + `.rsii`):
//     - `.rsip` (RaggieSoft Installer Project) : Standalone single-file project (like `.iss` / `.csproj`)
//     - `.rsis` (RaggieSoft Installer Suite)   : Optional multi-project workspace (like `.slnx`)
//     - `.rsii` (RaggieSoft Installer Include) : Optional `@include` shared header snippet
//
//   Community Edition White-Label Attribution Rule:
//     `kGeneratorSigWide` (`"RaggieSoft Installer Studio (instbuild.exe 0.1.0)"`) is
//     written ONLY to the non-visible MSI `SummaryInformation` `PID_APPNAME`
//     (Creating Application) diagnostic field—never stamped as a visible watermark
//     onto customer dialogs or `Setup.exe` `VERSIONINFO`!
// ================================================================================

#pragma once

#include <string_view>

namespace rsw {

// Semantic Versioning (`0.1.0`)
inline constexpr int kVersionMajor = 0;
inline constexpr int kVersionMinor = 1;
inline constexpr int kVersionPatch = 0;

inline constexpr std::string_view  kVersionString      = "0.1.0";
inline constexpr std::wstring_view kVersionStringWide  = L"0.1.0";

// Suite, Runtime Wizard, & MSI Prism Product Identity
inline constexpr std::string_view  kSuiteTitle         = "RaggieSoft Installer Studio";
inline constexpr std::string_view  kProductTitle       = "RaggieSoft Installer Studio";
inline constexpr std::string_view  kWizardTitle        = "RaggieSoft Setup Wizard";
inline constexpr std::string_view  kPrismTitle         = "RaggieSoft MSI Prism";
inline constexpr std::string_view  kLensTitle          = "RaggieSoft MSI Prism";
inline constexpr std::string_view  kProductShortCode   = "RSIP";
inline constexpr std::string_view  kProductTagline     = "Install Without Limits";
inline constexpr std::string_view  kCanonicalExt       = ".rsip";
inline constexpr std::string_view  kSuiteExt           = ".rsis";
inline constexpr std::string_view  kIncludeExt         = ".rsii";
inline constexpr std::string_view  kGeneratorSignature = "RaggieSoft Installer Studio (instbuild.exe 0.1.0)";
inline constexpr std::wstring_view kGeneratorSigWide   = L"RaggieSoft Installer Studio (instbuild.exe 0.1.0)";
inline constexpr std::string_view  kManifestSchemaVer  = "0.2";

// Embedded Win32 RCDATA resource IDs inside `SetupWizard.exe` -> `<Product>-setup.exe`
inline constexpr int kResIdEmbeddedMsi     = 201;
inline constexpr int kResIdBrandingIni     = 202;
inline constexpr int kResIdLicenseRtf      = 203;
inline constexpr int kResIdArtworkPng      = 204;

} // namespace rsw
