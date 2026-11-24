# RaggieSoft Installer Studio — *Install Without Limits*

**Official Product Portal:** [https://raggiesoftstudio.com](https://raggiesoftstudio.com) *(301 target of `installwithoutlimits.com`)*  
**Developer & AI Spec Hub:** [https://raggiesoft.dev](https://raggiesoft.dev)  
**Current Milestone:** `v0.4.0` Open Technical Preview (`x64` & `ARM64`)  
**License:** MIT License (Core Toolchain Source)

**RaggieSoft Installer Studio** is a 100% first-party, zero-dependency native Win32 C++20 Windows packaging suite compiled directly with `cl.exe` and the stock Windows 11 SDK (`/MT` static binaries across `x64` and `ARM64`—zero `.NET`, zero WebView2, zero Electron, zero third-party runtimes, zero telemetry, and zero crypto/blockchain bloat).

---

## Native C++20 Executables (`bin\arm64\` & `bin\x64\`)

| Executable | Role |
| :--- | :--- |
| **`InstallerStudio.exe`** | **10-View Native Win32 C++20 Visual IDE + 5 Guided AeroWizards** for standalone `.rsip` projects & multi-project `.rsis` suites with a Destination Computer folder tree, Setup Design (`Features -> Components -> KeyPaths`), Runtime Prerequisites & `.MSM` Merge Modules, Dual C++ & `MSI.DLL` Dialog Designers, and a 100% comment/`@include`-preserving AST. |
| **`instbuild.exe`** | **Headless CLI Compiler, JSON Linter, AI Spec & VPAT 2.5 Emitter** driving `msiquery.h` (`msi.lib`) and `fci.h` (`Cabinet.lib`) to output `.msi`, `.msm`, `.mst`, `.msp`, LZMS-compressed `Setup.exe`, `llms.txt`/`SKILL.md`, and HTML5 VPAT 2.5 Conformance Reports. |
| **`SetupWizard.exe`** | **White-Label Accessible Runtime Wizard** (`MsiSetInternalUI(INSTALLUILEVEL_NONE)` + `MsiSetExternalUIRecord`) with Express/Compact/Custom modes, authentic Windows Installer `SelectionTree`, Pre-MSI `[Prerequisite.*]` runtime broker, `640x480` `16:9` Autorun mode, Dark Mode Mica, Windows Contrast Themes, and Per-Monitor V2 DPI. |
| **`msiprism.exe`** | **RaggieSoft MSI Prism** — Native 64-bit (`x64` & `ARM64`) Dark Mode SQL database inspector and `.mst`/`.msp` differ for `.msi`, `.msm`, and `.msp` packages. |
| **`RaggieSoftHelp.exe`** | **RaggieSoft Help Viewer** — Zero-dependency native C++20 offline documentation viewer supporting pluggable `module.rshm.json` HTML5 modules, Contents/Index/Search panes, Dark/Light/High-Contrast typography, and instant Named Pipe `F1` context navigation. |
| **`insttests.exe`** | **Automated C++20 Verification Suite** running unit, SQL, compression, accessibility, and zero-competitor documentation assertions on every build. |

---

## Hybrid Project Architecture (`.rsip` + `.rsis` + `.rsii`)

Just like `.NET 10` pairs single-project `.csproj` files with optional `.slnx` solutions:

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

## Quick Build & Self-Test (`Windows 11 Pro x64 / ARM64`)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-and-verify.ps1
```

---

## Fictitious Persons & Entities Disclaimer

All narrative characters, bands (including *The Stardust Engine*), biographies, universities (including *Commonwealth Polytechnic Institute / CPI*, *Virginia Dominion University / VDU*, *The Forgers*, and *The Crucible*), corporations (including *OmniCorp*, *Apex Records*, *Prairie Coachworks*, and *Sainte-Claire*), transit agencies (*BRT* and *CVTA*), in-universe legal filings, and events referenced in sample content packs are works of fiction. In real life, **RaggieSoft**, **RaggieSoft Media**, **Engine Room Records**, **Ocean View Archives**, and **RaggieSoft Story Crossroads** are unincorporated creative imprints of Michael P. Ragsdale.
