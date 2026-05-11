param(
    [string]$ManifestPath = "",
    [string]$LogDir = "",
    [switch]$Execute,
    [string]$TexconvPath = "",
    [switch]$RequireConverter
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($ManifestPath)) {
    $ManifestPath = Join-Path $root "assets/environments/environments.json"
}
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "ibl_authoring_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

if (-not (Test-Path $ManifestPath)) {
    throw "Environment manifest not found: $ManifestPath"
}

function Resolve-ManifestPath {
    param(
        [Parameter(Mandatory=$true)][string]$ManifestDir,
        [string]$RelativePath
    )
    if ([string]::IsNullOrWhiteSpace($RelativePath)) {
        return ""
    }
    [System.IO.Path]::GetFullPath((Join-Path $ManifestDir $RelativePath))
}

function Find-Texconv {
    param([string]$ExplicitPath)
    if (-not [string]::IsNullOrWhiteSpace($ExplicitPath)) {
        if (-not (Test-Path $ExplicitPath)) {
            throw "TexconvPath was provided but does not exist: $ExplicitPath"
        }
        return (Resolve-Path $ExplicitPath).Path
    }

    $cmd = Get-Command texconv.exe -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    return ""
}

$manifestDir = Split-Path -Parent $ManifestPath
$manifest = Get-Content $ManifestPath -Raw | ConvertFrom-Json
$texconv = Find-Texconv $TexconvPath
$failures = New-Object System.Collections.Generic.List[string]
$warnings = New-Object System.Collections.Generic.List[string]
$rows = New-Object System.Collections.Generic.List[object]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Add-Warning([string]$Message) {
    $script:warnings.Add($Message)
}

if ($manifest.policy.normal_startup_downloads -ne $false) {
    Add-Failure "normal_startup_downloads must remain false; this tool never downloads IBL assets."
}
if ($manifest.policy.source_assets_optional -ne $true) {
    Add-Failure "source_assets_optional must remain true so public review does not require source HDR/EXR files."
}
if ($manifest.policy.legacy_scan_fallback -ne $false) {
    Add-Failure "legacy_scan_fallback must remain false; runtime environment selection must be manifest-driven."
}
if ($RequireConverter -and [string]::IsNullOrWhiteSpace($texconv)) {
    Add-Failure "RequireConverter was set but texconv.exe was not found. Pass -TexconvPath or add it to PATH."
}

$preferredFormats = @()
foreach ($format in $manifest.policy.runtime_format_preference) {
    $preferredFormats += ([string]$format).ToLowerInvariant()
}

foreach ($entry in $manifest.environments) {
    $id = [string]$entry.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        Add-Failure "environment entry has no id"
        continue
    }

    $type = [string]$entry.type
    $runtimePath = Resolve-ManifestPath $manifestDir ([string]$entry.runtime_path)
    $sourcePath = Resolve-ManifestPath $manifestDir ([string]$entry.source_path)
    $runtimeExists = -not [string]::IsNullOrWhiteSpace($runtimePath) -and (Test-Path $runtimePath)
    $sourceExists = -not [string]::IsNullOrWhiteSpace($sourcePath) -and (Test-Path $sourcePath)
    $required = $entry.required -eq $true
    $enabled = $entry.enabled -ne $false
    $runtimeExtension = if ([string]::IsNullOrWhiteSpace($runtimePath)) { "" } else {
        [System.IO.Path]::GetExtension($runtimePath).TrimStart(".").ToLowerInvariant()
    }
    $sourceExtension = if ([string]::IsNullOrWhiteSpace($sourcePath)) { "" } else {
        [System.IO.Path]::GetExtension($sourcePath).TrimStart(".").ToLowerInvariant()
    }

    $status = "ready"
    $command = ""
    $note = ""
    if ($type -eq "procedural") {
        $status = "procedural"
        $note = "No runtime asset required; this is the deterministic fallback."
    } elseif ([string]::IsNullOrWhiteSpace($runtimePath)) {
        $status = "missing_runtime_path"
        $note = "Non-procedural environments must declare runtime_path."
        if ($enabled) { Add-Failure "environment '$id' is enabled but has no runtime_path" }
    } elseif ($preferredFormats.Count -gt 0 -and $runtimeExtension -notin $preferredFormats) {
        $status = "invalid_runtime_format"
        $note = "Runtime extension '$runtimeExtension' is not in runtime_format_preference."
        Add-Failure "environment '$id' runtime extension '$runtimeExtension' is not allowed"
    } elseif ($runtimeExists) {
        $status = "ready"
        $note = "Runtime asset already exists."
    } elseif ($sourceExists) {
        $status = "convertible"
        $note = "Source exists; conversion is opt-in and local-only."
        if ($runtimeExtension -eq "dds" -and $sourceExtension -eq "hdr") {
            $runtimeDir = Split-Path -Parent $runtimePath
            $command = "texconv.exe -y -f BC6H_UF16 -m 0 -o `"$runtimeDir`" `"$sourcePath`""
            if ($Execute) {
                if ([string]::IsNullOrWhiteSpace($texconv)) {
                    Add-Failure "environment '$id' is convertible but texconv.exe was not found"
                } else {
                    New-Item -ItemType Directory -Force -Path $runtimeDir | Out-Null
                    & $texconv -y -f BC6H_UF16 -m 0 -o $runtimeDir $sourcePath | Set-Content -Encoding UTF8 (Join-Path $LogDir "$id-texconv.txt")
                    if ($LASTEXITCODE -ne 0) {
                        Add-Failure "texconv failed for environment '$id' with exit code $LASTEXITCODE"
                    }
                }
            }
        } else {
            $status = "manual_conversion_required"
            $note = "Source/runtime format pair '$sourceExtension' -> '$runtimeExtension' needs an explicit external conversion step."
            Add-Warning "environment '$id' needs manual conversion for '$sourceExtension' -> '$runtimeExtension'"
        }
    } else {
        $status = if ($required) { "missing_required_runtime" } else { "optional_source_missing" }
        $note = if ($required) {
            "Required runtime asset is missing and no source_path exists."
        } else {
            "Optional runtime/source is absent; runtime fallback remains allowed."
        }
        if ($required) {
            Add-Failure "required environment '$id' is missing runtime asset and source asset"
        }
    }

    if ($Execute -and -not [string]::IsNullOrWhiteSpace($runtimePath) -and -not (Test-Path $runtimePath)) {
        Add-Failure "environment '$id' did not produce runtime asset '$runtimePath'"
    }

    $rows.Add([pscustomobject]@{
        id = $id
        type = $type
        enabled = $enabled
        required = $required
        budget_class = [string]$entry.budget_class
        max_runtime_dimension = [int]$entry.max_runtime_dimension
        runtime_path = $runtimePath
        runtime_exists = $runtimeExists
        runtime_extension = $runtimeExtension
        source_path = $sourcePath
        source_exists = $sourceExists
        source_extension = $sourceExtension
        status = $status
        command = $command
        note = $note
    })
}

$summary = [ordered]@{
    schema = "cortex.ibl_authoring_plan.v1"
    manifest = [string]$ManifestPath
    execute = [bool]$Execute
    downloads_allowed = $false
    source_assets_optional = [bool]$manifest.policy.source_assets_optional
    runtime_format_preference = $preferredFormats
    converter = if ([string]::IsNullOrWhiteSpace($texconv)) { $null } else { $texconv }
    warning_count = $warnings.Count
    failure_count = $failures.Count
    warnings = $warnings
    environments = $rows
}

$summaryPath = Join-Path $LogDir "ibl_authoring_plan.json"
$summary | ConvertTo-Json -Depth 8 | Set-Content -Encoding UTF8 $summaryPath

if ($failures.Count -gt 0) {
    Write-Host "IBL authoring plan failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "summary=$summaryPath" -ForegroundColor Red
    exit 1
}

Write-Host "IBL authoring plan ready: environments=$($rows.Count) execute=$([bool]$Execute) summary=$summaryPath" -ForegroundColor Green
exit 0
