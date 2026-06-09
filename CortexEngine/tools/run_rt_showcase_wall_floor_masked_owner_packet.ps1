param(
    [string]$OutputRoot = "",
    [int]$CaptureStartFrame = 70,
    [int]$CaptureCount = 16,
    [int]$MotionFrames = 100,
    [double]$MotionLookAmplitude = 0.025,
    [double]$MotionLookCycles = 6.0,
    [double]$FixedDeltaTime = 0.008333333,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $OutputRoot = Join-Path $root "build\captures\rt_showcase_wall_floor_masked_owner_packet_$stamp"
}
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $OutputRoot | Out-Null

function Set-EnvOrClear([string]$Name, [string]$Value) {
    if ([string]::IsNullOrWhiteSpace($Value)) {
        Remove-Item "Env:\$Name" -ErrorAction SilentlyContinue
    } else {
        Set-Item "Env:\$Name" $Value
    }
}

function Run-CaptureCase([string]$Name, [string]$DebugView, [bool]$DisableAux) {
    $caseDir = Join-Path $OutputRoot $Name
    New-Item -ItemType Directory -Force -Path $caseDir | Out-Null

    Set-EnvOrClear "CORTEX_DEBUG_VIEW" $DebugView
    if ($DisableAux) {
        Set-Item "Env:\CORTEX_DISABLE_AUX_GEOMETRY" "1"
    } else {
        Remove-Item "Env:\CORTEX_DISABLE_AUX_GEOMETRY" -ErrorAction SilentlyContinue
    }

    $childArgs = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", (Join-Path $PSScriptRoot "run_rt_showcase_wall_floor_flicker_stability_smoke.ps1"),
        "-LogDir", $caseDir,
        "-CaptureStartFrame", [string]$CaptureStartFrame,
        "-CaptureCount", [string]$CaptureCount,
        "-MotionFrames", [string]$MotionFrames,
        "-MotionLookAmplitude", [string]$MotionLookAmplitude,
        "-MotionLookCycles", [string]$MotionLookCycles,
        "-FixedDeltaTime", [string]$FixedDeltaTime,
        "-MaxMeanAbsLumaDelta", "999",
        "-MaxChangedPixelRatio", "1",
        "-MaxLargeChangedPixelRatio", "1"
    )
    if ($NoBuild) {
        $childArgs += "-NoBuild"
    }

    & powershell.exe @childArgs | ForEach-Object { Write-Host $_ }
    $exitCode = $LASTEXITCODE
    $captureCountActual = @(Get-ChildItem -Path $caseDir -Filter "visual_validation_frame_*.bmp" -ErrorAction SilentlyContinue).Count
    [pscustomobject]@{
        name = $Name
        debug_view = $DebugView
        disable_aux = $DisableAux
        exit_code = $exitCode
        capture_count = $captureCountActual
        dir = $caseDir
    }
}

function Run-Analysis([string]$Name, [string]$CaptureDir, [string]$MaskDir, [bool]$InvertMask) {
    $json = Join-Path $CaptureDir "$Name.json"
    $md = Join-Path $CaptureDir "$Name.md"
    $analysisArgs = @(
        (Join-Path $root "tools\analyze_rt_showcase_wall_floor_roi_stability.py"),
        "--capture-dir", $CaptureDir,
        "--mask-dir", $MaskDir,
        "--mask-mode", "not-reference-color",
        "--mask-threshold", "12",
        "--mask-reference-x", "1270",
        "--mask-reference-y", "10",
        "--output-json", $json,
        "--output-md", $md
    )
    if ($InvertMask) {
        $analysisArgs += "--invert-mask"
    }
    & python @analysisArgs | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        throw "analysis failed for $Name"
    }
    [pscustomobject]@{
        name = $Name
        json = $json
        md = $md
        inverted = $InvertMask
    }
}

Push-Location $root
try {
    Copy-Item assets\shaders\Basic.hlsl build\bin\assets\shaders\Basic.hlsl -Force
    Copy-Item assets\shaders\DeferredLighting.hlsl build\bin\assets\shaders\DeferredLighting.hlsl -Force
    Copy-Item assets\shaders\MaterialResolve.hlsl build\bin\assets\shaders\MaterialResolve.hlsl -Force

    Set-Item "Env:\CORTEX_DISABLE_SHADER_CACHE" "1"

    $captures = @()
    $captures += Run-CaptureCase "beauty" "" $false
    $captures += Run-CaptureCase "mask_normal_roughness36" "36" $true
    $captures += Run-CaptureCase "specular9" "9" $true
    $captures += Run-CaptureCase "ownership92" "92" $true

    Remove-Item "Env:\CORTEX_DEBUG_VIEW" -ErrorAction SilentlyContinue
    Remove-Item "Env:\CORTEX_DISABLE_AUX_GEOMETRY" -ErrorAction SilentlyContinue
    Remove-Item "Env:\CORTEX_DISABLE_SHADER_CACHE" -ErrorAction SilentlyContinue

    $beautyDir = Join-Path $OutputRoot "beauty"
    $maskDir = Join-Path $OutputRoot "mask_normal_roughness36"
    $specularDir = Join-Path $OutputRoot "specular9"
    $ownershipDir = Join-Path $OutputRoot "ownership92"

    $analysis = @()
    $analysis += Run-Analysis "beauty_foreground" $beautyDir $maskDir $false
    $analysis += Run-Analysis "beauty_background" $beautyDir $maskDir $true
    $analysis += Run-Analysis "specular_foreground" $specularDir $maskDir $false
    $analysis += Run-Analysis "specular_background" $specularDir $maskDir $true
    $analysis += Run-Analysis "ownership_foreground" $ownershipDir $maskDir $false
    $analysis += Run-Analysis "ownership_background" $ownershipDir $maskDir $true

    $summary = [pscustomobject]@{
        schema = "cortex.rt_showcase.wall_floor_masked_owner_packet.v1"
        output_root = $OutputRoot
        captures = $captures
        analyses = $analysis
    }
    $summaryPath = Join-Path $OutputRoot "masked_owner_packet_summary.json"
    $summary | ConvertTo-Json -Depth 6 | Set-Content -Path $summaryPath -Encoding UTF8

    $summaryMd = Join-Path $OutputRoot "masked_owner_packet_summary.md"
    $lines = @(
        "# RT Showcase Wall/Floor Masked Owner Packet",
        "",
        "Output root: ``$OutputRoot``",
        "",
        "## Captures",
        "",
        "| Case | Debug view | Disable aux | Exit | BMPs |",
        "|---|---:|---:|---:|---:|"
    )
    foreach ($capture in $captures) {
        $lines += "| $($capture.name) | $($capture.debug_view) | $($capture.disable_aux) | $($capture.exit_code) | $($capture.capture_count) |"
    }
    $lines += ""
    $lines += "## Analyses"
    $lines += ""
    foreach ($item in $analysis) {
        $lines += "- $($item.name): ``$($item.md)``"
    }
    $lines | Set-Content -Path $summaryMd -Encoding UTF8

    Write-Host "RT Showcase masked owner packet complete"
    Write-Host " summary=$summaryPath"
    Write-Host " report=$summaryMd"
} finally {
    Pop-Location
    Remove-Item "Env:\CORTEX_DEBUG_VIEW" -ErrorAction SilentlyContinue
    Remove-Item "Env:\CORTEX_DISABLE_AUX_GEOMETRY" -ErrorAction SilentlyContinue
    Remove-Item "Env:\CORTEX_DISABLE_SHADER_CACHE" -ErrorAction SilentlyContinue
}
