# Phase 1 – Iron Foundation (Complete)

This document summarizes the work completed in Phase 1: bringing up the core renderer and engine
infrastructure for Project Cortex.

---

## Goals

- Initialize a modern DX12 rendering pipeline.
- Establish a clean Entity Component System (ECS) using EnTT.
- Provide a minimal scene (spinning test mesh) to validate the stack.
- Lay groundwork for later AI-driven features without coupling them to rendering.

---

## Core Architecture

### Graphics RHI (Rendering Hardware Interface)

Files under `src/Graphics/RHI/`:

- **DX12Device** – device creation, debug layers, adapter selection.
- **DX12CommandQueue** – graphics and upload queues with fence-based synchronization.
- **DX12Texture** – texture creation, mip management, and hot-swap support.
- **DX12Pipeline** – root signature and pipeline state object management.
- **DescriptorHeap** – descriptor heap manager and ring-buffer style allocation.
- **d3dx12.h** – helper utilities from Microsoft’s DX12 samples.

### Renderer

Files under `src/Graphics/`:

- **Renderer** – orchestrates the frame:
  - Per-frame constant buffers (view/projection, lighting).
  - Per-object constant buffers (model matrix, material parameters).
  - Mesh upload to GPU and draw submission.
  - Shadow map, SSAO, SSR, bloom, and post-process stages (foundation laid in Phase 1).
- **ShaderTypes** – shared C++/HLSL structures for constant buffers and material layout.
- HLSL shaders in `assets/shaders`:
  - `Basic.hlsl` – PBR shading, skybox, and debug views.

### Window and Platform

Files under `src/Core/`:

- **Window** – SDL3 windowing, DX12 swapchain, resize handling, back-buffer management.
- **Engine** – main loop, timing, and orchestration of the renderer and services.
- **ServiceLocator** – lightweight global registry for subsystems (Renderer, LLMService, etc.).

### Scene Management

Files under `src/Scene/`:

- **ECS_Registry** – EnTT wrapper for entity creation, destruction, and queries.
- **Components** – standard component set:
  - `TransformComponent` – position, rotation (quaternion), scale.
  - `RenderableComponent` – mesh handle, material parameters, texture references.
  - `CameraComponent` – view/projection matrices and camera parameters.

---

## Phase 1 Milestone

With Phase 1 complete, the engine can:

- Open a DX12 window and maintain a steady frame rate.
- Render a test mesh with basic PBR shading.
- Manage GPU resources (buffers, textures, descriptors) robustly.
- Evolve toward more complex scenes and post-processing.

This foundation enables Phase 2 (LLM integration) and Phase 3 (diffusion textures) without
requiring changes to the low-level graphics stack.

