# Project Cortex: Neural-Native Rendering Engine

Project Cortex is a real-time DirectX 12 renderer with integrated AI tooling.
It is designed as a portfolio-quality engine that demonstrates:

- Hybrid raster + ray tracing rendering on DX12
- Physically based material and surface classification
- Frame contracts, resource contracts, and repeatable smoke validation
- Visibility-buffer rendering, GPU culling, TAA, SSAO, SSR, bloom, and IBL
- RT shadows, reflections, GI targets, denoising, and signal-quality diagnostics
- Phase 3 public showcase scenes, graphics presets, HUD modes, and renderer UI controls
- Environment/IBL manifests with procedural fallback behavior
- Advanced material, particle, lighting-rig, and cinematic-post release foundations
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
- `tools/README.md` documents repeatable renderer validation tools.
- `RELEASE_READINESS.md` summarizes the current verified release gate.

---

## Release Validation

Run the full local release gate from the repository root:

```powershell
powershell -ExecutionPolicy Bypass -File CortexEngine/tools/run_release_validation.ps1
```

The gate builds Release, then runs the current public renderer suite:

- temporal validation and full RT showcase smokes,
- temporal camera-cut history invalidation smoke,
- render-graph transient alias/no-alias matrix,
- graphics settings persistence, graphics UI contract/runtime interaction, HUD mode, material editor, and preset contracts,
- public showcase scene, material lab, glass/water courtyard, effects showcase, visual baseline, and screenshot negative checks,
- Phase 3 visual matrix, IBL gallery, and fallback matrix validation,
- descriptor/memory stress scene for the historical persistent-descriptor budget,
- renderer ownership/full ownership audit, fatal error, environment manifest, advanced graphics catalog, and effects gallery contracts,
- particle-disabled zero-cost and RT reflection firefly/outlier gates,
- RT budget profile matrix and voxel backend smoke.

Use `-NoBuild` only when `build/bin/CortexEngine.exe` is already current.
Each step writes isolated logs under `build/bin/logs/runs`.

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
  - `Renderer` - orchestrates explicit state/service aggregates for frame
    planning, diagnostics, RT, pass resources, texture admission, command
    resources, and presentation.
  - Render graph infrastructure for HZB, shadows, visibility buffer, temporal
    passes, SSAO, SSR, bloom, and end-frame composition.
  - Frame/resource contracts that record pass reads/writes, budgets, histories,
    material parity, RT scheduling, descriptor pressure, and visual metrics.
  - HLSL shaders in `assets/shaders` for PBR, visibility-buffer lighting, RT
    composition, SSAO, SSR, TAA, post-process, motion vectors, and debug views.

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

The default real-time path combines explicit frame planning, raster passes, and
optional RT work:

1. Builds a scene snapshot and renderer budget plan.
2. Updates frame, lighting, temporal, and post-process constants.
3. Runs depth, shadow, motion-vector, HZB, and visibility-buffer work.
4. Runs GPU culling and indirect draw setup where enabled.
5. Executes material resolve and deferred/forward lighting.
6. Schedules RT shadows, reflections, and GI based on budget and readiness.
7. Denoises RT outputs and records raw/history reflection signal quality.
8. Runs temporal rejection, TAA, SSAO, SSR, bloom, tone mapping, and debug views.
9. Exports a frame contract and presents the final swapchain image.

Image-based lighting (IBL) uses:
- Diffuse irradiance from low-frequency environment maps.
- Specular reflections from roughness-controlled mips of the environment.

The RT showcase scene is the main renderer validation target. Its smoke test
fails if reflections are scheduled but missing required inputs, if RT material
parity breaks, if raw/history reflection signal becomes empty, if visual metrics
fall outside budget, or if descriptor/memory limits regress.

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

- **Phase 2 - Renderer Architecture and Contracts**
  - Stabilize the renderer around measurement-first contracts, explicit pass
    state, resource/budget validation, RT scheduling, temporal validation, and
    repeatable showcase gates.

- **Phase 3 - Public Renderer Surface**
  - Validate the public graphics control surface, environment/IBL policy,
    polished showcase scenes, material editor checks, clean HUD modes, advanced
    material/effects foundations, and pass-owned renderer state boundaries.

- **Architect / LLM Control**
  - Integrate Llama.cpp, define a constrained JSON command format, and drive
    the scene from natural-language prompts without blocking the render loop.

- **Dream Pipeline**
  - Integrate SDXL-Turbo via TensorRT, generate textures asynchronously,
    and stream them into the renderer without stalling the frame.

Current active polish is post-release hardening: keep validation reproducible,
avoid reintroducing loose renderer ownership, and grow future effects only
behind the same preset, budget, frame-contract, and visual-validation gates.

---

## License and Usage

Project Cortex is a learning and research project.
Please review the licenses of the bundled third-party libraries and models
(llama.cpp, SDXL-Turbo, etc.) before using it in commercial contexts.
