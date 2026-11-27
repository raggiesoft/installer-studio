<#
================================================================================
FILE: editors/package-vsix.ps1
PROJECT: RaggieScript Official VS Code Extension Packager (`.vsix`)
WEBSITE: https://raggiesoftstudio.com/raggiescript  |  DEV HUB: https://raggiesoft.dev
LICENSE: MIT License (Copyright (c) 2026 Michael P. Ragsdale / RaggieSoft)

PLAIN-ENGLISH OVERVIEW FOR 100-LEVEL READERS:
  How does a Visual Studio Code `.vsix` package work under the hood?
    A `.vsix` file is a standard Open Packaging Conventions (OPC) `.zip` archive
    containing:
      1. `[Content_Types].xml` — Declares MIME types for `.json`, `.js`, `.md`, & `.vsixmanifest`.
      2. `extension.vsixmanifest` — Declares publisher (`raggiesoft`), ID (`raggiescript`),
         version (`0.6.0`), and VS Code engine compatibility.
      3. `extension/` — Every file of the extension (`package.json`, `extension.js`,
         `language-configuration.json`, `syntaxes/*.json`, `snippets/*.code-snippets`, `README.md`).

  This zero-dependency PowerShell script builds `raggiescript-0.6.0.vsix` using
  .NET's built-in `System.IO.Compression.ZipArchive` (preserving forward-slash `/`
  entry paths required by VS Code across Windows, macOS, and Linux!).
================================================================================
#>

param(
    [string]$OutVsixPath = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
$ExtSrcDir = Join-Path $ScriptDir "vscode-raggiescript"
$PkgJson   = Get-Content -Raw (Join-Path $ExtSrcDir "package.json") | ConvertFrom-Json
$Version   = $PkgJson.version

if ([string]::IsNullOrWhiteSpace($OutVsixPath)) {
    $OutVsixPath = Join-Path $ScriptDir "dist\raggiescript-$Version.vsix"
}
$OutDir = Split-Path -Parent $OutVsixPath
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
if (Test-Path $OutVsixPath) { Remove-Item -Force $OutVsixPath }

$ContentTypesXml = @"
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension=".json" ContentType="application/json"/>
  <Default Extension=".code-snippets" ContentType="application/json"/>
  <Default Extension=".js" ContentType="application/javascript"/>
  <Default Extension=".md" ContentType="text/markdown"/>
  <Default Extension=".txt" ContentType="text/plain"/>
  <Default Extension=".vsixmanifest" ContentType="text/xml"/>
</Types>
"@

$VsixManifestXml = @"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="raggiescript" Version="$Version" Publisher="raggiesoft" />
    <DisplayName>RaggieScript &amp; RaggieSoft Studio Language Support</DisplayName>
    <Description xml:space="preserve">Syntax highlighting, live RSW701/RSW702 static diagnostics, IntelliSense, and build commands for RaggieScript (.rssc) and RaggieSoft Studio manifests (.rsip, .rsis, .rsii).</Description>
    <Tags>raggiescript,rssc,rsip,installer,msi,raggiesoft</Tags>
    <Categories>Programming Languages,Linters,Snippets</Categories>
    <GalleryFlags>Public</GalleryFlags>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code"/>
  </Installation>
  <Dependencies/>
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
  </Assets>
</PackageManifest>
"@

# Open `.vsix` stream and write UTF-8 XML headers + all `extension/` files with `/` slashes!
$fs = [System.IO.File]::Open($OutVsixPath, [System.IO.FileMode]::CreateNew)
try {
    $zip = New-Object System.IO.Compression.ZipArchive($fs, [System.IO.Compression.ZipArchiveMode]::Create, $false)
    try {
        $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
        foreach ($metaPair in @(
            @("[Content_Types].xml",    $ContentTypesXml),
            @("extension.vsixmanifest", $VsixManifestXml)
        )) {
            $entry = $zip.CreateEntry($metaPair[0], [System.IO.Compression.CompressionLevel]::Optimal)
            $sw = New-Object System.IO.StreamWriter($entry.Open(), $utf8NoBom)
            $sw.Write($metaPair[1])
            $sw.Dispose()
        }

        $allFiles = Get-ChildItem -Recurse -File $ExtSrcDir
        foreach ($f in $allFiles) {
            $rel = $f.FullName.Substring($ExtSrcDir.Length).TrimStart('\', '/').Replace('\', '/')
            $entryName = "extension/$rel"
            [System.IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                $zip, $f.FullName, $entryName, [System.IO.Compression.CompressionLevel]::Optimal
            ) | Out-Null
        }
    } finally {
        $zip.Dispose()
    }
} finally {
    $fs.Dispose()
}

Write-Host "[OK] Packaged VS Code Extension VSIX -> $OutVsixPath" -ForegroundColor Green
