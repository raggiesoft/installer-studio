# RaggieSoft Setup Wizard Manifest Specification (`v0.1` — `.rsw`)

**File Extension:** `.rsw` (UTF-8 plain text, `LF` or `CRLF` line endings)  
**Compiler:** `rswbuild.exe` (`RaggieSoft Setup Wizard`)  
**Schema Source of Truth:** [`src/common/RswSchema.hpp`](../src/common/RswSchema.hpp)

---

## 1. Design Goals

1. **Zero-Noise Human & AI Readability**: Clean INI-style `[Section]` and `[Kind.Id]` blocks with `Key = Value` pairs—no XML GUID boilerplate, no positional line-count traps, and no opaque binary blobs.
2. **100% Lossless Comment & Formatting Preservation**:
   - Full-line `; ...` and `# ...` comments, blank lines, key ordering, and inline trailing `; ...` comments are preserved as first-class nodes in `RswManifestAst`.
   - Mutating a key in a visual IDE (such as bumping `Version = 1.1.0`) and saving writes back byte-identical comments and layout everywhere else in the file.
3. **Deterministic Upgrades Without Manual Component GUID Lists**:
   - You supply **one** permanent `UpgradeCode` GUID per product in `[Package]`.
   - `rswbuild.exe` computes every MSI `ComponentId` deterministically using **RFC 4122 SHA-1 UUIDv5** (`UpgradeCode` + normalized target install path) via Windows CNG (`bcrypt.dll`), guaranteeing 100% stable component identity across updates and Major Upgrades.
   - `ProductCode` and Summary Info `PackageCode` (`PID_REVNUMBER`) are generated fresh via `UuidCreate` on each build (unless pinned explicitly for specialized servicing).

---

## 2. Path Resolution Rules

- Relative file paths (`LicenseRtf`, `IconFile`, `BannerBmp`, `WelcomeBmp`, `[Payload.<id>] Source`) resolve relative to the directory containing the `.rsw` manifest file.
- `$(ARCH)` inside any value expands automatically to the active target architecture (`x64` or `arm64`), allowing **one single `.rsw` manifest** to build both `x64` and `ARM64` installers cleanly (`rswbuild.exe app.rsw --arch=x64` and `rswbuild.exe app.rsw --arch=arm64`)!

---

## 3. Section Reference (`v0.1`)

### `[Package]` *(Required Singleton)*
Defines product identity, Windows Installer Summary Information, and target silicon.

| Key | Type | Required | Default | Description |
| :--- | :--- | :---: | :--- | :--- |
| `SchemaVersion` | `string` | No | `0.1` | Manifest grammar version (`0.1`). |
| `Name` | `string` | **Yes** | — | Consumer-facing `ProductName` (e.g., `Hello RaggieSoft`). |
| `Version` | `version` | **Yes** | — | MSI `ProductVersion` (`Major.Minor.Build`, max `255.255.65535`). |
| `Manufacturer` | `string` | **Yes** | — | Publisher written to `Manufacturer` and `VERSIONINFO` `CompanyName`. |
| `UpgradeCode` | `guid` | **Yes** | — | Permanent `{GUID}` linking all versions of this product across Major Upgrades. |
| `ProductCode` | `guid_or_auto` | No | `*` | `*` generates a fresh `{GUID}` per build; or supply a fixed `{GUID}`. |
| `Description` | `string` | No | `""` | Summary Information description & `VERSIONINFO` `FileDescription`. |
| `Language` | `int` | No | `1033` | Numeric LCID (`1033` = `en-US`). |
| `Architecture` | `enum` | No | `arm64` | Target silicon (`x64` or `arm64`; overridable via `--arch=x64\|arm64`). |
| `Scope` | `enum` | No | `machine` | `machine` (`ALLUSERS=1`, `ProgramFiles64Folder`) or `user` (per-user `LocalAppDataFolder`). |
| `OutputName` | `string` | No | `<slug>-$(ARCH)` | Base stem for `<OutputDir>\<OutputName>.msi` and `<OutputName>-setup.exe`. |
| `OutputDir` | `path` | No | `out` | Output directory (relative to `.rsw` folder or absolute). |
| `BuildBootstrapper` | `bool` | No | `true` | Emits `<OutputName>-setup.exe` embedding `Product.msi` alongside standalone `.msi`. |
| `CabinetCompression` | `enum` | No | `lzx` | `#payload.cab` compression algorithm: `lzx` (high-ratio LZX:21) or `mszip`. |
| `HideFromInstalledApps` | `bool` | No | `false` | Sets `ARPSYSTEMCOMPONENT=1` (ideal for book/album content micro-installers). |
| `AboutUrl` | `string` | No | `""` | Sets `ARPURLINFOABOUT` link in Windows *Installed apps*. |

### `[Requirements]` *(Optional Singleton)*
Configures OS build gates enforced in both `Product.msi` (`LaunchCondition`) and `rswsetup.exe` (`RtlGetVersion`).

| Key | Type | Required | Default | Description |
| :--- | :--- | :---: | :--- | :--- |
| `MinWindowsBuild` | `int` | No | `22000` | Minimum Windows NT 10.0 build (`22000` = Windows 11 21H2+; matches .NET 10). |
| `OsLaunchMessage` | `string` | No | `"This application requires Windows 11 (Build 22000) or later."` | Friendly message shown if OS floor is not met. |
| `BlockDowngrades` | `bool` | No | `true` | Blocks installing an older version over a newer installed version via `Upgrade` table. |

### `[Install]` *(Required Singleton)*
Configures `INSTALLDIR` and Start Menu folder hierarchy.

| Key | Type | Required | Default | Description |
| :--- | :--- | :---: | :--- | :--- |
| `DefaultSubdir` | `relpath` | **Yes** | — | Subfolder under `ProgramFiles64Folder` (or `LocalAppDataFolder`), e.g. `RaggieSoft\Hello RaggieSoft`. |
| `StartMenuFolder` | `relpath` | No | `RaggieSoft` | Subfolder under `ProgramMenuFolder` for Start Menu shortcuts. |
| `MainExecutable` | `relpath` | No | `""` | Relative path under `INSTALLDIR` for `ARPINSTALLLOCATION` / optional Finish launch. |

### `[UI]` *(Optional Singleton)*
Customizes both the embedded Windows 2000 Wizard 97 MSI dialogs and the branded Win32 `rswsetup.exe` shell.

| Key | Type | Required | Default | Description |
| :--- | :--- | :---: | :--- | :--- |
| `LicenseRtf` | `path` | No | `""` | Path to `.rtf` license agreement shown in `LicenseAgreementDlg` and `rswsetup.exe`. |
| `IconFile` | `path` | No | `""` | `.ico` file used for Windows *Installed apps* (`ARPPRODUCTICON`) and `Setup.exe`. |
| `BannerTitle` | `string` | No | `""` | Optional custom subtitle in top header banner. |
| `WelcomeHeadline` | `string` | No | `""` | Optional custom headline on Welcome page. |
| `InstallButtonText` | `accel_string` | No | `&Install` | Must include `&` keyboard accelerator mnemonic for WCAG 2.2 AA / Section 508 keyboard access. |
| `FinishLaunchCheckbox` | `accel_string` | No | `&Launch application when setup finishes` | Label (with `&` mnemonic) when `MainExecutable` is set. |
| `OfferLaunchOnFinish` | `bool` | No | `true` | Displays optional launch checkbox on completion screen when `MainExecutable` is set. |

### `[Signing]` *(Optional Singleton)*
Configures dual-layer Authenticode signing via Windows SDK `signtool.exe` (signs `Product.msi` first, embeds into `Setup.exe`, then signs `Setup.exe`).

| Key | Type | Required | Default | Description |
| :--- | :--- | :---: | :--- | :--- |
| `Enabled` | `bool` | No | `false` | Enables `signtool.exe` signing step. |
| `Thumbprint` | `string` | No | `""` | Optional SHA-1 certificate thumbprint in cert store. |
| `TimestampUrl` | `string` | No | `http://timestamp.acs.microsoft.com` | RFC 3161 timestamp server URL. |
| `DlibPath` | `path` | No | `""` | Optional Azure Artifact Signing `Azure.CodeSigning.Dlib.dll` path. |
| `MetadataJson` | `path` | No | `""` | Optional Azure Artifact Signing metadata `.json` path. |

### `[Feature.<id>]` *(Repeatable Named Section; Default `Main` Created Automatically if Omitted)*
Defines selectable Windows Installer `Feature` rows.

| Key | Type | Required | Default | Description |
| :--- | :--- | :---: | :--- | :--- |
| `Title` | `string` | **Yes** | — | Consumer-facing feature title. |
| `Description` | `string` | No | `""` | Feature description shown in custom feature trees. |
| `Level` | `int` | No | `1` | `1` = installed by default; `0` = disabled; `>1` = optional tier. |

### `[Payload.<id>]` *(At Least One Required)*
Maps a single file or recursive directory tree from the build machine into `INSTALLDIR` (or a subdirectory).

| Key | Type | Required | Default | Description |
| :--- | :--- | :---: | :--- | :--- |
| `Source` | `path` | **Yes** | — | File path or directory path (supports `$(ARCH)` token). |
| `TargetSubdir` | `relpath` | No | `""` | Destination subfolder under `INSTALLDIR` (`""` = root of `INSTALLDIR`). |
| `Recursive` | `bool` | No | `true` | When `Source` is a directory, recursively includes subdirectories. |
| `Feature` | `string` | No | `Main` | Feature ID owning the generated deterministic components. |

### `[Shortcut.<id>]` *(Optional Repeatable Named Section)*
Creates Start Menu or Desktop shortcuts (`Shortcut` + `RemoveFolder` ICE64-clean tables).

| Key | Type | Required | Default | Description |
| :--- | :--- | :---: | :--- | :--- |
| `Name` | `string` | **Yes** | — | Shortcut display name (without `.lnk`). |
| `TargetFile` | `relpath` | **Yes** | — | Relative path under `INSTALLDIR` to the target `.exe` in `[Payload.*]`. |
| `Location` | `enum` | No | `StartMenu` | `StartMenu` (`ProgramMenuFolder\<StartMenuFolder>`) or `Desktop` (`DesktopFolder`). |
| `Description` | `string` | No | `""` | Accessible tooltip description read by Windows Explorer & Narrator/NVDA. |
| `Arguments` | `string` | No | `""` | Optional command-line arguments. |
| `Feature` | `string` | No | `Main` | Feature ID owning the shortcut. |

### `[Registry.<id>]` *(Optional Repeatable Named Section)*
Writes per-machine (`HKLM`) or per-user (`HKCU`) Windows Registry values cleanly removed on uninstall.

| Key | Type | Required | Default | Description |
| :--- | :--- | :---: | :--- | :--- |
| `Root` | `enum` | No | `HKLM` | `HKLM`, `HKCU`, or `HKCR`. |
| `Key` | `string` | **Yes** | — | Registry key path (e.g., `Software\RaggieSoft\Hello RaggieSoft`). |
| `Name` | `string` | **Yes** | — | Value name (use `@` for default key value). |
| `Value` | `string` | **Yes** | — | Value data (supports MSI formatted tokens such as `[INSTALLDIR]` and `[ProductVersion]`). |
| `Type` | `enum` | No | `string` | `string` (`REG_SZ`), `expand_string` (`REG_EXPAND_SZ`), or `dword` (`REG_DWORD`). |
| `Feature` | `string` | No | `Main` | Feature ID owning the registry component. |
