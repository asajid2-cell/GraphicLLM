@echo off
REM Project Cortex - Quick Run Script (Batch)

if not exist build\bin\Release\CortexEngine.exe (
    echo [ERROR] Executable not found!
    echo.
    echo Did you build the project? Run:
    echo   setup.bat
    echo.
    pause
    exit /b 1
)

echo Starting Project Cortex...
echo.

cd build\bin\Release
CortexEngine.exe
cd ..\..\..
