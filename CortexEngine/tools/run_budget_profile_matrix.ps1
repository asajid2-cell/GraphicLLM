param(
    [int]$SmokeFrames = 240,
    [int]$TemporalRuns = 1,
    [int]$MaxParallel = 3,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$smokeScript = Join-Path $PSScriptRoot "run_rt_showcase_smoke.ps1"
$matrixId = "budget_matrix_{0}_{1}_{2}" -f `
    (Get-Date -Format "yyyyMMdd_HHmmss_fff"),
    $PID,
    ([Guid]::NewGuid().ToString("N").Substring(0, 8))
$matrixLogDir = Join-Path (Join-Path $root "build/bin/logs/runs") $matrixId

if (-not $NoBuild) {
    cmake --build (Join-Path $root "build") --config Release --target CortexEngine
}

New-Item -ItemType Directory -Force -Path $matrixLogDir | Out-Null

$runs = @(
    [pscustomobject]@{ Name = "default"; Profile = ""; Expected = "" },
    [pscustomobject]@{ Name = "4gb_low"; Profile = "4gb_low"; Expected = "4gb_low" },
    [pscustomobject]@{ Name = "2gb_ultra_low"; Profile = "2gb_ultra_low"; Expected = "2gb_ultra_low" }
)

$running = New-Object System.Collections.Generic.List[object]
$failures = New-Object System.Collections.Generic.List[string]
$maxParallelClamped = [Math]::Max(1, $MaxParallel)

function Start-BudgetSmoke([object]$run) {
    $runLogDir = Join-Path $script:matrixLogDir $run.Name
    New-Item -ItemType Directory -Force -Path $runLogDir | Out-Null

    $stdout = Join-Path $runLogDir "smoke_stdout.txt"
    $stderr = Join-Path $runLogDir "smoke_stderr.txt"
    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $script:smokeScript,
        "-NoBuild",
        "-SmokeFrames", [string]$script:SmokeFrames,
        "-TemporalRuns", [string]$script:TemporalRuns,
        "-SkipSurfaceDebug",
        "-AllowRTCadenceSkips",
        "-LogDir", $runLogDir
    )
    if (-not [string]::IsNullOrWhiteSpace([string]$run.Profile)) {
        $args += @("-RTBudgetProfile", [string]$run.Profile)
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$run.Expected)) {
        $args += @("-ExpectedRTBudgetProfile", [string]$run.Expected)
    }

    $process = Start-Process `
        -FilePath "powershell" `
        -ArgumentList $args `
        -RedirectStandardOutput $stdout `
        -RedirectStandardError $stderr `
        -PassThru `
        -WindowStyle Hidden

    return [pscustomobject]@{
        Run = $run
        Process = $process
        LogDir = $runLogDir
        Stdout = $stdout
        Stderr = $stderr
    }
}

function Invoke-BudgetSmokeDirect([object]$run) {
    $runLogDir = Join-Path $script:matrixLogDir $run.Name
    New-Item -ItemType Directory -Force -Path $runLogDir | Out-Null

    $stdout = Join-Path $runLogDir "smoke_stdout.txt"
    $stderr = Join-Path $runLogDir "smoke_stderr.txt"
    $args = @(
        "-NoProfile",
        "-ExecutionPolicy", "Bypass",
        "-File", $script:smokeScript,
        "-NoBuild",
        "-SmokeFrames", [string]$script:SmokeFrames,
        "-TemporalRuns", [string]$script:TemporalRuns,
        "-SkipSurfaceDebug",
        "-AllowRTCadenceSkips",
        "-LogDir", $runLogDir
    )
    if (-not [string]::IsNullOrWhiteSpace([string]$run.Profile)) {
        $args += @("-RTBudgetProfile", [string]$run.Profile)
    }
    if (-not [string]::IsNullOrWhiteSpace([string]$run.Expected)) {
        $args += @("-ExpectedRTBudgetProfile", [string]$run.Expected)
    }

    $output = & powershell @args 2>&1
    $exitCode = $LASTEXITCODE
    $outputText = ($output | ForEach-Object { $_.ToString() }) -join [Environment]::NewLine
    Set-Content -Path $stdout -Value $outputText -Encoding UTF8
    Set-Content -Path $stderr -Value "" -Encoding UTF8

    return [pscustomobject]@{
        Run = $run
        ExitCode = $exitCode
        LogDir = $runLogDir
        Stdout = $stdout
        Stderr = $stderr
    }
}

function Complete-Smoke([object]$entry) {
    $entry.Process.WaitForExit()
    $entry.Process.Refresh()
    $exitCode = $entry.Process.ExitCode
    $stdoutText = if (Test-Path $entry.Stdout) { Get-Content $entry.Stdout -Raw } else { "" }
    $stderrText = if (Test-Path $entry.Stderr) { Get-Content $entry.Stderr -Raw } else { "" }
    $passedByOutput = $stdoutText.Contains("RT showcase smoke passed")

    if (($null -eq $exitCode -and $passedByOutput -and [string]::IsNullOrWhiteSpace($stderrText)) -or
        ($null -ne $exitCode -and $exitCode -eq 0)) {
        Write-Host "Budget profile '$($entry.Run.Name)' passed; logs=$($entry.LogDir)"
        if (-not [string]::IsNullOrWhiteSpace($stdoutText)) {
            $stdoutText -split "`r?`n" |
                Where-Object {
                    $_ -match "^RT showcase smoke passed" -or
                    $_ -match "^\s+logs=" -or
                    $_ -match "^\s+frames="
                } |
                ForEach-Object { Write-Host $_ }
        }
    } else {
        $script:failures.Add(
            "profile '$($entry.Run.Name)' failed with exit code $exitCode. " +
            "logs=$($entry.LogDir)`n$stderrText`n$stdoutText")
    }
}

function Complete-DirectSmoke([object]$entry) {
    $exitCode = $entry.ExitCode
    $stdoutText = if (Test-Path $entry.Stdout) { Get-Content $entry.Stdout -Raw } else { "" }
    $stderrText = if (Test-Path $entry.Stderr) { Get-Content $entry.Stderr -Raw } else { "" }
    $passedByOutput = $stdoutText.Contains("RT showcase smoke passed")

    if (($null -ne $exitCode -and $exitCode -eq 0) -or
        ($passedByOutput -and [string]::IsNullOrWhiteSpace($stderrText))) {
        Write-Host "Budget profile '$($entry.Run.Name)' passed; logs=$($entry.LogDir)"
        if (-not [string]::IsNullOrWhiteSpace($stdoutText)) {
            $stdoutText -split "`r?`n" |
                Where-Object {
                    $_ -match "^RT showcase smoke passed" -or
                    $_ -match "^\s+logs=" -or
                    $_ -match "^\s+frames="
                } |
                ForEach-Object { Write-Host $_ }
        }
    } else {
        $script:failures.Add(
            "profile '$($entry.Run.Name)' failed with exit code $exitCode. " +
            "logs=$($entry.LogDir)`n$stderrText`n$stdoutText")
    }
}

if ($maxParallelClamped -eq 1) {
    foreach ($run in $runs) {
        Complete-DirectSmoke (Invoke-BudgetSmokeDirect $run)
    }

    if ($failures.Count -gt 0) {
        Write-Host "Budget profile matrix failed:" -ForegroundColor Red
        foreach ($failure in $failures) {
            Write-Host " - $failure" -ForegroundColor Red
        }
        exit 1
    }

    Write-Host "Budget profile matrix passed; logs=$matrixLogDir" -ForegroundColor Green
    exit 0
}

foreach ($run in $runs) {
    while ($running.Count -ge $maxParallelClamped) {
        Start-Sleep -Milliseconds 200
        for ($i = $running.Count - 1; $i -ge 0; --$i) {
            if ($running[$i].Process.HasExited) {
                Complete-Smoke $running[$i]
                $running.RemoveAt($i)
            }
        }
    }
    $running.Add((Start-BudgetSmoke $run))
}

while ($running.Count -gt 0) {
    Start-Sleep -Milliseconds 200
    for ($i = $running.Count - 1; $i -ge 0; --$i) {
        if ($running[$i].Process.HasExited) {
            Complete-Smoke $running[$i]
            $running.RemoveAt($i)
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Budget profile matrix failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Budget profile matrix passed; logs=$matrixLogDir" -ForegroundColor Green
