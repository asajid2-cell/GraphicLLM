param(
    [string]$LogDir = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$rendererHeaderPath = Join-Path $root "src/Graphics/Renderer.h"
$ownershipScript = Join-Path $PSScriptRoot "run_renderer_ownership_tests.ps1"
$activeLogDir = if ([string]::IsNullOrWhiteSpace($LogDir)) {
    Join-Path $root "build/bin/logs"
} else {
    $LogDir
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null

if (-not (Test-Path $rendererHeaderPath)) {
    throw "Renderer.h not found: $rendererHeaderPath"
}

$rendererHeader = Get-Content $rendererHeaderPath -Raw
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

$expectedMembers = @(
    [pscustomobject]@{ Name = "m_services"; Type = "RendererServiceState" },
    [pscustomobject]@{ Name = "m_rtDenoiseState"; Type = "RTDenoisePassState" },
    [pscustomobject]@{ Name = "m_rtReflectionSignalState"; Type = "RTReflectionSignalStatsState" },
    [pscustomobject]@{ Name = "m_rtShadowTargets"; Type = "RTShadowTargetState" },
    [pscustomobject]@{ Name = "m_rtReflectionTargets"; Type = "RTReflectionTargetState" },
    [pscustomobject]@{ Name = "m_rtGITargets"; Type = "RTGITargetState" },
    [pscustomobject]@{ Name = "m_rtReflectionReadiness"; Type = "RTReflectionReadinessState" },
    [pscustomobject]@{ Name = "m_commandResources"; Type = "RendererCommandResourceState" },
    [pscustomobject]@{ Name = "m_pipelineState"; Type = "RendererPipelineState" },
    [pscustomobject]@{ Name = "m_particleState"; Type = "ParticleRenderState" },
    [pscustomobject]@{ Name = "m_debugOverlayState"; Type = "RendererDebugOverlayState" },
    [pscustomobject]@{ Name = "m_constantBuffers"; Type = "RendererConstantBufferState" },
    [pscustomobject]@{ Name = "m_uploadCommands"; Type = "UploadCommandPoolState" },
    [pscustomobject]@{ Name = "m_breadcrumbs"; Type = "RendererBreadcrumbState" },
    [pscustomobject]@{ Name = "m_assetRuntime"; Type = "RendererAssetRuntimeState" },
    [pscustomobject]@{ Name = "m_depthResources"; Type = "DepthTargetState" },
    [pscustomobject]@{ Name = "m_hzbResources"; Type = "HZBPassState" },
    [pscustomobject]@{ Name = "m_shadowResources"; Type = "ShadowMapPassState<kShadowArraySize, kShadowCascadeCount>" },
    [pscustomobject]@{ Name = "m_environmentState"; Type = "EnvironmentLightingState" },
    [pscustomobject]@{ Name = "m_mainTargets"; Type = "MainRenderTargetState" },
    [pscustomobject]@{ Name = "m_ssaoResources"; Type = "SSAOPassState" },
    [pscustomobject]@{ Name = "m_ssrResources"; Type = "SSRPassState" },
    [pscustomobject]@{ Name = "m_temporalScreenState"; Type = "TemporalScreenPassState" },
    [pscustomobject]@{ Name = "m_temporalMaskState"; Type = "TemporalMaskPassState" },
    [pscustomobject]@{ Name = "m_bloomResources"; Type = "BloomPassState<kBloomLevels, kBloomDescriptorSlots>" },
    [pscustomobject]@{ Name = "m_materialFallbacks"; Type = "MaterialFallbackTextureState" },
    [pscustomobject]@{ Name = "m_debugLineState"; Type = "DebugLineRenderState" },
    [pscustomobject]@{ Name = "m_debugViewState"; Type = "RendererDebugViewState" },
    [pscustomobject]@{ Name = "m_lightingState"; Type = "RendererLightingState" },
    [pscustomobject]@{ Name = "m_qualityRuntimeState"; Type = "RendererQualityRuntimeState" },
    [pscustomobject]@{ Name = "m_temporalAAState"; Type = "TemporalAAState" },
    [pscustomobject]@{ Name = "m_cameraState"; Type = "RendererCameraFrameState" },
    [pscustomobject]@{ Name = "m_localShadowState"; Type = "RendererLocalShadowState<kMaxShadowedLocalLights>" },
    [pscustomobject]@{ Name = "m_shadowCascadeState"; Type = "ShadowCascadeFrameState<kShadowCascadeCount>" },
    [pscustomobject]@{ Name = "m_rtRuntimeState"; Type = "RTRuntimeState" },
    [pscustomobject]@{ Name = "m_gpuCullingState"; Type = "GpuCullingRuntimeState" },
    [pscustomobject]@{ Name = "m_visibilityBufferState"; Type = "RendererVisibilityBufferState" },
    [pscustomobject]@{ Name = "m_frameLifecycle"; Type = "RendererFrameLifecycleState" },
    [pscustomobject]@{ Name = "m_framePlanning"; Type = "RendererFramePlanningState" },
    [pscustomobject]@{ Name = "m_temporalHistory"; Type = "RendererTemporalHistoryState" },
    [pscustomobject]@{ Name = "m_frameDiagnostics"; Type = "RendererFrameDiagnosticsState" },
    [pscustomobject]@{ Name = "m_voxelState"; Type = "VoxelRenderState" },
    [pscustomobject]@{ Name = "m_fractalSurfaceState"; Type = "RendererFractalSurfaceState" },
    [pscustomobject]@{ Name = "m_postProcessState"; Type = "RendererPostProcessState" },
    [pscustomobject]@{ Name = "m_fogState"; Type = "RendererFogState" },
    [pscustomobject]@{ Name = "m_waterState"; Type = "RendererWaterState" },
    [pscustomobject]@{ Name = "m_vegetationState"; Type = "VegetationRenderState" },
    [pscustomobject]@{ Name = "m_frameRuntime"; Type = "RendererFrameRuntimeState" }
)

$actualMembers = @{}
$memberRegex = [regex]'(?m)^\s*([A-Za-z0-9_:]+(?:<[^;]+>)?)\s+(m_[A-Za-z0-9_]+)\s*;'
foreach ($match in $memberRegex.Matches($rendererHeader)) {
    $type = ($match.Groups[1].Value -replace '\s+', ' ').Trim()
    $name = $match.Groups[2].Value
    $actualMembers[$name] = $type
}

$expectedByName = @{}
foreach ($member in $expectedMembers) {
    $expectedByName[$member.Name] = $member.Type
    if (-not $actualMembers.ContainsKey($member.Name)) {
        Add-Failure "Renderer.h missing expected state member $($member.Name) of type $($member.Type)"
        continue
    }
    if ($actualMembers[$member.Name] -ne $member.Type) {
        Add-Failure "Renderer.h member $($member.Name) type drifted: expected '$($member.Type)', actual '$($actualMembers[$member.Name])'"
    }
}

foreach ($name in $actualMembers.Keys) {
    if (-not $expectedByName.ContainsKey($name)) {
        Add-Failure "Renderer.h has unowned renderer member '$name' of type '$($actualMembers[$name])'"
    }
}

$looseGpuPatterns = @(
    'ComPtr<[^>]+>\s+m_',
    'DescriptorHandle\s+m_',
    'D3D12_[A-Za-z0-9_<>:,\s]+\s+m_',
    'std::unique_ptr<[^>]+>\s+m_',
    'std::shared_ptr<[^>]+>\s+m_'
)
foreach ($pattern in $looseGpuPatterns) {
    foreach ($match in [regex]::Matches($rendererHeader, $pattern)) {
        Add-Failure "Renderer.h exposes loose resource/service member instead of a state aggregate: '$($match.Value.Trim())'"
    }
}

$graphicsHeaders = Get-ChildItem (Join-Path $root "src/Graphics") -Filter "*.h" -File
$headerTextByName = @{}
foreach ($header in $graphicsHeaders) {
    $headerTextByName[$header.Name] = Get-Content $header.FullName -Raw
}

foreach ($member in $expectedMembers) {
    $baseType = [regex]::Replace($member.Type, '<.*$', '')
    $foundType = $false
    foreach ($entry in $headerTextByName.GetEnumerator()) {
        if ($entry.Value -match "\b(struct|class)\s+$([regex]::Escape($baseType))\b") {
            $foundType = $true
            break
        }
    }
    if (-not $foundType) {
        Add-Failure "Could not find struct/class definition for renderer member type '$($member.Type)' ($($member.Name))"
    }
}

if ($rendererHeader -match 'current_issue|next_step') {
    Add-Failure "Renderer.h contains ownership-planning placeholders instead of finalized state boundaries"
}

if (Test-Path $ownershipScript) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $ownershipScript
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "selected renderer ownership target gate failed"
    }
} else {
    Add-Failure "selected renderer ownership target script missing: $ownershipScript"
}

$summary = [pscustomobject]@{
    renderer_members = $actualMembers.Count
    expected_members = $expectedMembers.Count
    loose_gpu_patterns_checked = $looseGpuPatterns.Count
    ownership_manifest_gate = "run_renderer_ownership_tests.ps1"
}
$summaryPath = Join-Path $activeLogDir "renderer_full_ownership_audit_summary.json"
$summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $summaryPath

if ($failures.Count -gt 0) {
    Write-Host "Renderer full ownership audit failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$activeLogDir"
    exit 1
}

Write-Host "Renderer full ownership audit passed." -ForegroundColor Green
Write-Host ("  renderer_members={0} expected_members={1}" -f $actualMembers.Count, $expectedMembers.Count)
Write-Host "  logs=$activeLogDir"
