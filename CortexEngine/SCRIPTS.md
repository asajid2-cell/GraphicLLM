# Project Cortex – Automation Scripts

The engine includes a small set of scripts to streamline setup, build, and run
operations on Windows.

---

## Quick Start

From the `CortexEngine` directory:

### PowerShell (recommended)

```powershell
powershell -ExecutionPolicy Bypass -File .\setup.ps1
```

### Batch (alternative)

```bat
setup.bat
```

These entry points perform a full setup and build on a clean machine.

---

## Script Overview

### `setup.ps1` / `setup.bat` – Full setup + build

Responsibilities:

- Check for CMake, Git, and Visual Studio.
- Install or detect vcpkg.
- Install all required libraries (SDL3, EnTT, DirectX headers, GLM, spdlog, etc.).
- Configure the CMake build directory.
- Build the engine (Release by default).
- Verify that the executable and shader assets exist.

Common options (PowerShell version):

```powershell
.\setup.ps1                    # full setup and build
.\setup.ps1 -SkipVcpkg         # assume vcpkg is already installed
.\setup.ps1 -SkipBuild         # configure only
.\setup.ps1 -BuildConfig Debug # build Debug instead of Release
```

### `build.ps1` – Rebuild only

Runs the build step without re-running the full setup:

```powershell
.\build.ps1                    # build Release
.\build.ps1 -Config Debug      # build Debug
.\build.ps1 -Clean             # clean first, then build
```

### `run.ps1` – Launch the engine

Locates the built executable in `build\bin\Release` (or a non‑config path,
depending on generator) and runs it:

```powershell
.\run.ps1
```

### Other helper scripts

- `full-build.ps1` – convenience wrapper that runs setup and then build.
- `swap.ps1`, `convert_and_clean.py`, `smart_convert.py`, etc. – tools used for
  model and asset conversion; see comments in each script for details.

---

## Typical Workflow

1. Clone the repository.
2. Run `setup.ps1` once to install dependencies and configure the build.
3. Use `build.ps1` after making code changes.
4. Use `run.ps1` to launch the engine.

This workflow keeps the focus on engine development while the scripts handle
repetitive environment and build steps.

