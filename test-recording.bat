@echo off
REM Quick launcher for automated test recording

echo Starting automated test recording...
echo.

powershell -ExecutionPolicy Bypass -File "%~dp0automate-test.ps1" %*

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Test recording failed!
    pause
    exit /b 1
)

echo.
pause
