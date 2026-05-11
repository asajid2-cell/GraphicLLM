param(
    [string]$LogDir = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$baseLogDir = Join-Path $root "build/bin/logs"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "conductor_energy_{0}_{1}_{2}" -f `
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

function Require-NoRegex([string]$Text, [string]$Pattern, [string]$Message) {
    if ($Text -match $Pattern) {
        Add-Failure $Message
    }
}

if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Release rebuild failed before conductor energy contract tests"
    }
}

$basicPath = Join-Path $root "assets/shaders/Basic.hlsl"
$deferredPath = Join-Path $root "assets/shaders/DeferredLighting.hlsl"
$pbrPath = Join-Path $root "assets/shaders/PBR_Lighting.hlsli"
$materialResolvePath = Join-Path $root "assets/shaders/MaterialResolve.hlsl"

$basic = Get-Content $basicPath -Raw
$deferred = Get-Content $deferredPath -Raw
$pbr = Get-Content $pbrPath -Raw
$materialResolve = Get-Content $materialResolvePath -Raw

Require-Contains $basic "// Preserve the authored conductor value." `
    "Basic.hlsl no longer documents the forward conductor preservation contract"
Require-Contains $basic "metallic = saturate(metallic);" `
    "Basic.hlsl does not preserve conductor metallic via saturate(metallic)"
Require-Contains $basic "float3 F0 = lerp(dielectricF0, albedo, metallic);" `
    "Basic.hlsl does not derive conductor F0 from authored albedo/metallic"
if ([regex]::Matches($basic, [regex]::Escape("float3 kd = (1.0f - F) * (1.0f - metallic);")).Count -lt 2) {
    Add-Failure "Basic.hlsl must use the conductor energy split in both forward and clustered lighting paths"
}
Require-NoRegex $basic "metallic\s*=\s*(min|clamp)\s*\([^;]*(0\.25|0\.5|0\.6|0\.8|0\.9)" `
    "Basic.hlsl appears to reintroduce a forced metallic clamp"

Require-Contains $deferred "float3 F0 = lerp(dielectricF0, albedoColor, metallic);" `
    "DeferredLighting.hlsl does not derive conductor F0 from authored albedo/metallic"
Require-Contains $deferred "float3 kD = (1.0 - kS) * (1.0 - metallic);" `
    "DeferredLighting.hlsl sun path does not use conductor diffuse energy split"
Require-Contains $deferred "float3 kD_l = (1.0f - kS_l) * (1.0f - metallic);" `
    "DeferredLighting.hlsl local-light path does not use conductor diffuse energy split"
Require-Contains $deferred "float3 kD_ibl = (1.0 - metallic) * (1.0 - Fibl);" `
    "DeferredLighting.hlsl IBL path does not use conductor diffuse energy split"

Require-Contains $pbr "float3 kD = (1.0f - kS) * (1.0f - metallic);" `
    "Shared PBR helper no longer uses metallic-aware diffuse split"
Require-Contains $materialResolve "if (metallic > 0.9f && baseRoughness <= 0.06f)" `
    "MaterialResolve.hlsl no longer preserves authored mirror-class conductor roughness"

$materialLabLogDir = Join-Path $LogDir "material_lab"
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot "run_material_lab_smoke.ps1") `
    -NoBuild `
    -LogDir $materialLabLogDir
if ($LASTEXITCODE -ne 0) {
    Add-Failure "Material Lab smoke failed while validating conductor energy contract"
}

$reportPath = Join-Path $materialLabLogDir "frame_report_last.json"
if (-not (Test-Path $reportPath)) {
    Add-Failure "Material Lab frame report missing: $reportPath"
} else {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $materials = $report.frame_contract.materials
    $stats = $report.visual_validation.image_stats

    if ([string]$report.scene -ne "material_lab") {
        Add-Failure "expected material_lab report, got '$($report.scene)'"
    }
    if ([int]$materials.resolved_conductor -lt 4 -or [int]$materials.reflection_conductor -lt 3) {
        Add-Failure "Material Lab conductor coverage too low: resolved=$($materials.resolved_conductor) reflection=$($materials.reflection_conductor)"
    }
    if ([double]$materials.max_metallic -lt 0.99) {
        Add-Failure "Material Lab did not preserve at least one full conductor; max_metallic=$($materials.max_metallic)"
    }
    if ([int]$materials.metallic_out_of_range -ne 0 -or [int]$materials.validation_errors -ne 0) {
        Add-Failure "Material Lab reported invalid metallic/material values"
    }
    if ([int]$materials.very_bright_albedo -ne 0) {
        Add-Failure "Material Lab reported very bright albedo conductors/materials"
    }
    if ([double]$materials.max_reflection_ceiling_estimate -gt 1.0) {
        Add-Failure "Material Lab reflection ceiling estimate exceeds 1.0: $($materials.max_reflection_ceiling_estimate)"
    }
    if (-not [bool]$stats.valid) {
        Add-Failure "Material Lab visual stats invalid"
    } else {
        if ([double]$stats.saturated_ratio -gt 0.12) {
            Add-Failure "Material Lab saturated ratio too high for conductor energy gate: $($stats.saturated_ratio)"
        }
        if ([double]$stats.near_white_ratio -gt 0.05) {
            Add-Failure "Material Lab near-white ratio too high for conductor energy gate: $($stats.near_white_ratio)"
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Conductor energy contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Conductor energy contract tests passed." -ForegroundColor Green
Write-Host "  forward_and_deferred_energy_split=present"
Write-Host "  conductor_material_lab=passed"
Write-Host "  logs=$LogDir"
exit 0
