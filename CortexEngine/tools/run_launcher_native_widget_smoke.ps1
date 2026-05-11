param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 90,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "launcher_native_widget_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

if (-not $NoBuild) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
}
if (-not (Test-Path $exe)) {
    throw "CortexEngine executable not found at $exe. Build Release first or run with -NoBuild."
}

Add-Type -TypeDefinition @"
using System;
using System.Runtime.InteropServices;

public static class CortexLauncherSmoke {
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

$IDC_LAUNCH_GRAPHICS_PRESET = 2008
$IDC_LAUNCH_OK = 2010

$stdoutPath = Join-Path $LogDir "engine_stdout.txt"
$stderrPath = Join-Path $LogDir "engine_stderr.txt"
$arguments = @(
    "--no-llm",
    "--no-dreamer",
    "--smoke-frames=$SmokeFrames"
)

$oldLogDir = $env:CORTEX_LOG_DIR
$oldDisableDebugLayer = $env:CORTEX_DISABLE_DEBUG_LAYER
$env:CORTEX_LOG_DIR = $LogDir
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
    if ($null -eq $oldDisableDebugLayer) { Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue } else { $env:CORTEX_DISABLE_DEBUG_LAYER = $oldDisableDebugLayer }
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) { $script:failures.Add($Message) }
function Assert-Near([string]$Name, [double]$Actual, [double]$Expected, [double]$Tolerance) {
    if ([Math]::Abs($Actual - $Expected) -gt $Tolerance) {
        Add-Failure "$Name=$Actual expected $Expected +/- $Tolerance"
    }
}

function Wait-LauncherWindow([System.Diagnostics.Process]$Process, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return [IntPtr]::Zero
        }
        $hwnd = [CortexLauncherSmoke]::FindWindow("CortexLauncherWindow", $null)
        if ($hwnd -ne [IntPtr]::Zero -and [CortexLauncherSmoke]::IsWindowVisible($hwnd)) {
            [uint32]$windowPid = 0
            [void][CortexLauncherSmoke]::GetWindowThreadProcessId($hwnd, [ref]$windowPid)
            if ($windowPid -eq [uint32]$Process.Id) {
                return $hwnd
            }
        }
        Start-Sleep -Milliseconds 100
    }
    return [IntPtr]::Zero
}

function Get-Control([IntPtr]$Parent, [int]$Id) {
    $control = [CortexLauncherSmoke]::GetDlgItem($Parent, $Id)
    if ($control -eq [IntPtr]::Zero) {
        throw "Missing launcher control id $Id"
    }
    return $control
}

function Select-ComboIndex([IntPtr]$Parent, [int]$Id, [int]$Index) {
    $control = Get-Control $Parent $Id
    [void][CortexLauncherSmoke]::SendMessage($control, $CB_SETCURSEL, [IntPtr]$Index, [IntPtr]::Zero)
    $wParam = (($CBN_SELCHANGE -shl 16) -bor ($Id -band 0xffff))
    [void][CortexLauncherSmoke]::SendMessage($Parent, $WM_COMMAND, [IntPtr]$wParam, $control)
    Start-Sleep -Milliseconds 200
}

function Click-Control([IntPtr]$Parent, [int]$Id) {
    $control = Get-Control $Parent $Id
    [void][CortexLauncherSmoke]::SendMessage($control, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 300
}

try {
    $window = Wait-LauncherWindow $process 35
    if ($window -eq [IntPtr]::Zero) {
        Add-Failure "Cortex launcher native window did not appear for process $($process.Id)."
    } else {
        Select-ComboIndex $window $IDC_LAUNCH_GRAPHICS_PRESET 1
        Click-Control $window $IDC_LAUNCH_OK
    }

    if (-not $process.WaitForExit(120000)) {
        Add-Failure "launcher native widget smoke timed out; killing process $($process.Id)."
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
    Add-Failure "launcher smoke did not write a frame report in $LogDir"
} else {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $fc = $report.frame_contract
    if ([string]$fc.graphics_preset.id -ne "safe_startup") {
        Add-Failure "graphics preset id was '$($fc.graphics_preset.id)', expected safe_startup"
    }
    if ([bool]$fc.graphics_preset.dirty_from_ui) {
        Add-Failure "graphics preset dirty_from_ui was true after launcher preset selection"
    }
    Assert-Near "render_scale" ([double]$fc.graphics_preset.render_scale) 0.67 0.04
    Assert-Near "exposure" ([double]$fc.lighting.exposure) 1.0 0.05
    Assert-Near "background_blur" ([double]$fc.environment.background_blur) 0.25 0.05
    if ([bool]$fc.ray_tracing.enabled) {
        Add-Failure "ray tracing remained enabled after launcher safe_startup preset"
    }
    if ([bool]$fc.ray_tracing.reflections_enabled) {
        Add-Failure "RT reflections remained enabled after launcher safe_startup preset"
    }
    if ([bool]$fc.ray_tracing.gi_enabled) {
        Add-Failure "RT GI remained enabled after launcher safe_startup preset"
    }
    if ([bool]$fc.particles.enabled) {
        Add-Failure "particles remained enabled after launcher safe_startup preset"
    }
    if ([bool]$fc.cinematic_post.enabled) {
        Add-Failure "cinematic post remained enabled after launcher safe_startup preset"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Launcher native widget smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Launcher native widget smoke passed logs=$LogDir" -ForegroundColor Green
