param(
    [string]$ShowcasePath = "",
    [switch]$RuntimeSmoke,
    [switch]$NoBuild,
    [int]$SmokeFrames = 90,
    [string]$LogDir = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($ShowcasePath)) {
    $ShowcasePath = Join-Path $root "assets/config/showcase_scenes.json"
}
if ([string]::IsNullOrWhiteSpace($LogDir)) {
    $runId = "showcase_scene_contract_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $LogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $runId
}

$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Test-NumberArray3([object]$Value) {
    if ($null -eq $Value -or $Value.Count -ne 3) {
        return $false
    }
    foreach ($component in $Value) {
        if (-not ($component -is [ValueType])) {
            return $false
        }
    }
    return $true
}

function Assert-Bookmark([object]$Bookmark, [string]$SceneId) {
    $id = [string]$Bookmark.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        Add-Failure "$SceneId has a camera bookmark with no id"
        return
    }
    if (-not (Test-NumberArray3 $Bookmark.position)) {
        Add-Failure "$SceneId.$id position must be a numeric [x,y,z] array"
    }
    if (-not (Test-NumberArray3 $Bookmark.target)) {
        Add-Failure "$SceneId.$id target must be a numeric [x,y,z] array"
    }
    $fov = [double]$Bookmark.fov
    if ($fov -lt 1.0 -or $fov -gt 179.0) {
        Add-Failure "$SceneId.$id fov=$fov outside [1,179]"
    }
}

if (-not (Test-Path $ShowcasePath)) {
    throw "Showcase scene config not found: $ShowcasePath"
}

$doc = Get-Content $ShowcasePath -Raw | ConvertFrom-Json
if ([int]$doc.schema -ne 1) {
    Add-Failure "showcase scene schema must be 1"
}
if ($null -eq $doc.scenes -or $doc.scenes.Count -lt 1) {
    Add-Failure "showcase scene list is empty"
}

$sceneIds = @{}
foreach ($scene in $doc.scenes) {
    $sceneId = [string]$scene.id
    if ([string]::IsNullOrWhiteSpace($sceneId)) {
        Add-Failure "showcase scene id is missing"
        continue
    }
    if ($sceneIds.ContainsKey($sceneId)) {
        Add-Failure "duplicate showcase scene id '$sceneId'"
    }
    $sceneIds[$sceneId] = $true

    if ([string]::IsNullOrWhiteSpace([string]$scene.display_name)) {
        Add-Failure "$sceneId display_name is missing"
    }
    if ([string]::IsNullOrWhiteSpace([string]$scene.default_environment)) {
        Add-Failure "$sceneId default_environment is missing"
    }
    if ([string]::IsNullOrWhiteSpace([string]$scene.default_lighting_rig)) {
        Add-Failure "$sceneId default_lighting_rig is missing"
    }
    if ($null -eq $scene.required_bookmarks -or $scene.required_bookmarks.Count -lt 1) {
        Add-Failure "$sceneId required_bookmarks is empty"
    }

    $implemented = ([string]$scene.current_status) -match "implemented|public"
    if ($implemented) {
        if ($null -eq $scene.camera_bookmarks -or $scene.camera_bookmarks.Count -lt 1) {
            Add-Failure "$sceneId is implemented but has no camera_bookmarks"
        } else {
            $bookmarkIds = @{}
            foreach ($bookmark in $scene.camera_bookmarks) {
                Assert-Bookmark $bookmark $sceneId
                $bookmarkIds[[string]$bookmark.id] = $true
            }
            foreach ($required in $scene.required_bookmarks) {
                $requiredId = [string]$required
                if (-not $bookmarkIds.ContainsKey($requiredId)) {
                    Add-Failure "$sceneId missing required camera bookmark '$requiredId'"
                }
            }
        }

        if ($null -ne $scene.polish_gaps -and $scene.polish_gaps.Count -gt 0) {
            foreach ($gap in $scene.polish_gaps) {
                Add-Failure "$sceneId still lists a public-release polish gap: $gap"
            }
        }
    }
}

if ($RuntimeSmoke -and $failures.Count -eq 0) {
    $exe = Join-Path $root "build/bin/CortexEngine.exe"
    if (-not $NoBuild) {
        cmake --build (Join-Path $root "build") --config Release --target CortexEngine
    }
    if (-not (Test-Path $exe)) {
        Add-Failure "CortexEngine executable not found at $exe. Build Release first or run with -NoBuild."
    } else {
        New-Item -ItemType Directory -Force -Path $LogDir | Out-Null
        $env:CORTEX_LOG_DIR = $LogDir
        $env:CORTEX_DISABLE_DEBUG_LAYER = "1"

        Push-Location (Split-Path -Parent $exe)
        try {
            $output = & $exe `
                "--scene" "rt_showcase" `
                "--camera-bookmark" "hero" `
                "--mode=default" `
                "--no-llm" `
                "--no-dreamer" `
                "--no-launcher" `
                "--smoke-frames=$SmokeFrames" 2>&1
            $exitCode = $LASTEXITCODE
            $output | Set-Content -Encoding UTF8 (Join-Path $LogDir "engine_stdout.txt")
        } finally {
            Pop-Location
            Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
            Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
        }

        $reportPath = Join-Path $LogDir "frame_report_last.json"
        if (-not (Test-Path $reportPath)) {
            $reportPath = Join-Path $LogDir "frame_report_shutdown.json"
        }

        if ($exitCode -ne 0) {
            Add-Failure "runtime showcase bookmark smoke failed with exit code $exitCode. logs=$LogDir"
        } elseif (-not (Test-Path $reportPath)) {
            Add-Failure "runtime showcase bookmark smoke did not write a frame report. logs=$LogDir"
        } else {
            $report = Get-Content $reportPath -Raw | ConvertFrom-Json
            if ([string]$report.scene -ne "rt_showcase") {
                Add-Failure "runtime scene was '$($report.scene)', expected rt_showcase"
            }
            if ([string]$report.camera.bookmark -ne "hero") {
                Add-Failure "runtime camera bookmark was '$($report.camera.bookmark)', expected hero"
            }
            $dx = [Math]::Abs([double]$report.camera.position.x - (-14.0))
            $dy = [Math]::Abs([double]$report.camera.position.y - 2.05)
            $dz = [Math]::Abs([double]$report.camera.position.z - (-6.8))
            if (($dx + $dy + $dz) -gt 0.02) {
                Add-Failure "runtime camera hero position drifted: x=$($report.camera.position.x) y=$($report.camera.position.y) z=$($report.camera.position.z)"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Showcase scene contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Showcase scene contract tests passed: scenes=$($doc.scenes.Count)" -ForegroundColor Green
if ($RuntimeSmoke) {
    Write-Host "logs=$LogDir"
}
