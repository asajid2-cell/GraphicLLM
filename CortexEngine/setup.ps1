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
# STEP 1: Check Prerequisites & Tools
# ============================================================================
Write-Step "Checking prerequisites..."

# Check for CMake
try {
    $cmakeVersion = & cmake --version 2>&1 | Select-String -Pattern "version (\d+\.\d+)" | ForEach-Object { $_.Matches.Groups[1].Value }
    Write-Success "CMake found: version $cmakeVersion"
} catch {
    Write-Error "CMake not found! Install from https://cmake.org/download/"
    exit 1
}

# Check for Git
try {
    $gitVersion = & git --version 2>&1
    Write-Success "Git found: $gitVersion"
} catch {
    Write-Error "Git not found!"
    exit 1
}

# Check for Visual Studio / MSBuild
try {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    $vsPath = & $vswhere -latest -property installationPath
    
    if (-not $vsPath) { throw "Visual Studio not found" }
    Write-Success "Visual Studio found at: $vsPath"

    $vsDevCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
    
    Write-Info "Importing VS environment..."
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
    Write-Success "Visual Studio environment imported"

} catch {
    Write-Error "Could not detect Visual Studio! Ensure C++ workload is installed."
    exit 1
}

# Check for CUDA
if ($env:CUDA_PATH) {
    Write-Success "CUDA toolkit detected: $env:CUDA_PATH"
} else {
    Write-Error "CUDA not found. Please install NVIDIA CUDA Toolkit 13.0."
    exit 1
}

# Optional: Detect TensorRT for Dreamer GPU diffusion. If found, we will
# enable CORTEX_ENABLE_TENSORRT at CMake configure time.
$TensorRTIncludeDir = $null
$TensorRTLibDir = $null

if ($env:TENSORRT_ROOT) {
    $candidateRoot = $env:TENSORRT_ROOT
    $inc = Join-Path $candidateRoot "include"
    $lib = Join-Path $candidateRoot "lib"
    if ((Test-Path $inc) -and (Test-Path $lib)) {
        $TensorRTIncludeDir = $inc
        $TensorRTLibDir = $lib
        Write-Success "TensorRT detected via TENSORRT_ROOT at: $candidateRoot"
    } else {
        Write-Info "TENSORRT_ROOT is set but 'include' or 'lib' was not found under $candidateRoot; ignoring."
    }
} else {
    # Common default install location; adjust if your TensorRT is elsewhere.
    $defaultTrtRoot = "C:\TensorRT"
    $inc = Join-Path $defaultTrtRoot "include"
    $lib = Join-Path $defaultTrtRoot "lib"
    if ((Test-Path $inc) -and (Test-Path $lib)) {
        $TensorRTIncludeDir = $inc
        $TensorRTLibDir = $lib
        Write-Success "TensorRT detected at: $defaultTrtRoot"
    } else {
        Write-Info "TensorRT not detected automatically. Set TENSORRT_ROOT to enable Dreamer GPU diffusion (otherwise CPU stub will be used)."
    }
}

# Ensure Ninja is available
function Ensure-Ninja {
    $ninjaDir = "$env:ProgramFiles\Ninja"
    $ninjaExe = "$ninjaDir\ninja.exe"
    
    if (-not (Test-Path $ninjaExe)) {
        Write-Info "Installing Ninja build tool..."
        mkdir $ninjaDir -ErrorAction SilentlyContinue | Out-Null
        Invoke-WebRequest -Uri "https://github.com/ninja-build/ninja/releases/download/v1.12.1/ninja-win.zip" -OutFile "$env:TEMP\ninja.zip"
        Expand-Archive "$env:TEMP\ninja.zip" -DestinationPath $ninjaDir -Force
    }
    
    # Add to PATH strictly if missing
    if ($env:PATH -notlike "*$ninjaDir*") { 
        $env:PATH = "$ninjaDir;$env:PATH" 
    }
    
    Write-Success "Ninja build tool ready at $ninjaExe"
    return $ninjaExe
}
$ninjaExe = Ensure-Ninja

# ============================================================================
# STEP 2: Initialize Git Submodules
# ============================================================================
Write-Step "Initializing git submodules..."
$projectRoot = $PSScriptRoot
Push-Location $projectRoot

# FORCE FIX: Re-register submodule if broken
if (-not (Test-Path "vendor\llama.cpp\.git")) {
    Write-Info "Submodule not found. Re-registering..."
    if (-not (Test-Path ".gitmodules")) {
        Set-Content -Path .gitmodules -Value '[submodule "vendor/llama.cpp"]
	path = vendor/llama.cpp
	url = https://github.com/ggerganov/llama.cpp.git'
    }
    git submodule sync
    git submodule update --init --recursive --force
}

# CRITICAL: Force update llama.cpp to latest MASTER (Required for Llama 3.2)
Write-Info "Updating llama.cpp to latest version (fixing Empty Generation bug)..."
Push-Location vendor/llama.cpp
git fetch origin
git checkout master
git pull origin master
Pop-Location
Write-Success "llama.cpp updated to latest master"

Pop-Location

# ============================================================================
# STEP 3: Download Model
# ============================================================================
Write-Step "Downloading LLM model..."
$modelDir = Join-Path $projectRoot "models"
$modelFile = Join-Path $modelDir "Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf"
if (-not (Test-Path $modelDir)) { New-Item -ItemType Directory -Force -Path $modelDir | Out-Null }

if (Test-Path $modelFile) {
    Write-Success "Model already exists"
} else {
    Write-Info "Downloading Llama 3.1 8B Instruct (Q4_K_M)..."
    $url = "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/main/Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf"
    try {
        $webClient = New-Object System.Net.WebClient
        $webClient.DownloadFile($url, $modelFile)
        $webClient.Dispose()
        Write-Success "Model downloaded successfully!"
    } catch {
        Write-Error "Failed to download model. Setup will continue in Mock Mode."
    }
}

# ============================================================================
# STEP 4: Setup vcpkg
# ============================================================================
if (-not $SkipVcpkg) {
    Write-Step "Setting up vcpkg..."

    # FORCE STANDALONE VCPKG
    if (Test-Path "C:\vcpkg\vcpkg.exe") {
        Write-Info "Forcing use of standalone vcpkg at C:\vcpkg"
        $vcpkgRoot = "C:\vcpkg"
        $env:VCPKG_ROOT = "C:\vcpkg"
    } else {
        Write-Info "Installing vcpkg to C:\vcpkg..."
        Push-Location C:\
        git clone https://github.com/Microsoft/vcpkg.git
        Set-Location vcpkg
        .\bootstrap-vcpkg.bat
        Pop-Location
        $vcpkgRoot = "C:\vcpkg"
        $env:VCPKG_ROOT = "C:\vcpkg"
    }
    Write-Success "vcpkg found at: $vcpkgRoot"

    Write-Step "Installing dependencies..."
    $packages = @("sdl3:x64-windows", "entt:x64-windows", "nlohmann-json:x64-windows", "spdlog:x64-windows", "directx-headers:x64-windows", "directxtk12:x64-windows", "glm:x64-windows")
    
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
}
else {
    # When skipping vcpkg installation, try to infer vcpkg root from environment or default location.
    if (-not $vcpkgRoot) {
        if ($env:VCPKG_ROOT) {
            $vcpkgRoot = $env:VCPKG_ROOT
        } else {
            $vcpkgRoot = "C:\vcpkg"
        }
    }
}

# ============================================================================
# STEP 5: Configure & Build
# ============================================================================
Write-Step "Configuring CMake build..."
$buildDir = Join-Path $projectRoot "build"
$toolchainFile = Join-Path $vcpkgRoot "scripts\buildsystems\vcpkg.cmake"

# Verification debug prints
Write-Info "Ninja Path: $ninjaExe"
Write-Info "Toolchain:  $toolchainFile"

# Clean build directory
if (Test-Path $buildDir) {
    try {
        Remove-Item -Recurse -Force $buildDir -ErrorAction Stop
    } catch {
        Write-Info "Build directory is in use; reusing existing folder instead of deleting it."
    }
}
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null
Push-Location $buildDir

Write-Info "Running CMake configure..."

# Use an Array for arguments to prevent parsing errors
$cmakeArgs = @(
    "..",
    "-G", "Ninja",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
    "-DGGML_CUDA=ON",
    "-DCMAKE_CUDA_FLAGS=--use-local-env -allow-unsupported-compiler",
    "-DCMAKE_BUILD_TYPE=$BuildConfig",
    "-DCMAKE_MAKE_PROGRAM=$ninjaExe"
)

if ($TensorRTIncludeDir -and $TensorRTLibDir) {
    $cmakeArgs += "-DCORTEX_ENABLE_TENSORRT=ON"
    $cmakeArgs += "-DTensorRT_INCLUDE_DIR=$TensorRTIncludeDir"
    $cmakeArgs += "-DTensorRT_LIB_DIR=$TensorRTLibDir"
} else {
    # Be explicit so toggling between ON/OFF reconfigures correctly.
    $cmakeArgs += "-DCORTEX_ENABLE_TENSORRT=OFF"
}

& cmake $cmakeArgs

if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed!"
    Pop-Location
    exit 1
}
Write-Success "CMake configuration complete!"

if (-not $SkipBuild) {
    Write-Step "Building project..."
    & cmake --build . --config $BuildConfig --parallel
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Build failed!"
        exit 1
    }
    Write-Success "Build complete!"

    # Auto-Copy Assets & CUDA DLLs
    Write-Step "Deploying Runtime Files..."
    $exeDir = $buildDir # Ninja outputs to root build folder usually
    
    # If CortexEngine.exe is not in root build, check bin
    if (-not (Test-Path "$exeDir\CortexEngine.exe")) { $exeDir = "$buildDir\bin" }
    
    if (Test-Path "$exeDir\CortexEngine.exe") {
        Copy-Item -Path "$projectRoot\assets" -Destination "$exeDir\assets" -Recurse -Force
        Copy-Item -Path "$projectRoot\models" -Destination "$exeDir\models" -Recurse -Force
        
        # Copy CUDA 13 Runtime
        $cudaBin = "$env:CUDAToolkit_ROOT\bin"
        if (Test-Path $cudaBin) {
            Copy-Item "$cudaBin\cudart64_*.dll" $exeDir -Force
            Copy-Item "$cudaBin\cublas64_*.dll" $exeDir -Force
            Copy-Item "$cudaBin\cublasLt64_*.dll" $exeDir -Force
        }
        Write-Success "Deployment complete to: $exeDir"
    }
}

$totalTime = (Get-Date) - $startTime
Write-Host "Setup completed in $($totalTime.TotalSeconds.ToString('F1')) seconds" -ForegroundColor Green
