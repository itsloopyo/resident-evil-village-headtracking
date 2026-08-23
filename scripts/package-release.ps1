#!/usr/bin/env pwsh
#Requires -Version 5.1
# Package RE8 Head Tracking release ZIPs

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

# Get version from manifest
$manifest = Get-Content (Join-Path $projectDir "manifest.json") | ConvertFrom-Json
$version = $manifest.version

Write-Host "=== RE8 Head Tracking - Package Release ===" -ForegroundColor Magenta
Write-Host ""
Write-Host "Version: $version" -ForegroundColor Cyan
Write-Host ""

$releaseDir = Join-Path $projectDir "release"
if (-not (Test-Path $releaseDir)) {
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
}

# Validate required source files
$dllPath = Join-Path $projectDir "bin/Release/RE8HeadTracking.dll"
if (-not (Test-Path $dllPath)) {
    throw "RE8HeadTracking.dll not found at: $dllPath"
}

$iniPath = Join-Path $projectDir "HeadTracking.ini"
if (-not (Test-Path $iniPath)) {
    throw "HeadTracking.ini not found at: $iniPath"
}

$scriptsDir = Join-Path $projectDir "scripts"
foreach ($script in @("install.cmd", "uninstall.cmd")) {
    $scriptPath = Join-Path $scriptsDir $script
    if (-not (Test-Path $scriptPath)) {
        throw "Required script not found: $scriptPath"
    }
}

# --- GitHub Release ZIP ---
Write-Host "--- GitHub Release ZIP ---" -ForegroundColor Yellow
Write-Host ""

$ghStagingDir = Join-Path $releaseDir "staging-github"
if (Test-Path $ghStagingDir) { Remove-Item -Recurse -Force $ghStagingDir }
New-Item -ItemType Directory -Path $ghStagingDir -Force | Out-Null

foreach ($script in @("install.cmd", "uninstall.cmd")) {
    Copy-Item (Join-Path $scriptsDir $script) -Destination $ghStagingDir -Force
    Write-Host "  $script" -ForegroundColor Green
}

Copy-SharedBundle -StagingDir $ghStagingDir -CoreRoot (Join-Path $projectDir 'cameraunlock-core')

$pluginsDir = Join-Path $ghStagingDir "plugins"
New-Item -ItemType Directory -Path $pluginsDir -Force | Out-Null

Copy-Item $dllPath -Destination $pluginsDir -Force
Write-Host "  plugins/RE8HeadTracking.dll" -ForegroundColor Green

Copy-Item $iniPath -Destination $pluginsDir -Force
Write-Host "  plugins/HeadTracking.ini" -ForegroundColor Green

# Stage the vendored REFramework so install.cmd can extract it offline.
# Vendor tree is the install-time source of truth; the build/package
# pipeline never refreshes it - bump via `pixi run update-deps`.
$vendorSrcDir = Join-Path $projectDir "vendor\reframework"
$vendorZip = Join-Path $vendorSrcDir "REFramework.zip"
if (-not (Test-Path $vendorZip)) {
    throw "Vendored REFramework not found at $vendorZip. Run 'pixi run update-deps' to populate it, then commit the result."
}
$vendorDstDir = Join-Path $ghStagingDir "vendor\reframework"
New-Item -ItemType Directory -Path $vendorDstDir -Force | Out-Null
foreach ($vendorFile in @("REFramework.zip", "LICENSE", "README.md")) {
    $srcPath = Join-Path $vendorSrcDir $vendorFile
    if (-not (Test-Path $srcPath)) {
        throw "Vendored REFramework file missing: $srcPath. Run 'pixi run update-deps' to refresh."
    }
    Copy-Item $srcPath -Destination $vendorDstDir -Force
    Write-Host "  vendor/reframework/$vendorFile" -ForegroundColor Green
}

# Stage the launcher manifest at the ZIP root. The launcher (Lopari) reads
# this for native, receipt-tracked deployment; install.cmd is the legacy path.
# Stamp the real release version and refresh the seeded default config from the
# live HeadTracking.ini so the committed base64 can never drift out of sync.
$manifestSrc = Join-Path $projectDir "launcher-manifest.json"
if (-not (Test-Path $manifestSrc)) {
    throw "launcher-manifest.json not found at $manifestSrc."
}
# Stamp via raw-text replacement, not a ConvertTo-Json round-trip: PowerShell
# 5.1's ConvertTo-Json unwraps single-element arrays (files/archives/seed),
# which would silently corrupt the manifest the launcher parses.
$manifestText = Get-Content $manifestSrc -Raw
$iniB64 = [System.Convert]::ToBase64String([System.IO.File]::ReadAllBytes($iniPath))
$manifestText = $manifestText -replace '("version":\s*")[^"]*(")', "`${1}$version`${2}"
$manifestText = $manifestText -replace '("content_b64":\s*")[^"]*(")', "`${1}$iniB64`${2}"
Set-Content (Join-Path $ghStagingDir "launcher-manifest.json") -Value $manifestText -NoNewline
Write-Host "  launcher-manifest.json (v$version)" -ForegroundColor Green

# LICENSE and THIRD-PARTY-NOTICES.md are licence obligations, not niceties:
# the ZIP redistributes the vendored REFramework loader and a DLL with
# REFramework's SDK headers, MinHook and cameraunlock-core compiled in. A
# missing one is a compliance failure, so fail the build rather than skipping
# the copy and going green.
$docFiles = @("README.md", "LICENSE", "CHANGELOG.md", "THIRD-PARTY-NOTICES.md")
foreach ($doc in $docFiles) {
    $docPath = Join-Path $projectDir $doc
    if (-not (Test-Path $docPath)) {
        throw "Required document not found: $doc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $docPath -Destination $ghStagingDir -Force
    Write-Host "  $doc" -ForegroundColor Green
}

$ghZipName = "RE8HeadTracking-v$version-installer.zip"
$ghZipPath = Join-Path $releaseDir $ghZipName
if (Test-Path $ghZipPath) { Remove-Item $ghZipPath -Force }

Write-Host ""
Write-Host "Creating GitHub ZIP..." -ForegroundColor Cyan

Push-Location $ghStagingDir
try {
    Compress-Archive -Path ".\*" -DestinationPath $ghZipPath -Force
} finally {
    Pop-Location
}
Remove-Item -Recurse -Force $ghStagingDir

$ghZipSize = (Get-Item $ghZipPath).Length / 1KB
Write-Host ("  $ghZipPath ({0:N1} KB)" -f $ghZipSize) -ForegroundColor Green

# --- Nexus Mods ZIP ---
Write-Host ""
Write-Host "--- Nexus Mods ZIP ---" -ForegroundColor Yellow
Write-Host ""

$nexusStagingDir = Join-Path $releaseDir "staging-nexus"
if (Test-Path $nexusStagingDir) { Remove-Item -Recurse -Force $nexusStagingDir }

$nexusPluginsDir = Join-Path $nexusStagingDir "reframework\plugins"
New-Item -ItemType Directory -Path $nexusPluginsDir -Force | Out-Null

Copy-Item $dllPath -Destination $nexusPluginsDir -Force
Write-Host "  reframework/plugins/RE8HeadTracking.dll" -ForegroundColor Green

Copy-Item $iniPath -Destination $nexusPluginsDir -Force
Write-Host "  reframework/plugins/HeadTracking.ini" -ForegroundColor Green

$nexusZipName = "RE8HeadTracking-v$version-nexus.zip"
$nexusZipPath = Join-Path $releaseDir $nexusZipName
if (Test-Path $nexusZipPath) { Remove-Item $nexusZipPath -Force }

Write-Host ""
Write-Host "Creating Nexus ZIP..." -ForegroundColor Cyan

# The Nexus ZIP is a binary distribution too: the licences of everything
# compiled into or bundled with the payload require their notices to travel
# with it, so LICENSE and THIRD-PARTY-NOTICES.md ship at its root.
foreach ($noticeDoc in @('LICENSE', 'THIRD-PARTY-NOTICES.md', 'README.md')) {
    $noticeSrc = Join-Path $projectDir $noticeDoc
    if (-not (Test-Path $noticeSrc)) {
        throw "Required notice file not found: $noticeDoc. Every published ZIP is a binary distribution and must carry it."
    }
    Copy-Item $noticeSrc -Destination $nexusStagingDir -Force
    Write-Host "  $noticeDoc" -ForegroundColor Green
}
Push-Location $nexusStagingDir
try {
    Compress-Archive -Path ".\*" -DestinationPath $nexusZipPath -Force
} finally {
    Pop-Location
}
Remove-Item -Recurse -Force $nexusStagingDir

$nexusZipSize = (Get-Item $nexusZipPath).Length / 1KB
Write-Host ("  $nexusZipPath ({0:N1} KB)" -f $nexusZipSize) -ForegroundColor Green

# --- Summary ---
Write-Host ""
Write-Host "=== Package Complete ===" -ForegroundColor Magenta
Write-Host ""
Write-Host ("GitHub Release: $ghZipPath ({0:N1} KB)" -f $ghZipSize) -ForegroundColor Green
Write-Host ("Nexus Mods:     $nexusZipPath ({0:N1} KB)" -f $nexusZipSize) -ForegroundColor Green

Write-Output $ghZipPath
Write-Output $nexusZipPath
