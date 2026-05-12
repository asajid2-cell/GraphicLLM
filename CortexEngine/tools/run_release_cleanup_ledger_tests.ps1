param()

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
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
    "RC-20",
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

foreach ($id in 1..20) {
    $rowId = "RC-{0:D2}" -f $id
    if ($ledger.IndexOf("| $rowId ", [System.StringComparison]::Ordinal) -lt 0) {
        Add-Failure "ledger missing row $rowId"
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
