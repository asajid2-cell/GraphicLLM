@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat" -arch=amd64
cd /d z:\328\CMPUT328-A2\codexworks\301\graphics\CortexEngine\build
ninja -j8 2>&1
