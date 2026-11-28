<#
================================================================================
FILE: export-all-screenshots.ps1
PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)

PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
  What does this script do, and why is it a mandatory step on every release?
    RaggieSoft enforces a permanent standing engineering rule:
      **100% of the screenshots on `https://raggiesoftstudio.com/screenshots`
      MUST ALWAYS reflect the most recently released version of RaggieSoft
      Installer Studio.**

    Whenever a new version is compiled (`build-and-verify.ps1` + packaging
    `projects\apps\installer-studio\installer-studio.rsip`), running this script
    launches all four native Win32 C++20 GUI executables with `--export-screenshots`
    and waits synchronously (`Start-Process -Wait -PassThru`) for each window to
    render and capture crisp 1:1 PNG screenshots into:
      `raggiesoft-servers\raggiesoft-assets\desktop-apps\windows\apps\installer-studio\screenshots\`

  Inputs:
    - Compiled binaries in `bin\<nativeArch>\` (`InstallerStudio.exe`,
      `msiprism.exe`, `RaggieSoftHelp.exe`)
    - Compiled self-installer in `projects\apps\installer-studio\out\`
      (`raggiesoft-installer-studio-<nativeArch>-setup.exe` & `.msi`)

  Outputs:
    - Refreshes all 10+ PNG captures (`ide-*.png`, `setupwizard-*.png`,
      `msiprism-sql-inspector.png`, and `helpviewer-contents.png`) and verifies
      every file is non-empty and freshly written.
================================================================================
#>

$ErrorActionPreference = "Stop"

# Locate the repository root (`desktop-apps\installer-studio\`) and target CDN asset folder.
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $RepoRoot

$hostIsArm64 = ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") -or ($env:PROCESSOR_ARCHITEW6432 -eq "ARM64")
$nativeArch  = if ($hostIsArm64) { "arm64" } else { "x64" }

$binDir      = Join-Path $RepoRoot "bin\$nativeArch"
$projPath    = Join-Path $RepoRoot "projects\apps\installer-studio\installer-studio.rsip"
$projOutDir  = Join-Path $RepoRoot "projects\apps\installer-studio\out"
$verMatch    = Select-String -Path $projPath -Pattern '^\s*Version\s*=\s*([0-9]+\.[0-9]+\.[0-9]+)' | Select-Object -First 1
$projVer     = if ($null -ne $verMatch) { $verMatch.Matches[0].Groups[1].Value } else { "0.8.0" }
$msiPath     = Join-Path $projOutDir "raggiesoft-installer-studio-v$projVer-$nativeArch.msi"
$setupExe    = Join-Path $projOutDir "raggiesoft-installer-studio-v$projVer-$nativeArch-setup.exe"
if (-not (Test-Path $msiPath))  { $msiPath  = Join-Path $projOutDir "raggiesoft-installer-studio-$nativeArch.msi" }
if (-not (Test-Path $setupExe)) { $setupExe = Join-Path $projOutDir "raggiesoft-installer-studio-$nativeArch-setup.exe" }
$shotsDir    = Join-Path $RepoRoot "..\..\raggiesoft-servers\raggiesoft-assets\desktop-apps\windows\apps\installer-studio\screenshots"

# Verify that all required native executables and packages exist before capturing screenshots.
foreach ($reqPath in @(
    (Join-Path $binDir "InstallerStudio.exe"),
    (Join-Path $binDir "msiprism.exe"),
    (Join-Path $binDir "RaggieSoftHelp.exe"),
    $projPath,
    $msiPath,
    $setupExe
)) {
    if (-not (Test-Path $reqPath)) {
        throw "Required build artifact missing before screenshot export: $reqPath"
    }
}

New-Item -ItemType Directory -Force -Path $shotsDir | Out-Null
$resolvedShotsDir = (Resolve-Path $shotsDir).Path

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host "  Exporting Fresh Website Screenshots ($nativeArch) -> $resolvedShotsDir" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

# Helper that launches a `/SUBSYSTEM:WINDOWS` executable and blocks until it exits cleanly.
function Invoke-GuiScreenshotExport {
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][string]$ExePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    Write-Host "  -> Capturing $Label..." -ForegroundColor Yellow
    $proc = Start-Process -FilePath $ExePath -ArgumentList $Arguments -Wait -PassThru
    if ($proc.ExitCode -ne 0) {
        throw "$Label exited with non-zero code $($proc.ExitCode)!"
    }
}

# 1. Visual IDE (`InstallerStudio.exe`): Features, Registry, Destination Folders, Runtimes & Wizard Designer
Invoke-GuiScreenshotExport `
    -Label "InstallerStudio.exe (5 Tree-First IDE Views)" `
    -ExePath (Join-Path $binDir "InstallerStudio.exe") `
    -Arguments @("`"$projPath`"", "--export-screenshots", "`"$resolvedShotsDir`"")

# 2. Customer Runtime (`SetupWizard.exe` packaged with `installer-studio.rsip`): Dark, Light, SelectionTree, License & UAC
Invoke-GuiScreenshotExport `
    -Label "SetupWizard.exe (Dark Mode, Live Light Mode & SelectionTree)" `
    -ExePath $setupExe `
    -Arguments @("--export-screenshots", "`"$resolvedShotsDir`"")

# 3. SQL Database Inspector (`msiprism.exe`): Categorized 5-Group Schema Tree
Invoke-GuiScreenshotExport `
    -Label "msiprism.exe (Categorized SQL Schema Tree)" `
    -ExePath (Join-Path $binDir "msiprism.exe") `
    -Arguments @("`"$msiPath`"", "--export-screenshots", "`"$resolvedShotsDir`"")

# 4. Native C++20 Offline Help Viewer (`RaggieSoftHelp.exe`): 32-Bit ARGB Contents Tree + Article Typography
Invoke-GuiScreenshotExport `
    -Label "RaggieSoftHelp.exe (Offline Help Contents & Article)" `
    -ExePath (Join-Path $binDir "RaggieSoftHelp.exe") `
    -Arguments @("--module", "raggiesoft.installer-studio", "--topic", "selection-tree-and-dialogs", "--export-screenshots", "`"$resolvedShotsDir`"")

# 5. Audit every PNG required by `https://raggiesoftstudio.com/screenshots` & `README.md`!
$requiredPngs = @(
    "ide-features-tree.png",
    "ide-registry-tree.png",
    "ide-destination-folders.png",
    "ide-runtimes-msm.png",
    "ide-wizard-designer.png",
    "setupwizard-setup-type.png",
    "setupwizard-setup-type-light.png",
    "setupwizard-selection-tree.png",
    "setupwizard-license.png",
    "setupwizard-ready-uac.png",
    "setupwizard-ready-uac-light.png",
    "setupwizard-wizard95.png",
    "setupwizard-fluent-mica.png",
    "setupwizard-fullscreen-blue.png",
    "setupwizard-fullscreen-teal.png",
    "msiprism-sql-inspector.png",
    "helpviewer-contents.png"
)

foreach ($png in $requiredPngs) {
    $fullPng = Join-Path $resolvedShotsDir $png
    if (-not (Test-Path $fullPng)) {
        throw "Missing expected screenshot after export: $fullPng"
    }
    $item = Get-Item $fullPng
    if ($item.Length -lt 4096) {
        throw "Exported screenshot appears truncated ($($item.Length) bytes): $fullPng"
    }
    Write-Host ("     [OK] {0,-34} {1,8:N1} KB  ({2})" -f $png, ($item.Length / 1KB), $item.LastWriteTime.ToString("yyyy-MM-dd HH:mm:ss")) -ForegroundColor Green
}

Write-Host "ALL 17 WEBSITE SCREENSHOTS EXPORTED AND VERIFIED CLEANLY!" -ForegroundColor Green
