@echo off
REM Top-level helper to rebuild CortexEngine in Debug.
REM Delegates to CortexEngine\rebuild.ps1, which ensures the
REM Visual Studio build environment (cl.exe, standard headers)
REM is correctly initialized before invoking CMake/Ninja.

setlocal

echo ========================================
echo Rebuilding CortexEngine (Debug)
echo ========================================

pushd "%~dp0CortexEngine"

powershell -ExecutionPolicy Bypass -File ".\rebuild.ps1" -Config Debug
set BUILD_ERROR=%ERRORLEVEL%

popd

if %BUILD_ERROR% EQU 0 (
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
