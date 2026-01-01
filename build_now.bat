@echo off
echo Setting up VS environment...
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
if errorlevel 1 (
    echo Failed to set up VS environment
    exit /b 1
)
echo Changing to build directory...
cd /d z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build
echo Running ninja...
ninja -j8
echo Build completed with exit code %ERRORLEVEL%
