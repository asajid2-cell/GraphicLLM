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
    if ([string]::IsNullOrWhiteSpace([string]$target.status)) {
        Add-Failure "$id status is missing"
    }
    if ([string]::IsNullOrWhiteSpace([string]$target.current_issue)) {
        Add-Failure "$id current_issue is missing"
    }
    if ([string]::IsNullOrWhiteSpace([string]$target.next_step)) {
        Add-Failure "$id next_step is missing"
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
        foreach ($required in @("struct ParticleRenderState", "densityScale", "frameLiveParticles", "frameSubmittedInstances", "frameDensityScale", "InstanceBufferBytes")) {
            if ($particleState.IndexOf($required, [StringComparison]::Ordinal) -lt 0) {
                Add-Failure "particle_resources missing public billboard state marker in RendererParticleState.h: $required"
            }
        }
        if ($particleRenderer.IndexOf("View<Scene::ParticleEmitterComponent, Scene::TransformComponent>", [StringComparison]::Ordinal) -lt 0) {
            Add-Failure "particle_resources public renderer path is not the ECS billboard particle path"
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
