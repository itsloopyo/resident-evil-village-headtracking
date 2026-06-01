[CmdletBinding()]
param(
    [switch]$AllowDirty
)

$ErrorActionPreference = 'Stop'

$ProjectRoot = Resolve-Path (Join-Path $PSScriptRoot '..')

Import-Module (Join-Path $ProjectRoot 'cameraunlock-core\powershell\NightlyRelease.psm1') -Force

$manifestFile = Join-Path $ProjectRoot 'manifest.json'
$versionMatch = Select-String -Path $manifestFile -Pattern '"version"\s*:\s*"([^"]+)"'
if (-not $versionMatch) {
    throw "Could not extract version from $manifestFile"
}
$version = $versionMatch.Matches[0].Groups[1].Value

Publish-NightlyBuild `
    -ModId 'resident-evil-village' `
    -ModName 'RE8HeadTracking' `
    -Version $version `
    -ProjectRoot $ProjectRoot `
    -AllowDirty:$AllowDirty
