# RaggieSoft Installer Studio — *Install Without Limits*

**Official Product Portal:** [https://raggiesoftstudio.com](https://raggiesoftstudio.com) *(301 target of `installwithoutlimits.com`)*  
**Developer & AI Spec Hub:** [https://raggiesoft.dev](https://raggiesoft.dev)  
**License:** MIT License (Core Toolchain Source)

**RaggieSoft Installer Studio** is a 100% first-party, zero-dependency native Win32 C++20 Windows packaging suite compiled directly with `cl.exe` and the stock Windows 11 SDK (`/MT` static binaries across `x64` and `ARM64`—zero `.NET`, zero Electron, zero third-party runtimes, zero telemetry, and zero crypto/blockchain bloat).

---

## Native C++20 Executables (`bin\arm64\` & `bin\x64\`)

| Executable | Role |
| :--- | :--- |
| **`InstallerStudio.exe`** | **7-View Native Win32 C++20 Visual IDE** for standalone `.rsip` projects & multi-project `.rsis` suites with a real-time GDI Wizard Preview Canvas and 100% lossless comment/`@include`-preserving AST. |
| **`instbuild.exe`** | **Headless CLI Compiler, JSON Linter, AI Spec & VPAT 2.5 Emitter** driving `msiquery.h` (`msi.lib`) and `fci.h` (`Cabinet.lib`) to output `.msi`, `.msm`, `.mst`, `.msp`, branded `Setup.exe`, `llms.txt`/`SKILL.md`, and HTML5 VPAT 2.5 Conformance Reports. |
| **`SetupWizard.exe`** | **White-Label Accessible Runtime Wizard** (`MsiSetInternalUI(INSTALLUILEVEL_NONE)` + `MsiSetExternalUIRecord`) with Express/Compact/Custom modes, `640x480` `16:9` Autorun mode, Dark Mode Mica, Windows Contrast Themes, and Per-Monitor V2 DPI. |
| **`msiprism.exe`** | **RaggieSoft MSI Prism** — Native 64-bit (`x64` & `ARM64`) Dark Mode SQL database inspector and `.mst`/`.msp` differ replacing legacy 32-bit `Orca.exe`. |
| **`insttests.exe`** | **Automated C++20 Verification Suite** running 20 unit and end-to-end packaging assertions on every build. |

---

## Hybrid Project Architecture (`.rsip` + `.rsis` + `.rsii`)

Just like `.NET 10` pairs single-project `.csproj` files with optional `.slnx` solutions:

1. **`.rsip` (*RaggieSoft Installer Project*)**: 100% standalone single-file manifest (like Inno Setup `.iss`). Build one app with zero multi-file ceremony:
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

## 100% Uncrippled 3-Tier Lineup

- **Community Edition (`$0`)**: Free for students, coursework, academic research, personal non-commercial hobbyware, and OSI-approved open-source projects. **100% uncrippled** (`x64` + `ARM64`, zero online login, zero telemetry, zero UI watermarks—`VERSIONINFO` and wizard footers belong 100% to `[Package] Manufacturer`).
- **Indie / Pro (`$249` Perpetual)**: Commercial closed-source software redistribution for solo developers and 1–2 seat ISVs (`100%` offline perpetual license).
- **Enterprise & Compliance (`$2,499` Team / Site)**: Multi-seat engineering teams, universities, and government agencies—includes unlimited ephemeral CI/CD build runners, formal signed **VPAT 2.5 (WCAG 2.2 AA, Revised Section 508 & ADA Title II)** documentation, GPO `.mst`/`.msp` workflows, and Net-30 PO invoicing.

---

## Quick Build & Self-Test (`Windows 11 Pro x64 / ARM64`)

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\build-and-verify.ps1
```

---

## Fictitious Persons & Entities Disclaimer

All narrative characters, bands (including *The Stardust Engine*), biographies, universities (including *Commonwealth Polytechnic Institute / CPI*, *Virginia Dominion University / VDU*, *The Forgers*, and *The Crucible*), corporations (including *OmniCorp*, *Apex Records*, *Prairie Coachworks*, and *Sainte-Claire*), transit agencies (*BRT* and *CVTA*), in-universe legal filings, and events referenced in sample content packs are works of fiction. In real life, **RaggieSoft**, **RaggieSoft Media**, **Engine Room Records**, **Ocean View Archives**, and **RaggieSoft Story Crossroads** are unincorporated creative imprints of Michael P. Ragsdale.
