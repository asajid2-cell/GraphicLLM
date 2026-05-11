param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 360,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "graphics_environment_budget_ui_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run with -NoBuild."
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class CortexEnvironmentBudgetUiSmoke {
    [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr FindWindow(string lpClassName, string lpWindowName);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr GetDlgItem(IntPtr hDlg, int nIDDlgItem);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern IntPtr SendMessage(IntPtr hWnd, int Msg, IntPtr wParam, IntPtr lParam);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll", SetLastError = true)]
    public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint lpdwProcessId);
}
"@

$WM_COMMAND = 0x0111
$BM_CLICK = 0x00F5
$CB_SETCURSEL = 0x014E
$CBN_SELCHANGE = 1

$IDC_GFX_ENV_BUDGET_SELECT = 9214
$IDC_GFX_ENV_SELECT = 9211
$IDC_GFX_ENV_REAPPLY = 9212
$IDC_GFX_ENV_RELOAD_MANIFEST = 9215

$stdoutPath = Join-Path $LogDir "engine_stdout.txt"
$stderrPath = Join-Path $LogDir "engine_stderr.txt"
$arguments = @(
    "--scene", "effects_showcase",
    "--mode=default",
    "--no-llm",
    "--no-dreamer",
    "--no-launcher",
    "--smoke-frames=$SmokeFrames"
)

$oldLogDir = $env:CORTEX_LOG_DIR
$oldOpenSettings = $env:CORTEX_OPEN_GRAPHICS_SETTINGS_ON_STARTUP
$oldDisableDebugLayer = $env:CORTEX_DISABLE_DEBUG_LAYER
$env:CORTEX_LOG_DIR = $LogDir
$env:CORTEX_OPEN_GRAPHICS_SETTINGS_ON_STARTUP = "1"
$env:CORTEX_DISABLE_DEBUG_LAYER = "1"

try {
    $process = Start-Process `
        -FilePath $exe `
        -ArgumentList $arguments `
        -WorkingDirectory (Split-Path -Parent $exe) `
        -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath `
        -PassThru
} finally {
    if ($null -eq $oldLogDir) { Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue } else { $env:CORTEX_LOG_DIR = $oldLogDir }
    if ($null -eq $oldOpenSettings) { Remove-Item Env:\CORTEX_OPEN_GRAPHICS_SETTINGS_ON_STARTUP -ErrorAction SilentlyContinue } else { $env:CORTEX_OPEN_GRAPHICS_SETTINGS_ON_STARTUP = $oldOpenSettings }
    if ($null -eq $oldDisableDebugLayer) { Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue } else { $env:CORTEX_DISABLE_DEBUG_LAYER = $oldDisableDebugLayer }
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) { $script:failures.Add($Message) }

function Wait-GraphicsWindow([System.Diagnostics.Process]$Process, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return [IntPtr]::Zero
        }
        $hwnd = [CortexEnvironmentBudgetUiSmoke]::FindWindow("CortexGraphicsSettingsWindow", "Cortex Graphics Settings")
        if ($hwnd -ne [IntPtr]::Zero -and [CortexEnvironmentBudgetUiSmoke]::IsWindowVisible($hwnd)) {
            [uint32]$windowPid = 0
            [void][CortexEnvironmentBudgetUiSmoke]::GetWindowThreadProcessId($hwnd, [ref]$windowPid)
            if ($windowPid -eq [uint32]$Process.Id) {
                return $hwnd
            }
        }
        Start-Sleep -Milliseconds 100
    }
    return [IntPtr]::Zero
}

function Get-Control([IntPtr]$Parent, [int]$Id) {
    $control = [CortexEnvironmentBudgetUiSmoke]::GetDlgItem($Parent, $Id)
    if ($control -eq [IntPtr]::Zero) {
        throw "Missing graphics UI control id $Id"
    }
    return $control
}

function Click-Control([IntPtr]$Parent, [int]$Id) {
    $control = Get-Control $Parent $Id
    [void][CortexEnvironmentBudgetUiSmoke]::SendMessage($control, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 220
}

function Select-ComboIndex([IntPtr]$Parent, [int]$Id, [int]$Index) {
    $control = Get-Control $Parent $Id
    [void][CortexEnvironmentBudgetUiSmoke]::SendMessage($control, $CB_SETCURSEL, [IntPtr]$Index, [IntPtr]::Zero)
    $wParam = ($CBN_SELCHANGE -shl 16) -bor ($Id -band 0xffff)
    [void][CortexEnvironmentBudgetUiSmoke]::SendMessage($Parent, $WM_COMMAND, [IntPtr]$wParam, $control)
    Start-Sleep -Milliseconds 500
}

try {
    $window = Wait-GraphicsWindow $process 35
    if ($window -eq [IntPtr]::Zero) {
        Add-Failure "Cortex Graphics Settings native window did not appear for process $($process.Id)."
    } else {
        Click-Control $window $IDC_GFX_ENV_RELOAD_MANIFEST
        Select-ComboIndex $window $IDC_GFX_ENV_BUDGET_SELECT 3
        Select-ComboIndex $window $IDC_GFX_ENV_SELECT 2
        Click-Control $window $IDC_GFX_ENV_REAPPLY
    }

    Start-Sleep -Milliseconds 1800
    if (-not $process.WaitForExit(90000)) {
        Add-Failure "environment budget UI smoke timed out; killing process $($process.Id)."
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
    $process.Refresh()
} catch {
    Add-Failure $_.Exception.Message
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}

$reportPath = Join-Path $LogDir "frame_report_shutdown.json"
if (-not (Test-Path $reportPath)) {
    $reportPath = Join-Path $LogDir "frame_report_last.json"
}

if (-not $process.HasExited) {
    Add-Failure "engine process did not exit cleanly"
} elseif ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
    Add-Failure "engine exited with code $($process.ExitCode)"
}
if (-not (Test-Path $reportPath)) {
    Add-Failure "environment budget UI smoke did not write a frame report in $LogDir"
} else {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $fc = $report.frame_contract
    if (-not [bool]$report.ui_state.graphics_settings_open) {
        Add-Failure "ui_state.graphics_settings_open was false"
    }
    if ([string]$fc.environment.active -ne "cool_overcast") {
        Add-Failure "environment active was '$($fc.environment.active)', expected cool_overcast"
    }
    if ([string]$fc.environment.requested -ne "cool_overcast") {
        Add-Failure "environment requested was '$($fc.environment.requested)', expected cool_overcast"
    }
    if ([string]$fc.environment.budget_class -ne "medium") {
        Add-Failure "environment budget_class was '$($fc.environment.budget_class)', expected medium"
    }
    if ([bool]$fc.environment.fallback) {
        Add-Failure "environment unexpectedly reported fallback after budget-filtered selection"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Graphics environment budget UI smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Graphics environment budget UI smoke passed logs=$LogDir" -ForegroundColor Green
