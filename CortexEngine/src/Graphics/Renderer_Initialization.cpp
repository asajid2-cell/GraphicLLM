#include "Renderer.h"
#include "RenderGraph.h"
#include "Debug/GPUProfiler.h"
#include "Core/Window.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/MaterialState.h"
#include "Graphics/MeshBuffers.h"
#include "Graphics/SurfaceClassification.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/FrameContractValidation.h"
#include "Graphics/FrameContractResources.h"
#include "Graphics/RendererGeometryUtils.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <unordered_set>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {
Result<void> Renderer::Initialize(DX12Device* device, Window* window) {
    if (!device || !window) {
        return Result<void>::Err("Invalid device or window pointer");
    }

    m_frameLifecycle.deviceRemoved = false;
    m_frameLifecycle.deviceRemovedLogged = false;
    m_frameLifecycle.missingBufferWarningLogged = false;
    m_frameLifecycle.zeroDrawWarningLogged = false;
    m_services.device = device;
    m_services.window = window;

    m_framePlanning.budgetPlan = BudgetPlanner::BuildPlan(
        m_services.device->GetDedicatedVideoMemoryBytes(),
        std::max(1u, m_services.window->GetWidth()),
        std::max(1u, m_services.window->GetHeight()));
    m_shadowResources.controls.mapSize = static_cast<float>(m_framePlanning.budgetPlan.shadowMapSize);
    m_bloomResources.resources.activeLevels = std::clamp<uint32_t>(m_framePlanning.budgetPlan.bloomLevels, 1u, kBloomLevels);
    m_assetRuntime.registry.SetBudgets(m_framePlanning.budgetPlan.textureBudgetBytes,
                               m_framePlanning.budgetPlan.environmentBudgetBytes,
                               m_framePlanning.budgetPlan.geometryBudgetBytes,
                               m_framePlanning.budgetPlan.rtStructureBudgetBytes);
    SetRenderScale(m_qualityRuntimeState.renderScale);

    spdlog::info("Initializing Renderer...");

    // Detect basic DXR ray tracing support (optional path).
    m_rtRuntimeState.supported = false;
    m_rtRuntimeState.enabled = false;
    {
        const bool forceNoDxr = [] {
            const char* value = std::getenv("CORTEX_FORCE_DXR_UNSUPPORTED");
            return value && value[0] != '\0' && value[0] != '0';
        }();
        if (forceNoDxr) {
            spdlog::warn("DXR ray tracing forced unsupported via CORTEX_FORCE_DXR_UNSUPPORTED");
        } else {
            Microsoft::WRL::ComPtr<ID3D12Device5> dxrDevice;
            HRESULT dxrHr = m_services.device->GetDevice()->QueryInterface(IID_PPV_ARGS(&dxrDevice));
            if (SUCCEEDED(dxrHr)) {
                D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
                HRESULT featHr = dxrDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                                                &options5,
                                                                sizeof(options5));
                if (SUCCEEDED(featHr) && options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
                    m_rtRuntimeState.supported = true;
                    spdlog::info("DXR ray tracing supported (tier {}).",
                                 static_cast<int>(options5.RaytracingTier));
                } else {
                    spdlog::info("DXR ray tracing not supported (feature tier not available).");
                }
            } else {
                spdlog::info("DXR ray tracing not supported (ID3D12Device5 not available).");
            }
        }
    }

    // Create command queue
    m_services.commandQueue = std::make_unique<DX12CommandQueue>();
    auto queueResult = m_services.commandQueue->Initialize(device->GetDevice());
    if (queueResult.IsErr()) {
        return Result<void>::Err("Failed to create command queue: " + queueResult.Error());
    }
    Debug::GPUProfiler::Get().SetBufferCount(kFrameCount);
    if (!Debug::GPUProfiler::Get().Initialize(device->GetDevice(), m_services.commandQueue->GetCommandQueue())) {
        spdlog::warn("GPU timestamp profiler unavailable; frame reports will use CPU timings only");
    }

    m_services.uploadQueue = std::make_unique<DX12CommandQueue>();
    auto uploadQueueResult = m_services.uploadQueue->Initialize(device->GetDevice(), D3D12_COMMAND_LIST_TYPE_COPY);
    if (uploadQueueResult.IsErr()) {
        return Result<void>::Err("Failed to create upload command queue: " + uploadQueueResult.Error());
    }

    // Create async compute queue for parallel workloads (SSAO, Bloom, GPU culling)
    m_services.computeQueue = std::make_unique<DX12CommandQueue>();
    auto computeQueueResult = m_services.computeQueue->Initialize(device->GetDevice(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
    if (computeQueueResult.IsErr()) {
        spdlog::warn("Failed to create async compute queue: {} (compute work will run on graphics queue)",
                     computeQueueResult.Error());
        m_services.computeQueue.reset();
        m_frameRuntime.asyncComputeSupported = false;
    } else {
        m_frameRuntime.asyncComputeSupported = true;
        spdlog::info("Async compute queue created for parallel workloads");
    }

    // Initialize swap chain (now that we have a command queue)
    auto swapChainResult = window->InitializeSwapChain(device, m_services.commandQueue.get());
    if (swapChainResult.IsErr()) {
        return Result<void>::Err("Failed to initialize swap chain: " + swapChainResult.Error());
    }

    // Create descriptor heaps
    m_services.descriptorManager = std::make_unique<DescriptorHeapManager>();
    auto heapResult = m_services.descriptorManager->Initialize(device->GetDevice(), kFrameCount);
    if (heapResult.IsErr()) {
        return Result<void>::Err("Failed to create descriptor heaps: " + heapResult.Error());
    }
    m_services.descriptorManager->SetFlushCallback([this]() {
        WaitForGPU();
    });

    // Create bindless resource manager for SM6.6 bindless access
    m_services.bindlessManager = std::make_unique<BindlessResourceManager>();
    auto bindlessResult = m_services.bindlessManager->Initialize(device->GetDevice(), 16384, 8192);
    if (bindlessResult.IsErr()) {
        spdlog::warn("Bindless resource manager initialization failed: {} (falling back to legacy descriptor tables)",
                     bindlessResult.Error());
        m_services.bindlessManager.reset();
    } else {
        // Set flush callback for safe deferred releases
        m_services.bindlessManager->SetFlushCallback([this]() {
            WaitForGPU();
        });
        spdlog::info("Bindless resource manager initialized (16384 textures, 8192 buffers)");

        // Diagnostic: Show current rendering mode based on compile-time flag
#ifdef ENABLE_BINDLESS
        spdlog::info("Shader mode: SM6.6 bindless resources (DXC compiler, ResourceDescriptorHeap[])");
#else
        spdlog::info("Shader mode: SM5.1 descriptor tables (FXC fallback, traditional binding)");
#endif
    }

    // Initialize GPU Culling pipeline for GPU-driven rendering
    m_services.gpuCulling = std::make_unique<GPUCullingPipeline>();
    auto cullingResult = m_services.gpuCulling->Initialize(device, m_services.descriptorManager.get(), m_services.commandQueue.get(), 65536);
    if (cullingResult.IsErr()) {
        spdlog::warn("GPU Culling initialization failed: {} (falling back to CPU culling)",
                     cullingResult.Error());
        m_services.gpuCulling.reset();
        m_gpuCullingState.enabled = false;
        m_gpuCullingState.indirectDrawEnabled = false;
    } else {
        m_services.gpuCulling->SetFlushCallback([this]() {
            WaitForGPU();
        });
        // GPU culling is ready but disabled by default - can be enabled via config
        m_gpuCullingState.enabled = false;
        m_gpuCullingState.indirectDrawEnabled = false;
        spdlog::info("GPU Culling Pipeline initialized (max 65536 instances)");
    }
#ifndef ENABLE_BINDLESS
    m_gpuCullingState.enabled = false;
    m_gpuCullingState.indirectDrawEnabled = false;
    spdlog::info("GPU culling disabled: bindless resources not enabled");
#endif

    // Initialize Render Graph for declarative pass management
    m_services.renderGraph = std::make_unique<RenderGraph>();
    auto rgResult = m_services.renderGraph->Initialize(
        device,
        m_services.commandQueue.get(),
        m_frameRuntime.asyncComputeSupported ? m_services.computeQueue.get() : nullptr,
        m_services.uploadQueue.get()
    );
    if (rgResult.IsErr()) {
        spdlog::warn("RenderGraph initialization failed: {} (using legacy manual barriers)",
                     rgResult.Error());
        m_services.renderGraph.reset();
    } else {
        spdlog::info("RenderGraph initialized for declarative pass management");
    }

    // Initialize Visibility Buffer Renderer
    m_services.visibilityBuffer = std::make_unique<VisibilityBufferRenderer>();
    auto vbResult = m_services.visibilityBuffer->Initialize(
        device,
        m_services.descriptorManager.get(),
        m_services.bindlessManager.get(),
        GetInternalRenderWidth(),
        GetInternalRenderHeight()
    );
    if (vbResult.IsErr()) {
        spdlog::warn("VisibilityBuffer initialization failed: {} (using forward rendering)",
                     vbResult.Error());
        m_services.visibilityBuffer.reset();
    } else {
        spdlog::info("VisibilityBuffer initialized for two-phase deferred rendering");
        // Set flush callback so VB resize waits for GPU before destroying resources
        m_services.visibilityBuffer->SetFlushCallback([this]() {
            WaitForGPU();
        });
        const bool vbDisabled = (std::getenv("CORTEX_DISABLE_VISIBILITY_BUFFER") != nullptr);
        const bool vbEnabledLegacy = (std::getenv("CORTEX_ENABLE_VISIBILITY_BUFFER") != nullptr);
        // VB is enabled by default; opt out via env var.
        m_visibilityBufferState.enabled = !vbDisabled;
        if (vbDisabled) {
            spdlog::info("VisibilityBuffer disabled via CORTEX_DISABLE_VISIBILITY_BUFFER=1 (using forward rendering).");
        } else if (vbEnabledLegacy) {
            spdlog::info("VisibilityBuffer explicitly enabled via CORTEX_ENABLE_VISIBILITY_BUFFER=1.");
        } else {
            spdlog::info("VisibilityBuffer enabled by default (set CORTEX_DISABLE_VISIBILITY_BUFFER=1 to disable).");
        }
    }

#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    // Initialize Hyper-Geometry (GPU-driven) path
    m_services.hyperGeometry = std::make_unique<HyperGeometry::HyperGeometryEngine>();
    HyperGeometry::HyperGeometryConfig hyperConfig{};
    hyperConfig.maxMeshlets = 256 * 1024;
    hyperConfig.meshletTargetSize = 64;
    hyperConfig.meshletTargetVerts = 96;
    hyperConfig.debugDirectDraw = false; // avoid double-draw plane; rely on indirect/ classic fallback

    auto hyperResult = m_services.hyperGeometry->Initialize(device, m_services.descriptorManager.get(), m_services.commandQueue.get(), hyperConfig);
    if (hyperResult.IsErr()) {
        spdlog::warn("Hyper-Geometry initialization failed: {}", hyperResult.Error());
        m_services.hyperGeometry.reset();
    }
#endif

    // Initialize ray tracing context if DXR is supported. If this fails for any
    // reason, hard-disable ray tracing so the toggle becomes inert.
    if (m_rtRuntimeState.supported) {
        m_services.rayTracingContext = std::make_unique<DX12RaytracingContext>();
        auto rtResult = m_services.rayTracingContext->Initialize(device, m_services.descriptorManager.get());
        if (rtResult.IsErr()) {
            spdlog::warn("DXR context initialization failed: {}", rtResult.Error());
            m_services.rayTracingContext.reset();
            m_rtRuntimeState.supported = false;
            m_rtRuntimeState.enabled = false;
        } else {
            // Set flush callback so RT context can force GPU sync when resizing buffers
            m_services.rayTracingContext->SetFlushCallback([this]() {
                WaitForGPU();
            });
        }
    }

    // Create command allocators (one per frame)
    for (uint32_t i = 0; i < 3; ++i) {
        HRESULT hr = device->GetDevice()->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_commandResources.graphicsAllocators[i])
        );

        if (FAILED(hr)) {
            char buf[64];
            sprintf_s(buf, "0x%08X", static_cast<unsigned int>(hr));
            // If device was removed earlier (e.g., HyperGeometry init), surface reason
            HRESULT removed = device->GetDevice()->GetDeviceRemovedReason();
            char remBuf[64];
            sprintf_s(remBuf, "0x%08X", static_cast<unsigned int>(removed));
            return Result<void>::Err("Failed to create command allocator " + std::to_string(i) +
                                     " (hr=" + buf + ", removed=" + remBuf + ")");
        }
    }

    // Create compute command allocators if async compute is supported
    if (m_frameRuntime.asyncComputeSupported) {
        for (uint32_t i = 0; i < 3; ++i) {
            HRESULT hr = device->GetDevice()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COMPUTE,
                IID_PPV_ARGS(&m_commandResources.computeAllocators[i])
            );
            if (FAILED(hr)) {
                spdlog::warn("Failed to create compute allocator {}, disabling async compute", i);
                m_frameRuntime.asyncComputeSupported = false;
                m_services.computeQueue.reset();
                break;
            }
        }

        // Create compute command list
        if (m_frameRuntime.asyncComputeSupported) {
            HRESULT hr = device->GetDevice()->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_COMPUTE,
                m_commandResources.computeAllocators[0].Get(),
                nullptr,
                IID_PPV_ARGS(&m_commandResources.computeList)
            );
            if (FAILED(hr)) {
                spdlog::warn("Failed to create compute command list, disabling async compute");
                m_frameRuntime.asyncComputeSupported = false;
                m_services.computeQueue.reset();
            } else {
                m_commandResources.computeList->Close();
                m_frameRuntime.computeListOpen = false;
            }
        }
    }

    // Create command list
    auto cmdListResult = CreateCommandList();
    if (cmdListResult.IsErr()) {
        return cmdListResult;
    }

    // Create upload command list/allocator pool
    for (uint32_t i = 0; i < UploadCommandPoolState::kPoolSize; ++i) {
        HRESULT uploadHr = m_services.device->GetDevice()->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_COPY,
            IID_PPV_ARGS(&m_uploadCommands.commandAllocators[i])
        );
        if (FAILED(uploadHr)) {
            return Result<void>::Err("Failed to create upload command allocator");
        }

        uploadHr = m_services.device->GetDevice()->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_COPY,
            m_uploadCommands.commandAllocators[i].Get(),
            nullptr,
            IID_PPV_ARGS(&m_uploadCommands.commandLists[i])
        );
        if (FAILED(uploadHr)) {
            return Result<void>::Err("Failed to create upload command list");
        }
        m_uploadCommands.commandLists[i]->Close();
    }

    // Create depth buffer
    auto depthResult = CreateDepthBuffer();
    if (depthResult.IsErr()) {
        return depthResult;
    }

    // Create directional light shadow map
    auto shadowResult = CreateShadowMapResources();
    if (shadowResult.IsErr()) {
        spdlog::warn("Failed to create shadow map resources: {}", shadowResult.Error());
        m_shadowResources.controls.enabled = false;
    }

    // Create HDR render target for main pass
    auto hdrResult = CreateHDRTarget();
    if (hdrResult.IsErr()) {
        spdlog::warn("Failed to create HDR target: {}", hdrResult.Error());
        m_mainTargets.hdrColor.Reset();
    }

    // RT sun shadow mask is optional; if creation fails we simply keep using
    // cascaded shadows even when RT is enabled.
    auto rtMaskResult = CreateRTShadowMask();
    if (rtMaskResult.IsErr()) {
        spdlog::warn("Failed to create RT shadow mask: {}", rtMaskResult.Error());
    }

    // RT reflections buffer is also optional and only meaningful when the
    // DXR path is active. For now we allocate it eagerly when ray tracing is
    // supported so the post-process path can consume it in a future pass.
    if (m_rtRuntimeState.supported && m_services.rayTracingContext) {
        auto rtReflResult = CreateRTReflectionResources();
        if (rtReflResult.IsErr()) {
            spdlog::warn("Failed to create RT reflection buffer: {}", rtReflResult.Error());
        }

        // RT diffuse GI buffer is likewise optional; if creation fails we
        // simply fall back to SSAO + ambient only.
        auto rtGiResult = CreateRTGIResources();
        if (rtGiResult.IsErr()) {
            spdlog::warn("Failed to create RT GI buffer: {}", rtGiResult.Error());
        }
    }

    // Create constant buffers
    // Use kFrameCount slots to avoid race conditions with triple buffering
    auto cbResult = m_constantBuffers.frame.Initialize(device->GetDevice(), kFrameCount);
    if (cbResult.IsErr()) {
        return Result<void>::Err("Failed to create frame constant buffer: " + cbResult.Error());
    }
    // Initialize all frame constant slots with default data to prevent reading garbage on first frames
    // This ensures m_constantBuffers.currentFrameGPU is valid before any render pass tries to use it
    FrameConstants defaultFrameData = {};
    defaultFrameData.viewMatrix = glm::mat4(1.0f);
    defaultFrameData.projectionMatrix = glm::mat4(1.0f);
    defaultFrameData.viewProjectionMatrix = glm::mat4(1.0f);
    defaultFrameData.invProjectionMatrix = glm::mat4(1.0f);
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        m_constantBuffers.frame.WriteToSlot(defaultFrameData, i);
    }
    m_constantBuffers.currentFrameGPU = m_constantBuffers.frame.WriteToSlot(defaultFrameData, 0);

    cbResult = m_constantBuffers.object.Initialize(device->GetDevice(), 1024); // enough for typical scenes per frame
    if (cbResult.IsErr()) {
        return Result<void>::Err("Failed to create object constant buffer: " + cbResult.Error());
    }

    cbResult = m_constantBuffers.material.Initialize(device->GetDevice(), 1024);
    if (cbResult.IsErr()) {
        return Result<void>::Err("Failed to create material constant buffer: " + cbResult.Error());
    }

    // Shadow constants: need slots for all cascades + local lights per frame.
    // Multiply by kFrameCount to avoid race conditions with triple buffering.
    cbResult = m_constantBuffers.shadow.Initialize(device->GetDevice(), kShadowArraySize * kFrameCount);
    if (cbResult.IsErr()) {
        return Result<void>::Err("Failed to create shadow constant buffer: " + cbResult.Error());
    }

    // Biome materials constant buffer for GPU-side material lookups.
    // Single slot since biome configs are static per-frame.
    cbResult = m_constantBuffers.biomeMaterials.Initialize(device->GetDevice(), 1);
    if (cbResult.IsErr()) {
        return Result<void>::Err("Failed to create biome materials constant buffer: " + cbResult.Error());
    }

    // Initialize GPU breadcrumb buffer for device-removed diagnostics.
    auto breadcrumbResult = CreateBreadcrumbBuffer();
    if (breadcrumbResult.IsErr()) {
        spdlog::warn("Renderer: failed to create GPU breadcrumb buffer: {}", breadcrumbResult.Error());
    }

    // Optional "no HDR" debug path. When CORTEX_DISABLE_HDR is set, skip the
    // intermediate HDR/post-process pipeline and render the main pass
    // directly into the swap-chain back buffer. This also disables effects
    // that depend on the HDR target (TAA/SSR/SSAO/Bloom) to maximize
    // stability when diagnosing device-removed issues.
    if (std::getenv("CORTEX_DISABLE_HDR")) {
        spdlog::warn("Renderer: CORTEX_DISABLE_HDR set; main pass will render directly to back buffer (HDR/TAA/SSR/SSAO/Bloom disabled)");
        m_mainTargets.hdrColor.Reset();
        m_mainTargets.hdrRTV = {};
        m_mainTargets.hdrSRV = {};
        SetTAAEnabled(false);
        SetSSREnabled(false);
        SetSSAOEnabled(false);
        m_bloomResources.controls.intensity = 0.0f;
    }

    // Compile shaders and create pipeline
    auto shaderResult = CompileShaders();
    if (shaderResult.IsErr()) {
        return shaderResult;
    }

    auto pipelineResult = CreatePipeline();
    if (pipelineResult.IsErr()) {
        return pipelineResult;
    }

    // Create placeholder texture
    auto texResult = CreatePlaceholderTexture();
    if (texResult.IsErr()) {
        return texResult;
    }

    // Environment maps and IBL setup (optional; falls back to flat ambient if assets missing).
    auto envResult = InitializeEnvironmentMaps();
    if (envResult.IsErr()) {
        spdlog::warn("Environment maps not fully initialized: {}", envResult.Error());
    }

    auto taaTableResult = InitializeTAAResolveDescriptorTable();
    if (taaTableResult.IsErr()) {
        spdlog::warn("TAA resolve descriptor table init failed; TAA will skip persistent descriptor binding: {}",
                     taaTableResult.Error());
    }

    auto postTableResult = InitializePostProcessDescriptorTable();
    if (postTableResult.IsErr()) {
        spdlog::warn("Post-process descriptor table init failed; post-process will skip persistent descriptor binding: {}",
                     postTableResult.Error());
    }

    spdlog::info("Renderer initialized successfully");
    return Result<void>::Ok();
}

UINT Renderer::GetInternalRenderWidth() const {
    if (!m_services.window) {
        return 1u;
    }

    const float scale = std::clamp(m_qualityRuntimeState.renderScale, 0.5f, 1.5f);
    const float scaled = static_cast<float>(std::max(1u, m_services.window->GetWidth())) * scale;
    return std::max(1u, static_cast<UINT>(std::lround(scaled)));
}

UINT Renderer::GetInternalRenderHeight() const {
    if (!m_services.window) {
        return 1u;
    }

    const float scale = std::clamp(m_qualityRuntimeState.renderScale, 0.5f, 1.5f);
    const float scaled = static_cast<float>(std::max(1u, m_services.window->GetHeight())) * scale;
    return std::max(1u, static_cast<UINT>(std::lround(scaled)));
}
} // namespace Cortex::Graphics
