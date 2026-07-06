param(
    [Parameter(Mandatory = $true)][string]$Tag,
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Continue'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $root

$artifactRoot = Join-Path $root 'artifacts\genscene_acceptance'
$dir = Join-Path $artifactRoot $Tag
$currentMd = Join-Path $root 'CURRENT.md'
$failedMd = Join-Path $root 'CURRENT_FAILED.md'
New-Item -ItemType Directory -Force $dir | Out-Null

$results = [ordered]@{}
$hardFail = $false

function Report {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    $status = 'FAIL'
    if ($Ok) { $status = 'PASS' }
    $script:results[$Name] = @{ ok = $Ok; detail = $Detail }
    Write-Host ("[{0}] {1}  {2}" -f $status, $Name.PadRight(22), $Detail)
    if (-not $Ok) { $script:hardFail = $true }
}

function Find-VsDevCmd {
    $candidates = @(
        'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files\Microsoft Visual Studio\18\Insiders\Common7\Tools\VsDevCmd.bat',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat'
    )
    foreach ($candidate in $candidates) {
        if (Test-Path $candidate) { return $candidate }
    }
    return $null
}

$prevTag = ''
if (Test-Path $currentMd) {
    $m = Select-String -Path $currentMd -Pattern 'Accepted tag:\s*`([^`]+)`' | Select-Object -First 1
    if ($m) { $prevTag = $m.Matches[0].Groups[1].Value }
}

$statusFile = Join-Path $dir 'git_status.txt'
git status --porcelain *> $statusFile
$statusLines = @(Get-Content $statusFile)
$cleanTree = $statusLines.Count -eq 0
Report 'clean_tree' $cleanTree $statusFile

$diffStat = Join-Path $dir 'git_diff_stat.txt'
git diff --stat *> $diffStat

$graphicsDiff = git diff -- tools/scene_graphics_gate.py
$addedMissingGate = $false
foreach ($line in $graphicsDiff) {
    if ($line -match '^\+.*missing_') { $addedMissingGate = $true }
}
$gateRatchetDetail = 'no added missing_* hard gates in scene_graphics_gate.py'
if ($addedMissingGate) { $gateRatchetDetail = 'added missing_* hard-gate lines detected in scene_graphics_gate.py' }
Report 'gate_ratchet_freeze' (-not $addedMissingGate) $gateRatchetDetail

$pyLog = Join-Path $dir 'python_compile.log'
$pyFiles = @(
    'tools\scene_gen.py',
    'tools\scene_compiler.py',
    'tools\scene_quality_gate.py',
    'tools\scene_graphics_gate.py'
)
if (Test-Path 'tools\curate_gallery.py') {
    $pyFiles += 'tools\curate_gallery.py'
}
python -m py_compile @pyFiles *> $pyLog
Report 'python_compile' ($LASTEXITCODE -eq 0) $pyLog

$curationLog = Join-Path $dir 'curation_gate.log'
$curationOk = $true
$curationDetails = @()
try {
    $manifest = Join-Path $root 'docs\media\genscene\manifest.json'
    if (-not (Test-Path $manifest)) {
        $curationOk = $false
        $curationDetails += 'missing docs/media/genscene/manifest.json'
    } else {
        $manifestData = Get-Content -LiteralPath $manifest -Raw | ConvertFrom-Json
        if ($null -eq $manifestData.entries) {
            $curationOk = $false
            $curationDetails += 'manifest missing entries array'
        }
    }

    if (Test-Path 'tools\curate_gallery.py') {
        $probeSrc = 'docs\media\rt_showcase_hero.png'
        $badOut = & python tools\curate_gallery.py --src $probeSrc --id aaa_loop_bad --prompt 'curation verifier probe' --dry-run 2>&1
        $badCode = $LASTEXITCODE
        $goodOut = & python tools\curate_gallery.py --src $probeSrc --id genscene_verifier_probe --prompt 'curation verifier probe' --dry-run 2>&1
        $goodCode = $LASTEXITCODE
        $curationDetails += "bad_id_exit=$badCode"
        $curationDetails += "good_dry_run_exit=$goodCode"
        if ($badCode -eq 0) {
            $curationOk = $false
            $curationDetails += 'bad loop-style id was accepted'
        }
        if ($goodCode -ne 0) {
            $curationOk = $false
            $curationDetails += 'good genscene id dry-run failed'
        }
        $curationDetails += 'bad_id_output=' + (($badOut | Select-Object -Last 2) -join ' ')
        $curationDetails += 'good_output=' + (($goodOut | Select-Object -Last 2) -join ' ')
    } else {
        $curationOk = $false
        $curationDetails += 'missing tools/curate_gallery.py'
    }

    $trackedDebris = @(git ls-files -- 'docs/media/final_art/*' 'docs/media/genscene/tmp/*' 'build/bin/logs/*' 'build_*.log')
    if ($trackedDebris.Count -gt 0) {
        $curationOk = $false
        $curationDetails += 'tracked generated debris: ' + ($trackedDebris -join ', ')
    }

    $ignoreOut = @(git check-ignore docs/media/final_art/model_authored/example.bmp docs/media/genscene/tmp/example.png build_probe.log 2>$null)
    if ($ignoreOut.Count -lt 3) {
        $curationOk = $false
        $curationDetails += 'expected generated debris paths are not all ignored'
    }
} catch {
    $curationOk = $false
    $curationDetails += $_.Exception.Message
}
$curationDetails | Out-File -FilePath $curationLog -Encoding utf8
Report 'curation_gate' $curationOk $curationLog

$graphicsResetLog = Join-Path $dir 'graphics_gate_reset.log'
$graphicsResetOk = $true
$graphicsResetDetails = @()
try {
    $gateTextHits = @(Select-String -Path 'tools\scene_graphics_gate.py' -Pattern 'missing_' -SimpleMatch)
    $graphicsResetDetails += "legacy_missing_prefix_hits=$($gateTextHits.Count)"
    if ($gateTextHits.Count -gt 0) {
        $graphicsResetOk = $false
        $graphicsResetDetails += 'scene_graphics_gate.py still contains legacy missing-prefix hard codes'
    }

    $badIr = 'build\bin\logs\v3_campsite_ridge_test_0_ir.json'
    $badPng = 'build\bin\logs\v3_campsite_ridge_test_0.png'
    $goodBase = 'build\bin\logs\aaa_pipeline_campsite_loop32_0'
    $goodIr = "${goodBase}_ir.json"
    $goodPng = "${goodBase}.png"
    $goodLog = "${goodBase}.out"
    $goodFrame = "${goodBase}_frame_report.json"
    if (-not (Test-Path $badIr) -or -not (Test-Path $badPng)) {
        $graphicsResetOk = $false
        $graphicsResetDetails += 'known-bad graphics fixture is absent'
    } else {
        $badOut = & python tools\scene_graphics_gate.py --prompt 'a foggy mountain campsite beside a purple lake at dawn' --ir $badIr --png $badPng --expect-fail 2>&1
        $badCode = $LASTEXITCODE
        $graphicsResetDetails += "known_bad_expect_fail_exit=$badCode"
        $graphicsResetDetails += 'known_bad_tail=' + (($badOut | Select-Object -Last 4) -join ' ')
        if ($badCode -ne 0) {
            $graphicsResetOk = $false
            $graphicsResetDetails += 'known-bad fixture did not fail under expect-fail'
        }
    }

    if (-not (Test-Path $goodIr) -or -not (Test-Path $goodPng) -or -not (Test-Path $goodFrame)) {
        $graphicsResetOk = $false
        $graphicsResetDetails += 'sidecar-backed good graphics fixture is absent'
    } else {
        $goodOut = & python tools\scene_graphics_gate.py --prompt 'a foggy mountain campsite beside a purple lake at dawn' --ir $goodIr --png $goodPng --log $goodLog --frame-report $goodFrame 2>&1
        $goodCode = $LASTEXITCODE
        $graphicsResetDetails += "good_fixture_exit=$goodCode"
        $graphicsResetDetails += 'good_tail=' + (($goodOut | Select-Object -Last 4) -join ' ')
        if ($goodCode -ne 0) {
            $graphicsResetOk = $false
            $graphicsResetDetails += 'sidecar-backed good fixture did not pass'
        }
    }
} catch {
    $graphicsResetOk = $false
    $graphicsResetDetails += $_.Exception.Message
}
$graphicsResetDetails | Out-File -FilePath $graphicsResetLog -Encoding utf8
Report 'graphics_gate_reset' $graphicsResetOk $graphicsResetLog

$structuralLog = Join-Path $dir 'structural_scene_gate.log'
$structuralOk = $true
$structuralDetails = @()
try {
    $structuralItems = @(
        @{
            name = 'campsite'
            prompt = 'a foggy mountain campsite beside a purple lake at dawn'
            base = 'gen_a_foggy_mountain_campsite_beside_2'
        },
        @{
            name = 'alpine'
            prompt = 'a stormy alpine lake with a small cabin and blue moonlight'
            base = 'gen_a_stormy_alpine_lake_with_a_smal_2'
        },
        @{
            name = 'desert'
            prompt = 'a sunny desert canyon campsite with red rocks and a turquoise river'
            base = 'gen_a_sunny_desert_canyon_campsite_w_2'
        }
    )
    foreach ($item in $structuralItems) {
        $base = Join-Path 'build\bin\logs' $item.base
        $ir = "${base}_ir.json"
        $png = "${base}.png"
        $out = "${base}.out"
        $frame = "${base}_frame_report.json"
        foreach ($requiredPath in @($ir, $png, $out, $frame)) {
            if (-not (Test-Path $requiredPath)) {
                $structuralOk = $false
                $structuralDetails += "$($item.name): absent $requiredPath"
            }
        }
        if ((Test-Path $ir) -and (Test-Path $png)) {
            $qualityOut = & python tools\scene_quality_gate.py --prompt $item.prompt --ir $ir --png $png 2>&1
            $qualityCode = $LASTEXITCODE
            $structuralDetails += "$($item.name): quality_exit=$qualityCode"
            if ($qualityCode -ne 0) {
                $structuralOk = $false
                $structuralDetails += "$($item.name): quality_tail=" + (($qualityOut | Select-Object -Last 4) -join ' ')
            }
        }
        if ((Test-Path $ir) -and (Test-Path $png) -and (Test-Path $out) -and (Test-Path $frame)) {
            $graphicsOut = & python tools\scene_graphics_gate.py --prompt $item.prompt --ir $ir --png $png --log $out --frame-report $frame 2>&1
            $graphicsCode = $LASTEXITCODE
            $structuralDetails += "$($item.name): graphics_exit=$graphicsCode"
            if ($graphicsCode -ne 0) {
                $structuralOk = $false
                $structuralDetails += "$($item.name): graphics_tail=" + (($graphicsOut | Select-Object -Last 4) -join ' ')
            }
            $outText = Get-Content -LiteralPath $out -Raw
            $hasTerrain = $outText.Contains('structural terrain-water base terrain=shared_fbm_heightfield')
            $hasWater = $outText.Contains('structural water surface component=WaterSurfaceComponent')
            $structuralDetails += "$($item.name): structural_terrain_receipt=$hasTerrain structural_water_receipt=$hasWater"
            if (-not $hasTerrain -or -not $hasWater) {
                $structuralOk = $false
            }
        }
    }
} catch {
    $structuralOk = $false
    $structuralDetails += $_.Exception.Message
}
$structuralDetails | Out-File -FilePath $structuralLog -Encoding utf8
Report 'structural_scene_gate' $structuralOk $structuralLog

if ($SkipBuild) {
    Report 'release_build' $true 'skipped by -SkipBuild'
} else {
    $buildLog = Join-Path $dir 'release_build.log'
    $vs = Find-VsDevCmd
    if (-not $vs) {
        Report 'release_build' $false 'VsDevCmd.bat not found'
    } else {
        $cmdLine = '"' + $vs + '" -arch=x64 >nul && cmake --build build --config Release'
        & cmd.exe /d /s /c $cmdLine *> $buildLog
        Report 'release_build' ($LASTEXITCODE -eq 0) $buildLog
    }
}

$phase0Policy = 'dirty tree and old overlay-gate changes must not be accepted by assertion'
Report 'phase0_policy' $cleanTree $phase0Policy

$head = (git log -1 --pretty='%h %s' 2>$null)
$dirtyLine = 'CLEAN'
if (-not $cleanTree) {
    $dirtyLine = 'DIRTY: ' + (($statusLines | Select-Object -First 20) -join '; ')
}

$overall = 'GREEN - acceptable checkpoint (commit it now)'
if ($hardFail) { $overall = 'RED - DO NOT ACCEPT. Fix or revert before anything else.' }
$acceptedTag = $Tag
if ($hardFail) { $acceptedTag = $prevTag }

$lines = @(
    '# CURRENT -- machine-generated by the acceptance runner. DO NOT HAND-EDIT.'
    ''
    "Overall: **$overall**"
    ''
    "- Tag: ``$Tag``"
    "- Date: $(Get-Date -Format 'yyyy-MM-dd HH:mm')"
    "- Accepted tag: ``$acceptedTag``"
    "- Previous tag: ``$prevTag``"
    "- HEAD: $head"
    "- Tracked tree: $dirtyLine"
    ''
    '## Gates'
    ''
)
foreach ($key in $results.Keys) {
    $state = 'FAIL'
    if ($results[$key].ok) { $state = 'PASS' }
    $lines += "- **${key}**: $state -- $($results[$key].detail)"
}
$lines += @(
    ''
    '## Residuals'
    ''
    '    State is accepted only when this runner writes GREEN. Red output means fix or revert before new feature work.'
    ''
    '## Regression'
    ''
    "    Previous accepted tag: $prevTag"
    ''
    'Next steps live in `QUEUE.md` and `PLAN.md`. This file is state, nothing else.'
)

$target = $currentMd
if ($hardFail) { $target = $failedMd }
$lines | Out-File -FilePath $target -Encoding utf8
Write-Host ''
Write-Host ('State written to ' + $target)
if ($hardFail) { exit 1 }
exit 0
