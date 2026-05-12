param()

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$readmePath = Join-Path $root "README.md"
$releasePath = Join-Path $root "RELEASE_READINESS.md"
$manifestPath = Join-Path $root "docs/media/gallery_manifest.json"
$packageManifestPath = Join-Path $root "assets/config/release_package_manifest.json"
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message) | Out-Null
}

function Require-Contains([string]$Name, [string]$Text, [string]$Needle) {
    if ($Text.IndexOf($Needle, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
        Add-Failure "$Name is missing '$Needle'"
    }
}

if (-not (Test-Path $readmePath)) { throw "README missing: $readmePath" }
if (-not (Test-Path $releasePath)) { throw "RELEASE_READINESS missing: $releasePath" }
if (-not (Test-Path $manifestPath)) { Add-Failure "Gallery manifest missing: $manifestPath" }
if (-not (Test-Path $packageManifestPath)) { throw "Package manifest missing: $packageManifestPath" }

$readme = Get-Content $readmePath -Raw
$release = Get-Content $releasePath -Raw
$packageManifest = Get-Content $packageManifestPath -Raw | ConvertFrom-Json

foreach ($section in @(
    "# Project Cortex",
    "## Screenshots",
    "## What It Shows",
    "## Current Metrics",
    "## Run Modes",
    "## Validation",
    "## Architecture",
    "## Limitations"
)) {
    Require-Contains "README.md" $readme $section
}

foreach ($token in @(
    "real-time DirectX 12 hybrid renderer",
    "run_public_capture_gallery.ps1",
    "run_release_validation.ps1",
    "run_release_package_contract_tests.ps1",
    "run_release_package_launch_smoke.ps1",
    "gallery_manifest.json",
    "public_high",
    "safe_startup",
    "release_showcase"
)) {
    Require-Contains "README.md" $readme $token
}

foreach ($banned in @(
    "Neural-Native",
    "portfolio-quality",
    "revolutionary",
    "game-changing",
    "AI-powered renderer"
)) {
    if ($readme.IndexOf($banned, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
        Add-Failure "README.md still contains banned public wording '$banned'"
    }
}

if (Test-Path $manifestPath) {
    $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
    if ([int]$manifest.schema -ne 1) {
        Add-Failure "gallery_manifest.json schema is not 1"
    }
    if (@($manifest.entries).Count -lt 6) {
        Add-Failure "gallery_manifest.json must contain at least six public captures"
    }
    foreach ($entry in $manifest.entries) {
        $image = [string]$entry.image
        if ([string]::IsNullOrWhiteSpace($image)) {
            Add-Failure "gallery entry '$($entry.id)' has no image"
            continue
        }
        $imagePath = Join-Path $root ($image -replace "/", "\")
        if (-not (Test-Path $imagePath -PathType Leaf)) {
            Add-Failure "gallery image missing: $image"
        }
        Require-Contains "README.md" $readme $image
        if ([int]$entry.capture_width -lt 1600 -or [int]$entry.capture_height -lt 900) {
            Add-Failure "gallery image '$image' is not high resolution: $($entry.capture_width)x$($entry.capture_height)"
        }
        if ([double]$entry.render_scale -lt 0.99) {
            Add-Failure "gallery image '$image' did not use high render scale: $($entry.render_scale)"
        }
    }
}

foreach ($doc in @(
    "docs/media/gallery_manifest.json",
    "docs/media/rt_showcase_hero.png",
    "docs/media/material_lab_hero.png",
    "docs/media/glass_water_courtyard_hero.png",
    "docs/media/effects_showcase_hero.png",
    "docs/media/outdoor_sunset_beach_hero.png",
    "docs/media/ibl_gallery_sweep.png"
)) {
    $found = $false
    foreach ($requiredDoc in $packageManifest.required_docs) {
        if ([string]$requiredDoc -eq $doc) {
            $found = $true
            break
        }
    }
    if (-not $found) {
        Add-Failure "release_package_manifest.json required_docs is missing '$doc'"
    }
}

foreach ($token in @(
    "Latest Verified Gate",
    "release_validation_",
    "public_high",
    "gallery_manifest.json",
    "Release package"
)) {
    Require-Contains "RELEASE_READINESS.md" $release $token
}

if ($failures.Count -gt 0) {
    Write-Host "Public README contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Public README contract tests passed" -ForegroundColor Green
