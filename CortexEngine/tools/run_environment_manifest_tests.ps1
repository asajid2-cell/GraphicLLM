param(
    [string]$ManifestPath = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $root "assets/environments/environments.json"
}

$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

if (-not (Test-Path $ManifestPath)) {
    throw "Environment manifest not found: $ManifestPath"
}

$manifestDir = Split-Path -Parent $ManifestPath
$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json

if ([int]$manifest.schema -ne 1) {
    Add-Failure "environment manifest schema must be 1"
}
if ([string]::IsNullOrWhiteSpace([string]$manifest.default)) {
    Add-Failure "environment manifest default is missing"
}
if ([string]::IsNullOrWhiteSpace([string]$manifest.fallback)) {
    Add-Failure "environment manifest fallback is missing"
}
if ($manifest.policy.normal_startup_downloads -ne $false) {
    Add-Failure "normal_startup_downloads must stay false"
}
if ($manifest.policy.legacy_scan_fallback -ne $false) {
    Add-Failure "legacy_scan_fallback must stay false so manifest fallback behavior is deterministic"
}

$ids = @{}
$defaultFound = $false
$fallbackFound = $false
$enabledRuntimeCount = 0
foreach ($entry in $manifest.environments) {
    $id = [string]$entry.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        Add-Failure "environment id is missing"
        continue
    }
    if ($ids.ContainsKey($id)) {
        Add-Failure "duplicate environment id '$id'"
    }
    $ids[$id] = $true
    if ($id -eq [string]$manifest.default) {
        $defaultFound = $true
    }
    if ($id -eq [string]$manifest.fallback) {
        $fallbackFound = $true
    }
    if ($entry.enabled -eq $false) {
        continue
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$entry.runtime_path)) {
        ++$enabledRuntimeCount
        $runtimePath = Join-Path $manifestDir ([string]$entry.runtime_path)
        $resolved = [System.IO.Path]::GetFullPath($runtimePath)
        if (-not (Test-Path $resolved)) {
            if ($entry.required -eq $true) {
                Add-Failure "required environment '$id' missing runtime asset '$resolved'"
            } else {
                Write-Host "Optional environment '$id' missing runtime asset '$resolved'; fallback policy allows this." -ForegroundColor Yellow
            }
        }
    } elseif ($id -ne [string]$manifest.fallback) {
        Add-Failure "enabled non-fallback environment '$id' has no runtime_path"
    }

    $budgetClass = [string]$entry.budget_class
    if ($budgetClass -notin @("tiny", "small", "medium", "large")) {
        Add-Failure "environment '$id' has invalid budget_class '$budgetClass'"
    }
    if ($null -ne $entry.max_runtime_dimension -and [int]$entry.max_runtime_dimension -gt 8192) {
        Add-Failure "environment '$id' max_runtime_dimension exceeds 8192"
    }
}

if (-not $defaultFound) {
    Add-Failure "default environment '$($manifest.default)' is not declared"
}
if (-not $fallbackFound) {
    Add-Failure "fallback environment '$($manifest.fallback)' is not declared"
}
if ($enabledRuntimeCount -lt 1) {
    Add-Failure "manifest has no enabled runtime environment"
}

if ($failures.Count -gt 0) {
    Write-Host "Environment manifest tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Environment manifest tests passed: environments=$($manifest.environments.Count) default=$($manifest.default) fallback=$($manifest.fallback)" -ForegroundColor Green
