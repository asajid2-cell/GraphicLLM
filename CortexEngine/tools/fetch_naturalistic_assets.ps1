param(
    [string]$ManifestPath = "",
    [switch]$Force
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $root "assets/models/naturalistic_showcase/asset_manifest.json"
}

if (-not (Test-Path $ManifestPath)) {
    throw "Naturalistic asset manifest not found: $ManifestPath"
}

$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
$assetRoot = Split-Path -Parent $ManifestPath
$headers = @{ "User-Agent" = "CortexEngineNaturalisticAssetFetch/1.0 (academic renderer showcase)" }
$downloaded = 0
$skipped = 0

foreach ($asset in $manifest.assets) {
    $id = [string]$asset.id
    $resolution = [string]$manifest.policy.runtime_resolution
    $filesUri = "https://api.polyhaven.com/files/$id"
    Write-Host "==> $id $resolution"
    $files = Invoke-RestMethod -Uri $filesUri -Headers $headers -TimeoutSec 60
    $gltfEntry = $files.gltf.$resolution.gltf
    if ($null -eq $gltfEntry) {
        throw "Poly Haven asset '$id' has no glTF $resolution entry"
    }

    $targetDir = Join-Path $assetRoot $id
    New-Item -ItemType Directory -Force -Path $targetDir | Out-Null

    $downloads = @()
    $downloads += [pscustomobject]@{
        relative = (Split-Path -Leaf ([string]$asset.runtime_gltf))
        url = [string]$gltfEntry.url
        size = [int64]$gltfEntry.size
    }
    foreach ($include in $gltfEntry.include.PSObject.Properties) {
        $downloads += [pscustomobject]@{
            relative = [string]$include.Name
            url = [string]$include.Value.url
            size = [int64]$include.Value.size
        }
    }

    foreach ($entry in $downloads) {
        $outPath = Join-Path $targetDir ($entry.relative -replace "/", "\")
        $outDir = Split-Path -Parent $outPath
        New-Item -ItemType Directory -Force -Path $outDir | Out-Null
        if ((Test-Path $outPath) -and -not $Force) {
            ++$skipped
            continue
        }
        Invoke-WebRequest -Uri $entry.url -Headers $headers -OutFile $outPath -TimeoutSec 180
        ++$downloaded
    }
}

Write-Host "Naturalistic assets fetched: downloaded=$downloaded skipped=$skipped manifest=$ManifestPath" -ForegroundColor Green
