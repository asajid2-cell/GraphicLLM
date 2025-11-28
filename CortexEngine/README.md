# Project Cortex: Neural-Native Rendering Engine

Project Cortex is a real-time DirectX 12 renderer with integrated AI tooling.  
It is designed as a portfolio-quality engine that demonstrates:

- Physically based rendering (PBR) on DX12
- EnTT-based Entity Component System
- Natural-language scene control via Llama.cpp
- Asynchronous diffusion-based texture generation (TensorRT)
- CUDA and DX12 interop for AI-generated content

---

## Quick Start

From the `CortexEngine` directory:

```powershell
powershell -ExecutionPolicy Bypass -File .\setup.ps1
.\run.ps1
```

`setup.ps1` installs dependencies, configures CMake, builds the engine, and verifies that
the executable and shader assets are present.

For more detail:
- `SCRIPTS.md` documents all helper scripts.
- `BUILD.md` contains manual build instructions.

---

## High-Level Architecture

### Core subsystems

- **Core**
  - `Engine` - main loop, frame timing, and orchestration.
  - `Window` - SDL3 windowing, input, and DX12 swapchain creation.
  - `ServiceLocator` - central registry for engine-wide services.

- **Graphics**
  - DirectX 12 RHI (`DX12Device`, `DX12CommandQueue`, `DX12Texture`,
    `DX12Pipeline`, `DescriptorHeap`).
  - `Renderer` - forward PBR lighting, cascaded shadows, SSAO, SSR, bloom,
    tone mapping, and FXAA/TAA.
  - HLSL shaders in `assets/shaders` (PBR, SSAO, SSR, post-process, motion vectors).

- **Scene**
  - EnTT-based `ECS_Registry` and component set:
    `TransformComponent`, `RenderableComponent`, `CameraComponent`,
    `LightComponent`, tagging and utility components.

- **AI / LLM**
  - `LLMService` - wraps Llama.cpp and runs on a background thread.
  - `CommandQueue` and `SceneCommands` - parse JSON commands from the model
    and apply them to the ECS (spawn entities, adjust lights, change materials).
  - `RegressionTests` - automated command sequences for validation.

- **AI / Vision (Diffusion)**
  - `DiffusionEngine` - TensorRT or CPU stub for SDXL-Turbo.
  - `DreamerService` - asynchronous texture generation and hand-off into GPU textures.

---

## Render Pipeline Overview

Per frame the renderer:

1. Updates frame constants and TAA jitter.
2. Renders cascaded shadow maps for the main directional light.
3. Renders the skybox using HDR environment maps (diffuse and specular IBL).
4. Renders geometry with PBR shading into HDR and normal/roughness targets.
5. Runs SSAO, SSR, and motion-vector passes.
6. Applies bloom and tone mapping in a post-process pass.
7. Presents the final image to the swapchain.

Image-based lighting (IBL) uses:
- Diffuse irradiance from low-frequency environment maps.
- Specular reflections from roughness-controlled mips of the environment.

---

## Directory Structure (CortexEngine/)

```text
assets/             # Shaders and sample textures (large HDR/EXR IBL assets are git-ignored)
  shaders/

models/             # LLM and diffusion models (gguf, TensorRT engines; git-ignored)

src/
  Core/             # Engine, Window, ServiceLocator
  Graphics/         # Renderer, RHI, shaders, post-processing
    RHI/            # DX12 device, command queue, pipeline, texture, descriptor heap
  AI/
    LLM/            # LLMService, CommandQueue, SceneCommands, RegressionTests
    Vision/         # DiffusionEngine, DreamerService
  Scene/            # ECS_Registry and components
  UI/               # Debug HUD and prompt/console UI
  Utils/            # FileUtils, MeshGenerator, Result<T>, GLTF loader

vendor/             # Third-party libraries (llama.cpp, stb, TinyEXR, etc.)
build/              # CMake build directory (git-ignored)
```

---

## Building Manually

### Prerequisites

- Windows 10/11 with Windows SDK
- Visual Studio 2022 (or newer) with C++ desktop workload
- CMake 3.20 or later
- vcpkg for dependency management

### Dependencies (via vcpkg)

- SDL3 - windowing and input
- EnTT - ECS
- GLM - math
- spdlog - logging
- nlohmann-json - JSON parsing
- DirectX-Headers - DX12 headers
- DirectXTK12 - helper utilities for DX12

### Example build commands

```bat
set VCPKG_ROOT=C:\path\to\vcpkg

cmake -B build -S . ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake

cmake --build build --config Release

build\bin\Release\CortexEngine.exe
```

---

## Phase Roadmap (Abbreviated)

- **Phase 1 - Iron Foundation**
  - Bring up DX12 renderer, ECS, and PBR shading on a spinning test mesh.

- **Phase 2 - Neuro-Linguistic Control**
  - Integrate Llama.cpp, define a constrained JSON command format,
    and drive the scene from natural-language prompts.

- **Phase 3 - Dream Pipeline**
  - Integrate SDXL-Turbo via TensorRT, generate textures asynchronously,
    and stream them into the renderer without stalling the frame.

Later phases focus on higher-level semantic scene understanding and polish
(caching, streaming, and tooling).

---

## License and Usage

Project Cortex is a learning and research project.  
Please review the licenses of the bundled third-party libraries and models
(llama.cpp, SDXL-Turbo, etc.) before using it in commercial contexts.

