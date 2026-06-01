#!/usr/bin/env pwsh
# Detect Resident Evil Village installation directory

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Find-RE8Installation {
    # Check environment variable override first
    if ($env:RE8_PATH) {
        $gamePath = $env:RE8_PATH
        if (Test-RE8Installation $gamePath) {
            return $gamePath
        }
        Write-Warning "RE8_PATH is set but path is invalid: $gamePath"
    }

    # Find Steam installation
    $steamPath = $null

    # Try registry (64-bit)
    try {
        $steamPath = (Get-ItemProperty -Path "HKLM:\SOFTWARE\WOW6432Node\Valve\Steam" -ErrorAction Stop).InstallPath
    } catch { }

    # Try registry (32-bit fallback)
    if (-not $steamPath) {
        try {
            $steamPath = (Get-ItemProperty -Path "HKLM:\SOFTWARE\Valve\Steam" -ErrorAction Stop).InstallPath
        } catch { }
    }

    if (-not $steamPath) {
        return $null
    }

    # Parse libraryfolders.vdf to find all Steam library paths
    $libraryFolders = @($steamPath)
    $vdfPath = Join-Path $steamPath "steamapps\libraryfolders.vdf"

    if (Test-Path $vdfPath) {
        $content = Get-Content $vdfPath -Raw
        $matches = [regex]::Matches($content, '"path"\s+"([^"]+)"')
        foreach ($match in $matches) {
            $path = $match.Groups[1].Value -replace '\\\\', '\'
            if ($path -and (Test-Path $path)) {
                $libraryFolders += $path
            }
        }
    }

    # Known folder names for RE Village
    $folderNames = @(
        "Resident Evil Village BIOHAZARD VILLAGE",
        "Resident Evil Village"
    )

    # Search each library for RE8
    foreach ($library in $libraryFolders) {
        foreach ($folderName in $folderNames) {
            $gamePath = Join-Path $library "steamapps\common\$folderName"
            if (Test-RE8Installation $gamePath) {
                return $gamePath
            }
        }
    }

    return $null
}

function Test-RE8Installation {
    param([string]$path)

    if (-not (Test-Path $path)) {
        return $false
    }

    $exePath = Join-Path $path "re8.exe"
    return (Test-Path $exePath)
}

# Main
$gamePath = Find-RE8Installation

if ($gamePath) {
    Write-Output $gamePath
    exit 0
} else {
    Write-Host ""
    Write-Host "========================================" -ForegroundColor Red
    Write-Host "  ERROR: Resident Evil Village not found" -ForegroundColor Red
    Write-Host "========================================" -ForegroundColor Red
    Write-Host ""
    Write-Host "To fix this:" -ForegroundColor Yellow
    Write-Host "  1. Find your RE Village installation folder" -ForegroundColor White
    Write-Host "  2. Set the environment variable:" -ForegroundColor White
    Write-Host "     `$env:RE8_PATH = 'C:\path\to\Resident Evil Village'" -ForegroundColor Green
    Write-Host "  3. Run deploy again:" -ForegroundColor White
    Write-Host "     pixi run deploy" -ForegroundColor Green
    Write-Host ""
    exit 1
}
