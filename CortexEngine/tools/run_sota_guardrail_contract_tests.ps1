param(
    [switch]$NoFullPathTracing,
    [switch]$NoRuntimeNeuralShaderWithoutFallback,
    [switch]$GuardVendorUpscalers,
    [switch]$CapabilityTierOnly,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
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

function Get-ProjectText([string[]]$Roots) {
    $text = New-Object System.Text.StringBuilder
    foreach ($relativeRoot in $Roots) {
        $path = Join-Path $root $relativeRoot
        if (-not (Test-Path $path)) { continue }
        Get-ChildItem -Path $path -Recurse -File |
            Where-Object {
                $_.FullName -notmatch "\\build\\" -and
                $_.Extension -in @(".cpp", ".h", ".hpp", ".hlsl", ".hlsli", ".json", ".md")
            } |
            ForEach-Object {
                [void]$text.AppendLine((Get-Content -Raw -Path $_.FullName))
            }
    }
    return $text.ToString()
}

if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "sota_guardrail_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$runtimeText = Get-ProjectText @("src", "assets/config", "assets/shaders")
$completionLedger = Read-Text "docs/COMPLETION_LEDGER.md"
$generatedAdmission = Read-Text "src/Scene/GeneratedAssetAdmission.cpp"
$generatedAdmissionHeader = Read-Text "src/Scene/GeneratedAssetAdmission.h"

if ($runtimeText -match "(?i)\bfull\s*path\s*trac|\bpath\s*tracing\s*mode|FullPathTracer") {
    Add-Failure "Runtime source/config appears to expose full path tracing as an implementation path."
}

if ($runtimeText -match "(?i)runtime\s*neural\s*shader|neural_shader|NeuralShader") {
    if ($runtimeText -notmatch "fallbackReady" -or $runtimeText -notmatch "targetCapabilityTier") {
        Add-Failure "Runtime neural shader tokens exist without capability-tier and fallback policy metadata."
    }
}

if ($runtimeText -match "(?i)\bDLSS\b|\bXeSS\b|\bDirectSR\b|\bFSR\s*[23]?\b") {
    if ($completionLedger -notmatch "motion vectors, exposure, reactive masks, and edit invalidation") {
        Add-Failure "Vendor upscaler tokens exist before the temporal upscaling contract is represented in the ledger."
    }
}

$tierFeatureTokens = @(
    "sampler_feedback",
    "SamplerFeedback",
    "shader_execution_reordering",
    "ShaderExecutionReordering",
    "opacity_micromap",
    "OpacityMicromap",
    "neural_shader",
    "NeuralShader"
)
foreach ($token in $tierFeatureTokens) {
    if ($runtimeText.IndexOf($token, [StringComparison]::Ordinal) -ge 0) {
        if ($generatedAdmissionHeader.IndexOf("targetCapabilityTier", [StringComparison]::Ordinal) -lt 0 -or
            $generatedAdmission.IndexOf("fallbackReady", [StringComparison]::Ordinal) -lt 0) {
            Add-Failure "Capability-tier feature '$token' appears without generated asset tier/fallback admission."
        }
    }
}

if ($completionLedger -match "Current status: COMPLETE|SOTA plan complete|foundation complete") {
    Add-Failure "Completion ledger contains a premature completion phrase."
}
if ($completionLedger -notmatch "larger authored content" -or
    $completionLedger -notmatch "must not be counted as SOTA architecture completion") {
    Add-Failure "Completion ledger does not preserve the future content work guardrail."
}

if ($failures.Count -gt 0) {
    Write-Host "SOTA guardrail contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir"
    exit 1
}

Write-Host "SOTA guardrail contract tests passed." -ForegroundColor Green
Write-Host "  no_full_path_tracing=$true"
Write-Host "  neural_shader_requires_fallback=$true"
Write-Host "  vendor_upscaler_guard=$true"
Write-Host "  optional_features_require_capability_tiers=$true"
Write-Host "  logs=$LogDir"
exit 0
