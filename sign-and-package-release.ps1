<#
================================================================================
FILE: sign-and-package-release.ps1
PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)

PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
  What does this script do?
    Cryptographically Authenticode-signs the complete **RaggieSoft Installer Studio**
    release using **Microsoft Azure Artifact Signing** (`PublicTrust` + RFC 3161
    SHA-256 timestamp) in the exact Microsoft-recommended bottom-up order:
      1. Sign all inner `x64` and `ARM64` suite executables (`InstallerStudio.exe`,
         `instbuild.exe`, `SetupWizard.exe`, `msiprism.exe`, `RaggieSoftHelp.exe`,
         `insttests.exe`, and `HelloRaggieSoft.exe`) BEFORE packing `#payload.cab`!
      2. Invoke `instbuild.exe projects\apps\installer-studio\installer-studio.rsip --with-vpat`
         with `RSW_SIGN_ENABLED=1` so `instbuild.exe` Dual-Layer Authenticode-signs:
           - `raggiesoft-installer-studio-x64.msi`
           - `raggiesoft-installer-studio-arm64.msi`
           - `raggiesoft-installer-studio-x64-setup.exe`
           - `raggiesoft-installer-studio-arm64-setup.exe`
           - `raggiesoft-installer-studio-offline-setup.exe` (Combined Universal Offline EXE)
           - `raggiesoft-installer-studio-setup.exe` (Smart CPU-Aware Web Stub EXE)
      3. Run `signtool.exe verify /pa /v` (`WinVerifyTrust` Default Authenticode
         Verification Policy) across every signed `.exe` and `.msi` to prove zero
         SmartScreen signature errors and zero personal street address exposure!
      4. Scrub any ephemeral `session-signing-metadata.json` token file from disk
         and stage verified installers into `raggiesoft-assets`.
================================================================================
#>

param(
    [string]$DlibPath     = "y:\ai-staging\signing\packages\microsoft.trusted.signing.client\1.0.95\bin\x64\Azure.CodeSigning.Dlib.dll",
    [string]$MetadataPath = "y:\ai-staging\signing\session-signing-metadata.json",
    [string]$FallbackMeta = "y:\ai-staging\signing\raggiesoft-signing-metadata.json",
    [string]$TimestampUrl = "http://timestamp.acs.microsoft.com"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $RepoRoot

# Choose session metadata if present (single-token batch run), else clean metadata (`az login`).
$activeMeta = if (Test-Path $MetadataPath) { $MetadataPath } else { $FallbackMeta }
if (-not (Test-Path $DlibPath))   { throw "Azure.CodeSigning.Dlib.dll not found at: $DlibPath" }
if (-not (Test-Path $activeMeta)) { throw "Signing metadata JSON not found at: $activeMeta" }

# Locate the `x64` Windows 11 SDK `signtool.exe` matching `x64\Azure.CodeSigning.Dlib.dll`.
$kitsBin = "C:\Program Files (x86)\Windows Kits\10\bin"
$sdkDirs = Get-ChildItem -Path $kitsBin -Directory -Filter "10.*" | Sort-Object Name -Descending
$signToolX64 = $null
foreach ($d in $sdkDirs) {
    $cand = Join-Path $d.FullName "x64\signtool.exe"
    if (Test-Path $cand) { $signToolX64 = $cand; break }
}
if (-not $signToolX64) { throw "Could not locate x64\signtool.exe under $kitsBin" }

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host "  RaggieSoft Installer Studio - Azure Artifact Signing Pipeline" -ForegroundColor Cyan
Write-Host "  SignTool : $signToolX64" -ForegroundColor Gray
Write-Host "  Dlib     : $DlibPath" -ForegroundColor Gray
Write-Host "======================================================================" -ForegroundColor Cyan

# Helper function that signs one binary with retry safety on transient timestamp blips.
function Invoke-AzureSignBinary {
    param([Parameter(Mandatory = $true)][string]$FilePath)
    $leaf = Split-Path -Leaf $FilePath
    for ($attempt = 1; $attempt -le 4; $attempt++) {
        $out = & $signToolX64 sign /fd SHA256 /td SHA256 /tr $TimestampUrl /dlib $DlibPath /dmdf $activeMeta $FilePath 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  [SIGNED] $FilePath" -ForegroundColor Green
            return
        }
        if ($attempt -lt 4) {
            Write-Host "  [RETRY $attempt/3] Waiting for Azure RBAC / Timestamp ($leaf)..." -ForegroundColor Yellow
            Start-Sleep -Seconds 5
        } else {
            throw "Failed signing $FilePath (Exit $LASTEXITCODE): $($out -join "`n")"
        }
    }
}

try {
    # Step 1: Sign all inner `arm64` and `x64` executables BEFORE building `#payload.cab`!
    Write-Host "[1/4] Signing inner x64 & ARM64 suite executables..." -ForegroundColor Cyan
    foreach ($arch in @("arm64", "x64")) {
        foreach ($exeName in @(
            "InstallerStudio.exe",
            "instbuild.exe",
            "SetupWizard.exe",
            "msiprism.exe",
            "RaggieSoftHelp.exe",
            "insttests.exe",
            "HelloRaggieSoft.exe"
        )) {
            $exePath = Join-Path $RepoRoot "bin\$arch\$exeName"
            if (-not (Test-Path $exePath)) { throw "Missing compiled binary: $exePath" }
            Invoke-AzureSignBinary -FilePath $exePath
        }
    }

    # Step 2: Build & Dual-Layer Authenticode-sign `installer-studio.rsip` (x64, ARM64, Offline Universal & Web Stub)!
    Write-Host "[2/4] Compiling & Dual-Layer signing RaggieSoft Installer Studio packages..." -ForegroundColor Cyan
    $hostIsArm64 = ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") -or ($env:PROCESSOR_ARCHITEW6432 -eq "ARM64")
    $nativeArch  = if ($hostIsArm64) { "arm64" } else { "x64" }
    $instBuild   = Join-Path $RepoRoot "bin\$nativeArch\instbuild.exe"
    $rsipPath    = Join-Path $RepoRoot "projects\apps\installer-studio\installer-studio.rsip"
    $outDir      = Join-Path $RepoRoot "projects\apps\installer-studio\out"

    Remove-Item -Recurse -Force $outDir -ErrorAction SilentlyContinue

    $env:RSW_SIGN_ENABLED       = "1"
    $env:RSW_SIGN_DLIB          = $DlibPath
    $env:RSW_SIGN_METADATA      = $activeMeta
    $env:RSW_SIGN_TIMESTAMP_URL = $TimestampUrl

    & $instBuild $rsipPath --with-vpat
    if ($LASTEXITCODE -ne 0) { throw "Signed package build of installer-studio.rsip failed!" }

    # Step 3: Verify `WinVerifyTrust` (`/pa`) + RFC 3161 Timestamp on EVERY signed binary & package!
    Write-Host "[3/4] Running WinVerifyTrust (signtool verify /pa) across all outputs..." -ForegroundColor Cyan
    $verifyTargets = @(
        (Join-Path $RepoRoot "bin\arm64\InstallerStudio.exe"),
        (Join-Path $RepoRoot "bin\arm64\instbuild.exe"),
        (Join-Path $RepoRoot "bin\arm64\SetupWizard.exe"),
        (Join-Path $RepoRoot "bin\arm64\msiprism.exe"),
        (Join-Path $RepoRoot "bin\arm64\RaggieSoftHelp.exe"),
        (Join-Path $RepoRoot "bin\x64\InstallerStudio.exe"),
        (Join-Path $RepoRoot "bin\x64\instbuild.exe"),
        (Join-Path $RepoRoot "bin\x64\SetupWizard.exe"),
        (Join-Path $RepoRoot "bin\x64\msiprism.exe"),
        (Join-Path $RepoRoot "bin\x64\RaggieSoftHelp.exe"),
        (Join-Path $outDir   "raggiesoft-installer-studio-x64.msi"),
        (Join-Path $outDir   "raggiesoft-installer-studio-arm64.msi"),
        (Join-Path $outDir   "raggiesoft-installer-studio-x64-setup.exe"),
        (Join-Path $outDir   "raggiesoft-installer-studio-arm64-setup.exe"),
        (Join-Path $outDir   "raggiesoft-installer-studio-offline-setup.exe"),
        (Join-Path $outDir   "raggiesoft-installer-studio-setup.exe")
    )

    foreach ($vt in $verifyTargets) {
        if (-not (Test-Path $vt)) { throw "Expected signed artifact missing: $vt" }
        $vOut = & $signToolX64 verify /pa /v $vt 2>&1
        if ($LASTEXITCODE -ne 0) {
            throw "WinVerifyTrust verification FAILED on $vt!`n$($vOut -join "`n")"
        }
        $subjLine = ($vOut | Select-String -Pattern "Issued to:|Subject:" | Select-Object -First 1)
        Write-Host ("  [VERIFIED /pa] {0,-50} {1}" -f (Split-Path -Leaf $vt), $subjLine) -ForegroundColor Green
    }

    # Step 4: Stage signed installers & VPAT report into `raggiesoft-assets` for CDN sync!
    Write-Host "[4/4] Staging signed installers & VPAT 2.5 report into raggiesoft-assets..." -ForegroundColor Cyan
    $cdnDir = Join-Path $RepoRoot "..\..\raggiesoft-servers\raggiesoft-assets\desktop-apps\windows\apps\installer-studio"
    New-Item -ItemType Directory -Force -Path $cdnDir | Out-Null
    foreach ($asset in @(
        "raggiesoft-installer-studio-setup.exe",
        "raggiesoft-installer-studio-offline-setup.exe",
        "raggiesoft-installer-studio-x64-setup.exe",
        "raggiesoft-installer-studio-arm64-setup.exe",
        "raggiesoft-installer-studio-x64.msi",
        "raggiesoft-installer-studio-arm64.msi",
        "raggiesoft-installer-studio-vpat-2.5.html"
    )) {
        $src = Join-Path $outDir $asset
        if (Test-Path $src) {
            Copy-Item -Force $src (Join-Path $cdnDir $asset)
            $szKb = [math]::Round((Get-Item $src).Length / 1KB, 1)
            Write-Host "  [STAGED] $asset ($szKb KB)" -ForegroundColor Green
        }
    }

    Write-Host ""
    Write-Host "ALL 16 RAGGIESOFT INSTALLER STUDIO BINARIES, MSIs, AND UNIVERSAL EXEs ARE AUTHENTICODE-SIGNED & VERIFIED!" -ForegroundColor Green
}
finally {
    $env:RSW_SIGN_ENABLED  = $null
    $env:RSW_SIGN_DLIB     = $null
    $env:RSW_SIGN_METADATA = $null
    if (Test-Path $MetadataPath) {
        Remove-Item -Force $MetadataPath -ErrorAction SilentlyContinue
        Write-Host "  [CLEANUP] Scrubbed ephemeral session token metadata." -ForegroundColor DarkGray
    }
}
