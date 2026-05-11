param(
    [string]$BaselinePath = "",
    [int]$MinProbeEdgePixels = 2500,
    [double]$MinProbeEdgeRatio = 0.004,
    [double]$MaxPureDominantRatio = 0.12,
    [double]$MaxAverageChannelShare = 0.62,
    [string]$LogDir = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($BaselinePath)) {
    $BaselinePath = Join-Path $root "assets/config/visual_baselines.json"
}
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "visual_probe_validation_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Get-BmpProbeStats([string]$Path) {
    if (-not (Test-Path $Path)) {
        return [pscustomobject]@{ valid = $false; reason = "missing" }
    }

    [byte[]]$bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 54 -or $bytes[0] -ne 0x42 -or $bytes[1] -ne 0x4d) {
        return [pscustomobject]@{ valid = $false; reason = "not_bmp" }
    }

    $dataOffset = [BitConverter]::ToUInt32($bytes, 10)
    $width = [BitConverter]::ToInt32($bytes, 18)
    $height = [Math]::Abs([BitConverter]::ToInt32($bytes, 22))
    $bpp = [BitConverter]::ToUInt16($bytes, 28)
    if ($width -le 0 -or $height -le 0 -or ($bpp -ne 24 -and $bpp -ne 32)) {
        return [pscustomobject]@{ valid = $false; reason = "unsupported_format" }
    }

    $bytesPerPixel = [int]($bpp / 8)
    $rowStride = [int]([Math]::Floor(((($width * $bytesPerPixel) + 3) / 4.0))) * 4
    $requiredSize = [int64]$dataOffset + ([int64]$rowStride * [int64]$height)
    if ($requiredSize -gt $bytes.Length) {
        return [pscustomobject]@{ valid = $false; reason = "truncated" }
    }

    $pixelCount = [int64]$width * [int64]$height
    $edgePixels = 0
    $pureDominantPixels = 0
    $sumR = 0.0
    $sumG = 0.0
    $sumB = 0.0

    for ($y = 0; $y -lt $height; ++$y) {
        $row = [int]$dataOffset + ($y * $rowStride)
        for ($x = 0; $x -lt $width; ++$x) {
            $p = $row + ($x * $bytesPerPixel)
            $b = [double]$bytes[$p]
            $g = [double]$bytes[$p + 1]
            $r = [double]$bytes[$p + 2]
            $sumR += $r
            $sumG += $g
            $sumB += $b

            $maxChannel = [Math]::Max($r, [Math]::Max($g, $b))
            $minChannel = [Math]::Min($r, [Math]::Min($g, $b))
            if ($maxChannel -gt 24.0 -and (($maxChannel - $minChannel) / $maxChannel) -gt 0.85) {
                ++$pureDominantPixels
            }

            if ($x -gt 0) {
                $q = $p - $bytesPerPixel
                $luma = (0.2126 * $r) + (0.7152 * $g) + (0.0722 * $b)
                $prev = (0.2126 * [double]$bytes[$q + 2]) +
                        (0.7152 * [double]$bytes[$q + 1]) +
                        (0.0722 * [double]$bytes[$q])
                if ([Math]::Abs($luma - $prev) -gt 20.0) {
                    ++$edgePixels
                }
            }
        }
    }

    $sumAll = [Math]::Max(1.0, $sumR + $sumG + $sumB)
    $avgR = $sumR / [double]$pixelCount
    $avgG = $sumG / [double]$pixelCount
    $avgB = $sumB / [double]$pixelCount
    $maxAverageChannelShareActual = [Math]::Max($sumR, [Math]::Max($sumG, $sumB)) / $sumAll

    return [pscustomobject]@{
        valid = $true
        reason = ""
        width = $width
        height = $height
        pixel_count = $pixelCount
        edge_pixels = $edgePixels
        edge_ratio = [double]$edgePixels / [double]$pixelCount
        pure_dominant_ratio = [double]$pureDominantPixels / [double]$pixelCount
        avg_r = $avgR
        avg_g = $avgG
        avg_b = $avgB
        max_average_channel_share = $maxAverageChannelShareActual
    }
}

if (-not (Test-Path $BaselinePath)) {
    throw "Visual baseline manifest not found: $BaselinePath"
}
$baselineDoc = Get-Content $BaselinePath -Raw | ConvertFrom-Json
$caseCount = @($baselineDoc.cases).Count
if ($caseCount -lt 1) {
    throw "Visual baseline manifest has no cases: $BaselinePath"
}

$contractArgs = @(
    "-NoProfile",
    "-ExecutionPolicy", "Bypass",
    "-File", (Join-Path $PSScriptRoot "run_visual_baseline_contract_tests.ps1"),
    "-RuntimeSmoke",
    "-MaxRuntimeCases", [string]$caseCount,
    "-LogDir", $LogDir
)
if ($NoBuild) {
    $contractArgs += "-NoBuild"
}

$contractOutput = & powershell @contractArgs 2>&1
$contractExit = $LASTEXITCODE
$contractText = ($contractOutput | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
Set-Content -Path (Join-Path $LogDir "visual_baseline_contract_stdout.txt") -Value $contractText -Encoding UTF8
if (-not [string]::IsNullOrWhiteSpace($contractText)) {
    $contractText -split "`r?`n" | ForEach-Object { Write-Host $_ }
}
if ($contractExit -ne 0) {
    Write-Host "Visual probe validation failed: visual baseline runtime contract failed." -ForegroundColor Red
    Write-Host "logs=$LogDir"
    exit $contractExit
}

$failures = New-Object System.Collections.Generic.List[string]
$caseSummaries = New-Object System.Collections.Generic.List[object]

foreach ($case in $baselineDoc.cases) {
    $caseId = [string]$case.id
    $caseLogDir = Join-Path $LogDir $caseId
    $reportPath = Join-Path $caseLogDir "frame_report_last.json"
    $capturePath = Join-Path $caseLogDir ([string]$case.capture.filename)

    if (-not (Test-Path $reportPath)) {
        Add-Failure "$caseId missing frame report: $reportPath"
        continue
    }
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $probe = Get-BmpProbeStats $capturePath
    if (-not [bool]$probe.valid) {
        Add-Failure "$caseId invalid visual capture: $($probe.reason)"
        continue
    }
    if ([int64]$probe.edge_pixels -lt $MinProbeEdgePixels) {
        Add-Failure "$caseId edge pixels $($probe.edge_pixels) below minimum $MinProbeEdgePixels"
    }
    if ([double]$probe.edge_ratio -lt $MinProbeEdgeRatio) {
        Add-Failure "$caseId edge ratio $($probe.edge_ratio) below minimum $MinProbeEdgeRatio"
    }
    if ([double]$probe.pure_dominant_ratio -gt $MaxPureDominantRatio) {
        Add-Failure "$caseId pure dominant color ratio $($probe.pure_dominant_ratio) above maximum $MaxPureDominantRatio"
    }
    if ([double]$probe.max_average_channel_share -gt $MaxAverageChannelShare) {
        Add-Failure "$caseId average channel share $($probe.max_average_channel_share) above maximum $MaxAverageChannelShare"
    }
    if (-not [bool]$report.visual_validation.captured -or
        -not [bool]$report.visual_validation.image_stats.valid) {
        Add-Failure "$caseId frame report says visual validation capture is invalid"
    }

    $caseSummaries.Add([pscustomobject]@{
        id = $caseId
        scene = [string]$report.scene
        gpu_frame_ms = [double]$report.gpu_frame_ms
        avg_luma = [double]$report.visual_validation.image_stats.avg_luma
        center_avg_luma = [double]$report.visual_validation.image_stats.center_avg_luma
        nonblack_ratio = [double]$report.visual_validation.image_stats.nonblack_ratio
        saturated_ratio = [double]$report.visual_validation.image_stats.saturated_ratio
        near_white_ratio = [double]$report.visual_validation.image_stats.near_white_ratio
        dark_detail_ratio = [double]$report.visual_validation.image_stats.dark_detail_ratio
        edge_pixels = [int64]$probe.edge_pixels
        edge_ratio = [double]$probe.edge_ratio
        pure_dominant_ratio = [double]$probe.pure_dominant_ratio
        max_average_channel_share = [double]$probe.max_average_channel_share
        capture = $capturePath
    })
}

$summary = [pscustomobject]@{
    cases = $caseSummaries
    thresholds = [pscustomobject]@{
        min_probe_edge_pixels = $MinProbeEdgePixels
        min_probe_edge_ratio = $MinProbeEdgeRatio
        max_pure_dominant_ratio = $MaxPureDominantRatio
        max_average_channel_share = $MaxAverageChannelShare
    }
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -Path (Join-Path $LogDir "visual_probe_summary.json") -Encoding UTF8

if ($failures.Count -gt 0) {
    Write-Host "Visual probe validation failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Visual probe validation passed." -ForegroundColor Green
Write-Host ("  cases={0} min_edge_pixels={1} min_edge_ratio={2:N4} max_pure_dominant={3:N3}" -f `
    $caseSummaries.Count, $MinProbeEdgePixels, $MinProbeEdgeRatio, $MaxPureDominantRatio)
foreach ($row in $caseSummaries) {
    Write-Host ("  {0}: scene={1} gpu_ms={2:N3} luma={3:N2} edge={4}/{5:N4} dominant={6:N4}" -f `
        $row.id, $row.scene, $row.gpu_frame_ms, $row.avg_luma, $row.edge_pixels, $row.edge_ratio, $row.pure_dominant_ratio)
}
Write-Host "  logs=$LogDir"
