# Project Cortex - Quick Start Guide

Get from zero to spinning cube in **one command**! 🚀

## Prerequisites

You need **Windows 10/11** with:
- Visual Studio 2022 (with "Desktop development with C++")
- Git (to clone repositories)

**That's it!** The setup script handles everything else automatically.

---

## Step 1: Run Setup (One Command)

Open **PowerShell** and run:

```powershell
powershell -ExecutionPolicy Bypass -File setup.ps1
```

### What This Does (Automatically):

1. ✅ **Checks Prerequisites** - CMake, Git, Visual Studio
2. ✅ **Installs vcpkg** - Package manager (if not found)
3. ✅ **Installs Dependencies** - SDL3, EnTT, DirectX, GLM, spdlog, etc.
4. ✅ **Configures CMake** - Sets up build system
5. ✅ **Compiles Project** - Builds Release executable
6. ✅ **Verifies Assets** - Ensures shaders are copied

**Time:** 15-25 minutes (first run), mostly dependency downloads

**Output:** You'll see a nice progress display with green checkmarks ✓

---

## Step 2: Run the Application

```powershell
.\run.ps1
```

Or manually:
```powershell
cd build\bin\Release
.\CortexEngine.exe
```

### Expected Result:

- **Window opens** (1280x720)
- **Spinning orange-red cube** on a dark blue background
- **Console output** showing FPS (should be ~60)
- **Press ESC** to exit

**Congratulations!** Phase 1 is complete! 🎉

---

## Troubleshooting

### "Execution of scripts is disabled"

Your PowerShell execution policy is restricted. Use:

```powershell
powershell -ExecutionPolicy Bypass -File setup.ps1
```

This bypasses the restriction for this one script only (safe).

---

### "Visual Studio not found"

Install **Visual Studio 2022** (Community Edition is free):
1. Download: https://visualstudio.microsoft.com/downloads/
2. Install with workload: **"Desktop development with C++"**
3. Rerun setup script

---

### "CMake not found"

Install CMake:
- **Option 1:** Through Visual Studio Installer ("CMake tools for Windows")
- **Option 2:** Direct download: https://cmake.org/download/

---

### SDL3 Installation Fails

SDL3 is very new. If vcpkg doesn't have it yet:

**Option 1:** Use SDL2 instead (requires minor code changes)
```powershell
# In vcpkg.json, change:
"sdl3" → "sdl2"
```

**Option 2:** Build SDL3 from source and point CMake to it

---

### Build Succeeds but Executable Won't Run

Check:
1. **DX12 Support** - GPU must support DirectX 12
2. **Graphics Drivers** - Update to latest drivers
3. **Assets Folder** - Should be in `build/bin/Release/assets/`

Run from correct directory:
```powershell
cd build\bin\Release
.\CortexEngine.exe
```

---

## What You Built

### File Count
- **34 files** total (17 source + 17 headers)
- **~3,500 lines** of modern C++20 code
- **1 HLSL shader** (Basic.hlsl)

### Key Systems
- ✅ DirectX 12 renderer with PBR lighting
- ✅ EnTT-based Entity Component System
- ✅ SDL3 windowing and input
- ✅ Hot-swappable texture system (ready for AI generation)
- ✅ Thread-safe architecture (ready for async AI loops)

---

## Next Steps

### Phase 1 Complete! What's Next?

1. **Explore the Code**
   - Read [PHASE1_COMPLETE.md](PHASE1_COMPLETE.md) for architecture details
   - Check out [src/](src/) to see the implementation

2. **Customize the Scene**
   - Edit [src/Core/Engine.cpp](src/Core/Engine.cpp) in `InitializeScene()`
   - Change cube color, add more objects, adjust camera

3. **Start Phase 2: The Neuro-Linguistic Link**
   - Integrate llama.cpp
   - Add text command parsing
   - Control engine with natural language

4. **Start Phase 3: The Dream Pipeline**
   - Install CUDA + TensorRT
   - Add AI texture generation
   - Hot-swap textures in real-time

---

## Daily Workflow

After initial setup, your workflow is:

```powershell
# 1. Make code changes
# (edit files in src/)

# 2. Rebuild
.\build.ps1

# 3. Run
.\run.ps1
```

**Build time:** ~30 seconds (incremental builds)

---

## Available Scripts

| Script | Purpose | When to Use |
|--------|---------|-------------|
| `setup.ps1` | Full setup + build | First time, after major changes |
| `build.ps1` | Quick rebuild | After code changes |
| `run.ps1` | Launch executable | Testing the application |

See [SCRIPTS.md](SCRIPTS.md) for detailed documentation.

---

## Development Tips

### Use Visual Studio

Open the solution file:
```powershell
start build\CortexEngine.sln
```

Benefits:
- IntelliSense (code completion)
- Visual debugger
- Integrated builds (F5 to compile + run)

### Debug Build

For debugging with breakpoints:
```powershell
.\setup.ps1 -BuildConfig Debug
.\run.ps1 -Config Debug
```

### Performance Profiling

Release builds are **much faster** (10x+):
- Debug: ~10 FPS with validation
- Release: ~60+ FPS

---

## Project Structure

```
CortexEngine/
├── setup.ps1              ← Run this first!
├── build.ps1              ← Rebuild after changes
├── run.ps1                ← Launch the application
├── src/
│   ├── main.cpp           ← Entry point
│   ├── Core/              ← Engine, Window, etc.
│   ├── Graphics/          ← Renderer, DX12 wrappers
│   ├── Scene/             ← ECS and components
│   └── Utils/             ← Helpers
├── assets/
│   └── shaders/
│       └── Basic.hlsl     ← Vertex/Pixel shaders
├── README.md              ← Project overview
├── BUILD.md               ← Manual build instructions
├── SCRIPTS.md             ← Script documentation
└── PHASE1_COMPLETE.md     ← Architecture deep-dive
```

---

## Performance Expectations

### Phase 1 (Current)
- **FPS:** 60 (VSync limited)
- **Frame Time:** ~16ms
- **GPU Usage:** <10%
- **RAM Usage:** ~200 MB

### Phase 2 (After LLM)
- **FPS:** 60 (unchanged)
- **LLM Inference:** ~500ms per command
- **RAM Usage:** ~2 GB (model loaded)

### Phase 3 (After AI Textures)
- **FPS:** 60 (unchanged)
- **Texture Gen:** 100-200ms
- **RAM Usage:** ~4 GB (diffusion model)

---

## Getting Help

- **Documentation:** Check [README.md](README.md), [BUILD.md](BUILD.md), [SCRIPTS.md](SCRIPTS.md)
- **Architecture:** Read [PHASE1_COMPLETE.md](PHASE1_COMPLETE.md)
- **Issues:** If something breaks, check console output for errors

---

## Success Checklist

After running `setup.ps1`, you should have:

- ✅ No red error messages in console
- ✅ Green "Setup Complete!" message
- ✅ File exists: `build/bin/Release/CortexEngine.exe`
- ✅ Folder exists: `build/bin/Release/assets/shaders/`
- ✅ Running `.\run.ps1` opens a window with a spinning cube
- ✅ Console shows FPS counter (~60)

If all checked, **Phase 1 is complete!** 🎉

---

## What Makes This Special

This isn't just a renderer demo. It's architected for:

1. **Real-Time AI Integration**
   - Hot-swappable textures
   - Zero-copy GPU memory operations
   - Thread-safe async loops

2. **Natural Language Control**
   - JSON command system
   - Grammar-constrained LLM output
   - Direct ECS manipulation

3. **Modern C++ Patterns**
   - Smart pointers throughout
   - Explicit error handling (Result<T>)
   - Move semantics
   - No raw pointers in hot paths

---

## Final Words

You've just built a **neural-native rendering engine** from scratch!

The foundation is solid, the architecture is clean, and the code is ready for Phases 2 and 3.

**Enjoy the spinning cube!** 🎮

Then dive into [PHASE1_COMPLETE.md](PHASE1_COMPLETE.md) to understand how everything works.

---

**Questions?** Check the documentation:
- [README.md](README.md) - Project overview
- [BUILD.md](BUILD.md) - Manual build guide
- [SCRIPTS.md](SCRIPTS.md) - Automation scripts
- [PHASE1_COMPLETE.md](PHASE1_COMPLETE.md) - Implementation details

Happy coding! 🚀
