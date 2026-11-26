<#
================================================================================
FILE: build-and-verify.ps1
PROJECT: RaggieSoft Installer Studio ("Install Without Limits")
WEBSITE: https://raggiesoftstudio.com  |  DEV HUB: https://raggiesoft.dev
LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)

PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
  Compiles and verifies the entire **RaggieSoft Installer Studio** native Win32
  C++20 toolkit (`x64` + `ARM64`, static `/MT`, zero external dependencies):
    - `instbuild.exe`       : CLI compiler (`.rsip` & `.rsis`), `--emit-ai-spec`, & `--emit-vpat`
    - `SetupWizard.exe`     : Accessible customer-facing runtime wizard stub
    - `InstallerStudio.exe` : 10-view native Win32 C++20 Visual IDE (`.rsip` & `.rsis`)
    - `msiprism.exe`        : Native 64-bit MSI, MSM, MST & MSP SQL table inspector ("MSI Prism")
    - `RaggieSoftHelp.exe`  : Zero-dependency C++20 offline Help Viewer (`.rshm.json` + Named Pipe F1 IPC)
    - `insttests.exe`       : Automated C++20 unit & integration self-tests
    - `HelloRaggieSoft.exe` : Sample target payload executable
================================================================================
#>

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $RepoRoot

Write-Host "======================================================================" -ForegroundColor Cyan
Write-Host "  RaggieSoft Installer Studio v0.6.0 - x64 + ARM64 Build & Test Suite" -ForegroundColor Cyan
Write-Host "======================================================================" -ForegroundColor Cyan

# Step 1: Locate Visual Studio `vcvarsall.bat` for `cl.exe`, `rc.exe`, & Windows 11 SDK
$vsCandidates = @(
    "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files\Microsoft Visual Studio\18\Professional\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files\Microsoft Visual Studio\18\Enterprise\VC\Auxiliary\Build\vcvarsall.bat",
    "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvarsall.bat"
)
$vcvars = $null
foreach ($c in $vsCandidates) {
    if (Test-Path $c) { $vcvars = $c; break }
}
if (-not $vcvars) { throw "Could not find Visual Studio vcvarsall.bat." }
Write-Host "[1/6] Using Visual C++ Toolchain: $vcvars" -ForegroundColor Green

$sampleDir   = Join-Path $RepoRoot "examples\hello-raggiesoft"
$sampleIco   = Join-Path $sampleDir "HelloRaggieSoft.ico"
$launcherIco = Join-Path $RepoRoot "..\windows\RaggieSoftApps\RaggieSoftLauncher\Assets\RaggieSoftLauncher.ico"
if (Test-Path $launcherIco) { Copy-Item $launcherIco $sampleIco -Force }

$hostIsArm64 = ($env:PROCESSOR_ARCHITECTURE -eq "ARM64") -or ($env:PROCESSOR_ARCHITEW6432 -eq "ARM64")

# Step 2: Compile static `/MT` binaries for both `arm64` and `x64`
foreach ($targetArch in @("arm64", "x64")) {
    $vcvarsArg = if ($hostIsArm64) {
        if ($targetArch -eq "arm64") { "arm64" } else { "arm64_x64" }
    } else {
        if ($targetArch -eq "arm64") { "amd64_arm64" } else { "amd64" }
    }

    Write-Host "[2/6] Compiling static /MT C++20 suite for [$targetArch] (vcvarsall $vcvarsArg)..." -ForegroundColor Cyan
    $outBinDir = Join-Path $RepoRoot "bin\$targetArch"
    $objDir    = Join-Path $env:TEMP "installerstudio-obj-$targetArch"
    Remove-Item -Recurse -Force $outBinDir -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $outBinDir | Out-Null
    New-Item -ItemType Directory -Force -Path $objDir    | Out-Null

    $batchPath = Join-Path $objDir "compile-$targetArch.cmd"
    $batchLines = @(
        "@echo off",
        "call `"$vcvars`" $vcvarsArg >nul || exit /b 1",
        "rc.exe /nologo /fo `"$objDir\RswBuild.res`" `"$RepoRoot\src\compiler\RswBuild.rc`" || exit /b 1",
        "rc.exe /nologo /fo `"$objDir\RswSetup.res`" `"$RepoRoot\src\bootstrapper\RswSetupBootstrapper.rc`" || exit /b 1",
        "rc.exe /nologo /fo `"$objDir\RswIde.res`" `"$RepoRoot\src\ide\RswIde.rc`" || exit /b 1",
        "rc.exe /nologo /fo `"$objDir\MsiPrism.res`" `"$RepoRoot\src\prism\MsiPrism.rc`" || exit /b 1",
        "rc.exe /nologo /fo `"$objDir\RswHelp.res`" `"$RepoRoot\src\help\RswHelpViewer.rc`" || exit /b 1",
        "rc.exe /nologo /fo `"$objDir\Hello.res`" `"$RepoRoot\examples\hello-raggiesoft\HelloRaggieSoft.rc`" || exit /b 1",
        "cl.exe /nologo /std:c++20 /O2 /MT /EHsc /utf-8 /W4 `"$RepoRoot\src\compiler\RswBuildMain.cpp`" `"$objDir\RswBuild.res`" /Fo`"$objDir\RswBuildMain.obj`" /Fe`"$outBinDir\instbuild.exe`" /link /INCREMENTAL:NO || exit /b 1",
        "cl.exe /nologo /std:c++20 /O2 /MT /EHsc /utf-8 /W4 `"$RepoRoot\src\bootstrapper\RswSetupMain.cpp`" `"$objDir\RswSetup.res`" /Fo`"$objDir\RswSetupMain.obj`" /Fe`"$outBinDir\SetupWizard.exe`" /link /INCREMENTAL:NO /SUBSYSTEM:WINDOWS || exit /b 1",
        "cl.exe /nologo /std:c++20 /O2 /MT /EHsc /utf-8 /W4 `"$RepoRoot\src\ide\RswIdeMain.cpp`" `"$objDir\RswIde.res`" /Fo`"$objDir\RswIdeMain.obj`" /Fe`"$outBinDir\InstallerStudio.exe`" /link /INCREMENTAL:NO /SUBSYSTEM:WINDOWS || exit /b 1",
        "cl.exe /nologo /std:c++20 /O2 /MT /EHsc /utf-8 /W4 `"$RepoRoot\src\prism\MsiPrismMain.cpp`" `"$objDir\MsiPrism.res`" /Fo`"$objDir\MsiPrismMain.obj`" /Fe`"$outBinDir\msiprism.exe`" /link /INCREMENTAL:NO /SUBSYSTEM:WINDOWS || exit /b 1",
        "cl.exe /nologo /std:c++20 /O2 /MT /EHsc /utf-8 /W4 `"$RepoRoot\src\help\RswHelpMain.cpp`" `"$objDir\RswHelp.res`" /Fo`"$objDir\RswHelpMain.obj`" /Fe`"$outBinDir\RaggieSoftHelp.exe`" /link /INCREMENTAL:NO /SUBSYSTEM:WINDOWS || exit /b 1",
        "cl.exe /nologo /std:c++20 /O2 /MT /EHsc /utf-8 /W4 `"$RepoRoot\src\tests\RswSelfTests.cpp`" /Fo`"$objDir\RswSelfTests.obj`" /Fe`"$outBinDir\insttests.exe`" /link /INCREMENTAL:NO || exit /b 1",
        "cl.exe /nologo /std:c++20 /O2 /MT /EHsc /utf-8 /W4 `"$RepoRoot\examples\hello-raggiesoft\HelloRaggieSoft.cpp`" `"$objDir\Hello.res`" /Fo`"$objDir\Hello.obj`" /Fe`"$outBinDir\HelloRaggieSoft.exe`" /link /INCREMENTAL:NO /SUBSYSTEM:WINDOWS || exit /b 1"
    )
    Set-Content -Path $batchPath -Value ($batchLines -join "`r`n") -Encoding ASCII

    & cmd.exe /c "`"$batchPath`""
    if ($LASTEXITCODE -ne 0) { throw "C++20 compilation failed for [$targetArch]!" }

    # Stage offline `.rshm.json` Help Module into `bin\<arch>\HelpModules\raggiesoft.installer-studio`
    $helpSrcDir = Join-Path $RepoRoot "help\raggiesoft.installer-studio"
    $helpDstDir = Join-Path $outBinDir "HelpModules\raggiesoft.installer-studio"
    New-Item -ItemType Directory -Force -Path $helpDstDir | Out-Null
    Copy-Item -Recurse -Force (Join-Path $helpSrcDir "*") $helpDstDir

    Write-Host "      [OK] Built instbuild.exe, SetupWizard.exe, InstallerStudio.exe, msiprism.exe, RaggieSoftHelp.exe & insttests.exe ($targetArch)." -ForegroundColor Green
}

$nativeArch = if ($hostIsArm64) { "arm64" } else { "x64" }
$instBuild  = Join-Path $RepoRoot "bin\$nativeArch\instbuild.exe"
$instStudio = Join-Path $RepoRoot "bin\$nativeArch\InstallerStudio.exe"
$rswHelp    = Join-Path $RepoRoot "bin\$nativeArch\RaggieSoftHelp.exe"
$instTests  = Join-Path $RepoRoot "bin\$nativeArch\insttests.exe"

# Step 3: Emit AI Specs (`specs/` + `.agents/skills/raggiesoft-installer-studio/`)
Write-Host "[3/6] Emitting constexpr AI specs (specs\ + .agents\skills\raggiesoft-installer-studio\)..." -ForegroundColor Cyan
$specsDir = Join-Path $RepoRoot "specs"
& $instBuild --emit-ai-spec $specsDir
if ($LASTEXITCODE -ne 0) { throw "--emit-ai-spec failed!" }

$oldSkillDir = Join-Path $RepoRoot "..\..\.agents\skills\raggiesoft-setup-studio"
if (Test-Path $oldSkillDir) { Remove-Item -Recurse -Force $oldSkillDir }

$wsSkillDir = Join-Path $RepoRoot "..\..\.agents\skills\raggiesoft-installer-studio"
New-Item -ItemType Directory -Force -Path $wsSkillDir | Out-Null
Copy-Item (Join-Path $specsDir "SKILL.md")      (Join-Path $wsSkillDir "SKILL.md")      -Force
Copy-Item (Join-Path $specsDir "llms-full.txt") (Join-Path $wsSkillDir "llms-full.txt") -Force
Copy-Item (Join-Path $specsDir "llms.txt")      (Join-Path $wsSkillDir "llms.txt")      -Force

# Step 4: Validate & compile standalone `.rsip`, `@include` `.rsii`, AND `.rsis` Multi-Project Suite!
Write-Host "[4/6] Building sample .rsip + .rsii + .rsis Suite (MSI, Setup.exe, Autorun, MSM, & VPAT 2.5)..." -ForegroundColor Cyan
$stdManifest     = Join-Path $sampleDir "hello-raggiesoft.rsip"
$suiteManifest   = Join-Path $sampleDir "hello-raggiesoft-suite.rsis"
$sampleOutDir    = Join-Path $sampleDir "out"
Remove-Item -Recurse -Force $sampleOutDir -ErrorAction SilentlyContinue

& $instBuild --validate --diagnostics=json $suiteManifest | Out-Null
if ($LASTEXITCODE -ne 0) { throw "Suite validation failed on hello-raggiesoft-suite.rsis!" }

& $instBuild $suiteManifest --with-vpat
if ($LASTEXITCODE -ne 0) { throw "Suite MSI + Setup.exe + VPAT 2.5 build failed!" }

foreach ($a in @("arm64", "x64")) {
    $msmPath = Join-Path $sampleOutDir "hello-raggiesoft-$a.msm"
    & $instBuild --emit-msm $stdManifest $msmPath "--arch=$a"
    if ($LASTEXITCODE -ne 0) { throw "Merge Module (.msm) build failed ($a)!" }
}

# Step 5: Smoke-test 10-View Visual IDE (`InstallerStudio.exe`) & Native C++ Help Viewer (`RaggieSoftHelp.exe`)
Write-Host "[5/6] Verifying Visual IDE & Native C++ Help Viewer (--headless-smoke-test)..." -ForegroundColor Cyan
& $instStudio --headless-smoke-test $stdManifest
if ($LASTEXITCODE -ne 0) { throw "InstallerStudio.exe headless smoke test failed ($LASTEXITCODE)!" }
& $rswHelp --headless-smoke-test
if ($LASTEXITCODE -ne 0) { throw "RaggieSoftHelp.exe headless smoke test failed ($LASTEXITCODE)!" }
Write-Host "      [OK] InstallerStudio.exe & RaggieSoftHelp.exe headless smoke tests passed." -ForegroundColor Green

# Step 6: Run automated C++20 self-test suite (`insttests.exe`)
Write-Host "[6/6] Running automated C++20 self-test suite (insttests.exe)..." -ForegroundColor Cyan
& $instTests
if ($LASTEXITCODE -ne 0) { throw "insttests.exe reported failures!" }

Write-Host ""
Write-Host "ALL RAGGIESOFT INSTALLER STUDIO v0.6.0 x64 + ARM64 BINARIES, UNIVERSAL EXE, HELP VIEWER, AI SPECS, SUITE, VPAT, AND TESTS PASSED!" -ForegroundColor Green
