# Project Cortex - Quick Run Script (PowerShell)

param(
    [string]$Config = "Release",
    [switch]$ForceSync,
    [switch]$NoLLM
)

$exeCandidates = @(
    (Join-Path $PSScriptRoot "build\bin\$Config\CortexEngine.exe"),
    (Join-Path $PSScriptRoot "build\bin\CortexEngine.exe"),
    (Join-Path $PSScriptRoot "build\CortexEngine.exe")
)
$exePath = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1

if (-not $exePath) {
    Write-Host "Executable not found in expected locations." -ForegroundColor Red
    Write-Host "Checked:" -ForegroundColor Gray
    $exeCandidates | ForEach-Object { Write-Host "  $_" -ForegroundColor Gray }
    Write-Host ""
    Write-Host "Did you build the project? Run one of:" -ForegroundColor Yellow
    Write-Host "  .\setup.ps1          (Full setup + build)" -ForegroundColor Gray
    Write-Host "  .\build.ps1          (Just build)" -ForegroundColor Gray
    exit 1
}

function Add-CudaBin {
    param([string]$root)
    $bins = @()
    if (Test-Path (Join-Path $root "bin\x64")) { $bins += (Join-Path $root "bin\x64") }
    if (Test-Path (Join-Path $root "bin")) { $bins += (Join-Path $root "bin") }
    foreach ($b in $bins) {
        if ($env:PATH.Split(';') -notcontains $b) {
            $env:PATH = "$b;$env:PATH"
            Write-Host "Adding CUDA to PATH: $b" -ForegroundColor Gray
        }
    }
    return $bins
}

$trtBins = @()
if ($env:TENSORRT_ROOT) {
    $trtBin = Join-Path $env:TENSORRT_ROOT "bin"
    if (Test-Path $trtBin) {
        if ($env:PATH.Split(';') -notcontains $trtBin) {
            $env:PATH = "$trtBin;$env:PATH"
            Write-Host "Adding TensorRT to PATH: $trtBin" -ForegroundColor Gray
        }
        $trtBins += $trtBin
    }
}
if (-not $trtBins) {
    # Common default; adjust if your TensorRT is installed elsewhere.
    $defaultTrt = "C:\TensorRT"
    $defaultTrtBin = Join-Path $defaultTrt "bin"
    if (Test-Path $defaultTrtBin) {
        if ($env:PATH.Split(';') -notcontains $defaultTrtBin) {
            $env:PATH = "$defaultTrtBin;$env:PATH"
            Write-Host "Adding TensorRT to PATH: $defaultTrtBin" -ForegroundColor Gray
        }
        $trtBins += $defaultTrtBin
    }
    if (-not $trtBins) {
        Write-Host "Warning: TensorRT bin not found; Dreamer GPU diffusion may fail to load." -ForegroundColor Yellow
    }
}

$cudaBins = @()
if ($env:CUDAToolkit_ROOT) {
    $cudaBins += Add-CudaBin -root $env:CUDAToolkit_ROOT
}
if (-not $cudaBins) {
    $defaultCuda = "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0"
    if (Test-Path $defaultCuda) { $cudaBins += Add-CudaBin -root $defaultCuda }
}
if (-not $cudaBins) {
    Write-Host "Warning: CUDA bin not found; LLM CUDA runtime may fail to load." -ForegroundColor Yellow
}
$nvcuda = Join-Path $env:WINDIR "System32\nvcuda.dll"
if (-not (Test-Path $nvcuda)) {
    Write-Host "Warning: NVIDIA driver runtime (nvcuda.dll) not found in System32; install/update the GPU driver." -ForegroundColor Yellow
}

Write-Host "Starting Project Cortex..." -ForegroundColor Cyan
Write-Host "Executable: $exePath" -ForegroundColor Gray
Write-Host ""

$repoRoot = $PSScriptRoot
$exeDir = Split-Path $exePath -Parent
$assetSource = Join-Path $repoRoot "assets"
$modelSource = Join-Path $repoRoot "models"
$assetDest = Join-Path $exeDir "assets"
$modelDest = Join-Path $exeDir "models"
$forceSync = $ForceSync.IsPresent -or ($env:CORTEX_FORCE_SYNC -and $env:CORTEX_FORCE_SYNC -ne "0")

if (Test-Path $assetSource) {
    if ($forceSync -or -not (Test-Path $assetDest)) {
        Write-Host "Syncing assets to $exeDir" -ForegroundColor Gray
        Copy-Item -Path $assetSource -Destination $exeDir -Recurse -Force
    } else {
        Write-Host "Assets already present in $exeDir; skipping asset sync" -ForegroundColor Gray
    }
} else {
    Write-Host "Warning: assets folder not found at $assetSource" -ForegroundColor Yellow
}

if (Test-Path $modelSource) {
    if ($forceSync -or -not (Test-Path $modelDest)) {
        Write-Host "Syncing models to $exeDir" -ForegroundColor Gray
        Copy-Item -Path $modelSource -Destination $exeDir -Recurse -Force
    } else {
        Write-Host "Models already present in $exeDir; skipping model sync" -ForegroundColor Gray
    }
} else {
    Write-Host "Warning: models folder not found at $modelSource" -ForegroundColor Yellow
}

if ($cudaBins) {
    foreach ($binPath in $cudaBins) {
        Write-Host "Copying CUDA DLLs from $binPath" -ForegroundColor Gray
        Get-ChildItem -Path $binPath -Filter "*.dll" -File | ForEach-Object {
            Copy-Item -Path $_.FullName -Destination $exeDir -Force
        }
    }
}
if ($trtBins) {
    foreach ($binPath in $trtBins) {
        Write-Host "Copying TensorRT DLLs from $binPath" -ForegroundColor Gray
        Get-ChildItem -Path $binPath -Filter "*.dll" -File | ForEach-Object {
            Copy-Item -Path $_.FullName -Destination $exeDir -Force
        }
    }
}

Push-Location (Split-Path $exePath -Parent)
$exeName = Split-Path $exePath -Leaf
$argsList = @()
if ($NoLLM.IsPresent) {
    $argsList += "--no-llm"
}
& ".\${exeName}" @argsList
Pop-Location
