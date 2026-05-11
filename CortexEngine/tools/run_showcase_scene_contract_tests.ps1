param(
    [string]$ShowcasePath = "",
    [string]$VisualBaselinePath = "",
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
if ([string]::IsNullOrWhiteSpace($VisualBaselinePath)) {
    $VisualBaselinePath = Join-Path $root "assets/config/visual_baselines.json"
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

function Get-ValidationBookmark([object]$Scene) {
    foreach ($bookmark in $Scene.camera_bookmarks) {
        if ([bool]$bookmark.validation) {
            return $bookmark
        }
    }
    $requiredId = [string]$Scene.required_bookmarks[0]
    foreach ($bookmark in $Scene.camera_bookmarks) {
        if ([string]$bookmark.id -eq $requiredId) {
            return $bookmark
        }
    }
    return $Scene.camera_bookmarks[0]
}

if (-not (Test-Path $ShowcasePath)) {
    throw "Showcase scene config not found: $ShowcasePath"
}
if (-not (Test-Path $VisualBaselinePath)) {
    throw "Visual baseline config not found: $VisualBaselinePath"
}

$doc = Get-Content $ShowcasePath -Raw | ConvertFrom-Json
$baselineDoc = Get-Content $VisualBaselinePath -Raw | ConvertFrom-Json
if ([int]$doc.schema -ne 1) {
    Add-Failure "showcase scene schema must be 1"
}
if ([int]$baselineDoc.schema -ne 1) {
    Add-Failure "visual baseline schema must be 1"
}
if ($null -eq $doc.scenes -or $doc.scenes.Count -lt 1) {
    Add-Failure "showcase scene list is empty"
}

$sceneIds = @{}
$requiredBaselineByScene = @{}
foreach ($case in $baselineDoc.cases) {
    if ([bool]$case.required) {
        $requiredBaselineByScene[[string]$case.scene] = $case
    }
}
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
    if ($null -eq $scene.required_features -or $scene.required_features.Count -lt 2) {
        Add-Failure "$sceneId must declare at least two public renderer features"
    }

    $implemented = ([string]$scene.current_status) -match "implemented|public"
    if ($implemented) {
        if ([string]$scene.target_status -ne "public_showcase_validated") {
            Add-Failure "$sceneId target_status must be public_showcase_validated"
        }
        if ($null -eq $scene.camera_bookmarks -or $scene.camera_bookmarks.Count -lt 1) {
            Add-Failure "$sceneId is implemented but has no camera_bookmarks"
        } else {
            if ($scene.camera_bookmarks.Count -lt 2) {
                Add-Failure "$sceneId needs a hero and at least one detail/secondary public bookmark"
            }
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

        if (-not $requiredBaselineByScene.ContainsKey($sceneId)) {
            Add-Failure "$sceneId has no required visual baseline case"
        } else {
            $baselineCase = $requiredBaselineByScene[$sceneId]
            if ([string]$baselineCase.camera_bookmark -notin @($scene.required_bookmarks)) {
                Add-Failure "$sceneId visual baseline bookmark '$($baselineCase.camera_bookmark)' is not a required bookmark"
            }
            if ([string]$baselineCase.environment -ne [string]$scene.default_environment) {
                Add-Failure "$sceneId visual baseline environment '$($baselineCase.environment)' does not match default '$($scene.default_environment)'"
            }
            if ([string]$baselineCase.lighting_rig -ne [string]$scene.default_lighting_rig) {
                Add-Failure "$sceneId visual baseline lighting rig '$($baselineCase.lighting_rig)' does not match default '$($scene.default_lighting_rig)'"
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
        foreach ($scene in $doc.scenes) {
            $sceneId = [string]$scene.id
            $implemented = ([string]$scene.current_status) -match "implemented|public"
            if (-not $implemented) {
                continue
            }

            $bookmark = Get-ValidationBookmark $scene
            $bookmarkId = [string]$bookmark.id
            $caseLogDir = Join-Path $LogDir $sceneId
            New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null
            $env:CORTEX_LOG_DIR = $caseLogDir
            $env:CORTEX_DISABLE_DEBUG_LAYER = "1"

            Push-Location (Split-Path -Parent $exe)
            try {
                $output = & $exe `
                    "--scene" $sceneId `
                    "--camera-bookmark" $bookmarkId `
                    "--environment" ([string]$scene.default_environment) `
                    "--mode=default" `
                    "--no-llm" `
                    "--no-dreamer" `
                    "--no-launcher" `
                    "--smoke-frames=$SmokeFrames" 2>&1
                $exitCode = $LASTEXITCODE
                $output | Set-Content -Encoding UTF8 (Join-Path $caseLogDir "engine_stdout.txt")
            } finally {
                Pop-Location
                Remove-Item Env:\CORTEX_LOG_DIR -ErrorAction SilentlyContinue
                Remove-Item Env:\CORTEX_DISABLE_DEBUG_LAYER -ErrorAction SilentlyContinue
            }

            $reportPath = Join-Path $caseLogDir "frame_report_last.json"
            if (-not (Test-Path $reportPath)) {
                $reportPath = Join-Path $caseLogDir "frame_report_shutdown.json"
            }

            if ($exitCode -ne 0) {
                Add-Failure "runtime showcase '$sceneId' failed with exit code $exitCode. logs=$caseLogDir"
                continue
            }
            if (-not (Test-Path $reportPath)) {
                Add-Failure "runtime showcase '$sceneId' did not write a frame report. logs=$caseLogDir"
                continue
            }

            $report = Get-Content $reportPath -Raw | ConvertFrom-Json
            if ([string]$report.scene -ne $sceneId) {
                Add-Failure "runtime scene was '$($report.scene)', expected $sceneId"
            }
            if ([string]$report.camera.bookmark -ne $bookmarkId) {
                Add-Failure "$sceneId runtime camera bookmark was '$($report.camera.bookmark)', expected $bookmarkId"
            }
            if ([string]$report.frame_contract.environment.active -ne [string]$scene.default_environment) {
                Add-Failure "$sceneId runtime environment was '$($report.frame_contract.environment.active)', expected '$($scene.default_environment)'"
            }
            if ([string]$report.frame_contract.lighting.rig_id -ne [string]$scene.default_lighting_rig) {
                Add-Failure "$sceneId runtime lighting rig was '$($report.frame_contract.lighting.rig_id)', expected '$($scene.default_lighting_rig)'"
            }
            if ($report.health_warnings.Count -ne 0) {
                Add-Failure "$sceneId runtime health_warnings is not empty: $($report.health_warnings -join ', ')"
            }
            if ($report.frame_contract.warnings.Count -ne 0) {
                Add-Failure "$sceneId runtime frame_contract warnings is not empty: $($report.frame_contract.warnings -join ', ')"
            }
            if ($null -ne $report.hud -and -not [bool]$report.hud.public_capture_clean) {
                Add-Failure "$sceneId runtime HUD is not public-capture clean"
            }

            $dx = [Math]::Abs([double]$report.camera.position.x - [double]$bookmark.position[0])
            $dy = [Math]::Abs([double]$report.camera.position.y - [double]$bookmark.position[1])
            $dz = [Math]::Abs([double]$report.camera.position.z - [double]$bookmark.position[2])
            if (($dx + $dy + $dz) -gt 0.02) {
                Add-Failure "$sceneId runtime camera '$bookmarkId' position drifted: x=$($report.camera.position.x) y=$($report.camera.position.y) z=$($report.camera.position.z)"
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
