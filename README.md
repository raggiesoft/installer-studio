# RaggieSoft Installer Studio — *Install Without Limits*

**Official Product Portal:** [https://raggiesoftstudio.com](https://raggiesoftstudio.com) *(301 target of `installwithoutlimits.com`)*  
**Visual Tour & Screenshots:** [https://raggiesoftstudio.com/screenshots](https://raggiesoftstudio.com/screenshots)  
**Public Engineering Roadmap:** [ROADMAP.md](ROADMAP.md) • [https://raggiesoftstudio.com/roadmap](https://raggiesoftstudio.com/roadmap)  
**Version History & Changelog:** [https://raggiesoftstudio.com/changelog](https://raggiesoftstudio.com/changelog)  
**Developer & AI Spec Hub:** [https://raggiesoft.dev](https://raggiesoft.dev)  
**Frequently Asked Questions:** [https://raggiesoftstudio.com/faq](https://raggiesoftstudio.com/faq)  
**Current Milestone:** `v0.4.5` Open Technical Preview (`x64` & `ARM64` Universal Single-Download • Signed via Microsoft Azure Artifact Signing)  
**License:** MIT License (Core Toolchain Source)

<p align="center">
  <a href="https://raggiesoftstudio.com">
    <picture>
      <source media="(prefers-color-scheme: dark)" srcset="https://assets.raggiesoft.com/desktop-apps/windows/apps/installer-studio/screenshots/setupwizard-setup-type.png">
      <source media="(prefers-color-scheme: light)" srcset="https://assets.raggiesoft.com/desktop-apps/windows/apps/installer-studio/screenshots/setupwizard-setup-type-light.png">
      <img alt="RaggieSoft Installer Studio SetupWizard.exe running natively on Windows 11 with automatic Dark and Light Mode support" src="https://assets.raggiesoft.com/desktop-apps/windows/apps/installer-studio/screenshots/setupwizard-setup-type.png" width="640">
    </picture>
  </a>
</p>

**RaggieSoft Installer Studio** is a 100% first-party, zero-dependency native Win32 C++20 Windows packaging suite compiled directly with `cl.exe` and the stock Windows 11 SDK (`/MT` static binaries across `x64` and `ARM64`—zero `.NET`, zero WebView2, zero Electron, zero third-party runtimes, zero telemetry, and zero crypto/blockchain bloat).

---

## Why RaggieSoft Installer Studio Exists (28 Years in the Making, 1998–2026)

Since writing my first Visual Basic 6.0 application in 1998, I spent 28 years searching for the right Windows installation tool—shipping software across more than a dozen packaging engines spanning every era of Windows deployment:

- **Late-1990s Bundled IDE Wizards**: Starting with the limited-edition setup wizard bundled inside the Visual Studio 6.0 box and classic 1990s procedural script installers.
- **3D-Beveled Suites & CD-ROM Multimedia Builders**: Commercial rapid-packaging suites and interactive CD-ROM autorun studios *(yes, at one point I even tried bending an interactive multimedia autorun menu builder into an application installer!)*.
- **Scriptable Open-Source Packagers**: Lightweight text-scriptable installers that nailed small binary size and version control, yet lacked native relational Windows Installer (`.msi`) databases.
- **Enterprise Relational MSI & XML Suites**: Heavyweight commercial workstations and verbose XML toolchains that exposed `msi.dll` power at the cost of multi-gigabyte installs, 32-bit legacy debt, opaque binary forms, or broken Windows 11 Dark Mode and screen-reader accessibility.

**RaggieSoft Installer Studio** takes everything those eras got right—plain-text diffable manifests, authentic relational `.msi` compliance, single-file `LZMS` solid compression, pre-MSI runtime chaining, and classic-to-modern wizard ergonomics—and rebuilds it from first principles in static `/MT` Win32 C++20 for native `x64` and `ARM64` Windows 11.

---

## Native C++20 Executables (`bin\arm64\` & `bin\x64\`)

| Executable | Role |
| :--- | :--- |
| **`InstallerStudio.exe`** | **12-View Tree-First Win32 C++20 Visual IDE + 5 Guided AeroWizards** for standalone `.rsip` projects & multi-project `.rsis` suites with 32-bit ARGB vector `SysTreeView32` hierarchies across a categorized sidebar, dedicated **Features** tree (`Move Up`/`Down`), **`RegEdit`-style Registry** tree (`.REG` import + Feature filter), Dual Destination Folder & Contents trees, Pre-MSI Runtimes & `.MSM` Merge Modules, Dual C++ & `MSI.DLL` Dialog Designers, live `ImmersiveColorSet` Light/Dark hot-swapping, and a 100% comment/`@include`-preserving AST. |
| **`instbuild.exe`** | **Headless CLI Compiler, JSON Linter, AI Spec & VPAT 2.5 Emitter** driving `msiquery.h` (`msi.lib`) and `fci.h` (`Cabinet.lib`) to output `.msi`, `.msm`, `.mst`, `.msp`, LZMS-compressed `Setup.exe`, `llms.txt`/`SKILL.md`, and HTML5 VPAT 2.5 Conformance Reports. |
| **`SetupWizard.exe`** | **White-Label Accessible Runtime Wizard** (`MsiSetInternalUI(INSTALLUILEVEL_NONE)` + `MsiSetExternalUIRecord`) with Express/Compact/Custom modes, authentic Windows Installer `SelectionTree`, Pre-MSI `[Prerequisite.*]` runtime broker, `640x480` `16:9` Autorun mode, custom-subclassed Dark Mode buttons & checkboxes, live `WM_SETTINGCHANGE` Light/Dark hot-swapping, Windows Contrast Themes, and Per-Monitor V2 DPI. |
| **`msiprism.exe`** | **RaggieSoft MSI Prism** — Native 64-bit (`x64` & `ARM64`) Dark Mode SQL database inspector and `.mst`/`.msp` differ for `.msi`, `.msm`, and `.msp` packages. |
| **`RaggieSoftHelp.exe`** | **RaggieSoft Help Viewer** — Zero-dependency native C++20 offline documentation viewer supporting pluggable `module.rshm.json` HTML5 modules, Contents/Index/Search panes, Dark/Light/High-Contrast typography, and instant Named Pipe `F1` context navigation. |
| **`insttests.exe`** | **Automated C++20 Verification Suite** running unit, SQL, compression, accessibility, live theme broadcast, and documentation integrity assertions on every build. |

---

## Hybrid Project Architecture (`.rsip` + `.rsis` + `.rsii`)

Just like `.NET` pairs single-project `.csproj` files with optional `.slnx` solutions:

1. **`.rsip` (*RaggieSoft Installer Project*)**: 100% standalone single-file manifest. Build one app from scratch with zero multi-file ceremony:
   ```powershell
   .\bin\arm64\instbuild.exe .\examples\hello-raggiesoft\hello-raggiesoft.rsip --arch=arm64
   ```
2. **`.rsis` (*RaggieSoft Installer Suite*)**: Optional multi-project workspace grouping dozens of `.rsip` packages with `[Variable]` tokens and deterministic `BuildOrder` (e.g., `.msm` Merge Modules before `.msi` apps):
   ```powershell
   .\bin\arm64\instbuild.exe .\RaggieSoft.rsis --validate
   .\bin\arm64\instbuild.exe .\RaggieSoft.rsis --project=installer-studio --with-vpat
   ```
3. **`.rsii` (*RaggieSoft Installer Include*)**: Optional `@include "shared.rsii"` header inherited across projects without copy-pasting `[Signing]` or publisher metadata.

---

## Product Editions (`Community == 100% Premier Parity`)

Commercial pricing is intentionally omitted while **RaggieSoft Installer Studio** is in active Technical Preview. The IDE models four edition badges (`Community`, `Express`, `Professional`, and `Premier`) with a permanent architectural rule:

- **Community Edition**: Permanently maintains **100% technical feature parity with Premier Edition**—every MSI table, Pre-MSI Runtime Prerequisite, `.MSM` Merge Module, AeroWizard, VPAT 2.5 generator, and CLI command is unlocked with zero UI watermarks (`VERSIONINFO` and wizard footers belong 100% to `[Package] Manufacturer`).
- **Express / Professional / Premier**: Placeholder organizational tiers reserved for future commercial licensing once dogfood validation completes.

---

## Public Engineering Roadmap (`v0.5.0` → `v1.0.0 GA`)

Read the full phase-by-phase engineering plan in **[ROADMAP.md](ROADMAP.md)** or browse the interactive web edition at **[raggiesoftstudio.com/roadmap](https://raggiesoftstudio.com/roadmap)** (anchored to our **`v1.0.0 GA` North Star**: packaging and distributing 100% of RaggieSoft's own Windows software exclusively with Installer Studio):

- **`v0.5.0`**: 8 Wizard Layout Variations (`Wizard95`, `Wizard97`, `AeroWizard`, `FluentMica`, `OneClickSplash`, `PassiveToast`, `FullScreenBlue` with rotating billboards & docked 3D progress gauge, & `Autorun16x9`), classic Windows 95-style & custom author-defined Setup Types (`Typical`, `Compact`, `Complete`, `Custom`, & `[SetupType.*]`), automatic `SPI_GETHIGHCONTRAST` contrast-theme override, complete `[Branding]` / `.rswskin` imagery, and live WCAG 2.2 AA contrast enforcement.
- **`v0.6.0`**: Lightweight **Self-Extracting Archive `.EXE` Builder** (`PackageKind = sfx` — folder/`.zip` to Dark-Mode `LZMS` `.exe` that unpacks to a chosen folder and optionally runs a bundled `.exe` or opens a document in the user's default system viewer), granular `.rswpak` On-Demand Feature Packs, curated `.NET 11` + `.NET 10 LTS` runtime presets, and `Engine = native`.
- **`v0.7.0`**: From-scratch Dual C++ & Raw `370x270` `MSI.DLL` Dialog Builders + `.rssc` event hooks & `RsDialog` (`Rs*()`) procedural dialog scripting.
- **`v0.8.0`**: Visual Studio `.slnx`/`.csproj`, Visual Basic 6.0 `.vbp` (Windows 11 WRP + Reg-Free COM for *StoreTrainer*), and `.msi` importers + Windows Restart Manager (`Rstrtmgr.dll`).
- **`v0.9.0`**: Authenticode & Azure Artifact Signing Studio, white-label `RsUpdate.exe` auto-updater, and guided Microsoft-compliant `.MSP` servicing (`--plan-update`).
- **`v1.0.0 GA`**: GitHub Actions / Azure DevOps tasks, `--emit-winget`, and 100% RaggieSoft production cutover across all apps, interactive books, and supporter audio archives *(followed by `v1.1+` Windows Sandbox `.wsb` Setup Capture, `.vsix`, Services/Firewall/Config Patchers, `.MSIXBUNDLE`, `I18N` + RTL, and `--emit-sbom`)*.

---

## Quick Build & Self-Test (`Windows 11 Pro x64 / ARM64`)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-and-verify.ps1
```

---

## Fictitious Persons & Entities Disclaimer

All narrative characters, bands (including *The Stardust Engine*), biographies, universities (including *Commonwealth Polytechnic Institute / CPI*, *Virginia Dominion University / VDU*, *The Forgers*, and *The Crucible*), corporations (including *OmniCorp*, *Apex Records*, *Prairie Coachworks*, and *Sainte-Claire*), transit agencies (*BRT* and *CVTA*), in-universe legal filings, and events referenced in sample content packs are works of fiction. In real life, **RaggieSoft**, **RaggieSoft Media**, **Engine Room Records**, **Ocean View Archives**, and **RaggieSoft Story Crossroads** are unincorporated creative imprints of Michael P. Ragsdale.
