param(
    [string]$ManifestPath = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $root "assets/models/naturalistic_showcase/asset_manifest.json"
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) { $script:failures.Add($Message) | Out-Null }

function Normalize-Rel([string]$Path) {
    return ($Path -replace "\\", "/").TrimStart("./")
}

if (-not (Test-Path $ManifestPath)) {
    throw "Naturalistic asset manifest missing: $ManifestPath"
}

$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
$assetRoot = Split-Path -Parent $ManifestPath

if ([int]$manifest.schema -ne 1) {
    Add-Failure "asset manifest schema must be 1"
}
if ([string]$manifest.policy.license_required -ne "CC0") {
    Add-Failure "asset manifest policy must require CC0"
}
if ([bool]$manifest.policy.normal_startup_downloads) {
    Add-Failure "naturalistic assets may not download during normal startup"
}
if ([string]$manifest.policy.source -ne "Poly Haven") {
    Add-Failure "initial naturalistic asset source must be Poly Haven"
}

$ids = @{}
$totalBytes = 0L
$maxTotalBytes = [int64]$manifest.policy.max_total_bytes
$maxSingleBytes = [int64]$manifest.policy.max_single_asset_bytes
$sceneUses = @{}

foreach ($asset in $manifest.assets) {
    $id = [string]$asset.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        Add-Failure "asset id is missing"
        continue
    }
    if ($ids.ContainsKey($id)) {
        Add-Failure "duplicate asset id '$id'"
    }
    $ids[$id] = $true
    if ([string]$asset.license -ne "CC0") {
        Add-Failure "$id license is '$($asset.license)', expected CC0"
    }
    if (-not ([string]$asset.source_url).StartsWith("https://polyhaven.com/a/")) {
        Add-Failure "$id source_url must point to polyhaven.com/a/"
    }
    if ([string]::IsNullOrWhiteSpace([string]$asset.runtime_gltf)) {
        Add-Failure "$id runtime_gltf is missing"
        continue
    }
    foreach ($scene in @($asset.scene_uses)) {
        $sceneUses[[string]$scene] = $true
    }

    $gltfPath = Join-Path $assetRoot ([string]$asset.runtime_gltf -replace "/", "\")
    if (-not (Test-Path $gltfPath)) {
        Add-Failure "$id runtime glTF missing: $gltfPath"
        continue
    }

    $assetDir = Split-Path -Parent $gltfPath
    $assetBytes = 0L
    foreach ($file in Get-ChildItem -Path $assetDir -File -Recurse) {
        $rel = Normalize-Rel ($file.FullName.Substring($assetDir.Length).TrimStart("\", "/"))
        if ($file.Extension -match "^\.(blend|fbx|usd|usdz)$") {
            Add-Failure "$id contains forbidden source/runtime format: $rel"
        }
        $assetBytes += [int64]$file.Length
    }
    $totalBytes += $assetBytes
    if ($assetBytes -gt $maxSingleBytes) {
        Add-Failure "$id asset bytes $assetBytes exceed max_single_asset_bytes $maxSingleBytes"
    }

    try {
        $gltf = Get-Content $gltfPath -Raw | ConvertFrom-Json
        if ($null -eq $gltf.asset -or [string]$gltf.asset.version -ne "2.0") {
            Add-Failure "$id glTF asset.version must be 2.0"
        }
        if ($null -eq $gltf.meshes -or $gltf.meshes.Count -lt 1) {
            Add-Failure "$id glTF has no meshes"
        }
        if ($null -eq $gltf.buffers -or $gltf.buffers.Count -lt 1) {
            Add-Failure "$id glTF has no buffers"
        } else {
            foreach ($buffer in $gltf.buffers) {
                $bufferPath = Join-Path $assetDir ([string]$buffer.uri -replace "/", "\")
                if (-not (Test-Path $bufferPath)) {
                    Add-Failure "$id missing glTF buffer '$($buffer.uri)'"
                }
            }
        }
    } catch {
        Add-Failure "$id failed to parse glTF: $($_.Exception.Message)"
    }
}

if ($manifest.assets.Count -lt 6) {
    Add-Failure "naturalistic asset set should include at least six assets"
}
foreach ($requiredScene in @("outdoor_sunset_beach", "material_lab", "effects_showcase")) {
    if (-not $sceneUses.ContainsKey($requiredScene)) {
        Add-Failure "asset manifest has no asset assigned to scene '$requiredScene'"
    }
}
if ($totalBytes -gt $maxTotalBytes) {
    Add-Failure "total naturalistic asset bytes $totalBytes exceed max_total_bytes $maxTotalBytes"
}

$releaseManifestPath = Join-Path $root "assets/config/release_package_manifest.json"
if (Test-Path $releaseManifestPath) {
    $releaseManifest = Get-Content $releaseManifestPath -Raw | ConvertFrom-Json
    $includes = @($releaseManifest.runtime_include_globs) -join "`n"
    if (-not $includes.Contains("assets/models/naturalistic_showcase/**")) {
        Add-Failure "release package manifest must include assets/models/naturalistic_showcase/**"
    }
    $forbidden = @($releaseManifest.package_forbidden_globs) -join "`n"
    if ($forbidden.Contains("models/*")) {
        Add-Failure "release package manifest still forbids all models/*"
    }
}

$releaseScriptPath = Join-Path $PSScriptRoot "run_release_validation.ps1"
if (Test-Path $releaseScriptPath) {
    $releaseScript = Get-Content $releaseScriptPath -Raw
    if (-not $releaseScript.Contains("naturalistic_asset_policy")) {
        Add-Failure "run_release_validation.ps1 must include naturalistic_asset_policy"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Naturalistic asset policy tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host ("Naturalistic asset policy tests passed: assets={0} bytes={1}/{2}" -f $manifest.assets.Count, $totalBytes, $maxTotalBytes) -ForegroundColor Green
