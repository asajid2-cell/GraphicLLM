param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 640,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "graphics_material_controls_{0}_{1}_{2}" -f `
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

public static class CortexGraphicsMaterialSmoke {
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

$IDC_GFX_MATERIAL_PRESET = 9226
$IDC_GFX_MATERIAL_METALLIC = 9227
$IDC_GFX_MATERIAL_ROUGHNESS = 9228
$IDC_GFX_MATERIAL_CLEARCOAT = 9229
$IDC_GFX_MATERIAL_COAT_ROUGHNESS = 9230
$IDC_GFX_MATERIAL_TRANSMISSION = 9231
$IDC_GFX_MATERIAL_EMISSIVE = 9232
$IDC_GFX_MATERIAL_SHEEN = 9233
$IDC_GFX_MATERIAL_SUBSURFACE = 9234
$IDC_GFX_MATERIAL_ANISOTROPY = 9235
$IDC_GFX_MATERIAL_WETNESS = 9236
$IDC_GFX_MATERIAL_EMISSIVE_BLOOM = 9237
$IDC_GFX_MATERIAL_PROCEDURAL = 9238
$IDC_GFX_APPLY_FOCUSED_MATERIAL = 9239

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
$oldOpenSettings = $env:CORTEX_OPEN_GRAPHICS_SETTINGS_ON_STARTUP
$oldFocus = $env:CORTEX_FOCUS_TARGET
$oldDisableDebugLayer = $env:CORTEX_DISABLE_DEBUG_LAYER
$env:CORTEX_LOG_DIR = $LogDir
$env:CORTEX_OPEN_GRAPHICS_SETTINGS_ON_STARTUP = "1"
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
    if ($null -eq $oldOpenSettings) { Remove-Item Env:\CORTEX_OPEN_GRAPHICS_SETTINGS_ON_STARTUP -ErrorAction SilentlyContinue } else { $env:CORTEX_OPEN_GRAPHICS_SETTINGS_ON_STARTUP = $oldOpenSettings }
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

function Wait-GraphicsWindow([System.Diagnostics.Process]$Process, [int]$TimeoutSeconds) {
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        if ($Process.HasExited) {
            return [IntPtr]::Zero
        }
        $hwnd = [CortexGraphicsMaterialSmoke]::FindWindow("CortexGraphicsSettingsWindow", "Cortex Graphics Settings")
        if ($hwnd -ne [IntPtr]::Zero -and [CortexGraphicsMaterialSmoke]::IsWindowVisible($hwnd)) {
            [uint32]$windowPid = 0
            [void][CortexGraphicsMaterialSmoke]::GetWindowThreadProcessId($hwnd, [ref]$windowPid)
            if ($windowPid -eq [uint32]$Process.Id) {
                return $hwnd
            }
        }
        Start-Sleep -Milliseconds 100
    }
    return [IntPtr]::Zero
}

function Get-Control([IntPtr]$Parent, [int]$Id) {
    $control = [CortexGraphicsMaterialSmoke]::GetDlgItem($Parent, $Id)
    if ($control -eq [IntPtr]::Zero) {
        throw "Missing graphics material control id $Id"
    }
    return $control
}

function Set-ComboSelection([IntPtr]$Parent, [int]$Id, [int]$Index) {
    $control = Get-Control $Parent $Id
    [void][CortexGraphicsMaterialSmoke]::SendMessage($control, $CB_SETCURSEL, [IntPtr]$Index, [IntPtr]::Zero)
    $wParam = (($CBN_SELCHANGE -shl 16) -bor $Id)
    [void][CortexGraphicsMaterialSmoke]::SendMessage($Parent, $WM_COMMAND, [IntPtr]$wParam, $control)
    Start-Sleep -Milliseconds 160
}

function Set-Trackbar([IntPtr]$Parent, [int]$Id, [int]$Position) {
    $control = Get-Control $Parent $Id
    [void][CortexGraphicsMaterialSmoke]::SendMessage($control, $TBM_SETPOS, [IntPtr]1, [IntPtr]$Position)
    [void][CortexGraphicsMaterialSmoke]::SendMessage($Parent, $WM_HSCROLL, [IntPtr]$TB_ENDTRACK, $control)
    Start-Sleep -Milliseconds 120
}

function Click-Control([IntPtr]$Parent, [int]$Id) {
    $control = Get-Control $Parent $Id
    [void][CortexGraphicsMaterialSmoke]::SendMessage($control, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 320
}

try {
    $window = Wait-GraphicsWindow $process 35
    if ($window -eq [IntPtr]::Zero) {
        Add-Failure "Cortex Graphics Settings native window did not appear for process $($process.Id)."
    } else {
        Set-ComboSelection $window $IDC_GFX_MATERIAL_PRESET 11
        Set-Trackbar $window $IDC_GFX_MATERIAL_METALLIC 88
        Set-Trackbar $window $IDC_GFX_MATERIAL_ROUGHNESS 23
        Set-Trackbar $window $IDC_GFX_MATERIAL_CLEARCOAT 72
        Set-Trackbar $window $IDC_GFX_MATERIAL_COAT_ROUGHNESS 18
        Set-Trackbar $window $IDC_GFX_MATERIAL_TRANSMISSION 52
        Set-Trackbar $window $IDC_GFX_MATERIAL_EMISSIVE 40
        Set-Trackbar $window $IDC_GFX_MATERIAL_SHEEN 31
        Set-Trackbar $window $IDC_GFX_MATERIAL_SUBSURFACE 27
        Set-Trackbar $window $IDC_GFX_MATERIAL_ANISOTROPY 66
        Set-Trackbar $window $IDC_GFX_MATERIAL_WETNESS 44
        Set-Trackbar $window $IDC_GFX_MATERIAL_EMISSIVE_BLOOM 58
        Set-Trackbar $window $IDC_GFX_MATERIAL_PROCEDURAL 35
        Click-Control $window $IDC_GFX_APPLY_FOCUSED_MATERIAL
    }

    Start-Sleep -Milliseconds 3000
    if (-not $process.WaitForExit(90000)) {
        Add-Failure "graphics material controls smoke timed out; killing process $($process.Id)."
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
    Add-Failure "graphics material controls smoke did not write a frame report in $LogDir"
} else {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    if (-not [bool]$report.ui_state.graphics_settings_open) {
        Add-Failure "ui_state.graphics_settings_open was false"
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
        if ([string]$mat.preset -ne "glass") {
            Add-Failure "focused material preset was '$($mat.preset)', expected glass"
        }
        Assert-Near "focused material metallic" ([double]$mat.metallic) 0.0 0.04
        Assert-Near "focused material roughness" ([double]$mat.roughness) 0.23 0.04
        Assert-Near "focused material clearcoat" ([double]$mat.clearcoat) 0.72 0.04
        Assert-Near "focused material clearcoat_roughness" ([double]$mat.clearcoat_roughness) 0.18 0.04
        Assert-Near "focused material transmission" ([double]$mat.transmission) 0.52 0.04
        Assert-Near "focused material emissive_strength" ([double]$mat.emissive_strength) 6.4 0.18
        Assert-Near "focused material sheen" ([double]$mat.sheen) 0.31 0.04
        Assert-Near "focused material subsurface" ([double]$mat.subsurface) 0.27 0.04
        Assert-Near "focused material anisotropy" ([double]$mat.anisotropy) 0.66 0.04
        Assert-Near "focused material wetness" ([double]$mat.wetness) 0.44 0.04
        Assert-Near "focused material emissive_bloom" ([double]$mat.emissive_bloom) 0.58 0.04
        Assert-Near "focused material procedural_mask" ([double]$mat.procedural_mask) 0.35 0.04
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Graphics material controls smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Graphics material controls smoke passed logs=$LogDir" -ForegroundColor Green
