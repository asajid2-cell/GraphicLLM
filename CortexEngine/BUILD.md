# Building Project Cortex

This document describes how to build the Cortex engine manually on Windows.

---

## Prerequisites

### Required software

1. **Windows 10/11** with the Windows SDK.
2. **Visual Studio 2022** (or newer) with:
   - “Desktop development with C++” workload
   - CMake tools for Windows
3. **CMake 3.20+**
4. **Git**
5. **vcpkg** (for dependency management)

Install vcpkg:

```bat
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```

Add vcpkg to your environment:

```bat
setx VCPKG_ROOT "C:\path\to\vcpkg"
```

---

## Install Dependencies with vcpkg

From `%VCPKG_ROOT%`:

```bat
cd %VCPKG_ROOT%
.\vcpkg install sdl3:x64-windows
.\vcpkg install entt:x64-windows
.\vcpkg install nlohmann-json:x64-windows
.\vcpkg install spdlog:x64-windows
.\vcpkg install directx-headers:x64-windows
.\vcpkg install directxtk12:x64-windows
```

You can add additional packages (e.g., glm) as needed; the CMakeLists in
`CortexEngine` already references the required libraries.

---

## Configure with CMake

From the `CortexEngine` directory:

```bat
set VCPKG_ROOT=C:\path\to\vcpkg

cmake -B build -S . ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
```

This generates the build system (Visual Studio solution or Ninja files,
depending on your default generator).

---

## Build

```bat
cmake --build build --config Release
```

The main executable is placed under:

- `build\bin\Release\CortexEngine.exe` (multi‑config generators), or
- `build\bin\CortexEngine.exe` (single‑config generators such as Ninja).

---

## Running from Visual Studio

If you prefer to work entirely inside Visual Studio:

1. Open `build\CortexEngine.sln`.
2. Select the `CortexEngine` startup project.
3. Choose the `Release` configuration.
4. Build using **Build > Build All** or `Ctrl+Shift+B`.
5. Run with **Debug > Start Without Debugging** or `Ctrl+F5`.

---

## Using the Automation Scripts

Instead of driving CMake and vcpkg manually, you can use:

```powershell
.\setup.ps1   # full setup and build
.\build.ps1   # incremental build
.\run.ps1     # run the engine
```

These scripts encapsulate the steps above and are the recommended path for
day‑to‑day development.

