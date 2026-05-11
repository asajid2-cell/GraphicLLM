param(
    [string]$LogDir = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$smokeScript = Join-Path $PSScriptRoot "run_rt_showcase_smoke.ps1"
$activeLogDir = if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "vb_debug_views_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    Join-Path (Join-Path $root "build/bin/logs/runs") $runId
} else {
    $LogDir
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null

if (-not $NoBuild) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
}

$cases = @(
    [pscustomobject]@{
        Name = "vb_depth"
        View = 34
        MinColorful = 0.0
        MinNonBlack = 0.80
    },
    [pscustomobject]@{
        Name = "vb_gbuffer_albedo"
        View = 35
        MinColorful = 0.20
        MinNonBlack = 0.80
    }
)

$failures = New-Object System.Collections.Generic.List[string]
$summaries = New-Object System.Collections.Generic.List[object]

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

foreach ($case in $cases) {
    $caseLogDir = Join-Path $activeLogDir $case.Name
    New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null

    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $smokeScript,
        "-NoBuild",
        "-LogDir", $caseLogDir,
        "-SurfaceDebugView", [string]$case.View,
        "-MinSurfaceDebugColorfulRatio", [string]$case.MinColorful,
        "-MinSurfaceDebugNonBlackRatio", [string]$case.MinNonBlack,
        "-TemporalRuns", "1"
    )
    $output = & powershell @args 2>&1
    $exitCode = $LASTEXITCODE
    $outputText = ($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
    Set-Content -Path (Join-Path $caseLogDir "smoke_stdout.txt") -Value $outputText -Encoding UTF8

    if ($exitCode -ne 0) {
        Add-Failure "$($case.Name) debug view $($case.View) failed with exit code $exitCode. logs=$caseLogDir`n$outputText"
        continue
    }

    $debugReportPath = Join-Path $caseLogDir "frame_report_surface_debug.json"
    if (-not (Test-Path $debugReportPath)) {
        Add-Failure "$($case.Name) did not write surface debug report: $debugReportPath"
        continue
    }

    $report = Get-Content $debugReportPath -Raw | ConvertFrom-Json
    $view = [int]$report.renderer.debug_view_mode
    $nonBlack = [double]$report.visual_validation.image_stats.nonblack_ratio
    $colorful = [double]$report.visual_validation.image_stats.colorful_ratio

    if ($view -ne [int]$case.View) {
        Add-Failure "$($case.Name) captured debug view $view, expected $($case.View)"
    }
    if ($nonBlack -lt [double]$case.MinNonBlack) {
        Add-Failure "$($case.Name) nonblack ratio $nonBlack below $($case.MinNonBlack)"
    }
    if ($colorful -lt [double]$case.MinColorful) {
        Add-Failure "$($case.Name) colorful ratio $colorful below $($case.MinColorful)"
    }

    $summaries.Add([pscustomobject]@{
        name = $case.Name
        debug_view = $view
        nonblack_ratio = $nonBlack
        colorful_ratio = $colorful
        avg_luma = [double]$report.visual_validation.image_stats.avg_luma
        log_dir = $caseLogDir
    })
}

$summaryPath = Join-Path $activeLogDir "vb_debug_views_summary.json"
$summaries | ConvertTo-Json -Depth 4 | Set-Content -Path $summaryPath -Encoding UTF8

if ($failures.Count -gt 0) {
    Write-Host "VB debug view validation failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$activeLogDir"
    exit 1
}

Write-Host "VB debug view validation passed." -ForegroundColor Green
foreach ($row in $summaries) {
    Write-Host ("  {0}: view={1} nonblack={2:N3} colorful={3:N3} luma={4:N2}" -f `
        $row.name, $row.debug_view, $row.nonblack_ratio, $row.colorful_ratio, $row.avg_luma)
}
Write-Host "  logs=$activeLogDir"
