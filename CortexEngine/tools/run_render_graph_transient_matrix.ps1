param(
    [string]$LogDir = "",
    [switch]$IsolatedLogs,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$baseLogDir = Join-Path $root "build/bin/logs"
$activeLogDir = $baseLogDir
if (-not [string]::IsNullOrWhiteSpace($LogDir)) {
    $activeLogDir = $LogDir
} elseif ($IsolatedLogs) {
    $runId = "render_graph_transient_matrix_{0}_{1}_{2}" -f `
        (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
        $PID,
        ([Guid]::NewGuid().ToString("N").Substring(0, 8))
    $activeLogDir = Join-Path (Join-Path $baseLogDir "runs") $runId
}

if (-not $NoBuild) {
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $root "rebuild.ps1") -Config Release
}

$smokeScript = Join-Path $PSScriptRoot "run_rt_showcase_smoke.ps1"
$cases = @(
    [pscustomobject]@{
        Name = "aliasing_on_bloom_transients_on"
        DisableAliasing = $false
        DisableBloomTransients = $false
        ExpectAliasing = $true
        ExpectBloomTransients = $true
        ExpectFinalTransients = $true
    },
    [pscustomobject]@{
        Name = "aliasing_off_bloom_transients_on"
        DisableAliasing = $true
        DisableBloomTransients = $false
        ExpectAliasing = $false
        ExpectBloomTransients = $true
        ExpectFinalTransients = $true
    },
    [pscustomobject]@{
        Name = "aliasing_on_bloom_transients_off"
        DisableAliasing = $false
        DisableBloomTransients = $true
        ExpectAliasing = $false
        ExpectBloomTransients = $false
        ExpectFinalTransients = $false
    }
)

$failures = New-Object System.Collections.Generic.List[string]
$summaries = New-Object System.Collections.Generic.List[object]

function Add-Failure([string]$message) {
    $script:failures.Add($message)
}

function Get-FrameContractPass([object]$ReportObject, [string]$Name) {
    if ($null -eq $ReportObject.frame_contract.passes) {
        return $null
    }
    foreach ($pass in $ReportObject.frame_contract.passes) {
        if ([string]$pass.name -eq $Name) {
            return $pass
        }
    }
    return $null
}

function Test-AnyPassWrites([object]$ReportObject, [string]$ResourceName) {
    if ($null -eq $ReportObject.frame_contract.passes) {
        return $false
    }
    foreach ($pass in $ReportObject.frame_contract.passes) {
        if ($null -eq $pass.writes) {
            continue
        }
        foreach ($write in $pass.writes) {
            if ([string]$write -eq $ResourceName) {
                return $true
            }
        }
    }
    return $false
}

New-Item -ItemType Directory -Force -Path $activeLogDir | Out-Null

foreach ($case in $cases) {
    $caseLogDir = Join-Path $activeLogDir $case.Name
    New-Item -ItemType Directory -Force -Path $caseLogDir | Out-Null

    $env:CORTEX_RG_TRANSIENT_VALIDATE = "1"
    $env:CORTEX_RG_HEAP_DUMP = "1"
    if ($case.DisableAliasing) {
        $env:CORTEX_RG_DISABLE_ALIASING = "1"
    } else {
        Remove-Item Env:\CORTEX_RG_DISABLE_ALIASING -ErrorAction SilentlyContinue
    }
    if ($case.DisableBloomTransients) {
        $env:CORTEX_DISABLE_BLOOM_TRANSIENTS = "1"
    } else {
        Remove-Item Env:\CORTEX_DISABLE_BLOOM_TRANSIENTS -ErrorAction SilentlyContinue
    }

    try {
        & powershell -NoProfile -ExecutionPolicy Bypass -File $smokeScript `
            -NoBuild `
            -LogDir $caseLogDir `
            -SkipSurfaceDebug `
            -TemporalRuns 1
        $exitCode = $LASTEXITCODE
    } finally {
        Remove-Item Env:\CORTEX_RG_TRANSIENT_VALIDATE -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_RG_HEAP_DUMP -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_RG_DISABLE_ALIASING -ErrorAction SilentlyContinue
        Remove-Item Env:\CORTEX_DISABLE_BLOOM_TRANSIENTS -ErrorAction SilentlyContinue
    }

    if ($exitCode -ne 0) {
        Add-Failure "$($case.Name): RT showcase smoke failed with exit code $exitCode"
        continue
    }

    $reportPath = Join-Path $caseLogDir "frame_report_last.json"
    if (-not (Test-Path $reportPath)) {
        Add-Failure "$($case.Name): missing frame report at $reportPath"
        continue
    }

    $report = Get-Content $reportPath -Raw | ConvertFrom-Json
    $renderGraph = $report.frame_contract.render_graph
    if ($null -eq $renderGraph) {
        Add-Failure "$($case.Name): frame contract missing render_graph"
        continue
    }

    if (-not [bool]$renderGraph.active) {
        Add-Failure "$($case.Name): render graph was not active"
    }
    if (-not [bool]$renderGraph.transient_validation_ran) {
        Add-Failure "$($case.Name): transient validation pass did not report as run"
    }
    if ([int]$renderGraph.fallback_executions -ne 0) {
        Add-Failure "$($case.Name): render graph fallback executions = $($renderGraph.fallback_executions)"
    }
    if ($case.ExpectFinalTransients) {
        if ([int]$renderGraph.transient_resources -le 0 -or [int]$renderGraph.placed_resources -le 0) {
            Add-Failure "$($case.Name): transient/placed resource counts are not positive"
        }
        if ([uint64]$renderGraph.transient_requested_bytes -le 0 -or [uint64]$renderGraph.transient_heap_used_bytes -le 0) {
            Add-Failure "$($case.Name): transient byte counters are not positive"
        }
        if ([uint64]$renderGraph.transient_heap_size_bytes -lt [uint64]$renderGraph.transient_heap_used_bytes) {
            Add-Failure "$($case.Name): transient heap size is smaller than used bytes"
        }
    } else {
        if ([int]$renderGraph.transient_resources -ne 0 -or [int]$renderGraph.placed_resources -ne 0) {
            Add-Failure "$($case.Name): bloom transients disabled but final transient/placed counts were $($renderGraph.transient_resources)/$($renderGraph.placed_resources)"
        }
        if ([uint64]$renderGraph.transient_requested_bytes -ne 0 -or [uint64]$renderGraph.transient_heap_used_bytes -ne 0) {
            Add-Failure "$($case.Name): bloom transients disabled but final transient bytes were requested=$($renderGraph.transient_requested_bytes) used=$($renderGraph.transient_heap_used_bytes)"
        }
    }
    if ($null -ne $report.frame_contract.pass_budget_summary) {
        $transientDescriptorDelta = [int]$report.frame_contract.pass_budget_summary.transient_descriptor_delta_total
        $particleTransientDelta = 0
        foreach ($entry in @($report.frame_contract.pass_budget_summary.top_transient_descriptor_passes)) {
            if ([string]$entry.name -eq "Particles") {
                $particleTransientDelta += [int]$entry.transient_descriptor_delta
            }
        }
        if ($transientDescriptorDelta -ne $particleTransientDelta) {
            Add-Failure "$($case.Name): unexpected non-particle transient descriptor delta: total=$transientDescriptorDelta particles=$particleTransientDelta"
        }
        if ([bool]$report.frame_contract.particles.gpu_particle_public_path -and $particleTransientDelta -lt 2) {
            Add-Failure "$($case.Name): GPU particle path reported public but particle transient descriptor delta was $particleTransientDelta, expected >= 2"
        }
    }
    if ($report.frame_contract.warnings.Count -gt 0) {
        Add-Failure "$($case.Name): frame contract warnings were reported: $($report.frame_contract.warnings -join ', ')"
    }
    if ($report.health_warnings.Count -gt 0) {
        Add-Failure "$($case.Name): health warnings were reported: $($report.health_warnings -join ', ')"
    }

    if ($case.ExpectAliasing) {
        if ([int]$renderGraph.aliased_resources -le 0) {
            Add-Failure "$($case.Name): expected aliased resources but saw $($renderGraph.aliased_resources)"
        }
        if ([int]$renderGraph.aliasing_barriers -le 0) {
            Add-Failure "$($case.Name): expected aliasing barriers but saw $($renderGraph.aliasing_barriers)"
        }
        if ([uint64]$renderGraph.transient_saved_bytes -le 0) {
            Add-Failure "$($case.Name): expected transient saved bytes but saw $($renderGraph.transient_saved_bytes)"
        }
    } else {
        if ([int]$renderGraph.aliased_resources -ne 0) {
            Add-Failure "$($case.Name): aliasing disabled but aliased_resources=$($renderGraph.aliased_resources)"
        }
        if ([int]$renderGraph.aliasing_barriers -ne 0) {
            Add-Failure "$($case.Name): aliasing disabled but aliasing_barriers=$($renderGraph.aliasing_barriers)"
        }
        if ([uint64]$renderGraph.transient_saved_bytes -ne 0) {
            Add-Failure "$($case.Name): aliasing disabled but transient_saved_bytes=$($renderGraph.transient_saved_bytes)"
        }
    }

    if (-not $case.ExpectBloomTransients) {
        foreach ($resource in @("bloom_downsample_chain", "bloom_upsample_chain")) {
            if (Test-AnyPassWrites $report $resource) {
                Add-Failure "$($case.Name): bloom transient resource '$resource' was written while bloom transients were disabled"
            }
        }
    }

    $summaries.Add([pscustomobject]@{
        Name = $case.Name
        AliasedResources = [int]$renderGraph.aliased_resources
        AliasingBarriers = [int]$renderGraph.aliasing_barriers
        TransientResources = [int]$renderGraph.transient_resources
        HeapUsedBytes = [uint64]$renderGraph.transient_heap_used_bytes
        SavedBytes = [uint64]$renderGraph.transient_saved_bytes
        LogDir = $caseLogDir
    })
}

if ($failures.Count -gt 0) {
    Write-Host "Render graph transient matrix failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    Write-Host "logs=$activeLogDir"
    exit 1
}

$summaryPath = Join-Path $activeLogDir "render_graph_transient_matrix_summary.json"
$summaries | ConvertTo-Json -Depth 4 | Set-Content -Encoding UTF8 $summaryPath

Write-Host "Render graph transient matrix passed." -ForegroundColor Green
foreach ($summary in $summaries) {
    Write-Host ("  {0}: transients={1} aliased={2} barriers={3} heap_used={4} saved={5}" -f `
        $summary.Name,
        $summary.TransientResources,
        $summary.AliasedResources,
        $summary.AliasingBarriers,
        $summary.HeapUsedBytes,
        $summary.SavedBytes)
}
Write-Host "  logs=$activeLogDir"
