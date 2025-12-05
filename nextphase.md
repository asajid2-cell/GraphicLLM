# CortexEngine: Next-Generation Development Roadmap

> **Live Development Tracker**
> Last Updated: 2024-12-05
> Status: Planning Phase

---

## Quick Navigation
- [Current State](#current-engine-state)
- [Phase 1: Foundation](#phase-1-foundation-upgrades)
- [Phase 2: Advanced Rendering](#phase-2-advanced-rendering-architecture)
- [Phase 3: GI & Lighting](#phase-3-global-illumination--lighting)
- [Phase 4: Post-Processing](#phase-4-post-processing--anti-aliasing)
- [Phase 5: Editor](#phase-5-editor--tools)
- [Phase 6: Gameplay](#phase-6-gameplay-systems)
- [Priority Matrix](#implementation-priority-matrix)

---

## Progress Overview

| Phase | Status | Progress |
|-------|--------|----------|
| Phase 1: Foundation | Not Started | ░░░░░░░░░░ 0% |
| Phase 2: Advanced Rendering | Not Started | ░░░░░░░░░░ 0% |
| Phase 3: GI & Lighting | Not Started | ░░░░░░░░░░ 0% |
| Phase 4: Post-Processing | Not Started | ░░░░░░░░░░ 0% |
| Phase 5: Editor & Tools | Not Started | ░░░░░░░░░░ 0% |
| Phase 6: Gameplay Systems | Not Started | ░░░░░░░░░░ 0% |

---

## Current Engine State

### Implemented Features
| Category | Features | Status |
|----------|----------|--------|
| **Core Rendering** | DX12 triple-buffered, PBR forward (GGX BRDF), 16 max lights | ✅ Complete |
| **Ray Tracing** | DXR shadows, reflections, 2-ray diffuse GI | ✅ Complete |
| **Shadows** | 3-cascade CSM + PCSS, 3 local light shadows | ✅ Complete |
| **Post-Processing** | TAA (16-sample), FXAA, Bloom (3-level), Motion Blur | ✅ Complete |
| **Screen-Space** | SSAO (8-sample), SSR (64-step raymarch) | ✅ Complete |
| **Environment** | IBL with presets, exponential height fog, god rays | ✅ Complete |
| **Scene** | EnTT ECS, transform hierarchy, particles, water (Gerstner) | ✅ Complete |
| **Assets** | Memory budgeting (~3.5GB tex, 1.5GB geo, 1.5GB RT) | ✅ Complete |
| **Experimental** | Voxel renderer (384³ grid) | ⚠️ Experimental |

### Not Yet Implemented
- [ ] True bindless rendering (uses traditional descriptor tables)
- [ ] Mesh shaders / GPU culling (CPU ECS queries)
- [ ] Render graph (direct command list per pass)
- [ ] Compute-based deferred
- [ ] VRS (Variable Rate Shading)
- [ ] DDGI / probe-based GI
- [ ] Virtual Shadow Maps
- [ ] TSR / FSR / DLSS
- [ ] Editor tools (dockspace, gizmos, undo/redo)
- [ ] Job system / fiber threading
- [ ] Scripting (C#/Lua)
- [ ] Physics integration

---

# Phase 1: Foundation Upgrades
**Goal:** Modernize rendering architecture for GPU-driven workflows
**Status:** Not Started
**Dependencies:** None (Base Phase)

## 1.1 Bindless Resource Model
- [ ] **Status:** Not Started
- **Complexity:** High | **Impact:** Critical Foundation

### What & Why
Convert from traditional descriptor table binding to a single large shader-visible heap with global bindless indices. Enables GPU culling to dynamically select materials, reduces state changes, and is required for visibility buffer rendering.

### Implementation
**C++ Changes:**
```cpp
// New: BindlessResourceManager
class BindlessResourceManager {
    ComPtr<ID3D12DescriptorHeap> m_bindlessHeap; // 16384+ descriptors
    std::vector<uint32_t> m_freeList;
public:
    uint32_t AllocateTextureIndex(DX12Texture* tex);
    void ReleaseIndex(uint32_t index);
};
```

**Shader Changes (Basic.hlsl):**
```hlsl
// Replace register(t0) with SM6.6 heap access
#define BINDLESS_TEXTURE(idx) ResourceDescriptorHeap[idx]
Texture2D GetAlbedoTexture() { return BINDLESS_TEXTURE(g_MaterialData.albedoIndex); }
```

### Files to Modify
- [ ] `CortexEngine/src/Graphics/RHI/DescriptorHeap.h` - Add persistent bindless allocator
- [ ] `CortexEngine/src/Graphics/RHI/DX12Texture.h` - Add `uint32_t bindlessIndex`
- [ ] `CortexEngine/src/Graphics/Renderer.cpp` - Modify material binding
- [ ] `CortexEngine/assets/shaders/Basic.hlsl` - Bindless texture access

### Configuration
- `maxBindlessTextures` (default: 16384)
- `maxBindlessBuffers` (default: 8192)
- `enableBindlessValidation` (debug builds)

---

## 1.2 Render Graph System
- [ ] **Status:** Not Started
- **Complexity:** Very High | **Impact:** High

### What & Why
Replace direct command list recording with a declarative render graph that automatically handles resource transitions, barrier batching, and pass dependency resolution. Current manual state tracking (`m_hdrState`, `m_depthState`) is error-prone.

### Implementation
```cpp
class RenderGraph {
    struct PassNode {
        std::string name;
        std::function<void(RenderContext&)> execute;
        std::vector<ResourceHandle> reads, writes;
    };
    void AddPass(const std::string& name, PassBuilder& builder);
    void Compile(); // Generates barriers, aliasing
    void Execute(ID3D12CommandList* cmdList);
};

// Usage in Renderer.cpp
void Renderer::BuildRenderGraph() {
    m_graph.AddPass("ShadowPass", [this](auto& ctx) { RenderShadowPass(ctx); });
    m_graph.AddPass("MainPass", [this](auto& ctx) { RenderScene(ctx); });
    m_graph.AddPass("TAA", [this](auto& ctx) { RenderTAA(ctx); });
    // ...
}
```

### Files to Create/Modify
- [ ] NEW: `RenderGraph.h`, `RenderGraph.cpp`
- [ ] NEW: `RenderResource.h` (transient resource abstraction)
- [ ] `CortexEngine/src/Graphics/Renderer.cpp` - Wrap all `Render*()` methods

---

## 1.3 GPU Culling Pipeline
- [ ] **Status:** Not Started
- **Complexity:** High | **Impact:** High

### What & Why
Move frustum and occlusion culling from CPU ECS queries to GPU compute shaders. Current `RenderScene()` iterates all entities on CPU - this doesn't scale.

### Implementation
**New Shader (GPUCulling.hlsl):**
```hlsl
struct InstanceData {
    float4x4 worldMatrix;
    float4 boundingSphere; // xyz=center, w=radius
    uint meshIndex;
    uint materialIndex;
};

StructuredBuffer<InstanceData> g_Instances : register(t0);
Texture2D<float> g_HZB : register(t1); // Hierarchical Z-Buffer
RWStructuredBuffer<uint> g_VisibleIndices : register(u0);
RWByteAddressBuffer g_IndirectArgs : register(u1);

[numthreads(64, 1, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    InstanceData inst = g_Instances[tid.x];

    // Frustum culling
    if (!FrustumTest(inst.boundingSphere)) return;

    // HZB occlusion culling
    if (!OcclusionTest(inst.boundingSphere, g_HZB)) return;

    // Atomic append
    uint idx;
    InterlockedAdd(g_IndirectArgs.Load(0), 1, idx);
    g_VisibleIndices[idx] = tid.x;
}
```

### Files to Create/Modify
- [ ] NEW: `GPUCulling.hlsl`
- [ ] NEW: `GPUCullingSystem.h/cpp`
- [ ] `CortexEngine/src/Graphics/Renderer.cpp` - Add HZB pass, culling dispatch

---

## 1.4 Indirect Draw Architecture
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** High

Replace explicit `DrawIndexedInstanced` calls with `ExecuteIndirect` driven by GPU-generated argument buffers.

```cpp
// Command signature for indirect draws
D3D12_INDIRECT_ARGUMENT_DESC args[2] = {
    { D3D12_INDIRECT_ARGUMENT_TYPE_CONSTANT, ... },  // Material index
    { D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED, ... }
};
m_device->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&m_drawSignature));

// In RenderScene:
m_cmdList->ExecuteIndirect(m_drawSignature, maxDraws, m_indirectArgsBuffer, 0, m_countBuffer, 0);
```

---

## 1.5 Async Compute Queue
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** Medium

Add a separate compute queue for parallel operations (SSAO, bloom blur, TAA history, culling).

### Files to Modify
- [ ] `CortexEngine/src/Graphics/RHI/DX12CommandQueue.h` - Add compute queue
- [ ] `CortexEngine/src/Graphics/Renderer.cpp` - Fence sync between queues

---

# Phase 2: Advanced Rendering Architecture
**Goal:** Implement next-gen geometry processing (Nanite-style)
**Status:** Not Started
**Dependencies:** Phase 1 (Bindless, GPU Culling, Indirect Draw)

## 2.1 Visibility Buffer Rendering
- [ ] **Status:** Not Started
- **Complexity:** Very High | **Impact:** Critical

### What & Why
Two-phase rendering: rasterize triangle IDs to visibility buffer, then shade via compute. Decouples geometry complexity from shading rate.

### Implementation
**Phase 1 - Visibility Pass:**
```hlsl
// VisibilityPass.hlsl
struct VSOutput {
    float4 position : SV_Position;
    nointerpolation uint triangleID : TRIANGLE_ID;
    nointerpolation uint instanceID : INSTANCE_ID;
};
```

**Phase 2 - Material Resolve:**
```hlsl
// MaterialResolve.hlsl
Texture2D<uint2> g_VisibilityBuffer : register(t0);

[numthreads(8, 8, 1)]
void CSMain(uint3 tid : SV_DispatchThreadID) {
    uint2 visData = g_VisibilityBuffer[tid.xy];
    uint triangleID = visData.x;
    uint instanceID = visData.y;

    // Fetch vertices, interpolate barycentrics
    // Evaluate material, output to G-buffer
}
```

### Files to Create
- [ ] NEW: `VisibilityPass.hlsl`
- [ ] NEW: `MaterialResolve.hlsl`
- [ ] NEW: `VisibilityBufferRenderer.h/cpp`

---

## 2.2 Meshlet-Based Geometry Pipeline
- [ ] **Status:** Not Started
- **Complexity:** Very High | **Impact:** High

### What & Why
Decompose meshes into clusters of ~64-128 triangles for fine-grained GPU culling.

```cpp
struct Meshlet {
    uint32_t vertexOffset;
    uint32_t triangleOffset;
    uint32_t vertexCount;     // max 64
    uint32_t triangleCount;   // max 126
    float4 boundingSphere;
    float4 normalCone;        // for backface culling
};

struct MeshletMesh {
    std::vector<Meshlet> meshlets;
    std::vector<uint32_t> uniqueVertexIndices;
    std::vector<uint8_t> primitiveIndices;  // packed 3 bytes per tri
};
```

### Files to Modify
- [ ] `CortexEngine/src/Graphics/MeshBuffers.h` - Add meshlet structures
- [ ] `CortexEngine/src/Scene/Components.h` - Extend `MeshData`
- [ ] NEW: `MeshletBuilder.h/cpp` - Meshlet generation from raw mesh

---

## 2.3 Mesh Shader Pipeline
- [ ] **Status:** Not Started
- **Complexity:** High | **Impact:** High

DX12 Mesh Shaders (SM6.5) for native meshlet processing.

```hlsl
// MeshletAmplify.hlsl - Amplification shader for culling
[numthreads(1, 1, 1)]
void ASMain(uint gid : SV_GroupID) {
    Meshlet meshlet = g_Meshlets[gid];
    if (CullMeshlet(meshlet)) return;
    DispatchMesh(1, 1, 1, meshlet);
}

// MeshletMesh.hlsl - Mesh shader for output
[outputtopology("triangle")]
[numthreads(128, 1, 1)]
void MSMain(uint gtid : SV_GroupThreadID,
            out vertices VertexOutput verts[64],
            out indices uint3 tris[126]) {
    // Emit vertices and primitives
}
```

### Files to Create/Modify
- [ ] NEW: `MeshletAmplify.hlsl`
- [ ] NEW: `MeshletMesh.hlsl`
- [ ] `CortexEngine/src/Graphics/RHI/DX12Pipeline.h` - Add mesh pipeline creation

---

## 2.4 Variable Rate Shading (VRS)
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** Medium

Reduce shading rate in low-detail regions using D3D12 VRS Tier 2.

### Implementation
- Detect VRS tier via `D3D12_FEATURE_D3D12_OPTIONS6`
- Generate VRS image from velocity/depth gradients
- Apply 2x2 or 4x4 shading in motion blur / peripheral regions

### Configuration
- `vrsMode` (off, per-draw, image-based)
- `vrsMotionThreshold`, `vrsDepthThreshold`

---

## 2.5 Virtual Geometry System (Nanite-Style LOD)
- [ ] **Status:** Not Started
- **Complexity:** Extremely High | **Impact:** Very High

Runtime geometry streaming with DAG-based LOD hierarchy.

### Core Concepts
- Cluster DAG with parent-child relationships
- Screen-space error metric for LOD selection
- Software rasterizer for sub-pixel triangles
- GPU feedback for streaming decisions

*This is a multi-release feature requiring meshlets + visibility buffer first.*

---

# Phase 3: Global Illumination & Lighting
**Goal:** Production-quality GI comparable to Lumen
**Status:** Not Started
**Dependencies:** Phase 1 (Render Graph), Phase 2 (Visibility Buffer)

## 3.1 DDGI (Dynamic Diffuse Global Illumination)
- [ ] **Status:** Not Started
- **Complexity:** Very High | **Impact:** Very High

### What & Why
Probe-based irradiance field with runtime RT update. Replaces current 2-ray RTGI with stable, noise-free diffuse GI.

### Implementation
**Probe Update Shader:**
```hlsl
// DDGIProbeUpdate.hlsl
RWTexture2D<float4> g_ProbeIrradiance : register(u0);  // Octahedral encoding
RWTexture2D<float2> g_ProbeDistance : register(u1);

[shader("raygeneration")]
void RayGen_ProbeTrace() {
    uint probeIndex = DispatchRaysIndex().x;
    float3 probePos = GetProbePosition(probeIndex);

    for (int i = 0; i < g_RaysPerProbe; i++) {
        float3 dir = FibonacciSphere(i, g_RaysPerProbe);
        RayDesc ray = { probePos, 0.01, dir, g_MaxTraceDistance };

        TraceRay(g_TLAS, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);

        // Update irradiance SH
        UpdateProbeIrradiance(probeIndex, dir, payload.radiance);
        UpdateProbeDistance(probeIndex, dir, payload.hitT);
    }
}
```

**Sampling in Main Shader:**
```hlsl
// In Basic.hlsl
float3 SampleDDGI(float3 worldPos, float3 normal) {
    // Find 8 surrounding probes
    // Trilinear + visibility-weighted interpolation
    float3 irradiance = 0;
    for (int i = 0; i < 8; i++) {
        float3 probePos = GetCornerProbePos(worldPos, i);
        float weight = GetProbeWeight(worldPos, normal, probePos);
        irradiance += weight * SampleProbeIrradiance(probePos, normal);
    }
    return irradiance;
}
```

### Configuration
- `ddgiProbeSpacing` (default: 2.0 meters)
- `ddgiRaysPerProbe` (128, 256, 512)
- `ddgiCascadeCount` (1-4 for large worlds)
- `ddgiHysteresis` (0.97 for temporal stability)

### Files to Create/Modify
- [ ] NEW: `DDGIProbeUpdate.hlsl`
- [ ] NEW: `DDGIVolume.h/cpp`
- [ ] `CortexEngine/src/Graphics/RHI/DX12Raytracing.h` - Add DDGI pipeline
- [ ] `CortexEngine/assets/shaders/Basic.hlsl` - DDGI sampling

---

## 3.2 ReSTIR for Direct Lighting
- [ ] **Status:** Not Started
- **Complexity:** Very High | **Impact:** High

### What & Why
Reservoir-based importance resampling for many-light scenes. Current 16-light limit doesn't scale.

```hlsl
struct Reservoir {
    uint lightIndex;
    float weightSum;
    uint M;  // sample count
    float W;  // resampling weight
};

// 1. Initial candidates (per pixel)
// 2. Temporal reuse (from previous frame)
// 3. Spatial reuse (from neighbors)
// 4. Final shading with selected light
```

### Configuration
- `restirMaxLights` (1000+)
- `restirTemporalReuse` (on/off)
- `restirSpatialNeighbors` (5-20)

---

## 3.3 Virtual Shadow Maps
- [ ] **Status:** Not Started
- **Complexity:** Very High | **Impact:** High

### What & Why
Replace 3-cascade CSM with clipmap-based virtual shadow maps for consistent resolution at all distances.

### Core Concepts
- 16K virtual resolution, 128-texel pages
- Page table indirection
- GPU feedback for page allocation
- On-demand rendering of dirty pages

### Files to Modify
- [ ] `CortexEngine/src/Graphics/Renderer.cpp` - Replace `RenderShadowPass()`
- [ ] NEW: `VirtualShadowMap.h/cpp`

---

## 3.4 Screen-Space Global Illumination (SSGI)
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** Medium

Short-range GI bounce in screen space as complement to DDGI.

```hlsl
// Similar to SSR but samples color buffer for diffuse bounce
float3 SSGI(float3 worldPos, float3 normal, float2 uv) {
    float3 rayDir = CosineWeightedHemisphere(normal);
    float3 hitColor = TraceScreenSpace(uv, rayDir);
    return hitColor * saturate(dot(normal, rayDir));
}
```

---

## 3.5 Improved RT Reflection Denoising
- [ ] **Status:** Not Started
- **Complexity:** High | **Impact:** High

Replace basic 5-tap filter with proper spatiotemporal denoiser (SVGF or ReLAX-style).

### Denoiser Passes
1. Temporal accumulation with motion vectors
2. Variance estimation
3. A-trous wavelet filtering (5 iterations)
4. Temporal anti-aliasing of result

---

# Phase 4: Post-Processing & Anti-Aliasing
**Goal:** Modern upscaling and physically-based effects
**Status:** Not Started
**Dependencies:** Phase 1 (Render Graph), partially Phase 3

## 4.1 Temporal Super Resolution (FSR 3.0 / DLSS 3.5)
- [ ] **Status:** Not Started
- **Complexity:** High | **Impact:** Very High

### What & Why
Render at lower resolution, upscale with temporal information. 2-4x performance gain.

### Integration Options
| Upscaler | License | Vendor | Frame Gen |
|----------|---------|--------|-----------|
| FSR 3.0 | MIT | AMD | Yes |
| DLSS 3.5 | Proprietary | NVIDIA | Yes |
| XeSS | MIT | Intel | No |
| Custom TSR | N/A | Internal | Optional |

### Implementation
```cpp
// Integrate FSR 3.0 SDK
#include <ffx_fsr3.h>

FfxFsr3Context m_fsr3Context;
FfxFsr3ContextDescription desc = {
    .flags = FFX_FSR3_ENABLE_UPSCALING_ONLY,
    .maxRenderSize = { 1920, 1080 },
    .displaySize = { 3840, 2160 }
};
ffxFsr3ContextCreate(&m_fsr3Context, &desc);
```

### Configuration
- `upscaler` (native, FSR, DLSS, XeSS)
- `upscaleQuality` (ultra, quality, balanced, performance)
- `sharpness` (0.0-1.0)
- `frameGeneration` (FSR 3 / DLSS 3 only)

---

## 4.2 Ground Truth Ambient Occlusion (GTAO)
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** Medium

Replace 8-sample SSAO with higher quality GTAO.

```hlsl
float GTAO(float3 viewPos, float3 viewNormal) {
    float ao = 0;
    for (int slice = 0; slice < 4; slice++) {
        float2 dir = GetSliceDirection(slice);
        float2 horizons = FindHorizons(viewPos, dir);
        ao += IntegrateArc(horizons, viewNormal);
    }
    return ao / 4;
}
```

---

## 4.3 Volumetric Fog & Lighting
- [ ] **Status:** Not Started
- **Complexity:** High | **Impact:** High

Full 3D froxel-based volumetric fog with proper light scattering.

### Implementation
1. Build 3D froxel grid (160x90x64)
2. Inject lights and density
3. Raymarch for scattering
4. Apply in post-process

### Configuration
- `froxelGridResolution` (160x90x64)
- `volumetricScattering` (0.0-1.0)
- `volumetricDensity`, `volumetricHeight`
- `temporalReprojection` (on/off)

---

## 4.4 Physically-Based Bloom
- [ ] **Status:** Not Started
- **Complexity:** Low | **Impact:** Medium

Spectral bloom with energy conservation.

- Chromatic fringing (RGB separation)
- Lens-based convolution kernel
- HDR energy conservation

---

## 4.5 Screen-Space Subsurface Scattering
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** Medium

For skin, wax, leaves, etc.

```hlsl
float3 SSSS(float2 uv, float3 albedo, float sssStrength) {
    float3 result = albedo;
    for (int i = 0; i < KERNEL_SIZE; i++) {
        float2 offset = g_SSSKernel[i].xy * sssStrength;
        float weight = g_SSSKernel[i].z;
        result += SampleHDR(uv + offset) * weight;
    }
    return result;
}
```

---

## 4.6 Lens Effects Suite
- [ ] **Status:** Not Started
- **Complexity:** Low | **Impact:** Low-Medium

Complete lens simulation:
- **Chromatic Aberration:** RGB channel offset
- **Vignette:** Radial darkening
- **Film Grain:** Temporal noise overlay
- **Lens Flares:** Light scattering sprites
- **Lens Distortion:** Barrel/pincushion

---

## 4.7 HDR10 / Dolby Vision Output
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** Medium

HDR display support with ST.2084 PQ transfer function.

### Configuration
- `hdrMode` (SDR, HDR10, Dolby)
- `paperWhiteNits` (default: 200)
- `maxOutputNits` (1000-4000)

---

# Phase 5: Editor & Tools
**Goal:** Professional editing capabilities
**Status:** Not Started
**Dependencies:** Phases 1-3 ideally complete

## 5.1 ImGui Dockspace Integration
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** High

Full docking workspace with panels:
- Viewport (3D scene view)
- Hierarchy (entity tree)
- Inspector (component properties)
- Console (log output)
- Content Browser (assets)
- Profiler (GPU/CPU timings)

---

## 5.2 Transform Gizmos (ImGuizmo)
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** High

3D manipulation handles for translate, rotate, scale.

```cpp
ImGuizmo::Manipulate(
    viewMatrix.data(),
    projMatrix.data(),
    m_gizmoOperation,  // TRANSLATE, ROTATE, SCALE
    m_gizmoMode,       // LOCAL, WORLD
    worldMatrix.data()
);
```

---

## 5.3 Command Pattern & Undo/Redo
- [ ] **Status:** Not Started
- **Complexity:** High | **Impact:** High

```cpp
class ICommand {
public:
    virtual void Execute() = 0;
    virtual void Undo() = 0;
    virtual std::string GetDescription() = 0;
};

class TransformCommand : public ICommand {
    entt::entity m_entity;
    Transform m_oldTransform, m_newTransform;
public:
    void Execute() override { SetTransform(m_entity, m_newTransform); }
    void Undo() override { SetTransform(m_entity, m_oldTransform); }
};

class CommandHistory {
    std::vector<std::unique_ptr<ICommand>> m_undoStack;
    std::vector<std::unique_ptr<ICommand>> m_redoStack;
};
```

---

## 5.4 Asset Browser
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** Medium

Visual grid of assets with:
- Thumbnail preview (textures, meshes)
- Drag-drop to scene
- Import dialogs
- Search/filter

---

## 5.5 Material Editor
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** Medium

Live material property editing:
- PBR parameter sliders
- Texture slot assignment
- Preset library
- Real-time preview sphere

---

## 5.6 GPU Profiler
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** Medium

Timeline visualization with:
- Per-pass GPU timing
- Memory usage graph
- Draw call statistics
- PIX marker integration

---

## 5.7 Scene Serialization
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** High

Save/load complete scenes:
- EnTT snapshot serialization
- JSON or binary format
- Asset reference resolution
- Nested prefabs

---

# Phase 6: Gameplay Systems
**Goal:** Complete game engine capabilities
**Status:** Not Started
**Dependencies:** Phase 5 (Editor basics)

## 6.1 C# Scripting (Mono/CoreCLR)
- [ ] **Status:** Not Started
- **Complexity:** Very High | **Impact:** Very High

Managed scripting for gameplay logic.

```csharp
public class PlayerController : Component
{
    public float speed = 5.0f;

    void Update()
    {
        Vector3 input = new Vector3(Input.GetAxis("Horizontal"), 0, Input.GetAxis("Vertical"));
        Transform.position += input * speed * Time.deltaTime;
    }
}
```

### Implementation
- Embed Mono or .NET 8 CoreCLR
- Generate C# bindings via code generation
- Hot-reload support

---

## 6.2 Lua Scripting (Alternative)
- [ ] **Status:** Not Started
- **Complexity:** High | **Impact:** High

Lightweight alternative:
- Embed LuaJIT
- Sol3 binding library
- Console command support

---

## 6.3 Physics Integration (Jolt)
- [ ] **Status:** Not Started
- **Complexity:** High | **Impact:** Very High

**Recommended:** Jolt Physics (MIT, modern C++, excellent perf)

```cpp
struct RigidbodyComponent {
    JPH::BodyID bodyId;
    JPH::EMotionType motionType;  // Static, Kinematic, Dynamic
    float mass;
    float friction;
    float restitution;
};

struct ColliderComponent {
    JPH::ShapeRefC shape;  // Box, Sphere, Capsule, Mesh, Convex
    float3 offset;
};
```

### Files to Modify
- [ ] `CortexEngine/src/Scene/Components.h` - Add physics components
- [ ] `CortexEngine/src/Core/Engine.cpp` - Physics world update

---

## 6.4 Character Controller
- [ ] **Status:** Not Started
- **Complexity:** Medium | **Impact:** High

Kinematic character with:
- Capsule collision
- Ground detection
- Slope handling
- Step climbing
- State machine (idle, walk, run, jump, fall)

---

## 6.5 Animation System
- [ ] **Status:** Not Started
- **Complexity:** Very High | **Impact:** High

Skeletal animation:
- GPU skinning (vertex shader)
- Animation clips & blend trees
- State machine transitions
- IK (Inverse Kinematics)
- Animation events

---

## 6.6 Audio System (FMOD/Wwise)
- [ ] **Status:** Not Started
- **Complexity:** High | **Impact:** High

3D spatial audio:
- `AudioSourceComponent`, `AudioListenerComponent`
- Occlusion/obstruction
- Reverb zones
- Music system

---

## 6.7 Navmesh & AI Pathfinding
- [ ] **Status:** Not Started
- **Complexity:** High | **Impact:** Medium

Using Recast/Detour:
- Navmesh generation from geometry
- Agent steering
- Crowd simulation
- Dynamic obstacles

---

## 6.8 Networking Foundation
- [ ] **Status:** Not Started
- **Complexity:** Very High | **Impact:** Medium

Client-server multiplayer:
- Entity replication
- State synchronization
- Client prediction
- Lag compensation

---

# Implementation Priority Matrix

| Feature | Phase | Complexity | Impact | Priority | Status |
|---------|-------|------------|--------|----------|--------|
| **Bindless Resources** | 1 | High | Critical | **P0** | ⬜ |
| **Render Graph** | 1 | Very High | High | **P0** | ⬜ |
| **GPU Culling** | 1 | High | High | **P0** | ⬜ |
| **Visibility Buffer** | 2 | Very High | Critical | **P1** | ⬜ |
| **Meshlet Pipeline** | 2 | Very High | High | **P1** | ⬜ |
| **DDGI** | 3 | Very High | Very High | **P1** | ⬜ |
| **FSR/DLSS** | 4 | High | Very High | **P1** | ⬜ |
| **Virtual Shadow Maps** | 3 | Very High | High | **P2** | ⬜ |
| **Mesh Shaders** | 2 | High | High | **P2** | ⬜ |
| **ReSTIR** | 3 | Very High | High | **P2** | ⬜ |
| **Editor Dockspace** | 5 | Medium | High | **P2** | ⬜ |
| **Physics (Jolt)** | 6 | High | Very High | **P2** | ⬜ |
| **C# Scripting** | 6 | Very High | Very High | **P3** | ⬜ |
| **VRS** | 2 | Medium | Medium | **P3** | ⬜ |
| **GTAO** | 4 | Medium | Medium | **P3** | ⬜ |
| **Volumetric Fog** | 4 | High | High | **P3** | ⬜ |

**Legend:** ⬜ Not Started | 🔄 In Progress | ✅ Complete | ⏸️ Blocked

---

# Critical Files Reference

| Component | Path |
|-----------|------|
| Main Renderer | `CortexEngine/src/Graphics/Renderer.h` (975 lines) |
| Renderer Impl | `CortexEngine/src/Graphics/Renderer.cpp` (8700+ lines) |
| Descriptor Heap | `CortexEngine/src/Graphics/RHI/DescriptorHeap.h` |
| DX12 Raytracing | `CortexEngine/src/Graphics/RHI/DX12Raytracing.h` |
| PBR Shader | `CortexEngine/assets/shaders/Basic.hlsl` (1620 lines) |
| Post-Process | `CortexEngine/assets/shaders/PostProcess.hlsl` (1439 lines) |
| RT GI Shader | `CortexEngine/assets/shaders/RaytracedGI.hlsl` |
| ECS Components | `CortexEngine/src/Scene/Components.h` |
| Engine Core | `CortexEngine/src/Core/Engine.cpp` |
| Mesh Buffers | `CortexEngine/src/Graphics/MeshBuffers.h` |

---

# Quality Presets Configuration

## Ultra Quality (4K @ 30fps target)
```ini
renderScale=1.0
raytracing=true
ddgiEnabled=true
ddgiRaysPerProbe=512
rtReflections=true
rtShadows=true
shadowQuality=ultra (VSM 16K)
ssaoQuality=gtao_full
bloomQuality=spectral
taa=enabled
upscaler=native
vrs=off
```

## High Quality (1440p @ 60fps target)
```ini
renderScale=0.85
raytracing=true
ddgiEnabled=true
ddgiRaysPerProbe=256
rtReflections=true
rtShadows=true
shadowQuality=high (VSM 8K)
ssaoQuality=gtao_half
bloomQuality=standard
taa=enabled
upscaler=FSR_Quality
vrs=image_based
```

## Medium Quality (1080p @ 60fps target)
```ini
renderScale=0.67
raytracing=partial (shadows only)
ddgiEnabled=true
ddgiRaysPerProbe=128
rtReflections=false
rtShadows=true
shadowQuality=medium (CSM 4K)
ssaoQuality=ssao_standard
bloomQuality=standard
taa=enabled
upscaler=FSR_Balanced
vrs=image_based
```

## Low Quality (720p @ 60fps target)
```ini
renderScale=0.5
raytracing=false
ddgiEnabled=false
rtReflections=false
rtShadows=false
shadowQuality=low (CSM 2K)
ssaoQuality=ssao_half
bloomQuality=simple
taa=enabled
upscaler=FSR_Performance
vrs=aggressive
```

---

# Shader Model Requirements

| Feature | Minimum SM | Notes |
|---------|-----------|-------|
| Bindless Resources | SM 6.6 | ResourceDescriptorHeap |
| Mesh Shaders | SM 6.5 | D3D12_OPTIONS7 |
| VRS | SM 6.4 | D3D12_OPTIONS6 |
| Ray Tracing | SM 6.3 | DXR 1.0 |
| Wave Intrinsics | SM 6.0 | Already using |

---

# Changelog

## 2024-12-05
- Initial roadmap created
- Analyzed current engine state
- Defined 6-phase development plan
- Established priority matrix

---

*This roadmap represents a multi-year development effort. Prioritize Phase 1 foundation work before attempting advanced features. Each phase builds upon previous work.*

---

> **Next Steps:**
> 1. Begin with Phase 1.1 (Bindless Resources) as the critical foundation
> 2. Set up feature branches for each major component
> 3. Create unit tests for new systems
> 4. Track progress in this document
