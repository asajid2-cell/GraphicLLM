param(
    [switch]$NoBuild,
    [ValidateSet("High", "Release")]
    [string]$Quality = "High",
    [string]$OutputDir = "",
    [int]$Width = 1920,
    [int]$Height = 1080,
    [int]$SmokeFrames = 220
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $root "docs/media"
}
$OutputDir = (New-Item -ItemType Directory -Force -Path $OutputDir).FullName

if (-not $NoBuild) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Release rebuild failed before public capture gallery"
    }
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
}

Add-Type -AssemblyName System.Drawing

$preset = if ($Quality -eq "High") { "public_high" } else { "release_showcase" }
$cases = @(
    @{ id = "rt_showcase"; title = "RT Showcase"; scene = "rt_showcase"; bookmark = "hero"; environment = "studio"; frames = 260; image = "rt_showcase_hero.png" },
    @{ id = "material_lab"; title = "Material Lab"; scene = "material_lab"; bookmark = "hero"; environment = "cool_overcast"; frames = 180; image = "material_lab_hero.png" },
    @{ id = "glass_water_courtyard"; title = "Glass and Water Courtyard"; scene = "glass_water_courtyard"; bookmark = "hero"; environment = "sunset_courtyard"; frames = 180; image = "glass_water_courtyard_hero.png" },
    @{ id = "effects_showcase"; title = "Effects Showcase"; scene = "effects_showcase"; bookmark = "hero"; environment = "night_city"; frames = 220; image = "effects_showcase_hero.png" },
    @{ id = "outdoor_sunset_beach"; title = "Outdoor Sunset Beach"; scene = "outdoor_sunset_beach"; bookmark = "hero"; environment = "sunset_courtyard"; frames = 180; image = "outdoor_sunset_beach_hero.png" },
    @{ id = "ibl_gallery"; title = "IBL Gallery"; scene = "ibl_gallery"; bookmark = "environment_sweep"; environment = "warm_gallery"; frames = 180; image = "ibl_gallery_sweep.png" }
)

$runId = "public_capture_gallery_{0}_{1}_{2}" -f `
    (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
    $PID,
    ([Guid]::NewGuid().ToString("N").Substring(0, 8))
$runRoot = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
New-Item -ItemType Directory -Force -Path $runRoot | Out-Null

function Get-ReportPath([string]$CaseLogDir) {
    foreach ($name in @("frame_report_last.json", "frame_report_shutdown.json")) {
        $candidate = Join-Path $CaseLogDir $name
        if (Test-Path $candidate) {
            return $candidate
        }
    }
    return ""
}

function Convert-BmpToPng([string]$BmpPath, [string]$PngPath) {
    $image = [System.Drawing.Image]::FromFile($BmpPath)
    try {
        $image.Save($PngPath, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $image.Dispose()
    }
}

$entries = New-Object System.Collections.Generic.List[object]
$failures = New-Object System.Collections.Generic.List[string]
$commit = (& git -C (Join-Path $root "..") rev-parse --short HEAD 2>$null)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($commit)) {
    $commit = "unknown"
}

foreach ($case in $cases) {
    $caseLogDir = Join-Path $runRoot $case.id
    New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null

    $oldLogDir = $env:CORTEX_LOG_DIR
    $oldCapture = $env:CORTEX_CAPTURE_VISUAL_VALIDATION
    $oldDebugLayer = $env:CORTEX_DISABLE_DEBUG_LAYER
    $oldMinFrame = $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME
    try {
        $env:CORTEX_LOG_DIR = $caseLogDir
        $env:CORTEX_CAPTURE_VISUAL_VALIDATION = "1"
        $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
        $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = "30"

        $frames = [Math]::Max($SmokeFrames, [int]$case.frames)
        Push-Location (Split-Path -Parent $exe)
        try {
            $output = & $exe `
                "--scene" ([string]$case.scene) `
                "--camera-bookmark" ([string]$case.bookmark) `
                "--environment" ([string]$case.environment) `
                "--graphics-preset" $preset `
                "--window-width" ([string]$Width) `
                "--window-height" ([string]$Height) `
                "--mode=default" `
                "--no-llm" `
                "--no-dreamer" `
                "--no-launcher" `
                "--smoke-frames=$frames" `
                "--exit-after-visual-validation" 2>&1
            $exitCode = $LASTEXITCODE
            $output | Set-Content -Encoding UTF8 (Join-Path $caseLogDir "engine_stdout.txt")
        } finally {
            Pop-Location
        }
    } finally {
        if ($null -eq $oldLogDir) { Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue } else { $env:CORTEX_LOG_DIR = $oldLogDir }
        if ($null -eq $oldCapture) { Remove-Item Env:\CORTEX_CAPTURE_VISUAL_VALIDATION -ErrorAction SilentlyContinue } else { $env:CORTEX_CAPTURE_VISUAL_VALIDATION = $oldCapture }
        if ($null -eq $oldDebugLayer) { Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue } else { $env:CORTEX_DISABLE_DEBUG_LAYER = $oldDebugLayer }
        if ($null -eq $oldMinFrame) { Remove-Item Env:\CORTEX_VISUAL_VALIDATION_MIN_FRAME -ErrorAction SilentlyContinue } else { $env:CORTEX_VISUAL_VALIDATION_MIN_FRAME = $oldMinFrame }
    }

    if ($exitCode -ne 0) {
        $failures.Add("$($case.id) exited with code $exitCode logs=$caseLogDir") | Out-Null
        continue
    }

    $reportPath = Get-ReportPath $caseLogDir
    if ([string]::IsNullOrWhiteSpace($reportPath)) {
        $failures.Add("$($case.id) did not write a frame report logs=$caseLogDir") | Out-Null
        continue
    }

    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $bmpPath = Join-Path $caseLogDir "visual_validation_rt_showcase.bmp"
    if (-not (Test-Path $bmpPath)) {
        $failures.Add("$($case.id) did not write visual_validation_rt_showcase.bmp logs=$caseLogDir") | Out-Null
        continue
    }

    $pngPath = Join-Path $OutputDir ([string]$case.image)
    Convert-BmpToPng $bmpPath $pngPath

    $stats = $report.visual_validation.image_stats
    if (-not [bool]$stats.valid) {
        $failures.Add("$($case.id) visual stats invalid: $($stats.reason)") | Out-Null
    }

    $rt = $report.frame_contract.ray_tracing
    $entry = [ordered]@{
        id = [string]$case.id
        title = [string]$case.title
        image = ("docs/media/{0}" -f [string]$case.image)
        scene = [string]$case.scene
        camera_bookmark = [string]$case.bookmark
        environment = [string]$case.environment
        graphics_preset = $preset
        quality = $Quality
        window_width = [int]$report.window.width
        window_height = [int]$report.window.height
        capture_width = [int]$stats.width
        capture_height = [int]$stats.height
        render_scale = [double]$report.frame_contract.graphics_preset.render_scale
        gpu_frame_ms = [double]$report.gpu_frame_ms
        avg_luma = [double]$stats.avg_luma
        center_avg_luma = [double]$stats.center_avg_luma
        nonblack_ratio = [double]$stats.nonblack_ratio
        saturated_ratio = [double]$stats.saturated_ratio
        near_white_ratio = [double]$stats.near_white_ratio
        rt_reflection_signal_avg_luma = if ($rt) { [double]$rt.reflection_signal_avg_luma } else { 0.0 }
        rt_reflection_history_avg_luma = if ($rt) { [double]$rt.reflection_history_signal_avg_luma } else { 0.0 }
        report = $reportPath
        source_bmp = $bmpPath
    }
    $entries.Add([pscustomobject]$entry) | Out-Null
}

$manifest = [ordered]@{
    schema = 1
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    commit = $commit.Trim()
    quality = $Quality
    graphics_preset = $preset
    requested_width = $Width
    requested_height = $Height
    run_log_dir = $runRoot
    entries = @($entries.ToArray())
    failures = @($failures.ToArray())
}
$manifestPath = Join-Path $OutputDir "gallery_manifest.json"
$manifestJson = ($manifest | ConvertTo-Json -Depth 8) -replace "`r`n", "`n"
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($manifestPath, $manifestJson + "`n", $utf8NoBom)

if ($failures.Count -gt 0) {
    Write-Host "Public capture gallery failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "manifest=$manifestPath" -ForegroundColor Red
    exit 1
}

Write-Host "Public capture gallery passed" -ForegroundColor Green
Write-Host " manifest=$manifestPath"
Write-Host " logs=$runRoot"
Write-Host " captures=$($entries.Count) size=${Width}x${Height} preset=$preset"
