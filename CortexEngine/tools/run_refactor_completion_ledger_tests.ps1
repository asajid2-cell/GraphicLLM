param(
    [string]$LedgerPath = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($LedgerPath)) {
    $LedgerPath = Join-Path $root "docs/REFACTOR_COMPLETION_LEDGER.md"
}

if (-not (Test-Path $LedgerPath)) {
    throw "Refactor completion ledger not found: $LedgerPath"
}

$allowed = @(
    "DONE_VERIFIED",
    "DEFERRED_BY_USER_ONLY"
)
$knownStatuses = @(
    "DONE_VERIFIED",
    "DONE_UNVERIFIED",
    "PARTIAL",
    "NOT_STARTED",
    "BLOCKED",
    "DEFERRED_BY_USER_ONLY"
)

$failures = New-Object System.Collections.Generic.List[string]
$rows = 0
$completeRows = 0
$deferredRows = 0

foreach ($line in Get-Content $LedgerPath) {
    if ($line -notmatch '^\|\s*[^|]+\s*\|') {
        continue
    }

    $columns = $line -split '\|'
    if ($columns.Count -lt 4) {
        continue
    }

    $id = $columns[1].Trim()
    $status = $columns[3].Trim()
    if ($id -in @("---", "ID") -or [string]::IsNullOrWhiteSpace($id)) {
        continue
    }

    if ($knownStatuses -notcontains $status) {
        continue
    }

    ++$rows
    if ($allowed -notcontains $status) {
        $failures.Add("$id has incomplete status $status")
        continue
    }

    if ($status -eq "DONE_VERIFIED") {
        ++$completeRows
    } elseif ($status -eq "DEFERRED_BY_USER_ONLY") {
        ++$deferredRows
    }
}

$ledgerText = Get-Content $LedgerPath -Raw
if ($ledgerText.IndexOf("## Completion Gate", [StringComparison]::Ordinal) -lt 0) {
    $failures.Add("Completion Gate section is missing")
}
if ($ledgerText.IndexOf("Completion Gate Status: SATISFIED", [StringComparison]::Ordinal) -lt 0) {
    $failures.Add("Completion Gate status is not marked SATISFIED")
}

if ($rows -lt 1) {
    $failures.Add("No status rows were parsed from the ledger")
}

if ($failures.Count -gt 0) {
    Write-Host "Refactor completion ledger tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Refactor completion ledger tests passed." -ForegroundColor Green
Write-Host "  rows=$rows done_verified=$completeRows deferred_by_user=$deferredRows"
