@echo off
echo ========================================
echo Rebuilding CortexEngine (Release)
echo ========================================
cd CortexEngine\build
cmake --build . --config Release --target CortexEngine
if %ERRORLEVEL% EQU 0 (
    echo.
    echo ========================================
    echo Build succeeded!
    echo Executable: CortexEngine\build\bin\CortexEngine.exe
    echo ========================================
) else (
    echo.
    echo ========================================
    echo Build failed! Check errors above.
    echo ========================================
)
pause
