# Systematic Flickering Issue Finder
# This script runs multiple test configurations to isolate the flickering bug

param(
    [switch]$SkipBuild
)

$SCRIPT_DIR = $PSScriptRoot
$ENGINE_DIR = Join-Path $SCRIPT_DIR "CortexEngine"
$TEST_CONFIG = Join-Path $SCRIPT_DIR "test-config-flickering.json"
$OUTPUT_DIR = Join-Path $SCRIPT_DIR "flickering_tests"
$REPORT_FILE = Join-Path $OUTPUT_DIR "test_report.txt"

# Create output directory
New-Item -ItemType Directory -Force -Path $OUTPUT_DIR | Out-Null

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host "  Systematic Flickering Issue Finder" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
Write-Host ""

# Initialize report
$timestamp = Get-Date -Format "yyyy-MM-dd HH:mm:ss"
"Flickering Issue Test Report" | Out-File -FilePath $REPORT_FILE
"Generated: $timestamp" | Out-File -FilePath $REPORT_FILE -Append
"=" * 60 | Out-File -FilePath $REPORT_FILE -Append
"" | Out-File -FilePath $REPORT_FILE -Append

# Build if not skipped
if (-not $SkipBuild) {
    Write-Host "[1/N] Building project..." -ForegroundColor Yellow
    Push-Location $ENGINE_DIR
    & cmd /c "build.bat"
    $buildResult = $LASTEXITCODE
    Pop-Location

    if ($buildResult -ne 0) {
        Write-Host "[ERROR] Build failed!" -ForegroundColor Red
        exit 1
    }
    Write-Host "[OK] Build complete" -ForegroundColor Green
    Write-Host ""
} else {
    Write-Host "[1/N] Skipping build" -ForegroundColor Yellow
    Write-Host ""
}

# Test configurations
$testConfigs = @(
    @{
        Name = "baseline"
        Description = "Baseline test - all features enabled (should flicker)"
        EnvVars = @{}
    },
    @{
        Name = "no_vb_cullmask"
        Description = "Disable visibility buffer cull mask"
        EnvVars = @{
            "CORTEX_DISABLE_VB_CULL_MASK" = "1"
        }
    },
    @{
        Name = "no_gpu_culling"
        Description = "Disable entire GPU culling pipeline"
        EnvVars = @{
            "CORTEX_DISABLE_GPU_CULLING" = "1"
        }
    },
    @{
        Name = "no_hzb"
        Description = "Disable HZB occlusion culling only"
        EnvVars = @{
            "CORTEX_DISABLE_HZB" = "1"
        }
    },
    @{
        Name = "no_frustum"
        Description = "Disable frustum culling via environment variable"
        EnvVars = @{
            "CORTEX_DISABLE_FRUSTUM_CULLING" = "1"
        }
    },
    @{
        Name = "loose_hzb_gate"
        Description = "Use loose HZB camera movement gate"
        EnvVars = @{
            "CORTEX_GPUCULL_HZB_LOOSE_GATE" = "1"
        }
    },
    @{
        Name = "all_disabled"
        Description = "Disable all culling features"
        EnvVars = @{
            "CORTEX_DISABLE_GPU_CULLING" = "1"
            "CORTEX_DISABLE_HZB" = "1"
            "CORTEX_DISABLE_FRUSTUM_CULLING" = "1"
            "CORTEX_DISABLE_VB_CULL_MASK" = "1"
        }
    }
)

$testNumber = 2
$totalTests = $testConfigs.Count + 1

foreach ($config in $testConfigs) {
    Write-Host "[$testNumber/$totalTests] Running test: $($config.Name)" -ForegroundColor Yellow
    Write-Host "Description: $($config.Description)" -ForegroundColor Gray

    # Set environment variables
    foreach ($envVar in $config.EnvVars.GetEnumerator()) {
        Write-Host "  Setting $($envVar.Key)=$($envVar.Value)" -ForegroundColor Gray
        [System.Environment]::SetEnvironmentVariable($envVar.Key, $envVar.Value, [System.EnvironmentVariableTarget]::Process)
    }

    # Create test-specific output directory
    $testOutputDir = Join-Path $OUTPUT_DIR $config.Name
    New-Item -ItemType Directory -Force -Path $testOutputDir | Out-Null

    # Modify test config to use test-specific output
    $testConfigContent = Get-Content $TEST_CONFIG -Raw | ConvertFrom-Json
    $testConfigContent.recording.output_directory = $testOutputDir
    $testConfigContent.recording.duration = 30  # Shorter duration for faster testing
    $testConfigContent.build.skip_build = $true  # Never rebuild during tests

    # Save modified config
    $tempConfigPath = Join-Path $SCRIPT_DIR "temp_test_config.json"
    $testConfigContent | ConvertTo-Json -Depth 10 | Out-File -FilePath $tempConfigPath -Encoding UTF8

    # Run test
    Write-Host "  Starting test recording..." -ForegroundColor Gray
    try {
        & powershell -ExecutionPolicy Bypass -File (Join-Path $SCRIPT_DIR "automate-test.ps1") -ConfigFile "temp_test_config.json" -SkipBuild 2>&1 | Out-Null

        # Find the generated video file
        $videoFiles = Get-ChildItem -Path $testOutputDir -Filter "*.mp4" | Sort-Object LastWriteTime -Descending
        if ($videoFiles.Count -gt 0) {
            $videoFile = $videoFiles[0].FullName
            $videoSize = [math]::Round($videoFiles[0].Length / 1MB, 2)
            Write-Host "  [OK] Test complete - $videoSize MB" -ForegroundColor Green

            # Add to report
            "" | Out-File -FilePath $REPORT_FILE -Append
            "Test: $($config.Name)" | Out-File -FilePath $REPORT_FILE -Append
            "Description: $($config.Description)" | Out-File -FilePath $REPORT_FILE -Append
            "Environment:" | Out-File -FilePath $REPORT_FILE -Append
            foreach ($envVar in $config.EnvVars.GetEnumerator()) {
                "  $($envVar.Key) = $($envVar.Value)" | Out-File -FilePath $REPORT_FILE -Append
            }
            "Video: $videoFile" | Out-File -FilePath $REPORT_FILE -Append
            "Size: $videoSize MB" | Out-File -FilePath $REPORT_FILE -Append
            "-" * 60 | Out-File -FilePath $REPORT_FILE -Append
        } else {
            Write-Host "  [WARNING] No video file generated" -ForegroundColor Yellow
        }
    } catch {
        Write-Host "  [ERROR] Test failed: $_" -ForegroundColor Red
    }

    # Clear environment variables
    foreach ($envVar in $config.EnvVars.GetEnumerator()) {
        [System.Environment]::SetEnvironmentVariable($envVar.Key, $null, [System.EnvironmentVariableTarget]::Process)
    }

    Write-Host ""
    $testNumber++

    # Small delay between tests
    Start-Sleep -Seconds 2
}

# Clean up temp config
if (Test-Path $tempConfigPath) {
    Remove-Item $tempConfigPath -Force
}

Write-Host "==================================================" -ForegroundColor Green
Write-Host "  All tests complete!" -ForegroundColor Green
Write-Host "==================================================" -ForegroundColor Green
Write-Host ""
Write-Host "Results saved to: $OUTPUT_DIR" -ForegroundColor Cyan
Write-Host "Report: $REPORT_FILE" -ForegroundColor Cyan
Write-Host ""
Write-Host "Next steps:" -ForegroundColor Yellow
Write-Host "1. Review each video to identify which test STOPS the flickering" -ForegroundColor White
Write-Host "2. The environment variable(s) that stop flickering indicate the buggy component" -ForegroundColor White
Write-Host "3. Once identified, we can focus on fixing that specific component" -ForegroundColor White
Write-Host ""
Write-Host "Opening results folder..." -ForegroundColor Gray
Start-Process explorer $OUTPUT_DIR
