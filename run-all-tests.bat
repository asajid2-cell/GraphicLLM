@echo off
REM Run multiple test configurations in sequence
REM Useful for comprehensive regression testing

echo ========================================
echo  Running All Test Configurations
echo ========================================
echo.

REM Check if FFmpeg is installed
where ffmpeg >nul 2>&1
if %ERRORLEVEL% neq 0 (
    echo [ERROR] FFmpeg not found!
    echo Please install: winget install ffmpeg
    pause
    exit /b 1
)

REM Build once before all tests
echo [1] Building project...
cd CortexEngine
call build.bat
if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed!
    cd ..
    pause
    exit /b 1
)
cd ..
echo.

REM Run test with default config
echo [2] Running default test configuration...
powershell -ExecutionPolicy Bypass -File "%~dp0automate-test.ps1" -SkipBuild
echo.

REM Add more test configurations here
REM Example: powershell -ExecutionPolicy Bypass -File "%~dp0automate-test.ps1" -SkipBuild -ConfigFile "test-movement.json"

echo ========================================
echo  All tests complete!
echo ========================================
echo Check test_recordings folder for videos
echo.

explorer test_recordings

pause
