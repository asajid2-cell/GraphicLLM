$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

Push-Location -Path (Split-Path -Parent $MyInvocation.MyCommand.Path)
try {
    $exeCandidates = @(
        (Join-Path $PWD "build\\bin\\Release\\CortexEngine.exe"),
        (Join-Path $PWD "build\\bin\\CortexEngine.exe"),
        (Join-Path $PWD "build\\CortexEngine.exe")
    )
    $exePath = $exeCandidates | Where-Object { Test-Path $_ } | Select-Object -First 1
    if (-not $exePath) {
        Write-Host "Executable not found in expected locations." -ForegroundColor Yellow
        $exeCandidates | ForEach-Object { Write-Host "  $_" -ForegroundColor Gray }
        Write-Host "Run full-build.ps1 first to compile the project."
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

    $repoRoot = (Split-Path $PWD -Parent)
    $exeDir = Split-Path $exePath -Parent
    $assetSource = Join-Path $repoRoot "assets"
    $modelSource = Join-Path $repoRoot "models"

    if (Test-Path $assetSource) {
        Write-Host "Syncing assets to $exeDir"
        Copy-Item -Path $assetSource -Destination $exeDir -Recurse -Force
    } else {
        Write-Host "Warning: assets folder not found at $assetSource" -ForegroundColor Yellow
    }

    if (Test-Path $modelSource) {
        Write-Host "Syncing models to $exeDir"
        Copy-Item -Path $modelSource -Destination $exeDir -Recurse -Force
    } else {
        Write-Host "Warning: models folder not found at $modelSource" -ForegroundColor Yellow
    }

    if ($cudaBins) {
        foreach ($binPath in $cudaBins) {
            Write-Host "Copying CUDA DLLs from $binPath"
            Get-ChildItem -Path $binPath -Filter "*.dll" -File | ForEach-Object {
                Copy-Item -Path $_.FullName -Destination $exeDir -Force
            }
        }
    }

    Write-Host "Launching CortexEngine..."
    Push-Location $exeDir
    $exeName = Split-Path $exePath -Leaf
    & ".\${exeName}"
    Pop-Location
}
finally {
    Pop-Location
}
