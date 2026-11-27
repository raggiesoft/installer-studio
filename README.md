# RaggieSoft Installer Studio — *Install Without Limits*

**Official Product Portal:** [https://raggiesoftstudio.com/installer-studio](https://raggiesoftstudio.com/installer-studio) *(301 target of `installwithoutlimits.com`)*  
**Editions & Product Matrix:** [EDITIONS_AND_RAGGIESCRIPT.md](EDITIONS_AND_RAGGIESCRIPT.md) • [https://raggiesoftstudio.com/installer-studio/editions](https://raggiesoftstudio.com/installer-studio/editions)  
**Visual Tour & Screenshots:** [https://raggiesoftstudio.com/installer-studio/screenshots](https://raggiesoftstudio.com/installer-studio/screenshots)  
**Public Engineering Roadmap:** [ROADMAP.md](ROADMAP.md) • [https://raggiesoftstudio.com/installer-studio/roadmap](https://raggiesoftstudio.com/installer-studio/roadmap)  
**Version History & Changelog:** [https://raggiesoftstudio.com/installer-studio/changelog](https://raggiesoftstudio.com/installer-studio/changelog)  
**Developer & AI Spec Hub:** [https://raggiesoft.dev](https://raggiesoft.dev)  
**Frequently Asked Questions:** [https://raggiesoftstudio.com/installer-studio/faq](https://raggiesoftstudio.com/installer-studio/faq)  
**Current Milestone:** `v0.5.0` Public Technical Preview (`v0.6.0` Staged • `x64` & `ARM64` Universal Single-Download • 11 Wizard Styles incl. `#008080` ACME Teal & `#000080` Cobalt • Signed via Microsoft Azure Artifact Signing)  
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
| **`InstallerStudio.exe`** | **12-View Tree-First Win32 C++20 Visual IDE + 5 Guided AeroWizards** for standalone `.rsip` projects & multi-project `.rsis` suites with 32-bit ARGB vector `SysTreeView32` hierarchies across a categorized sidebar, dedicated **Features** tree (`Move Up`/`Down`), **`RegEdit`-style Registry** tree (`.REG` import + Feature filter), Dual Destination Folder & Contents trees, Pre-MSI Runtimes & `.MSM` Merge Modules, **Live 10-Style C++ Wizard Designer with WCAG 2.2 AA Auto-Contrast**, `370x270` `MSI.DLL` Dialog Designer, live `ImmersiveColorSet` Light/Dark hot-swapping, and a 100% comment/`@include`-preserving AST. |
| **`instbuild.exe`** | **Headless CLI Compiler, JSON Linter (`RSW055` WCAG Contrast), AI Spec & VPAT 2.5 Emitter** driving `msiquery.h` (`msi.lib`) and `fci.h` (`Cabinet.lib`) to output Dual-Layer Authenticode-signed `.msi`, `.msm`, `.mst`, `.msp`, LZMS-compressed `Setup.exe`, Universal `x64+ARM64` EXEs, `llms.txt`/`SKILL.md`, and HTML5 VPAT 2.5 Conformance Reports. |
| **`SetupWizard.exe`** | **White-Label Accessible Runtime Wizard** (`MsiSetInternalUI(INSTALLUILEVEL_NONE)` + `MsiSetExternalUIRecord`) offering **10 selectable era-spanning Wizard Styles** (`standard` Glass, `wizard95`, `wizard97`, `aerowizard`, `fluentmica` step-rail, `oneclick`, `passivetoast`, `fullscreenblue` `#000080` Cobalt wash, `fullscreenteal` `#008080` Microsoft ACME Setup Engine callback, and `autorun16x9`), Express/Compact/Custom & Windows 95 `Typical`/`Compact`/`Complete`/`Custom` (`[SetupType.*]`) modes, `[Billboard.*]` slides, authentic Windows Installer `SelectionTree`, Pre-MSI `[Prerequisite.*]` runtime broker, live `WM_SETTINGCHANGE` Light/Dark hot-swapping, and `SPI_GETHIGHCONTRAST` override. |
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

## Official Four Product Editions (`Community == 100% Architect Parity`)

Read the full product matrix and **`RaggieScript` (`.rssc`)** language specification in **[EDITIONS_AND_RAGGIESCRIPT.md](EDITIONS_AND_RAGGIESCRIPT.md)** or on the web at **[raggiesoftstudio.com/installer-studio/editions](https://raggiesoftstudio.com/installer-studio/editions)**:

- **Community Edition *(Free for Hobbyists, Solo Creators, EDU & Open Source)***: Permanently maintains **100% technical feature parity with Architect Edition (`Community == Architect`)**—unlocking every `.MSI`, `.MSM`, `.MSP`, `.rsis` Suite, `RaggieScript` (`.rssc`), Custom Dialog Designer, VPAT 2.5 generator, and CLI command with **zero UI watermarks** (`VERSIONINFO` and wizard footers belong 100% to `[Package] Manufacturer`).
- **Essentials Edition *(Commercial Tier 1)***: Guided no-code `.MSI` + Universal `Setup.exe` (`x64` & `ARM64`), Basic Folder/`.ZIP` Self-Extractor (`PackageKind = sfx` + `%TEMP%` Software Installation wrapper & `_ISDEL.EXE` process watcher), 100% WCAG 2.2 AA runtime accessibility & Authenticode/Azure Artifact Signing (never paywalled), 6 AeroWizards, 11 Wizard Styles, Pre-MSI Runtime Prerequisites, **`.MSM` Merge Module consumption** (`[MergeModule.*]`), and **Major Upgrades** (`1.0.0 -> 1.0.1` mints a fresh `ProductCode`; does not create `.MSP` patches).
- **Developer Edition *(Commercial Tier 2)***: Everything in **Essentials** plus **`.MSM` Merge Module compilation** (`PackageKind = msm`), **`RaggieScript` (`.rssc`)** zero-MSI native scripting & full `Engine = native`, From-Scratch C++ & `370×270` `MSI.DLL` Dialog Designers, `.slnx`/VB6 `.vbp` Reg-Free COM importers, `msiprism.exe`, `RsUpdate.exe`, `--emit-winget`, and commercial CI/CD build agent rights.
- **Architect Edition *(Commercial Tier 3)***: Everything in **Developer** plus **Multi-Product Unified `.rsis` Suites** (`Showcase Studio` front-door + dynamic `ARPSYSTEMCOMPONENT` adoption), **`.MSP` binary-delta patches (`RsPatch.exe`, `--plan-update`) & `.MST` GPO transforms**, Multi-Volume Disk Spanning & `msiexec /a` Admin Images, `.rswpak` CDN feature packs, **Automated HTML5 VPAT 2.5 conformance reports (`--emit-vpat`)**, and Windows 11 Pro Sandbox `.wsb` Setup Capture (`v1.1`).
- **Standalone Self-Extractor Tool**: The Folder / `.ZIP` Self-Extractor (`PackageKind = sfx` + `%TEMP%` Software Installation wrapper) will also be available as a lightweight, standalone desktop utility (`rswsfx.exe`).

---

## Public Engineering Roadmap (`v0.5.0` → `v1.0.0 GA`)

Read the full phase-by-phase engineering plan in **[ROADMAP.md](ROADMAP.md)** or browse the interactive web edition at **[raggiesoftstudio.com/installer-studio/roadmap](https://raggiesoftstudio.com/installer-studio/roadmap)** (anchored to our **`v1.0.0 GA` North Star**: packaging and distributing 100% of RaggieSoft's own Windows software exclusively with Installer Studio):

- **`v0.5.0` – `v0.5.1`**: 10 Wizard Styles (`Wizard95`, `Wizard97`, `AeroWizard`, `FluentMica`, `OneClickSplash`, `PassiveToast`, `FullScreenBlue`, `FullScreenTeal` `#008080` ACME callback, & `Autorun16x9`), classic & custom Setup Types (`Typical`, `Compact`, `Complete`, `Custom`, & `[SetupType.*]`), configurable outer `Setup.exe` `RequestedExecutionLevel` (`asInvoker` | `highestAvailable` | `requireAdministrator`), polite `Global\_MSIExecute` cross-product queuing, and live WCAG 2.2 AA contrast enforcement.
- **`v0.6.0` *(Built & Privately Staged)***: Lightweight **Self-Extracting Archive `.EXE` Builder** (`PackageKind = sfx` + `%TEMP%` Software Installation wrapper with `_ISDEL.EXE` / child-tree completion watcher & auto-cleanup), Multi-Volume Disk Spanning (`CAB1..N.cab`), Loose Source Media (`msiexec /a`), `.rswpak` On-Demand Feature Packs, `.NET 11` runtime presets, `Engine = native`, and Unified `.rsis` Suites.
- **`v0.7.0`**: **`RaggieScript` (`.rssc` — Style A)** zero-MSI native scripting language & C++20 `RSWBC1` bytecode VM (standalone `.rssc` projects & hybrid `.rsip` + `.rssc` hooks with *Fin#*-inspired `wizard` vs. `install` phase isolation and `/DELAYLOAD:msi.dll`) + From-Scratch Dual C++ & Raw `370x270` `MSI.DLL` Visual Dialog Builders.
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
