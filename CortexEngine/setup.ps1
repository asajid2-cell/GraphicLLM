# Project Cortex - Automated Setup Script (PowerShell)
# This script automates the entire build environment setup

param(
    [switch]$SkipVcpkg,
    [switch]$SkipBuild,
    [string]$BuildConfig = "Release"
)

$ErrorActionPreference = "Stop"

# Color output functions
function Write-Step { Write-Host "`n==> $args" -ForegroundColor Cyan }
function Write-Success { Write-Host "[OK] $args" -ForegroundColor Green }
function Write-Error { Write-Host "[ERROR] $args" -ForegroundColor Red }
function Write-Info { Write-Host "  $args" -ForegroundColor Gray }

$startTime = Get-Date

Write-Host @"
===============================================================
            PROJECT CORTEX - SETUP SCRIPT
         Neural-Native Rendering Engine v0.1.0

              Phase 2: The Architect (LLM Edition)
===============================================================
"@ -ForegroundColor Magenta

# Check if running as Administrator
$isAdmin = ([Security.Principal.WindowsPrincipal] [Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $isAdmin) {
    Write-Info "Note: Not running as Administrator. Some operations may require elevation."
}

# ============================================================================
# STEP 1: Check Prerequisites
# ============================================================================
Write-Step "Checking prerequisites..."

# Check for CMake
try {
    $cmakeVersion = & cmake --version 2>&1 | Select-String -Pattern "version (\d+\.\d+)" | ForEach-Object { $_.Matches.Groups[1].Value }
    Write-Success "CMake found: version $cmakeVersion"

    $cmakeMajor = [int]($cmakeVersion -split '\.')[0]
    $cmakeMinor = [int]($cmakeVersion -split '\.')[1]

    if ($cmakeMajor -lt 3 -or ($cmakeMajor -eq 3 -and $cmakeMinor -lt 20)) {
        Write-Error "CMake 3.20+ required, found $cmakeVersion"
        Write-Info "Download from: https://cmake.org/download/"
        exit 1
    }
} catch {
    Write-Error "CMake not found!"
    Write-Info "Download from: https://cmake.org/download/"
    Write-Info "Or install via Visual Studio Installer (CMake tools for Windows)"
    exit 1
}

# Check for Git
try {
    $gitVersion = & git --version 2>&1
    Write-Success "Git found: $gitVersion"
} catch {
    Write-Error "Git not found!"
    Write-Info "Download from: https://git-scm.com/download/win"
    exit 1
}

# Check for Visual Studio / MSBuild and import environment (vswhere + VsDevCmd)
try {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        Write-Error "Visual Studio not found! Install the Desktop development with C++ workload."
        exit 1
    }

    $vsPath = & $vswhere -latest -requires Microsoft.Component.MSBuild -property installationPath
    if (-not $vsPath) {
        $vsPath = & $vswhere -latest -property installationPath
    }
    $vsDisplay = & $vswhere -latest -property catalog_productDisplayVersion
    Write-Success "Visual Studio found: $vsDisplay"
    Write-Info "Path: $vsPath"

    $vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
    if (-not (Test-Path $vsDevCmd)) {
        Write-Error "VsDevCmd.bat not found at $vsDevCmd"
        exit 1
    }
    Write-Info "Importing VS environment..."
    $envOutput = & cmd /c "call `"$vsDevCmd`" -arch=amd64 -host_arch=amd64 >nul && set"
    foreach ($line in $envOutput) {
        if ($line -match "^(.*?)=(.*)$") {
            $name = $matches[1]
            $value = $matches[2]
            Set-Item -Path "env:$name" -Value $value
        }
    }

} catch {
    Write-Error "Could not detect Visual Studio!"
    exit 1
}

# Check for CUDA toolkit (for GPU LLM acceleration) and install if missing
function Find-CudaPath {
    $paths = @()
    if ($env:CUDAToolkit_ROOT) { $paths += $env:CUDAToolkit_ROOT }
    $defaultRoot = Join-Path ${env:ProgramFiles} "NVIDIA GPU Computing Toolkit\CUDA"
    if (Test-Path $defaultRoot) {
        $paths += (Get-ChildItem $defaultRoot -Directory | Sort-Object Name -Descending | ForEach-Object { $_.FullName })
    }
    foreach ($p in $paths) {
        $nvcc = Join-Path $p "bin\nvcc.exe"
        if (Test-Path $nvcc) {
            return $p
        }
    }
    return $null
}

function Set-CudaEnv($cudaPath) {
    if (-not $cudaPath) { return }
    $env:CUDAToolkit_ROOT = $cudaPath
    $env:CUDA_PATH = $cudaPath
    $env:CudaToolkitDir = $cudaPath
    # Set versioned CUDA_PATH if we can derive it
    $dirName = Split-Path $cudaPath -Leaf
    if ($dirName -match "^v?(?<ver>\d+\.\d+)") {
        $ver = $matches['ver']
        $envName = "CUDA_PATH_V$($ver -replace '\.','_')"
        Set-Item -Path "env:$envName" -Value $cudaPath
    }
    $nvBin = Join-Path $cudaPath "bin"
    if ($env:PATH.Split(';') -notcontains $nvBin) {
        $env:PATH = "$nvBin;$env:PATH"
    }
    $script:GlobalCudaBin = $nvBin
}

function Test-CudaInstalled {
    # Try PATH nvcc first
    if (Get-Command nvcc -ErrorAction SilentlyContinue) {
        $nvccVersion = & nvcc --version 2>&1 | Select-String "release" | Select-Object -First 1
        Write-Success "CUDA toolkit detected ($nvccVersion) - will enable GPU acceleration"
        return $true
    }
    $cudaPath = Find-CudaPath
    if ($cudaPath) {
        Set-CudaEnv $cudaPath
        $nvccVersion = & (Join-Path $cudaPath "bin\nvcc.exe") --version 2>&1 | Select-String "release" | Select-Object -First 1
        Write-Success "CUDA toolkit detected at $cudaPath ($nvccVersion) - will enable GPU acceleration"
        return $true
    }
    return $false
}

function Install-CudaIfMissing {
    if (Test-CudaInstalled) { return }

    Write-Info "CUDA toolkit not found; attempting installation for GPU acceleration."

    $installed = $false

    # Prefer winget if available
    if (Get-Command winget -ErrorAction SilentlyContinue) {
        Write-Info "Trying winget install (NVIDIA CUDA)..."
        try {
            & winget install -e --id NVIDIA.CUDA -h
            if ($LASTEXITCODE -eq 0 -and (Test-CudaInstalled)) {
                $installed = $true
            }
        } catch { Write-Info "winget install failed: $_" }
    }

    # Fallback to Chocolatey if available
    if (-not $installed -and (Get-Command choco -ErrorAction SilentlyContinue)) {
        Write-Info "Trying Chocolatey install (cuda)..."
        try {
            & choco install cuda -y
            if ($LASTEXITCODE -eq 0 -and (Test-CudaInstalled)) {
                $installed = $true
            }
        } catch { Write-Info "choco install failed: $_" }
    }

    if (-not $installed) {
        Write-Error "CUDA toolkit not installed. Install from https://developer.nvidia.com/cuda-downloads then re-run setup.ps1"
        exit 1
    }
}

Install-CudaIfMissing
if (-not (Test-CudaInstalled)) {
    Write-Error "CUDA toolkit not detected after install attempt. Please install manually and re-run setup."
    exit 1
}
$cudaFound = $true
$cudaPath = Find-CudaPath
if ($cudaPath) { Set-CudaEnv $cudaPath }

# ============================================================================
# STEP 2: Initialize Git Submodules (llama.cpp)
# ============================================================================
Write-Step "Initializing git submodules..."

$projectRoot = $PSScriptRoot

Push-Location $projectRoot

Write-Info "Checking for llama.cpp submodule..."

if (Test-Path "vendor\llama.cpp\.git") {
    Write-Success "llama.cpp submodule already initialized"
} else {
    Write-Info "Initializing and updating llama.cpp submodule..."
    & git submodule update --init --recursive vendor/llama.cpp

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Failed to initialize git submodules!"
        Pop-Location
        exit 1
    }

    Write-Success "llama.cpp submodule initialized"
}

Pop-Location

# ============================================================================
# STEP 3: Download LLM Model
# ============================================================================
Write-Step "Downloading LLM model for The Architect..."

$modelDir = Join-Path $projectRoot "models"
$modelFile = Join-Path $modelDir "Llama-3.2-3B-Instruct-Q4_K_M.gguf"

if (-not (Test-Path $modelDir)) {
    Write-Info "Creating models directory..."
    New-Item -ItemType Directory -Force -Path $modelDir | Out-Null
}

if (Test-Path $modelFile) {
    Write-Success "Model already exists"
    $modelSize = (Get-Item $modelFile).Length / 1MB
    Write-Info "Size: $($modelSize.ToString('F2')) MB"
} else {
    Write-Info "Downloading Llama 3.2 3B Instruct (Q4_K_M quantization, ~2.0 GB)"
    Write-Info "This model provides reliable JSON generation and scene understanding"
    Write-Info "Balances quality and VRAM usage - leaves room for renderer + diffusion"
    Write-Info ""
    Write-Info "Download URL: https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF"
    Write-Info "This may take 10-20 minutes depending on your connection..."
    Write-Info ""

    $url = "https://huggingface.co/bartowski/Llama-3.2-3B-Instruct-GGUF/resolve/main/Llama-3.2-3B-Instruct-Q4_K_M.gguf"

    try {
        # Use System.Net.WebClient for better progress reporting
        $webClient = New-Object System.Net.WebClient

        # Register progress event
        $onProgressChanged = Register-ObjectEvent -InputObject $webClient -EventName DownloadProgressChanged -Action {
            $percent = $EventArgs.ProgressPercentage
            $downloaded = $EventArgs.BytesReceived / 1MB
            $total = $EventArgs.TotalBytesToReceive / 1MB

            Write-Progress -Activity "Downloading Llama 3.2 3B model" `
                -Status "$($downloaded.ToString('F1')) MB / $($total.ToString('F1')) MB" `
                -PercentComplete $percent
        }

        # Download file
        $webClient.DownloadFile($url, $modelFile)

        # Cleanup
        Unregister-Event -SourceIdentifier $onProgressChanged.Name
        $webClient.Dispose()

        Write-Progress -Activity "Downloading Llama 3.2 3B model" -Completed

        Write-Success "Model downloaded successfully!"
        $modelSize = (Get-Item $modelFile).Length / 1MB
        Write-Info "Size: $($modelSize.ToString('F2')) MB"
    }
    catch {
        Write-Error "Failed to download model: $_"
        Write-Info "You can manually download from:"
        Write-Info "  $url"
        Write-Info "And place it in: $modelDir"
        Write-Info ""
        Write-Info "The engine will run in mock mode without a model."
    }
}

# ============================================================================
# STEP 4: Setup vcpkg
# ============================================================================
if (-not $SkipVcpkg) {
    Write-Step "Setting up vcpkg..."

    $vcpkgRoot = $env:VCPKG_ROOT

    if (-not $vcpkgRoot) {
        Write-Info "VCPKG_ROOT not set. Looking for vcpkg in common locations..."

        $possiblePaths = @(
            "C:\vcpkg",
            "C:\src\vcpkg",
            "C:\dev\vcpkg",
            "$HOME\vcpkg",
            "$HOME\source\vcpkg"
        )

        foreach ($path in $possiblePaths) {
            if (Test-Path "$path\vcpkg.exe") {
                $vcpkgRoot = $path
                Write-Success "Found vcpkg at: $vcpkgRoot"
                break
            }
        }

        if (-not $vcpkgRoot) {
            Write-Info "vcpkg not found. Installing to C:\vcpkg..."

            Push-Location C:\
            git clone https://github.com/Microsoft/vcpkg.git
            Set-Location vcpkg
            .\bootstrap-vcpkg.bat
            Pop-Location

            $vcpkgRoot = "C:\vcpkg"

            # Set environment variable
            [System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", $vcpkgRoot, [System.EnvironmentVariableTarget]::User)
            $env:VCPKG_ROOT = $vcpkgRoot

            Write-Success "vcpkg installed to: $vcpkgRoot"
            Write-Info "VCPKG_ROOT environment variable set (restart terminal for persistence)"
        }
    } else {
        Write-Success "vcpkg found at: $vcpkgRoot"
    }

    # ============================================================================
    # STEP 3: Install Dependencies
    # ============================================================================
    Write-Step "Installing dependencies via vcpkg..."
    Write-Info "This may take 10-20 minutes on first run..."

    $packages = @(
        "sdl3:x64-windows",
        "entt:x64-windows",
        "nlohmann-json:x64-windows",
        "spdlog:x64-windows",
        "directx-headers:x64-windows",
        "directxtk12:x64-windows",
        "glm:x64-windows"
    )

    Push-Location $vcpkgRoot

    foreach ($package in $packages) {
        Write-Info "Installing $package..."
        & .\vcpkg install $package

        if ($LASTEXITCODE -ne 0) {
            Write-Error "Failed to install $package"
            Pop-Location
            exit 1
        }
    }

    Pop-Location
    Write-Success "All dependencies installed!"

} else {
    Write-Info "Skipping vcpkg setup (--SkipVcpkg flag set)"
    $vcpkgRoot = $env:VCPKG_ROOT

    if (-not $vcpkgRoot) {
        Write-Error "VCPKG_ROOT not set and --SkipVcpkg specified!"
        exit 1
    }
}

# ============================================================================
# STEP 5: Configure CMake
# ============================================================================
Write-Step "Configuring CMake build..."

$projectRoot = $PSScriptRoot
$buildDir = Join-Path $projectRoot "build"
$toolchainFile = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"

Write-Info "Project root: $projectRoot"
Write-Info "Build directory: $buildDir"
Write-Info "Toolchain file: $toolchainFile"

# Clean build directory if it exists
if (Test-Path $buildDir) {
    Write-Info "Cleaning existing build directory..."
    Remove-Item -Recurse -Force $buildDir
}

New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

Push-Location $buildDir

Write-Info "Running CMake configure..."
# Let CMake auto-detect the generator (works with any VS version including previews)
$cmakeCmd = @(
    "..",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
    "-DGGML_CUDA=ON",
    "-DCUDAToolkit_ROOT=$env:CUDAToolkit_ROOT",
    "-A", "x64"
)
if ($generator) {
    $cmakeCmd += "-G"
    $cmakeCmd += $generator
}
& cmake @cmakeCmd

if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed!"
    Pop-Location
    exit 1
}

Pop-Location
Write-Success "CMake configuration complete!"

# ============================================================================
# STEP 6: Build Project
# ============================================================================
if (-not $SkipBuild) {
    Write-Step "Building project ($BuildConfig configuration)..."

    Push-Location $buildDir

    Write-Info "Compiling (this may take a few minutes)..."
    & cmake --build . --config $BuildConfig --parallel

    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed!"
        Pop-Location
        exit 1
    }

    Pop-Location
    Write-Success "Build complete!"

    # Check if executable exists
    $exePath = Join-Path $buildDir "bin\$BuildConfig\CortexEngine.exe"

    if (Test-Path $exePath) {
        Write-Success "Executable created: $exePath"

        $exeSize = (Get-Item $exePath).Length / 1MB
        Write-Info "Size: $($exeSize.ToString('F2')) MB"
    } else {
        Write-Error "Executable not found at expected location!"
        exit 1
    }

} else {
    Write-Info "Skipping build (--SkipBuild flag set)"
}

# ============================================================================
# STEP 7: Verify Assets
# ============================================================================
Write-Step "Verifying assets..."

$assetsSource = Join-Path $projectRoot "assets"
$assetsTarget = Join-Path $buildDir "bin\$BuildConfig\assets"

if (Test-Path $assetsSource) {
    Write-Success "Source assets found: $assetsSource"

    if (Test-Path $assetsTarget) {
        Write-Success "Assets copied to build directory"

        $shaderFile = Join-Path $assetsTarget "shaders\Basic.hlsl"
        if (Test-Path $shaderFile) {
            Write-Success "Shader file verified: Basic.hlsl"
        } else {
            Write-Error "Shader file missing!"
        }
    } else {
        Write-Info "Assets not yet copied (will be copied on build)"
    }
} else {
    Write-Error "Assets directory not found!"
}

# ============================================================================
# FINAL SUMMARY
# ============================================================================
Write-Host @"

===============================================================
                   SETUP COMPLETE!
===============================================================
"@ -ForegroundColor Green

Write-Host "`nNext Steps:" -ForegroundColor Cyan
Write-Host "  1. Run the application:" -ForegroundColor White
Write-Host "     cd build\bin\$BuildConfig" -ForegroundColor Gray
Write-Host "     .\CortexEngine.exe" -ForegroundColor Gray
Write-Host ""
Write-Host "  2. Or use the run script:" -ForegroundColor White
Write-Host "     .\run.ps1" -ForegroundColor Gray
Write-Host ""
Write-Host "  3. Open in Visual Studio:" -ForegroundColor White
Write-Host "     start build\CortexEngine.sln" -ForegroundColor Gray
Write-Host ""

Write-Host "Controls:" -ForegroundColor Cyan
Write-Host "  T      - Enter text input mode (natural language commands)" -ForegroundColor Gray
Write-Host "  Enter  - Submit command to The Architect" -ForegroundColor Gray
Write-Host "  ESC    - Exit text input mode / Exit application" -ForegroundColor Gray
Write-Host ""

Write-Host "Try these commands:" -ForegroundColor Cyan
Write-Host "  'Add a red cube at position 2, 1, 0'" -ForegroundColor Gray
Write-Host "  'Add a blue sphere'" -ForegroundColor Gray
Write-Host "  'Make it bigger'" -ForegroundColor Gray
Write-Host ""

Write-Host "LLM Model:" -ForegroundColor Cyan
if (Test-Path $modelFile) {
    Write-Host "  Model: Llama 3.2 3B Instruct (Q4_K_M)" -ForegroundColor Gray
    Write-Host "  Path: models\Llama-3.2-3B-Instruct-Q4_K_M.gguf" -ForegroundColor Gray
    Write-Host "  Expected inference: ~2-4 seconds per command (CPU)" -ForegroundColor Gray
    Write-Host "  VRAM usage: ~2GB (leaves room for renderer + diffusion)" -ForegroundColor Gray
} else {
    Write-Host "  Running in MOCK MODE (no model loaded)" -ForegroundColor Yellow
    Write-Host "  For real LLM, download model manually" -ForegroundColor Gray
}
Write-Host ""

Write-Host "Documentation:" -ForegroundColor Cyan
Write-Host "  README.md              - Project overview" -ForegroundColor Gray
Write-Host "  BUILD.md               - Build instructions" -ForegroundColor Gray
Write-Host "  PHASE1_COMPLETE.md     - Renderer implementation" -ForegroundColor Gray
Write-Host "  PHASE2_COMPLETE.md     - LLM integration guide" -ForegroundColor Gray
Write-Host "  PHASE2_ARCHITECTURE.md - Technical architecture" -ForegroundColor Gray
Write-Host ""

$totalTime = (Get-Date) - $startTime
Write-Host "Setup completed in $($totalTime.TotalSeconds.ToString('F1')) seconds" -ForegroundColor Green
