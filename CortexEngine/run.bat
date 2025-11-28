@echo off
REM Project Cortex - Quick Run Script (Batch)

set "EXE1=build\bin\Release\CortexEngine.exe"
set "EXE2=build\CortexEngine.exe"

if exist "%EXE1%" (
    set "EXE=%EXE1%"
) else if exist "%EXE2%" (
    set "EXE=%EXE2%"
) else (
    echo [ERROR] Executable not found in:
    echo   %EXE1%
    echo   %EXE2%
    echo.
    echo Did you build the project? Run:
    echo   setup.bat
    echo.
    pause
    exit /b 1
)

echo Starting Project Cortex...
echo Executable: %EXE%
echo.

set "CUDA_BIN="
set "CUDA_BIN="
set "CUDA_BIN_X64="
if defined CUDAToolkit_ROOT (
    if exist "%CUDAToolkit_ROOT%\bin\x64" set "CUDA_BIN_X64=%CUDAToolkit_ROOT%\bin\x64"
    if exist "%CUDAToolkit_ROOT%\bin" set "CUDA_BIN=%CUDAToolkit_ROOT%\bin"
)
if not defined CUDA_BIN_X64 (
    if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0\bin\x64" set "CUDA_BIN_X64=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0\bin\x64"
)
if not defined CUDA_BIN (
    if exist "C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0\bin" set "CUDA_BIN=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.0\bin"
)
if defined CUDA_BIN_X64 (
    echo Using CUDA bin (x64): %CUDA_BIN_X64%
    set "PATH=%CUDA_BIN_X64%;%PATH%"
)
if defined CUDA_BIN (
    echo Using CUDA bin: %CUDA_BIN%
    set "PATH=%CUDA_BIN%;%PATH%"
)
if not defined CUDA_BIN if not defined CUDA_BIN_X64 (
    echo Warning: CUDA bin not found; LLM CUDA runtime may fail to load.
)

if defined CUDA_BIN_X64 (
    echo Copying CUDA DLLs from %CUDA_BIN_X64%
    robocopy "%CUDA_BIN_X64%" "%EXEDIR%" *.dll /NFL /NDL /NJH /NJS >nul
)
if defined CUDA_BIN (
    echo Copying CUDA DLLs from %CUDA_BIN%
    robocopy "%CUDA_BIN%" "%EXEDIR%" *.dll /NFL /NDL /NJH /NJS >nul
)

for %%I in ("%EXE%") do (
    set "EXEDIR=%%~dpI"
    set "EXENAME=%%~nxI"
)

set "ASSETS=%~dp0assets"
set "MODELS=%~dp0models"
if exist "%ASSETS%" (
    echo Copying assets to %EXEDIR%
    robocopy "%ASSETS%" "%EXEDIR%assets" /E /NFL /NDL /NJH /NJS >nul
) else (
    echo Warning: assets folder not found at %ASSETS%
)
if exist "%MODELS%" (
    echo Copying models to %EXEDIR%
    robocopy "%MODELS%" "%EXEDIR%models" /E /NFL /NDL /NJH /NJS >nul
) else (
    echo Warning: models folder not found at %MODELS%
)

pushd "%~dp0"
pushd "%EXEDIR%"
.\%EXENAME%
popd
popd
