<#
================================================================================
FILE: stage-private-app-build.ps1
PROJECT: RaggieSoft Installer Studio — Private Versioned App Staging Pipeline
WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)

PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
  What does this script do?
    Compiles any RaggieSoft Studio tool or RaggieSoft Consumer desktop app `.rsip`
    manifest using `instbuild.exe` and stages its versioned `x64`, `ARM64`,
    Combined Offline Universal `.exe`, `.msi`, HTML5 VPAT 2.5 report, and
    `SHA256SUMS.txt` directly into our local private handoff folder:

      `Y:\ai-staging\raggiesoft-apps\<studio|consumer>\<slug>\v<Version>\`

  Why stage into `Y:\ai-staging\raggiesoft-apps\` instead of `raggiesoft-assets`?
    1. Protects live CDN downloads (such as the frozen `Installer Studio v0.5.0`
       binary under Microsoft WDSI SmartScreen review) while we rapidly build and
       test new versions (`v0.5.1`, `v0.6.0`, and brand-new Studio tools!).
    2. Keeps `ai-staging/**` 100% out of Git repositories while automatically
       syncing across local NVMe, Parallels VMs, and 4TB backup drives!
    3. Supports optional `-Sign` when you want a Microsoft Azure Artifact Signed
       private test build, while defaulting to fast unsigned builds for daily dev.
================================================================================
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("studio", "consumer")]
    [string]$Group,

    [Parameter(Mandatory = $true)]
    [string]$Slug,

    [string]$RsipPath       = "",
    [string]$Version        = "",
    [switch]$Sign,
    [switch]$IncludeWebStub,
    [string]$DlibPath     = "y:\ai-staging\signing\packages\microsoft.trusted.signing.client\1.0.95\bin\x64\Azure.CodeSigning.Dlib.dll",
    [string]$MetadataPath = "y:\ai-staging\signing\session-signing-metadata.json",
    [string]$FallbackMeta = "y:\ai-staging\signing\raggiesoft-signing-metadata.json",
    [string]$TimestampUrl = "http://timestamp.acs.microsoft.com"
)

$ErrorActionPreference = "Stop"
$InstallerStudioRoot   = Split-Path -Parent $MyInvocation.MyCommand.Definition
$DeveloperRoot         = (Resolve-Path (Join-Path $InstallerStudioRoot "..\..")).Path

# 1. Safety Gate: Verify the public SmartScreen-submitted v0.5.0 binary has NOT been touched!
$FrozenStudioExe   = Join-Path $DeveloperRoot "raggiesoft-servers\raggiesoft-assets\desktop-apps\windows\apps\installer-studio\raggiesoft-installer-studio-setup.exe"
$ExpectedStudioSha = "DBFA09DA994353B738ED49EC5CB05689EE846B93708EC32DD4FD7490D425A074"
if (Test-Path $FrozenStudioExe) {
    $actualSha = (Get-FileHash -Algorithm SHA256 $FrozenStudioExe).Hash
    if ($actualSha -ne $ExpectedStudioSha) {
        throw "SAFETY ABORT: Frozen public Installer Studio v0.5.0 hash mismatch ($actualSha != $ExpectedStudioSha)!"
    }
}

# 2. Locate native `instbuild.exe` (`arm64` on Windows 11 ARM64, `x64` on Intel/AMD)
$hostIsArm64 = ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") -or ($env:PROCESSOR_ARCHITEW6432 -eq "ARM64")
$nativeArch  = if ($hostIsArm64) { "arm64" } else { "x64" }
$instBuild   = Join-Path $InstallerStudioRoot "bin\$nativeArch\instbuild.exe"
if (-not (Test-Path $instBuild)) {
    throw "instbuild.exe not found at '$instBuild'. Run build-and-verify.ps1 first."
}

# 3. Resolve `.rsip` manifest path if not explicitly supplied
if ([string]::IsNullOrWhiteSpace($RsipPath)) {
    $candidates = @(
        (Join-Path $InstallerStudioRoot "projects\apps\$Slug\$Slug.rsip"),
        (Join-Path $DeveloperRoot       "desktop-apps\$Slug\$Slug.rsip"),
        (Join-Path $DeveloperRoot       "desktop-apps\$Slug\installer\$Slug.rsip")
    )
    foreach ($c in $candidates) {
        if (Test-Path $c) { $RsipPath = $c; break }
    }
}
if ([string]::IsNullOrWhiteSpace($RsipPath) -or -not (Test-Path $RsipPath)) {
    throw "Could not locate .rsip manifest for [$Group/$Slug]. Pass -RsipPath <path-to.rsip>."
}

# 4. Read effective 3-part `Version` from `.rsip` unless `-Version` override was passed
if ([string]::IsNullOrWhiteSpace($Version)) {
    $verMatch = Select-String -Path $RsipPath -Pattern '^\s*Version\s*=\s*([0-9]+\.[0-9]+\.[0-9]+)' | Select-Object -First 1
    if ($null -ne $verMatch) {
        $Version = $verMatch.Matches[0].Groups[1].Value
    } else {
        $Version = "0.1.0"
    }
}

$stageDir = Join-Path $DeveloperRoot "ai-staging\raggiesoft-apps\$Group\$Slug\v$Version"
Remove-Item -Recurse -Force $stageDir -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $stageDir | Out-Null

$stemPrefix = if ($Slug -like "raggiesoft-*") { $Slug } else { "raggiesoft-$Slug" }

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host "  Private Build Stage : [$Group / $Slug] v$Version" -ForegroundColor Cyan
Write-Host "  Manifest            : $RsipPath" -ForegroundColor Gray
Write-Host "  Destination         : $stageDir" -ForegroundColor Gray
Write-Host "  Authenticode Sign   : $($Sign.IsPresent)" -ForegroundColor Gray
Write-Host "======================================================================" -ForegroundColor Cyan

try {
    if ($Sign) {
        $activeMeta = if (Test-Path $MetadataPath) { $MetadataPath } else { $FallbackMeta }
        $env:RSW_SIGN_ENABLED       = "1"
        $env:RSW_SIGN_DLIB          = $DlibPath
        $env:RSW_SIGN_METADATA      = $activeMeta
        $env:RSW_SIGN_TIMESTAMP_URL = $TimestampUrl
    } else {
        $env:RSW_SIGN_ENABLED = $null
    }

    # Compile `x64` + `ARM64` `.msi` + `-setup.exe` + Combined Offline Universal EXE + VPAT 2.5 HTML
    # directly into `ai-staging\raggiesoft-apps\<Group>\<Slug>\v<Version>\`!
    # By default, suppress un-uploaded CDN Smart Web Stubs inside `ai-staging` so every `.exe` in
    # private staging works 100% offline immediately (`-IncludeWebStub` re-enables the web stub).
    $buildArgs = @(
        $RsipPath,
        "--universal",
        "--with-vpat",
        "--version-override=$Version",
        "--out-dir=$stageDir",
        "--output-name=$stemPrefix-v$Version-`$(ARCH)",
        "--universal-name=$stemPrefix-v$Version"
    )
    if (-not $IncludeWebStub) {
        $buildArgs += "--web-base-url="
    }
    & $instBuild @buildArgs
    if ($LASTEXITCODE -ne 0) {
        throw "instbuild.exe failed staging [$Group/$Slug] v$Version (Exit $LASTEXITCODE)!"
    }

    # Emit deterministic `SHA256SUMS.txt` manifest alongside staged private artifacts
    $sumLines = @()
    Get-ChildItem -File -Path $stageDir | Sort-Object Name | ForEach-Object {
        $hash = (Get-FileHash -Algorithm SHA256 $_.FullName).Hash
        $kb   = [math]::Round($_.Length / 1KB, 1)
        $sumLines += "$hash  $($_.Name)  ($kb KB)"
        Write-Host ("  [STAGED] {0,-58} {1,8} KB" -f $_.Name, $kb) -ForegroundColor Green
    }
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines((Join-Path $stageDir "SHA256SUMS.txt"), $sumLines, $utf8NoBom)

    # Re-verify public frozen v0.5.0 binary was never touched
    if (Test-Path $FrozenStudioExe) {
        $postSha = (Get-FileHash -Algorithm SHA256 $FrozenStudioExe).Hash
        if ($postSha -ne $ExpectedStudioSha) {
            throw "SAFETY ABORT: Frozen public Installer Studio v0.5.0 hash changed!"
        }
    }

    Write-Host "[OK] Private build staged cleanly in: $stageDir" -ForegroundColor Green
}
finally {
    $env:RSW_SIGN_ENABLED       = $null
    $env:RSW_SIGN_DLIB          = $null
    $env:RSW_SIGN_METADATA      = $null
    $env:RSW_SIGN_TIMESTAMP_URL = $null
}
