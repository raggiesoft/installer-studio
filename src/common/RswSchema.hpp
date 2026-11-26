// ================================================================================
// FILE: src/common/RswSchema.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What is this file?
//     This file is the **Single Source of Truth Grammar (`constexpr` Descriptor Table)**
//     for `.rsip` (RaggieSoft Installer Project) and `.rsii` (Include) manifests!
//
//   Why put the entire manifest schema (`v0.5`) into `constexpr` C++ tables?
//     In C++20, `constexpr` arrays live directly in read-only compile-time memory
//     with zero startup overhead. Defining every valid `.rsip` section, key name,
//     type, default value, allowed enum tokens, and plain-English description here
//     keeps THREE subsystems 100% synchronized forever:
//       1. `RswValidator.hpp` (`instbuild.exe --validate --diagnostics=json`):
//          Checks syntax, GUIDs, 3-part MSI versions, `&` mnemonics, WCAG 2.2 AA
//          brand color contrast, `[Prerequisite.*]` rules, AND Microsoft's official
//          Windows Installer Component Rules!
//       2. `RswAiSpecEmitter.hpp` (`instbuild.exe --emit-ai-spec <dir>`):
//          Deterministically generates `llms.txt`, `llms-full.txt`, and `SKILL.md`
//          straight from the compiled binary with zero documentation drift!
//       3. `InstallerStudio.exe` (Native Win32 C++20 Visual IDE):
//          Powers Destination Folders, Setup Design (`Features -> Components ->
//          KeyPaths`), Runtime Prerequisites, File Associations, Dual Dialog
//          Designer (all 10 WizardLayout variations + Setup Types), and the
//          Lossless AST Source Editor!
// ================================================================================

#pragma once

#include <array>
#include <string_view>

namespace rsw {

/// Distinguishes singleton sections (`[Package]`) from repeatable named sections (`[Component.<id>]`).
enum class SectionCardinality {
    SingletonRequired, // Must appear once in a complete `.rsip` project (`[Package]`, `[Install]`)
    SingletonOptional, // May appear 0 or 1 times (`[Requirements]`, `[UI]`, `[Signing]`)
    NamedRepeatable    // Appears 0 or more times as `[Prefix.<id>]` (`[Feature.*]`, `[SetupType.*]`, etc.)
};

/// Value data types checked by `RswValidator` before `cabinet.dll` or `msi.dll` ever run.
enum class FieldValueKind {
    String,            // Arbitrary UTF-8 text
    AcceleratorString, // Interactive UI label that MUST contain a Windows `&` mnemonic (WCAG / Section 508)
    MsiVersion,        // 3-part `Major.Minor.Build` (`0..255 . 0..255 . 0..65535`)
    Guid,              // Uppercase or lowercase `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}`
    GuidOrAuto,        // `*` (deterministic UUIDv5 or auto-generated GUID) OR explicit `{GUID}`
    Integer,           // Base-10 integer
    Boolean,           // `true` / `false` / `yes` / `no` / `1` / `0`
    EnumChoice,        // Pipe-delimited allowed tokens in `allowedValues`
    Path,              // Local build-machine file or directory path (supports `$(ARCH)`)
    RelativePath       // Relative target directory/file path inside the installed image
};

struct SectionDescriptor {
    std::string_view   nameOrPrefix; // Exact name (`"Package"`) or prefix (`"Prerequisite"` for `[Prerequisite.<id>]`)
    SectionCardinality cardinality;
    bool               atLeastOneRequired;
    std::string_view   summary;
};

struct FieldDescriptor {
    std::string_view sectionPrefix;  // Matches `SectionDescriptor::nameOrPrefix`
    std::string_view key;            // Canonical PascalCase key name (case-insensitive lookup)
    FieldValueKind   kind;
    bool             required;
    std::string_view defaultValue;
    std::string_view allowedValues;  // Used when `kind == FieldValueKind::EnumChoice`
    std::string_view description;
};

/// Standard Windows Installer system directory roots supported across `[Payload.*]`, `[Component.*]`, & `[Folder.*]`.
inline constexpr std::string_view kAllowedTargetRoots =
    "INSTALLDIR|CommonFiles64Folder|CommonAppDataFolder|AppDataFolder|LocalAppDataFolder|"
    "ProgramMenuFolder|ApplicationProgramsFolder|DesktopFolder|System64Folder|FontsFolder";

/// Curated one-click runtime prerequisite presets supported by `[Prerequisite.<id>] Preset`.
inline constexpr std::string_view kAllowedPrereqPresets =
    "Custom|DotNet11Desktop|DotNet11AspNet|DotNet11Runtime|DotNet10Desktop|DotNet10AspNet|DotNet10Runtime|"
    "DotNet9Desktop|DotNet8Desktop|EdgeWebView2|VcRedist2022";

/// All selectable customer-facing `SetupWizard.exe` layout variations (`[UI] WizardLayout`).
inline constexpr std::string_view kAllowedWizardLayouts =
    "standard|wizard95|wizard97|aerowizard|fluentmica|oneclick|passivetoast|fullscreenblue|fullscreenteal|autorun16x9|simple";

// ================================================================================
// 1. CANONICAL `.RSIP` SECTION DESCRIPTOR TABLE (`21` SECTIONS)
// ================================================================================
inline constexpr std::array<SectionDescriptor, 21> kSectionDescriptors{{
    {"Package",         SectionCardinality::SingletonRequired, true,
     "Core product identity, PackageKind (msi/msm/sfx/suite), Engine (msi/native), UAC execution level, target CPU architecture (x64/arm64), and universal output settings."},
    {"Requirements",    SectionCardinality::SingletonOptional, false,
     "Minimum Windows 11 build gate (RtlGetVersion & MSI LaunchCondition), downgrade protection, and silent reboot exit code."},
    {"Install",         SectionCardinality::SingletonRequired, true,
     "Default installation subfolder under ProgramFiles64Folder/LocalAppDataFolder and Start Menu group (optional when PackageKind = sfx or suite)."},
    {"Media",           SectionCardinality::SingletonOptional, false,
     "Multi-volume cabinet disk spanning (MaxCabinetSizeMB -> CAB1.cab..CABN.cab), external vs. embedded cabinets, and uncompressed loose source media (Compression = none | cab)."},
    {"SFX",             SectionCardinality::SingletonOptional, false,
     "Lightweight LZMS self-extracting archive (.exe) post-extraction launch (PostExtractRun), document open (PostExtractOpen), temp staging, and folder options (PackageKind = sfx)."},
    {"UI",              SectionCardinality::SingletonOptional, false,
     "Branding, RTF license, icon, 11 selectable C++ WizardLayout styles (including 'simple' ultra-compact single-dialog mode, Wizard95, FluentMica, FullScreenBlue & ACME FullScreenTeal), SetupTypePreset, and MSI.DLL dialog workflow."},
    {"Signing",         SectionCardinality::SingletonOptional, false,
     "Optional dual-layer Authenticode / Microsoft Azure Artifact Signing (Product.msi first, then outer Setup.exe) via signtool.exe."},
    {"SetupType",       SectionCardinality::NamedRepeatable,   false,
     "Custom author-defined setup mode card ([SetupType.<id>]: Typical, Compact, Complete, Developer Workstation, etc.) with '&' mnemonic, description, feature tier, and icon."},
    {"Billboard",       SectionCardinality::NamedRepeatable,   false,
     "Optional rotating feature highlight card ([Billboard.<id>]) displayed during installation on FullScreenBlue, FullScreenTeal, and progress views."},
    {"Prerequisite",    SectionCardinality::NamedRepeatable,   false,
     "Runtime prerequisite ([Prerequisite.<id>]: .NET 11, .NET 10 LTS, WebView2, VC++ Redist, or Custom .exe/.msi) executed BEFORE msiexec with download/embedded/side-by-side delivery and reboot exit-code tracking."},
    {"SuitePackage",    SectionCardinality::NamedRepeatable,   false,
     "Child .msi or native setup package ([SuitePackage.<id>]) chained under a single UAC prompt in Unified Suite Setup (PackageKind = suite) with dynamic ARPSYSTEMCOMPONENT adoption."},
    {"MergeModule",     SectionCardinality::NamedRepeatable,   false,
     "Consumes a Windows Installer Merge Module ([MergeModule.<id>]: .msm) merging its Components, Files, Registry, and MergeModule.CABinet directly into Product.msi."},
    {"Feature",         SectionCardinality::NamedRepeatable,   false,
     "Named Feature or Sub-Feature ([Feature.<id>]) with plain-English Inclusion tiers, optional Run-from-Source (MenuCD/MenuNetwork), and embedded vs. external .rswpak on-demand delivery."},
    {"Component",       SectionCardinality::NamedRepeatable,   false,
     "Explicit MSI Component ([Component.<id>]) enforcing Microsoft Single-Directory & KeyPath rules (File, Registry, or CreateFolder)."},
    {"Folder",          SectionCardinality::NamedRepeatable,   false,
     "Creates a destination subdirectory ([Folder.<id>]) via MSI CreateFolder + ICE64 RemoveFolder cleanup."},
    {"Payload",         SectionCardinality::NamedRepeatable,   true,
     "Maps a build-machine file, directory, or .zip archive ([Payload.<id>]) into #payload.cab / RSWPAK1 and deterministic or explicit Components."},
    {"FileAssociation", SectionCardinality::NamedRepeatable,   false,
     "Registers a file extension, ProgId, document icon, MIME type, and Explorer shell verbs ([FileAssociation.<id>])."},
    {"Shortcut",        SectionCardinality::NamedRepeatable,   false,
     "Creates an advertised-free Start Menu or Desktop shortcut ([Shortcut.<id>]) with ICE38 HKCU KeyPath & ICE64 RemoveFolder."},
    {"Registry",        SectionCardinality::NamedRepeatable,   false,
     "Writes an HKLM/HKCU/HKCR/HKMU registry value ([Registry.<id>]) tied to an auto or explicit 64-bit MSI Component."},
    {"MsiDialog",       SectionCardinality::NamedRepeatable,   false,
     "Defines a custom 370x270 Windows Installer SQL Dialog ([MsiDialog.<id>]) spliced into the MSI.DLL UI sequence."},
    {"MsiControl",      SectionCardinality::NamedRepeatable,   false,
     "Adds a native MSI control ([MsiControl.<id>]: Text, Edit, CheckBox, RadioButtonGroup, etc.) to a new or built-in MSI Dialog."}
}};

// ================================================================================
// 2. CORE `.RSIP` FIELD DESCRIPTOR TABLE
// ================================================================================
inline constexpr std::array<FieldDescriptor, 45> kFieldDescriptors{{
    // --- [Package] ---
    {"Package", "SchemaVersion",         FieldValueKind::String,      false, "0.6",               "",
     "Manifest grammar version (`0.6`)."},
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
     "Output folder for `<OutputName>.msi` and `<OutputName>-setup.exe` (relative to `.rsip` folder or absolute)."},
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
     "Friendly message shown by LaunchCondition / SetupWizard.exe when the host OS is older than MinWindowsBuild."},
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
     "Optional `.rtf` license file displayed in Wizard 97 `LicenseAgreementDlg` and `SetupWizard.exe`."},
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
     "Plain-English description of this MSI Feature shown in SelectionTree."},
    {"Feature", "Level",                 FieldValueKind::Integer,     false, "1",                 "",
     "MSI InstallLevel (`1` = core/Compact & Express, `2..100` = Express/Custom optional, `0` = hidden)."},

    // --- [Payload.<id>] ---
    {"Payload", "Source",                FieldValueKind::Path,        true,  "",                  "",
     "Source file or directory on the build machine (supports `$(ARCH)` expansion)."},
    {"Payload", "TargetSubdir",          FieldValueKind::RelativePath,false, "",                  "",
     "Optional destination subfolder inside `TargetRoot` (`\"\"` = root of `TargetRoot`)."},
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

// ================================================================================
// 3. EXTENDED `.RSIP` FIELD DESCRIPTOR TABLE (`v0.5` — WIZARD STYLES, SETUP TYPES,
//    BILLBOARDS, PREREQUISITES, SELECTION TREE, COMPONENTS, FOLDERS, & DIALOGS)
// ================================================================================
inline constexpr auto kExtendedFieldDescriptors = std::to_array<FieldDescriptor>({
    // --- Extended [Package], [Media], [SFX], [Requirements], [UI], [SetupType], [Billboard], [SuitePackage], [Feature], [Payload], [Shortcut], [Registry] ---
    {"Package",  "PackageKind",          FieldValueKind::EnumChoice,  false, "msi",               "msi|msm|sfx|suite",
     "`msi` builds an Enterprise `.msi` (+ optional `-setup.exe`), `msm` builds a Merge Module (`.msm`), `sfx` builds a lightweight LZMS Self-Extracting `.exe` (zero Installed Apps clutter), and `suite` builds a single-UAC Unified Suite Chainer (`ARPSYSTEMCOMPONENT` adoption)."},
    {"Package",  "Engine",               FieldValueKind::EnumChoice,  false, "msi",               "msi|native",
     "`msi` uses Microsoft Windows Installer (`msi.dll`); `native` uses RaggieSoft's zero-`msi.dll` transactional LZMS extraction engine with rollback journal, `.lnk` shortcuts, `Uninstall` registry registration, and signed `Uninstall.exe`."},
    {"Package",  "RequestedExecutionLevel", FieldValueKind::EnumChoice, false, "asInvoker",       "asInvoker|highestAvailable|requireAdministrator",
     "Controls Windows UAC level stamped onto PE `RT_MANIFEST` `#1` across `msi`, `native`, `sfx`, and `suite` EXEs: `asInvoker` (default — un-elevated launch; prompts UAC only if per-machine install begins), `highestAvailable` (elevates upfront for Admins while letting Standard Users run), or `requireAdministrator` (requires Admin UAC immediately at `0.0s` launch)."},
    {"Package",  "BuildUniversal",       FieldValueKind::Boolean,     false, "false",             "",
     "When `true` (or when `--universal` is passed to `instbuild.exe`), compiles both `x64` and `arm64` editions and emits `<UniversalOutputName>-offline-setup.exe` plus `<UniversalOutputName>-setup.exe`."},
    {"Package",  "UniversalOutputName",  FieldValueKind::String,      false, "",                  "",
     "Optional base filename stem for universal combined installers; defaults to `OutputName` stripped of `-$(ARCH)`."},
    {"Package",  "UniversalDownloadBaseUrl", FieldValueKind::String,  false, "",                  "",
     "Optional HTTPS folder URL hosting `<OutputName>-x64-setup.exe`, `<OutputName>-arm64-setup.exe`, and `.rswpak` feature packs."},
    {"Package",  "Tagline",              FieldValueKind::String,      false, "",                  "",
     "Short subtitle displayed beneath the product title in `SetupWizard.exe` and `InstallerStudio.exe`."},
    {"Package",  "WebsiteLabel",         FieldValueKind::String,      false, "",                  "",
     "Optional domain badge shown in the setup wizard footer (left blank if omitted — 100% white-label!)."},

    // --- [Media] (Multi-Volume Disk Spanning & Uncompressed Loose Source Media!) ---
    {"Media",    "Compression",          FieldValueKind::EnumChoice,  false, "cab",               "cab|none",
     "`cab` compresses payloads into `#payload.cab` or external `CAB1.cab..CABN.cab`; `none` emits uncompressed loose source files (`PID_WORDCOUNT` bit `1` cleared + `msidbFileAttributesNoncompressed` `0x2000`) for Run-from-Media / Network shares."},
    {"Media",    "EmbedCabinet",         FieldValueKind::Boolean,     false, "true",              "",
     "When `true` and `MaxCabinetSizeMB = 0` (`MaxCabinetSizeKB = 0`), embeds `#payload.cab` inside `Product.msi`; when `false` or spanning is active, emits external `CAB1.cab..CABN.cab` next to `Product.msi`."},
    {"Media",    "MaxCabinetSizeMB",     FieldValueKind::Integer,     false, "0",                 "",
     "Maximum size in megabytes (`1..2047`) per external cabinet (`CAB1.cab..CABN.cab`) before spanning to the next `DiskId` in the `Media` SQL table (`0` = undivided single cabinet)."},
    {"Media",    "MaxCabinetSizeKB",     FieldValueKind::Integer,     false, "0",                 "",
     "Optional fine-grained maximum cabinet size in kilobytes (`>= 32`) before spanning to `CAB2.cab..CABN.cab` (overrides `MaxCabinetSizeMB` when `> 0`)."},
    {"Media",    "VolumeLabel",          FieldValueKind::String,      false, "DISK",              "",
     "Volume label prefix written to `Media.VolumeLabel` (`DISK1`, `DISK2`, ...)."},
    {"Media",    "DiskPrompt",           FieldValueKind::String,      false, "Please insert [ProductName] Disk [1]", "",
     "Change-media prompt written to `Media.DiskPrompt` and `Property.DiskPrompt` (`Error 1302`)."},

    // --- [SFX] (Self-Extracting `.EXE` Options when `PackageKind = sfx`!) ---
    {"SFX",      "DefaultExtractSubdir", FieldValueKind::RelativePath,false, "",                  "",
     "Optional default subfolder name created next to the self-extracting `.exe` when `ExtractToTemp = false`."},
    {"SFX",      "PostExtractRun",       FieldValueKind::RelativePath,false, "",                  "",
     "Optional bundled `.exe` or `.cmd` (relative to the extraction folder) launched automatically after extraction completes."},
    {"SFX",      "PostExtractArgs",      FieldValueKind::String,      false, "",                  "",
     "Optional command-line arguments passed when launching `PostExtractRun`."},
    {"SFX",      "PostExtractOpen",      FieldValueKind::RelativePath,false, "",                  "",
     "Optional bundled document (`.pdf`, `.html`, `.txt`, `.rtf`, etc. relative to extraction folder) opened via `ShellExecuteExW` when a compatible viewer is installed."},
    {"SFX",      "ExtractToTemp",        FieldValueKind::Boolean,     false, "false",             "",
     "When `true`, defaults extraction to an isolated `%TEMP%\\<Slug>-<PID>` temporary directory instead of prompting for a permanent folder."},
    {"SFX",      "AllowChangeFolder",    FieldValueKind::Boolean,     false, "true",              "",
     "When `true` (default), allows the user to edit or browse the destination folder before clicking `&Extract`."},
    {"SFX",      "SilentOverwrite",      FieldValueKind::Boolean,     false, "true",              "",
     "When `true`, overwrites existing files in the destination folder without extra prompts."},

    {"Requirements", "SilentRebootExitCode", FieldValueKind::Integer, false, "3010",              "",
     "Process exit code returned in silent mode (`/s` / `/qn`) when a runtime prerequisite or MSI requests a Windows restart (`3010` standard MSI)."},
    {"UI",       "WizardLayout",         FieldValueKind::EnumChoice,  false, "standard",          kAllowedWizardLayouts,
     "Selects the customer-facing `SetupWizard.exe` style: `simple` (ultra-compact single-dialog `Destination folder` + `[...]` + progress + `&Install`/`&Extract`), `standard` (RaggieSoft Glass), `wizard95` (1995 `#C0C0C0` 3D beveled + left sidebar), `wizard97` (classic top-banner), `aerowizard` (command bar + circular `<-` Back), `fluentmica` (Windows 11 Mica + left step rail), `oneclick` (compact 1-click splash), `passivetoast` (`/passive` progress), `fullscreenblue` (1995 `#000080` Cobalt wash + 3D dialog), `fullscreenteal` (1995 `#008080` Microsoft ACME Teal wash + 3D dialog), or `autorun16x9` (640x480 16:9 launcher)."},
    {"UI",       "SetupTypePreset",      FieldValueKind::EnumChoice,  false, "express_custom",    "express_custom|win95_classic",
     "`express_custom` displays Express / Compact / Custom cards; `win95_classic` displays classic 4-option Typical / Compact / Complete / Custom cards (overridden if custom `[SetupType.<id>]` sections are defined)."},
    {"UI",       "MsiDialogPreset",      FieldValueKind::EnumChoice,  false, "express_custom",    "express_custom|minimal",
     "`express_custom` builds the full Express / Compact / Custom + SelectionTree + BrowseDlg + Shortcuts + Repair Mode workflow inside MSI.DLL's Dialog table; `minimal` builds a compact 3-screen MSI UI."},
    {"UI",       "MsiProgressStyle",     FieldValueKind::EnumChoice,  false, "standard",          "standard|flat",
     "`standard` (default) sets `msidbControlAttributesProgress95` (`0x10000`) on `ProgressDlg.ProgressBar` so Windows renders a native themed Common Controls progress bar instead of MSI.DLL's legacy flat 2D box; `flat` opts into legacy owner-draw."},
    {"UI",       "SystemMenuAbout",      FieldValueKind::Boolean,     false, "false",             "",
     "When `true`, appends an author-branded `About <ProductName> Setup...` item to the top-left `Alt+Space` window System Menu with zero forced third-party watermarks."},
    {"UI",       "AccentPrimaryHex",     FieldValueKind::String,      false, "#2563EB",           "",
     "Primary header/button accent hex color (checked against WCAG 2.2 AA contrast in `instbuild.exe --validate`)."},
    {"UI",       "AccentSecondaryHex",   FieldValueKind::String,      false, "#FBBF24",           "",
     "Secondary gradient accent hex color (e.g. `#FBBF24` Warm Gold, `#10B981` Emerald, `#EC4899` Rose)."},
    {"UI",       "ArtworkPng",           FieldValueKind::Path,        false, "",                  "",
     "Optional `.png` 16:9 banner, `164x314` wizard sidebar bitmap, or square product cover rendered inside `SetupWizard.exe`."},
    {"UI",       "WelcomeIntro",         FieldValueKind::String,      false, "",                  "",
     "Optional introductory paragraph displayed on the Welcome page of `SetupWizard.exe`."},
    {"UI",       "ReadMeText",           FieldValueKind::String,      false, "",                  "",
     "Optional edition liner notes or ReadMe summary displayed in the ReadMe view of `autorun16x9` installers."},
    {"UI",       "ExpressDescription",   FieldValueKind::String,      false,
     "One-click setup into the default installation folder with recommended shortcuts. Skips extra prompts.", "",
     "Helper caption for the Express Setup mode card in `SetupWizard.exe` and MSI `SetupTypeDlg`."},
    {"UI",       "CompactDescription",   FieldValueKind::String,      false,
     "Installs essential core application files (`Inclusion = minimal`) with minimal disk footprint.", "",
     "Helper caption for the Compact Core Setup mode card in `SetupWizard.exe` and MSI `SetupTypeDlg`."},
    {"UI",       "CustomDescription",    FieldValueKind::String,      false,
     "Choose features in the MSI SelectionTree, destination folder, Start Menu group, and shortcuts step by step.", "",
     "Helper caption for the Custom Setup mode card in `SetupWizard.exe` and MSI `SetupTypeDlg`."},
    {"UI",       "AuthorOrArtist",       FieldValueKind::String,      false, "",                  "",
     "Optional author or band credit badge displayed in `autorun16x9` book/album micro-installers."},

    // --- [SetupType.<id>] (Custom Author-Defined Setup Types!) ---
    {"SetupType","Title",                FieldValueKind::AcceleratorString, true, "",             "",
     "Option card heading; MUST include a `&` keyboard mnemonic (e.g. `&Developer Workstation (Recommended)`)."},
    {"SetupType","Description",          FieldValueKind::String,      false, "",                  "",
     "Plain-English description of what this Setup Type installs."},
    {"SetupType","Tier",                 FieldValueKind::EnumChoice,  false, "typical",           "minimal|typical|complete|custom",
     "Target feature tier (`minimal` -> `INSTALLLEVEL=20`, `typical` -> `100`, `complete` -> `1000`, or `custom` -> opens `SelectionTree` + folder steps)."},
    {"SetupType","Features",             FieldValueKind::String,      false, "",                  "",
     "Optional comma-separated explicit Feature IDs (`ADDLOCAL=Main,Templates`) overriding `Tier`."},
    {"SetupType","Glyph",                FieldValueKind::EnumChoice,  false, "Bolt",
     "Sparkle|ShieldCheck|Bolt|Cube|Sliders|Blocks|Folder|PinGrid|Play|Gear",
     "Vector icon badge displayed on this Setup Type card."},
    {"SetupType","Recommended",          FieldValueKind::Boolean,     false, "false",             "",
     "Marks this Setup Type as the default pre-selected choice on `WizardPage::SetupType`."},

    // --- [Billboard.<id>] (Rotating Feature Highlight Cards During Installation!) ---
    {"Billboard","Headline",             FieldValueKind::String,      true,  "",                  "",
     "Short feature headline displayed on the rotating billboard card while files copy."},
    {"Billboard","Body",                 FieldValueKind::String,      false, "",                  "",
     "Supporting description displayed beneath `Headline` on the billboard card."},
    {"Billboard","Glyph",                FieldValueKind::EnumChoice,  false, "Sparkle",
     "Sparkle|ShieldCheck|Bolt|Cube|Sliders|Blocks|Folder|PinGrid|Play|Gear",
     "Vector icon badge rendered on this billboard slide."},

    {"Feature",  "Inclusion",            FieldValueKind::EnumChoice,  false, "minimal",           "minimal|typical|complete|hidden",
     "Plain-English setup inclusion tier replacing raw numeric `InstallLevel`: `minimal` (`Level = 1`, installed in Minimal/Compact, Typical/Express, & Complete), `typical` (`Level = 50`, installed in Typical/Express & Complete, skipped in Minimal/Compact), `complete` (`Level = 150`, installed in Complete or when opted in inside Custom SelectionTree), or `hidden` (`Level = 0`, hidden/conditional)."},
    {"Feature",  "Parent",               FieldValueKind::String,      false, "",                  "",
     "Optional parent Feature ID (`Feature_Parent`) creating a hierarchical sub-feature in `SelectionTree`."},
    {"Feature",  "DisallowAbsent",       FieldValueKind::Boolean,     false, "false",             "",
     "Sets `msidbFeatureAttributesUIDisallowAbsent` (`16`) locking a required core feature so users cannot disable it in `SelectionTree`."},
    {"Feature",  "AllowAdvertise",       FieldValueKind::Boolean,     false, "false",             "",
     "When `false` (default), sets `msidbFeatureAttributesDisallowAdvertise` (`8`) hiding MSI on-demand advertisement; set `true` to enable 'Feature will be installed when required'."},
    {"Feature",  "AllowRunFromSource",   FieldValueKind::Boolean,     false, "false",             "",
     "When `true`, sets `msidbComponentAttributesOptional` (`2`) on owned components and enables `MenuCD` ('Will be installed to run from CD') & `MenuNetwork` ('Will be installed to run from network') in `SelectionTree`."},
    {"Feature",  "Delivery",             FieldValueKind::EnumChoice,  false, "embedded",          "embedded|ondemand",
     "`embedded` packs feature files inside the main installer; `ondemand` splits this feature into an external LZMS-compressed `<OutputName>-<FeatureId>.rswpak` block fetched via WinHTTP with SHA-256 + Authenticode verification when selected."},
    {"Feature",  "PackUrl",              FieldValueKind::String,      false, "",                  "",
     "Optional HTTPS download URL for `<OutputName>-<FeatureId>.rswpak` when `Delivery = ondemand` (defaults to `UniversalDownloadBaseUrl` or side-by-side local `.rswpak`)."},

    // --- [SuitePackage.<id>] (Unified Suite Chainer & Dynamic ARPSYSTEMCOMPONENT Adoption!) ---
    {"SuitePackage", "Name",             FieldValueKind::String,      true,  "",                  "",
     "Consumer-facing child tool title displayed in the Unified Suite Setup & Maintenance Hub."},
    {"SuitePackage", "Description",      FieldValueKind::String,      false, "",                  "",
     "Plain-English description of this child package in the Suite checklist."},
    {"SuitePackage", "Source",           FieldValueKind::Path,        true,  "",                  "",
     "Path to the compiled child `.msi` or native `-setup.exe` binary on the build machine (supports `$(ARCH)`)."},
    {"SuitePackage", "UpgradeCode",      FieldValueKind::Guid,        false, "",                  "",
     "Permanent `{UpgradeCode}` of this child package used to detect & dynamically adopt already-installed standalone siblings (`RS_SUITE_MANAGED=1`)."},
    {"SuitePackage", "Engine",           FieldValueKind::EnumChoice,  false, "msi",               "msi|native|exe",
     "Child installer engine (`msi` passes `RS_SUITE_MANAGED=1` via `MsiInstallProductW`; `native` / `exe` passes `/s RS_SUITE_MANAGED=1`)."},
    {"SuitePackage", "InstallArgs",      FieldValueKind::String,      false, "",                  "",
     "Optional extra properties or CLI switches passed to this child package during Suite install."},
    {"SuitePackage", "Required",         FieldValueKind::Boolean,     false, "false",             "",
     "When `true`, locks this core Suite package as required in the Suite Setup checklist."},
    {"SuitePackage", "SelectedByDefault",FieldValueKind::Boolean,     false, "true",              "",
     "Whether this child package is pre-checked by default on fresh Suite installations."},
    {"SuitePackage", "AdoptStandalone",  FieldValueKind::Boolean,     false, "true",              "",
     "When `true` (default), dynamically adopts any already-installed standalone sibling (`REINSTALL=ALL REINSTALLMODE=vomus RS_SUITE_MANAGED=1`) under the single Suite Installed Apps entry."},
    {"Payload",  "TargetRoot",           FieldValueKind::EnumChoice,  false, "INSTALLDIR",        kAllowedTargetRoots,
     "Destination MSI system folder root (`INSTALLDIR`, `CommonAppDataFolder`, `AppDataFolder`, `FontsFolder`, etc.)."},
    {"Payload",  "Component",            FieldValueKind::String,      false, "",                  "",
     "Optional `[Component.<id>]` name. When omitted, each file gets its own deterministic SHA-1 UUIDv5 Component & File KeyPath per Microsoft best practice."},
    {"Shortcut", "Arguments",            FieldValueKind::String,      false, "",                  "",
     "Optional command-line arguments passed when the shortcut launches `TargetFile`."},
    {"Shortcut", "Feature",              FieldValueKind::String,      false, "Main",              "",
     "Feature ID owning this shortcut."},
    {"Registry", "Root",                 FieldValueKind::EnumChoice,  false, "HKLM",              "HKLM|HKCU|HKCR|HKU|HKMU",
     "Registry hive (`HKLM` machine, `HKCU` user, `HKCR` classes, `HKU` users, or `HKMU` auto-switching per `ALLUSERS`)."},
    {"Registry", "Key",                  FieldValueKind::String,      true,  "",                  "",
     "Registry key path without hive prefix, e.g. `Software\\RaggieSoft\\MyApp`."},
    {"Registry", "Name",                 FieldValueKind::String,      true,  "",                  "",
     "Registry value name (use `@` for the key's default value)."},
    {"Registry", "Value",                FieldValueKind::String,      true,  "",                  "",
     "Registry value data (supports MSI formatted tokens like `[INSTALLDIR]` and `[ProductVersion]`)."},
    {"Registry", "Type",                 FieldValueKind::EnumChoice,  false, "string",            "string|expand_string|dword",
     "Registry value type (`string` = REG_SZ, `expand_string` = REG_EXPAND_SZ, `dword` = REG_DWORD)."},
    {"Registry", "Feature",              FieldValueKind::String,      false, "Main",              "",
     "Feature ID owning this registry value."},
    {"Registry", "Component",            FieldValueKind::String,      false, "",                  "",
     "Optional `[Component.<id>]` owning this registry row (used when grouping registry values or setting a Registry KeyPath)."},

    // --- [Prerequisite.<id>] (Pre-MSI Runtime Bootstrapper: Download, Embedded, & Side-by-Side!) ---
    {"Prerequisite", "Preset",             FieldValueKind::EnumChoice,  false, "Custom",          kAllowedPrereqPresets,
     "One-click curated preset (`DotNet10Desktop`, `DotNet10AspNet`, `DotNet9Desktop`, `DotNet8Desktop`, `EdgeWebView2`, `VcRedist2022`) or `Custom`."},
    {"Prerequisite", "Name",               FieldValueKind::String,      false, "",                "",
     "Consumer-friendly runtime display name (auto-filled when `Preset` != `Custom`)."},
    {"Prerequisite", "Distribution",       FieldValueKind::EnumChoice,  false, "download",        "download|embedded|side-by-side",
     "`download` streams over WinHTTP HTTPS on demand; `embedded` packs `SourceFile` inside `Setup.exe` for offline installs; `side-by-side` reads `Redist\\<FileName>` next to `Setup.exe` (with URL fallback)."},
    {"Prerequisite", "SourceFile",         FieldValueKind::Path,        false, "",                "",
     "Local runtime `.exe` or `.msi` on the build machine (required for `Distribution = embedded`, optional for `side-by-side`, supports `$(ARCH)`)."},
    {"Prerequisite", "FileName",           FieldValueKind::String,      false, "",                "",
     "Runtime installer filename used inside `Redist\\` or `%TEMP%` (auto-filled for presets)."},
    {"Prerequisite", "Url",                FieldValueKind::String,      false, "",                "",
     "Architecture-neutral HTTPS download URL (or fallback URL for `side-by-side`)."},
    {"Prerequisite", "UrlX64",             FieldValueKind::String,      false, "",                "",
     "Optional `x64`-specific HTTPS download URL (auto-filled for `aka.ms` presets)."},
    {"Prerequisite", "UrlArm64",           FieldValueKind::String,      false, "",                "",
     "Optional `arm64`-specific HTTPS download URL (auto-filled for `aka.ms` presets)."},
    {"Prerequisite", "Sha256",             FieldValueKind::String,      false, "",                "",
     "Optional expected SHA-256 hex digest verified via `bcrypt.dll` before executing a downloaded runtime."},
    {"Prerequisite", "SilentArgs",         FieldValueKind::String,      false, "",                "",
     "Command-line switches passed to the runtime installer (auto-filled for presets, e.g. `/install /quiet /norestart`)."},
    {"Prerequisite", "SuccessExitCodes",   FieldValueKind::String,      false, "0, 1638",         "",
     "Comma-separated exit codes treated as clean success (`0` = OK, `1638` = newer redistributable already installed)."},
    {"Prerequisite", "RebootExitCodes",    FieldValueKind::String,      false, "3010, 1641",      "",
     "Comma-separated exit codes treated as success WITH a Windows restart requested (`3010`, `1641`, `23`, or any custom vendor codes)."},
    {"Prerequisite", "DetectRegistryHive", FieldValueKind::EnumChoice,  false, "HKLM",            "HKLM|HKCU",
     "Registry hive checked before running a `Custom` prerequisite (`HKLM` or `HKCU`, probed in native 64-bit view)."},
    {"Prerequisite", "DetectRegistryKey",  FieldValueKind::String,      false, "",                "",
     "Optional registry key checked to skip an already-installed `Custom` prerequisite."},
    {"Prerequisite", "DetectRegistryValue",FieldValueKind::String,      false, "",                "",
     "Optional registry value name under `DetectRegistryKey` (non-empty / non-zero means installed)."},
    {"Prerequisite", "DetectMinVersion",   FieldValueKind::String,      false, "",                "",
     "Optional minimum dotted version compared against `DetectRegistryValue` or `DetectFilePath`."},
    {"Prerequisite", "DetectFilePath",     FieldValueKind::String,      false, "",                "",
     "Optional file path (supports `%ProgramW6432%` / `%SystemRoot%`) whose presence/version skips the prerequisite."},
    {"Prerequisite", "DetectUpgradeCode",  FieldValueKind::Guid,        false, "",                "",
     "Optional MSI `{UpgradeCode}` probed via `MsiEnumRelatedProductsW` to skip an already-installed MSI runtime."},
    {"Prerequisite", "RequiresElevation",  FieldValueKind::Boolean,     false, "true",            "",
     "When `true` (default), executes inside the single unified elevated UAC session right before `Product.msi`."},
    {"Prerequisite", "Optional",           FieldValueKind::Boolean,     false, "false",           "",
     "When `true`, logs a warning and continues installing `Product.msi` even if this prerequisite fails."},

    // --- [MergeModule.<id>] (Consume Third-Party or First-Party .MSM Merge Modules!) ---
    {"MergeModule", "Source",              FieldValueKind::Path,        true,  "",                "",
     "Path to the `.msm` Merge Module database on the build machine (supports `$(ARCH)`)."},
    {"MergeModule", "Feature",             FieldValueKind::String,      false, "Main",            "",
     "Parent `[Feature.<id>]` in `Product.msi` that receives the Merge Module's components."},
    {"MergeModule", "TargetRoot",          FieldValueKind::EnumChoice,  false, "INSTALLDIR",      kAllowedTargetRoots,
     "Destination root folder to which the Merge Module's `TARGETDIR` / `MergeRedirectFolder` maps."},

    // --- [Component.<id>] (Microsoft Official Component & KeyPath Rules!) ---
    {"Component", "Feature",             FieldValueKind::String,      false, "Main",              "",
     "Feature ID owning this Component in `FeatureComponents`."},
    {"Component", "TargetRoot",          FieldValueKind::EnumChoice,  false, "INSTALLDIR",        kAllowedTargetRoots,
     "Single Windows Installer root directory for this Component (Microsoft Single-Directory Component Rule)."},
    {"Component", "TargetSubdir",        FieldValueKind::RelativePath,false, "",                  "",
     "Optional subdirectory under `TargetRoot` where all files of this Component reside."},
    {"Component", "Guid",                FieldValueKind::GuidOrAuto,  false, "*",                 "",
     "`*` computes a deterministic SHA-1 UUIDv5 ComponentId, or specify an explicit `{GUID}`."},
    {"Component", "KeyPathKind",         FieldValueKind::EnumChoice,  false, "auto",              "auto|file|registry|folder",
     "KeyPath resource type required by Windows Installer (`file` = File table key, `registry` = Registry key per ICE38, `folder` = NULL KeyPath + CreateFolder per ICE18)."},
    {"Component", "KeyPathTarget",       FieldValueKind::String,      false, "",                  "",
     "Target filename (when `KeyPathKind = file`) or `[Registry.<id>]` section ID (when `KeyPathKind = registry`)."},
    {"Component", "Condition",           FieldValueKind::String,      false, "",                  "",
     "Optional MSI install condition evaluated during `CostFinalize` (e.g. `DESKTOP_SHORTCUT = \"1\"`)."},
    {"Component", "Permanent",           FieldValueKind::Boolean,     false, "false",             "",
     "Sets `msidbComponentAttributesPermanent` (`16`) so Windows Installer leaves this component on disk during uninstall."},
    {"Component", "NeverOverwrite",      FieldValueKind::Boolean,     false, "false",             "",
     "Sets `msidbComponentAttributesNeverOverwrite` (`128`) so existing user-modified config files/keys are never overwritten."},
    {"Component", "SharedDllRefCount",   FieldValueKind::Boolean,     false, "false",             "",
     "Sets `msidbComponentAttributesSharedDllRefCount` (`8`) for legacy shared reference counting."},

    // --- [Folder.<id>] (Explicit Destination Subdirectories & Empty CreateFolder Nodes) ---
    {"Folder",    "TargetRoot",          FieldValueKind::EnumChoice,  false, "INSTALLDIR",        kAllowedTargetRoots,
     "Parent Windows Installer root folder (`INSTALLDIR`, `CommonAppDataFolder`, `AppDataFolder`, etc.)."},
    {"Folder",    "TargetSubdir",        FieldValueKind::RelativePath,true,  "",                  "",
     "Subfolder path to create under `TargetRoot` (e.g. `Logs` or `Plugins\\Custom`)."},
    {"Folder",    "Feature",             FieldValueKind::String,      false, "Main",              "",
     "Feature ID owning the `CreateFolder` component."},
    {"Folder",    "Component",           FieldValueKind::String,      false, "",                  "",
     "Optional `[Component.<id>]` name; auto-generates a `CreateFolder` component if omitted."},
    {"Folder",    "RemoveOnUninstall",   FieldValueKind::Boolean,     false, "true",              "",
     "Emits a `RemoveFile` (`InstallMode = 2`) row so empty folders are removed cleanly on uninstall (ICE64)."},

    // --- [FileAssociation.<id>] (Document Extensions, Icons, MIME & Shell Verbs!) ---
    {"FileAssociation", "Extension",     FieldValueKind::String,      true,  "",                  "",
     "File extension to register (with or without leading dot, e.g. `.rsip` or `rsip`)."},
    {"FileAssociation", "ProgId",        FieldValueKind::String,      true,  "",                  "",
     "Canonical programmatic identifier, e.g. `RaggieSoft.InstallerStudio.Project`."},
    {"FileAssociation", "Description",   FieldValueKind::String,      true,  "",                  "",
     "Human-friendly document type description displayed in Windows Explorer (e.g. `RaggieSoft Installer Project`)."},
    {"FileAssociation", "TargetFile",    FieldValueKind::RelativePath,true,  "",                  "",
     "Relative path under `INSTALLDIR` to the handler `.exe` packaged by `[Payload.*]`."},
    {"FileAssociation", "Verbs",         FieldValueKind::String,      false, "open:&Open:\"%1\"", "",
     "Pipe-separated shell verb definitions `verbId:MenuCaption:Arguments` (e.g. `open:&Open:\"%1\"|build:&Compile MSI:--build \"%1\"`)."},
    {"FileAssociation", "ContentType",   FieldValueKind::String,      false, "",                  "",
     "Optional MIME content type (e.g. `application/x-raggiesoft-rsip`)."},
    {"FileAssociation", "PerceivedType", FieldValueKind::String,      false, "",                  "",
     "Optional Windows Explorer PerceivedType (`text`, `document`, `audio`, `image`, `video`)."},
    {"FileAssociation", "IconIndex",     FieldValueKind::Integer,     false, "0",                 "",
     "Zero-based icon resource index inside `TargetFile` (or `[UI] IconFile` when `Advertise = true`)."},
    {"FileAssociation", "Advertise",     FieldValueKind::Boolean,     false, "false",             "",
     "`false` (default) writes clean `HKMU\\Software\\Classes` + `OpenWithProgids` keys with zero MSI self-repair popups; `true` populates native MSI `ProgId`, `Extension`, `Verb`, and `MIME` tables."},
    {"FileAssociation", "Feature",       FieldValueKind::String,      false, "Main",              "",
     "Feature ID owning this file association."},

    // --- [MsiDialog.<id>] (Custom 370x270 Windows Installer SQL Dialogs!) ---
    {"MsiDialog", "Title",               FieldValueKind::String,      false, "[ProductName] Setup", "",
     "Window caption bar title of this MSI Dialog."},
    {"MsiDialog", "Width",               FieldValueKind::Integer,     false, "370",               "",
     "Width in Windows Installer dialog units (standard Wizard 97 width is `370`)."},
    {"MsiDialog", "Height",              FieldValueKind::Integer,     false, "270",               "",
     "Height in Windows Installer dialog units (standard Wizard 97 height is `270`)."},
    {"MsiDialog", "Banner",              FieldValueKind::Boolean,     false, "true",              "",
     "Automatically adds standard Wizard 97 top `BannerBitmap`, `BannerLine`, `BottomLine`, and `Back`/`Next`/`Cancel` buttons."},
    {"MsiDialog", "HeaderTitle",         FieldValueKind::String,      false, "Custom Configuration", "",
     "Bold title text rendered inside the top 44-unit Wizard 97 banner."},
    {"MsiDialog", "HeaderSubtitle",      FieldValueKind::String,      false, "",                  "",
     "Subtitle description rendered beneath `HeaderTitle` inside the top Wizard 97 banner."},
    {"MsiDialog", "PrevDialog",          FieldValueKind::String,      false, "SetupTypeDlg",      "",
     "Previous dialog ID in the wizard chain (`< &Back` target and automatic forward splice point!)."},
    {"MsiDialog", "NextDialog",          FieldValueKind::String,      false, "VerifyReadyDlg",    "",
     "Next dialog ID opened when the user clicks `&Next >`."},

    // --- [MsiControl.<id>] (Native Controls Placed on Custom or Built-In MSI Dialogs!) ---
    {"MsiControl", "Dialog",             FieldValueKind::String,      true,  "",                  "",
     "Target dialog ID (`[MsiDialog.<Dialog>]` or built-in dialog such as `SetupTypeDlg` or `VerifyReadyDlg`)."},
    {"MsiControl", "Type",               FieldValueKind::EnumChoice,  true,  "Text",
     "Text|Edit|MaskedEdit|PathEdit|CheckBox|RadioButtonGroup|PushButton|ComboBox|ListBox|SelectionTree|GroupBox|Line|Bitmap|ScrollableText",
     "Native Windows Installer Control table type."},
    {"MsiControl", "X",                  FieldValueKind::Integer,     true,  "20",                "",
     "Horizontal position in MSI dialog units (`0..370`)."},
    {"MsiControl", "Y",                  FieldValueKind::Integer,     true,  "60",                "",
     "Vertical position in MSI dialog units (`0..270`)."},
    {"MsiControl", "Width",              FieldValueKind::Integer,     true,  "330",               "",
     "Control width in MSI dialog units."},
    {"MsiControl", "Height",             FieldValueKind::Integer,     true,  "18",                "",
     "Control height in MSI dialog units."},
    {"MsiControl", "Property",           FieldValueKind::String,      false, "",                  "",
     "MSI Property bound to this control (automatically added to `SecureCustomProperties` if uppercase!)."},
    {"MsiControl", "DefaultValue",       FieldValueKind::String,      false, "",                  "",
     "Optional initial value written to the MSI `Property` table."},
    {"MsiControl", "Text",               FieldValueKind::String,      false, "",                  "",
     "Control label or rich text (`{\\DlgFontBold8}...`)."},
    {"MsiControl", "Items",              FieldValueKind::String,      false, "",                  "",
     "Pipe-separated `Value:Caption` entries for `RadioButtonGroup`, `ComboBox`, or `ListBox` controls."}
});

/// Returns `true` if `rootDirKey` points to a per-user Windows profile directory (requiring ICE38 HKCU KeyPath & ICE64 RemoveFolder!).
inline constexpr bool IsPerUserProfileRootDir(std::string_view rootDirKey) noexcept {
    return rootDirKey == "AppDataFolder" ||
           rootDirKey == "LocalAppDataFolder" ||
           rootDirKey == "ProgramMenuFolder" ||
           rootDirKey == "ApplicationProgramsFolder" ||
           rootDirKey == "DesktopFolder";
}

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

/// Plain-English setup inclusion tiers replacing raw numeric Windows Installer `InstallLevel` mystery values!
enum class FeatureInclusionTier : int {
    Minimal  = 0,
    Typical  = 1,
    Complete = 2,
    Hidden   = 3
};

inline constexpr bool SchemaEqNoCase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        char ca = a[i];
        char cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

inline constexpr FeatureInclusionTier ResolveFeatureInclusionTier(
    std::string_view inclusionStr,
    int rawLevel = 1,
    bool hasExplicitLevel = false) noexcept
{
    if (!inclusionStr.empty()) {
        if (SchemaEqNoCase(inclusionStr, "minimal") || SchemaEqNoCase(inclusionStr, "compact")) return FeatureInclusionTier::Minimal;
        if (SchemaEqNoCase(inclusionStr, "typical") || SchemaEqNoCase(inclusionStr, "express")) return FeatureInclusionTier::Typical;
        if (SchemaEqNoCase(inclusionStr, "complete") || SchemaEqNoCase(inclusionStr, "optional")) return FeatureInclusionTier::Complete;
        if (SchemaEqNoCase(inclusionStr, "hidden") || SchemaEqNoCase(inclusionStr, "none")) return FeatureInclusionTier::Hidden;
    }
    if (hasExplicitLevel) {
        if (rawLevel <= 0)   return FeatureInclusionTier::Hidden;
        if (rawLevel <= 39)  return FeatureInclusionTier::Minimal;
        if (rawLevel <= 100) return FeatureInclusionTier::Typical;
        return FeatureInclusionTier::Complete;
    }
    return FeatureInclusionTier::Minimal;
}

inline constexpr int ResolveFeatureMsiLevel(
    std::string_view inclusionStr,
    int rawLevel = 1,
    bool hasExplicitLevel = false) noexcept
{
    if (!inclusionStr.empty()) {
        switch (ResolveFeatureInclusionTier(inclusionStr, rawLevel, false)) {
            case FeatureInclusionTier::Minimal:  return 1;
            case FeatureInclusionTier::Typical:  return 50;
            case FeatureInclusionTier::Complete: return 150;
            case FeatureInclusionTier::Hidden:   return 0;
        }
    }
    return hasExplicitLevel ? rawLevel : 1;
}

inline constexpr std::string_view FeatureInclusionToken(FeatureInclusionTier tier) noexcept {
    switch (tier) {
        case FeatureInclusionTier::Minimal:  return "minimal";
        case FeatureInclusionTier::Typical:  return "typical";
        case FeatureInclusionTier::Complete: return "complete";
        case FeatureInclusionTier::Hidden:   return "hidden";
    }
    return "minimal";
}

inline constexpr std::string_view FeatureInclusionBadge(FeatureInclusionTier tier) noexcept {
    switch (tier) {
        case FeatureInclusionTier::Minimal:  return "Minimal & All Setups";
        case FeatureInclusionTier::Typical:  return "Typical & Complete";
        case FeatureInclusionTier::Complete: return "Complete / Optional";
        case FeatureInclusionTier::Hidden:   return "Hidden / Conditional";
    }
    return "Minimal & All Setups";
}

} // namespace rsw
