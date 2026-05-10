@echo off
echo Forcing recompile of modified files...
cd CortexEngine

REM Touch the modified source files to update their timestamps
copy /b src\Graphics\Renderer.cpp +,, >nul 2>&1
copy /b src\Graphics\GPUCulling.cpp +,, >nul 2>&1

echo Modified files touched, rebuilding Release...
powershell -ExecutionPolicy Bypass -File "rebuild.ps1" -Config Release

if %ERRORLEVEL% EQU 0 (
    echo Build succeeded!
) else (
    echo Build failed!
)
pause
