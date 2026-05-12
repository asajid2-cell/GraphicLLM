param()

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$repoRoot = Resolve-Path (Join-Path $root "..")
$ledgerPath = Join-Path $root "docs/RELEASE_CLEANUP_COMPLETION_LEDGER.md"
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message) | Out-Null
}

if (-not (Test-Path $ledgerPath)) {
    throw "Release cleanup ledger missing: $ledgerPath"
}

$ledger = Get-Content $ledgerPath -Raw
foreach ($required in @(
    "## Completion Gate",
    "## Ledger Items",
    "RC-01",
    "RC-21",
    "run_public_capture_gallery.ps1",
    "run_public_readme_contract_tests.ps1",
    "run_release_cleanup_ledger_tests.ps1"
)) {
    if ($ledger.IndexOf($required, [System.StringComparison]::OrdinalIgnoreCase) -lt 0) {
        Add-Failure "ledger missing '$required'"
    }
}

$openStatuses = @("NOT_STARTED", "PARTIAL", "DONE_UNVERIFIED", "BLOCKED")
foreach ($status in $openStatuses) {
    if ($ledger -match "\|\s*$status\s*\|") {
        Add-Failure "ledger still contains open status '$status'"
    }
}

foreach ($id in 1..21) {
    $rowId = "RC-{0:D2}" -f $id
    if ($ledger.IndexOf("| $rowId ", [System.StringComparison]::Ordinal) -lt 0) {
        Add-Failure "ledger missing row $rowId"
    }
}

$repoReadme = Join-Path $repoRoot "README.md"
if (-not (Test-Path $repoReadme -PathType Leaf)) {
    Add-Failure "repository root README.md is missing"
}

$trackedRoot = @(& git -C $repoRoot ls-files --cached | ForEach-Object {
    if ($_.IndexOf("/") -lt 0) { $_ }
})
foreach ($forbidden in @(
    "phase2.Md",
    "phase3.md",
    "findingplan.Md",
    "nextphase.md",
    "FLICKERING_FIX_SUMMARY.md",
    "DEVICE_REMOVAL_FIX.md",
    "HOW_TO_BUILD_AND_DEBUG.md",
    "TESTING_README.md",
    "AGENTS.md"
)) {
    if ($trackedRoot -contains $forbidden) {
        Add-Failure "public branch still tracks root scratch/planning file '$forbidden'"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Release cleanup ledger tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Release cleanup ledger tests passed" -ForegroundColor Green
