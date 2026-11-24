# RaggieSoft Installer Studio — Engineering Roadmap & Milestone Log

**Product:** RaggieSoft Installer Studio (*Install Without Limits*)  
**Portal:** [https://raggiesoftstudio.com](https://raggiesoftstudio.com) • **Developer Hub:** [https://raggiesoft.dev](https://raggiesoft.dev)  
**Core Pledge:** Zero third-party runtime dependencies (`/MT` Win32 C++20 for `x64` & `ARM64`), 100% WCAG 2.2 Level AA accessibility, first-class AI coding agent support (`llms.txt` + `SKILL.md`), zero competitor mentions, and **Community Edition maintaining 100% technical feature parity with Premier Edition**.

---

## Shipped Milestones (`v0.1.0` – `v0.4.1`)

- **`v0.4.1` — Dark-Mode Push Button Subclassing & Live `ImmersiveColorSet` Palette Hot-Swapping**:
  - Added `AttachAccessiblePushButtonSubclass` (`SetWindowSubclass` + GDI+ on genuine `BS_PUSHBUTTON` / `BS_DEFPUSHBUTTON` controls with `BCM_SETSHIELD` UAC shield rendering, Royal Cobalt primary fills, Slate-Navy secondary fills, and `SPI_GETHIGHCONTRAST` fallback) plus `ApplyDarkComboBoxTheme` (`DarkMode_CFD`) across `SetupWizard.exe`, `InstallerStudio.exe`, `msiprism.exe`, and `RaggieSoftHelp.exe`—eliminating `ComCtl32 v6`'s bright `#FFFFFF` light-mode push buttons in Dark Mode while keeping 100% native UI Automation `Button` semantics.
  - Wired `IsLiveThemeBroadcast` (`WM_SETTINGCHANGE` `lParam == L"ImmersiveColorSet"`, `SPI_SETHIGHCONTRAST`, `WM_SYSCOLORCHANGE`, and `WM_THEMECHANGED`) + `RedrawWindow(..., RDW_ALLCHILDREN)` across all four GUI executables so toggling Windows 11 between Light Mode, Dark Mode, and High Contrast flips the entire open window live in `< 16ms` without restarting.
- **`v0.4.0` — Native C++20 Help Viewer, Dark-Mode Control Polish, Screenshot Exporter & Standalone Identity**:
  - Added **`RaggieSoftHelp.exe`**, a static `/MT` native Win32 C++20 offline documentation viewer (`x64` & `ARM64`, zero `.NET` and zero `WebView2` dependencies) supporting pluggable `module.rshm.json` manifests, Contents / Index / Search navigation, in-process HTML5-to-RTF typography (`Msftedit.dll`), and instant single-instance `F1` Named Pipe IPC (`\\.\pipe\RaggieSoft.HelpViewer.IPC.<User>`).
  - Authored the 8-chapter `raggiesoft.installer-studio` offline HTML5 help module and wired `F1` context navigation across all 10 views of `InstallerStudio.exe`.
  - Added shared Win32 Dark Mode subclasses (`AttachAccessibleCheckboxSubclass`, `AttachDarkAwareListViewSubclass`, and `ApplyDarkScrollbarTheme`) across `InstallerStudio.exe`, `SetupWizard.exe`, `msiprism.exe`, and `RaggieSoftHelp.exe` so checkboxes and `SysListView32` column headers never render black-on-dark text or bright white strips.
  - Added `--export-screenshots` WIC PNG window capture, published the `/changelog` Version History and live UI gallery on `https://raggiesoftstudio.com`, and enforced automated zero-competitor-mention checks across all emitted AI specifications.
- **`v0.3.0` — Editions (`Community == Premier`), LZMS Solid Compression, Pre-MSI Runtimes, `.MSM` & `SelectionTree`**:
  - Introduced `Community`, `Express`, `Professional`, and `Premier` edition badges (`Community == 100% Premier` capability).
  - Added Windows `COMPRESS_ALGORITHM_LZMS` (`Cabinet.lib`) solid payload compression inside `Setup.exe`.
  - Built the unlimited Pre-MSI `[Prerequisite.<Id>]` runtime broker (`WinHTTP` download, `LZMS` offline embed, and `Redist\`) running before `msiexec` under one UAC prompt and latching standardized or custom `RebootExitCodes` (`3010, 1641, 23, ...`).
  - Added native 64-bit `[MergeModule.<Id>]` (`.msm`) SQL + CAB merging (`Cabinet.lib` FDI, zero 32-bit `mergemod.dll` dependency).
  - Added the authentic Windows Installer `SelectionTree` (`WizardPage::CustomFeatures`) with 32-bit ARGB split-dropdown icons, live byte costing, `DisallowAbsent` locks, and `MsiFormatRecordW` status formatting (eliminating unexpanded `[1]` strings).
- **`v0.2.0` & `v0.2.1` — 10-View AeroWizard IDE, Microsoft Component Rules & UAC Broker**:
  - Added the Destination Computer folder tree, Setup Design (`Features -> Components -> KeyPaths` enforcing `ICE18`, `ICE38`, `ICE64`, and `ICE80`), File Associations, Dual C++ & `370x270` `MSI.DLL` Dialog Designers, 5 native Win32 AeroWizards, Dark Mode checkbox subclassing, Wizard97 footer `< Back` navigation, `Signature`/`AppSearch` SQL tables, and the `--elevated-msi-worker` UAC broker.
- **`v0.1.0` — Initial Native C++20 Vertical Slice**:
  - Shipped `instbuild.exe`, `SetupWizard.exe`, `InstallerStudio.exe`, `msiprism.exe`, and `insttests.exe` with deterministic SHA-1 UUIDv5 Component GUIDs, `RtlGetVersion` + `IsWow64Process2` silicon detection, `--emit-ai-spec`, and `--emit-vpat`.

---

## Upcoming Engineering Phases

### Phase B (`v0.5.0`) — Authenticode Signing Studio, Skinning (`Wizard95` & True `AeroWizard`), Custom Dialogs & `RsDialog` Scripting
1. **First-Class Authenticode Signing UX (Ready for Azure Artifact Signing & Cert Stores)**:
   - Add an interactive Windows Certificate Store picker (`CryptUIDlgSelectCertificateFromStore`), local `.pfx` / hardware token configuration, Azure Artifact Signing (`dlib` + `MetadataJson`) profile tester, and one-click Dual-Layer Signature Verifier (`WinVerifyTrust` on both inner `Product.msi` and outer `Setup.exe`) in View 9 of `InstallerStudio.exe`.
2. **Installer Skinning Engine (`.rswskin`) + `Wizard95` & Pixel-Perfect `AeroWizard` Chrome**:
   - **Theme & Skinning System**: Customizable `[Skin]` / `.rswskin` palettes, typography, card/button chrome, and sidebars with live WCAG contrast verification.
   - **Retro `Wizard95` Mode (`WizardLayout = wizard95`)**: Classic Windows 95 / NT 4.0 3D beveled dialog geometry (`#C0C0C0` surface, sunken etched dividers, `164x314` left bitmap rail, and classic `< Back` / `Next >` / `Cancel` bevel buttons—ideal for classic utilities like *StoreTrainer*).
   - **True `AeroWizard` Circular Back Button (`WizardLayout = aerowizard`)**: Replace rectangular button fallback with a `SetWindowSubclass` + GDI+ vector `30x30` circular `<-` AeroWizard glyph (normal, hover, pressed, disabled, and keyboard focus states + UI Automation `"Back"` label) in the top-left command area, with author choice between top-left Aero `<-` or bottom-right `< Back`.
3. **WYSIWYG Custom Dialog Designer & `.rssc` Scripting with the `RsDialog` (`Rs*()`) Function Family**:
   - **Full Freeform Custom Dialogs (`[Dialog.<Id>]`)**: Visual drag-and-drop canvas in `InstallerStudio.exe` for building arbitrary custom wizard pages (text inputs, path pickers, checkboxes, radio groups, combo boxes, listviews, banners, and conditional visibility) in `SetupWizard.exe` alongside `370x270` `MSI.DLL` SQL tables.
   - **`RsDialog` Standard Library + `.rssc` Bytecode Engine**: Combine event hooks (`OnBegin`, `OnPrerequisitesComplete`, `OnFeatureSelected`, `OnBeforeInstall`, `OnAfterInstall`), `wizard { step }` state-machine control, `RsShowDialog("<CustomId>")`, and our first-party **`RsDialog`** procedural dialog family (`RsWelcome()`, `RsLicense()`, `RsSetupType()`, `RsFeatureTree()`, `RsAskDestPath()`, `RsAskText()`, `RsAskOptions()`, `RsStartCopy()`, `RsFinish()`, `RsFinishReboot()`).
4. **Optional Web Media Blocks (`.rswpak`)**:
   - Support splitting large multi-gigabyte payloads into external LZMS-compressed per-feature `.rswpak` media blocks downloaded on demand via `WinHTTP` with SHA-256 verification when a user selects optional features.
5. **First-Class `.NET 11` (`net11.0` / `net11.0-windows`, GA Nov 2026) Runtime Presets & Detection**:
   - Expand `kCuratedRuntimePresets` and `IsPrerequisiteSatisfied` in `RswPrerequisites.hpp` (which currently stop at `.NET 10.0`) to support **`DotNet11Desktop`** (`Microsoft .NET 11.0 Desktop Runtime` — `https://aka.ms/dotnet/11.0/windowsdesktop-runtime-win-x64.exe` & `win-arm64.exe`, WinGet `Microsoft.DotNet.DesktopRuntime.11`), **`DotNet11AspNet`** (`https://aka.ms/dotnet/11.0/aspnetcore-runtime-win-x64.exe` & `win-arm64.exe`), and **`DotNet11Runtime` / `DotNet10Runtime`** (`Microsoft.NETCore.App` base console/service runtimes) alongside `.NET 10.0 LTS`, `.NET 9.0`, and `.NET 8.0 LTS`.
   - Expose `.NET 11.0 (STS)` and `.NET 10.0 (LTS)` side-by-side in `InstallerStudio.exe`'s New Project AeroWizard, Prerequisites Picker, offline `RaggieSoftHelp.exe` documentation, and compiler-emitted `llms.txt` / `SKILL.md` specs, verifying both native `arm64` (`%ProgramW6432%\dotnet\shared\...\11.*`) and `x64`-on-ARM64 (`dotnet\x64\shared\...\11.*`) installations in `< 1ms`.
6. **Universal Dual-Architecture (`x64` + `ARM64`) Single-EXE Bootstrapper (`Architecture = universal`) & Native Direct Non-MSI Engine (`Engine = native | msi`)**:
   - **Consumer-Friendly Universal Single EXE (`<output>-setup.exe` with zero CPU jargon)**: Pack both `x64` and `ARM64` LZMS payloads inside one unified `Setup.exe` that queries `IsWow64Process2` at launch and automatically installs the 100% native `ARM64` or `x64` image without forcing end users to pick an architecture.
   - **Native Direct Engine (`Engine = native`)**: Optional zero-`msi.dll` direct extraction backend with an atomic transactional rollback journal, `HKLM`/`HKCU` `Uninstall` registration, and a signed native `Uninstall.exe`—eliminating `Global\_MSIExecute` mutex collisions when Windows Update is running in the background and installing multi-hundred-file content packs in milliseconds.

### Phase C (`v0.6.0`) — Setup Capture ("Repackager") & Disposable Windows Sandbox (`.wsb`) Mode
1. **Snapshot-Diff Repackager (`Legacy Setup.exe -> .rsip -> .msi + .mst`)**:
   - Capture fast pre/post filesystem and `HKLM`/`HKCU`/`HKCR` registry baselines while running legacy `.exe` installers inside a Win32 Job Object (`CreateJobObjectW`) so child sub-installers are tracked to completion.
   - Apply smart Windows background noise filtering, tokenize hard-coded paths (`C:\Program Files\...` -> `[ProgramFiles64Folder]`), harvest `.lnk` shortcuts (`IShellLinkW`) and Windows Services, emit a clean `.rsip` manifest, and generate enterprise `.mst` transforms via `MsiDatabaseGenerateTransformW`.
2. **Windows Edition Detection (`GetProductInfo`) + One-Click Windows Sandbox (`.wsb`) Capture**:
   - Query `GetProductInfo` alongside `RtlGetVersion` to detect **Windows 11 Pro, Enterprise, and Education** hosts where Microsoft Windows Sandbox is supported.
   - Offer one-click **"Capture Inside Disposable Windows Sandbox"**: automatically generate a `.wsb` configuration mapping a staging folder, execute the legacy installer + capture agent inside the pristine throwaway VM, return the clean `.rsip` + captured payload files to the host IDE, and discard the sandbox without touching the developer's workstation.

### Phase D (`v0.7.0`) — Setup Conversion / Import Wizard (Including VB6 `.vbp`!) & Visual Studio Extension (`.vsix`)
1. **Open-Format Setup Conversion Wizard (`New Project -> Import / Convert...`)**:
   - **Existing `.msi` / `.msm` -> `.rsip`**: Extend `msiprism`'s SQL reader + `<fdi.h>` CAB extractor to decompile existing Windows Installer databases directly into editable `.rsip` projects.
   - **Visual Studio `.slnx` / `.sln` / `.csproj` / `.vcxproj` / `CMakeLists.txt` -> `.rsip`**: Auto-detect `.NET 11` / `.NET 10 LTS` / `.NET 9` / `.NET 8 LTS` (`net11.0-windows`, `net11.0`, `net10.0-windows`, etc. -> mapping `<UseWPF>`/`<UseWindowsForms>`, `Sdk.Web`, or console apps to `DotNet11Desktop` / `DotNet11AspNet` / `DotNet11Runtime`), `VC++`, and `WebView2` runtime prerequisites, `x64` + `ARM64` build output paths, `.ico` assets, and PE versions in one click.
   - **Visual Basic 6.0 (`.vbp` — e.g., `TStore.vbp`) -> `.rsip` with Windows 11 WRP & Reg-Free COM Modes**:
     - Parse `ExeName32`, `Reference=*\G{...}`, and `Object={GUID}#ver#0; File.ocx` lines directly from `.vbp` projects.
     - **Windows 11 WRP Awareness**: Automatically skip or guard (`VersionNT < 600`) OS-protected `SysWOW64` core runtime files (`msvbvm60.dll`, `oleaut32.dll`, `stdole2.tlb`) that already ship inside Windows 11 (`x64` & `ARM64` Prism), while packaging required extended ActiveX controls (`COMCT232.OCX`, `Comdlg32.ocx`, `MSCOMCTL.OCX`, `RICHTX32.OCX`, `MSWINSCK.OCX`).
     - **Dual VB6 Packaging Choice**: Offer **(a) Classic Local `.MSM` Merge Module Mode** (discovering local SP6 `.msm` modules from `%CommonProgramFiles(x86)%\Merge Modules\` or a user redist folder without bundling proprietary redistributables in Git) OR **(b) Modern Windows 11 Reg-Free COM Mode** (staging `.ocx` files privately beside `<App>.exe` and auto-generating `<App>.exe.manifest` `<comClass>` / `<typelib>` SxS entries for zero-elevation, zero-`regsvr32`, zero-`SysWOW64` installs!).
   - **Open XML & Structured Script Importers (`AppxManifest.xml`, XML installer schemas, & INI setup scripts -> `.rsip`)**: Convert directory trees, components, shortcuts, registry keys, and file associations into `.rsip`, flagging custom procedural script blocks so an AI coding agent (using our emitted `SKILL.md`) can translate custom hooks into `.rssc` + `RsDialog` calls automatically.
2. **Visual Studio Add-In / Extension (`.vsix`)**:
   - Ship a Visual Studio extension providing `.rsip` / `.rsis` solution item templates, syntax highlighting, schema hover tooltips, MSBuild packaging targets, and right-click *"Build Installer Studio Package (`x64` + `ARM64`)"* integration.

### Phase E (`v0.8.0`) — Enterprise System Primitives, Config Patchers & Windows Restart Manager
1. **Windows Services, Environment Variables, Firewall Rules & Scheduled Tasks**:
   - **`[Service.<Id>]`**: Native MSI `ServiceInstall`/`ServiceControl` SQL generation + Native Engine SCM (`CreateServiceW`) lifecycle management (install, start, stop, failure recovery actions, and clean uninstall teardown).
   - **`[Environment.<Id>]`**: Safe `PATH` prefix/suffix manipulation (`[~]` semantics that never truncate or wipe existing `HKLM`/`HKCU` `PATH` entries) + immediate `WM_SETTINGCHANGE` broadcast.
   - **`[FirewallRule.<Id>]` & `[ScheduledTask.<Id>]`**: Declarative Windows Defender Firewall rules (`INetFwPolicy2`) and Task Scheduler jobs with automatic rollback and uninstall cleanup.
2. **Declarative Config File Patchers (`[JsonPatch.*]`, `[XmlConfig.*]`, `[IniFile.*]`)**:
   - Stamp install-time wizard properties (endpoints, ports, install paths, license/channel flags) cleanly into `appsettings.json`, `.xml`/`.config`, and `.ini` files during install while preserving user settings across upgrades.
3. **Windows Restart Manager (`Rstrtmgr.dll`) "Files-in-Use" Resolution**:
   - Integrate `RmStartSession` / `RmRegisterResources` / `RmGetList` into `SetupWizard.exe` so upgrades detect running processes locking target `.exe`/`.dll` files, offer a WCAG-accessible *"Close Running Applications & Continue"* step, and prevent unnecessary OS reboots.

### Phase F (`v0.9.0`) — White-Label Redistributable Auto-Updater, Guided `.MSP` Patching, Native `.MSIX` & Global `I18N` / SBOM
1. **Royalty-Free White-Label Redistributable Auto-Updater (`RsUpdate.exe` / `[AutoUpdate]`)**:
   - Ship a zero-dependency `/MT` Win32 C++20 auto-updater redistributable (`x64` & `ARM64`, unlocked across all editions including Community!) that Installer Studio stamps with the **customer's own `.ico`, `ProductName`, and `CompanyName` (`VERSIONINFO`, 100% white-label)** and bundles right into their installed directory (`[AutoUpdate] Enabled = true`).
   - Emit static HTTPS update-feed JSON manifests (`stable` / `beta` channels), verify SHA-256 + `WinVerifyTrust` Authenticode signatures via `WinHTTP`, support both WCAG-accessible interactive release-notes prompts and silent background updates, and automatically apply `.msp` binary delta patches, `.msi` Major Upgrades, or Native Direct Engine updates.
2. **Guided Microsoft-Compliant Update & `.MSP` Patch Authoring (`New Update / Patch AeroWizard` & `--plan-update`)**:
   - Make authoring updates *the right way* effortless per official Microsoft Windows Installer servicing rules (`Small Update`, `Minor Update` `.msp`, and `Major Upgrade` `.msi`):
     - **Baseline Diff & Servicing Rule Linter (`instbuild.exe --plan-update <baseline.msi> <current.rsip>`)**: Diff the current project against the shipped baseline `.msi` and enforce Microsoft's strict `.msp` Component rules automatically (preserving `UpgradeCode` + `ProductCode`, rotating `PackageCode`, blocking illegal Component GUID/KeyPath mutations or un-flagged file removals from existing Components, and routing new files into fresh Components).
     - **One-Click `.MSP` Generation vs. Major Upgrade Guidance**: When changes qualify for a Minor Update, emit a compact binary-delta `.msp` with populated `MsiPatchMetadata` and `MsiPatchSequence` superseding tables (plus optional `Setup.exe` patch wrapper passing `REINSTALL=ALL REINSTALLMODE=omus`); when structural removals require a **Major Upgrade**, explain why in plain English and rotate `ProductCode` + `Upgrade` table scheduling in one click.
3. **In-Process Native `.MSIX` & Dual-Architecture `.MSIXBUNDLE` Compiler (`AppxPackaging.dll`) + `winget` Export**:
   - Call Windows' built-in `AppxPackaging.dll` COM APIs (`IAppxFactory`, `IAppxPackageWriter`, and `IAppxBundleWriter`) directly in C++20—zero external SDK tools required—to emit `AppxManifest.xml`, SHA-256 `AppxBlockMap.xml`, single-arch `.msix`, and unified `x64 + ARM64` **`.msixbundle`** packages from the exact same `.rsip` project, auto-matching `Publisher` to the `[Signing]` certificate `Subject`!
   - Keep **Optional Windows 11 Sparse Package Identity** for classic Win32 `.msi` / Native installs needing modern Win11 Explorer context menus and Toast notifications without container virtualization, plus **`instbuild.exe --emit-winget`** (`winget-pkgs` multi-file YAML manifests).
4. **Multi-Language Localization (`I18N` + RTL Mirroring)**:
   - UTF-8 `.rslang` localization tables for 20+ languages (including `WS_EX_LAYOUTRTL` right-to-left layout mirroring for Arabic and Hebrew), auto-selecting via `GetUserDefaultUILanguage()` with an optional language picker in `SetupWizard.exe`.
5. **Procurement Compliance Pack (`--emit-sbom` Alongside `--emit-vpat`)**:
   - Generate **SPDX 2.3 / CycloneDX JSON Software Bills of Materials (`SBOM`)** listing every packaged payload file, PE version, SHA-256 hash, and runtime prerequisite alongside the HTML5 **VPAT 2.5** Accessibility Conformance Report—making RaggieSoft Installer Studio the only packaging suite that emits **AI Specs + VPAT 2.5 + SBOM** straight from the compiler.

### Phase G (`v1.0.0 GA`) — CI/CD Actions, Full RaggieSoft Dogfood Cutover & Public Launch
1. **Turnkey CI/CD Integration (`GitHub Actions` & `Azure DevOps`)**:
   - Publish a first-party GitHub Action (`raggiesoft/setup-installer-studio`) and Azure Pipelines task supporting headless `--validate --diagnostics=json`, matrix `x64` + `ARM64` + `universal` builds, Azure Artifact Signing, `--emit-vpat`, `--emit-sbom`, and `--emit-winget`.
2. **100% RaggieSoft Production Dogfood Cutover Gate**:
   - Build, sign (`raggiesoft-signing`), install, upgrade, repair, silent-deploy (`/s`), and cleanly uninstall all **Core Desktop Apps**, **33 Ocean View Archives / Story Crossroads Books**, and **37 Engine Room Records Supporter Albums** across clean Windows 11 `x64` and `ARM64` machines before retiring the legacy parallel packaging directory.

---

## Standing Operational Rule
- Keep 100% of existing legacy packaging scripts in our parallel workspace directory untouched until **RaggieSoft Installer Studio** completes full battle-testing across all RaggieSoft applications, books, and audio archives.
