<#
================================================================================
FILE: desktop-apps/installer-studio/build-raggiesoft-catalog.ps1
PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)

PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
  Batch-validates and compiles `RaggieSoft.rsis` (and member `.rsip` projects)
  across `arm64` and `x64` using native `instbuild.exe`!
================================================================================
#>

[CmdletBinding()]
param(
    [switch]$ValidateOnly,
    [string]$Category = "",
    [string]$ProjectSlug = "",
    [switch]$WithVpat,
    [string[]]$Architectures = @("arm64", "x64")
)

$ErrorActionPreference = "Stop"
$Root = Split-Path -Parent $MyInvocation.MyCommand.Definition
$hostIsArm64 = ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") -or ($env:PROCESSOR_ARCHITEW6432 -eq "ARM64")
$nativeArch  = if ($hostIsArm64) { "arm64" } else { "x64" }
$instBuild   = Join-Path $Root "bin\$nativeArch\instbuild.exe"
$suitePath   = Join-Path $Root "RaggieSoft.rsis"

if (-not (Test-Path $instBuild)) {
    throw "instbuild.exe not found at $instBuild. Run .\build-and-verify.ps1 first!"
}
if (-not (Test-Path $suitePath)) {
    throw "RaggieSoft.rsis not found at $suitePath. Run py -3 .\tools\generate-raggiesoft-rsip-projects.py first!"
}

foreach ($arch in $Architectures) {
    $cliArgs = @($suitePath, "--arch=$arch")
    if ($ValidateOnly) { $cliArgs += "--validate"; $cliArgs += "--diagnostics=json" }
    if ($Category)     { $cliArgs += "--category=$Category" }
    if ($ProjectSlug)  { $cliArgs += "--project=$ProjectSlug" }
    if ($WithVpat)     { $cliArgs += "--with-vpat" }

    & $instBuild @cliArgs
    if ($LASTEXITCODE -ne 0) {
        throw "instbuild.exe failed for RaggieSoft.rsis ($arch)!"
    }
}

Write-Host "[OK] Completed RaggieSoft.rsis across [$($Architectures -join ', ')]!" -ForegroundColor Green
