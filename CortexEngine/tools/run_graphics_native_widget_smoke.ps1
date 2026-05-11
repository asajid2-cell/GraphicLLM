param(
    [switch]$NoBuild,
    [int]$SmokeFrames = 600,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$exe = Join-Path $root "build/bin/CortexEngine.exe"
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "graphics_native_widget_{0}_{1}_{2}" -f `
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

public static class CortexWin32UiSmoke {
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

$WM_HSCROLL = 0x0114
$WM_COMMAND = 0x0111
$TBM_SETPOS = 0x0405
$TB_ENDTRACK = 8
$BM_CLICK = 0x00F5
$CB_SETCURSEL = 0x014E
$CBN_SELCHANGE = 1

$IDC_GFX_RENDER_SCALE = 9010
$IDC_GFX_ENV_SELECT = 9211
$IDC_GFX_ENV_REAPPLY = 9212
$IDC_GFX_SSR_STRENGTH = 9045
$IDC_GFX_FOG_HEIGHT = 9040
$IDC_GFX_WATER_ROUGHNESS = 9046
$IDC_GFX_PARTICLE_DENSITY = 9028
$IDC_GFX_PARTICLE_QUALITY = 9064
$IDC_GFX_PARTICLE_BLOOM = 9065
$IDC_GFX_PARTICLE_SOFT_DEPTH = 9066
$IDC_GFX_PARTICLE_WIND = 9067
$IDC_GFX_RT_REFL_STRENGTH = 9034
$IDC_GFX_MOTION_BLUR = 9058
$IDC_GFX_DOF = 9059
$IDC_GFX_GRADE_COOL_MOON = 9056
$IDC_GFX_TONE_PUNCHY = 9063
$IDC_GFX_RIG_LANTERNS = 9032

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
        $hwnd = [CortexWin32UiSmoke]::FindWindow("CortexGraphicsSettingsWindow", "Cortex Graphics Settings")
        if ($hwnd -ne [IntPtr]::Zero -and [CortexWin32UiSmoke]::IsWindowVisible($hwnd)) {
            [uint32]$windowPid = 0
            [void][CortexWin32UiSmoke]::GetWindowThreadProcessId($hwnd, [ref]$windowPid)
            if ($windowPid -eq [uint32]$Process.Id) {
                return $hwnd
            }
        }
        Start-Sleep -Milliseconds 100
    }
    return [IntPtr]::Zero
}

function Get-Control([IntPtr]$Parent, [int]$Id) {
    $control = [CortexWin32UiSmoke]::GetDlgItem($Parent, $Id)
    if ($control -eq [IntPtr]::Zero) {
        throw "Missing graphics UI control id $Id"
    }
    return $control
}

function Set-Trackbar([IntPtr]$Parent, [int]$Id, [int]$Position) {
    $control = Get-Control $Parent $Id
    [void][CortexWin32UiSmoke]::SendMessage($control, $TBM_SETPOS, [IntPtr]1, [IntPtr]$Position)
    [void][CortexWin32UiSmoke]::SendMessage($Parent, $WM_HSCROLL, [IntPtr]$TB_ENDTRACK, $control)
    Start-Sleep -Milliseconds 120
}

function Click-Control([IntPtr]$Parent, [int]$Id) {
    $control = Get-Control $Parent $Id
    [void][CortexWin32UiSmoke]::SendMessage($control, $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 160
}

function Select-ComboIndex([IntPtr]$Parent, [int]$Id, [int]$Index) {
    $control = Get-Control $Parent $Id
    [void][CortexWin32UiSmoke]::SendMessage($control, $CB_SETCURSEL, [IntPtr]$Index, [IntPtr]::Zero)
    $wParam = ($CBN_SELCHANGE -shl 16) -bor ($Id -band 0xffff)
    [void][CortexWin32UiSmoke]::SendMessage($Parent, $WM_COMMAND, [IntPtr]$wParam, $control)
    Start-Sleep -Milliseconds 350
}

try {
    $window = Wait-GraphicsWindow $process 35
    if ($window -eq [IntPtr]::Zero) {
        Add-Failure "Cortex Graphics Settings native window did not appear for process $($process.Id)."
    } else {
        Set-Trackbar $window $IDC_GFX_RENDER_SCALE 68
        Set-Trackbar $window $IDC_GFX_SSR_STRENGTH 72
        Set-Trackbar $window $IDC_GFX_FOG_HEIGHT 53
        Set-Trackbar $window $IDC_GFX_WATER_ROUGHNESS 41
        Set-Trackbar $window $IDC_GFX_PARTICLE_DENSITY 61
        Set-Trackbar $window $IDC_GFX_PARTICLE_QUALITY 66
        Set-Trackbar $window $IDC_GFX_PARTICLE_BLOOM 57
        Set-Trackbar $window $IDC_GFX_PARTICLE_SOFT_DEPTH 42
        Set-Trackbar $window $IDC_GFX_PARTICLE_WIND 48
        Set-Trackbar $window $IDC_GFX_RT_REFL_STRENGTH 62
        Set-Trackbar $window $IDC_GFX_MOTION_BLUR 29
        Set-Trackbar $window $IDC_GFX_DOF 31
        Click-Control $window $IDC_GFX_GRADE_COOL_MOON
        Click-Control $window $IDC_GFX_TONE_PUNCHY
        Click-Control $window $IDC_GFX_RIG_LANTERNS
        Select-ComboIndex $window $IDC_GFX_ENV_SELECT 4
        Click-Control $window $IDC_GFX_ENV_REAPPLY
    }

    Start-Sleep -Milliseconds 2500
    if (-not $process.WaitForExit(90000)) {
        Add-Failure "graphics native widget smoke timed out; killing process $($process.Id)."
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
    Add-Failure "native widget smoke did not write a frame report in $LogDir"
} else {
    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $fc = $report.frame_contract
    if (-not [bool]$report.ui_state.graphics_settings_open) {
        Add-Failure "ui_state.graphics_settings_open was false"
    }
    if (-not [bool]$fc.graphics_preset.dirty_from_ui) {
        Add-Failure "graphics_preset.dirty_from_ui was false after native slider automation"
    }
    Assert-Near "render_scale" ([double]$fc.graphics_preset.render_scale) 0.84 0.04
    Assert-Near "ssr_strength" ([double]$fc.screen_space.ssr_strength) 0.72 0.04
    Assert-Near "fog_height" ([double]$fc.lighting.fog_height) 6.0 1.0
    Assert-Near "water_roughness" ([double]$fc.water.roughness) 0.416 0.05
    Assert-Near "particle_density" ([double]$fc.particles.density_scale) 1.22 0.05
    Assert-Near "particle_quality" ([double]$fc.particles.quality_scale) 1.405 0.06
    Assert-Near "particle_bloom" ([double]$fc.particles.bloom_contribution) 1.14 0.06
    Assert-Near "particle_soft_depth" ([double]$fc.particles.soft_depth_fade) 0.42 0.06
    Assert-Near "particle_wind" ([double]$fc.particles.wind_influence) 0.96 0.06
    Assert-Near "rt_reflection_composition_strength" ([double]$fc.ray_tracing.rt_reflection_tuning.composition_strength) 0.62 0.05
    Assert-Near "motion_blur" ([double]$fc.cinematic_post.motion_blur) 0.29 0.05
    Assert-Near "depth_of_field" ([double]$fc.cinematic_post.depth_of_field) 0.31 0.05
    if ([string]$fc.cinematic_post.color_grade_preset -ne "cool_moon") {
        Add-Failure "color_grade_preset was '$($fc.cinematic_post.color_grade_preset)', expected cool_moon"
    }
    if ([string]$fc.cinematic_post.tone_mapper_preset -ne "punchy") {
        Add-Failure "tone_mapper_preset was '$($fc.cinematic_post.tone_mapper_preset)', expected punchy"
    }
    if ([string]$fc.lighting.rig_id -ne "street_lanterns") {
        Add-Failure "lighting rig_id was '$($fc.lighting.rig_id)', expected street_lanterns"
    }
    if ([string]$fc.lighting.rig_source -ne "renderer_rig") {
        Add-Failure "lighting rig_source was '$($fc.lighting.rig_source)', expected renderer_rig"
    }
    if ([string]$fc.environment.active -ne "cool_overcast") {
        Add-Failure "environment active was '$($fc.environment.active)', expected cool_overcast"
    }
    if ([string]$fc.environment.requested -ne "cool_overcast") {
        Add-Failure "environment requested was '$($fc.environment.requested)', expected cool_overcast"
    }
    if (-not [bool]$fc.environment.loaded) {
        Add-Failure "environment.loaded was false after native dropdown selection"
    }
    if ([bool]$fc.environment.fallback) {
        Add-Failure "environment unexpectedly reported fallback after native dropdown selection"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Graphics native widget smoke failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$LogDir" -ForegroundColor Red
    exit 1
}

Write-Host "Graphics native widget smoke passed logs=$LogDir" -ForegroundColor Green
