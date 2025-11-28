# Project Cortex - Automated Setup Scripts

This document explains all the automation scripts available for building and running Project Cortex.

## 🚀 Quick Start (One Command)

### PowerShell (Recommended)
```powershell
powershell -ExecutionPolicy Bypass -File setup.ps1
```

### Batch (Alternative)
```batch
setup.bat
```

This single command will:
- ✅ Check for prerequisites (CMake, Git, Visual Studio)
- ✅ Install/detect vcpkg
- ✅ Install all dependencies (SDL3, EnTT, DirectX, etc.)
- ✅ Configure CMake build
- ✅ Compile the project
- ✅ Verify assets and executable

**Estimated time:** 15-25 minutes (first run), 2-5 minutes (subsequent builds)

---

## 📜 Available Scripts

### 1. `setup.ps1` / `setup.bat` - Full Setup + Build

**PowerShell Version (Recommended):**
```powershell
# Full setup and build
.\setup.ps1

# Skip vcpkg install (if already installed)
.\setup.ps1 -SkipVcpkg

# Configure only, don't build
.\setup.ps1 -SkipBuild

# Build Debug configuration
.\setup.ps1 -BuildConfig Debug
```

**Batch Version:**
```batch
setup.bat
```

**What it does:**
- Checks for CMake, Git, Visual Studio
- Installs vcpkg (if not found)
- Installs all package dependencies
- Configures CMake with vcpkg toolchain
- Builds the project (Release by default)
- Verifies executable and assets

**When to use:**
- First time setup
- After major changes to dependencies
- After cleaning everything

---

### 2. `build.ps1` / `build.bat` - Quick Rebuild

**PowerShell Version:**
```powershell
# Rebuild Release
.\build.ps1

# Rebuild Debug
.\build.ps1 -Config Debug

# Clean and rebuild
.\build.ps1 -Clean
```

**Batch Version:**
```batch
build.bat
```

**What it does:**
- Quickly rebuilds the project
- Uses existing CMake configuration
- Parallel compilation
- Shows build time and executable size

**When to use:**
- After code changes
- Daily development workflow
- Testing compilation

**Note:** If `build/` doesn't exist, automatically runs `setup.ps1` first.

---

### 3. `run.ps1` / `run.bat` - Quick Run

**PowerShell Version:**
```powershell
# Run Release build
.\run.ps1

# Run Debug build
.\run.ps1 -Config Debug
```

**Batch Version:**
```batch
run.bat
```

**What it does:**
- Launches the compiled executable
- Automatically sets working directory
- Shows error if not built yet

**When to use:**
- Quick testing after build
- Running the application

---

## 🎯 Common Workflows

### First Time Setup
```powershell
# 1. Clone repository
git clone <repo-url>
cd CortexEngine

# 2. Run full setup (one command does everything!)
powershell -ExecutionPolicy Bypass -File setup.ps1

# 3. Run the application
.\run.ps1
```

### Daily Development
```powershell
# 1. Make code changes
# ...

# 2. Rebuild
.\build.ps1

# 3. Run
.\run.ps1
```

### Clean Rebuild
```powershell
# Option 1: Clean build directory and rebuild
Remove-Item -Recurse -Force build
.\setup.ps1

# Option 2: Use clean flag
.\build.ps1 -Clean
```

### Debug Build
```powershell
# Setup with Debug config
.\setup.ps1 -BuildConfig Debug

# Or rebuild as Debug
.\build.ps1 -Config Debug

# Run Debug version
.\run.ps1 -Config Debug
```

---

## 🔧 Manual Override

If you prefer manual control or the scripts fail, you can run commands directly:

```powershell
# Manual vcpkg install
cd C:\vcpkg
.\vcpkg install sdl3:x64-windows entt:x64-windows nlohmann-json:x64-windows `
    spdlog:x64-windows directx-headers:x64-windows directxtk12:x64-windows glm:x64-windows

# Manual CMake configure
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake

# Manual build
cmake --build build --config Release --parallel

# Manual run
cd build\bin\Release
.\CortexEngine.exe
```

---

## ⚙️ Script Parameters

### setup.ps1 Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `-SkipVcpkg` | Switch | Off | Skip vcpkg installation and dependency check |
| `-SkipBuild` | Switch | Off | Only configure CMake, don't build |
| `-BuildConfig` | String | "Release" | Build configuration (Release/Debug/RelWithDebInfo) |

**Examples:**
```powershell
# Skip vcpkg (assume dependencies installed)
.\setup.ps1 -SkipVcpkg

# Configure only, build later
.\setup.ps1 -SkipBuild

# Setup for Debug
.\setup.ps1 -BuildConfig Debug

# Multiple flags
.\setup.ps1 -SkipVcpkg -BuildConfig Debug
```

### build.ps1 Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `-Config` | String | "Release" | Build configuration |
| `-Clean` | Switch | Off | Clean before building |

**Examples:**
```powershell
# Debug build
.\build.ps1 -Config Debug

# Clean rebuild
.\build.ps1 -Clean

# Clean Debug rebuild
.\build.ps1 -Config Debug -Clean
```

### run.ps1 Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `-Config` | String | "Release" | Configuration to run |
| `-ForceSync` | Switch | Off | Force re-copy of `assets/` and `models/` into the executable folder |
| `-NoLLM` | Switch | Off | Run without initializing The Architect (faster startup) |

**Examples:**
```powershell
# Run Debug
.\run.ps1 -Config Debug

# Run Debug and resync assets/models
.\run.ps1 -Config Debug -ForceSync

# Run Debug without LLM startup cost
.\run.ps1 -Config Debug -NoLLM
```

---

## 🐛 Troubleshooting

### "Execution of scripts is disabled"
```powershell
# Temporary bypass (current session only)
powershell -ExecutionPolicy Bypass -File setup.ps1

# Or set for current user (persistent)
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

### "vcpkg not found"
```powershell
# Let setup.ps1 install it automatically
.\setup.ps1

# Or install manually
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat
[System.Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")
```

### "CMake not found"
Download from https://cmake.org/download/ or install via Visual Studio Installer.

### "Visual Studio not found"
Install Visual Studio 2022 with "Desktop development with C++" workload.

### Build fails on SDL3
SDL3 is very new. If vcpkg doesn't have it:
```powershell
# Use SDL2 instead (requires code changes)
# Or build SDL3 from source
```

### Dependencies fail to install
```powershell
# Update vcpkg
cd $env:VCPKG_ROOT
git pull
.\bootstrap-vcpkg.bat

# Clear vcpkg cache
.\vcpkg remove --outdated
```

---

## 📊 What Gets Installed

### vcpkg Packages (Total: ~500 MB)

| Package | Size | Purpose |
|---------|------|---------|
| sdl3 | ~5 MB | Window management and input |
| entt | ~1 MB | Entity Component System |
| nlohmann-json | ~1 MB | JSON parsing |
| spdlog | ~2 MB | Fast logging |
| directx-headers | ~5 MB | DirectX 12 headers |
| directxtk12 | ~10 MB | DirectX Toolkit |
| glm | ~1 MB | Math library |

**Total install time:** 10-20 minutes (varies by internet speed)

### Build Output (Total: ~50 MB)

```
build/
├── bin/
│   └── Release/
│       ├── CortexEngine.exe    (~2 MB)
│       ├── SDL3.dll             (~1 MB)
│       └── assets/              (copied from source)
└── ... (intermediate files)
```

---

## 🎮 Expected Output

After running `setup.ps1` successfully, you should see:

```
╔═══════════════════════════════════════════════════════════╗
║                                                           ║
║                   SETUP COMPLETE! ✓                      ║
║                                                           ║
╚═══════════════════════════════════════════════════════════╝

Next Steps:
  1. Run the application:
     cd build\bin\Release
     .\CortexEngine.exe

  2. Or use the run script:
     .\run.ps1

  3. Open in Visual Studio:
     start build\CortexEngine.sln

Controls:
  ESC - Exit application

Setup completed in 42.5 seconds
```

Then when running the executable:
- Window opens (1280x720)
- Orange-red spinning cube
- Console shows FPS counter
- Press ESC to exit

---

## 💡 Pro Tips

1. **Use PowerShell over Batch** - Better error handling and colored output
2. **Pin vcpkg location** - Set `VCPKG_ROOT` permanently to avoid reinstalls
3. **Parallel builds** - Scripts automatically use `--parallel` flag
4. **Quick iteration** - Use `build.ps1` for fast rebuilds during development
5. **Visual Studio integration** - Open `build\CortexEngine.sln` for IDE experience

---

## 📚 Additional Resources

- [BUILD.md](BUILD.md) - Manual build instructions
- [README.md](README.md) - Project overview
- [PHASE1_COMPLETE.md](PHASE1_COMPLETE.md) - Implementation details
- [vcpkg docs](https://vcpkg.io/) - Package manager documentation
- [CMake docs](https://cmake.org/documentation/) - Build system documentation

---

## 🚀 Next Steps After Successful Build

1. **Run the demo** - See the spinning cube (Phase 1 milestone!)
2. **Read the docs** - Check out PHASE1_COMPLETE.md for architecture details
3. **Start Phase 2** - Integrate llama.cpp for LLM control
4. **Start Phase 3** - Add TensorRT for AI texture generation

Happy coding! 🎉
