param(
    [int]$SmokeFrames = 160,
    [string]$LogDir = "",
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
$exeWorkingDir = Split-Path -Parent $exe
$baseLogDir = Join-Path $root "build/bin/logs"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "dreamer_positive_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Require-Contains([string]$Text, [string]$Needle, [string]$Message) {
    if (-not $Text.Contains($Needle)) {
        Add-Failure $Message
    }
}

if (-not $NoBuild) {
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
    if ($LASTEXITCODE -ne 0) {
        throw "Release rebuild failed before Dreamer positive runtime tests"
    }
}

$engineSource = Get-Content (Join-Path $root "src/Core/Engine.cpp") -Raw
$dreamerSource = Get-Content (Join-Path $root "src/AI/Vision/DreamerService.cpp") -Raw
$diffusionSource = Get-Content (Join-Path $root "src/AI/Vision/DiffusionEngine.cpp") -Raw
Require-Contains $engineSource "Queued startup texture job" `
    "Engine startup Architect command path does not route GenerateTexture to Dreamer"
Require-Contains $engineSource "TextureUsage::Environment" `
    "Engine startup Architect command path does not route GenerateEnvmap to Dreamer"
Require-Contains $dreamerSource "DreamerService initialized" `
    "DreamerService initialization log marker missing"
Require-Contains $diffusionSource "RunCPU" `
    "DiffusionEngine CPU fallback path missing"

$commandJson = '{"commands":[{"type":"generate_texture","target":"TemporalLab_RotatingRedSphere","prompt":"blue checker material","usage":"albedo","preset":"blue_checker","width":64,"height":64,"seed":123}]}'

$previousLogDir = $env:CORTEX_LOG_DIR
$previousMock = $env:CORTEX_LLM_MOCK
$previousArchitectJson = $env:CORTEX_ARCHITECT_COMMAND_JSON
$previousDisableDebug = $env:CORTEX_DISABLE_DEBUG_LAYER
$previousDisableDreamer = $env:CORTEX_DISABLE_DREAMER
try {
    $env:CORTEX_LOG_DIR = $LogDir
    $env:CORTEX_LLM_MOCK = "1"
    $env:CORTEX_ARCHITECT_COMMAND_JSON = $commandJson
    $env:CORTEX_DISABLE_DEBUG_LAYER = "1"
    Remove-Item Env:\CORTEX_DISABLE_DREAMER -ErrorAction SilentlyContinue

    Push-Location $exeWorkingDir
    try {
        $output = & $exe "--scene" "temporal_validation" "--llm-mock" "--no-launcher" "--smoke-frames=$SmokeFrames" 2>&1
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
} finally {
    if ($null -eq $previousLogDir) { Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue } else { $env:CORTEX_LOG_DIR = $previousLogDir }
    if ($null -eq $previousMock) { Remove-Item Env:\CORTEX_LLM_MOCK -ErrorAction SilentlyContinue } else { $env:CORTEX_LLM_MOCK = $previousMock }
    if ($null -eq $previousArchitectJson) { Remove-Item Env:\CORTEX_ARCHITECT_COMMAND_JSON -ErrorAction SilentlyContinue } else { $env:CORTEX_ARCHITECT_COMMAND_JSON = $previousArchitectJson }
    if ($null -eq $previousDisableDebug) { Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue } else { $env:CORTEX_DISABLE_DEBUG_LAYER = $previousDisableDebug }
    if ($null -eq $previousDisableDreamer) { Remove-Item Env:\CORTEX_DISABLE_DREAMER -ErrorAction SilentlyContinue } else { $env:CORTEX_DISABLE_DREAMER = $previousDisableDreamer }
}

$outputText = ($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
$outputText | Set-Content -Path (Join-Path $LogDir "dreamer_positive_stdout.log") -Encoding UTF8

if ($exitCode -ne 0) {
    Add-Failure "Dreamer positive runtime exited with code $exitCode"
}

Require-Contains $outputText "After parsing args: enableDreamer=true" `
    "Dreamer was not enabled in the positive runtime"
Require-Contains $outputText "DreamerService initialized" `
    "Dreamer service did not initialize"
Require-Contains $outputText "The Dreamer is online" `
    "Engine did not report Dreamer online"
Require-Contains $outputText "[Dreamer] Queued startup texture job for 'TemporalLab_RotatingRedSphere'" `
    "Startup Architect GenerateTexture command was not routed to Dreamer"
Require-Contains $outputText "DiffusionEngine" `
    "DiffusionEngine did not run or report backend choice"
Require-Contains $outputText "generated texture for 'TemporalLab_RotatingRedSphere'" `
    "Dreamer did not generate the requested texture"
Require-Contains $outputText "Texture created: " `
    "Renderer did not create a GPU texture from Dreamer output"
Require-Contains $outputText "(Dreamer_TemporalLab_RotatingRedSphere)" `
    "Renderer did not name the Dreamer GPU texture for the requested target"
Require-Contains $outputText "[Dreamer] Applied albedo texture to" `
    "Dreamer texture result was not applied to a scene entity"

if ($failures.Count -gt 0) {
    Write-Host "Dreamer positive runtime tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Dreamer positive runtime tests passed." -ForegroundColor Green
Write-Host "  startup=online"
Write-Host "  generated_texture=applied"
Write-Host "  logs=$LogDir"
exit 0
