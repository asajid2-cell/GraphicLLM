param(
    [switch]$NoBuild,
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$repoRoot = Resolve-Path (Join-Path $root "..")
$binDir = Join-Path $root "build/bin"
$manifestPath = Join-Path $root "assets/config/release_package_manifest.json"
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path $root "release"
}
$OutputDir = (New-Item -ItemType Directory -Force -Path $OutputDir).FullName

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

function Add-PackageFile(
    [System.Collections.Generic.Dictionary[string,System.IO.FileInfo]]$Selected,
    [string]$Base,
    [string]$SourcePath,
    [string]$PackagePrefix
) {
    if (-not (Test-Path -LiteralPath $SourcePath -PathType Leaf)) {
        throw "Package source file missing: $SourcePath"
    }
    $rel = Normalize-Rel (Join-Path $PackagePrefix (Get-RelativePath $Base $SourcePath))
    if (-not $Selected.ContainsKey($rel)) {
        $Selected.Add($rel, (Get-Item -LiteralPath $SourcePath))
    }
}

if (-not $NoBuild) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Release rebuild failed before package creation"
    }
}
if (-not (Test-Path $manifestPath)) {
    throw "Release package manifest missing: $manifestPath"
}
if (-not (Test-Path $binDir)) {
    throw "Runtime output directory missing: $binDir"
}

$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
$commit = (& git -C $repoRoot rev-parse --short HEAD 2>$null)
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($commit)) {
    $commit = "unknown"
}
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$packageId = [string]$manifest.package_id
$stageRoot = Join-Path $OutputDir ("{0}_{1}_{2}_stage" -f $packageId, $commit.Trim(), $stamp)
$archivePath = Join-Path $OutputDir ("{0}_{1}_{2}.zip" -f $packageId, $commit.Trim(), $stamp)
New-Item -ItemType Directory -Force -Path $stageRoot | Out-Null

$selected = New-Object 'System.Collections.Generic.Dictionary[string,System.IO.FileInfo]'
foreach ($doc in $manifest.required_docs) {
    Add-PackageFile $selected $root (Join-Path $root ([string]$doc)) "CortexEngine"
}
foreach ($source in $manifest.required_source_files) {
    Add-PackageFile $selected $root (Join-Path $root ([string]$source)) "CortexEngine"
}
foreach ($pattern in $manifest.runtime_include_globs) {
    foreach ($match in (Resolve-GlobFiles $binDir ([string]$pattern))) {
        Add-PackageFile $selected $binDir $match.FullName "runtime"
    }
}
foreach ($runtimeFile in $manifest.required_runtime_files) {
    Add-PackageFile $selected $binDir (Join-Path $binDir ([string]$runtimeFile)) "runtime"
}

$totalBytes = 0L
foreach ($entry in $selected.GetEnumerator()) {
    $dest = Join-Path $stageRoot ($entry.Key -replace "/", "\")
    $destDir = Split-Path -Parent $dest
    if (-not (Test-Path $destDir)) {
        New-Item -ItemType Directory -Force -Path $destDir | Out-Null
    }
    Copy-Item -LiteralPath $entry.Value.FullName -Destination $dest -Force
    $totalBytes += [int64]$entry.Value.Length
}

if (Test-Path $archivePath) {
    Remove-Item -Force $archivePath
}
Compress-Archive -Path (Join-Path $stageRoot "*") -DestinationPath $archivePath -CompressionLevel Optimal
$archive = Get-Item -LiteralPath $archivePath

$summary = [ordered]@{
    schema = 1
    package_id = $packageId
    commit = $commit.Trim()
    generated_utc = (Get-Date).ToUniversalTime().ToString("o")
    archive = $archivePath
    archive_bytes = [int64]$archive.Length
    stage_root = $stageRoot
    selected_files = $selected.Count
    selected_bytes = $totalBytes
    max_package_bytes = [int64]$manifest.max_package_bytes
}
$summaryPath = Join-Path $OutputDir ("{0}_{1}_{2}_summary.json" -f $packageId, $commit.Trim(), $stamp)
$summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $summaryPath

if ([int64]$manifest.max_package_bytes -gt 0 -and [int64]$archive.Length -gt [int64]$manifest.max_package_bytes) {
    Write-Host "Release package archive exceeds manifest size cap: $($archive.Length) > $($manifest.max_package_bytes)" -ForegroundColor Red
    Write-Host "summary=$summaryPath" -ForegroundColor Red
    exit 1
}

Write-Host "Release package created" -ForegroundColor Green
Write-Host " archive=$archivePath"
Write-Host " summary=$summaryPath"
Write-Host " files=$($selected.Count) bytes=$($archive.Length)/$($manifest.max_package_bytes)"
