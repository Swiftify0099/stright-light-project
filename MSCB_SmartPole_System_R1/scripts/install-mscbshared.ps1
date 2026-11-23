<#
.SYNOPSIS
    Installs or updates the MSCBShared library into the Arduino IDE Sketchbook libraries folder.

.DESCRIPTION
    MSCB Smart Pole System R1 relies on the MSCBShared Arduino library for protocol definitions,
    cryptographic security (HMAC-SHA256, session keys), replay protection, network framing,
    and storage types.
    
    This script provides an idempotent installer that:
    1. Locates MSCB_SmartPole_System_R1/MSCBShared.
    2. Validates all required source files and headers exist.
    3. Auto-detects the active Arduino IDE Sketchbook path (or accepts a custom -SketchbookPath).
    4. Safely copies MSCBShared into <Sketchbook>/libraries/MSCBShared without nested duplication.
    5. Verifies all installed files.

.PARAMETER SketchbookPath
    Optional custom path to the Arduino Sketchbook directory. If omitted, the script checks
    ~/.arduinoIDE/arduino-cli.yaml and standard Documents/Arduino locations.

.EXAMPLE
    .\install-mscbshared.ps1

.EXAMPLE
    .\install-mscbshared.ps1 -SketchbookPath "C:\Users\username\OneDrive\Documents\Arduino"
#>

[CmdletBinding()]
param(
    [Parameter(Mandatory = $false, HelpMessage = "Custom path to Arduino Sketchbook directory")]
    [string]$SketchbookPath = ""
)

$ErrorActionPreference = "Stop"

Write-Host "============================================================" -ForegroundColor Cyan
Write-Host "  MSCB Smart Pole R1 - MSCBShared Library Installer" -ForegroundColor Cyan
Write-Host "============================================================" -ForegroundColor Cyan

# 1. Resolve source MSCBShared directory
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $ScriptDir) {
    $ScriptDir = Get-Location
}
$RepoRoot = Split-Path -Parent $ScriptDir
$SourceLibDir = Join-Path $RepoRoot "MSCBShared"

if (-not (Test-Path $SourceLibDir)) {
    # Fallback: check current directory / parent directories
    $Candidate = Join-Path (Get-Location) "MSCB_SmartPole_System_R1\MSCBShared"
    if (Test-Path $Candidate) {
        $SourceLibDir = $Candidate
    } else {
        $Candidate2 = Join-Path (Get-Location) "MSCBShared"
        if (Test-Path $Candidate2) {
            $SourceLibDir = $Candidate2
        } else {
            Write-Error "ERROR: Could not locate MSCBShared source directory. Searched: '$SourceLibDir'"
            exit 1
        }
    }
}

Write-Host "[1/5] Source library located at: $SourceLibDir" -ForegroundColor Green

# 2. Validate required files exist in source library
$RequiredFiles = @(
    "library.properties",
    "BuildConfig.h",
    "CommandSecurity.h",
    "CommandSecurity.cpp",
    "NetworkTypes.h",
    "Protocol.h",
    "ReplayProtection.h",
    "ReplayProtection.cpp",
    "StorageTypes.h"
)

$MissingFiles = @()
foreach ($file in $RequiredFiles) {
    $filePath = Join-Path $SourceLibDir $file
    if (-not (Test-Path $filePath)) {
        $MissingFiles += $file
    }
}

if ($MissingFiles.Count -gt 0) {
    Write-Error "ERROR: Missing required files in $SourceLibDir : $($MissingFiles -join ', ')"
    exit 1
}

Write-Host "[2/5] Source library validation passed ($($RequiredFiles.Count) verified files)." -ForegroundColor Green

# 3. Determine Arduino Sketchbook directory
$ResolvedSketchbook = ""

if ($SketchbookPath -and $SketchbookPath.Trim().Length -gt 0) {
    $ResolvedSketchbook = $SketchbookPath.Trim()
    Write-Host "[3/5] Using explicitly provided Sketchbook path: $ResolvedSketchbook" -ForegroundColor Yellow
} else {
    Write-Host "[3/5] Auto-detecting Arduino IDE Sketchbook path..." -ForegroundColor Gray
    
    # Check 1: ~/.arduinoIDE/arduino-cli.yaml
    $CliYamlPath = Join-Path $env:USERPROFILE ".arduinoIDE\arduino-cli.yaml"
    if (Test-Path $CliYamlPath) {
        try {
            $YamlContent = Get-Content $CliYamlPath -Raw
            if ($YamlContent -match 'user:\s*([^\r\n]+)') {
                $CandidateUser = $matches[1].Trim().Trim('"').Trim("'")
                if ($CandidateUser -and (Test-Path $CandidateUser)) {
                    $ResolvedSketchbook = $CandidateUser
                    Write-Host "      Detected from arduino-cli.yaml: $ResolvedSketchbook" -ForegroundColor Gray
                }
            }
        } catch {
            Write-Verbose "Could not parse arduino-cli.yaml: $_"
        }
    }

    # Check 2: Standard MyDocuments/Arduino
    if (-not $ResolvedSketchbook) {
        $MyDocs = [Environment]::GetFolderPath([Environment+SpecialFolder]::MyDocuments)
        if ($MyDocs) {
            $CandPath = Join-Path $MyDocs "Arduino"
            if (Test-Path $CandPath) {
                $ResolvedSketchbook = $CandPath
            }
        }
    }

    # Check 3: %USERPROFILE%\Documents\Arduino
    if (-not $ResolvedSketchbook) {
        $CandPath = Join-Path $env:USERPROFILE "Documents\Arduino"
        if (Test-Path $CandPath) {
            $ResolvedSketchbook = $CandPath
        }
    }

    # Check 4: OneDrive Documents\Arduino
    if (-not $ResolvedSketchbook -and $env:OneDrive) {
        $CandPath = Join-Path $env:OneDrive "Documents\Arduino"
        if (Test-Path $CandPath) {
            $ResolvedSketchbook = $CandPath
        }
    }

    # Default fallback: MyDocuments/Arduino (will create if missing)
    if (-not $ResolvedSketchbook) {
        $MyDocs = [Environment]::GetFolderPath([Environment+SpecialFolder]::MyDocuments)
        $ResolvedSketchbook = Join-Path $MyDocs "Arduino"
    }

    Write-Host "[3/5] Resolved Arduino Sketchbook path: $ResolvedSketchbook" -ForegroundColor Green
}

# 4. Ensure destination directories exist
$LibrariesDir = Join-Path $ResolvedSketchbook "libraries"
if (-not (Test-Path $LibrariesDir)) {
    Write-Host "      Creating libraries directory: $LibrariesDir" -ForegroundColor Gray
    New-Item -ItemType Directory -Path $LibrariesDir -Force | Out-Null
}

$DestLibDir = Join-Path $LibrariesDir "MSCBShared"
if (-not (Test-Path $DestLibDir)) {
    Write-Host "      Creating target library directory: $DestLibDir" -ForegroundColor Gray
    New-Item -ItemType Directory -Path $DestLibDir -Force | Out-Null
}

Write-Host "[4/5] Target destination directory: $DestLibDir" -ForegroundColor Green

# 5. Copy files idempotently without nested folder recursion
Write-Host "[5/5] Installing MSCBShared library files..." -ForegroundColor Gray

$CopyCount = 0
foreach ($file in $RequiredFiles) {
    $src = Join-Path $SourceLibDir $file
    $dst = Join-Path $DestLibDir $file
    
    Copy-Item -Path $src -Destination $dst -Force
    if (Test-Path $dst) {
        $CopyCount++
        Write-Verbose "Installed $file -> $dst"
    } else {
        Write-Error "ERROR: Failed to copy $file to $dst"
        exit 1
    }
}

# Remove any accidental nested directory if previous manual copies created it
$NestedBadDir = Join-Path $DestLibDir "MSCBShared"
if (Test-Path $NestedBadDir) {
    Write-Warning "Removing invalid nested MSCBShared directory: $NestedBadDir"
    Remove-Item -Path $NestedBadDir -Recurse -Force
}

Write-Host ""
Write-Host "============================================================" -ForegroundColor Green
Write-Host "  SUCCESS: MSCBShared Library Installed Successfully!" -ForegroundColor Green
Write-Host "============================================================" -ForegroundColor Green
Write-Host "  Destination: $DestLibDir" -ForegroundColor Cyan
Write-Host "  Files Installed ($CopyCount/$($RequiredFiles.Count)):" -ForegroundColor White
foreach ($file in $RequiredFiles) {
    Write-Host "    - $file" -ForegroundColor Gray
}
Write-Host ""
Write-Host "Next Steps in Arduino IDE:" -ForegroundColor Yellow
Write-Host "  1. If Arduino IDE 2.x is currently open, restart it or press Ctrl+Shift+P -> 'Reload Window'" -ForegroundColor Yellow
Write-Host "  2. Open MSCB_SmartPole_System_R1/Gateway_ESP32S3/Gateway_ESP32S3.ino" -ForegroundColor Yellow
Write-Host "  3. Select Board: 'ESP32S3 Dev Module', Flash: '16MB', PSRAM: 'OPI PSRAM'" -ForegroundColor Yellow
Write-Host "  4. Press Verify (Ctrl+R) to compile Gateway" -ForegroundColor Yellow
Write-Host "  5. Open MSCB_SmartPole_System_R1/FieldPole_ESP32/FieldPole_ESP32.ino" -ForegroundColor Yellow
Write-Host "  6. Select Board: 'ESP32 Dev Module', Flash: '4MB', PSRAM: 'Disabled'" -ForegroundColor Yellow
Write-Host "  7. Press Verify (Ctrl+R) to compile Field Pole" -ForegroundColor Yellow
Write-Host "============================================================" -ForegroundColor Green

exit 0
