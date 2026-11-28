// ================================================================================
// FILE: src/compiler/RswAiSpecEmitter.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   What does this header do, and why is it critical for AI coding agents?
//     `EmitAiSpecifications(...)` generates `llms.txt`, `llms-full.txt`, and
//     `SKILL.md` (`raggiesoft-installer-studio`) deterministically from the exact
//     C++20 `constexpr` field descriptors in `RswSchema.hpp`.
//
//     Because the compiler, validator (`--validate --diagnostics=json`), IDE, and
//     AI documentation all share one compile-time source of truth, an AI coding
//     agent can read `llms-full.txt` and author a complete, production-grade
//     `.rsip` project or `.rsis` multi-project suite from scratch with zero
//     hallucinated keys and zero schema drift!
// ================================================================================

#pragma once

#include "../common/RswSchema.hpp"
#include "../common/RswVersion.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace rsw {

inline void AppendFieldsMarkdownTable(std::ostringstream& md, std::string_view sectionPrefix) {
    md << "| Key | Type | Required | Default | Allowed / Notes |\n";
    md << "| :--- | :--- | :---: | :--- | :--- |\n";

    auto emitRow = [&](const FieldDescriptor& fd) {
        if (fd.sectionPrefix != sectionPrefix) return;
        md << "| `" << fd.key << "` | `" << ValueKindToDisplayName(fd.kind) << "` | "
           << (fd.required ? "**Yes**" : "No") << " | "
           << (fd.defaultValue.empty() ? "—" : (std::string("`") + std::string(fd.defaultValue) + "`")) << " | ";
        if (!fd.allowedValues.empty()) {
            md << "Allowed: `" << fd.allowedValues << "`. ";
        }
        md << fd.description << " |\n";
    };

    for (const auto& fd : kFieldDescriptors) emitRow(fd);
    for (const auto& fd : kExtendedFieldDescriptors) emitRow(fd);
    md << "\n";
}

inline bool EmitAiSpecifications(const std::filesystem::path& outDir, std::string& outError) {
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) {
        outError = "Failed creating output directory: " + outDir.string();
        return false;
    }

    // 1. `llms.txt` — Concise High-Signal Entrypoint for Coding Agents
    {
        std::ostringstream s;
        s << "# " << kSuiteTitle << " (" << kProductTagline << ") — v" << kVersionString << "\n\n"
          << "> First-party, zero-dependency native Win32 C++20 Windows packaging suite (`InstallerStudio.exe` 12-view Visual IDE + 6 Guided AeroWizards, `instbuild.exe` CLI compiler & JSON linter, `SetupWizard.exe` 11-style WCAG 2.2 AA runtime wizard, `msiprism.exe` 64-bit MSI/MSM/MST/MSP SQL inspector, and `RaggieSoftHelp.exe` native offline Help Viewer) targeting Windows 11 `x64` and `ARM64`. Official portal: https://raggiesoftstudio.com | Editions Matrix: https://raggiesoftstudio.com/installer-studio/editions | Open-source dev hub: https://raggiesoft.dev\n\n"
          << "## Hybrid Project Architecture (`.rsip` + `.rsis` + `.rsii` + `.rssc`)\n"
          << "- **`.rsip` (*RaggieSoft Installer Project*)**: 100% standalone single-file INI-style project manifest (similar to a standalone `.csproj`).\n"
          << "- **`.rsis` (*RaggieSoft Installer Suite*)**: Optional multi-project workspace (similar to `.slnx`) with `[Suite]`, `[Variable]`, and ordered `[Project.<Id>]` entries.\n"
          << "- **`.rsii` (*RaggieSoft Installer Include*)**: Optional `@include \"shared.rsii\"` header inherited losslessly across projects.\n"
          << "- **`.rssc` (*RaggieScript* — `v0.7.0`)**: Modern C#/TypeScript-style curly-brace native scripting language compiling to `RSWBC1` bytecode for zero-MSI setups (standalone `.rssc` or hybrid `.rsip` + `ScriptFile = setup.rssc`).\n\n"
          << "## Official Four Editions (`Community` • `Essentials` • `Developer` • `Architect`)\n"
          << "- **Community Edition (`Community == 100% Architect Parity`)**: Free for hobbyists, solo creators, open-source maintainers, students, educators, and non-profits—100% unlocked with zero watermarks (`VERSIONINFO` & wizard footers belong 100% to `[Package] Manufacturer`).\n"
          << "- **Essentials / Developer / Architect Editions**: Commercial tiers (`Essentials`: guided `.MSI` + `Setup.exe`, `.ZIP`/Folder `sfx`, WCAG 2.2 AA UI, Signing, `.MSM` consumption, & Major Upgrades; `Developer`: adds `.MSM` creation, `RaggieScript`, Custom Dialog Designers, VS/VB6 Importers, & CI/CD agents; `Architect`: adds `.rsis` Suites, `.MSP`/`.MST` deltas, Disk Spanning/`msiexec /a`, & VPAT 2.5).\n\n"
          << "## Why Comment-Preserving `.rsip` Excels for AI Coding Agents & Git\n"
          << "- **Zero Backslash Escaping**: Write Windows paths naturally (`DefaultSubdir = Sample Studio\\My App`) with full `;` and `#` inline/block comments.\n"
          << "- **Minimal Token Footprint**: Clean `[Section]` and `[Prefix.<Id>]` blocks without verbose XML closing tags.\n"
          << "- **100% Lossless IDE Parity & White-Label Output**: `InstallerStudio.exe` preserves every comment and `@include` byte-for-byte, and Community Edition maintains 100% feature parity with Architect Edition with zero UI watermarks.\n\n"
          << "## Core CLI Commands (`instbuild.exe`, `msiprism.exe`, & `RaggieSoftHelp.exe`)\n"
          << "- `instbuild.exe --validate --diagnostics=json <project.rsip|suite.rsis> [--arch=x64|arm64]`: Sub-millisecond schema, GUID, 3-part MSI version, Component rule (`RSW031`/`RSW032`), filesystem, and `&` mnemonic linter.\n"
          << "- `instbuild.exe <project.rsip|suite.rsis> [--arch=x64|arm64] [--universal] [--category=<cat>] [--project=<id>] [--with-vpat]`: Compiles standalone Enterprise `<OutputName>.msi` + LZMS-compressed `<OutputName>-setup.exe`—plus combined Universal Single-File outputs (`<UniversalOutputName>-offline-setup.exe` with x64+ARM64 in one EXE, and `<UniversalOutputName>-setup.exe` CPU-aware WinHTTP Web Stub) when `BuildUniversal = true` or `--universal` is passed.\n"
          << "- `instbuild.exe --emit-vpat <project.rsip> <out-dir>`: Generates an audited WCAG 2.2 AA & Revised Section 508 HTML5 VPAT 2.5 Conformance Report.\n"
          << "- `instbuild.exe --emit-msm <project.rsip> <out.msm> [--arch=x64|arm64]`: Compiles a 64-bit Windows Installer Merge Module (`.msm`).\n"
          << "- `instbuild.exe --generate-mst <base.msi> <target.msi> <out.mst>`: Generates a GPO Transform (`.mst`).\n"
          << "- `instbuild.exe --generate-msp <base.msi> <upgraded.msi> <out.msp>`: Generates a Patch Package (`.msp`).\n"
          << "- `instbuild.exe --verify-msi <file.msi>`: Runs SQL & ICE80 64-bit consistency checks on a compiled `.msi`.\n"
          << "- `instbuild.exe --emit-ai-spec <dir>`: Regenerates `llms.txt`, `llms-full.txt`, and `SKILL.md`.\n"
          << "- `msiprism.exe [--inspect-summary] <file.msi|msm|msp>`: Inspects any Windows Installer database in RaggieSoft MSI Prism.\n"
          << "- `RaggieSoftHelp.exe [--module raggiesoft.installer-studio] [--topic <id>]`: Opens the offline C++20 Help Viewer.\n"
          << "- `<product>-setup.exe --extract-msi <dir>`: Extracts the embedded `Product.msi` for enterprise deployment.\n\n"
          << "## `.rsip` Manifest Sections (`v" << kManifestSchemaVer << "`)\n";
        for (const auto& sd : kSectionDescriptors) {
            const char* suffix = (sd.cardinality == SectionCardinality::NamedRepeatable) ? ".<id>" : "";
            s << "- `[" << sd.nameOrPrefix << suffix << "]`: " << sd.summary << "\n";
        }
        s << "\n## Detailed Reference\n"
          << "- [llms-full.txt](llms-full.txt): Complete `constexpr` `.rsip` & `.rsis` field tables, AI Pre-Flight Checklist, and copy-paste Golden Blueprints.\n"
          << "- [SKILL.md](SKILL.md): Autonomous 4-step AI agent authoring workflow.\n";

        std::ofstream f(outDir / "llms.txt", std::ios::binary);
        if (!f) { outError = "Could not write llms.txt"; return false; }
        const std::string text = s.str();
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    // 2. `llms-full.txt` — Authoritative From-Scratch Reference & Golden Blueprints
    {
        std::ostringstream s;
        s << "# " << kSuiteTitle << " — Complete `.rsip`, `.rsis` & `.rsii` Reference (v" << kVersionString << ")\n\n"
          << "Generated deterministically by `" << kGeneratorSignature << " --emit-ai-spec`.\n\n"
          << "## 1. Architectural Rules & AI Pre-Flight Checklist\n"
          << "1. **Native MSI, MSM, MST, MSP & LZMS SetupWizard.exe Output**: Calls `msiquery.h` (`msi.lib`), `fci.h`/`fdi.h`, and `COMPRESS_ALGORITHM_LZMS` (`Cabinet.lib`) directly from static `/MT` C++20.\n"
          << "2. **100% White-Label Attribution (`Community == Architect`)**: Visible dialogs and `Setup.exe` `VERSIONINFO` use `[Package] Manufacturer` and `Name` exclusively; `" << kGeneratorSignature << "` is recorded solely in MSI `SummaryInformation` `PID_APPNAME`.\n"
          << "3. **3-Part MSI Version Rule**: `[Package] Version` MUST use 3-part `Major.Minor.Build` format (`0..255.0..255.0..65535`, e.g., `1.2.0` — never a 4-part `1.2.0.0` string).\n"
          << "4. **Uppercase Braced `UpgradeCode` GUID**: `[Package] UpgradeCode` MUST be a permanent `{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}` GUID kept identical across all releases of the same product.\n"
          << "5. **Mandatory `&` Keyboard Mnemonics**: Custom interactive labels (`[UI] InstallButtonText = &Install`, `FinishLaunchCheckbox = &Launch app`) MUST include `&` for WCAG 2.2 AA keyboard access.\n"
          << "6. **Microsoft Component Rules (`RSW031` & `RSW032`)**: Every `[Payload.*]` assigned to an explicit `[Component.<Id>]` must share one destination directory (`RSW031`), and components targeting per-user folders (`AppDataFolder`, `LocalAppDataFolder`, `StartMenu`, `Desktop`) must use `KeyPathKind = registry` (`HKCU`, `RSW032`).\n"
          << "7. **Pre-MSI `[Prerequisite.<Id>]` Execution**: Runtimes (`DotNet11Desktop`, `DotNet11AspNet`, `DotNet11Runtime`, `DotNet10Desktop`, `DotNet10AspNet`, `DotNet10Runtime`, `DotNet9Desktop`, `DotNet8Desktop`, `EdgeWebView2`, `VcRedist2022`, or `Custom`) execute BEFORE `msiexec` under a single UAC prompt (`download`, `embedded` LZMS, or `side-by-side` `Redist\\`) and latch `RebootExitCodes` (`3010, 1641, 23`).\n"
          << "8. **64-Bit Silicon & `PID_TEMPLATE` Rules (`x64;1033` vs `Arm64;1033` — Zero `IA-64` / `x86` Host Builds)**: Host toolchain & outer Universal `Setup.exe` stubs are strictly 64-bit (`IMAGE_FILE_MACHINE_AMD64` `0x8664` outer stub using `IsWow64Process2` to pierce Windows 11 Prism and dispatch native `ARM64` `0xAA64` vs `x64` `0x8664`). MSI `SummaryInformation` `PID_TEMPLATE` MUST strictly use `x64;1033` (AMD64/Intel 64-bit) or `Arm64;1033` (64-bit ARM)—NEVER `Intel64;1033` (deceased IA-64 Itanium!).\n"
          << "9. **Multi-Engine Packaging (`v0.8` `PackageKind = msi|msm|sfx|suite`, `Engine = msi|raggiescript`, `RsPatch.exe` & `RsUpdate.exe`)**: Build enterprise `.msi` databases (including `[Upgrade]` Major/Minor/Small servicing, `[Media]` multi-volume `CAB1.cab..CABN.cab` spanning, `Compression = none` loose source media, `msiexec /a` administrative installs, and `.rswpak` `Delivery = ondemand` feature packs), lightweight self-extracting archives (`PackageKind = sfx` with selectable UAC `RequestedExecutionLevel`), zero-MSI **RaggieScript EXE** (`Engine = raggiescript`) direct transactional setups with retired-file pruning & `RsPatch.exe` `msdelta.dll` `.rswpatch` deltas, `RsUpdate.exe` DRY `apps-manifest.json` client updates, or Unified Suite setups (`PackageKind = suite` with dynamic `ARPSYSTEMCOMPONENT` adoption).\n"
          << "10. **Lossless AST Comments**: `;` and `#` comments plus `@include` directives survive `InstallerStudio.exe` GUI edits byte-for-byte.\n\n"
          << "## 2. Complete `.rsip` Project Schema (`v" << kManifestSchemaVer << "`)\n\n";

        for (const auto& sd : kSectionDescriptors) {
            const char* suffix = (sd.cardinality == SectionCardinality::NamedRepeatable) ? ".<id>" : "";
            const char* reqTag = (sd.cardinality == SectionCardinality::SingletonRequired || sd.atLeastOneRequired)
                ? "Required"
                : "Optional";
            s << "### `[" << sd.nameOrPrefix << suffix << "]` (*" << reqTag << "*)\n"
              << sd.summary << "\n\n";
            AppendFieldsMarkdownTable(s, sd.nameOrPrefix);
        }

        s << "## 3. `.rsis` Multi-Project Suite Schema (Optional — Multi-Package Workspace)\n\n"
          << "```ini\n"
          << "[Suite]\n"
          << "Name          = Sample Studio Workspace\n"
          << "Version       = 1.0.0\n"
          << "Architectures = arm64|x64\n\n"
          << "[Variable]\n"
          << "PUBLISHER     = Sample Studio\n\n"
          << "[Project.CoreMsm]\n"
          << "Path          = shared\\core-runtime.rsip\n"
          << "Category      = MergeModules\n"
          << "BuildOrder    = 10\n\n"
          << "[Project.DesktopApp]\n"
          << "Path          = apps\\desktop-client.rsip\n"
          << "Category      = CoreApps\n"
          << "BuildOrder    = 20\n"
          << "```\n\n"
          << "## 4. Minimal From-Scratch `.rsip` Project Blueprint\n\n"
          << "```ini\n"
          << "; Minimal standalone RaggieSoft Installer Project (.rsip) — compiles x64 and arm64!\n"
          << "[Package]\n"
          << "Name              = Hello Studio App\n"
          << "Tagline           = Fast, Accessible Native Windows Application\n"
          << "Version           = 1.0.0\n"
          << "Manufacturer      = Sample Studio\n"
          << "UpgradeCode       = {A18F4E20-7C93-4D62-9B44-524147474945}\n"
          << "Architecture      = $(ARCH)\n"
          << "Scope             = machine\n"
          << "BuildBootstrapper = true\n\n"
          << "[Install]\n"
          << "DefaultSubdir     = Sample Studio\\Hello App\n"
          << "MainExecutable    = HelloApp.exe\n\n"
          << "[UI]\n"
          << "WizardLayout      = aerowizard\n"
          << "AccentPrimaryHex  = #2563EB\n"
          << "AccentSecondaryHex= #FBBF24\n"
          << "InstallButtonText = &Install\n\n"
          << "[Payload.App]\n"
          << "Source            = ..\\..\\bin\\$(ARCH)\\HelloApp.exe\n\n"
          << "[Shortcut.StartMenu]\n"
          << "Name              = Hello Studio App\n"
          << "TargetFile        = HelloApp.exe\n"
          << "Location          = StartMenu\n"
          << "Description       = Launch Hello Studio App\n"
          << "```\n\n"
          << "## 5. Complete Enterprise `.rsip` Blueprint (SelectionTree Features, Pre-MSI Runtimes, `.MSM`, FileAssoc & Custom MSI Dialog)\n\n"
          << "```ini\n"
          << "; Full-featured enterprise .rsip demonstrating SelectionTree hierarchy, Pre-MSI\n"
          << "; Prerequisites, 64-bit .MSM Merge Modules, File Associations, & Custom MSI SQL Dialogs.\n"
          << "[Package]\n"
          << "Name              = Studio Workbench\n"
          << "Tagline           = Accessible Desktop Authoring Environment\n"
          << "WebsiteLabel      = raggiesoftstudio.com\n"
          << "Version           = 2.1.0\n"
          << "Manufacturer      = RaggieSoft\n"
          << "UpgradeCode       = {7F4E91C2-58B4-4D21-8E77-314159265358}\n"
          << "Architecture      = $(ARCH)\n"
          << "Scope             = machine\n"
          << "OutputName        = studio-workbench-$(ARCH)\n"
          << "OutputDir         = out\n"
          << "BuildBootstrapper = true\n\n"
          << "[Requirements]\n"
          << "MinWindowsBuild   = 22000\n\n"
          << "[Install]\n"
          << "DefaultSubdir     = RaggieSoft\\Studio Workbench\n"
          << "StartMenuFolder   = RaggieSoft Studio Workbench\n"
          << "MainExecutable    = Workbench.exe\n\n"
          << "[UI]\n"
          << "WizardLayout          = aerowizard\n"
          << "MsiDialogPreset       = express_custom\n"
          << "AccentPrimaryHex      = #2563EB\n"
          << "AccentSecondaryHex    = #FBBF24\n"
          << "LicenseRtf            = License.rtf\n"
          << "InstallButtonText     = &Install\n"
          << "FinishLaunchCheckbox  = &Launch Studio Workbench when setup finishes\n\n"
          << "; Hierarchical MSI SelectionTree Features (Core locked via DisallowAbsent = true)\n"
          << "[Feature.Main]\n"
          << "Title                 = Core Workbench Application\n"
          << "Description           = Primary executable and core authoring engine.\n"
          << "Level                 = 1\n"
          << "DisallowAbsent        = true\n\n"
          << "[Feature.Templates]\n"
          << "Parent                = Main\n"
          << "Title                 = Starter Project Templates\n"
          << "Description           = Optional starter blueprints and offline documentation.\n"
          << "Level                 = 1\n\n"
          << "; Pre-MSI Runtime Prerequisite (runs BEFORE msiexec under one UAC prompt)\n"
          << "[Prerequisite.DotNet10]\n"
          << "Preset                = DotNet10Desktop\n"
          << "Distribution          = download\n"
          << "RebootExitCodes       = 3010, 1641, 23\n\n"
          << "; Optional 64-Bit Merge Module (.msm) merged natively into Product.msi\n"
          << "[MergeModule.SharedCodec]\n"
          << "Source                = ..\\shared\\out\\shared-codec-$(ARCH).msm\n"
          << "Feature               = Main\n"
          << "TargetRoot            = INSTALLDIR\n\n"
          << "[Component.CoreExeComp]\n"
          << "Feature               = Main\n"
          << "TargetRoot            = INSTALLDIR\n"
          << "Guid                  = *\n"
          << "KeyPathKind           = file\n"
          << "KeyPathTarget         = Workbench.exe\n\n"
          << "[Payload.MainExe]\n"
          << "Source                = ..\\bin\\$(ARCH)\\Workbench.exe\n"
          << "Component             = CoreExeComp\n\n"
          << "[Payload.TemplatesDir]\n"
          << "Source                = ..\\templates\n"
          << "TargetRoot            = INSTALLDIR\n"
          << "TargetSubdir          = templates\n"
          << "Feature               = Templates\n\n"
          << "[FileAssociation.WorkbenchDoc]\n"
          << "Extension             = .rswb\n"
          << "ProgId                = RaggieSoft.Workbench.Document\n"
          << "Description           = Studio Workbench Project\n"
          << "TargetFile            = Workbench.exe\n"
          << "Verbs                 = open:&Open in Workbench:\"%1\"\n"
          << "Advertise             = false\n\n"
          << "[Shortcut.StartMenuApp]\n"
          << "Name                  = Studio Workbench\n"
          << "TargetFile            = Workbench.exe\n"
          << "Location              = StartMenu\n\n"
          << "[MsiDialog.WorkspaceUrlDlg]\n"
          << "HeaderTitle           = Default Asset Endpoint\n"
          << "PrevDialog            = SetupTypeDlg\n"
          << "NextDialog            = VerifyReadyDlg\n\n"
          << "[MsiControl.EndpointEdit]\n"
          << "Dialog                = WorkspaceUrlDlg\n"
          << "Type                  = Edit\n"
          << "X                     = 20\n"
          << "Y                     = 74\n"
          << "Width                 = 330\n"
          << "Height                = 18\n"
          << "Property              = ASSET_ENDPOINT\n"
          << "DefaultValue          = https://assets.raggiesoft.com\n"
          << "```\n";

        std::ofstream f(outDir / "llms-full.txt", std::ios::binary);
        if (!f) { outError = "Could not write llms-full.txt"; return false; }
        const std::string text = s.str();
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    // 3. `SKILL.md` — Autonomous Agent Skill Definition
    {
        std::ostringstream s;
        s << "---\n"
          << "name: raggiesoft-installer-studio\n"
          << "description: Author, lint, audit VPAT 2.5 accessibility, and compile Windows 11 x64 and ARM64 .msi, .msm, .mst, .msp, and Setup.exe installers using RaggieSoft Installer Studio (instbuild.exe, InstallerStudio.exe, SetupWizard.exe, msiprism.exe, RaggieSoftHelp.exe) and plain-text .rsip projects, .rsis suites, and .rsii includes.\n"
          << "---\n\n"
          << "# RaggieSoft Installer Studio (`.rsip` / `.rsis`) Authoring Skill\n\n"
          << "Always read [`llms-full.txt`](llms-full.txt) in this folder for the authoritative `v" << kManifestSchemaVer
          << "` `.rsip` project and `.rsis` suite grammar, AI Pre-Flight Checklist, and copy-paste blueprints emitted directly by `instbuild.exe`.\n\n"
          << "## Mandatory 4-Step Agent Workflow\n\n"
          << "1. **Author Heavily Commented `.rsip` (or `.rsis` Suite) Manifests from Scratch**:\n"
          << "   - Include clear `;` explanation comments above each section, use a permanent `{GUID}` for `UpgradeCode`, use 3-part `Major.Minor.Build` `Version` values (`<= 255.255.65535`), use `$(ARCH)` in multi-architecture payload paths, and include `&` keyboard mnemonics on custom UI labels (`InstallButtonText`, `FinishLaunchCheckbox`).\n"
          << "   - Declare `[Prerequisite.<Id>]` for any required `.NET`, `WebView2`, `VC++`, or custom runtimes so `SetupWizard.exe` runs them before `msiexec` and latches `RebootExitCodes`.\n"
          << "2. **Run Instant JSON Validation Before Compiling**:\n"
          << "   - Execute `instbuild.exe --validate --diagnostics=json <project.rsip|suite.rsis> --arch=arm64` (and `--arch=x64`) until `\"ok\": true` (`0` errors).\n"
          << "3. **Compile & Verify MSI / MSM / MST / MSP & Universal Installers**:\n"
          << "   - Execute `instbuild.exe <project.rsip|suite.rsis> --arch=arm64` and `--arch=x64` (or set `BuildUniversal = true` / pass `--universal` to emit `<stem>-offline-setup.exe` bundling both architectures in one EXE plus `<stem>-setup.exe` CPU-aware smart web stub, and optionally inspect SQL tables via `msiprism.exe --inspect-summary <out.msi>`).\n"
          << "4. **Optional Compliance Export**:\n"
          << "   - Execute `instbuild.exe --emit-vpat <project.rsip> <out-dir>` (or pass `--with-vpat`) when generating WCAG 2.2 AA / Revised Section 508 procurement documentation.\n\n"
          << "## Permanent Release & Website Screenshot Rule\n\n"
          << "- **Standing Rule**: Screenshots on `https://raggiesoftstudio.com/screenshots` MUST ALWAYS reflect the most recently released version of RaggieSoft Installer Studio.\n"
          << "- Whenever shipping a new release of `desktop-apps/installer-studio/`, ALWAYS run `powershell -NoProfile -ExecutionPolicy Bypass -File .\\export-all-screenshots.ps1` after compiling so 100% of `SetupWizard.exe`, `InstallerStudio.exe`, `msiprism.exe`, and `RaggieSoftHelp.exe` PNG captures match the newly released version before building and deploying `raggiesoftstudio.com`.\n";

        std::ofstream f(outDir / "SKILL.md", std::ios::binary);
        if (!f) { outError = "Could not write SKILL.md"; return false; }
        const std::string text = s.str();
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    return true;
}

} // namespace rsw
