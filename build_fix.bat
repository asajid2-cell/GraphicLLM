@echo off
REM Quick build to test flickering fix

cd /d "%~dp0CortexEngine"

echo ========================================
echo Building CortexEngine with flickering fix
echo ========================================
echo.

if not exist build (
    echo ERROR: Build directory not found!
    echo Run setup.bat first.
    pause
    exit /b 1
)

cd build
cmake --build . --config Release --parallel 8

if %ERRORLEVEL% neq 0 (
    echo.
    echo ========================================
    echo BUILD FAILED!
    echo ========================================
    pause
    exit /b 1
)

cd ..

echo.
echo ========================================
echo BUILD SUCCESS!
echo ========================================
echo.
echo Changes made:
echo  - Removed CPU-side visible flag checks (4 locations in Renderer.cpp)
echo  - GPU culling now sole authority on visibility
echo  - Restored full GPU culling functionality
echo.
echo To test: Run infinite world demo and move camera
echo Expected: No flickering during chunk loading/unloading
echo.
pause
