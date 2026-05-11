param()

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Assert-Contains([string]$Content, [string]$Needle, [string]$Message) {
    if ($Content.IndexOf($Needle, [StringComparison]::Ordinal) -lt 0) {
        Add-Failure $Message
    }
}

function Assert-File([string]$RelativePath) {
    $path = Join-Path $root $RelativePath
    if (-not (Test-Path $path)) {
        Add-Failure "Missing required GPU particle file: $RelativePath"
    }
}

$cmakePath = Join-Path $root "CMakeLists.txt"
$headerPath = Join-Path $root "src/Graphics/GPUParticles.h"
$cppPath = Join-Path $root "src/Graphics/GPUParticles.cpp"
$catalogPath = Join-Path $root "assets/config/advanced_graphics_catalog.json"

Assert-File "src/Graphics/GPUParticles.h"
Assert-File "src/Graphics/GPUParticles.cpp"
Assert-File "assets/shaders/ParticleEmit.hlsl"
Assert-File "assets/shaders/ParticleSimulate.hlsl"
Assert-File "assets/shaders/ParticleSort.hlsl"
Assert-File "assets/shaders/ParticleRender.hlsl"

$cmake = if (Test-Path $cmakePath) { Get-Content $cmakePath -Raw } else { "" }
$header = if (Test-Path $headerPath) { Get-Content $headerPath -Raw } else { "" }
$cpp = if (Test-Path $cppPath) { Get-Content $cppPath -Raw } else { "" }
$catalogRaw = if (Test-Path $catalogPath) { Get-Content $catalogPath -Raw } else { "" }

Assert-Contains $cmake "src/Graphics/GPUParticles.cpp" "GPUParticles.cpp is not part of the CortexEngine build."
Assert-Contains $header "class GPUParticleSystem" "GPUParticleSystem class declaration is missing."
Assert-Contains $header "ParticleEmitterConfig" "ParticleEmitterConfig is missing."
Assert-Contains $header "ParticleStats" "ParticleStats is missing."
Assert-Contains $cpp "GPUParticleSystem::Initialize" "GPUParticleSystem Initialize implementation is missing."
Assert-Contains $cpp "GPUParticleSystem::Update" "GPUParticleSystem Update implementation is missing."
Assert-Contains $cpp "GPUParticleSystem::Render" "GPUParticleSystem Render constants implementation is missing."
Assert-Contains $cpp "CreateFireEmitter" "Default fire GPU particle emitter is missing."
Assert-Contains $cpp "CreateSmokeEmitter" "Default smoke GPU particle emitter is missing."
Assert-Contains $cpp "CreateSparkEmitter" "Default spark GPU particle emitter is missing."

if (-not [string]::IsNullOrWhiteSpace($catalogRaw)) {
    $catalog = $catalogRaw | ConvertFrom-Json
    $particleSystem = $catalog.systems | Where-Object { [string]$_.id -eq "particles" } | Select-Object -First 1
    if ($null -eq $particleSystem) {
        Add-Failure "advanced_graphics_catalog.json missing particles system entry."
    } else {
        $entries = @{}
        foreach ($entry in $particleSystem.existing_entry_points) {
            $entries[[string]$entry] = $true
        }
        foreach ($requiredEntry in @(
            "src/Graphics/GPUParticles.h",
            "src/Graphics/GPUParticles.cpp",
            "assets/shaders/ParticleEmit.hlsl",
            "assets/shaders/ParticleSimulate.hlsl",
            "assets/shaders/ParticleSort.hlsl",
            "assets/shaders/ParticleRender.hlsl")) {
            if (-not $entries.ContainsKey($requiredEntry)) {
                Add-Failure "particles catalog entry missing '$requiredEntry'"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "GPU particle contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "GPU particle contract tests passed" -ForegroundColor Green
