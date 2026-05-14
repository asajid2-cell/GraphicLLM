param(
    [switch]$NoBuild,
    [switch]$Support,
    [switch]$ValidationCameras,
    [switch]$RegressionCorpus,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Read-Text([string]$RelativePath) {
    $path = Join-Path $root $RelativePath
    if (-not (Test-Path $path)) {
        throw "Missing required file: $RelativePath"
    }
    return Get-Content -Raw -Path $path
}

function Require-Contains([string]$Text, [string]$Needle, [string]$Message) {
    if ($Text.IndexOf($Needle, [StringComparison]::Ordinal) -lt 0) {
        Add-Failure $Message
    }
}

if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "semantic_visual_validation_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$header = Read-Text "src/Scene/SceneTransaction.h"
$source = Read-Text "src/Scene/SceneTransaction.cpp"
$main = Read-Text "src/main.cpp"
$releaseValidation = Read-Text "tools/run_release_validation.ps1"
$layoutContracts = Read-Text "assets/scenes/hand_authored/runtime_layout_contracts.json"

Require-Contains $header "SemanticVisualPolicy" "Transaction semantic visual policy is missing."
Require-Contains $header "requireSupportValidation" "Support/contact validation policy is missing."
Require-Contains $header "requireForegroundMidgroundBackground" "Foreground/midground/background policy is missing."
Require-Contains $header "requireMaterialDiversity" "Material/palette validation policy is missing."
Require-Contains $header "requireValidationCameraPerDirtyRegion" "Validation-camera dirty-region policy is missing."
Require-Contains $header "requireRegressionCorpus" "Regression corpus policy is missing."
Require-Contains $source "ValidateSemanticVisualPolicy" "Semantic visual policy is not enforced by transaction validation."
Require-Contains $source "asset_led_coastal_disconnected_rails" "Asset-led coastal rail regression case is missing."
Require-Contains $source "asset_led_rain_macro_backdrop_exposure" "Asset-led rain backdrop regression case is missing."
Require-Contains $source "asset_led_desert_placeholder_cylinders" "Asset-led desert placeholder regression case is missing."
Require-Contains $source "asset_led_neon_missing_sign_brackets" "Asset-led neon bracket regression case is missing."
Require-Contains $source "asset_led_forest_creek_edge_composition" "Asset-led forest composition regression case is missing."
Require-Contains $main "--semantic-visual-validation-self-test" "Engine CLI semantic visual validation self-test is missing."
Require-Contains $releaseValidation "run_semantic_visual_validation_matrix.ps1" "Release validation does not run semantic visual validation."
Require-Contains $layoutContracts "support_groups" "Runtime layout contracts do not expose support groups for semantic regression source."

if ($failures.Count -eq 0 -and -not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "Release rebuild failed before semantic visual validation."
    }
}

if ($failures.Count -eq 0) {
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run without -NoBuild."
    } else {
        $stdoutPath = Join-Path $LogDir "semantic_visual_validation_self_test_stdout.txt"
        $exeWorkingDir = Split-Path -Parent $exe
        Push-Location $exeWorkingDir
        try {
            $output = & $exe "--semantic-visual-validation-self-test" 2>&1
            $exitCode = $LASTEXITCODE
        } finally {
            Pop-Location
        }

        $outputText = ($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
        $outputText | Set-Content -Path $stdoutPath -Encoding UTF8
        if (-not [string]::IsNullOrWhiteSpace($outputText)) {
            $outputText -split "`r?`n" | ForEach-Object { Write-Host $_ }
        }

        if ($exitCode -ne 0) {
            Add-Failure "Semantic visual validation runtime self-test failed with exit code $exitCode."
        } else {
            try {
                $jsonStart = $outputText.IndexOf("{")
                if ($jsonStart -lt 0) {
                    throw "runtime output did not contain JSON"
                }
                $report = $outputText.Substring($jsonStart) | ConvertFrom-Json
                if (-not [bool]$report.pass) {
                    Add-Failure "Semantic visual validation runtime report pass=false."
                }
                if (-not [bool]$report.valid.accepted) {
                    Add-Failure "Valid semantic visual transaction was not accepted."
                }
                if (-not [bool]$report.valid.requires_support) {
                    Add-Failure "Runtime report did not enforce support validation."
                }
                if (-not [bool]$report.valid.requires_composition_bands) {
                    Add-Failure "Runtime report did not enforce foreground/midground/background validation."
                }
                if (-not [bool]$report.valid.requires_material_diversity) {
                    Add-Failure "Runtime report did not enforce material diversity validation."
                }
                if (-not [bool]$report.valid.requires_camera_per_dirty_region) {
                    Add-Failure "Runtime report did not enforce validation cameras per dirty region."
                }
                if (-not [bool]$report.valid.requires_regression_corpus -or [int]$report.valid.regression_case_count -lt 9) {
                    Add-Failure "Runtime report did not enforce the asset-led regression corpus."
                }
                if ([bool]$report.missing_support.accepted) {
                    Add-Failure "Unsupported generated fixture was accepted."
                }
                if ([bool]$report.missing_camera.accepted) {
                    Add-Failure "Missing-validation-camera fixture was accepted."
                }
                if ([bool]$report.over_budget.accepted) {
                    Add-Failure "Over-budget semantic visual fixture was accepted."
                }
            } catch {
                Add-Failure "Could not parse semantic visual validation runtime report: $($_.Exception.Message)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Semantic visual validation matrix failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "Semantic visual validation matrix passed." -ForegroundColor Green
Write-Host "  runtime_self_test=passed"
Write-Host "  logs=$LogDir"
exit 0
