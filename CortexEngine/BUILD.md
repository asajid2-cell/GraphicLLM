# Building Project Cortex

## Prerequisites

### Required Software

1. **Windows 10/11** with Windows SDK
2. **Visual Studio 2022** (or newer) with:
   - Desktop development with C++
   - Windows SDK (10.0.19041.0 or later)
   - CMake tools for Windows

3. **CMake 3.20+**
   - Download from https://cmake.org/download/
   - Or install via Visual Studio Installer

4. **vcpkg** (Package Manager)
   ```bash
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   ```

   Add vcpkg to your environment variables:
   ```bash
   setx VCPKG_ROOT "C:\path\to\vcpkg"
   ```

## Install Dependencies

Using vcpkg, install all required packages:

```bash
cd %VCPKG_ROOT%
.\vcpkg install sdl3:x64-windows
.\vcpkg install entt:x64-windows
.\vcpkg install nlohmann-json:x64-windows
.\vcpkg install spdlog:x64-windows
.\vcpkg install directx-headers:x64-windows
.\vcpkg install directxtk12:x64-windows
.\vcpkg install glm:x64-windows
```

This may take 10-15 minutes depending on your internet connection.

## Build Steps

### Option 1: Visual Studio (Recommended)

1. Open Visual Studio 2022
2. File → Open → CMake → Select `CortexEngine/CMakeLists.txt`
3. Visual Studio will automatically configure CMake using vcpkg
4. Select build configuration: **x64-Debug** or **x64-Release**
5. Build → Build All (Ctrl+Shift+B)
6. The executable will be in `build/bin/Debug/` or `build/bin/Release/`

### Option 2: Command Line

```bash
cd CortexEngine

# Configure (first time only)
cmake -B build -S . ^
    -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
    -G "Visual Studio 17 2022" ^
    -A x64

# Build Debug
cmake --build build --config Debug

# Or build Release
cmake --build build --config Release
```

### Option 3: CMake GUI

1. Open CMake GUI
2. Set "Where is the source code" to `CortexEngine/`
3. Set "Where to build the binaries" to `CortexEngine/build`
4. Click "Configure"
5. Select generator: **Visual Studio 17 2022**, Platform: **x64**
6. Add entry: `CMAKE_TOOLCHAIN_FILE` = `%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake`
7. Click "Generate"
8. Click "Open Project" to open in Visual Studio
9. Build solution

## Running the Application

After building, run from the build output directory:

```bash
cd build/bin/Release
.\CortexEngine.exe
```

Or simply press F5 in Visual Studio to run with debugging.

### Expected Output

You should see:
- A window titled "Project Cortex - Phase 1: Spinning Cube"
- A spinning orange-red cube in the center
- Console output showing FPS and frame time

Press **ESC** to exit.

## Troubleshooting

### CMake can't find vcpkg

Make sure `VCPKG_ROOT` environment variable is set:
```bash
echo %VCPKG_ROOT%
```

Or specify toolchain file manually:
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake ..
```

### SDL3 not found

SDL3 is very new. If vcpkg doesn't have it yet, you may need to:
1. Use SDL2 instead (change vcpkg.json and CMakeLists.txt)
2. Or build SDL3 from source: https://github.com/libsdl-org/SDL

### DirectX 12 errors

Make sure you have:
- Windows SDK 10.0.19041.0 or later
- DirectX 12 compatible GPU
- Up-to-date graphics drivers

### Shader compilation errors

Shaders are compiled at runtime from `assets/shaders/Basic.hlsl`.
Make sure:
- The `assets/` folder is copied to the build output directory (CMake does this automatically)
- You're running from the correct directory

### Performance issues

Debug builds are SLOW due to validation layers. Use Release build for performance:
```bash
cmake --build build --config Release
```

## Next Steps: Phase 2

Once Phase 1 works (spinning cube), proceed to Phase 2:
- Integrate llama.cpp for LLM control
- Implement JSON command parser
- Add ImGui console overlay

See README.md for the full roadmap.

## Additional Resources

- DirectX 12 Docs: https://docs.microsoft.com/en-us/windows/win32/direct3d12/
- EnTT Documentation: https://github.com/skypjack/entt
- SDL3 Migration Guide: https://wiki.libsdl.org/SDL3/
