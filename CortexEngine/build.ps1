# Project Cortex - Quick Build Script (PowerShell)
# Use this after initial setup to rebuild

param(
    [string]$Config = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Building Project Cortex ($Config)" -ForegroundColor Cyan

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (-not (Test-Path $vswhere)) {
    Write-Host "[ERROR] vswhere not found; please install Visual Studio with C++ workload." -ForegroundColor Red
    exit 1
}

$vsPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
if (-not $vsPath) { $vsPath = & $vswhere -latest -property installationPath }

if (-not $vsPath) {
    Write-Host "[ERROR] Visual Studio not found; install VS 2022/2026 with C++ workload." -ForegroundColor Red
    exit 1
}

$vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
if (-not (Test-Path $vsDevCmd)) {
    Write-Host "[ERROR] VsDevCmd.bat not found at $vsDevCmd" -ForegroundColor Red
    exit 1
}

Write-Host "Importing Visual Studio build environment..." -ForegroundColor Gray
$tempFile = [System.IO.Path]::GetTempFileName()
cmd /c " `"$vsDevCmd`" -arch=amd64 -host_arch=amd64 > NUL && set > `"$tempFile`" "
Get-Content $tempFile | ForEach-Object {
    if ($_ -match "^(.*?)=(.*)$") {
        $name = $matches[1]; $value = $matches[2]
        if ($name -match "^(PATH|INCLUDE|LIB|LIBPATH|VC|WindowsSDK)") {
            Set-Item -Path "env:$name" -Value $value
        }
    }
}
Remove-Item $tempFile -Force
Write-Host "VS environment imported from $vsPath" -ForegroundColor Gray

$buildDir = Join-Path $PSScriptRoot "build"

if (-not (Test-Path $buildDir)) {
    Write-Host "Build directory not found. Running full setup..." -ForegroundColor Yellow
    & "$PSScriptRoot\setup.ps1" -BuildConfig $Config
    exit $LASTEXITCODE
}

if ($Clean) {
    Write-Host "Cleaning build..." -ForegroundColor Yellow
    Push-Location $buildDir
    & cmake --build . --config $Config --target clean
    Pop-Location
}

Push-Location $buildDir

# Refresh CMake cache with (optional) TensorRT settings so that toggling
# TENSORRT_ROOT between runs correctly enables/disables Dreamer GPU diffusion.
$TensorRTIncludeDir = $null
$TensorRTLibDir = $null
if ($env:TENSORRT_ROOT) {
    $candidateRoot = $env:TENSORRT_ROOT
    $inc = Join-Path $candidateRoot "include"
    $lib = Join-Path $candidateRoot "lib"
    if ((Test-Path $inc) -and (Test-Path $lib)) {
        $TensorRTIncludeDir = $inc
        $TensorRTLibDir = $lib
    }
}

$cmakeConfigureArgs = @("..")
if ($TensorRTIncludeDir -and $TensorRTLibDir) {
    Write-Host "Configuring CMake with TensorRT support (CORTEX_ENABLE_TENSORRT=ON)..." -ForegroundColor Gray
    $cmakeConfigureArgs += "-DCORTEX_ENABLE_TENSORRT=ON"
    $cmakeConfigureArgs += "-DTensorRT_INCLUDE_DIR=$TensorRTIncludeDir"
    $cmakeConfigureArgs += "-DTensorRT_LIB_DIR=$TensorRTLibDir"
} else {
    Write-Host "Configuring CMake without TensorRT (CORTEX_ENABLE_TENSORRT=OFF)..." -ForegroundColor Gray
    $cmakeConfigureArgs += "-DCORTEX_ENABLE_TENSORRT=OFF"
}

& cmake $cmakeConfigureArgs

Write-Host "Compiling..." -ForegroundColor Gray
$startTime = Get-Date

& cmake --build . --config $Config --parallel

if ($LASTEXITCODE -ne 0) {
    Write-Host "`n[ERROR] Build failed!" -ForegroundColor Red
    Pop-Location
    exit 1
}

$buildTime = ((Get-Date) - $startTime).TotalSeconds

Pop-Location

Write-Host "`n[OK] Build complete! ($($buildTime.ToString('F1'))s)" -ForegroundColor Green

# Handle both multi-config (VS) and single-config (Ninja) layouts.
$exePath = Join-Path $PSScriptRoot "build\bin\$Config\CortexEngine.exe"
if (-not (Test-Path $exePath)) {
    $exePath = Join-Path $PSScriptRoot "build\bin\CortexEngine.exe"
}
if (Test-Path $exePath) {
    $exeSize = (Get-Item $exePath).Length / 1MB
    Write-Host "Executable: $($exeSize.ToString('F2')) MB ($exePath)" -ForegroundColor Gray
} else {
    Write-Host "Executable not found in expected locations; build may have used a different output directory." -ForegroundColor Yellow
}

Write-Host "`nRun with: .\run.ps1" -ForegroundColor Cyan
