@echo off
REM Project Cortex - Quick Build Script (Batch)

if not exist build (
    echo Build directory not found. Running full setup...
    call setup.bat
    exit /b
)

echo Building Project Cortex (Release)...
echo.

cd build
cmake --build . --config Release --parallel

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERROR] Build failed!
    cd ..
    pause
    exit /b 1
)

cd ..

echo.
echo [OK] Build complete!
echo.
echo Run with: run.bat

pause
