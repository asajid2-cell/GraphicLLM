# Project Cortex – Quick Start

This guide walks you from a clean Windows machine to running the engine with a spinning test object.

---

## 1. Prerequisites

You need:

- Windows 10 or 11
- Visual Studio 2022 (Community Edition is fine) with:
  - “Desktop development with C++” workload
- Git

The setup scripts will install and configure the rest (CMake, vcpkg, libraries).

---

## 2. One-Command Setup

From the `CortexEngine` directory, open PowerShell and run:

```powershell
powershell -ExecutionPolicy Bypass -File .\setup.ps1
```

This script:

1. Checks for CMake, Git, and Visual Studio.
2. Installs or locates vcpkg.
3. Installs all required packages (SDL3, EnTT, DirectX headers, GLM, spdlog, etc.).
4. Configures the CMake build directory.
5. Builds the engine (Release by default).
6. Copies shader assets into the build output.

First run typically takes 15–25 minutes (most of the time is package downloads).  
Subsequent builds are much faster.

---

## 3. Running the Engine

After `setup.ps1` completes, either:

```powershell
.\run.ps1
```

or run the executable directly:

```powershell
cd build\bin\Release
.\CortexEngine.exe
```

Expected result:

- A 1280×720 window opens.
- The scene shows a test object lit by the PBR pipeline.
- The console prints frame timing and basic diagnostics.
- Press `Esc` to exit.

---

## 4. Troubleshooting

### “Execution of scripts is disabled”

Your PowerShell execution policy is locked down. Use:

```powershell
powershell -ExecutionPolicy Bypass -File .\setup.ps1
```

This relaxes the policy only for this invocation.

### “Visual Studio not found”

Install Visual Studio 2022:

1. Download from <https://visualstudio.microsoft.com/downloads/>.
2. Select the **Desktop development with C++** workload.
3. After installation, rerun `setup.ps1`.

### “vcpkg not found”

`setup.ps1` will clone and bootstrap vcpkg for you if `VCPKG_ROOT` is not set.  
If you prefer to manage vcpkg manually, set `VCPKG_ROOT` before running the script.

---

## 5. Manual Build (Optional)

If you prefer to drive CMake yourself:

```bat
set VCPKG_ROOT=C:\path\to\vcpkg

cmake -B build -S . ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

cmake --build build --config Release
build\bin\Release\CortexEngine.exe
```

Use this when you want tighter control over configurations or generators, or when integrating
the engine into a larger solution.

