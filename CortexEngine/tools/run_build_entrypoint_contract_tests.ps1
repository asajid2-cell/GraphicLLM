param()

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

function Read-Text([string]$RelativePath) {
    $path = Join-Path $root $RelativePath
    if (-not (Test-Path $path)) {
        Add-Failure "Missing required file: $RelativePath"
        return ""
    }
    return Get-Content $path -Raw
}

function Assert-Matches([string]$Name, [string]$Text, [string]$Pattern) {
    if ($Text -notmatch $Pattern) {
        Add-Failure "$Name does not match required pattern: $Pattern"
    }
}

function Assert-NotMatches([string]$Name, [string]$Text, [string]$Pattern) {
    if ($Text -match $Pattern) {
        Add-Failure "$Name matches forbidden pattern: $Pattern"
    }
}

$rebuild = Read-Text "rebuild.ps1"
$setup = Read-Text "setup.ps1"
$releaseValidation = Read-Text "tools/run_release_validation.ps1"
$buildDoc = Read-Text "BUILD.md"

Assert-Matches "rebuild.ps1" $rebuild "Get-Command\s+cl"
Assert-Matches "rebuild.ps1" $rebuild "vswhere\.exe"
Assert-Matches "rebuild.ps1" $rebuild "VsDevCmd\.bat"
Assert-Matches "rebuild.ps1" $rebuild "cmake\s+--build"
Assert-NotMatches "rebuild.ps1" $rebuild "(?im)^\s*&?\s*ninja(\.exe)?\b"

Assert-Matches "setup.ps1" $setup "VsDevCmd\.bat"
Assert-Matches "setup.ps1" $setup "CMAKE_MAKE_PROGRAM=.*ninja"
Assert-Matches "setup.ps1" $setup "Visual Studio environment imported"

Assert-Matches "run_release_validation.ps1" $releaseValidation "build_release"
Assert-Matches "run_release_validation.ps1" $releaseValidation "rebuild\.ps1"
Assert-Matches "run_release_validation.ps1" $releaseValidation "release_validation_summary\.json"
Assert-Matches "run_release_validation.ps1" $releaseValidation "Write-ReleaseSummary"
Assert-NotMatches "run_release_validation.ps1" $releaseValidation "(?im)^\s*&?\s*ninja(\.exe)?\b"

Assert-Matches "BUILD.md" $buildDoc "cmake\s+--build\s+build\s+--config\s+Release"
Assert-Matches "BUILD.md" $buildDoc "Using the Automation Scripts"

if ($failures.Count -gt 0) {
    Write-Host "Build entrypoint contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Build entrypoint contract tests passed." -ForegroundColor Green
Write-Host "  rebuild=vsdevcmd+cmake-build"
Write-Host "  release_validation=rebuild.ps1"
Write-Host "  raw_ninja=not used by rebuild/release"
