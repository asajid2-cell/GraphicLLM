param()

$ErrorActionPreference = "Stop"

$root = Resolve-Path (Join-Path $PSScriptRoot "..")
$cmakePath = Join-Path $root "CMakeLists.txt"
$failures = New-Object System.Collections.Generic.List[string]

function Add-Failure([string]$Message) {
    $script:failures.Add($Message)
}

if (-not (Test-Path $cmakePath)) {
    Add-Failure "Missing CMakeLists.txt"
} else {
    $cmake = Get-Content $cmakePath -Raw
    $sourceMatches = [regex]::Matches($cmake, "src/[A-Za-z0-9_./-]+\.cpp")
    $sources = @($sourceMatches | ForEach-Object { $_.Value })
    $uniqueSources = @($sources | Sort-Object -Unique)

    if ($sources.Count -eq 0) {
        Add-Failure "No source files found in CMakeLists.txt"
    }

    $duplicates = @($sources | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })
    if ($duplicates.Count -gt 0) {
        Add-Failure "Duplicate CMake source entries:`n$($duplicates -join [Environment]::NewLine)"
    }

    foreach ($source in $uniqueSources) {
        if (-not (Test-Path (Join-Path $root $source))) {
            Add-Failure "CMake source entry does not exist: $source"
        }
        $fileName = [System.IO.Path]::GetFileName($source)
        if ($fileName -match "^(tmp_|head_|fix_|scratch_|backup_)" -or
            $fileName -match "(_old|_bak|_backup)\.cpp$") {
            Add-Failure "Temporary or backup source file is listed in CMake: $source"
        }
    }

    $rendererFiles = @(
        Get-ChildItem (Join-Path $root "src/Graphics") -Filter "Renderer*.cpp" |
            ForEach-Object { "src/Graphics/$($_.Name)" }
    )
    $sourceSet = New-Object "System.Collections.Generic.HashSet[string]"
    foreach ($source in $uniqueSources) {
        [void]$sourceSet.Add($source)
    }
    $missingRendererFiles = @($rendererFiles | Where-Object { -not $sourceSet.Contains($_) })
    if ($missingRendererFiles.Count -gt 0) {
        Add-Failure "Renderer split source files missing from CMake:`n$($missingRendererFiles -join [Environment]::NewLine)"
    }

    $stubSources = @($uniqueSources | Where-Object { $_ -match "Stub\.cpp$" })
    $allowedStubSources = @("src/LLM/LLMServiceStub.cpp")
    $unexpectedStubs = @($stubSources | Where-Object { $allowedStubSources -notcontains $_ })
    if ($unexpectedStubs.Count -gt 0) {
        Add-Failure "Unexpected stub source files listed in CMake:`n$($unexpectedStubs -join [Environment]::NewLine)"
    }
}

if ($failures.Count -gt 0) {
    Write-Host "Source list contract tests failed:" -ForegroundColor Red
    foreach ($failure in $failures) {
        Write-Host " - $failure" -ForegroundColor Red
    }
    exit 1
}

Write-Host "Source list contract tests passed." -ForegroundColor Green
Write-Host "  cmake_sources=present"
Write-Host "  duplicates=0"
Write-Host "  renderer_split_sources=covered"
Write-Host "  temporary_sources=0"
