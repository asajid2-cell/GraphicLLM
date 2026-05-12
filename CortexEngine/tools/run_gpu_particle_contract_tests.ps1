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
Assert-File "src/Graphics/Passes/ParticleGpuPreparePass.h"
Assert-File "src/Graphics/Passes/ParticleGpuPreparePass.cpp"
Assert-File "src/Graphics/Passes/ParticleGpuLifecyclePass.h"
Assert-File "src/Graphics/Passes/ParticleGpuLifecyclePass.cpp"
Assert-File "assets/shaders/ParticleEmit.hlsl"
Assert-File "assets/shaders/ParticleSimulate.hlsl"
Assert-File "assets/shaders/ParticleSort.hlsl"
Assert-File "assets/shaders/ParticleRender.hlsl"
Assert-File "assets/shaders/ParticleInstancePrepare.hlsl"
Assert-File "assets/shaders/ParticleEmitterLifecycle.hlsl"

$cmake = if (Test-Path $cmakePath) { Get-Content $cmakePath -Raw } else { "" }
$header = if (Test-Path $headerPath) { Get-Content $headerPath -Raw } else { "" }
$cpp = if (Test-Path $cppPath) { Get-Content $cppPath -Raw } else { "" }
$catalogRaw = if (Test-Path $catalogPath) { Get-Content $catalogPath -Raw } else { "" }

Assert-Contains $cmake "src/Graphics/GPUParticles.cpp" "GPUParticles.cpp is not part of the CortexEngine build."
Assert-Contains $cmake "src/Graphics/Passes/ParticleGpuPreparePass.cpp" "ParticleGpuPreparePass.cpp is not part of the CortexEngine build."
Assert-Contains $cmake "src/Graphics/Passes/ParticleGpuLifecyclePass.cpp" "ParticleGpuLifecyclePass.cpp is not part of the CortexEngine build."
Assert-Contains $header "class GPUParticleSystem" "GPUParticleSystem class declaration is missing."
Assert-Contains $header "ParticleEmitterConfig" "ParticleEmitterConfig is missing."
Assert-Contains $header "ParticleStats" "ParticleStats is missing."
Assert-Contains $cpp "GPUParticleSystem::Initialize" "GPUParticleSystem Initialize implementation is missing."
Assert-Contains $cpp "GPUParticleSystem::Update" "GPUParticleSystem Update implementation is missing."
Assert-Contains $cpp "GPUParticleSystem::Render" "GPUParticleSystem Render constants implementation is missing."
Assert-Contains $cpp "CreateFireEmitter" "Default fire GPU particle emitter is missing."
Assert-Contains $cpp "CreateSmokeEmitter" "Default smoke GPU particle emitter is missing."
Assert-Contains $cpp "CreateSparkEmitter" "Default spark GPU particle emitter is missing."
Assert-Contains (Get-Content (Join-Path $root "src/Graphics/Renderer_GeometryPipelineSetup.cpp") -Raw) "ParticleInstancePrepare.hlsl" "Renderer does not compile the public GPU particle prepare shader."
Assert-Contains (Get-Content (Join-Path $root "src/Graphics/Renderer_GeometryPipelineSetup.cpp") -Raw) "ParticleEmitterLifecycle.hlsl" "Renderer does not compile the public GPU particle lifecycle shader."
$particleState = Get-Content (Join-Path $root "src/Graphics/RendererParticleState.h") -Raw
$particlePreparePass = Get-Content (Join-Path $root "src/Graphics/Passes/ParticleGpuPreparePass.cpp") -Raw
$particleLifecyclePass = Get-Content (Join-Path $root "src/Graphics/Passes/ParticleGpuLifecyclePass.cpp") -Raw
Assert-Contains $particleState "EnsureGpuPrepareDescriptors" "ParticleRenderResources does not own persistent prepare descriptors."
Assert-Contains $particleState "EnsureGpuLifecycleDescriptors" "ParticleRenderResources does not own persistent lifecycle descriptors."
Assert-Contains $particleState "AllocateCBV_SRV_UAV" "ParticleRenderResources does not allocate persistent shader-visible descriptors."
Assert-Contains $particleState "CreateShaderResourceView" "ParticleRenderResources does not publish particle SRVs."
Assert-Contains $particleState "CreateUnorderedAccessView" "ParticleRenderResources does not publish the particle instance UAV."
Assert-Contains $particlePreparePass "SetComputeRootSignature" "ParticleGpuPreparePass does not dispatch the GPU prepare pass."
Assert-Contains $particlePreparePass "EnsureGpuPrepareDescriptors" "ParticleGpuPreparePass does not bind persistent prepare descriptors."
Assert-Contains $particleLifecyclePass "SetComputeRootSignature" "ParticleGpuLifecyclePass does not dispatch the GPU lifecycle pass."
Assert-Contains $particleLifecyclePass "EnsureGpuLifecycleDescriptors" "ParticleGpuLifecyclePass does not bind persistent lifecycle descriptors."
foreach ($transientDescriptorCall in @("AllocateTransientCBV_SRV_UAV", "AllocateTransientCBV_SRV_UAVRange")) {
    if ($particlePreparePass.IndexOf($transientDescriptorCall, [StringComparison]::Ordinal) -ge 0) {
        Add-Failure "ParticleGpuPreparePass still allocates transient descriptors: $transientDescriptorCall"
    }
    if ($particleLifecyclePass.IndexOf($transientDescriptorCall, [StringComparison]::Ordinal) -ge 0) {
        Add-Failure "ParticleGpuLifecyclePass still allocates transient descriptors: $transientDescriptorCall"
    }
}
Assert-Contains (Get-Content (Join-Path $root "src/Graphics/Renderer_FrameContractSnapshot.cpp") -Raw) "gpu_emitter_lifecycle_sort_dx12_instanced_billboard" "Frame contract does not report the GPU lifecycle particle backend."
Assert-Contains (Get-Content (Join-Path $root "src/Graphics/FrameContractJson.cpp") -Raw) "gpu_lifecycle_path" "Frame contract JSON does not expose gpu_lifecycle_path."
Assert-Contains (Get-Content (Join-Path $root "src/Core/Engine.cpp") -Raw) "UsesGpuParticleLifecycle" "Engine CPU particle update is not gated by the GPU particle lifecycle path."
$rendererParticles = Get-Content (Join-Path $root "src/Graphics/Renderer_Particles.cpp") -Raw
if ($rendererParticles.IndexOf("emitter.particles", [StringComparison]::Ordinal) -ge 0) {
    Add-Failure "Renderer_Particles.cpp still consumes ECS-owned emitter.particles in the public path."
}

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
            "src/Graphics/Passes/ParticleGpuPreparePass.h",
            "src/Graphics/Passes/ParticleGpuPreparePass.cpp",
            "src/Graphics/Passes/ParticleGpuLifecyclePass.h",
            "src/Graphics/Passes/ParticleGpuLifecyclePass.cpp",
            "assets/shaders/ParticleEmit.hlsl",
            "assets/shaders/ParticleSimulate.hlsl",
            "assets/shaders/ParticleSort.hlsl",
            "assets/shaders/ParticleRender.hlsl",
            "assets/shaders/ParticleInstancePrepare.hlsl",
            "assets/shaders/ParticleEmitterLifecycle.hlsl")) {
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
