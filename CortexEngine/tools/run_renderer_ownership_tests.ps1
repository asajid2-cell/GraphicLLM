param(
    [string]$OwnershipPath = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OwnershipPath)) {
    $OwnershipPath = Join-Path $root "assets/config/renderer_ownership_targets.json"
}

$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

if (-not (Test-Path $OwnershipPath)) {
    throw "Renderer ownership target file not found: $OwnershipPath"
}

$doc = Get-Content $OwnershipPath -Raw | ConvertFrom-Json
$rendererHeaderPath = Join-Path $root "src/Graphics/Renderer.h"
if (-not (Test-Path $rendererHeaderPath)) {
    throw "Renderer.h not found: $rendererHeaderPath"
}
$rendererHeader = Get-Content $rendererHeaderPath -Raw
$rendererRtStatePath = Join-Path $root "src/Graphics/RendererRTState.h"
if (-not (Test-Path $rendererRtStatePath)) {
    throw "RendererRTState.h not found: $rendererRtStatePath"
}
$rendererRtState = Get-Content $rendererRtStatePath -Raw
$particleStatePath = Join-Path $root "src/Graphics/RendererParticleState.h"
if (-not (Test-Path $particleStatePath)) {
    throw "RendererParticleState.h not found: $particleStatePath"
}
$particleState = Get-Content $particleStatePath -Raw
$particleRendererPath = Join-Path $root "src/Graphics/Renderer_Particles.cpp"
if (-not (Test-Path $particleRendererPath)) {
    throw "Renderer_Particles.cpp not found: $particleRendererPath"
}
$particleRenderer = Get-Content $particleRendererPath -Raw
$postStatePath = Join-Path $root "src/Graphics/RendererPostProcessState.h"
if (-not (Test-Path $postStatePath)) {
    throw "RendererPostProcessState.h not found: $postStatePath"
}
$postState = Get-Content $postStatePath -Raw
$environmentStatePath = Join-Path $root "src/Graphics/RendererEnvironmentState.h"
if (-not (Test-Path $environmentStatePath)) {
    throw "RendererEnvironmentState.h not found: $environmentStatePath"
}
$environmentState = Get-Content $environmentStatePath -Raw
$bloomStatePath = Join-Path $root "src/Graphics/RendererBloomState.h"
$temporalScreenPath = Join-Path $root "src/Graphics/RendererTemporalScreenState.h"
$ssaoStatePath = Join-Path $root "src/Graphics/RendererSSAOState.h"
$ssrStatePath = Join-Path $root "src/Graphics/RendererSSRState.h"
$ssaoRendererPath = Join-Path $root "src/Graphics/Renderer_SSAO.cpp"
$ssrRendererPath = Join-Path $root "src/Graphics/Renderer_SSRPass.cpp"
$hzbStatePath = Join-Path $root "src/Graphics/RendererHZBState.h"
$hzbRendererPath = Join-Path $root "src/Graphics/Renderer_HZB.cpp"

if ([int]$doc.schema -ne 1) {
    Add-Failure "renderer ownership schema must be 1"
}
if ($null -eq $doc.targets -or $doc.targets.Count -lt 1) {
    Add-Failure "renderer ownership target list is empty"
}

$ids = @{}
foreach ($target in $doc.targets) {
    $id = [string]$target.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        Add-Failure "ownership target id is missing"
        continue
    }
    if ($ids.ContainsKey($id)) {
        Add-Failure "duplicate ownership target id '$id'"
    }
    $ids[$id] = $true

    if ([string]::IsNullOrWhiteSpace([string]$target.owner)) {
        Add-Failure "$id owner is missing"
    }
    $status = [string]$target.status
    if ([string]::IsNullOrWhiteSpace($status)) {
        Add-Failure "$id status is missing"
    } elseif ($status -notmatch "validated|release") {
        Add-Failure "$id status '$status' is not release validated"
    }
    if ($null -ne $target.current_issue) {
        Add-Failure "$id still declares current_issue; release ownership metadata must use release_boundary plus future_extension"
    }
    if ($null -ne $target.next_step) {
        Add-Failure "$id still declares next_step; release ownership metadata must separate validated scope from future extension"
    }
    if ([string]::IsNullOrWhiteSpace([string]$target.release_boundary)) {
        Add-Failure "$id release_boundary is missing"
    }
    if ($null -eq $target.validated_contracts -or $target.validated_contracts.Count -lt 1) {
        Add-Failure "$id validated_contracts is empty"
    } else {
        foreach ($contract in $target.validated_contracts) {
            if ([string]::IsNullOrWhiteSpace([string]$contract)) {
                Add-Failure "$id has an empty validated_contracts entry"
            }
        }
    }
    if ([string]::IsNullOrWhiteSpace([string]$target.future_extension)) {
        Add-Failure "$id future_extension is missing"
    }
    if ($null -eq $target.target_files -or $target.target_files.Count -lt 1) {
        Add-Failure "$id target_files is empty"
    } else {
        foreach ($file in $target.target_files) {
            $fileName = [string]$file
            $path = Join-Path $root ("src/Graphics/{0}" -f $fileName)
            if (-not (Test-Path $path)) {
                Add-Failure "$id target file does not exist under src/Graphics: $fileName"
            }
        }
    }

    if ($null -eq $target.renderer_state_members -or $target.renderer_state_members.Count -lt 1) {
        Add-Failure "$id renderer_state_members is empty"
    } else {
        foreach ($member in $target.renderer_state_members) {
            $memberName = [string]$member
            if ($rendererHeader -notmatch "\b$([regex]::Escape($memberName))\b") {
                Add-Failure "$id renderer state member not found in Renderer.h: $memberName"
            }
        }
    }

    if ($id -eq "rt_reflection_stats") {
        foreach ($required in @("struct TargetResources", "struct DescriptorTableBundle", "rawResources", "historyResources", "descriptors")) {
            if ($rendererRtState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "rt_reflection_stats missing bundled ownership marker in RendererRTState.h: $required"
            }
        }
        foreach ($oldField in @("rawStatsBuffer", "historyStatsBuffer", "rawReadback", "historyReadback", "descriptorSrvTables", "descriptorUavTables")) {
            if ($rendererRtState -match "\b$([regex]::Escape($oldField))\b") {
                Add-Failure "rt_reflection_stats still exposes loose state field in RendererRTState.h: $oldField"
            }
        }
    }

    if ($id -eq "particle_resources") {
        foreach ($required in @("struct ParticleRenderControls", "struct ParticleRenderResources", "struct ParticleFrameStats", "struct ParticleRenderState", "ParticleRenderControls controls", "ParticleRenderResources resources", "ParticleFrameStats frame", "densityScale", "frameLiveParticles", "frameSubmittedInstances", "frameDensityScale", "InstanceBufferBytes")) {
            if ($particleState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "particle_resources missing public billboard state marker in RendererParticleState.h: $required"
            }
        }
        foreach ($oldFlatAccess in @("m_particleState.densityScale", "m_particleState.instanceBuffer", "m_particleState.frameLiveParticles", "m_particleState.effectPreset")) {
            if ($particleRenderer.IndexOf($oldFlatAccess, [StringComparison]::Ordinal) -ge 0) {
                Add-Failure "particle_resources still uses flat Renderer particle state access in Renderer_Particles.cpp: $oldFlatAccess"
            }
        }
        if ($particleRenderer.IndexOf("View<Scene::ParticleEmitterComponent, Scene::TransformComponent>", [StringComparison]::Ordinal) -lt 0) {
            Add-Failure "particle_resources public renderer path is not the ECS billboard particle path"
        }
    }

    if ($id -eq "postprocess_resources") {
        if (-not (Test-Path $bloomStatePath)) {
            Add-Failure "postprocess_resources missing RendererBloomState.h"
        } else {
            $bloomState = Get-Content $bloomStatePath -Raw
            foreach ($required in @("struct BloomPassControls", "struct BloomPyramidResources", "struct BloomDescriptorTables", "BloomPassControls controls", "BloomPyramidResources<BloomLevels> resources", "BloomDescriptorTables<BloomDescriptorSlots> descriptors")) {
                if ($bloomState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "postprocess_resources missing bloom ownership marker in RendererBloomState.h: $required"
                }
            }
            foreach ($oldField in @("float intensity", "float threshold", "float softKnee", "float maxContribution", "srvTableValid = false;")) {
                if ($bloomState -match "struct BloomPassState[\s\S]*$([regex]::Escape($oldField))") {
                    Add-Failure "postprocess_resources still exposes loose bloom state field in BloomPassState: $oldField"
                }
            }
        }
        if (-not (Test-Path $temporalScreenPath)) {
            Add-Failure "postprocess_resources missing RendererTemporalScreenState.h"
        }
        foreach ($required in @("cinematicEnabled", "EffectiveVignette", "EffectiveLensDirt", "EncodedLensDirtByte")) {
            if ($postState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "postprocess_resources missing post-state ownership marker in RendererPostProcessState.h: $required"
            }
        }
    }

    if ($id -eq "environment_resources") {
        foreach ($required in @("ActiveEnvironment", "ActiveEnvironmentName", "UsingFallbackEnvironment", "ResidentCount", "PendingCount")) {
            if ($environmentState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "environment_resources missing state ownership marker in RendererEnvironmentState.h: $required"
            }
        }
    }

    if ($id -eq "screen_space_resources") {
        if (-not (Test-Path $ssaoStatePath)) {
            Add-Failure "screen_space_resources missing RendererSSAOState.h"
        } else {
            $ssaoState = Get-Content $ssaoStatePath -Raw
            foreach ($required in @("struct SSAOControls", "struct SSAOResources", "struct SSAODescriptorTables", "SSAOControls controls", "SSAOResources resources", "SSAODescriptorTables descriptors")) {
                if ($ssaoState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "screen_space_resources missing SSAO ownership marker in RendererSSAOState.h: $required"
                }
            }
            foreach ($oldField in @("bool enabled", "float radius", "float bias", "float intensity", "descriptorTablesValid = false;")) {
                if ($ssaoState -match "struct SSAOPassState[\s\S]*$([regex]::Escape($oldField))") {
                    Add-Failure "screen_space_resources still exposes loose SSAO state field in SSAOPassState: $oldField"
                }
            }
        }
        if (-not (Test-Path $ssrStatePath)) {
            Add-Failure "screen_space_resources missing RendererSSRState.h"
        } else {
            $ssrState = Get-Content $ssrStatePath -Raw
            foreach ($required in @("struct SSRControls", "struct SSRResources", "struct SSRDescriptorTables", "struct SSRFrameState", "SSRControls controls", "SSRResources resources", "SSRDescriptorTables descriptors", "SSRFrameState frame")) {
                if ($ssrState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "screen_space_resources missing SSR ownership marker in RendererSSRState.h: $required"
                }
            }
            foreach ($oldField in @("bool enabled", "bool activeThisFrame", "float maxDistance", "float thickness", "float strength", "srvTableValid = false;")) {
                if ($ssrState -match "struct SSRPassState[\s\S]*$([regex]::Escape($oldField))") {
                    Add-Failure "screen_space_resources still exposes loose SSR state field in SSRPassState: $oldField"
                }
            }
        }
        if (Test-Path $ssaoRendererPath) {
            $ssaoRenderer = Get-Content $ssaoRendererPath -Raw
            foreach ($oldFlatAccess in @("m_ssaoResources.enabled", "m_ssaoResources.texture", "m_ssaoResources.resourceState", "m_ssaoResources.srvTables")) {
                if ($ssaoRenderer.IndexOf($oldFlatAccess, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "screen_space_resources still uses flat SSAO state access in Renderer_SSAO.cpp: $oldFlatAccess"
                }
            }
        }
        if (Test-Path $ssrRendererPath) {
            $ssrRenderer = Get-Content $ssrRendererPath -Raw
            foreach ($oldFlatAccess in @("m_ssrResources.enabled", "m_ssrResources.color", "m_ssrResources.resourceState", "m_ssrResources.srvTables")) {
                if ($ssrRenderer.IndexOf($oldFlatAccess, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "screen_space_resources still uses flat SSR state access in Renderer_SSRPass.cpp: $oldFlatAccess"
                }
            }
        }
    }

    if ($id -eq "hzb_resources") {
        if (-not (Test-Path $hzbStatePath)) {
            Add-Failure "hzb_resources missing RendererHZBState.h"
        } else {
            $hzbState = Get-Content $hzbStatePath -Raw
            foreach ($required in @("struct HZBResources", "struct HZBDescriptorTables", "struct HZBDebugControls", "struct HZBCaptureState", "HZBResources resources", "HZBDescriptorTables descriptors", "HZBDebugControls debug", "HZBCaptureState capture")) {
                if ($hzbState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                    Add-Failure "hzb_resources missing ownership marker in RendererHZBState.h: $required"
                }
            }
            foreach ($oldField in @("ComPtr<ID3D12Resource> texture", "DescriptorHandle fullSRV", "dispatchTablesValid = false;", "uint32_t debugMip", "bool captureValid")) {
                if ($hzbState -match "struct HZBPassState[\s\S]*$([regex]::Escape($oldField))") {
                    Add-Failure "hzb_resources still exposes loose HZB state field in HZBPassState: $oldField"
                }
            }
        }
        if (Test-Path $hzbRendererPath) {
            $hzbRenderer = Get-Content $hzbRendererPath -Raw
            foreach ($oldFlatAccess in @("m_hzbResources.texture", "m_hzbResources.fullSRV", "m_hzbResources.dispatchTablesValid", "m_hzbResources.captureValid", "m_hzbResources.debugMip")) {
                if ($hzbRenderer.IndexOf($oldFlatAccess, [StringComparison]::Ordinal) -ge 0) {
                    Add-Failure "hzb_resources still uses flat HZB state access in Renderer_HZB.cpp: $oldFlatAccess"
                }
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Renderer ownership tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Renderer ownership tests passed: targets=$($doc.targets.Count)" -ForegroundColor Green
