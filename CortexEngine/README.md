# Project Cortex: Neural-Native Rendering Engine

A real-time rendering engine with integrated AI capabilities for Windows, featuring:
- DirectX 12 rendering with PBR support
- Real-time AI texture generation via TensorRT (Phase 3)
- Natural language scene manipulation via Llama.cpp (Phase 2)
- EnTT-based Entity Component System
- Zero-copy CUDA-DX12 interop for AI-generated assets

## 🚀 Quick Start (One Command!)

```powershell
powershell -ExecutionPolicy Bypass -File setup.ps1
```

This automatically:
- ✅ Installs all dependencies (vcpkg, SDL3, EnTT, DirectX, etc.)
- ✅ Configures CMake build
- ✅ Compiles the project
- ✅ Verifies everything is ready to run

**Then run:** `.\run.ps1`

See [SCRIPTS.md](SCRIPTS.md) for all automation scripts or [BUILD.md](BUILD.md) for manual instructions.

## Architecture Overview

### The Three Async Loops

1. **Main Render Loop** (60/120 FPS)
   - Standard game loop: Input → Update → Render
   - Deferred renderer with PBR
   - Uses "placeholder" textures while AI generates

2. **The Architect** (LLM - Phase 2)
   - Llama.cpp running on CPU/GPU
   - Parses natural language → JSON scene commands
   - Direct ECS manipulation

3. **The Dreamer** (Diffusion - Phase 3)
   - TensorRT/CoreML image generation
   - Writes directly to GPU texture memory (zero-copy)
   - Hot-swaps textures in real-time

## Current Status: Phase 1 - The Iron Foundation ✅

### Completed Components

#### Graphics RHI (Rendering Hardware Interface)
- [x] `DX12Device` - Device initialization with debug layer support
- [x] `DX12CommandQueue` - Command submission and GPU synchronization
- [x] `DescriptorHeap` - Dynamic descriptor allocation (ring buffer)
- [x] `DX12Texture` - Texture management with hot-swap support

#### Core Systems
- [x] `Window` - SDL3 integration with DX12 swapchain
- [x] `ServiceLocator` - Global service access pattern
- [x] `Result<T>` - Modern error handling (std::expected alternative)

#### Scene Management
- [x] `ECS_Registry` - EnTT wrapper with entity management
- [x] Components:
  - `TransformComponent` - Position, rotation, scale
  - `RenderableComponent` - Mesh + texture references
  - `TagComponent` - Semantic labels for AI (Phase 4)
  - `CameraComponent` - View/projection matrices
  - `RotationComponent` - Animation helper

#### Shaders
- [x] `Basic.hlsl` - PBR-style vertex/pixel shaders
- [x] `ShaderTypes.h` - Shared C++/HLSL structures

### Directory Structure

```
CortexEngine/
├── src/
│   ├── Core/              # Engine core (Window, main loop)
│   ├── Graphics/
│   │   ├── RHI/           # DirectX 12 wrappers
│   │   └── Shaders/       # HLSL shaders
│   ├── AI/
│   │   ├── LLM/           # Phase 2: Llama.cpp integration
│   │   └── Vision/        # Phase 3: TensorRT diffusion
│   ├── Scene/             # ECS and components
│   └── Utils/             # Utilities (Result, FileUtils)
├── assets/
│   ├── shaders/           # Compiled shader bytecode
│   ├── textures/          # Texture files
│   └── models/            # GGUF and TensorRT models
└── vendor/                # Third-party libraries
```

## Building

### Prerequisites

- Windows 10/11 with Windows SDK
- Visual Studio 2022 (or newer)
- CMake 3.20+
- vcpkg (for package management)

### Dependencies (via vcpkg)

- SDL3 - Windowing
- EnTT - Entity Component System
- GLM - Math library
- spdlog - Logging
- nlohmann-json - JSON parsing
- DirectX-Headers - DX12 headers
- DirectXTK12 - DirectX helper utilities

### Build Instructions

```bash
# Set vcpkg toolchain (if not in environment)
set VCPKG_ROOT=C:\path\to\vcpkg

# Configure
cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

# Build
cmake --build build --config Release

# Run
build\bin\Release\CortexEngine.exe
```

## Phase Roadmap

### ✅ Phase 1: The Iron Foundation
**Goal:** Render a textured spinning cube

- [x] DX12 device and command queue
- [x] Descriptor heap system
- [x] Texture management
- [x] SDL3 windowing
- [x] EnTT ECS integration
- [x] Basic PBR shaders
- [ ] **TODO:** Renderer implementation
- [ ] **TODO:** Cube mesh generation
- [ ] **TODO:** Main game loop
- [ ] **TEST:** Spinning cube milestone

### Phase 2: The Neuro-Linguistic Link (Next)
**Goal:** Control engine with text commands

- [ ] Integrate llama.cpp as library
- [ ] Implement GBNF grammar for strict JSON output
- [ ] JSON command parser → ECS actions
- [ ] ImGui console overlay
- **Milestone:** Type "Add a red light" → red light spawns

### Phase 3: The Dream Pipeline
**Goal:** Generate textures on the fly

- [ ] Install CUDA Toolkit + TensorRT
- [ ] Convert SDXL-Turbo to ONNX → TensorRT
- [ ] Implement texture request queue
- [ ] CUDA-DX12 zero-copy interop
- **Milestone:** Type "Wood texture" → cube becomes wood

### Phase 4: Semantic Scene Graph
**Goal:** AI understands scene context

- [ ] Scene description serializer
- [ ] Context-aware generation
- [ ] Spatial reasoning for LLM

### Phase 5: Optimization & Polish
- [ ] Texture caching (hash-based)
- [ ] Async texture uploads
- [ ] PBR material generation (normal/roughness maps)

## Key Technical Features

### Hot-Swappable Textures
The engine uses a "placeholder → replace" strategy:
1. Entity created with white placeholder texture
2. Renderer draws immediately (no stutter)
3. AI generates texture in background
4. `UpdateData()` hot-swaps GPU texture memory
5. Next frame automatically shows new texture

### Zero-Copy CUDA-DX12 Interop (Phase 3)
```cpp
// Create DX12 texture with ALLOW_SIMULTANEOUS_ACCESS
// Get shared handle → Import to CUDA
// Diffusion writes directly to CUDA array
// DX12 reads same memory (zero CPU copy)
```

### Grammar-Constrained LLM (Phase 2)
Instead of prompt engineering, use GBNF grammars:
```
root ::= "{" "action" ":" action "," params "}"
action ::= "CREATE_LIGHT" | "SET_TEXTURE" | ...
```
Guarantees valid JSON output.

## Performance Targets

- Render Loop: 60+ FPS (120 target)
- LLM Inference: <500ms (Q4_K_M quantization)
- Texture Generation: 100-200ms (SDXL-Turbo, 1-4 steps)

## License

This is a learning/research project. See individual dependencies for their licenses.

## Acknowledgments

- Microsoft DirectX Team - DX12 samples
- EnTT - Amazing ECS library
- llama.cpp - LLM inference engine
- Stable Diffusion - Image generation models
