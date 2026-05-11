param()

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$repoRoot = Resolve-Path (Join-Path $root "..")
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

$gitignorePath = Join-Path $repoRoot ".gitignore"
if (-not (Test-Path $gitignorePath)) {
    Add-Failure "Missing repository .gitignore"
} else {
    $gitignore = Get-Content $gitignorePath -Raw
    foreach ($pattern in @(
        "CortexEngine/build/",
        "CortexEngine/vcpkg_installed/",
        "CortexEngine/build/bin/",
        "*.log",
        "glTF-Sample-Models/",
        "TensorRT/"
    )) {
        if ($gitignore -notmatch [regex]::Escape($pattern)) {
            Add-Failure ".gitignore missing required pattern: $pattern"
        }
    }
}

Push-Location $repoRoot
try {
    $diffCheck = & git -c core.autocrlf=false diff --check --ignore-submodules=all 2>&1
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "git diff --check failed:`n$($diffCheck -join [Environment]::NewLine)"
    }

    $trackedGenerated = & git ls-files `
        "CortexEngine/build" `
        "CortexEngine/build/bin/logs" `
        "CortexEngine/vcpkg_installed" `
        ".vs" `
        "CortexEngine/.vs" 2>&1
    if ($LASTEXITCODE -ne 0) {
        Add-Failure "git ls-files generated-artifact scan failed: $($trackedGenerated -join ' ')"
    } elseif ($trackedGenerated.Count -gt 0) {
        Add-Failure "Generated build/cache artifacts are tracked:`n$($trackedGenerated -join [Environment]::NewLine)"
    }
} finally {
    Pop-Location
}

if ($failures.Count -gt 0) {
    Write-Host "Repository hygiene tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Repository hygiene tests passed." -ForegroundColor Green
Write-Host "  diff_check=passed"
Write-Host "  generated_artifacts_tracked=0"
Write-Host "  gitignore=build/log/model guards present"
