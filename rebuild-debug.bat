@echo off
echo ========================================
echo Rebuilding CortexEngine (Debug)
echo ========================================
cd CortexEngine\build
cmake --build . --config Debug --target CortexEngine
if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo Build succeeded!
    echo Executable: CortexEngine\build\bin\Debug\CortexEngine.exe
    echo ========================================
) else (
    echo.
    echo ========================================
    echo Build failed! Check errors above.
    echo ========================================
)
pause
