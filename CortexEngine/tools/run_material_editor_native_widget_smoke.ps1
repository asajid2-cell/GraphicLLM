param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 480,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "material_editor_native_widget_{0}_{1}_{2}" -f `
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

public static class CortexWin32SceneEditorSmoke {
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
$WM_HSCROLL = 0x0114
$BM_CLICK = 0x00F5
$CB_SETCURSEL = 0x014E
$CBN_SELCHANGE = 1
$TBM_SETPOS = 0x0405
$TB_ENDTRACK = 8

$IDC_SE_FOCUSED_MAT_PRESET = 3204
$IDC_SE_FOCUSED_MET_SLIDER = 3206
$IDC_SE_FOCUSED_ROUGH_SLIDER = 3208
$IDC_SE_APPLY_MATERIAL = 3211

$focusTarget = "MaterialLab_PlasticSphere"
$stdoutPath = Join-Path $LogDir "engine_stdout.txt"
$stderrPath = Join-Path $LogDir "engine_stderr.txt"
$arguments = @(
    "--scene", "material_lab",
    "--mode=default",
    "--llm-mock",
    "--no-dreamer",
    "--no-launcher",
    "--smoke-frames=$SmokeFrames"
)

$oldLogDir = $env:CORTEX_LOG_DIR
$oldOpenEditor = $env:CORTEX_OPEN_SCENE_EDITOR_ON_STARTUP
$oldFocus = $env:CORTEX_FOCUS_TARGET
$oldDisableDebugLayer = $env:CORTEX_DISABLE_DEBUG_LAYER
$env:CORTEX_LOG_DIR = $LogDir
$env:CORTEX_OPEN_SCENE_EDITOR_ON_STARTUP = "1"
$env:CORTEX_FOCUS_TARGET = $focusTarget
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
    if ($null -eq $oldOpenEditor) { Remove-Item Env:\CORTEX_OPEN_SCENE_EDITOR_ON_STARTUP -ErrorAction SilentlyContinue } else { $env:CORTEX_OPEN_SCENE_EDITOR_ON_STARTUP = $oldOpenEditor }
    if ($null -eq $oldFocus) { Remove-Item Env:\CORTEX_FOCUS_TARGET -ErrorAction SilentlyContinue } else { $env:CORTEX_FOCUS_TARGET = $oldFocus }
    if ($null -eq $oldDisableDebugLayer) { Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue } else { $env:CORTEX_DISABLE_DEBUG_LAYER = $oldDisableDebugLayer }
}

$failures = New-Object System.Collections.Generic.List[string]
function Add-Failure([string]$Message) { $script:failures.Add($Message) }
function Assert-Near([string]$Name, [double]$Actual, [double]$Expected, [double]$Tolerance) {
    if ([Math]::Abs($Actual - $Expected) -gt $Tolerance) {
        Add-Failure "$Name=$Actual expected $Expected +/- $Tolerance"
    }
}

function Wait-SceneEditorWindow([System.Diagnostics.Process]$Process, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return [IntPtr]::Zero
        }
        $hwnd = [CortexWin32SceneEditorSmoke]::FindWindow("CortexSceneEditorWindow", "Cortex Scene Editor")
        if ($hwnd -ne [IntPtr]::Zero -and [CortexWin32SceneEditorSmoke]::IsWindowVisible($hwnd)) {
            [uint32]$windowPid = 0
            [void][CortexWin32SceneEditorSmoke]::GetWindowThreadProcessId($hwnd, [ref]$windowPid)
            if ($windowPid -eq [uint32]$Process.Id) {
                return $hwnd
            }
        }
        Start-Sleep -Milliseconds 100
    }
    return [IntPtr]::Zero
}

function Get-Control([IntPtr]$Parent, [int]$Id) {
    $control = [CortexWin32SceneEditorSmoke]::GetDlgItem($Parent, $Id)
    if ($control -eq [IntPtr]::Zero) {
        throw "Missing scene editor control id $Id"
    }
    return $control
}

function Set-ComboSelection([IntPtr]$Parent, [int]$Id, [int]$Index) {
    $control = Get-Control $Parent $Id
    [void][CortexWin32SceneEditorSmoke]::SendMessage($control, $CB_SETCURSEL, [IntPtr]$Index, [IntPtr]::Zero)
    $wParam = (($CBN_SELCHANGE -shl 16) -bor $Id)
    [void][CortexWin32SceneEditorSmoke]::SendMessage($Parent, $WM_COMMAND, [IntPtr]$wParam, $control)
    Start-Sleep -Milliseconds 160
}

function Set-Trackbar([IntPtr]$Parent, [int]$Id, [int]$Position) {
    $control = Get-Control $Parent $Id
    [void][CortexWin32SceneEditorSmoke]::SendMessage($control, $TBM_SETPOS, [IntPtr]1, [IntPtr]$Position)
    [void][CortexWin32SceneEditorSmoke]::SendMessage($Parent, $WM_HSCROLL, [IntPtr]$TB_ENDTRACK, $control)
    Start-Sleep -Milliseconds 120
}

function Click-Control([IntPtr]$Parent, [int]$Id) {
    $control = Get-Control $Parent $Id
    [void][CortexWin32SceneEditorSmoke]::SendMessage($control, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 220
}

try {
    $window = Wait-SceneEditorWindow $process 35
    if ($window -eq [IntPtr]::Zero) {
        Add-Failure "Cortex Scene Editor native window did not appear for process $($process.Id)."
    } else {
        Set-ComboSelection $window $IDC_SE_FOCUSED_MAT_PRESET 1
        Set-Trackbar $window $IDC_SE_FOCUSED_MET_SLIDER 88
        Set-Trackbar $window $IDC_SE_FOCUSED_ROUGH_SLIDER 23
        Click-Control $window $IDC_SE_APPLY_MATERIAL
    }

    Start-Sleep -Milliseconds 3000
    if (-not $process.WaitForExit(90000)) {
        Add-Failure "material editor native widget smoke timed out; killing process $($process.Id)."
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
    Add-Failure "material editor native widget smoke did not write a frame report in $LogDir"
} else {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    if (-not [bool]$report.ui_state.scene_editor_open) {
        Add-Failure "ui_state.scene_editor_open was false"
    }
    if ([string]$report.editor_state.focus_target -ne $focusTarget) {
        Add-Failure "editor_state.focus_target was '$($report.editor_state.focus_target)', expected $focusTarget"
    }
    if ($null -eq $report.editor_state.focused_material) {
        Add-Failure "editor_state.focused_material was missing"
    } else {
        $mat = $report.editor_state.focused_material
        if ([string]$mat.tag -ne $focusTarget) {
            Add-Failure "focused material tag was '$($mat.tag)', expected $focusTarget"
        }
        if ([string]$mat.preset -ne "chrome") {
            Add-Failure "focused material preset was '$($mat.preset)', expected chrome"
        }
        Assert-Near "focused material metallic" ([double]$mat.metallic) 0.88 0.04
        Assert-Near "focused material roughness" ([double]$mat.roughness) 0.23 0.04
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Material editor native widget smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Material editor native widget smoke passed logs=$LogDir" -ForegroundColor Green
