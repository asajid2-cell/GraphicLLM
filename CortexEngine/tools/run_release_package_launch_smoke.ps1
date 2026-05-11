param(
    [switch]$NoBuild,
    [string]$LogDir = "",
    [int]$SmokeFrames = 45
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$binDir = Join-Path $root "build/bin"
$manifestPath = Join-Path $root "assets/config/release_package_manifest.json"

function Normalize-Rel([string]$Path) {
    return ($Path -replace "\\", "/").TrimStart("./")
}

function Get-RelativePath([string]$Base, [string]$Path) {
    $baseUri = [Uri]((Resolve-Path -LiteralPath $Base).Path.TrimEnd('\') + '\')
    $pathUri = [Uri]((Resolve-Path -LiteralPath $Path).Path)
    return Normalize-Rel ([Uri]::UnescapeDataString($baseUri.MakeRelativeUri($pathUri).ToString()))
}

function Resolve-GlobFiles([string]$Base, [string]$Pattern) {
    $normalized = Normalize-Rel $Pattern
    $literalPrefix = $normalized
    $wildcardIndex = $normalized.IndexOfAny([char[]]"*?[")
    if ($wildcardIndex -ge 0) {
        $slashIndex = $normalized.LastIndexOf("/", $wildcardIndex)
        if ($slashIndex -ge 0) {
            $literalPrefix = $normalized.Substring(0, $slashIndex)
        } else {
            $literalPrefix = ""
        }
    }

    $searchRoot = if ([string]::IsNullOrWhiteSpace($literalPrefix)) {
        $Base
    } else {
        Join-Path $Base ($literalPrefix -replace "/", "\")
    }
    if (-not (Test-Path $searchRoot)) {
        return @()
    }

    return @(Get-ChildItem -Path $searchRoot -File -Recurse | Where-Object {
        (Get-RelativePath $Base $_.FullName) -like $normalized
    })
}

function Add-SelectedFile(
    [System.Collections.Generic.Dictionary[string,System.IO.FileInfo]]$Selected,
    [string]$Base,
    [string]$Path
) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        throw "Package launch smoke source file missing: $Path"
    }
    $rel = Get-RelativePath $Base $Path
    if (-not $Selected.ContainsKey($rel)) {
        $Selected.Add($rel, (Get-Item -LiteralPath $Path))
    }
}

if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Release rebuild failed before package launch smoke"
    }
}

if (-not (Test-Path $manifestPath)) {
    throw "Release package manifest missing: $manifestPath"
}
if (-not (Test-Path $binDir)) {
    throw "Runtime output directory missing: $binDir"
}

if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss_fff"
    $LogDir = Join-Path $binDir "logs/runs/release_package_launch_$stamp"
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$selected = New-Object 'System.Collections.Generic.Dictionary[string,System.IO.FileInfo]'
foreach ($pattern in $manifest.runtime_include_globs) {
    foreach ($match in (Resolve-GlobFiles $binDir ([string]$pattern))) {
        Add-SelectedFile $selected $binDir $match.FullName
    }
}
foreach ($file in $manifest.required_runtime_files) {
    Add-SelectedFile $selected $binDir (Join-Path $binDir ([string]$file))
}

$stageRoot = Join-Path $LogDir "package_stage"
New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null
foreach ($entry in $selected.GetEnumerator()) {
    $dest = Join-Path $stageRoot ($entry.Key -replace "/", "\")
    $destDir = Split-Path -Parent $dest
    if (-not (Test-Path $destDir)) {
        New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    }
    Copy-Item -LiteralPath $entry.Value.FullName -Destination $dest -Force
}

$exe = Join-Path $stageRoot "CortexEngine.exe"
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
    throw "Staged package is missing CortexEngine.exe"
}

$stdoutPath = Join-Path $LogDir "engine_stdout.txt"
$exitCode = 0
Push-Location $stageRoot
try {
    $output = & $exe "--scene" "temporal_validation" "--graphics-preset" "safe_startup" "--environment" "studio" "--mode=default" "--no-llm" "--no-dreamer" "--no-launcher" "--smoke-frames=$SmokeFrames" "--exit-after-visual-validation" 2>&1
    $exitCode = $LASTEXITCODE
    $output | Set-Content -Encoding UTF8 $stdoutPath
} finally {
    Pop-Location
}

$reportPath = ""
foreach ($reportName in @("frame_report_last.json", "frame_report_shutdown.json")) {
    $candidatePath = Join-Path $stageRoot "logs/$reportName"
    if (Test-Path -LiteralPath $candidatePath -PathType Leaf) {
        $reportPath = $candidatePath
        break
    }
    $candidate = Get-ChildItem -Path (Join-Path $stageRoot "logs") -Filter $reportName -File -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($candidate) {
        $reportPath = $candidate.FullName
        break
    }
}

$failures = New-Object System.Collections.Generic.List[string]
if ($exitCode -ne 0) {
    $failures.Add("staged package launch exited with code $exitCode") | Out-Null
}
if ([string]::IsNullOrWhiteSpace($reportPath) -or -not (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
    $failures.Add("staged package launch did not produce a frame report") | Out-Null
}

$report = $null
if (-not [string]::IsNullOrWhiteSpace($reportPath) -and (Test-Path -LiteralPath $reportPath -PathType Leaf)) {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    if (-not [bool]$report.frame_contract.startup.preflight_passed) {
        $failures.Add("staged package startup preflight did not pass") | Out-Null
    }
    if ([string]$report.frame_contract.startup.working_directory -notlike "$stageRoot*") {
        $failures.Add("staged package working directory did not point at package stage") | Out-Null
    }
    if ([string]$report.frame_contract.graphics_preset.id -ne "safe_startup") {
        $failures.Add("staged package did not apply safe_startup graphics preset") | Out-Null
    }
    if (-not [bool]$report.frame_contract.environment.loaded) {
        $failures.Add("staged package did not load the requested environment") | Out-Null
    }
    if ([int]$report.smoke_automation.total_frames -le 0) {
        $failures.Add("staged package did not run any smoke frames") | Out-Null
    }
}

$summary = [ordered]@{
    package_id = [string]$manifest.package_id
    staged_files = $selected.Count
    stage_root = $stageRoot
    exit_code = $exitCode
    report = $reportPath
    stdout = $stdoutPath
    smoke_frames = $SmokeFrames
    visual_captured = if ($report -and $report.visual_validation) { [bool]$report.visual_validation.captured } else { $false }
    failures = @($failures)
}
$summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $LogDir "release_package_launch_smoke.json")

if ($failures.Count -gt 0) {
    Write-Host "Release package launch smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host ("Release package launch smoke passed: files={0} frames={1} logs={2}" -f $selected.Count, $report.smoke_automation.total_frames, $LogDir) -ForegroundColor Green
