param(
    [string]$OwnershipPath = ""
)

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
if ([string]::IsNullOrWhiteSpace($OwnershipPath)) {
    $OwnershipPath = Join-Path $root "assets/config/renderer_ownership_targets.json"
}

$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

if (-not (Test-Path $OwnershipPath)) {
    throw "Renderer ownership target file not found: $OwnershipPath"
}

$doc = Get-Content $OwnershipPath -Raw | ConvertFrom-Json
$rendererHeaderPath = Join-Path $root "src/Graphics/Renderer.h"
if (-not (Test-Path $rendererHeaderPath)) {
    throw "Renderer.h not found: $rendererHeaderPath"
}
$rendererHeader = Get-Content $rendererHeaderPath -Raw

if ([int]$doc.schema -ne 1) {
    Add-Failure "renderer ownership schema must be 1"
}
if ($null -eq $doc.targets -or $doc.targets.Count -lt 1) {
    Add-Failure "renderer ownership target list is empty"
}

$ids = @{}
foreach ($target in $doc.targets) {
    $id = [string]$target.id
    if ([string]::IsNullOrWhiteSpace($id)) {
        Add-Failure "ownership target id is missing"
        continue
    }
    if ($ids.ContainsKey($id)) {
        Add-Failure "duplicate ownership target id '$id'"
    }
    $ids[$id] = $true

    if ([string]::IsNullOrWhiteSpace([string]$target.owner)) {
        Add-Failure "$id owner is missing"
    }
    if ([string]::IsNullOrWhiteSpace([string]$target.status)) {
        Add-Failure "$id status is missing"
    }
    if ([string]::IsNullOrWhiteSpace([string]$target.current_issue)) {
        Add-Failure "$id current_issue is missing"
    }
    if ([string]::IsNullOrWhiteSpace([string]$target.next_step)) {
        Add-Failure "$id next_step is missing"
    }
    if ($null -eq $target.target_files -or $target.target_files.Count -lt 1) {
        Add-Failure "$id target_files is empty"
    } else {
        foreach ($file in $target.target_files) {
            $fileName = [string]$file
            $path = Join-Path $root ("src/Graphics/{0}" -f $fileName)
            if (-not (Test-Path $path)) {
                Add-Failure "$id target file does not exist under src/Graphics: $fileName"
            }
        }
    }

    if ($null -eq $target.renderer_state_members -or $target.renderer_state_members.Count -lt 1) {
        Add-Failure "$id renderer_state_members is empty"
    } else {
        foreach ($member in $target.renderer_state_members) {
            $memberName = [string]$member
            if ($rendererHeader -notmatch "\b$([regex]::Escape($memberName))\b") {
                Add-Failure "$id renderer state member not found in Renderer.h: $memberName"
            }
        }
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Renderer ownership tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Renderer ownership tests passed: targets=$($doc.targets.Count)" -ForegroundColor Green
