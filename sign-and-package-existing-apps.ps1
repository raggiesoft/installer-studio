<#
================================================================================
FILE: sign-and-package-existing-apps.ps1
PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)

PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
  What does this script do?
    Rebuilds and Dual-Layer Authenticode-signs all 6 existing RaggieSoft Windows
    desktop applications using **RaggieSoft Installer Studio v0.5.0** and
    **Microsoft Azure Artifact Signing** (`PublicTrust` + RFC 3161 SHA-256
    timestamp), WITHOUT touching any of the frozen `raggiesoft-installer-studio-*`
    binaries currently under Microsoft SmartScreen / WDSI review:

      1. `launcher`             -> `fluentmica` (Windows 11 Step Rail)
      2. `book-reader`          -> `standard` (RaggieSoft Glass)
      3. `listening-room`       -> `standard` (RaggieSoft Glass)
      4. `backstage`            -> `standard` (RaggieSoft Glass)
      5. `storetrainer`         -> `fullscreenteal` (1995 ACME #008080 Teal + `win95_classic`)
      6. `accessibility-studio` -> `aerowizard` (+ 4 Author-Defined Setup Types)

    Optionally, when `-IncludeAllInOneBoxSet` is passed, it also compiles and
    Dual-Layer signs the ~1.48 GB DVD-scale local stress-test installer
    (`ai-staging\all-in-one-boxset\raggiesoft-all-in-one.rsip`) strictly inside
    `ai-staging\all-in-one-boxset\out\` (`fullscreenblue` + 8 Billboards!).

  Outputs Produced:
    - Stages all 6 signed artifacts per app (`-setup.exe`, `-offline-setup.exe`,
      `-x64-setup.exe`, `-arm64-setup.exe`, `-x64.msi`, `-arm64.msi`, plus VPAT)
      into `raggiesoft-servers\raggiesoft-assets\desktop-apps\windows\apps\<slug>\`.
    - Updates `apps-manifest.json` with fresh `SHA-256` digests and byte sizes.
    - Updates `desktop-apps\winget\manifests\r\RaggieSoft\<App>\<Ver>\*.installer.yaml`
      to point directly to the Dual-Layer signed native `x64` & `arm64` `.msi`
      packages with exact MSI `ProductCode` and `UpgradeCode` GUIDs!
================================================================================
#>

[CmdletBinding()]
param(
    [string]$DlibPath     = "y:\ai-staging\signing\packages\microsoft.trusted.signing.client\1.0.95\bin\x64\Azure.CodeSigning.Dlib.dll",
    [string]$MetadataPath = "y:\ai-staging\signing\session-signing-metadata.json",
    [string]$FallbackMeta = "y:\ai-staging\signing\raggiesoft-signing-metadata.json",
    [string]$TimestampUrl = "http://timestamp.acs.microsoft.com",
    [switch]$SkipExistingApps,
    [switch]$IncludeAllInOneBoxSet
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $RepoRoot

# ==============================================================================
# SAFETY GATE: Verify `raggiesoft-installer-studio-setup.exe` stays 100% frozen!
# ==============================================================================
$FrozenStudioExe  = "y:\raggiesoft-servers\raggiesoft-assets\desktop-apps\windows\apps\installer-studio\raggiesoft-installer-studio-setup.exe"
$ExpectedStudioSha = "DBFA09DA994353B738ED49EC5CB05689EE846B93708EC32DD4FD7490D425A074"
function Assert-StudioBinaryFrozen {
    $actual = (Get-FileHash -Algorithm SHA256 $FrozenStudioExe).Hash
    if ($actual -ne $ExpectedStudioSha) {
        throw "SAFETY ABORT: Frozen Installer Studio hash changed ($actual != $ExpectedStudioSha)!"
    }
}
Assert-StudioBinaryFrozen

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

# Helper function: queries an `.msi` database's `Property` table via Windows Installer COM (`Msi.Installer`)
# so our `winget` `.installer.yaml` manifests receive the exact compiled `ProductCode` & `UpgradeCode`.
function Get-MsiPropertyValue {
    param([string]$MsiPath, [string]$PropName)
    $installer = New-Object -ComObject WindowsInstaller.Installer
    $db = $installer.GetType().InvokeMember("OpenDatabase", "InvokeMethod", $null, $installer, @($MsiPath, 0))
    $view = $db.GetType().InvokeMember("OpenView", "InvokeMethod", $null, $db, @("SELECT ``Value`` FROM ``Property`` WHERE ``Property`` = '$PropName'"))
    $view.GetType().InvokeMember("Execute", "InvokeMethod", $null, $view, $null) | Out-Null
    $rec = $view.GetType().InvokeMember("Fetch", "InvokeMethod", $null, $view, $null)
    $val = ""
    if ($null -ne $rec) {
        $val = $rec.GetType().InvokeMember("StringData", "GetProperty", $null, $rec, @(1))
    }
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($view) | Out-Null
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($db) | Out-Null
    [System.Runtime.InteropServices.Marshal]::ReleaseComObject($installer) | Out-Null
    return $val
}

# Helper function: signs one PE `.exe` or `.msi` with retry protection on transient timestamp blips,
# skipping inner `.exe` files that are ALREADY verified with our Authenticode certificate so re-runs stay fast!
function Invoke-AzureSignIfUnsigned {
    param([Parameter(Mandatory = $true)][string]$FilePath)
    if (-not (Test-Path $FilePath)) { throw "Missing binary to sign: $FilePath" }
    $null = & $signToolX64 verify /pa /q $FilePath 2>&1
    if ($LASTEXITCODE -eq 0) {
        Write-Host "  [ALREADY SIGNED] $(Split-Path -Leaf $FilePath)" -ForegroundColor DarkGreen
        return
    }
    $leaf = Split-Path -Leaf $FilePath
    for ($attempt = 1; $attempt -le 4; $attempt++) {
        $out = & $signToolX64 sign /fd SHA256 /td SHA256 /tr $TimestampUrl /dlib $DlibPath /dmdf $activeMeta $FilePath 2>&1
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  [SIGNED] $FilePath" -ForegroundColor Green
            return
        }
        if ($attempt -lt 4) {
            Write-Host "  [RETRY $attempt/3] Waiting on Azure timestamp ($leaf)..." -ForegroundColor Yellow
            Start-Sleep -Seconds 4
        } else {
            throw "Failed signing $FilePath (Exit $LASTEXITCODE): $($out -join "`n")"
        }
    }
}

$hostIsArm64 = ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") -or ($env:PROCESSOR_ARCHITEW6432 -eq "ARM64")
$nativeArch  = if ($hostIsArm64) { "arm64" } else { "x64" }
$instBuild   = Join-Path $RepoRoot "bin\$nativeArch\instbuild.exe"

try {
    $env:RSW_SIGN_ENABLED       = "1"
    $env:RSW_SIGN_DLIB          = $DlibPath
    $env:RSW_SIGN_METADATA      = $activeMeta
    $env:RSW_SIGN_TIMESTAMP_URL = $TimestampUrl

    # Step 1: Sign all inner application `.exe` binaries (`x64` & `arm64`) BEFORE packing `#payload.cab`!
    Write-Host "[1/4] Ensuring all inner x64 & ARM64 application executables are Authenticode-signed..." -ForegroundColor Cyan
    $innerExes = @(
        "y:\desktop-apps\windows\RaggieSoftApps\RaggieSoftLauncher\bin\Release\net10.0-windows\win-x64\publish\RaggieSoftLauncher.exe",
        "y:\desktop-apps\windows\RaggieSoftApps\RaggieSoftLauncher\bin\Release\net10.0-windows\win-arm64\publish\RaggieSoftLauncher.exe",
        "y:\desktop-apps\windows\RaggieSoftApps\RaggieSoftPkgCli\bin\Release\net10.0\win-x64\publish\RaggieSoftPkgCli.exe",
        "y:\desktop-apps\windows\RaggieSoftApps\RaggieSoftPkgCli\bin\Release\net10.0\win-arm64\publish\RaggieSoftPkgCli.exe",
        "y:\desktop-apps\windows\RaggieSoftApps\BookReader\bin\Release\net10.0-windows\win-x64\publish\BookReader.exe",
        "y:\desktop-apps\windows\RaggieSoftApps\BookReader\bin\Release\net10.0-windows\win-arm64\publish\BookReader.exe",
        "y:\desktop-apps\windows\RaggieSoftApps\ListeningRoom\bin\Release\net10.0-windows\win-x64\publish\ListeningRoom.exe",
        "y:\desktop-apps\windows\RaggieSoftApps\ListeningRoom\bin\Release\net10.0-windows\win-arm64\publish\ListeningRoom.exe",
        "y:\desktop-apps\windows\RaggieSoftApps\Backstage\bin\Release\net10.0-windows\win-x64\publish\Backstage.exe",
        "y:\desktop-apps\windows\RaggieSoftApps\Backstage\bin\Release\net10.0-windows\win-arm64\publish\Backstage.exe",
        "y:\desktop-apps\windows\RaggieSoftApps\StoreTrainer\bin\Release\net10.0-windows\win-x64\publish\StoreTrainer.exe",
        "y:\desktop-apps\windows\RaggieSoftApps\StoreTrainer\bin\Release\net10.0-windows\win-arm64\publish\StoreTrainer.exe",
        "y:\desktop-apps\accessibility-studio\build\INSTALLDIR\Release\x64\StudioIDE\AccessibilityStudio.exe",
        "y:\desktop-apps\accessibility-studio\build\INSTALLDIR\Release\arm64\StudioIDE\AccessibilityStudio.exe",
        "y:\desktop-apps\accessibility-studio\build\INSTALLDIR\Release\x64\CLI\a11y-studio-cli.exe",
        "y:\desktop-apps\accessibility-studio\build\INSTALLDIR\Release\arm64\CLI\a11y-studio-cli.exe",
        "y:\desktop-apps\accessibility-studio\build\INSTALLDIR\Release\x64\HelpViewer\RaggieSoftHelp.exe",
        "y:\desktop-apps\accessibility-studio\build\INSTALLDIR\Release\arm64\HelpViewer\RaggieSoftHelp.exe"
    )
    foreach ($ie in $innerExes) {
        if (Test-Path $ie) {
            Invoke-AzureSignIfUnsigned -FilePath $ie
        }
    }

    if (-not $SkipExistingApps) {
        # Step 2: Compile & Dual-Layer sign all 6 desktop applications + stage to CDN + update manifests!
        Write-Host "[2/4] Compiling & Dual-Layer signing all 6 RaggieSoft applications with Installer Studio v0.5.0..." -ForegroundColor Cyan
        $appSpecs = @(
            @{ Slug = "launcher";             WingetDir = "Launcher";            WingetId = "RaggieSoft.Launcher";            Ver = "1.0.0" },
            @{ Slug = "book-reader";          WingetDir = "BookReader";          WingetId = "RaggieSoft.BookReader";          Ver = "1.0.0" },
            @{ Slug = "listening-room";       WingetDir = "ListeningRoom";       WingetId = "RaggieSoft.ListeningRoom";       Ver = "1.0.0" },
            @{ Slug = "backstage";            WingetDir = "Backstage";           WingetId = "RaggieSoft.Backstage";           Ver = "1.0.0" },
            @{ Slug = "storetrainer";         WingetDir = "StoreTrainer";        WingetId = "RaggieSoft.StoreTrainer";        Ver = "2.0.0" },
            @{ Slug = "accessibility-studio"; WingetDir = "AccessibilityStudio"; WingetId = "RaggieSoft.AccessibilityStudio"; Ver = "1.0.0" }
        )

        $appsManifestPath = "y:\raggiesoft-servers\raggiesoft-assets\desktop-apps\windows\apps-manifest.json"
        $appsManifestJson = Get-Content -Raw $appsManifestPath | ConvertFrom-Json

        foreach ($spec in $appSpecs) {
            $slug     = $spec.Slug
            $projDir  = Join-Path $RepoRoot "projects\apps\$slug"
            $rsipPath = Join-Path $projDir  "$slug.rsip"
            $outDir   = Join-Path $projDir  "out"
            $cdnDir   = "y:\raggiesoft-servers\raggiesoft-assets\desktop-apps\windows\apps\$slug"

            Write-Host ""
            Write-Host "  ---> Building & Signing [$slug] ($($spec.WingetId) v$($spec.Ver))..." -ForegroundColor Cyan
            Remove-Item -Recurse -Force $outDir -ErrorAction SilentlyContinue

            & $instBuild $rsipPath --with-vpat
            if ($LASTEXITCODE -ne 0) { throw "instbuild.exe failed for $rsipPath!" }

            New-Item -ItemType Directory -Force -Path $cdnDir | Out-Null
            $expectedFiles = @(
                "raggiesoft-$slug-setup.exe",
                "raggiesoft-$slug-offline-setup.exe",
                "raggiesoft-$slug-x64-setup.exe",
                "raggiesoft-$slug-arm64-setup.exe",
                "raggiesoft-$slug-x64.msi",
                "raggiesoft-$slug-arm64.msi"
            )
            foreach ($ef in $expectedFiles) {
                $builtFile = Join-Path $outDir $ef
                if (-not (Test-Path $builtFile)) { throw "Missing expected output: $builtFile" }
                $vOut = & $signToolX64 verify /pa /q $builtFile 2>&1
                if ($LASTEXITCODE -ne 0) { throw "signtool verify /pa FAILED on $builtFile`n$($vOut -join "`n")" }
                Copy-Item -Force $builtFile (Join-Path $cdnDir $ef)
                $kb = [math]::Round((Get-Item $builtFile).Length / 1KB, 1)
                Write-Host "       [VERIFIED & STAGED] $ef ($kb KB)" -ForegroundColor Green
            }

            # Also stage generated VPAT 2.5 HTML report if emitted
            Get-ChildItem -File $outDir -Filter "*vpat*.html" -ErrorAction SilentlyContinue | ForEach-Object {
                Copy-Item -Force $_.FullName (Join-Path $cdnDir $_.Name)
            }

            # Update `apps-manifest.json` entry (`raggiesoft-<slug>-setup.exe` SHA-256 & byte size)
            $webStubPath = Join-Path $cdnDir "raggiesoft-$slug-setup.exe"
            $webStubHash = (Get-FileHash -Algorithm SHA256 $webStubPath).Hash
            $webStubSize = (Get-Item $webStubPath).Length
            foreach ($entry in $appsManifestJson) {
                if ($entry.id -eq $slug) {
                    $entry.sha256    = $webStubHash
                    $entry.sizeBytes = $webStubSize
                }
            }

            # Update `winget` v1.9.0 `.installer.yaml` with native Dual-Layer signed `x64` & `arm64` MSIs!
            $x64MsiPath   = Join-Path $cdnDir "raggiesoft-$slug-x64.msi"
            $arm64MsiPath = Join-Path $cdnDir "raggiesoft-$slug-arm64.msi"
            $x64Sha       = (Get-FileHash -Algorithm SHA256 $x64MsiPath).Hash
            $arm64Sha     = (Get-FileHash -Algorithm SHA256 $arm64MsiPath).Hash
            $x64ProdCode  = Get-MsiPropertyValue -MsiPath $x64MsiPath   -PropName "ProductCode"
            $arm64ProdCode= Get-MsiPropertyValue -MsiPath $arm64MsiPath -PropName "ProductCode"
            $upgCode      = Get-MsiPropertyValue -MsiPath $x64MsiPath   -PropName "UpgradeCode"

            $wingetInstYaml = "y:\desktop-apps\winget\manifests\r\RaggieSoft\$($spec.WingetDir)\$($spec.Ver)\$($spec.WingetId).installer.yaml"
            if (Test-Path (Split-Path -Parent $wingetInstYaml)) {
                $yamlLines = @(
                    "# Created with RaggieSoft Installer Studio v0.5.0",
                    "# yaml-language-server: `$schema=https://aka.ms/winget-manifest.installer.1.9.0.schema.json",
                    "",
                    "PackageIdentifier: $($spec.WingetId)",
                    "PackageVersion: $($spec.Ver)",
                    "Platform:",
                    "  - Windows.Desktop",
                    "MinimumOSVersion: 10.0.22000.0",
                    "InstallerType: msi",
                    "Scope: machine",
                    "InstallModes:",
                    "  - interactive",
                    "  - silent",
                    "  - silentWithProgress",
                    "UpgradeBehavior: install",
                    "ReleaseDate: 2026-11-25",
                    "AppsAndFeaturesEntries:",
                    "  - UpgradeCode: '$upgCode'",
                    "Installers:",
                    "  - Architecture: x64",
                    "    InstallerUrl: https://assets.raggiesoft.com/desktop-apps/windows/apps/$slug/raggiesoft-$slug-x64.msi",
                    "    InstallerSha256: $x64Sha",
                    "    ProductCode: '$x64ProdCode'",
                    "  - Architecture: arm64",
                    "    InstallerUrl: https://assets.raggiesoft.com/desktop-apps/windows/apps/$slug/raggiesoft-$slug-arm64.msi",
                    "    InstallerSha256: $arm64Sha",
                    "    ProductCode: '$arm64ProdCode'",
                    "ManifestType: installer",
                    "ManifestVersion: 1.9.0"
                )
                [System.IO.File]::WriteAllLines($wingetInstYaml, $yamlLines, (New-Object System.Text.UTF8Encoding($false)))
                Write-Host "       [WINGET UPDATED] $($spec.WingetId).installer.yaml (MSI x64 + arm64)" -ForegroundColor Cyan
            }
        }

        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        $formattedJson = ($appsManifestJson | ConvertTo-Json -Depth 6) + "`n"
        [System.IO.File]::WriteAllText($appsManifestPath, $formattedJson, $utf8NoBom)
        Write-Host "[OK] Updated apps-manifest.json with v0.5.0 signed web-stub hashes!" -ForegroundColor Green
    }

    if ($IncludeAllInOneBoxSet) {
        Write-Host ""
        Write-Host "[3/4] Compiling & Dual-Layer signing ~1.48 GB All-in-One Collector's Box Set in ai-staging..." -ForegroundColor Cyan
        $boxRsip = "y:\ai-staging\all-in-one-boxset\raggiesoft-all-in-one.rsip"
        $boxOut  = "y:\ai-staging\all-in-one-boxset\out"
        Remove-Item -Recurse -Force $boxOut -ErrorAction SilentlyContinue

        $swBox = [System.Diagnostics.Stopwatch]::StartNew()
        & $instBuild $boxRsip --arch=$nativeArch --with-vpat
        if ($LASTEXITCODE -ne 0) { throw "All-in-One Box Set build failed!" }
        $swBox.Stop()

        foreach ($bf in @(
            "raggiesoft-collectors-boxset-$nativeArch.msi",
            "raggiesoft-collectors-boxset-$nativeArch-setup.exe"
        )) {
            $bp = Join-Path $boxOut $bf
            if (-not (Test-Path $bp)) { throw "Missing Box Set artifact: $bp" }
            $vOut = & $signToolX64 verify /pa /q $bp 2>&1
            if ($LASTEXITCODE -ne 0) { throw "signtool verify /pa FAILED on $bp`n$($vOut -join "`n")" }
            $mb = [math]::Round((Get-Item $bp).Length / 1MB, 1)
            Write-Host "  [BOX SET VERIFIED /pa] $bf ($mb MB) — built & signed in $([math]::Round($swBox.Elapsed.TotalSeconds, 1))s!" -ForegroundColor Green
        }
    }

    Assert-StudioBinaryFrozen
    Write-Host ""
    Write-Host "[4/4] COMPLETE! Frozen Installer Studio v0.5.0 hashes verified untouched ($ExpectedStudioSha)." -ForegroundColor Green
}
finally {
    $env:RSW_SIGN_ENABLED       = $null
    $env:RSW_SIGN_DLIB          = $null
    $env:RSW_SIGN_METADATA      = $null
    $env:RSW_SIGN_TIMESTAMP_URL = $null
    if (Test-Path $MetadataPath) {
        Remove-Item -Force $MetadataPath -ErrorAction SilentlyContinue
        Write-Host "  [CLEANUP] Scrubbed ephemeral session token metadata." -ForegroundColor DarkGray
    }
}
