param(
    [string]$LogDir = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$baseLogDir = Join-Path $root "build/bin/logs"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "vegetation_state_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Require-Contains([string]$Text, [string]$Needle, [string]$Message) {
    if (-not $Text.Contains($Needle)) {
        Add-Failure $Message
    }
}

if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Release rebuild failed before vegetation state contract tests"
    }
}

$rendererHeader = Get-Content (Join-Path $root "src/Graphics/Renderer.h") -Raw
$stateHeader = Get-Content (Join-Path $root "src/Graphics/RendererVegetationState.h") -Raw
$vegetationCpp = Get-Content (Join-Path $root "src/Graphics/Renderer_Vegetation.cpp") -Raw
$frameContract = Get-Content (Join-Path $root "src/Graphics/FrameContract.h") -Raw
$frameJson = Get-Content (Join-Path $root "src/Graphics/FrameContractJson.cpp") -Raw
$snapshot = Get-Content (Join-Path $root "src/Graphics/Renderer_FrameContractSnapshot.cpp") -Raw
$ownershipAudit = Get-Content (Join-Path $PSScriptRoot "run_renderer_full_ownership_audit.ps1") -Raw

Require-Contains $rendererHeader "#include `"Graphics/RendererVegetationState.h`"" `
    "Renderer.h does not include RendererVegetationState.h"
Require-Contains $rendererHeader "VegetationRenderState m_vegetationState;" `
    "Renderer.h does not own vegetation through VegetationRenderState"
foreach ($required in @(
    "std::unique_ptr<DX12Pipeline> meshPipeline",
    "std::unique_ptr<DX12Pipeline> meshShadowPipeline",
    "std::unique_ptr<DX12Pipeline> billboardPipeline",
    "std::unique_ptr<DX12Pipeline> grassCardPipeline",
    "VegetationInstanceBufferState meshInstances",
    "VegetationInstanceBufferState billboardInstances",
    "VegetationInstanceBufferState grassInstances",
    "Scene::VegetationStats stats",
    "void ResetResources()"
)) {
    Require-Contains $stateHeader $required "RendererVegetationState.h missing state member: $required"
}
foreach ($required in @(
    "void Renderer::SetVegetationEnabled",
    "const Scene::VegetationStats& Renderer::GetVegetationStats",
    "Result<void> Renderer::CreateVegetationInstanceBuffer",
    "void Renderer::UpdateVegetationInstances",
    "void Renderer::UpdateBillboardInstances",
    "void Renderer::UpdateGrassInstances",
    "void Renderer::RenderVegetation"
)) {
    Require-Contains $vegetationCpp $required "Renderer_Vegetation.cpp missing function: $required"
}
foreach ($todo in @(
    "TODO: When vegetation mesh pipeline is fully implemented",
    "TODO: When billboard pipeline is fully implemented",
    "TODO: When grass card pipeline is fully implemented",
    "TODO: Implement shadow rendering for vegetation"
)) {
    Require-Contains $vegetationCpp $todo "Vegetation full-runtime TODO marker missing: $todo"
}
Require-Contains $frameContract "struct VegetationInfo" `
    "FrameContract.h missing vegetation contract section"
Require-Contains $frameJson "`"vegetation`", {" `
    "FrameContractJson.cpp does not serialize vegetation contract state"
Require-Contains $snapshot "contract.vegetation.meshPipelineReady" `
    "Renderer_FrameContractSnapshot.cpp does not populate vegetation readiness"
Require-Contains $ownershipAudit "m_vegetationState" `
    "Renderer full ownership audit does not account for m_vegetationState"

$runtimeLogDir = Join-Path $LogDir "temporal_validation"
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "run_temporal_validation_smoke.ps1") `
    -NoBuild `
    -LogDir $runtimeLogDir
if ($LASTEXITCODE -ne 0) {
    Add-Failure "Temporal validation smoke failed while validating vegetation state contract"
}

$reportPath = Join-Path $runtimeLogDir "frame_report_last.json"
if (-not (Test-Path $reportPath)) {
    Add-Failure "Temporal validation frame report missing: $reportPath"
} else {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $vegetation = $report.frame_contract.vegetation
    if ($null -eq $vegetation) {
        Add-Failure "frame contract vegetation section missing at runtime"
    } else {
        if (-not [bool]$vegetation.enabled) {
            Add-Failure "vegetation state should default enabled even though public draw paths are dormant"
        }
        if ([int]$vegetation.total_instances -ne 0 -or
            [int]$vegetation.visible_instances -ne 0 -or
            [int]$vegetation.mesh_instances -ne 0 -or
            [int]$vegetation.billboard_instances -ne 0 -or
            [int]$vegetation.grass_instances -ne 0) {
            Add-Failure "temporal_validation unexpectedly submitted vegetation instances"
        }
        if ([bool]$vegetation.mesh_pipeline_ready -or
            [bool]$vegetation.billboard_pipeline_ready -or
            [bool]$vegetation.grass_pipeline_ready -or
            [bool]$vegetation.shadow_pipeline_ready) {
            Add-Failure "vegetation draw pipelines unexpectedly reported ready in the public validation path"
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Vegetation state contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Vegetation state contract tests passed." -ForegroundColor Green
Write-Host "  state_extraction=verified"
Write-Host "  public_draw_path=dormant"
Write-Host "  logs=$LogDir"
exit 0
