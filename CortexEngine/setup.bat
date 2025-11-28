@echo off
REM Project Cortex - Automated Setup Script (Batch)
REM This script automates the entire build environment setup

setlocal enabledelayedexpansion

echo.
echo ===============================================================
echo.
echo            PROJECT CORTEX - SETUP SCRIPT
echo         Neural-Native Rendering Engine v0.1.0
echo.
echo                 Phase 1: The Iron Foundation
echo.
echo ===============================================================
echo.

REM Check for vcpkg
if not defined VCPKG_ROOT (
    echo [ERROR] VCPKG_ROOT environment variable not set!
    echo.
    echo Please install vcpkg first:
    echo   1. git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
    echo   2. cd C:\vcpkg
    echo   3. bootstrap-vcpkg.bat
    echo   4. setx VCPKG_ROOT "C:\vcpkg"
    echo.
    echo Or run the PowerShell script which does this automatically:
    echo   powershell -ExecutionPolicy Bypass -File setup.ps1
    echo.
    pause
    exit /b 1
)

echo [INFO] Using vcpkg from: %VCPKG_ROOT%
echo.

REM Check for CMake
where cmake >nul 2>nul
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake not found!
    echo Download from: https://cmake.org/download/
    pause
    exit /b 1
)

echo [OK] CMake found
echo.

REM Install dependencies
echo [STEP] Installing dependencies via vcpkg...
echo This may take 10-20 minutes on first run...
echo.

pushd %VCPKG_ROOT%

call vcpkg install sdl3:x64-windows
if %ERRORLEVEL% neq 0 goto :error

call vcpkg install entt:x64-windows
if %ERRORLEVEL% neq 0 goto :error

call vcpkg install nlohmann-json:x64-windows
if %ERRORLEVEL% neq 0 goto :error

call vcpkg install spdlog:x64-windows
if %ERRORLEVEL% neq 0 goto :error

call vcpkg install directx-headers:x64-windows
if %ERRORLEVEL% neq 0 goto :error

call vcpkg install directxtk12:x64-windows
if %ERRORLEVEL% neq 0 goto :error

call vcpkg install glm:x64-windows
if %ERRORLEVEL% neq 0 goto :error

popd

echo.
echo [OK] All dependencies installed!
echo.

REM Configure CMake
echo [STEP] Configuring CMake build...
echo.

if exist build rmdir /s /q build
mkdir build
cd build

cmake .. ^
    -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake ^
    -G "Visual Studio 17 2022" ^
    -A x64

if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake configuration failed!
    cd ..
    pause
    exit /b 1
)

echo.
echo [OK] CMake configuration complete!
echo.

REM Build project
echo [STEP] Building project (Release configuration)...
echo.

cmake --build . --config Release --parallel

if %ERRORLEVEL% neq 0 (
    echo [ERROR] Build failed!
    cd ..
    pause
    exit /b 1
)

cd ..

echo.
echo [OK] Build complete!
echo.

REM Check if executable exists
if exist build\bin\Release\CortexEngine.exe (
    echo [OK] Executable created: build\bin\Release\CortexEngine.exe
) else (
    echo [ERROR] Executable not found!
    pause
    exit /b 1
)

echo.
echo ===============================================================
echo.
echo                   SETUP COMPLETE!
echo.
echo ===============================================================
echo.
echo Next Steps:
echo   1. Run the application:
echo      cd build\bin\Release
echo      CortexEngine.exe
echo.
echo   2. Or use the run script:
echo      run.bat
echo.
echo   3. Open in Visual Studio:
echo      start build\CortexEngine.sln
echo.
echo Controls:
echo   ESC - Exit application
echo.
echo Documentation:
echo   README.md          - Project overview
echo   BUILD.md           - Build instructions
echo   PHASE1_COMPLETE.md - Implementation details
echo.

pause
exit /b 0

:error
popd
echo.
echo [ERROR] Failed to install dependencies!
pause
exit /b 1
