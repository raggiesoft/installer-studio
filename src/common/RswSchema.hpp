// ================================================================================
// FILE: src/common/RswSchema.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file?
//     This file is the **Single Source of Truth Grammar (`constexpr` Descriptor Table)**
//     for `.rsip` (RaggieSoft Installer Project) manifests!
//
//   Why put the entire manifest schema into a `constexpr` C++ table instead of
//   hand-writing separate validator `if` chains and Markdown docs?
//     In C++20, `constexpr` data lives directly in read-only compile-time memory
//     with zero startup cost. By defining every valid `.rsip` section, key name,
//     value type, default value, allowed enum choices, and plain-English explanation
//     right here in `kSectionDescriptors` and `kFieldDescriptors`, THREE subsystems
//     stay 100% synchronized forever:
//       1. `RswValidator.hpp` (`instbuild.exe --validate --diagnostics=json`):
//          Flags unknown sections, typos in key names, missing required keys, bad
//          GUIDs, invalid MSI `Major.Minor.Build` versions, and missing `&` keyboard
//          accelerators in under 1 millisecond!
//       2. `RswAiSpecEmitter.hpp` (`instbuild.exe --emit-ai-spec <dir>`):
//          Deterministically generates `llms.txt`, `llms-full.txt`, and `SKILL.md`
//          straight from this compiled binary—so AI docs can NEVER drift from the
//          actual parser!
//       3. `InstallerStudio.exe` (Native Win32 C++ Visual IDE):
//          Iterates over `kFieldDescriptors` to inspect, validate, and edit `.rsip`
//          manifests without stripping a single comment or `@include` directive!
// ================================================================================

#pragma once

#include <array>
#include <string_view>

namespace rsw {

/// Distinguishes singleton sections (`[Package]`) from repeatable named sections (`[Payload.App]`).
enum class SectionCardinality {
    SingletonRequired, // Must appear once (e.g., `[Package]`, `[Install]`)
    SingletonOptional, // May appear 0 or 1 times (e.g., `[Requirements]`, `[UI]`, `[Signing]`)
    NamedRepeatable    // Appears as `[Prefix.<id>]` 0 or more times (`[Feature.Main]`, `[Payload.Core]`, etc.)
};

/// Value data types checked by `RswValidator` before `cabinet.dll` or `msi.dll` ever run.
enum class FieldValueKind {
    String,            // Arbitrary UTF-8 text
    AcceleratorString, // Interactive UI label that MUST contain a Windows `&` mnemonic (WCAG / Section 508)
    MsiVersion,        // 3-part `Major.Minor.Build` (`0..255 . 0..255 . 0..65535`)
    Guid,              // Uppercase or lowercase `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}`
    GuidOrAuto,        // `*` (auto-generate per build) OR explicit `{GUID}`
    Integer,           // Base-10 integer
    Boolean,           // `true` / `false` / `yes` / `no` / `1` / `0`
    EnumChoice,        // Pipe-delimited allowed tokens in `allowedValues` (e.g. `"x64|arm64"`)
    Path,              // Local build-machine file or directory path (supports `$(ARCH)`)
    RelativePath       // Relative target directory/file path inside the installed image
};

struct SectionDescriptor {
    std::string_view   nameOrPrefix; // Exact name (`"Package"`) or prefix (`"Payload"` for `[Payload.<id>]`)
    SectionCardinality cardinality;
    bool               atLeastOneRequired;
    std::string_view   summary;
};

struct FieldDescriptor {
    std::string_view sectionPrefix;  // Matches `SectionDescriptor::nameOrPrefix`
    std::string_view key;            // Exact key name (case-insensitive lookup, canonical PascalCase display)
    FieldValueKind   kind;
    bool             required;
    std::string_view defaultValue;
    std::string_view allowedValues;  // Used when `kind == FieldValueKind::EnumChoice`
    std::string_view description;
};

// ================================================================================
// 1. CANONICAL `.RSW` SECTION DESCRIPTOR TABLE
// ================================================================================
inline constexpr std::array<SectionDescriptor, 9> kSectionDescriptors{{
    {"Package",      SectionCardinality::SingletonRequired, true,
     "Core product identity, MSI Summary Information, target CPU architecture (x64/arm64), and output settings."},
    {"Requirements", SectionCardinality::SingletonOptional, false,
     "Minimum Windows 11 build gate (RtlGetVersion & MSI LaunchCondition) and downgrade protection."},
    {"Install",      SectionCardinality::SingletonRequired, true,
     "Default installation subfolder under ProgramFiles64Folder/LocalAppDataFolder and Start Menu group."},
    {"UI",           SectionCardinality::SingletonOptional, false,
     "Branding, RTF license, icon, and accessible & accelerator labels for Wizard 97 MSI and rswsetup.exe."},
    {"Signing",      SectionCardinality::SingletonOptional, false,
     "Optional dual-layer Authenticode code signing (Product.msi first, then outer Setup.exe) via signtool.exe."},
    {"Feature",      SectionCardinality::NamedRepeatable,   false,
     "Named MSI Feature definition ([Feature.<id>]); defaults to a single 'Main' feature if omitted."},
    {"Payload",      SectionCardinality::NamedRepeatable,   true,
     "Maps a build-machine file or directory ([Payload.<id>]) into #payload.cab and deterministic MSI Components."},
    {"Shortcut",     SectionCardinality::NamedRepeatable,   false,
     "Creates an advertised-free Start Menu or Desktop shortcut ([Shortcut.<id>]) with ICE64 RemoveFolder cleanup."},
    {"Registry",     SectionCardinality::NamedRepeatable,   false,
     "Writes an HKLM/HKCU/HKCR registry value ([Registry.<id>]) tied to a deterministic 64-bit MSI Component."}
}};

// ================================================================================
// 2. CANONICAL `.RSW` FIELD DESCRIPTOR TABLE
// ================================================================================
inline constexpr std::array<FieldDescriptor, 45> kFieldDescriptors{{
    // --- [Package] ---
    {"Package", "SchemaVersion",         FieldValueKind::String,      false, "0.1",               "",
     "Manifest grammar version (`0.1`)."},
    {"Package", "Name",                  FieldValueKind::String,      true,  "",                  "",
     "Consumer-facing ProductName written to MSI Property table and VERSIONINFO."},
    {"Package", "Version",               FieldValueKind::MsiVersion,  true,  "",                  "",
     "Three-part Windows Installer version (`Major.Minor.Build`, max `255.255.65535`)."},
    {"Package", "Manufacturer",          FieldValueKind::String,      true,  "",                  "",
     "Publisher name written to MSI `Manufacturer` property and VERSIONINFO `CompanyName`."},
    {"Package", "UpgradeCode",           FieldValueKind::Guid,        true,  "",                  "",
     "Permanent `{GUID}` shared across all releases of this product for Major Upgrades and UUIDv5 seeding."},
    {"Package", "ProductCode",           FieldValueKind::GuidOrAuto,  false, "*",                 "",
     "`*` generates a fresh ProductCode GUID on each build, or specify an explicit `{GUID}`."},
    {"Package", "Description",           FieldValueKind::String,      false, "",                  "",
     "Summary Information description and Setup.exe `FileDescription`."},
    {"Package", "Language",              FieldValueKind::Integer,     false, "1033",              "",
     "Numeric Windows LCID (`1033` = English - United States)."},
    {"Package", "Architecture",          FieldValueKind::EnumChoice,  false, "arm64",             "x64|arm64",
     "Target silicon (`x64` -> `x64;1033`, `arm64` -> `Arm64;1033`). Overridable via `--arch=<x64|arm64>`."},
    {"Package", "Scope",                 FieldValueKind::EnumChoice,  false, "machine",           "machine|user",
     "`machine` (`ALLUSERS=1`, `ProgramFiles64Folder`) or `user` (per-user `LocalAppDataFolder`, zero UAC)."},
    {"Package", "OutputName",            FieldValueKind::String,      false, "",                  "",
     "Base filename stem (supports `$(ARCH)`); defaults to `<kebab-name>-$(ARCH)`."},
    {"Package", "OutputDir",             FieldValueKind::Path,        false, "out",               "",
     "Output folder for `<OutputName>.msi` and `<OutputName>-setup.exe` (relative to `.rsw` folder or absolute)."},
    {"Package", "BuildBootstrapper",     FieldValueKind::Boolean,     false, "true",              "",
     "When `true`, emits branded `<OutputName>-setup.exe` embedding `Product.msi` alongside the standalone `.msi`."},
    {"Package", "CabinetCompression",    FieldValueKind::EnumChoice,  false, "lzx",               "lzx|mszip",
     "Compression algorithm for embedded `#payload.cab` (`lzx` = high-ratio LZX:21, `mszip` = fast DEFLATE)."},
    {"Package", "HideFromInstalledApps", FieldValueKind::Boolean,     false, "false",             "",
     "Sets `ARPSYSTEMCOMPONENT=1` so content micro-installers do not clutter Windows Installed Apps."},
    {"Package", "AboutUrl",              FieldValueKind::String,      false, "",                  "",
     "Optional support/product website URL written to `ARPURLINFOABOUT`."},

    // --- [Requirements] ---
    {"Requirements", "MinWindowsBuild",  FieldValueKind::Integer,     false, "22000",             "",
     "Minimum Windows 10/11 build number (`22000` = Windows 11 21H2+, matching .NET 10 Desktop Runtime)."},
    {"Requirements", "OsLaunchMessage",  FieldValueKind::String,      false,
     "This application requires Windows 11 (Build 22000) or later.", "",
     "Friendly message shown by LaunchCondition / rswsetup.exe when the host OS is older than MinWindowsBuild."},
    {"Requirements", "BlockDowngrades",  FieldValueKind::Boolean,     false, "true",              "",
     "Adds a newer-version Upgrade row + LaunchCondition preventing accidental downgrades."},

    // --- [Install] ---
    {"Install", "DefaultSubdir",         FieldValueKind::RelativePath,true,  "",                  "",
     "Default target folder path under `ProgramFiles64Folder` (or `LocalAppDataFolder`), e.g. `MyCompany\\MyApp`."},
    {"Install", "StartMenuFolder",       FieldValueKind::RelativePath,false, "",                  "",
     "Program Menu subfolder name created when `[Shortcut.*]` targets `StartMenu` (defaults to `[Package] Manufacturer`)."},
    {"Install", "MainExecutable",        FieldValueKind::RelativePath,false, "",                  "",
     "Optional primary `.exe` relative to `INSTALLDIR` (used for `ARPPRODUCTICON` default & Finish launch)."},

    // --- [UI] ---
    {"UI", "LicenseRtf",                 FieldValueKind::Path,        false, "",                  "",
     "Optional `.rtf` license file displayed in Wizard 97 `LicenseAgreementDlg` and `rswsetup.exe`."},
    {"UI", "IconFile",                   FieldValueKind::Path,        false, "",                  "",
     "Optional `.ico` file embedded into MSI `Icon` table (`ARPPRODUCTICON`) and `Setup.exe` resources."},
    {"UI", "BannerTitle",                FieldValueKind::String,      false, "",                  "",
     "Optional subtitle displayed in the wizard header bar."},
    {"UI", "WelcomeHeadline",            FieldValueKind::String,      false, "",                  "",
     "Optional custom headline on the Welcome screen."},
    {"UI", "InstallButtonText",          FieldValueKind::AcceleratorString, false, "&Install",    "",
     "Accessible primary install button caption; MUST include a `&` keyboard mnemonic."},
    {"UI", "FinishLaunchCheckbox",       FieldValueKind::AcceleratorString, false,
     "&Launch application when setup finishes", "",
     "Accessible finish-screen launch checkbox caption; MUST include a `&` keyboard mnemonic."},
    {"UI", "OfferLaunchOnFinish",        FieldValueKind::Boolean,     false, "true",              "",
     "Shows the optional launch checkbox on the completion screen when `MainExecutable` is set."},

    // --- [Signing] ---
    {"Signing", "Enabled",               FieldValueKind::Boolean,     false, "false",             "",
     "Signs `Product.msi` first, embeds it into `Setup.exe`, and signs `Setup.exe` via `signtool.exe`."},
    {"Signing", "Thumbprint",            FieldValueKind::String,      false, "",                  "",
     "Optional SHA-1 certificate thumbprint in the local Windows Certificate Store."},
    {"Signing", "TimestampUrl",          FieldValueKind::String,      false,
     "http://timestamp.acs.microsoft.com", "",
     "RFC 3161 Authenticode timestamp server URL."},
    {"Signing", "DlibPath",              FieldValueKind::Path,        false, "",                  "",
     "Optional path to Azure Artifact Signing `Azure.CodeSigning.Dlib.dll`."},
    {"Signing", "MetadataJson",          FieldValueKind::Path,        false, "",                  "",
     "Optional path to Azure Artifact Signing metadata `.json` file."},

    // --- [Feature.<id>] ---
    {"Feature", "Title",                 FieldValueKind::String,      true,  "",                  "",
     "Consumer-facing title of this MSI Feature."},
    {"Feature", "Description",           FieldValueKind::String,      false, "",                  "",
     "Plain-English description of this MSI Feature."},
    {"Feature", "Level",                 FieldValueKind::Integer,     false, "1",                 "",
     "MSI InstallLevel (`1` = installed by default, `0` = hidden/disabled)."},

    // --- [Payload.<id>] ---
    {"Payload", "Source",                FieldValueKind::Path,        true,  "",                  "",
     "Source file or directory on the build machine (supports `$(ARCH)` expansion)."},
    {"Payload", "TargetSubdir",          FieldValueKind::RelativePath,false, "",                  "",
     "Optional destination subfolder inside `INSTALLDIR` (`\"\"` = root of `INSTALLDIR`)."},
    {"Payload", "Recursive",             FieldValueKind::Boolean,     false, "true",              "",
     "Recursively includes subdirectories when `Source` points to a directory."},
    {"Payload", "Feature",               FieldValueKind::String,      false, "Main",              "",
     "Feature ID owning the generated file component(s)."},

    // --- [Shortcut.<id>] ---
    {"Shortcut", "Name",                 FieldValueKind::String,      true,  "",                  "",
     "Display name of the shortcut in Start Menu or Desktop."},
    {"Shortcut", "TargetFile",           FieldValueKind::RelativePath,true,  "",                  "",
     "Relative path under `INSTALLDIR` to the target executable packaged by `[Payload.*]`."},
    {"Shortcut", "Location",             FieldValueKind::EnumChoice,  false, "StartMenu",         "StartMenu|Desktop",
     "Where to place the shortcut (`StartMenu` or `Desktop`)."},
    {"Shortcut", "Description",          FieldValueKind::String,      false, "",                  "",
     "Accessible screen-reader/tooltip description for the shortcut."}
}};

// Additional optional fields on `[Package]`, `[UI]`, `[Shortcut.<id>]`, and `[Registry.<id>]`
// kept in a companion constexpr array so both arrays stay compact and crystal-clear to read!
inline constexpr std::array<FieldDescriptor, 21> kExtendedFieldDescriptors{{
    {"Package",  "PackageKind",          FieldValueKind::EnumChoice,  false, "msi",               "msi|msm",
     "`msi` builds a standalone Enterprise `.msi` (+ optional `-setup.exe`); `msm` builds a reusable Merge Module (`.msm`)."},
    {"Package",  "Tagline",              FieldValueKind::String,      false, "",                  "",
     "Short subtitle displayed beneath the product title in `SetupWizard.exe` and `InstallerStudio.exe`."},
    {"Package",  "WebsiteLabel",         FieldValueKind::String,      false, "",                  "",
     "Optional domain badge shown in the setup wizard footer (left blank if omitted — 100% white-label!)."},
    {"UI",       "WizardLayout",         FieldValueKind::EnumChoice,  false, "standard",          "standard|autorun16x9",
     "`standard` shows the full RaggieSoft Glass Express/Compact/Custom Setup Wizard; `autorun16x9` shows the 640x480 16:9 widescreen Autorun micro-installer."},
    {"UI",       "AccentPrimaryHex",     FieldValueKind::String,      false, "#2563EB",           "",
     "Primary header accent hex color (e.g. `#2563EB` Cobalt, `#38BDF8` Sky Cyan, `#14B8A6` Teal)."},
    {"UI",       "AccentSecondaryHex",   FieldValueKind::String,      false, "#FBBF24",           "",
     "Secondary gradient accent hex color (e.g. `#FBBF24` Warm Gold, `#10B981` Emerald, `#EC4899` Rose)."},
    {"UI",       "ArtworkPng",           FieldValueKind::Path,        false, "",                  "",
     "Optional `.png` 16:9 banner or square product/album/book cover rendered inside `SetupWizard.exe`."},
    {"UI",       "WelcomeIntro",         FieldValueKind::String,      false, "",                  "",
     "Optional introductory paragraph displayed beneath the Hero Brand Card on the Welcome page of `SetupWizard.exe`."},
    {"UI",       "ReadMeText",           FieldValueKind::String,      false, "",                  "",
     "Optional edition liner notes or ReadMe summary displayed in the ReadMe view of `autorun16x9` installers."},
    {"UI",       "ExpressDescription",   FieldValueKind::String,      false,
     "One-click setup into the default installation folder with recommended shortcuts. Skips extra prompts.", "",
     "Helper caption for the Express Setup mode card in `SetupWizard.exe`."},
    {"UI",       "CompactDescription",   FieldValueKind::String,      false,
     "Installs essential application files with minimal disk footprint using recommended folder defaults.", "",
     "Helper caption for the Compact Setup mode card in `SetupWizard.exe`."},
    {"UI",       "CustomDescription",    FieldValueKind::String,      false,
     "Choose a custom destination folder and review installation options step by step before installing.", "",
     "Helper caption for the Custom Setup mode card in `SetupWizard.exe`."},
    {"UI",       "AuthorOrArtist",       FieldValueKind::String,      false, "",                  "",
     "Optional author or band credit badge displayed in `autorun16x9` book/album micro-installers."},
    {"Shortcut", "Arguments",            FieldValueKind::String,      false, "",                  "",
     "Optional command-line arguments passed when the shortcut launches `TargetFile`."},
    {"Shortcut", "Feature",              FieldValueKind::String,      false, "Main",              "",
     "Feature ID owning this shortcut."},
    {"Registry", "Root",                 FieldValueKind::EnumChoice,  false, "HKLM",              "HKLM|HKCU|HKCR",
     "Registry hive (`HKLM` for machine installs, `HKCU` for per-user installs, `HKCR` for classes)."},
    {"Registry", "Key",                  FieldValueKind::String,      true,  "",                  "",
     "Registry key path without hive prefix, e.g. `Software\\RaggieSoft\\MyApp`."},
    {"Registry", "Name",                 FieldValueKind::String,      true,  "",                  "",
     "Registry value name (use `@` for the key's default value)."},
    {"Registry", "Value",                FieldValueKind::String,      true,  "",                  "",
     "Registry value data (supports MSI formatted tokens like `[INSTALLDIR]` and `[ProductVersion]`)."},
    {"Registry", "Type",                 FieldValueKind::EnumChoice,  false, "string",            "string|expand_string|dword",
     "Registry value type (`string` = REG_SZ, `expand_string` = REG_EXPAND_SZ, `dword` = REG_DWORD)."},
    {"Registry", "Feature",              FieldValueKind::String,      false, "Main",              "",
     "Feature ID owning this registry value."}
}};

inline constexpr std::string_view ValueKindToDisplayName(FieldValueKind kind) noexcept {
    switch (kind) {
        case FieldValueKind::String:            return "string";
        case FieldValueKind::AcceleratorString: return "accel_string (must include '&')";
        case FieldValueKind::MsiVersion:        return "msi_version (Major.Minor.Build)";
        case FieldValueKind::Guid:              return "guid ({XXXXXXXX-...})";
        case FieldValueKind::GuidOrAuto:        return "guid_or_auto ('*' or {GUID})";
        case FieldValueKind::Integer:           return "integer";
        case FieldValueKind::Boolean:           return "bool (true|false)";
        case FieldValueKind::EnumChoice:        return "enum";
        case FieldValueKind::Path:              return "path (supports $(ARCH))";
        case FieldValueKind::RelativePath:      return "relpath";
    }
    return "string";
}

} // namespace rsw
