// ================================================================================
// FILE: src/compiler/RswAiSpecEmitter.hpp
// PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
// WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
// LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)
//
// PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
//   Generates `llms.txt`, `llms-full.txt`, and `SKILL.md` (`raggiesoft-installer-studio`)
//   deterministically from `RswSchema.hpp`'s `constexpr` tables so AI coding agents
//   author, lint (`instbuild.exe --validate --diagnostics=json`), and compile
//   standalone `.rsip` projects, `.rsis` multi-project suites, and `@include` `.rsii`
//   headers with zero schema drift!
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

    // 1. `llms.txt`
    {
        std::ostringstream s;
        s << "# " << kSuiteTitle << " (" << kProductTagline << ") — v" << kVersionString << "\n\n"
          << "> First-party, zero-dependency native Win32 C++20 Windows packaging suite (`InstallerStudio.exe` Visual IDE, `instbuild.exe` CLI compiler, `SetupWizard.exe` accessible runtime wizard, and `msiprism.exe` 64-bit MSI/MST/MSP SQL inspector) targeting `x64` and `ARM64`. Official portal: https://raggiesoftstudio.com | Open-source dev hub: https://raggiesoft.dev\n\n"
          << "## Hybrid Project Architecture (`.rsip` + `.rsis` + `.rsii`)\n"
          << "- **`.rsip` (*RaggieSoft Installer Project*)**: 100% standalone single-file project manifest (like Inno Setup `.iss` or `.csproj`).\n"
          << "- **`.rsis` (*RaggieSoft Installer Suite*)**: Optional multi-project workspace (like Visual Studio `.slnx`) with `[Suite]`, `[Variable]`, and ordered `[Project.<Id>]` entries.\n"
          << "- **`.rsii` (*RaggieSoft Installer Include*)**: Optional `@include \"shared.rsii\"` header inherited losslessly across projects.\n\n"
          << "## Why Comment-Preserving `.rsip` Beats JSON & XML\n"
          << "- **Beats JSON**: No backslash double-escaping (`C:\\\\Program Files\\\\...`) and full `;` / `#` comments allowed.\n"
          << "- **Beats XML**: Zero closing-tag bloat; minimal LLM token footprint.\n"
          << "- **100% Lossless IDE Parity & White-Label Output**: `InstallerStudio.exe` preserves every comment and `@include` byte-for-byte, and Community/Indie/Enterprise builds contain zero UI watermarks (`VERSIONINFO` & wizard footers belong 100% to `[Package] Manufacturer`).\n\n"
          << "## Core Commands\n"
          << "- `instbuild.exe --validate --diagnostics=json <project.rsip|suite.rsis> [--arch=x64|arm64]`: Sub-millisecond structural, GUID, MSI 3-part version, filesystem, and `&` mnemonic linter.\n"
          << "- `instbuild.exe <project.rsip|suite.rsis> [--arch=x64|arm64] [--category=<cat>] [--project=<id>]`: Compiles standalone Enterprise `<OutputName>.msi` + accessible `<OutputName>-setup.exe` (or `<OutputName>.msm` when `PackageKind = msm`).\n"
          << "- `instbuild.exe --emit-vpat <project.rsip> <out-dir>`: Generates an audited WCAG 2.2 AA & Revised Section 508 HTML5 VPAT 2.5 Conformance Report.\n"
          << "- `instbuild.exe --emit-msm <project.rsip> <out.msm> [--arch=x64|arm64]`: Compiles a Merge Module (`.msm`).\n"
          << "- `instbuild.exe --generate-mst <base.msi> <target.msi> <out.mst>`: Generates a GPO Transform (`.mst`).\n"
          << "- `instbuild.exe --generate-msp <base.msi> <upgraded.msi> <out.msp>`: Generates a Patch Package (`.msp`).\n"
          << "- `instbuild.exe --verify-msi <file.msi>`: Runs SQL & ICE80 consistency checks on a compiled `.msi`.\n"
          << "- `instbuild.exe --emit-ai-spec <dir>`: Regenerates `llms.txt`, `llms-full.txt`, and `SKILL.md`.\n"
          << "- `msiprism.exe [file.msi|msm|msp]`: Opens any Windows Installer database in RaggieSoft MSI Prism.\n"
          << "- `<product>-setup.exe --extract-msi <dir>`: Extracts the embedded `Product.msi` for Intune / SCCM / GPO.\n\n"
          << "## `.rsip` Manifest Sections (`v" << kManifestSchemaVer << "`)\n";
        for (const auto& sd : kSectionDescriptors) {
            const char* suffix = (sd.cardinality == SectionCardinality::NamedRepeatable) ? ".<id>" : "";
            s << "- `[" << sd.nameOrPrefix << suffix << "]`: " << sd.summary << "\n";
        }
        s << "\n## Detailed Reference\n"
          << "- [llms-full.txt](llms-full.txt): Complete `constexpr` `.rsip` & `.rsis` field tables and annotated examples.\n"
          << "- [SKILL.md](SKILL.md): Autonomous AI agent authoring workflow.\n";

        std::ofstream f(outDir / "llms.txt", std::ios::binary);
        if (!f) { outError = "Could not write llms.txt"; return false; }
        const std::string text = s.str();
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    // 2. `llms-full.txt`
    {
        std::ostringstream s;
        s << "# " << kSuiteTitle << " — Complete `.rsip`, `.rsis` & `.rsii` Reference (v" << kVersionString << ")\n\n"
          << "Generated deterministically by `" << kGeneratorSignature << " --emit-ai-spec`.\n\n"
          << "## 1. Architectural Rules\n"
          << "1. **Native MSI, MSM, MST, MSP & SetupWizard.exe Output**: Calls `msiquery.h` (`msi.lib`) and `fci.h` (`Cabinet.lib`) directly from static `/MT` C++20.\n"
          << "2. **100% White-Label Attribution**: Visible dialogs and `Setup.exe` `VERSIONINFO` use `[Package] Manufacturer` and `Name` exclusively; `" << kGeneratorSignature << "` is recorded solely in MSI `SummaryInformation` `PID_APPNAME`.\n"
          << "3. **Hybrid `.rsip` + `.rsis` + `.rsii` Model**: Build a single `.rsip` standalone, share common blocks via `@include \"common.rsii\"`, or orchestrate multi-package fleets via `.rsis`.\n"
          << "4. **Deterministic SHA-1 UUIDv5 Component GUIDs**: Derived from `UpgradeCode + arch + targetRelPath`.\n"
          << "5. **True OS & Silicon Detection**: `RtlGetVersion` (`Build >= 22000`) + `IsWow64Process2` (pierces ARM64 Prism emulation).\n"
          << "6. **Lossless AST Comments**: `;` and `#` comments plus `@include` directives survive `InstallerStudio.exe` GUI edits byte-for-byte.\n\n"
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

        s << "## 3. `.rsis` Multi-Project Suite Schema (Optional — Like Visual Studio `.slnx`)\n\n"
          << "```ini\n"
          << "[Suite]\n"
          << "Name          = Acme Enterprise Suite\n"
          << "Version       = 1.0.0\n"
          << "Architectures = arm64|x64\n\n"
          << "[Variable]\n"
          << "PUBLISHER     = Acme Corporation\n\n"
          << "[Project.CoreMsm]\n"
          << "Path          = shared\\core-runtime.rsip\n"
          << "Category      = MergeModules\n"
          << "BuildOrder    = 10\n\n"
          << "[Project.DesktopApp]\n"
          << "Path          = apps\\desktop-client.rsip\n"
          << "Category      = CoreApps\n"
          << "BuildOrder    = 20\n"
          << "```\n\n"
          << "## 4. Minimal Annotated `.rsip` Project Example\n\n"
          << "```ini\n"
          << "; Sample RaggieSoft Installer Project (.rsip) — builds both x64 and arm64!\n"
          << "[Package]\n"
          << "Name              = Hello App\n"
          << "Tagline           = Fast, Accessible Native Windows Application\n"
          << "Version           = 1.0.0\n"
          << "Manufacturer      = Acme Open Source\n"
          << "UpgradeCode       = {A18F4E20-7C93-4D62-9B44-524147474945}\n"
          << "Architecture      = $(ARCH)\n"
          << "BuildBootstrapper = true\n\n"
          << "[Install]\n"
          << "DefaultSubdir     = Acme\\Hello App\n"
          << "MainExecutable    = HelloApp.exe\n\n"
          << "[UI]\n"
          << "WizardLayout      = standard\n"
          << "AccentPrimaryHex  = #2563EB\n"
          << "AccentSecondaryHex= #FBBF24\n"
          << "LicenseRtf        = License.rtf\n"
          << "InstallButtonText = &Install\n\n"
          << "[Payload.App]\n"
          << "Source            = ..\\..\\bin\\$(ARCH)\\HelloApp.exe\n\n"
          << "[Shortcut.StartMenu]\n"
          << "Name              = Hello App\n"
          << "TargetFile        = HelloApp.exe\n"
          << "Location          = StartMenu\n"
          << "Description       = Launch Hello App\n"
          << "```\n";

        std::ofstream f(outDir / "llms-full.txt", std::ios::binary);
        if (!f) { outError = "Could not write llms-full.txt"; return false; }
        const std::string text = s.str();
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    // 3. `SKILL.md`
    {
        std::ostringstream s;
        s << "---\n"
          << "name: raggiesoft-installer-studio\n"
          << "description: Author, lint, audit VPAT 2.5 accessibility, and compile Windows 11 x64 and ARM64 .msi, .msm, .mst, .msp, and Setup.exe installers using RaggieSoft Installer Studio (instbuild.exe, InstallerStudio.exe, SetupWizard.exe, msiprism.exe) and plain-text .rsip projects, .rsis suites, and .rsii includes.\n"
          << "---\n\n"
          << "# RaggieSoft Installer Studio (`.rsip` / `.rsis`) Authoring Skill\n\n"
          << "Always read [`llms-full.txt`](llms-full.txt) in this folder for the authoritative `v" << kManifestSchemaVer
          << "` `.rsip` project and `.rsis` suite grammar emitted directly by `instbuild.exe`.\n\n"
          << "## Mandatory 4-Step Agent Workflow\n\n"
          << "1. **Author Heavily Commented `.rsip` (or `.rsis` Suite) Manifests**:\n"
          << "   - Include clear `;` explanation comments above each section, use a permanent `{GUID}` for `UpgradeCode`, use 3-part `Major.Minor.Build` `Version` values (`<= 255.255.65535`), and include `&` keyboard mnemonics on custom UI labels (`InstallButtonText`, `FinishLaunchCheckbox`).\n"
          << "2. **Run Instant JSON Validation Before Compiling**:\n"
          << "   - Execute `instbuild.exe --validate --diagnostics=json <project.rsip|suite.rsis> --arch=arm64` (and `--arch=x64`) until `\"ok\": true`.\n"
          << "3. **Compile & Verify MSI / MSM / MST / MSP Integrity**:\n"
          << "   - Execute `instbuild.exe <project.rsip|suite.rsis> --arch=arm64` and `--arch=x64` (and optionally inspect SQL tables via `msiprism.exe --inspect-summary <out.msi>`).\n"
          << "4. **Optional Compliance Export**:\n"
          << "   - Execute `instbuild.exe --emit-vpat <project.rsip> <out-dir>` when generating WCAG 2.2 AA / Revised Section 508 procurement documentation.\n";

        std::ofstream f(outDir / "SKILL.md", std::ios::binary);
        if (!f) { outError = "Could not write SKILL.md"; return false; }
        const std::string text = s.str();
        f.write(text.data(), static_cast<std::streamsize>(text.size()));
    }

    return true;
}

} // namespace rsw
