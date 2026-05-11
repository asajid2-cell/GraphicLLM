param(
    [switch]$NoBuild,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$binDir = Join-Path $root "build/bin"
$manifestPath = Join-Path $root "assets/config/release_package_manifest.json"
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message) | Out-Null
}

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

function Test-RequiredFiles([string]$Base, [object[]]$Files, [string]$Label) {
    foreach ($file in $Files) {
        $path = Join-Path $Base ([string]$file)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            Add-Failure "$Label missing required file '$file'"
        }
    }
}

function Match-AnyPattern([string]$RelativePath, [object[]]$Patterns) {
    $rel = Normalize-Rel $RelativePath
    foreach ($pattern in $Patterns) {
        $normalized = Normalize-Rel ([string]$pattern)
        if ($rel -like $normalized) {
            return $normalized
        }
    }
    return $null
}

if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Release rebuild failed before package contract tests"
    }
}

if (-not (Test-Path $manifestPath)) {
    throw "Release package manifest missing: $manifestPath"
}
if (-not (Test-Path $binDir)) {
    throw "Runtime output directory missing: $binDir"
}

$manifest = Get-Content $manifestPath -Raw | ConvertFrom-Json
if ([int]$manifest.schema -ne 1) {
    Add-Failure "release package manifest schema is '$($manifest.schema)', expected 1"
}
if ([string]::IsNullOrWhiteSpace([string]$manifest.package_id)) {
    Add-Failure "release package manifest package_id is missing"
}

Test-RequiredFiles $root $manifest.required_docs "docs"
Test-RequiredFiles $root $manifest.required_source_files "source"
Test-RequiredFiles $binDir $manifest.required_runtime_files "runtime"

$selected = New-Object 'System.Collections.Generic.Dictionary[string,System.IO.FileInfo]'
foreach ($pattern in $manifest.runtime_include_globs) {
    $matches = Resolve-GlobFiles $binDir ([string]$pattern)
    if ($matches.Count -eq 0) {
        Add-Failure "runtime include glob matched no files: $pattern"
    }
    foreach ($match in $matches) {
        $rel = Get-RelativePath $binDir $match.FullName
        if (-not $selected.ContainsKey($rel)) {
            $selected.Add($rel, $match)
        }
    }
}

foreach ($file in $manifest.required_runtime_files) {
    $rel = Normalize-Rel ([string]$file)
    if (-not $selected.ContainsKey($rel)) {
        $path = Join-Path $binDir ([string]$file)
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $selected.Add($rel, (Get-Item -LiteralPath $path))
        }
    }
}

$totalBytes = 0L
foreach ($entry in $selected.GetEnumerator()) {
    $match = Match-AnyPattern $entry.Key $manifest.package_forbidden_globs
    if ($match) {
        Add-Failure "package selection includes forbidden file '$($entry.Key)' via '$match'"
    }
    $totalBytes += [int64]$entry.Value.Length
}

$maxBytes = [int64]$manifest.max_package_bytes
if ($maxBytes -le 0) {
    Add-Failure "release package manifest max_package_bytes must be positive"
} elseif ($totalBytes -gt $maxBytes) {
    Add-Failure "package selection is too large: $totalBytes bytes > $maxBytes bytes"
}

$releaseScript = Get-Content (Join-Path $PSScriptRoot "run_release_validation.ps1") -Raw
foreach ($step in $manifest.required_release_validation_steps) {
    if (-not $releaseScript.Contains("`"$step`"")) {
        Add-Failure "release validation does not include required package step '$step'"
    }
}

$readme = Get-Content (Join-Path $root "README.md") -Raw
$releaseReadiness = Get-Content (Join-Path $root "RELEASE_READINESS.md") -Raw
$toolsReadme = Get-Content (Join-Path $root "tools/README.md") -Raw
foreach ($docCheck in @(
    @{ name = "README.md"; text = $readme; token = "run_release_package_contract_tests.ps1" },
    @{ name = "RELEASE_READINESS.md"; text = $releaseReadiness; token = "release package contract" },
    @{ name = "tools/README.md"; text = $toolsReadme; token = "Release Package Contract" }
)) {
    if (-not ([string]$docCheck.text).Contains([string]$docCheck.token)) {
        Add-Failure "$($docCheck.name) does not document '$($docCheck.token)'"
    }
}

if (-not [string]::IsNullOrWhiteSpace($LogDir)) {
    New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
    $summary = [ordered]@{
        package_id = [string]$manifest.package_id
        selected_files = $selected.Count
        selected_bytes = $totalBytes
        max_package_bytes = $maxBytes
        required_runtime_files = @($manifest.required_runtime_files).Count
        include_globs = @($manifest.runtime_include_globs).Count
        forbidden_globs = @($manifest.package_forbidden_globs).Count
        failures = @($failures)
    }
    $summary | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 (Join-Path $LogDir "release_package_contract.json")
}

if ($failures.Count -gt 0) {
    Write-Host "Release package contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host ("Release package contract tests passed: files={0} bytes={1}/{2}" -f $selected.Count, $totalBytes, $maxBytes) -ForegroundColor Green
