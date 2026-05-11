param(
    [string]$ManifestPath = "",
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $root "assets/environments/environments.json"
}
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "ibl_asset_policy_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

if (-not (Test-Path $ManifestPath)) {
    throw "Environment manifest not found: $ManifestPath"
}

$manifestDir = Split-Path -Parent $ManifestPath
$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
$failures = New-Object System.Collections.Generic.List[string]
$rows = New-Object System.Collections.Generic.List[object]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

$budgetCapsMiB = @{
    tiny = 8
    small = 32
    medium = 128
    large = 256
}

$preferredFormats = @()
if ($manifest.policy.runtime_format_preference) {
    foreach ($format in $manifest.policy.runtime_format_preference) {
        $preferredFormats += ([string]$format).ToLowerInvariant()
    }
}

if ($manifest.policy.normal_startup_downloads -ne $false) {
    Add-Failure "normal_startup_downloads must be false"
}
if ($manifest.policy.source_assets_optional -ne $true) {
    Add-Failure "source_assets_optional must be true"
}
if ($manifest.policy.legacy_scan_fallback -ne $false) {
    Add-Failure "legacy_scan_fallback must be false"
}
if ($preferredFormats.Count -lt 1) {
    Add-Failure "runtime_format_preference must list at least one runtime format"
}

$ids = @{}
$defaultFound = $false
$fallbackFound = $false
$enabledRuntimeCount = 0

foreach ($entry in $manifest.environments) {
    $id = [string]$entry.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        Add-Failure "environment entry has no id"
        continue
    }
    if ($ids.ContainsKey($id)) {
        Add-Failure "duplicate environment id '$id'"
    }
    $ids[$id] = $true
    if ($id -eq [string]$manifest.default) { $defaultFound = $true }
    if ($id -eq [string]$manifest.fallback) { $fallbackFound = $true }

    $budgetClass = [string]$entry.budget_class
    if (-not $budgetCapsMiB.ContainsKey($budgetClass)) {
        Add-Failure "environment '$id' has invalid budget_class '$budgetClass'"
        continue
    }

    $runtimePathRaw = [string]$entry.runtime_path
    if ([string]::IsNullOrWhiteSpace($runtimePathRaw)) {
        if (($entry.enabled -ne $false) -and $id -ne [string]$manifest.fallback) {
            Add-Failure "enabled non-fallback environment '$id' has no runtime_path"
        }
        continue
    }

    $runtimePath = [System.IO.Path]::GetFullPath((Join-Path $manifestDir $runtimePathRaw))
    $extension = [System.IO.Path]::GetExtension($runtimePath).TrimStart(".").ToLowerInvariant()
    if ($preferredFormats.Count -gt 0 -and $extension -notin $preferredFormats) {
        Add-Failure "environment '$id' runtime extension '$extension' is not in runtime_format_preference"
    }

    $exists = Test-Path $runtimePath
    if (($entry.required -eq $true) -and -not $exists) {
        Add-Failure "required environment '$id' runtime asset missing: $runtimePath"
    }
    if (($entry.enabled -ne $false) -and $exists) {
        ++$enabledRuntimeCount
    }

    $sizeMiB = 0.0
    if ($exists) {
        $file = Get-Item $runtimePath
        $sizeMiB = [Math]::Round($file.Length / 1MB, 3)
        $capMiB = [double]$budgetCapsMiB[$budgetClass]
        if ($sizeMiB -gt $capMiB) {
            Add-Failure "environment '$id' size ${sizeMiB}MiB exceeds $budgetClass cap ${capMiB}MiB"
        }
    }

    $rows.Add([pscustomobject]@{
        id = $id
        enabled = $entry.enabled -ne $false
        required = $entry.required -eq $true
        budget_class = $budgetClass
        runtime_path = $runtimePath
        runtime_exists = $exists
        runtime_extension = $extension
        size_mib = $sizeMiB
        cap_mib = $budgetCapsMiB[$budgetClass]
        max_runtime_dimension = [int]$entry.max_runtime_dimension
    })
}

if (-not $defaultFound) {
    Add-Failure "default environment '$($manifest.default)' is not declared"
}
if (-not $fallbackFound) {
    Add-Failure "fallback environment '$($manifest.fallback)' is not declared"
}
if ($enabledRuntimeCount -lt 1) {
    Add-Failure "no enabled runtime environment asset exists"
}

$summary = [ordered]@{
    manifest = [string]$ManifestPath
    normal_startup_downloads = $manifest.policy.normal_startup_downloads
    source_assets_optional = $manifest.policy.source_assets_optional
    legacy_scan_fallback = $manifest.policy.legacy_scan_fallback
    runtime_format_preference = $preferredFormats
    budget_caps_mib = $budgetCapsMiB
    runtime_asset_count = $enabledRuntimeCount
    rows = $rows
}
$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 (Join-Path $LogDir "ibl_asset_policy_summary.json")

$authoringTool = Join-Path $PSScriptRoot "prepare_ibl_runtime_assets.ps1"
if (-not (Test-Path $authoringTool)) {
    Add-Failure "IBL authoring tool missing: $authoringTool"
} else {
    $authoringLogDir = Join-Path $LogDir "authoring_plan"
    $authoringOutput = & powershell -NoProfile -ExecutionPolicy Bypass -File $authoringTool -ManifestPath $ManifestPath -LogDir $authoringLogDir 2>&1
    $authoringExit = $LASTEXITCODE
    $authoringOutput | Set-Content -Encoding UTF8 (Join-Path $LogDir "ibl_authoring_stdout.txt")
    if ($authoringExit -ne 0) {
        Add-Failure "IBL authoring plan failed with exit code $authoringExit"
    }
    $authoringPlanPath = Join-Path $authoringLogDir "ibl_authoring_plan.json"
    if (-not (Test-Path $authoringPlanPath)) {
        Add-Failure "IBL authoring plan did not write ibl_authoring_plan.json"
    } else {
        $plan = Get-Content $authoringPlanPath -Raw | ConvertFrom-Json
        if ([string]$plan.schema -ne "cortex.ibl_authoring_plan.v1") {
            Add-Failure "IBL authoring plan schema was '$($plan.schema)'"
        }
        if ([bool]$plan.downloads_allowed) {
            Add-Failure "IBL authoring plan allowed downloads"
        }
        if ($null -eq $plan.environments -or $plan.environments.Count -lt $manifest.environments.Count) {
            Add-Failure "IBL authoring plan does not cover all manifest environments"
        }
        $fallbackPlan = $plan.environments | Where-Object { [string]$_.id -eq [string]$manifest.fallback } | Select-Object -First 1
        if ($null -eq $fallbackPlan -or [string]$fallbackPlan.status -ne "procedural") {
            Add-Failure "IBL authoring plan does not preserve procedural fallback status"
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "IBL asset policy tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "IBL asset policy tests passed: runtime_assets=$enabledRuntimeCount logs=$LogDir" -ForegroundColor Green
exit 0
