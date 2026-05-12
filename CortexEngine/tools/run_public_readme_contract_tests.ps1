param()

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$repoRoot = Resolve-Path (Join-Path $root "..")
$readmePath = Join-Path $root "README.md"
$repoReadmePath = Join-Path $repoRoot "README.md"
$releasePath = Join-Path $root "RELEASE_READINESS.md"
$manifestPath = Join-Path $root "docs/media/gallery_manifest.json"
$videoManifestPath = Join-Path $root "docs/media/video_manifest.json"
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
if (-not (Test-Path $repoReadmePath)) { throw "Repository README missing: $repoReadmePath" }
if (-not (Test-Path $releasePath)) { throw "RELEASE_READINESS missing: $releasePath" }
if (-not (Test-Path $manifestPath)) { Add-Failure "Gallery manifest missing: $manifestPath" }
if (-not (Test-Path $videoManifestPath)) { Add-Failure "Video manifest missing: $videoManifestPath" }
if (-not (Test-Path $packageManifestPath)) { throw "Package manifest missing: $packageManifestPath" }

$readme = Get-Content $readmePath -Raw
$repoReadme = Get-Content $repoReadmePath -Raw
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
    Require-Contains "repository README.md" $repoReadme $section
}

foreach ($token in @(
    "real-time DirectX 12 hybrid renderer",
    "run_public_capture_gallery.ps1",
    "run_public_gallery_reel.ps1",
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

foreach ($token in @(
    "real-time DirectX 12 hybrid renderer",
    "CortexEngine/docs/media/rt_showcase_hero.png",
    "CortexEngine/docs/media/gallery_manifest.json",
    "CortexEngine/docs/media/cortex_gallery_reel.mp4",
    "run_public_capture_gallery.ps1",
    "run_public_gallery_reel.ps1",
    "run_release_validation.ps1",
    "public_high",
    "release_showcase"
)) {
    Require-Contains "repository README.md" $repoReadme $token
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
    if ($repoReadme.IndexOf($banned, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
        Add-Failure "repository README.md still contains banned public wording '$banned'"
    }
}

if (Test-Path $manifestPath) {
    $manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
    if ([int]$manifest.schema -ne 1) {
        Add-Failure "gallery_manifest.json schema is not 1"
    }
    if (@($manifest.entries).Count -lt 12) {
        Add-Failure "gallery_manifest.json must contain at least twelve public captures"
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
        Require-Contains "repository README.md" $repoReadme ("CortexEngine/" + $image)
        if ([int]$entry.capture_width -lt 1600 -or [int]$entry.capture_height -lt 900) {
            Add-Failure "gallery image '$image' is not high resolution: $($entry.capture_width)x$($entry.capture_height)"
        }
        if ([double]$entry.render_scale -lt 0.99) {
            Add-Failure "gallery image '$image' did not use high render scale: $($entry.render_scale)"
        }
    }
}

if (Test-Path $videoManifestPath) {
    $videoManifest = Get-Content $videoManifestPath -Raw | ConvertFrom-Json
    if ([int]$videoManifest.schema -ne 1) {
        Add-Failure "video_manifest.json schema is not 1"
    }
    $videoPath = Join-Path $root (([string]$videoManifest.output) -replace "/", "\")
    if (-not (Test-Path $videoPath -PathType Leaf)) {
        Add-Failure "public video missing: $($videoManifest.output)"
    } elseif ((Get-Item $videoPath).Length -lt 1024) {
        Add-Failure "public video is unexpectedly small: $($videoManifest.output)"
    }
    Require-Contains "README.md" $readme ([string]$videoManifest.output)
    Require-Contains "repository README.md" $repoReadme ("CortexEngine/" + [string]$videoManifest.output)
    if ([int]$videoManifest.source_image_count -lt 12) {
        Add-Failure "video manifest must use at least twelve gallery images"
    }
}

foreach ($doc in @(
    "docs/media/gallery_manifest.json",
    "docs/media/video_manifest.json",
    "docs/media/rt_showcase_hero.png",
    "docs/media/rt_showcase_reflection_closeup.png",
    "docs/media/rt_showcase_material_overview.png",
    "docs/media/material_lab_hero.png",
    "docs/media/material_lab_metal_closeup.png",
    "docs/media/material_lab_glass_emissive.png",
    "docs/media/glass_water_courtyard_hero.png",
    "docs/media/glass_water_courtyard_water_closeup.png",
    "docs/media/glass_water_courtyard_glass_canopy.png",
    "docs/media/effects_showcase_hero.png",
    "docs/media/effects_showcase_particles_closeup.png",
    "docs/media/effects_showcase_neon_materials.png",
    "docs/media/outdoor_sunset_beach_hero.png",
    "docs/media/outdoor_sunset_beach_waterline.png",
    "docs/media/liquid_gallery_hero.png",
    "docs/media/liquid_gallery_water_lava.png",
    "docs/media/liquid_gallery_viscous_pair.png",
    "docs/media/ibl_gallery_hero.png",
    "docs/media/ibl_gallery_sweep.png",
    "docs/media/cortex_gallery_reel.mp4"
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
