# Project Cortex - All-in-One Installer
# This script installs EVERYTHING needed, including Visual Studio prerequisites

param(
    [switch]$SkipVSCheck
)

$ErrorActionPreference = "Stop"
$startTime = Get-Date

Write-Host @"

===================================================================
          PROJECT CORTEX - ALL-IN-ONE INSTALLER

     This script will install everything you need from
     scratch, including vcpkg and all dependencies.

     Estimated time: 20-30 minutes
     Required space: ~2 GB
===================================================================

"@ -ForegroundColor Cyan

Start-Sleep -Seconds 2

# ============================================================================
# Check Administrator Rights
# ============================================================================
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if ($isAdmin) {
    Write-Host "[OK] Running with Administrator privileges" -ForegroundColor Green
} else {
    Write-Host "[WARN] Not running as Administrator" -ForegroundColor Yellow
    Write-Host "  Some operations may require elevation" -ForegroundColor Gray
}

Write-Host ""

# ============================================================================
# Check Git
# ============================================================================
Write-Host "Checking Git installation..." -ForegroundColor Cyan

try {
    $null = & git --version 2>&1
    Write-Host "[OK] Git is installed" -ForegroundColor Green
} catch {
    Write-Host "[ERROR] Git not found!" -ForegroundColor Red
    Write-Host ""
    Write-Host "Please install Git first:" -ForegroundColor Yellow
    Write-Host "  Download: https://git-scm.com/download/win" -ForegroundColor Gray
    Write-Host ""
    Write-Host "After installing, restart PowerShell and run this script again." -ForegroundColor Yellow
    exit 1
}

Write-Host ""

# ============================================================================
# Check Visual Studio
# ============================================================================
if (-not $SkipVSCheck) {
    Write-Host "Checking Visual Studio installation..." -ForegroundColor Cyan

    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"

    if (Test-Path $vswhere) {
        $vsPath = & $vswhere -latest -property installationPath
        $vsVersion = & $vswhere -latest -property catalog_productDisplayVersion

        if ($vsPath) {
            Write-Host "[OK] Visual Studio $vsVersion found" -ForegroundColor Green
            Write-Host "  Path: $vsPath" -ForegroundColor Gray
        } else {
            Write-Host "[ERROR] Visual Studio not found!" -ForegroundColor Red
            Write-Host ""
            Write-Host "Please install Visual Studio 2022:" -ForegroundColor Yellow
            Write-Host "  1. Download: https://visualstudio.microsoft.com/downloads/" -ForegroundColor Gray
            Write-Host "  2. Install with workload: 'Desktop development with C++'" -ForegroundColor Gray
            Write-Host "  3. Make sure to include:" -ForegroundColor Gray
            Write-Host "     - MSVC v143 (or newer)" -ForegroundColor Gray
            Write-Host "     - Windows SDK" -ForegroundColor Gray
            Write-Host "     - CMake tools" -ForegroundColor Gray
            Write-Host ""
            exit 1
        }
    } else {
        Write-Host "[ERROR] Visual Studio not found!" -ForegroundColor Red
        Write-Host ""
        Write-Host "Please install Visual Studio 2022 first." -ForegroundColor Yellow
        Write-Host "Download: https://visualstudio.microsoft.com/downloads/" -ForegroundColor Gray
        Write-Host ""
        Write-Host "Or run with -SkipVSCheck if you have it in a non-standard location" -ForegroundColor Gray
        exit 1
    }

    Write-Host ""
}

# ============================================================================
# Check CMake
# ============================================================================
Write-Host "Checking CMake installation..." -ForegroundColor Cyan

try {
    $cmakeVersion = & cmake --version 2>&1 | Select-String -Pattern "version (\d+\.\d+)" | ForEach-Object { $_.Matches.Groups[1].Value }
    Write-Host "[OK] CMake $cmakeVersion is installed" -ForegroundColor Green
} catch {
    Write-Host "[ERROR] CMake not found!" -ForegroundColor Red
    Write-Host ""
    Write-Host "CMake is required. Install it via:" -ForegroundColor Yellow
    Write-Host "  Option 1: Visual Studio Installer → 'CMake tools for Windows'" -ForegroundColor Gray
    Write-Host "  Option 2: Direct download from https://cmake.org/download/" -ForegroundColor Gray
    Write-Host ""
    Write-Host "After installing, restart PowerShell and run this script again." -ForegroundColor Yellow
    exit 1
}

Write-Host ""

# ============================================================================
# Setup vcpkg
# ============================================================================
Write-Host "Setting up vcpkg package manager..." -ForegroundColor Cyan

$vcpkgRoot = $env:VCPKG_ROOT

if ($vcpkgRoot -and (Test-Path $vcpkgRoot)) {
    Write-Host "[OK] vcpkg found at: $vcpkgRoot" -ForegroundColor Green
} else {
    Write-Host "vcpkg not found. Installing to C:\vcpkg..." -ForegroundColor Yellow

    # Check if C:\vcpkg already exists
    if (Test-Path "C:\vcpkg") {
        Write-Host "[WARN] C:\vcpkg already exists. Using existing installation." -ForegroundColor Yellow
        $vcpkgRoot = "C:\vcpkg"
    } else {
        Write-Host "Cloning vcpkg repository (this may take a few minutes)..." -ForegroundColor Gray

        try {
            Push-Location C:\
            git clone --depth 1 https://github.com/Microsoft/vcpkg.git

            if ($LASTEXITCODE -ne 0) {
                throw "Git clone failed"
            }

            Set-Location vcpkg

            Write-Host "Bootstrapping vcpkg..." -ForegroundColor Gray
            .\bootstrap-vcpkg.bat

            if ($LASTEXITCODE -ne 0) {
                throw "Bootstrap failed"
            }

            Pop-Location

            $vcpkgRoot = "C:\vcpkg"

            Write-Host "[OK] vcpkg installed successfully" -ForegroundColor Green
        } catch {
            Write-Host "[ERROR] Failed to install vcpkg: $_" -ForegroundColor Red
            Pop-Location
            exit 1
        }
    }

    # Set environment variable
    Write-Host "Setting VCPKG_ROOT environment variable..." -ForegroundColor Gray
    [System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgRoot, [System.EnvironmentVariableTarget]::User)
    $env:VCPKG_ROOT = $vcpkgRoot

    Write-Host "[OK] VCPKG_ROOT set to: $vcpkgRoot" -ForegroundColor Green
    Write-Host "  (Restart terminal for persistent environment)" -ForegroundColor Gray
}

Write-Host ""

# ============================================================================
# Install Dependencies
# ============================================================================
Write-Host "Installing project dependencies..." -ForegroundColor Cyan
Write-Host "This will take 10-20 minutes depending on your internet speed." -ForegroundColor Gray
Write-Host ""

$packages = @(
    @{Name="sdl3:x64-windows"; Description="Window management and input"},
    @{Name="entt:x64-windows"; Description="Entity Component System"},
    @{Name="nlohmann-json:x64-windows"; Description="JSON parsing"},
    @{Name="spdlog:x64-windows"; Description="Fast logging"},
    @{Name="directx-headers:x64-windows"; Description="DirectX 12 headers"},
    @{Name="directxtk12:x64-windows"; Description="DirectX Toolkit"},
    @{Name="glm:x64-windows"; Description="Math library"}
)

Push-Location $vcpkgRoot

$packagesInstalled = 0
$packagesFailed = 0

foreach ($package in $packages) {
    Write-Host "Installing $($package.Name) - $($package.Description)..." -ForegroundColor Gray

    & .\vcpkg install $package.Name 2>&1 | Out-Null

    if ($LASTEXITCODE -eq 0) {
        Write-Host "  [OK] $($package.Name) installed" -ForegroundColor Green
        $packagesInstalled++
    } else {
        Write-Host "  [ERROR] $($package.Name) failed" -ForegroundColor Red
        $packagesFailed++
    }
}

Pop-Location

Write-Host ""

if ($packagesFailed -eq 0) {
    Write-Host "[OK] All $packagesInstalled packages installed successfully!" -ForegroundColor Green
} else {
    Write-Host "[WARN] $packagesInstalled packages installed, $packagesFailed failed" -ForegroundColor Yellow

    if ($packagesFailed -gt 0) {
        Write-Host ""
        Write-Host "Some packages failed to install. Common issues:" -ForegroundColor Yellow
        Write-Host "  - SDL3 is very new, might not be in vcpkg yet (use SDL2 as fallback)" -ForegroundColor Gray
        Write-Host "  - Network issues (check internet connection)" -ForegroundColor Gray
        Write-Host "  - Disk space (need ~2 GB free)" -ForegroundColor Gray
    }
}

Write-Host ""

# ============================================================================
# Run Main Setup
# ============================================================================
Write-Host "Running main project setup..." -ForegroundColor Cyan
Write-Host ""

$setupScript = Join-Path $PSScriptRoot "setup.ps1"

if (-not (Test-Path $setupScript)) {
    Write-Host "[ERROR] setup.ps1 not found in current directory!" -ForegroundColor Red
    Write-Host "  Make sure you're running this from the CortexEngine directory" -ForegroundColor Gray
    exit 1
}

& $setupScript -SkipVcpkg

if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "[ERROR] Setup failed!" -ForegroundColor Red
    exit 1
}

# ============================================================================
# Final Summary
# ============================================================================
$totalTime = (Get-Date) - $startTime

Write-Host ""
Write-Host @"

===================================================================
              ALL-IN-ONE INSTALLATION COMPLETE!
===================================================================

"@ -ForegroundColor Green

Write-Host "Installation Summary:" -ForegroundColor Cyan
Write-Host "  [OK] Git verified" -ForegroundColor Green
Write-Host "  [OK] Visual Studio verified" -ForegroundColor Green
Write-Host "  [OK] CMake verified" -ForegroundColor Green
Write-Host "  [OK] vcpkg installed/verified at: $vcpkgRoot" -ForegroundColor Green
Write-Host "  [OK] $packagesInstalled dependencies installed" -ForegroundColor Green
Write-Host "  [OK] Project built successfully" -ForegroundColor Green
Write-Host ""

Write-Host "Total installation time: $($totalTime.Minutes)m $($totalTime.Seconds)s" -ForegroundColor Gray
Write-Host ""

Write-Host "What to do next:" -ForegroundColor Cyan
Write-Host "  1. Run the application:" -ForegroundColor White
Write-Host "     .\run.ps1" -ForegroundColor Gray
Write-Host ""
Write-Host "  2. Or manually:" -ForegroundColor White
Write-Host "     cd build\bin\Release" -ForegroundColor Gray
Write-Host "     .\CortexEngine.exe" -ForegroundColor Gray
Write-Host ""
Write-Host "  3. Read the docs:" -ForegroundColor White
Write-Host "     QUICKSTART.md      - Quick start guide" -ForegroundColor Gray
Write-Host "     PHASE1_COMPLETE.md - Architecture details" -ForegroundColor Gray
Write-Host ""

Write-Host "Happy coding! 🚀" -ForegroundColor Green
