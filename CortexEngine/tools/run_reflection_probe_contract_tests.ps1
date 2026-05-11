param(
    [int]$SmokeFrames = 90,
    [string]$LogDir = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$exeWorkingDir = Split-Path -Parent $exe
$baseLogDir = Join-Path $root "build/bin/logs"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "reflection_probe_contract_{0}_{1}_{2}" -f `
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
        throw "Release rebuild failed before reflection probe contract tests"
    }
}

$sceneSource = Get-Content (Join-Path $root "src/Core/Engine_Scenes.cpp") -Raw
$deferredSource = Get-Content (Join-Path $root "src/Graphics/Renderer_VisibilityBufferDeferredLighting.cpp") -Raw
$shaderSource = Get-Content (Join-Path $root "assets/shaders/DeferredLighting.hlsl") -Raw
$frameContract = Get-Content (Join-Path $root "src/Graphics/FrameContract.h") -Raw
$frameJson = Get-Content (Join-Path $root "src/Graphics/FrameContractJson.cpp") -Raw

Require-Contains $sceneSource "RTGallery_LocalProbe_Left" `
    "RT Showcase does not declare the left local reflection probe"
Require-Contains $sceneSource "RTGallery_LocalProbe_Right" `
    "RT Showcase does not declare the right local reflection probe"
Require-Contains $deferredSource "View<Scene::ReflectionProbeComponent, Scene::TransformComponent>" `
    "VB deferred lighting does not collect ReflectionProbeComponent"
Require-Contains $deferredSource "UpdateReflectionProbes" `
    "VB deferred lighting does not upload local reflection probes"
Require-Contains $deferredSource "reflectionProbeParams = glm::uvec4" `
    "VB deferred lighting does not bind reflection probe params"
Require-Contains $shaderSource "ComputeProbeWeight" `
    "Deferred lighting shader is missing probe blend weighting"
Require-Contains $shaderSource "BoxProjectReflection" `
    "Deferred lighting shader is missing probe box projection"
Require-Contains $shaderSource "g_ReflectionProbeParams.z == 42u" `
    "Deferred lighting shader is missing local reflection-probe debug view 42"
Require-Contains $frameContract "localReflectionProbeCount" `
    "FrameContract.h does not expose local reflection probe counts"
Require-Contains $frameJson "local_reflection_probe_count" `
    "FrameContractJson.cpp does not serialize local reflection probe counts"

$previousLogDir = $env:CORTEX_LOG_DIR
$previousCapture = $env:CORTEX_CAPTURE_VISUAL_VALIDATION
$previousMinFrame = $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME
$previousDebug = $env:CORTEX_DEBUG_VIEW
$previousDisableDebug = $env:CORTEX_DISABLE_DEBUG_LAYER
try {
    $env:CORTEX_LOG_DIR = $LogDir
    $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
    $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "20"
    $env:CORTEX_DEBUG_VIEW = "42"
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"

    Push-Location $exeWorkingDir
    try {
        $output = & $exe "--scene" "rt_showcase" "--camera-bookmark" "hero" "--mode=default" "--no-llm" "--no-dreamer" "--no-launcher" "--smoke-frames=$SmokeFrames" "--exit-after-visual-validation" 2>&1
    } finally {
        Pop-Location
    }
    $output | Set-Content -Path (Join-Path $LogDir "reflection_probe_contract_stdout.log") -Encoding UTF8
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "RT Showcase reflection probe debug run failed with exit code $LASTEXITCODE"
    }
} finally {
    if ($null -eq $previousLogDir) { Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue } else { $env:CORTEX_LOG_DIR = $previousLogDir }
    if ($null -eq $previousCapture) { Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue } else { $env:CORTEX_CAPTURE_VISUAL_VALIDATION = $previousCapture }
    if ($null -eq $previousMinFrame) { Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue } else { $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = $previousMinFrame }
    if ($null -eq $previousDebug) { Remove-Item Env:\CORTEX_DEBUG_VIEW -ErrorAction SilentlyContinue } else { $env:CORTEX_DEBUG_VIEW = $previousDebug }
    if ($null -eq $previousDisableDebug) { Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue } else { $env:CORTEX_DISABLE_DEBUG_LAYER = $previousDisableDebug }
}

$reportPath = Join-Path $LogDir "frame_report_last.json"
if (-not (Test-Path $reportPath)) {
    Add-Failure "frame report missing: $reportPath"
} else {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    if ([int]$report.renderer.debug_view_mode -ne 42) {
        Add-Failure "debug view was $($report.renderer.debug_view_mode), expected 42"
    }

    $envContract = $report.frame_contract.environment
    if ($null -eq $envContract) {
        Add-Failure "frame contract environment section missing"
    } else {
        if ([int]$envContract.local_reflection_probe_count -lt 2) {
            Add-Failure "local reflection probe count is $($envContract.local_reflection_probe_count), expected >= 2"
        }
        if ([int]$envContract.local_reflection_probe_skipped -ne 0) {
            Add-Failure "local reflection probes skipped: $($envContract.local_reflection_probe_skipped)"
        }
        if (-not [bool]$envContract.local_reflection_probe_table_valid) {
            Add-Failure "local reflection probe table was not valid"
        }
    }

    $stats = $report.visual_validation.image_stats
    if (-not [bool]$report.visual_validation.captured -or -not [bool]$stats.valid) {
        Add-Failure "probe debug visual capture missing or invalid"
    } else {
        if ([double]$stats.nonblack_ratio -lt 0.15) {
            Add-Failure "probe debug nonblack ratio is $($stats.nonblack_ratio), expected >= 0.15"
        }
        if ([double]$stats.avg_luma -lt 5.0) {
            Add-Failure "probe debug average luma is $($stats.avg_luma), expected >= 5.0"
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Reflection probe contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Reflection probe contract tests passed." -ForegroundColor Green
Write-Host "  probes=runtime"
Write-Host "  debug_view=42"
Write-Host "  logs=$LogDir"
exit 0
