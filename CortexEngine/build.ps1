# Project Cortex - Quick Build Script (PowerShell)
# Use this after initial setup to rebuild

param(
    [string]$Config = "Release",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

Write-Host "`n==> Building Project Cortex ($Config)" -ForegroundColor Cyan

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

$exePath = Join-Path $PSScriptRoot "build\bin\$Config\CortexEngine.exe"
$exeSize = (Get-Item $exePath).Length / 1MB
Write-Host "Executable: $($exeSize.ToString('F2')) MB" -ForegroundColor Gray

Write-Host "`nRun with: .\run.ps1" -ForegroundColor Cyan
