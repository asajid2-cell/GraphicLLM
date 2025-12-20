#include "Renderer.h"
#include "Core/Window.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/MaterialState.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <chrono>
#include <array>
#include <limits>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>
#include <stdio.h>

namespace Cortex::Graphics {

Renderer::~Renderer() {
    // Ensure GPU is completely idle before any member destructors run
    WaitForGPU();
    Shutdown();
}

// Helper to tag the last successfully completed high-level render pass.
// This is used purely for diagnostics when the DX12 device reports a
// removed/hung state so logs can point at the most recent pass that ran.
void Renderer::MarkPassComplete(const char* passName) {
    m_lastCompletedPass = passName ? passName : "Unknown";
}

// Centralized device-removed reporting. Any code path that encounters a
// failure HRESULT and suspects device loss should call this helper so we
// emit a consistent, information-rich log entry (context, hr, device
// removed reason, frame index, last completed pass, file/line).
void Renderer::ReportDeviceRemoved(const char* context,
                                   HRESULT hr,
                                   const char* file,
                                   int line) {
    const char* ctx = context ? context : "Unknown";

    HRESULT reason = S_OK;
    if (m_device && m_device->GetDevice()) {
        reason = m_device->GetDevice()->GetDeviceRemovedReason();
    }

    // Snapshot the last GPU breadcrumb value (if available) so logs can
    // distinguish between CPU-side pass tags and the last marker the GPU
    // actually reached before the fault.
    uint32_t markerVal = (m_breadcrumbMap != nullptr) ? *m_breadcrumbMap : 0u;
    const char* markerName = "None";
    switch (static_cast<GpuMarker>(markerVal)) {
        case GpuMarker::BeginFrame:      markerName = "BeginFrame"; break;
        case GpuMarker::ShadowPass:      markerName = "ShadowPass"; break;
        case GpuMarker::Skybox:          markerName = "Skybox"; break;
        case GpuMarker::OpaqueGeometry:  markerName = "OpaqueGeometry"; break;
        case GpuMarker::TransparentGeom: markerName = "TransparentGeom"; break;
        case GpuMarker::MotionVectors:   markerName = "MotionVectors"; break;
        case GpuMarker::TAAResolve:      markerName = "TAAResolve"; break;
        case GpuMarker::SSR:             markerName = "SSR"; break;
        case GpuMarker::Particles:       markerName = "Particles"; break;
        case GpuMarker::SSAO:            markerName = "SSAO"; break;
        case GpuMarker::Bloom:           markerName = "Bloom"; break;
        case GpuMarker::PostProcess:     markerName = "PostProcess"; break;
        case GpuMarker::DebugLines:      markerName = "DebugLines"; break;
        default:                         markerName = "None"; break;
    }

    auto rs = [](D3D12_RESOURCE_STATES s) {
        return static_cast<unsigned int>(s);
    };

    spdlog::error(
        "DX12 device removed or GPU fault in '{}' (hr=0x{:08X}, reason=0x{:08X}, frameCounter={}, "
        "swapIndex={}, lastPass='{}', lastGpuMarker='{}', at {}:{}). "
        "ResourceStates: depth=0x{:X}, shadowMap=0x{:X}, hdr=0x{:X}, "
        "rtShadowMask=0x{:X}, rtShadowMaskHistory=0x{:X}, gbufferNR=0x{:X}, "
        "ssao=0x{:X}, ssr=0x{:X}, velocity=0x{:X}, history=0x{:X}, "
        "taaIntermediate=0x{:X}, rtRefl=0x{:X}, rtReflHist=0x{:X}, "
        "rtGI=0x{:X}, rtGIHist=0x{:X}",
        ctx,
        static_cast<unsigned int>(hr),
        static_cast<unsigned int>(reason),
        static_cast<unsigned long long>(m_renderFrameCounter),
        static_cast<unsigned int>(m_frameIndex),
        m_lastCompletedPass ? m_lastCompletedPass : "None",
        markerName,
        file ? file : "unknown",
        line,
        rs(m_depthState),
        rs(m_shadowMapState),
        rs(m_hdrState),
        rs(m_rtShadowMaskState),
        rs(m_rtShadowMaskHistoryState),
        rs(m_gbufferNormalRoughnessState),
        rs(m_ssaoState),
        rs(m_ssrState),
        rs(m_velocityState),
        rs(m_historyState),
        rs(m_taaIntermediateState),
        rs(m_rtReflectionState),
        rs(m_rtReflectionHistoryState),
        rs(m_rtGIState),
        rs(m_rtGIHistoryState));

    // Attempt to query DRED (Device Removed Extended Data) so we can log the
    // last command list / breadcrumb and any page-fault information the GPU
    // driver surfaced. This is best-effort and will silently skip if DRED is
    // not available on the current platform.
    if (m_device && m_device->GetDevice()) {
        Microsoft::WRL::ComPtr<ID3D12DeviceRemovedExtendedData1> dred1;
        if (SUCCEEDED(m_device->GetDevice()->QueryInterface(IID_PPV_ARGS(&dred1))) && dred1) {
            D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 autoOut{};
            if (SUCCEEDED(dred1->GetAutoBreadcrumbsOutput1(&autoOut)) && autoOut.pHeadAutoBreadcrumbNode) {
                // Walk to the last node in the chain; this corresponds to the
                // most recent command list that executed before the fault.
                const D3D12_AUTO_BREADCRUMB_NODE1* node = autoOut.pHeadAutoBreadcrumbNode;
                std::array<const D3D12_AUTO_BREADCRUMB_NODE1*, 3> lastNodes{};
                while (node->pNext) {
                    lastNodes[0] = lastNodes[1];
                    lastNodes[1] = lastNodes[2];
                    lastNodes[2] = node;
                    node = node->pNext;
                }
                lastNodes[0] = lastNodes[1];
                lastNodes[1] = lastNodes[2];
                lastNodes[2] = node;

                const char* listName = "Unknown";
                if (node->pCommandListDebugNameA) {
                    listName = node->pCommandListDebugNameA;
                }

                UINT lastValue = UINT_MAX;
                if (node->pLastBreadcrumbValue) {
                    lastValue = *node->pLastBreadcrumbValue;
                }

                spdlog::error(
                    "DRED: last command list='{}', breadcrumbCount={}, lastCompletedBreadcrumbValue={}",
                    listName,
                    node->BreadcrumbCount,
                    lastValue);

                // Log the tail of the breadcrumb chain (up to last 3 nodes) to
                // show which command queues/lists were executing prior to the fault.
                for (int i = 2; i >= 0; --i) {
                    const auto* n = lastNodes[i];
                    if (!n) continue;
                    const char* clName = n->pCommandListDebugNameA ? n->pCommandListDebugNameA : "UnknownCL";
                    const char* cqName = n->pCommandQueueDebugNameA ? n->pCommandQueueDebugNameA : "UnknownCQ";
                    UINT completed = (n->pLastBreadcrumbValue) ? *n->pLastBreadcrumbValue : UINT_MAX;
                    spdlog::error(
                        "DRED: chain[-{}] queue='{}' list='{}' breadcrumbs={} lastCompleted={}",
                        (2 - i),
                        cqName,
                        clName,
                        n->BreadcrumbCount,
                        completed);
                }
            }

            D3D12_DRED_PAGE_FAULT_OUTPUT1 pageOut{};
            if (SUCCEEDED(dred1->GetPageFaultAllocationOutput1(&pageOut))) {
                // Log the GPU virtual address that faulted and whether DRED
                // associated it with an existing or recently-freed allocation.
                unsigned long long faultVA =
                    static_cast<unsigned long long>(pageOut.PageFaultVA);
                const char* allocType = "Unknown";
                if (pageOut.pHeadExistingAllocationNode) {
                    allocType = "ExistingAllocation";
                } else if (pageOut.pHeadRecentFreedAllocationNode) {
                    allocType = "RecentFreedAllocation";
                }

                spdlog::error(
                    "DRED: page fault at GPU VA=0x{:016X}, allocationType={}",
                    faultVA,
                    allocType);

                auto logAllocNode = [](const D3D12_DRED_ALLOCATION_NODE1* n, const char* label) {
                    if (!n) return;
                    const char* name = n->ObjectNameA ? n->ObjectNameA : "Unnamed";
                    spdlog::error("DRED: {} allocationType={} name='{}' object={}", label,
                                  static_cast<int>(n->AllocationType),
                                  name,
                                  static_cast<const void*>(n->pObject));
                };
                logAllocNode(pageOut.pHeadExistingAllocationNode, "ExistingAlloc");
                logAllocNode(pageOut.pHeadRecentFreedAllocationNode, "RecentFreedAlloc");
            }
        }
    }

    m_deviceRemoved = true;
}

// Convenience macro so call sites automatically capture file/line.
#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)

Result<void> Renderer::CreateBreadcrumbBuffer() {
    if (!m_device || !m_device->GetDevice()) {
        return Result<void>::Err("Renderer not initialized for breadcrumb buffer creation");
    }
    if (m_breadcrumbBuffer) {
        return Result<void>::Ok();
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeof(uint32_t);
    desc.Height = 1;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_breadcrumbBuffer));

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create GPU breadcrumb buffer");
    }

    hr = m_breadcrumbBuffer->Map(0, nullptr, reinterpret_cast<void**>(&m_breadcrumbMap));
    if (FAILED(hr)) {
        m_breadcrumbBuffer.Reset();
        m_breadcrumbMap = nullptr;
        return Result<void>::Err("Failed to map GPU breadcrumb buffer");
    }

    if (m_breadcrumbMap) {
        *m_breadcrumbMap = static_cast<uint32_t>(GpuMarker::None);
    }

    spdlog::info("GPU breadcrumb buffer initialized for device-removed diagnostics");
    return Result<void>::Ok();
}

void Renderer::WriteBreadcrumb(GpuMarker marker) {
    if (!m_breadcrumbBuffer || !m_commandList) {
        return;
    }

    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> list4;
    if (FAILED(m_commandList.As(&list4)) || !list4) {
        return;
    }

    D3D12_WRITEBUFFERIMMEDIATE_PARAMETER param{};
    param.Dest = m_breadcrumbBuffer->GetGPUVirtualAddress();
    param.Value = static_cast<uint32_t>(marker);

    list4->WriteBufferImmediate(1, &param, nullptr);
}

// Simple Halton sequence helper for TAA jitter
static float Halton(uint32_t index, uint32_t base) {
    float f = 1.0f;
    float result = 0.0f;
    uint32_t i = index;
    while (i > 0) {
        f /= static_cast<float>(base);
        result += f * static_cast<float>(i % base);
        i /= base;
    }
    return result;
}

// Classify a renderable as transparent based on its opacity and preset name.
// Glass presets default to partial alpha and should be rendered in a
// separate blended pass after opaque geometry.
static bool IsTransparentRenderable(const Cortex::Scene::RenderableComponent& renderable) {
    // Primary signal: explicit alpha on the material color.
    const float opacity = renderable.albedoColor.a;
    if (opacity < 0.99f) {
        return true;
    }

    // Secondary signal: logical material preset name (e.g., "glass").
    if (!renderable.presetName.empty()) {
        std::string presetLower = renderable.presetName;
        std::transform(presetLower.begin(),
                       presetLower.end(),
                       presetLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (presetLower.find("glass") != std::string::npos) {
            return true;
        }
    }

    return false;
}

Result<void> Renderer::Initialize(DX12Device* device, Window* window) {
    if (!device || !window) {
        return Result<void>::Err("Invalid device or window pointer");
    }

    m_deviceRemoved = false;
    m_deviceRemovedLogged = false;
    m_missingBufferWarningLogged = false;
    m_zeroDrawWarningLogged = false;
    m_device = device;
    m_window = window;

    spdlog::info("Initializing Renderer...");

    // Detect basic DXR ray tracing support (optional path).
    m_rayTracingSupported = false;
    m_rayTracingEnabled = false;
    {
        Microsoft::WRL::ComPtr<ID3D12Device5> dxrDevice;
        HRESULT dxrHr = m_device->GetDevice()->QueryInterface(IID_PPV_ARGS(&dxrDevice));
        if (SUCCEEDED(dxrHr)) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5{};
            HRESULT featHr = dxrDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                                            &options5,
                                                            sizeof(options5));
            if (SUCCEEDED(featHr) && options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED) {
                m_rayTracingSupported = true;
                spdlog::info("DXR ray tracing supported (tier {}).",
                             static_cast<int>(options5.RaytracingTier));
            } else {
                spdlog::info("DXR ray tracing not supported (feature tier not available).");
            }
        } else {
            spdlog::info("DXR ray tracing not supported (ID3D12Device5 not available).");
        }
    }

    // Create command queue
    m_commandQueue = std::make_unique<DX12CommandQueue>();
    auto queueResult = m_commandQueue->Initialize(device->GetDevice());
    if (queueResult.IsErr()) {
        return Result<void>::Err("Failed to create command queue: " + queueResult.Error());
    }
    m_uploadQueue = std::make_unique<DX12CommandQueue>();
    auto uploadQueueResult = m_uploadQueue->Initialize(device->GetDevice(), D3D12_COMMAND_LIST_TYPE_COPY);
    if (uploadQueueResult.IsErr()) {
        return Result<void>::Err("Failed to create upload command queue: " + uploadQueueResult.Error());
    }

    // Create async compute queue for parallel workloads (SSAO, Bloom, GPU culling)
    m_computeQueue = std::make_unique<DX12CommandQueue>();
    auto computeQueueResult = m_computeQueue->Initialize(device->GetDevice(), D3D12_COMMAND_LIST_TYPE_COMPUTE);
    if (computeQueueResult.IsErr()) {
        spdlog::warn("Failed to create async compute queue: {} (compute work will run on graphics queue)",
                     computeQueueResult.Error());
        m_computeQueue.reset();
        m_asyncComputeSupported = false;
    } else {
        m_asyncComputeSupported = true;
        spdlog::info("Async compute queue created for parallel workloads");
    }

    // Initialize swap chain (now that we have a command queue)
    auto swapChainResult = window->InitializeSwapChain(device, m_commandQueue.get());
    if (swapChainResult.IsErr()) {
        return Result<void>::Err("Failed to initialize swap chain: " + swapChainResult.Error());
    }

    // Create descriptor heaps
    m_descriptorManager = std::make_unique<DescriptorHeapManager>();
    auto heapResult = m_descriptorManager->Initialize(device->GetDevice(), kFrameCount);
    if (heapResult.IsErr()) {
        return Result<void>::Err("Failed to create descriptor heaps: " + heapResult.Error());
    }
    m_descriptorManager->SetFlushCallback([this]() {
        WaitForGPU();
    });

    // Create bindless resource manager for SM6.6 bindless access
    m_bindlessManager = std::make_unique<BindlessResourceManager>();
    auto bindlessResult = m_bindlessManager->Initialize(device->GetDevice(), 16384, 8192);
    if (bindlessResult.IsErr()) {
        spdlog::warn("Bindless resource manager initialization failed: {} (falling back to legacy descriptor tables)",
                     bindlessResult.Error());
        m_bindlessManager.reset();
    } else {
        // Set flush callback for safe deferred releases
        m_bindlessManager->SetFlushCallback([this]() {
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
    m_gpuCulling = std::make_unique<GPUCullingPipeline>();
    auto cullingResult = m_gpuCulling->Initialize(device, m_descriptorManager.get(), m_commandQueue.get(), 65536);
    if (cullingResult.IsErr()) {
        spdlog::warn("GPU Culling initialization failed: {} (falling back to CPU culling)",
                     cullingResult.Error());
        m_gpuCulling.reset();
        m_gpuCullingEnabled = false;
        m_indirectDrawEnabled = false;
    } else {
        m_gpuCulling->SetFlushCallback([this]() {
            WaitForGPU();
        });
        // GPU culling is ready but disabled by default - can be enabled via config
        m_gpuCullingEnabled = false;
        m_indirectDrawEnabled = false;
        spdlog::info("GPU Culling Pipeline initialized (max 65536 instances)");
    }
#ifndef ENABLE_BINDLESS
    m_gpuCullingEnabled = false;
    m_indirectDrawEnabled = false;
    spdlog::info("GPU culling disabled: bindless resources not enabled");
#endif

    // Initialize Render Graph for declarative pass management
    m_renderGraph = std::make_unique<RenderGraph>();
    auto rgResult = m_renderGraph->Initialize(
        device,
        m_commandQueue.get(),
        m_asyncComputeSupported ? m_computeQueue.get() : nullptr,
        m_uploadQueue.get()
    );
    if (rgResult.IsErr()) {
        spdlog::warn("RenderGraph initialization failed: {} (using legacy manual barriers)",
                     rgResult.Error());
        m_renderGraph.reset();
    } else {
        spdlog::info("RenderGraph initialized for declarative pass management");
    }

    // Initialize Visibility Buffer Renderer
    m_visibilityBuffer = std::make_unique<VisibilityBufferRenderer>();
    auto vbResult = m_visibilityBuffer->Initialize(
        device,
        m_descriptorManager.get(),
        m_bindlessManager.get(),
        m_window->GetWidth(),
        m_window->GetHeight()
    );
    if (vbResult.IsErr()) {
        spdlog::warn("VisibilityBuffer initialization failed: {} (using forward rendering)",
                     vbResult.Error());
        m_visibilityBuffer.reset();
    } else {
        spdlog::info("VisibilityBuffer initialized for two-phase deferred rendering");
        m_visibilityBufferEnabled = (std::getenv("CORTEX_ENABLE_VISIBILITY_BUFFER") != nullptr);
        if (!m_visibilityBufferEnabled) {
            spdlog::info("VisibilityBuffer is disabled by default (set CORTEX_ENABLE_VISIBILITY_BUFFER=1 to enable).");
        }
    }

#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    // Initialize Hyper-Geometry (GPU-driven) path
    m_hyperGeometry = std::make_unique<HyperGeometry::HyperGeometryEngine>();
    HyperGeometry::HyperGeometryConfig hyperConfig{};
    hyperConfig.maxMeshlets = 256 * 1024;
    hyperConfig.meshletTargetSize = 64;
    hyperConfig.meshletTargetVerts = 96;
    hyperConfig.debugDirectDraw = false; // avoid double-draw plane; rely on indirect/ classic fallback

    auto hyperResult = m_hyperGeometry->Initialize(device, m_descriptorManager.get(), m_commandQueue.get(), hyperConfig);
    if (hyperResult.IsErr()) {
        spdlog::warn("Hyper-Geometry initialization failed: {}", hyperResult.Error());
        m_hyperGeometry.reset();
    }
#endif

    // Initialize ray tracing context if DXR is supported. If this fails for any
    // reason, hard-disable ray tracing so the toggle becomes inert.
    if (m_rayTracingSupported) {
        m_rayTracingContext = std::make_unique<DX12RaytracingContext>();
        auto rtResult = m_rayTracingContext->Initialize(device, m_descriptorManager.get());
        if (rtResult.IsErr()) {
            spdlog::warn("DXR context initialization failed: {}", rtResult.Error());
            m_rayTracingContext.reset();
            m_rayTracingSupported = false;
            m_rayTracingEnabled = false;
        } else {
            // Set flush callback so RT context can force GPU sync when resizing buffers
            m_rayTracingContext->SetFlushCallback([this]() {
                WaitForGPU();
            });
        }
    }

    // Create command allocators (one per frame)
    for (uint32_t i = 0; i < 3; ++i) {
        HRESULT hr = device->GetDevice()->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,
            IID_PPV_ARGS(&m_commandAllocators[i])
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
    if (m_asyncComputeSupported) {
        for (uint32_t i = 0; i < 3; ++i) {
            HRESULT hr = device->GetDevice()->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_COMPUTE,
                IID_PPV_ARGS(&m_computeAllocators[i])
            );
            if (FAILED(hr)) {
                spdlog::warn("Failed to create compute allocator {}, disabling async compute", i);
                m_asyncComputeSupported = false;
                m_computeQueue.reset();
                break;
            }
        }

        // Create compute command list
        if (m_asyncComputeSupported) {
            HRESULT hr = device->GetDevice()->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_COMPUTE,
                m_computeAllocators[0].Get(),
                nullptr,
                IID_PPV_ARGS(&m_computeCommandList)
            );
            if (FAILED(hr)) {
                spdlog::warn("Failed to create compute command list, disabling async compute");
                m_asyncComputeSupported = false;
                m_computeQueue.reset();
            } else {
                m_computeCommandList->Close();
                m_computeListOpen = false;
            }
        }
    }

    // Create command list
    auto cmdListResult = CreateCommandList();
    if (cmdListResult.IsErr()) {
        return cmdListResult;
    }

    // Create upload command list/allocator pool
    for (uint32_t i = 0; i < kUploadPoolSize; ++i) {
        HRESULT uploadHr = m_device->GetDevice()->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_COPY,
            IID_PPV_ARGS(&m_uploadCommandAllocators[i])
        );
        if (FAILED(uploadHr)) {
            return Result<void>::Err("Failed to create upload command allocator");
        }

        uploadHr = m_device->GetDevice()->CreateCommandList(
            0,
            D3D12_COMMAND_LIST_TYPE_COPY,
            m_uploadCommandAllocators[i].Get(),
            nullptr,
            IID_PPV_ARGS(&m_uploadCommandLists[i])
        );
        if (FAILED(uploadHr)) {
            return Result<void>::Err("Failed to create upload command list");
        }
        m_uploadCommandLists[i]->Close();
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
        m_shadowsEnabled = false;
    }

    // Create HDR render target for main pass
    auto hdrResult = CreateHDRTarget();
    if (hdrResult.IsErr()) {
        spdlog::warn("Failed to create HDR target: {}", hdrResult.Error());
        m_hdrColor.Reset();
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
    if (m_rayTracingSupported && m_rayTracingContext) {
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
    auto cbResult = m_frameConstantBuffer.Initialize(device->GetDevice());
    if (cbResult.IsErr()) {
        return Result<void>::Err("Failed to create frame constant buffer: " + cbResult.Error());
    }

    cbResult = m_objectConstantBuffer.Initialize(device->GetDevice(), 1024); // enough for typical scenes per frame
    if (cbResult.IsErr()) {
        return Result<void>::Err("Failed to create object constant buffer: " + cbResult.Error());
    }

    cbResult = m_materialConstantBuffer.Initialize(device->GetDevice(), 1024);
    if (cbResult.IsErr()) {
        return Result<void>::Err("Failed to create material constant buffer: " + cbResult.Error());
    }

    // Shadow constants: one slot per cascade so we can safely
    // update them independently while recording the shadow pass.
    cbResult = m_shadowConstantBuffer.Initialize(device->GetDevice(), kShadowCascadeCount);
    if (cbResult.IsErr()) {
        return Result<void>::Err("Failed to create shadow constant buffer: " + cbResult.Error());
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
        m_hdrColor.Reset();
        m_hdrRTV = {};
        m_hdrSRV = {};
        SetTAAEnabled(false);
        SetSSREnabled(false);
        SetSSAOEnabled(false);
        m_bloomIntensity = 0.0f;
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
        spdlog::warn("TAA resolve descriptor table init failed; falling back to transient SRV packing: {}",
                     taaTableResult.Error());
    }

    auto postTableResult = InitializePostProcessDescriptorTable();
    if (postTableResult.IsErr()) {
        spdlog::warn("Post-process descriptor table init failed; falling back to transient SRV packing: {}",
                     postTableResult.Error());
    }

    spdlog::info("Renderer initialized successfully");
    return Result<void>::Ok();
}

Result<void> Renderer::InitializeTAAResolveDescriptorTable() {
    m_taaResolveSrvTableValid = false;
    for (auto& handle : m_taaResolveSrvTable) {
        handle = {};
    }

    if (!m_device || !m_descriptorManager) {
        return Result<void>::Err("Renderer not initialized");
    }

    ID3D12Device* device = m_device->GetDevice();
    if (!device) {
        return Result<void>::Err("D3D12 device not available");
    }

    for (size_t i = 0; i < m_taaResolveSrvTable.size(); ++i) {
        auto handleResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (handleResult.IsErr()) {
            return Result<void>::Err("Failed to allocate TAA resolve descriptor: " + handleResult.Error());
        }
        m_taaResolveSrvTable[i] = handleResult.Value();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(nullptr, &srvDesc, m_taaResolveSrvTable[i].cpu);
    }

    m_taaResolveSrvTableValid = m_taaResolveSrvTable[0].IsValid();
    return Result<void>::Ok();
}

void Renderer::UpdateTAAResolveDescriptorTable() {
    if (!m_taaResolveSrvTableValid || !m_device) {
        return;
    }

    ID3D12Device* device = m_device->GetDevice();
    if (!device) {
        return;
    }

    auto copyOrNull = [&](size_t slot,
                          const DescriptorHandle& src,
                          DXGI_FORMAT fmt) {
        if (slot >= m_taaResolveSrvTable.size() || !m_taaResolveSrvTable[slot].IsValid()) {
            return;
        }
        if (src.IsValid()) {
            device->CopyDescriptorsSimple(
                1,
                m_taaResolveSrvTable[slot].cpu,
                src.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = fmt;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(nullptr, &srvDesc, m_taaResolveSrvTable[slot].cpu);
    };

    // Must match PostProcess.hlsl TAAResolvePS bindings:
    // t0 HDR, t1 bloom, t2 SSAO, t3 history, t4 depth, t5 normal/roughness,
    // t6 SSR, t7 velocity.
    copyOrNull(0, m_hdrSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
    copyOrNull(1, m_bloomCombinedSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
    copyOrNull(2, m_ssaoSRV, DXGI_FORMAT_R8_UNORM);
    copyOrNull(3, m_historySRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
    copyOrNull(4, m_depthSRV, DXGI_FORMAT_R32_FLOAT);
    copyOrNull(5, m_gbufferNormalRoughnessSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
    copyOrNull(6, m_ssrSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
    copyOrNull(7, m_velocitySRV, DXGI_FORMAT_R16G16_FLOAT);
}

Result<void> Renderer::InitializePostProcessDescriptorTable() {
    m_postProcessSrvTableValid = false;
    for (auto& handle : m_postProcessSrvTable) {
        handle = {};
    }

    if (!m_device || !m_descriptorManager) {
        return Result<void>::Err("Renderer not initialized");
    }

    ID3D12Device* device = m_device->GetDevice();
    if (!device) {
        return Result<void>::Err("D3D12 device not available");
    }

    for (size_t i = 0; i < m_postProcessSrvTable.size(); ++i) {
        auto handleResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (handleResult.IsErr()) {
            return Result<void>::Err("Failed to allocate post-process descriptor: " + handleResult.Error());
        }
        m_postProcessSrvTable[i] = handleResult.Value();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(nullptr, &srvDesc, m_postProcessSrvTable[i].cpu);
    }

    m_postProcessSrvTableValid = m_postProcessSrvTable[0].IsValid();
    return Result<void>::Ok();
}

void Renderer::UpdatePostProcessDescriptorTable() {
    if (!m_postProcessSrvTableValid || !m_device) {
        return;
    }

    ID3D12Device* device = m_device->GetDevice();
    if (!device) {
        return;
    }

    auto copyOrNull = [&](size_t slot,
                          const DescriptorHandle& src,
                          DXGI_FORMAT fmt) {
        if (slot >= m_postProcessSrvTable.size() || !m_postProcessSrvTable[slot].IsValid()) {
            return;
        }
        if (src.IsValid()) {
            device->CopyDescriptorsSimple(
                1,
                m_postProcessSrvTable[slot].cpu,
                src.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            return;
        }

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = fmt;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Texture2D.MipLevels = 1;
        device->CreateShaderResourceView(nullptr, &srvDesc, m_postProcessSrvTable[slot].cpu);
    };

    // Must match PostProcess.hlsl bindings:
    // t0 HDR, t1 bloom, t2 SSAO, t3 history, t4 depth, t5 normal/roughness,
    // t6 SSR, t7 velocity, t8 RT reflection, t9 RT reflection history.
    copyOrNull(0, m_hdrSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
    copyOrNull(1, m_bloomCombinedSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
    copyOrNull(2, m_ssaoSRV, DXGI_FORMAT_R8_UNORM);
    copyOrNull(3, m_historySRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
    copyOrNull(4, m_depthSRV, DXGI_FORMAT_R32_FLOAT);
    copyOrNull(5, m_gbufferNormalRoughnessSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
    copyOrNull(6, m_ssrSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
    copyOrNull(7, m_velocitySRV, DXGI_FORMAT_R16G16_FLOAT);
    copyOrNull(8, m_rtReflectionSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
    copyOrNull(9, m_rtReflectionHistorySRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
}

void Renderer::ProcessGpuJobsPerFrame() {
    if (m_deviceRemoved) {
        return;
    }

    uint32_t meshCount = 0;
    uint32_t blasCount = 0;

    while (!m_gpuJobQueue.empty()) {
        GpuJob job = m_gpuJobQueue.front();

        if (job.type == GpuJobType::MeshUpload) {
            if (meshCount >= m_maxMeshJobsPerFrame) {
                break;
            }
            if (job.mesh) {
                auto res = UploadMesh(job.mesh);
                if (res.IsErr()) {
                    spdlog::warn("GpuJob MeshUpload '{}' failed: {}", job.label, res.Error());
                }
            }
            if (m_pendingMeshJobs > 0) {
                --m_pendingMeshJobs;
            }
            ++meshCount;
        } else if (job.type == GpuJobType::BuildBLAS) {
            if (blasCount >= m_maxBLASJobsPerFrame) {
                break;
            }
            if (m_rayTracingContext && job.blasMeshKey) {
                m_rayTracingContext->BuildSingleBLAS(job.blasMeshKey);
            }
            if (m_pendingBLASJobs > 0) {
                --m_pendingBLASJobs;
            }
            ++blasCount;
        }

        m_gpuJobQueue.pop_front();
    }
}

bool Renderer::IsRTWarmingUp() const {
    if (!m_rayTracingSupported || !m_rayTracingEnabled || !m_rayTracingContext) {
        return false;
    }
    // Consider RT "warming up" while there are outstanding BLAS jobs either
    // in the renderer's queue or pending inside the DXR context.
    if (m_pendingBLASJobs > 0) {
        return true;
    }
    return m_rayTracingContext->GetPendingBLASCount() > 0;
}

float Renderer::GetEstimatedVRAMMB() const {
    if (!m_window) {
        return 0.0f;
    }

    const float scale = std::clamp(m_renderScale, 0.5f, 1.5f);
    const UINT width  = std::max(1u, static_cast<UINT>(m_window->GetWidth()  * scale));
    const UINT height = std::max(1u, static_cast<UINT>(m_window->GetHeight() * scale));

    auto bytesForRT = [&](UINT w, UINT h, DXGI_FORMAT fmt) -> uint64_t {
        uint32_t bpp = 0;
        switch (fmt) {
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            bpp = 8 * 4;  // 16 bits * 4
            break;
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            bpp = 4 * 8;
            break;
        case DXGI_FORMAT_D32_FLOAT:
            bpp = 4 * 8;
            break;
        default:
            bpp = 4 * 8;
            break;
        }
        const uint64_t bytesPerPixel = bpp / 8u;
        return static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * bytesPerPixel;
    };

    uint64_t totalBytes = 0;

    // Main HDR color + history/taa intermediate (if allocated).
    totalBytes += bytesForRT(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT); // m_hdrColor
    totalBytes += bytesForRT(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT); // history / TAA

    // Depth buffer
    totalBytes += bytesForRT(width, height, DXGI_FORMAT_D32_FLOAT);

    // SSAO at half resolution (only when enabled)
    if (m_ssaoEnabled) {
        totalBytes += bytesForRT(std::max(1u, width / 2u),
                                 std::max(1u, height / 2u),
                                 DXGI_FORMAT_R8G8B8A8_UNORM);
    }

    // SSR color buffer (full resolution RGBA16F, only when enabled)
    if (m_ssrEnabled) {
        totalBytes += bytesForRT(width, height, DXGI_FORMAT_R16G16B16A16_FLOAT);
    }

    // RT reflections + history (half-res RGBA16F, only when RT + reflections are enabled).
    // Base the estimate on the same internal render size used for HDR/depth so
    // scaled resolutions are reflected accurately.
    const UINT halfW = std::max(1u, width  / 2u);
    const UINT halfH = std::max(1u, height / 2u);
    if (m_rayTracingEnabled && m_rtReflectionsEnabled) {
        totalBytes += bytesForRT(halfW, halfH, DXGI_FORMAT_R16G16B16A16_FLOAT); // refl
        totalBytes += bytesForRT(halfW, halfH, DXGI_FORMAT_R16G16B16A16_FLOAT); // refl history
    }

    // RT GI color + history (half-res RGBA16F, only when RT + GI are enabled)
    if (m_rayTracingEnabled && m_rtGIEnabled) {
        totalBytes += bytesForRT(halfW, halfH, DXGI_FORMAT_R16G16B16A16_FLOAT);
        totalBytes += bytesForRT(halfW, halfH, DXGI_FORMAT_R16G16B16A16_FLOAT);
    }

    // Shadow map (four cascades packed into one atlas)
    const UINT shadowRes = static_cast<UINT>(m_shadowMapSize);
    totalBytes += bytesForRT(shadowRes, shadowRes, DXGI_FORMAT_D32_FLOAT);

    // Very coarse allowance for vertex/index buffers and other resources.
    // This keeps the estimate conservative without walking all GPU objects.
    totalBytes += 256ull * 1024ull * 1024ull; // ~256 MB mesh/texture slack

    // Add acceleration-structure memory usage when DXR is active. This folds
    // BLAS/TLAS buffers into the on-screen VRAM estimate so heavy RT scenes
    // surface their additional footprint to the user.
    if (m_rayTracingContext && m_rayTracingSupported) {
        const uint64_t rtBytes = m_rayTracingContext->GetAccelerationStructureBytes();
        totalBytes += rtBytes;
        // Mirror RT structure usage into the asset registry so the memory
        // inspector can report it alongside textures/geometry.
        m_assetRegistry.SetRTStructureBytes(rtBytes);
    }

    const double mb = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
    return static_cast<float>(mb);
}

void Renderer::ApplySafeQualityPreset() {
    // Lower internal resolution and disable the heaviest features so the
    // engine can render more complex scenes on 8 GB GPUs without hitting
    // device-removed errors. Users can re-enable individual features once
    // they confirm there is headroom.
    // Turn off optional RT passes by default; RT shadows follow the master
    // toggle, and reflections/GI are separate feature bits.
    SetRayTracingEnabled(false);
    m_rtReflectionsEnabled = false;
    m_rtGIEnabled = false;

    // Disable costly screen-space effects; FXAA stays on as a cheap fallback.
    SetTAAEnabled(false);
    SetFXAAEnabled(true);
    SetSSREnabled(false);
    SetSSAOEnabled(false);
    SetFogEnabled(false);

    // Cap shadow-map resolution aggressively to keep cascaded shadows from
    // dominating memory and bandwidth in conservative mode.
    m_shadowMapSize = std::min(m_shadowMapSize, 1024.0f);
    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
        m_cascadeResolutionScale[i] = std::min(m_cascadeResolutionScale[i], 0.75f);
    }
    // If the current atlas is larger than the new safe size, recreate it so
    // the VRAM savings take effect immediately instead of waiting for a
    // resize-triggered reallocation.
    RecreateShadowMapResourcesForCurrentSize();

    // Aggressive low-quality preset intended for troubleshooting heavy scenes
    // on 8 GB-class GPUs. This trades resolution, RT, and shadow quality for
    // stability so complex layouts can be inspected without immediately
    // exhausting VRAM. Heavy effects were disabled above so the resolution
    // clamp in SetRenderScale uses the "light" path.
    SetRenderScale(0.75f);

    spdlog::info("Renderer: applied safe low-quality preset (scale=0.75, RT off, SSR/SSAO/Fog off, shadows capped)");
}

void Renderer::Shutdown() {
    // CRITICAL FIX: Wait for GPU to finish all work before destroying resources
    // Otherwise we get OBJECT_DELETED_WHILE_STILL_IN_USE crash
    WaitForGPU();

    if (m_commandQueue) {
        m_commandQueue->Flush();
    }

    if (m_rayTracingContext) {
        m_rayTracingContext->Shutdown();
        m_rayTracingContext.reset();
    }

    if (m_bindlessManager) {
        m_bindlessManager->Shutdown();
        m_bindlessManager.reset();
    }

    if (m_gpuCulling) {
        m_gpuCulling->Shutdown();
        m_gpuCulling.reset();
    }

    if (m_renderGraph) {
        m_renderGraph->Shutdown();
        m_renderGraph.reset();
    }

    // Clean up async compute resources
    if (m_computeQueue) {
        m_computeQueue->Flush();
    }
    m_computeCommandList.Reset();
    for (auto& allocator : m_computeAllocators) {
        allocator.Reset();
    }
    m_computeQueue.reset();
    m_asyncComputeSupported = false;

    m_placeholderAlbedo.reset();
    m_placeholderNormal.reset();
    m_placeholderMetallic.reset();
    m_placeholderRoughness.reset();
    m_textureCache.clear();
    m_depthBuffer.Reset();
    m_shadowMap.Reset();
    m_hdrColor.Reset();
    m_ssaoTex.Reset();
    m_commandList.Reset();
    for (auto& allocator : m_commandAllocators) {
        allocator.Reset();
    }

    m_shadowPipeline.reset();
    m_pipeline.reset();
    m_rootSignature.reset();
    m_descriptorManager.reset();
    m_commandQueue.reset();

    spdlog::info("Renderer shut down");
}

void Renderer::Render(Scene::ECS_Registry* registry, float deltaTime) {
    // Monotonic frame counter for diagnostics; independent of swap-chain
    // buffer index so logs can be correlated easily.
    ++m_renderFrameCounter;
    MarkPassComplete("Render_Entry");

    // All passes enabled by default; per-feature runtime flags (m_ssaoEnabled,
    // m_ssrEnabled, etc.) still control whether they actually run.
    constexpr bool kEnableShadowPass    = true;
    constexpr bool kEnableMotionVectors = true;
    constexpr bool kEnableTAA           = true;
    constexpr bool kEnableSSRDefault    = true;
    constexpr bool kEnableParticles     = true;
    constexpr bool kEnableSSAODefault   = true;
    constexpr bool kEnableBloomDefault  = true;
    // Fullscreen post-process resolve writes HDR scene color to the swap-chain back buffer.
    constexpr bool kEnablePostProcessDefault = true;
    constexpr bool kEnableDebugLines    = true;

    if (m_deviceRemoved) {
        if (!m_deviceRemovedLogged) {
            spdlog::error("Renderer::Render skipped because DX12 device was removed earlier (likely out of GPU memory). Restart is required.");
            m_deviceRemovedLogged = true;
        }
        return;
    }

    if (!m_window || !m_window->GetCurrentBackBuffer()) {
        spdlog::error("Renderer::Render called without a valid back buffer; skipping frame");
        return;
    }

    using clock = std::chrono::high_resolution_clock;

    m_totalTime += deltaTime;

    // Force all graphics features to be enabled - ignore environment variables
    // that would disable them for debugging purposes
    static bool s_checkedPassEnv = false;
    static bool s_disableSSR   = false;
    static bool s_disableSSAO  = false;
    static bool s_disableBloom = false;
    static bool s_disableTAA   = false;
    if (!s_checkedPassEnv) {
        s_checkedPassEnv = true;
        // Always keep features enabled - don't check environment variables
        s_disableSSR = false;
        s_disableSSAO = false;
        s_disableBloom = false;
        s_disableTAA = false;
        spdlog::info("Renderer: All graphics features forcibly enabled (SSR, SSAO, Bloom, TAA)");
    }

    const bool kEnableSSR  = kEnableSSRDefault  && !s_disableSSR;
    const bool kEnableSSAO = kEnableSSAODefault && !s_disableSSAO;
    const bool kEnableBloom = kEnableBloomDefault && !s_disableBloom;
    const bool kEnableTAAThisFrame = kEnableTAA && !s_disableTAA;

    // Optional DXGI video memory diagnostics. When CORTEX_LOG_VRAM is set,
    // log current GPU memory usage and budget periodically so device-removed
    // faults under HDR/post-process load can be correlated with VRAM
    // pressure on the user's adapter.
    static bool s_checkedVramEnv = false;
    static bool s_logVramUsage = false;
    if (!s_checkedVramEnv) {
        s_checkedVramEnv = true;
        if (std::getenv("CORTEX_LOG_VRAM")) {
            s_logVramUsage = true;
            spdlog::info("Renderer: CORTEX_LOG_VRAM set; logging DXGI video memory usage periodically");
        }
    }
    if (s_logVramUsage && m_device) {
        constexpr uint64_t kLogIntervalFrames = 60;
        if ((m_renderFrameCounter % kLogIntervalFrames) == 0) {
            auto vramResult = m_device->QueryVideoMemoryInfo();
            if (vramResult.IsOk()) {
                const DX12Device::VideoMemoryInfo& info = vramResult.Value();
                const double usageMB = static_cast<double>(info.currentUsageBytes) / (1024.0 * 1024.0);
                const double budgetMB = static_cast<double>(info.budgetBytes) / (1024.0 * 1024.0);
                const double availMB = static_cast<double>(info.availableForReservationBytes) / (1024.0 * 1024.0);
                spdlog::info("VRAM: usage={:.1f} MB, budget={:.1f} MB, availableForReservation={:.1f} MB",
                             usageMB, budgetMB, availMB);
            } else {
                spdlog::warn("Renderer: QueryVideoMemoryInfo failed (disabling CORTEX_LOG_VRAM): {}",
                             vramResult.Error());
                s_logVramUsage = false;
            }
        }
    }

    // Ensure all environment maps are loaded before rendering the scene. This
    // trades a slightly longer startup for stable frame times once the scene
    // becomes interactive. On 8 GB-class GPUs we avoid automatically loading
    // deferred environments to keep env/IBL memory bounded.
    uint32_t maxEnvLoadsPerFrame = std::numeric_limits<uint32_t>::max();
    if (m_device) {
        const std::uint64_t bytes = m_device->GetDedicatedVideoMemoryBytes();
        const std::uint64_t mb = bytes / (1024ull * 1024ull);
        if (mb > 0 && mb <= 8192ull) {
            maxEnvLoadsPerFrame = 0;
        } else {
            maxEnvLoadsPerFrame = 2;
        }
    }
    ProcessPendingEnvironmentMaps(maxEnvLoadsPerFrame);

    // Process a limited number of heavy GPU jobs (mesh uploads / BLAS builds)
    // per frame so scene rebuilds and RT warm-up do not spike the first frame.
    ProcessGpuJobsPerFrame();
    MarkPassComplete("Render_BeforeBeginFrame");

    // Common frame setup (depth/HDR resize, command list reset, constant
    // buffer updates) shared by both the classic raster/RT backend and the
    // experimental voxel renderer.
    BeginFrame();
    WriteBreadcrumb(GpuMarker::BeginFrame);
    if (m_deviceRemoved) {
        // A fatal error occurred while preparing frame resources (for example,
        // depth/HDR creation failed due to device removal). Skip the rest of
        // this frame; the next call will early-out at the top.
        MarkPassComplete("BeginFrame_DeviceRemoved");
        return;
    }
    MarkPassComplete("BeginFrame_Done");
    UpdateFrameConstants(deltaTime, registry);
    MarkPassComplete("UpdateFrameConstants_Done");
    PrewarmMaterialDescriptors(registry);

    // Optional ultra-minimal debug frame: clear the current back buffer and
    // present, skipping all geometry, lighting, and post-process work. This
    // is controlled via an environment variable so normal builds render the
    // full scene by default.
    static bool s_checkedMinimalEnv = false;
    static bool s_forceMinimalFrame = false;
    if (!s_checkedMinimalEnv) {
        s_checkedMinimalEnv = true;
        if (std::getenv("CORTEX_FORCE_MINIMAL_FRAME")) {
            s_forceMinimalFrame = true;
            spdlog::warn("Renderer: CORTEX_FORCE_MINIMAL_FRAME set; running ultra-minimal clear-only frame path");
        }
    }
    if (s_forceMinimalFrame) {
        ID3D12Resource* backBuffer = m_window->GetCurrentBackBuffer();
        if (backBuffer) {
            D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_window->GetCurrentRTV();

            D3D12_RESOURCE_BARRIER bbBarrier{};
            bbBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bbBarrier.Transition.pResource   = backBuffer;
            bbBarrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
            bbBarrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
            bbBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandList->ResourceBarrier(1, &bbBarrier);
            m_backBufferUsedAsRTThisFrame = true;

            const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
            m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
        }

        EndFrame();
        return;
    }

    // Experimental voxel backend: replace the traditional raster + RT path
    // with a fullscreen voxel raymarch pass. This is primarily intended for
    // research and diagnostics; when enabled we still reuse the same DX12
    // device, swap chain, and FrameConstants so camera controls and lighting
    // stay consistent with the classic renderer.
    if (m_voxelBackendEnabled) {
        RenderVoxel(registry);
        MarkPassComplete("RenderVoxel_Done");
        EndFrame();
        return;
    }

    // Optional ray tracing path (DXR). When enabled we build BLAS/TLAS and
    // dispatch ray-traced passes using the current frame's depth buffer. To
    // ensure depth and TLAS are consistent, render a depth-only prepass
    // before invoking the DXR pipelines.
    const auto tBeforeRT = clock::now();
    if (m_rayTracingSupported && m_rayTracingEnabled && m_rayTracingContext) {
        const auto tDepthStart = clock::now();
        RenderDepthPrepass(registry);
        MarkPassComplete("RenderDepthPrepass_Done");
        const auto tDepthEnd = clock::now();
        m_lastDepthPrepassMs =
            std::chrono::duration_cast<std::chrono::microseconds>(tDepthEnd - tDepthStart).count() / 1000.0f;

        RenderRayTracing(registry);
        MarkPassComplete("RenderRayTracing_Done");
    }
    const auto tAfterRT = clock::now();
    m_lastRTPassMs =
        std::chrono::duration_cast<std::chrono::microseconds>(tAfterRT - tBeforeRT).count() / 1000.0f;

    const auto tMainStart = clock::now();

    // First pass: render depth from directional light
    if (kEnableShadowPass && m_shadowsEnabled && m_shadowMap && m_shadowPipeline) {
        const auto tShadowStart = clock::now();
        WriteBreadcrumb(GpuMarker::ShadowPass);
        RenderShadowPass(registry);
        MarkPassComplete("RenderShadowPass_Done");
        const auto tShadowEnd = clock::now();
        m_lastShadowPassMs =
            std::chrono::duration_cast<std::chrono::microseconds>(tShadowEnd - tShadowStart).count() / 1000.0f;
    }

    // Main scene pass
    PrepareMainPass();
    MarkPassComplete("PrepareMainPass_Done");

    // Draw environment background (skybox) into the HDR target before geometry.
    WriteBreadcrumb(GpuMarker::Skybox);
    RenderSkybox();
    MarkPassComplete("RenderSkybox_Done");

    bool drewWithHyper = false;
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    if (m_hyperGeometry) {
        auto buildResult = EnsureHyperGeometryScene(registry);
        if (buildResult.IsErr()) {
            spdlog::warn("Hyper-Geometry scene build failed: {}", buildResult.Error());
        } else {
            auto hyperResult = m_hyperGeometry->Render(m_commandList.Get(), registry, m_window->GetAspectRatio());
            if (hyperResult.IsErr()) {
                spdlog::warn("Hyper-Geometry render failed: {}", hyperResult.Error());
            } else {
                drewWithHyper = true;
            }
        }
    }
#endif

    // Classic path now acts purely as fallback to avoid double-drawing/z-fighting
    if (!drewWithHyper) {
        // Visibility buffer path (Phase 2.1)
        if (m_visibilityBuffer && m_visibilityBufferEnabled) {
            // spdlog::info("Taking visibility buffer path");  // Too noisy
            WriteBreadcrumb(GpuMarker::OpaqueGeometry);
            RenderVisibilityBufferPath(registry);
            MarkPassComplete("VisibilityBuffer_Done");
        }
        // Fallback: GPU culling path (Phase 1 GPU-driven rendering)
        else if (m_gpuCullingEnabled && m_gpuCulling) {
            static uint64_t s_lastCullingPathLogFrame = 0;
            if ((m_renderFrameCounter % 120) == 0 && m_renderFrameCounter != s_lastCullingPathLogFrame) {
                spdlog::info("Taking GPU culling path");
                s_lastCullingPathLogFrame = m_renderFrameCounter;
            }
            WriteBreadcrumb(GpuMarker::OpaqueGeometry);
            RenderSceneIndirect(registry);
            MarkPassComplete("RenderSceneIndirect_Done");
        } else {
            // Opaque geometry first (legacy per-draw path)
            spdlog::info("Taking legacy forward rendering path");
            WriteBreadcrumb(GpuMarker::OpaqueGeometry);
            RenderScene(registry);
            MarkPassComplete("RenderScene_Done");
        }
        // Then blended transparent/glass objects, sorted back-to-front.
        WriteBreadcrumb(GpuMarker::TransparentGeom);
        RenderTransparent(registry);
        MarkPassComplete("RenderTransparent_Done");
    }

    // Camera motion vectors for TAA/motion blur (from depth + matrices).
    if (kEnableMotionVectors && m_motionVectorsPipeline && m_velocityBuffer && m_depthBuffer) {
        WriteBreadcrumb(GpuMarker::MotionVectors);
        RenderMotionVectors();
        MarkPassComplete("RenderMotionVectors_Done");
    }

    // HDR TAA resolve pass (stabilizes main lighting before reflections,
    // bloom, fog, and tonemapping).
    if (kEnableTAAThisFrame) {
        WriteBreadcrumb(GpuMarker::TAAResolve);
        RenderTAA();
        MarkPassComplete("RenderTAA_Done");
    }

    const auto tMainEnd = clock::now();
    m_lastMainPassMs =
        std::chrono::duration_cast<std::chrono::microseconds>(tMainEnd - tMainStart).count() / 1000.0f;

    // Screen-space reflections using HDR + depth + G-buffer (optional).
    const auto tPostStart = clock::now();

    if (kEnableSSR && m_ssrEnabled && m_ssrPipeline && m_ssrColor && m_hdrColor && m_gbufferNormalRoughness) {
        const auto tSsrStart = clock::now();
        // Dedicated helper keeps SSR logic contained.
        WriteBreadcrumb(GpuMarker::SSR);
        RenderSSR();
        MarkPassComplete("RenderSSR_Done");
        const auto tSsrEnd = clock::now();
        m_lastSSRMs =
            std::chrono::duration_cast<std::chrono::microseconds>(tSsrEnd - tSsrStart).count() / 1000.0f;
    } else {
        m_lastSSRMs = 0.0f;
    }

    // GPU-instanced particle sprites (smoke / fire). Rendered after the
    // TAA resolve so they layer over the stable HDR image but before SSAO,
    // bloom, and post-process tonemapping. Scenes can disable this via
    // SetParticlesEnabled when running on tight VRAM budgets.
    if (kEnableParticles && m_particlesEnabledForScene) {
        MarkPassComplete("RenderParticles_Begin");
        WriteBreadcrumb(GpuMarker::Particles);
        RenderParticles(registry);
        MarkPassComplete("RenderParticles_Done");
    }

    // Screen-space ambient occlusion from depth buffer (if enabled)
    {
        const auto tSsaoStart = clock::now();
        if (kEnableSSAO) {
            WriteBreadcrumb(GpuMarker::SSAO);
            // Use async compute SSAO if available (faster compute shader path)
            if (m_ssaoComputePipeline && m_asyncComputeSupported) {
                RenderSSAOAsync();
            } else {
                RenderSSAO();
            }
            MarkPassComplete("RenderSSAO_Done");
        } else {
            m_lastSSAOMs = 0.0f;
        }
        const auto tSsaoEnd = clock::now();
        m_lastSSAOMs =
            std::chrono::duration_cast<std::chrono::microseconds>(tSsaoEnd - tSsaoStart).count() / 1000.0f;
    }

    // Bloom passes operating on HDR buffer (if available)
    {
        const auto tBloomStart = clock::now();
        if (kEnableBloom) {
            WriteBreadcrumb(GpuMarker::Bloom);
            RenderBloom();
            MarkPassComplete("RenderBloom_Done");
        } else {
            m_lastBloomMs = 0.0f;
        }
        const auto tBloomEnd = clock::now();
        m_lastBloomMs =
            std::chrono::duration_cast<std::chrono::microseconds>(tBloomEnd - tBloomStart).count() / 1000.0f;
    }

    // Post-process HDR -> back buffer (or no-op if disabled). Allow disabling
    // via environment variable for targeted debugging of device-removed faults.
    static bool s_checkedDisablePostProcessEnv = false;
    static bool s_disablePostProcess = false;
    if (!s_checkedDisablePostProcessEnv) {
        s_checkedDisablePostProcessEnv = true;
        if (std::getenv("CORTEX_DISABLE_POST_PROCESS")) {
            s_disablePostProcess = true;
            spdlog::warn("Renderer: CORTEX_DISABLE_POST_PROCESS set; skipping RenderPostProcess pass");
        }
    }
    const bool enablePostProcess = kEnablePostProcessDefault && !s_disablePostProcess;

    if (enablePostProcess) {
        const auto tPostOnlyStart = clock::now();
        WriteBreadcrumb(GpuMarker::PostProcess);
        RenderPostProcess();
        MarkPassComplete("RenderPostProcess_Done");
        const auto tPostOnlyEnd = clock::now();
        m_lastPostMs =
            std::chrono::duration_cast<std::chrono::microseconds>(tPostOnlyEnd - tPostOnlyStart).count() / 1000.0f;
    } else {
        m_lastPostMs = 0.0f;
        MarkPassComplete("RenderPostProcess_Skipped");
    }

    // Debug overlay lines rendered after all post-processing so they are not
    // affected by tone mapping, bloom, or TAA.
    if (kEnableDebugLines) {
        WriteBreadcrumb(GpuMarker::DebugLines);
        RenderDebugLines();
        MarkPassComplete("RenderDebugLines_Done");
    }

    // Reset per-frame RT reflection write flag so history updates only occur
    // on frames where the DXR reflections pass actually ran.
    m_rtReflectionWrittenThisFrame = false;

    EndFrame();

    // If desired later, we can expose total render CPU time via
    // duration_cast here using (clock::now() - frameStart).
}

void Renderer::ResetTemporalHistoryForSceneChange() {
    // Reset TAA history so the first frame after a scene switch uses the
    // current HDR as the new history without blending in the previous scene.
    m_hasHistory = false;
    m_taaSampleIndex = 0;
    m_taaJitterPrevPixels = glm::vec2(0.0f);
    m_taaJitterCurrPixels = glm::vec2(0.0f);
    m_hasPrevViewProj = false;

    // Reset RT temporal data so RT shadows / GI / reflections do not leave
    // ghosted silhouettes from the previous scene.
    m_rtHasHistory   = false;
    m_rtGIHasHistory = false;
    m_rtReflHasHistory = false;
    m_hasPrevCamera  = false;

    // Clear any pending debug-line state to avoid drawing lines that belonged
    // to the previous layout.
    m_debugLines.clear();
    m_debugLinesDisabled = false;
}

void Renderer::WaitForAllFrames() {
    // Wait for ALL in-flight frames to complete, not just the current one.
    // With triple buffering, frames N-1 and N-2 might still be executing
    // and holding references to resources we're about to delete.
    for (uint32_t i = 0; i < kFrameCount; ++i) {
        if (m_fenceValues[i] > 0 && m_commandQueue) {
            m_commandQueue->WaitForFenceValue(m_fenceValues[i]);
        }
    }

    // Also flush any pending upload work
    if (m_uploadQueue) {
        m_uploadQueue->Flush();
    }
}

void Renderer::ResetCommandList() {
    // If we are mid-frame when a scene change occurs, the command list might
    // reference objects we are about to delete. We need to:
    // 1. Wait for ALL in-flight frames (not just current one) to complete
    // 2. Reset ALL command allocators to clear internal resource references
    // 3. Reset the command list with a fresh allocator
    // 4. Clear pending GPU jobs that hold raw pointers
    //
    // NOTE: BLAS cache and mesh asset keys are NOT cleared here - they are
    // cleared separately by the scene rebuild process to avoid timing issues
    // with the command list still referencing BLAS resources.

    if (!m_commandList || !m_commandQueue) {
        return;
    }

    // Step 1: Wait for ALL in-flight GPU work to complete
    // This ensures no GPU operations are still using resources we'll delete.
    WaitForAllFrames();

    // Step 2: Close the command list if it's open, then reset ALL allocators.
    // We reset ALL allocators because we don't know which one the command list
    // was using when it recorded RT commands.
    if (m_commandListOpen) {
        m_commandList->Close();
        m_commandListOpen = false;
    }

    for (uint32_t i = 0; i < kFrameCount; ++i) {
        if (m_commandAllocators[i]) {
            m_commandAllocators[i]->Reset();
        }
    }

    // Step 3: Reset the command list with a fresh allocator
    // This clears all internal references - the command list is now EMPTY
    if (m_frameIndex < kFrameCount && m_commandAllocators[m_frameIndex]) {
        m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr);
        m_commandListOpen = true;
    }

    // Step 4: Clear pending GPU jobs that contain raw pointers to mesh data.
    m_gpuJobQueue.clear();
    m_pendingMeshJobs = 0;
    m_pendingBLASJobs = 0;
}

void Renderer::ClearBLASCache() {
    // Clear all BLAS entries from the ray tracing context.
    // This MUST be called AFTER ResetCommandList() to ensure no GPU operations
    // are still referencing these resources.
    if (m_rayTracingContext) {
        m_rayTracingContext->ClearAllBLAS();
        spdlog::info("Renderer: BLAS cache cleared for scene switch");
    }

    // Also clear mesh asset keys so stale pointers don't get reused
    m_meshAssetKeys.clear();
}

void Renderer::RenderRayTracing(Scene::ECS_Registry* registry) {
    if (!m_rayTracingSupported || !m_rayTracingEnabled || !m_rayTracingContext || !registry) {
        return;
    }

    const bool rtWarmingUp = IsRTWarmingUp();

    ComPtr<ID3D12GraphicsCommandList4> rtCmdList;
    HRESULT hr = m_commandList.As(&rtCmdList);
    if (FAILED(hr) || !rtCmdList) {
        return;
    }

    // Ensure the depth buffer is in a readable state for the DXR passes.
    if (m_depthBuffer && m_depthState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER depthBarrier{};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = m_depthBuffer.Get();
        depthBarrier.Transition.StateBefore = m_depthState;
        depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rtCmdList->ResourceBarrier(1, &depthBarrier);
        m_depthState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Ensure the RT shadow mask is ready for UAV writes before the DXR pass.
    if (m_rtShadowMask && m_rtShadowMaskState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_rtShadowMask.Get();
        barrier.Transition.StateBefore = m_rtShadowMaskState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        rtCmdList->ResourceBarrier(1, &barrier);
        m_rtShadowMaskState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Set the current frame index so BLAS builds can track when they were
    // recorded. This is used by ReleaseScratchBuffers() to ensure scratch
    // buffers aren't freed until the GPU has finished using them.
    m_rayTracingContext->SetCurrentFrameIndex(m_absoluteFrameIndex);

    // Build TLAS over the current ECS scene.
    m_rayTracingContext->BuildTLAS(registry, rtCmdList.Get());

    // Dispatch the DXR sun-shadow pass when depth and mask descriptors are ready.
    if (m_depthSRV.IsValid() && m_rtShadowMaskUAV.IsValid()) {
        DescriptorHandle envTable = m_shadowAndEnvDescriptors[0];
            m_rayTracingContext->DispatchRayTracing(
                rtCmdList.Get(),
                m_depthSRV,
                m_rtShadowMaskUAV,
                m_frameConstantBuffer.gpuAddress,
                envTable);
    }

    // Optional RT reflections: reuse the same TLAS and depth buffer to write
    // a reflection color buffer that can be inspected or composed in
    // post-process. This pass is entirely optional and disabled by default;
    // if the reflection pipeline was not created successfully,
    // DispatchReflections is a no-op.
    if (!rtWarmingUp && m_rtReflectionsEnabled && m_rtReflectionColor && m_rtReflectionUAV.IsValid()) {
        if (m_rtReflectionState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            D3D12_RESOURCE_BARRIER reflBarrier{};
            reflBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            reflBarrier.Transition.pResource = m_rtReflectionColor.Get();
            reflBarrier.Transition.StateBefore = m_rtReflectionState;
            reflBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            reflBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            rtCmdList->ResourceBarrier(1, &reflBarrier);
            m_rtReflectionState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        if (m_depthSRV.IsValid() && m_gbufferNormalRoughnessSRV.IsValid()) {
            DescriptorHandle envTable = m_shadowAndEnvDescriptors[0];
            m_rayTracingContext->DispatchReflections(
                rtCmdList.Get(),
                m_depthSRV,
                m_rtReflectionUAV,
                m_frameConstantBuffer.gpuAddress,
                envTable,
                m_gbufferNormalRoughnessSRV);
            m_rtReflectionWrittenThisFrame = true;
        }
    }

    // Optional RT diffuse GI: writes a low-frequency indirect lighting buffer
    // that can be sampled by the main PBR shader. As with reflections, this
    // pass is optional and disabled by default; DispatchGI is a no-op if the
    // GI pipeline is not available.
    if (!rtWarmingUp && m_rtGIEnabled && m_rtGIColor && m_rtGIUAV.IsValid()) {
        if (m_rtGIState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
            D3D12_RESOURCE_BARRIER giBarrier{};
            giBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            giBarrier.Transition.pResource = m_rtGIColor.Get();
            giBarrier.Transition.StateBefore = m_rtGIState;
            giBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            giBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            rtCmdList->ResourceBarrier(1, &giBarrier);
            m_rtGIState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        }

        if (m_depthSRV.IsValid()) {
            DescriptorHandle envTable = m_shadowAndEnvDescriptors[0];
            m_rayTracingContext->DispatchGI(
                rtCmdList.Get(),
                m_depthSRV,
                m_rtGIUAV,
                m_frameConstantBuffer.gpuAddress,
                envTable);
        }
    }
}

void Renderer::RebuildAssetRefsFromScene(Scene::ECS_Registry* registry) {
    if (!registry) {
        return;
    }

    // Reset all ref-counts to zero and then rebuild them from the current
    // ECS graph. This produces an accurate snapshot of which meshes are
    // still referenced after a scene rebuild.
    m_assetRegistry.ResetAllRefCounts();

    auto view = registry->View<Scene::RenderableComponent>();
    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);

        // Mesh references: map MeshData* to asset key when available.
        if (renderable.mesh) {
            const Scene::MeshData* meshPtr = renderable.mesh.get();
            auto it = m_meshAssetKeys.find(meshPtr);
            if (it != m_meshAssetKeys.end()) {
                m_assetRegistry.AddRefMeshKey(it->second);
            }
        }

        // Texture references: paths are used as canonical keys. Dreamer and
        // other non-file sentinel values are ignored for now.
        auto refPath = [&](const std::string& path) {
            if (path.empty()) {
                return;
            }
            if (!path.empty() && path[0] == '[') {
                return;
            }
            m_assetRegistry.AddRefTextureKey(path);
        };

        refPath(renderable.textures.albedoPath);
        refPath(renderable.textures.normalPath);
        refPath(renderable.textures.metallicPath);
        refPath(renderable.textures.roughnessPath);
    }
}

void Renderer::PruneUnusedMeshes(Scene::ECS_Registry* /*registry*/) {
    // Focus on BLAS/geometry cleanup; texture lifetime is primarily tied to
    // scene entities and will be reclaimed when those are destroyed.
    auto unused = m_assetRegistry.CollectUnusedMeshes();
    if (unused.empty()) {
        return;
    }

    uint64_t totalBytes = 0;
    uint32_t count = 0;

    for (const auto& asset : unused) {
        totalBytes += asset.bytes;
        ++count;

        // Locate the MeshData* corresponding to this key so BLAS entries can
        // be released. We expect only a small number of meshes, so a simple
        // linear search over m_meshAssetKeys is sufficient.
        const Scene::MeshData* meshPtr = nullptr;
        for (const auto& kv : m_meshAssetKeys) {
            if (kv.second == asset.key) {
                meshPtr = kv.first;
                break;
            }
        }

        if (meshPtr && m_rayTracingContext) {
            m_rayTracingContext->ReleaseBLASForMesh(meshPtr);
        }

        // Remove from the mesh key map so future ref rebuilds do not consider it.
        if (meshPtr) {
            m_meshAssetKeys.erase(meshPtr);
        }
    }

    const double mb = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
    spdlog::info("Pruned {} unused meshes (≈{:.1f} MB of geometry/BLAS candidates)", count, mb);
}

void Renderer::PruneUnusedTextures() {
    auto unused = m_assetRegistry.CollectUnusedTextures();
    if (unused.empty()) {
        return;
    }

    uint64_t totalBytes = 0;
    uint32_t count = 0;

    for (const auto& asset : unused) {
        totalBytes += asset.bytes;
        ++count;
        // Removing the entry from the registry is sufficient from the
        // diagnostics perspective; the underlying DX12Texture resources are
        // owned by shared_ptrs attached to scene materials and will already
        // have been released when those components were destroyed.
        m_assetRegistry.UnregisterTexture(asset.key);
    }

    const double mb = static_cast<double>(totalBytes) / (1024.0 * 1024.0);
    spdlog::info("Pruned {} unused textures from registry (≈{:.1f} MB candidates)", count, mb);
}

void Renderer::BeginFrame() {
    // Handle window resize: recreate depth buffer when the window size
    // changes. To keep this path maximally stable on 8 GB-class GPUs we
    // currently allocate depth/HDR at the window resolution only and ignore
    // renderScale for the underlying resource size; internal resolution
    // scaling is handled in the shader paths instead. This avoids repeated
    // large reallocations when renderScale changes and has proven more
    // robust on devices prone to device-removed faults under memory
    // pressure.
    const float renderScale = std::clamp(m_renderScale, 0.5f, 1.5f);
    const UINT expectedDepthWidth  = std::max<UINT>(1, m_window->GetWidth());
    const UINT expectedDepthHeight = std::max<UINT>(1, m_window->GetHeight());

    bool needDepthResize = false;
    bool needHDRResize   = false;
    bool needSSAOResize  = false;

    // Reset per-frame back-buffer state tracking; individual passes that
    // render directly to the swap-chain will set this when they transition
    // the back buffer from PRESENT to RENDER_TARGET.
    m_backBufferUsedAsRTThisFrame = false;

    // Wait for this frame's command allocator/descriptor segment to be available
    m_frameIndex = m_window->GetCurrentBackBufferIndex();
    if (m_fenceValues[m_frameIndex] != 0) {
        uint64_t completedValue = m_commandQueue->GetLastCompletedFenceValue();
        uint64_t expectedValue = m_fenceValues[m_frameIndex];

        if (completedValue < expectedValue) {
            spdlog::debug("BeginFrame waiting for GPU: frameIndex={}, expected={}, completed={}, delta={}",
                          m_frameIndex, expectedValue, completedValue, expectedValue - completedValue);
        }

        m_commandQueue->WaitForFenceValue(m_fenceValues[m_frameIndex]);
    }

    if (m_gpuCulling) {
        m_gpuCulling->UpdateVisibleCountFromReadback();
    }

    if (m_descriptorManager) {
        m_descriptorManager->BeginFrame(m_frameIndex);
    }

    if (m_depthBuffer) {
        D3D12_RESOURCE_DESC depthDesc = m_depthBuffer->GetDesc();
        if (depthDesc.Width != expectedDepthWidth || depthDesc.Height != expectedDepthHeight) {
            needDepthResize = true;
        }
    }

    if (m_hdrColor) {
        D3D12_RESOURCE_DESC hdrDesc = m_hdrColor->GetDesc();
        if (hdrDesc.Width != expectedDepthWidth || hdrDesc.Height != expectedDepthHeight) {
            needHDRResize = true;
        }
    }

    // Check SSAO resize (half resolution)
    if (m_ssaoTex) {
        D3D12_RESOURCE_DESC ssaoDesc = m_ssaoTex->GetDesc();
        UINT expectedWidth  = std::max<UINT>(1, m_window->GetWidth()  / 2);
        UINT expectedHeight = std::max<UINT>(1, m_window->GetHeight() / 2);
        if (ssaoDesc.Width != expectedWidth || ssaoDesc.Height != expectedHeight) {
            needSSAOResize = true;
        }
    }

    // CRITICAL: Wait for GPU before destroying ANY render targets
    if ((needDepthResize || needHDRResize || needSSAOResize) && !m_deviceRemoved) {
        spdlog::info("BeginFrame: reallocating render targets for renderScale {:.2f} ({}x{})",
                     renderScale, expectedDepthWidth, expectedDepthHeight);
        // Must wait for GPU to finish using old resources before destroying them
        // Normal frame fencing is NOT sufficient - Debug Layer proves we need explicit sync here
        WaitForGPU();
    }

    if (needDepthResize && m_depthBuffer) {
        spdlog::info("BeginFrame: recreating depth buffer for renderScale {:.2f} ({}x{})",
                     renderScale, expectedDepthWidth, expectedDepthHeight);
        m_depthBuffer.Reset();
        m_depthStencilView = {};  // Invalidate descriptor handles before recreating
        m_depthSRV = {};          // Prevents stale descriptor usage if CreateDepthBuffer fails
        auto depthResult = CreateDepthBuffer();
        if (depthResult.IsErr()) {
            spdlog::error("Failed to recreate depth buffer on resize: {}", depthResult.Error());
            // Treat this as a fatal condition for the current run.
            m_deviceRemoved = true;
            return;
        }
    }

    // Handle HDR target resize using the same effective render resolution.
    if (needHDRResize && m_hdrColor) {
        spdlog::info("BeginFrame: recreating HDR target for renderScale {:.2f} ({}x{})",
                     renderScale, expectedDepthWidth, expectedDepthHeight);
        m_hdrColor.Reset();
        m_hdrRTV = {};   // Invalidate descriptor handle before recreating
        m_hdrSRV = {};   // Prevents stale descriptor usage if CreateHDRTarget fails
        auto hdrResult = CreateHDRTarget();
        if (hdrResult.IsErr()) {
            spdlog::error("Failed to recreate HDR target on resize: {}", hdrResult.Error());
            m_deviceRemoved = true;
            return;
        }

        auto rtMaskResult = CreateRTShadowMask();
        if (rtMaskResult.IsErr()) {
            spdlog::warn("Failed to recreate RT shadow mask on resize: {}", rtMaskResult.Error());
        }

        if (m_rayTracingSupported && m_rayTracingContext) {
            auto rtReflResult = CreateRTReflectionResources();
            if (rtReflResult.IsErr()) {
                spdlog::warn("Failed to recreate RT reflection buffer on resize: {}", rtReflResult.Error());
            }

            auto rtGiResult = CreateRTGIResources();
            if (rtGiResult.IsErr()) {
                spdlog::warn("Failed to recreate RT GI buffer on resize: {}", rtGiResult.Error());
            }
        }
    }
    // Handle SSAO target resize (SSAO is rendered at half resolution).
    if (needSSAOResize && m_ssaoTex) {
        spdlog::info("BeginFrame: recreating SSAO target (half resolution)");
        m_ssaoTex.Reset();
        auto ssaoResult = CreateSSAOResources();
        if (ssaoResult.IsErr()) {
            spdlog::error("Failed to recreate SSAO target on resize: {}", ssaoResult.Error());
            m_ssaoEnabled = false;
        }
    }
    // Propagate resize to ray tracing context so it can adjust any RT targets.
    if (m_rayTracingContext && m_window) {
        m_rayTracingContext->OnResize(m_window->GetWidth(), m_window->GetHeight());
    }

    // Resize visibility buffer
    if (m_visibilityBuffer && m_window) {
        auto vbResizeResult = m_visibilityBuffer->Resize(m_window->GetWidth(), m_window->GetHeight());
        if (vbResizeResult.IsErr()) {
            spdlog::warn("VisibilityBuffer resize failed: {}", vbResizeResult.Error());
        }
    }

    // Reset dynamic constant buffer offsets (safe because we fence each frame)
    m_objectConstantBuffer.ResetOffset();
    m_materialConstantBuffer.ResetOffset();

    // Ensure outstanding uploads are complete before reusing upload allocator
    if (m_uploadQueue) {
        for (uint64_t fence : m_uploadFences) {
            if (fence != 0 && !m_uploadQueue->IsFenceComplete(fence)) {
                m_uploadQueue->WaitForFenceValue(fence);
            }
        }
    }
    std::fill(m_uploadFences.begin(), m_uploadFences.end(), 0);
    m_pendingUploadFence = 0;
    for (uint32_t i = 0; i < kUploadPoolSize; ++i) {
        if (m_uploadCommandAllocators[i]) {
            m_uploadCommandAllocators[i]->Reset();
        }
        if (m_uploadCommandLists[i]) {
            m_uploadCommandLists[i]->Reset(m_uploadCommandAllocators[i].Get(), nullptr);
            m_uploadCommandLists[i]->Close();
        }
    }

    // Increment the absolute frame index. This is used for tracking BLAS build
    // timing to ensure scratch buffers aren't released while the GPU is still
    // using them.
    ++m_absoluteFrameIndex;

    // Now that the previous frame's GPU work is complete, release any BLAS
    // scratch buffers that were used for acceleration structure builds.
    // With triple buffering, when we've waited for m_fenceValues[m_frameIndex],
    // frame (m_absoluteFrameIndex - kFrameCount) is guaranteed complete.
    // We subtract kFrameCount to be safe: if we're at frame N, frames < N-2
    // have definitely finished.
    if (m_rayTracingContext) {
        uint64_t completedFrame = (m_absoluteFrameIndex > kFrameCount)
            ? (m_absoluteFrameIndex - kFrameCount)
            : 0;
        m_rayTracingContext->ReleaseScratchBuffers(completedFrame);
    }

    // Reset command allocator and list.
    // If the command list is already open (e.g., after ResetCommandList during scene switch),
    // we need to close it first before resetting the allocator.
    if (m_commandListOpen) {
        m_commandList->Close();
        m_commandListOpen = false;
    }
    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr);
    m_commandListOpen = true;
}

void Renderer::RenderParticles(Scene::ECS_Registry* registry) {
    if (m_deviceRemoved || !registry || !m_particlePipeline || !m_hdrColor || m_particleBufferMapFailed) {
        return;
    }

    // Cap the number of particles we draw in a single frame to keep the
    // per-frame instance buffer small and avoid pathological memory usage if
    // an emitter accidentally spawns an excessive number of particles.
    static constexpr uint32_t kMaxParticleInstances = 4096;

    auto view = registry->View<Scene::ParticleEmitterComponent, Scene::TransformComponent>();
    if (view.begin() == view.end()) {
        return;
    }

    std::vector<ParticleInstance> instances;
    instances.reserve(1024);

    for (auto entity : view) {
        auto& emitter   = view.get<Scene::ParticleEmitterComponent>(entity);
        auto& transform = view.get<Scene::TransformComponent>(entity);

        glm::vec3 emitterWorldPos = glm::vec3(transform.worldMatrix[3]);

        for (const auto& p : emitter.particles) {
            if (p.age >= p.lifetime) {
                continue;
            }

            if (instances.size() >= kMaxParticleInstances) {
                break;
            }

            ParticleInstance inst{};
            inst.position = emitter.localSpace ? (emitterWorldPos + p.position) : p.position;
            inst.size     = p.size;
            inst.color    = p.color;
            instances.push_back(inst);
        }

        if (instances.size() >= kMaxParticleInstances) {
            break;
        }
    }

    if (instances.empty()) {
        return;
    }

    ID3D12Device* device = m_device->GetDevice();
    if (!device) {
        return;
    }

    const UINT instanceCount = static_cast<UINT>(instances.size());
    const UINT requiredCapacity = instanceCount;
    const UINT minCapacity = 256;

    if (!m_particleInstanceBuffer || m_particleInstanceCapacity < requiredCapacity) {
        // CRITICAL: If replacing an existing buffer, wait for GPU to finish using it
        if (m_particleInstanceBuffer) {
            WaitForGPU();
        }

        UINT newCapacity = std::max(requiredCapacity, minCapacity);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<UINT64>(newCapacity) * sizeof(ParticleInstance);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> buffer;
        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&buffer));

        if (FAILED(hr)) {
            spdlog::warn("RenderParticles: failed to allocate instance buffer");
            return;
        }

        m_particleInstanceBuffer = buffer;
        m_particleInstanceCapacity = newCapacity;
    }

    // Upload instance data
    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    const UINT bufferSize = instanceCount * sizeof(ParticleInstance);
    HRESULT mapHr = m_particleInstanceBuffer->Map(0, &readRange, &mapped);
    if (SUCCEEDED(mapHr)) {
        memcpy(mapped, instances.data(), bufferSize);
        m_particleInstanceBuffer->Unmap(0, nullptr);
    } else {
        spdlog::warn("RenderParticles: failed to map instance buffer (hr=0x{:08X}); disabling particles for this run",
                     static_cast<unsigned int>(mapHr));
        // Map failures are one of the first places a hung device surfaces.
        // Capture rich diagnostics so we can see which pass/frame triggered
        // device removal.
        CORTEX_REPORT_DEVICE_REMOVED("RenderParticles_MapInstanceBuffer", mapHr);
        m_particleBufferMapFailed = true;
        return;
    }

    // Persistent quad vertex buffer in an upload heap; tiny and self-contained.
    struct QuadVertex { float px, py, pz; float u, v; };
    static const QuadVertex kQuadVertices[4] = {
        { -0.5f, -0.5f, 0.0f, 0.0f, 1.0f },
        { -0.5f,  0.5f, 0.0f, 0.0f, 0.0f },
        {  0.5f, -0.5f, 0.0f, 1.0f, 1.0f },
        {  0.5f,  0.5f, 0.0f, 1.0f, 0.0f },
    };

    if (!m_particleQuadVertexBuffer) {
        D3D12_HEAP_PROPERTIES quadHeapProps = {};
        quadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        quadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        quadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        quadHeapProps.CreationNodeMask = 1;
        quadHeapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC vbDesc = {};
        vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vbDesc.Width = sizeof(kQuadVertices);
        vbDesc.Height = 1;
        vbDesc.DepthOrArraySize = 1;
        vbDesc.MipLevels = 1;
        vbDesc.Format = DXGI_FORMAT_UNKNOWN;
        vbDesc.SampleDesc.Count = 1;
        vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        vbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hrVB = device->CreateCommittedResource(
            &quadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_particleQuadVertexBuffer));
        if (FAILED(hrVB)) {
            spdlog::warn("RenderParticles: failed to allocate quad vertex buffer (hr=0x{:08X})",
                         static_cast<unsigned int>(hrVB));
            CORTEX_REPORT_DEVICE_REMOVED("RenderParticles_CreateQuadVB", hrVB);
            return;
        }

        void* quadMapped = nullptr;
        HRESULT mapQuadHr = m_particleQuadVertexBuffer->Map(0, &readRange, &quadMapped);
        if (SUCCEEDED(mapQuadHr)) {
            memcpy(quadMapped, kQuadVertices, sizeof(kQuadVertices));
            m_particleQuadVertexBuffer->Unmap(0, nullptr);
        } else {
            spdlog::warn("RenderParticles: failed to map quad vertex buffer (hr=0x{:08X})",
                         static_cast<unsigned int>(mapQuadHr));
            CORTEX_REPORT_DEVICE_REMOVED("RenderParticles_MapQuadVB", mapQuadHr);
            m_particleQuadVertexBuffer.Reset();
            return;
        }
    }

    // --- FIX: Bind render targets with depth buffer BEFORE setting pipeline ---
    // The particle pipeline expects DXGI_FORMAT_D32_FLOAT depth, so we MUST bind the DSV

    // 1. Transition depth buffer to write state if needed
    // 2. NEW FIX: Transition HDR Color to RENDER_TARGET (may be in PIXEL_SHADER_RESOURCE from previous pass)
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    uint32_t barrierCount = 0;

    if (m_depthBuffer && m_depthState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_depthBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_depthState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    if (m_hdrColor && m_hdrState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_hdrColor.Get();
        barriers[barrierCount].Transition.StateBefore = m_hdrState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (barrierCount > 0) {
        m_commandList->ResourceBarrier(barrierCount, barriers);
    }

    // 3. Bind render targets (HDR color + depth)
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_hdrRTV.cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_depthStencilView.cpu;
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_particlePipeline->GetPipelineState());

    if (m_shadowAndEnvDescriptors[0].IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowAndEnvDescriptors[0].gpu);
    }

    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    ObjectConstants obj{};
    obj.modelMatrix  = glm::mat4(1.0f);
    obj.normalMatrix = glm::mat4(1.0f);
    auto objAddr = m_objectConstantBuffer.AllocateAndWrite(obj);
    m_commandList->SetGraphicsRootConstantBufferView(0, objAddr);

    D3D12_VERTEX_BUFFER_VIEW vbViews[2] = {};
    vbViews[0].BufferLocation = m_particleQuadVertexBuffer->GetGPUVirtualAddress();
    vbViews[0].StrideInBytes  = sizeof(QuadVertex);
    vbViews[0].SizeInBytes    = sizeof(kQuadVertices);

    vbViews[1].BufferLocation = m_particleInstanceBuffer->GetGPUVirtualAddress();
    vbViews[1].StrideInBytes  = sizeof(ParticleInstance);
    vbViews[1].SizeInBytes    = bufferSize;

    m_commandList->IASetVertexBuffers(0, 2, vbViews);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    m_commandList->DrawInstanced(4, instanceCount, 0, 0);
}

void Renderer::PrepareMainPass() {
    // Main pass renders into HDR + normal/roughness G-buffer when available,
    // otherwise directly to back buffer.
    D3D12_CPU_DESCRIPTOR_HANDLE rtvs[2] = {};
    UINT numRtvs = 0;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_depthStencilView.cpu;

    // Ensure depth buffer is in writable state for the main pass
    if (m_depthBuffer && m_depthState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER depthBarrier = {};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = m_depthBuffer.Get();
        depthBarrier.Transition.StateBefore = m_depthState;
        depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &depthBarrier);
        m_depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    // If the ray-traced shadow mask exists and was written by the DXR pass,
    // transition it to a shader-resource state so the PBR shader can sample it.
    if (m_rtShadowMask && m_rtShadowMaskState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER rtMaskBarrier{};
        rtMaskBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        rtMaskBarrier.Transition.pResource = m_rtShadowMask.Get();
        rtMaskBarrier.Transition.StateBefore = m_rtShadowMaskState;
        rtMaskBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        rtMaskBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &rtMaskBarrier);
        m_rtShadowMaskState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Likewise, if the RT diffuse GI buffer was written by the DXR pass,
    // transition it to a shader-resource state before sampling in the PBR
    // shader.
        if (m_rtGIColor && m_rtGIState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER giBarrier{};
        giBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        giBarrier.Transition.pResource = m_rtGIColor.Get();
        giBarrier.Transition.StateBefore = m_rtGIState;
        giBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        giBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &giBarrier);
        m_rtGIState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (m_hdrColor) {
        // Ensure HDR is in render target state
        if (m_hdrState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            D3D12_RESOURCE_BARRIER barrier = {};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_hdrColor.Get();
            barrier.Transition.StateBefore = m_hdrState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandList->ResourceBarrier(1, &barrier);
            m_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
        rtvs[numRtvs++] = m_hdrRTV.cpu;

        // Ensure G-buffer is in render target state
        if (m_gbufferNormalRoughness && m_gbufferNormalRoughnessState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
            D3D12_RESOURCE_BARRIER gbufBarrier = {};
            gbufBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            gbufBarrier.Transition.pResource = m_gbufferNormalRoughness.Get();
            gbufBarrier.Transition.StateBefore = m_gbufferNormalRoughnessState;
            gbufBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
            gbufBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandList->ResourceBarrier(1, &gbufBarrier);
            m_gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }
        if (m_gbufferNormalRoughness) {
            rtvs[numRtvs++] = m_gbufferNormalRoughnessRTV.cpu;
        }
    } else {
        // Fallback: render directly to back buffer
        ID3D12Resource* backBuffer = m_window->GetCurrentBackBuffer();
        if (!backBuffer) {
            spdlog::error("PrepareMainPass: back buffer is null; skipping frame");
            return;
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = backBuffer;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);
        m_backBufferUsedAsRTThisFrame = true;
        rtvs[numRtvs++] = m_window->GetCurrentRTV();
    }

    m_commandList->OMSetRenderTargets(numRtvs, rtvs, FALSE, &dsv);

    // Clear render targets and depth buffer
    const float clearColor[] = { 0.1f, 0.1f, 0.15f, 1.0f };  // Dark blue
    for (UINT i = 0; i < numRtvs; ++i) {
        m_commandList->ClearRenderTargetView(rtvs[i], clearColor, 0, nullptr);
    }
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Set viewport and scissor to match the internal render resolution when
    // using HDR (which may be supersampled relative to the window).
    D3D12_VIEWPORT viewport = {};
    D3D12_RECT scissorRect = {};
    if (m_hdrColor) {
        D3D12_RESOURCE_DESC hdrDesc = m_hdrColor->GetDesc();
        viewport.Width  = static_cast<float>(hdrDesc.Width);
        viewport.Height = static_cast<float>(hdrDesc.Height);
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right  = static_cast<LONG>(hdrDesc.Width);
        scissorRect.bottom = static_cast<LONG>(hdrDesc.Height);
    } else {
        viewport.Width  = static_cast<float>(m_window->GetWidth());
        viewport.Height = static_cast<float>(m_window->GetHeight());
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        scissorRect.left = 0;
        scissorRect.top = 0;
        scissorRect.right  = static_cast<LONG>(m_window->GetWidth());
        scissorRect.bottom = static_cast<LONG>(m_window->GetHeight());
    }

    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissorRect);

    // Set pipeline state and root signature
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_pipeline->GetPipelineState());

    // Bind descriptor heap
    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    // Set primitive topology
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
}

void Renderer::EndFrame() {
    // Mark the start of end-of-frame work (RT history copies, back-buffer
    // transition, present) so device-removed diagnostics can distinguish
    // hangs that occur after all main passes have finished.
    WriteBreadcrumb(GpuMarker::EndFrame);

    // Before presenting, update the RT shadow history buffer so the next
    // frame's temporal smoothing has valid data.
    if (m_rayTracingSupported && m_rayTracingEnabled && m_rtShadowMask && m_rtShadowMaskHistory) {
        D3D12_RESOURCE_BARRIER barriers[2] = {};
        UINT barrierCount = 0;

        if (m_rtShadowMaskState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_rtShadowMask.Get();
            barriers[barrierCount].Transition.StateBefore = m_rtShadowMaskState;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
            m_rtShadowMaskState = D3D12_RESOURCE_STATE_COPY_SOURCE;
        }

        if (m_rtShadowMaskHistoryState != D3D12_RESOURCE_STATE_COPY_DEST) {
            barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCount].Transition.pResource = m_rtShadowMaskHistory.Get();
            barriers[barrierCount].Transition.StateBefore = m_rtShadowMaskHistoryState;
            barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCount;
            m_rtShadowMaskHistoryState = D3D12_RESOURCE_STATE_COPY_DEST;
        }

        if (barrierCount > 0) {
            m_commandList->ResourceBarrier(barrierCount, barriers);
        }

        m_commandList->CopyResource(m_rtShadowMaskHistory.Get(), m_rtShadowMask.Get());

        // Return both resources to shader-resource state for the next frame.
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = m_rtShadowMask.Get();
        barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = m_rtShadowMaskHistory.Get();
        barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_commandList->ResourceBarrier(2, barriers);

        m_rtShadowMaskState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          m_rtShadowMaskHistoryState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          m_rtHasHistory = true;
      }

      // Update RT GI history buffer in lock-step with the RT GI color buffer
      // so temporal accumulation in the shader has a stable previous frame.
      if (m_rayTracingSupported && m_rayTracingEnabled && m_rtGIColor && m_rtGIHistory) {
          D3D12_RESOURCE_BARRIER giBarriers[2] = {};
          UINT giBarrierCount = 0;

          if (m_rtGIState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
              giBarriers[giBarrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
              giBarriers[giBarrierCount].Transition.pResource = m_rtGIColor.Get();
              giBarriers[giBarrierCount].Transition.StateBefore = m_rtGIState;
              giBarriers[giBarrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
              giBarriers[giBarrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
              ++giBarrierCount;
              m_rtGIState = D3D12_RESOURCE_STATE_COPY_SOURCE;
          }

          if (m_rtGIHistoryState != D3D12_RESOURCE_STATE_COPY_DEST) {
              giBarriers[giBarrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
              giBarriers[giBarrierCount].Transition.pResource = m_rtGIHistory.Get();
              giBarriers[giBarrierCount].Transition.StateBefore = m_rtGIHistoryState;
              giBarriers[giBarrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
              giBarriers[giBarrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
              ++giBarrierCount;
              m_rtGIHistoryState = D3D12_RESOURCE_STATE_COPY_DEST;
          }

          if (giBarrierCount > 0) {
              m_commandList->ResourceBarrier(giBarrierCount, giBarriers);
          }

          m_commandList->CopyResource(m_rtGIHistory.Get(), m_rtGIColor.Get());

          giBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          giBarriers[0].Transition.pResource = m_rtGIColor.Get();
          giBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
          giBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          giBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

          giBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          giBarriers[1].Transition.pResource = m_rtGIHistory.Get();
          giBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
          giBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          giBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

          m_commandList->ResourceBarrier(2, giBarriers);

          m_rtGIState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          m_rtGIHistoryState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          m_rtGIHasHistory = true;
      }

      // Update RT reflection history after the DXR reflections pass has
      // populated the current RT reflection color buffer. This mirrors the
      // shadow / GI history updates above so the post-process shader can
      // blend against the previous frame when g_DebugMode.w indicates that
      // RT history is valid. If no reflection rays were traced this frame,
      // skip the copy so we do not treat uninitialized data as valid history.
      if (m_rayTracingSupported && m_rayTracingEnabled && m_rtReflectionWrittenThisFrame && m_rtReflectionColor && m_rtReflectionHistory) {
          D3D12_RESOURCE_BARRIER reflBarriers[2] = {};
          UINT reflBarrierCount = 0;

          if (m_rtReflectionState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
              reflBarriers[reflBarrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
              reflBarriers[reflBarrierCount].Transition.pResource = m_rtReflectionColor.Get();
              reflBarriers[reflBarrierCount].Transition.StateBefore = m_rtReflectionState;
              reflBarriers[reflBarrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
              reflBarriers[reflBarrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
              ++reflBarrierCount;
              m_rtReflectionState = D3D12_RESOURCE_STATE_COPY_SOURCE;
          }

          if (m_rtReflectionHistoryState != D3D12_RESOURCE_STATE_COPY_DEST) {
              reflBarriers[reflBarrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
              reflBarriers[reflBarrierCount].Transition.pResource = m_rtReflectionHistory.Get();
              reflBarriers[reflBarrierCount].Transition.StateBefore = m_rtReflectionHistoryState;
              reflBarriers[reflBarrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
              reflBarriers[reflBarrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
              ++reflBarrierCount;
              m_rtReflectionHistoryState = D3D12_RESOURCE_STATE_COPY_DEST;
          }

          if (reflBarrierCount > 0) {
              m_commandList->ResourceBarrier(reflBarrierCount, reflBarriers);
          }

          m_commandList->CopyResource(m_rtReflectionHistory.Get(), m_rtReflectionColor.Get());

          reflBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          reflBarriers[0].Transition.pResource = m_rtReflectionColor.Get();
          reflBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
          reflBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          reflBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

          reflBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
          reflBarriers[1].Transition.pResource = m_rtReflectionHistory.Get();
          reflBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
          reflBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          reflBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

          m_commandList->ResourceBarrier(2, reflBarriers);

          m_rtReflectionState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          m_rtReflectionHistoryState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
          m_rtReflHasHistory = true;
      }

    // Ensure screen-space/post-process inputs are back in a shader-resource
    // state by the end of the frame so future passes (or diagnostics) never
    // observe them left in RENDER_TARGET / UNORDERED_ACCESS when Present is
    // called, even if the main post-process resolve was skipped.
    {
        D3D12_RESOURCE_BARRIER ppBarriers[8] = {};
        UINT ppCount = 0;

        if (m_ssaoTex && m_ssaoState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            ppBarriers[ppCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ppBarriers[ppCount].Transition.pResource = m_ssaoTex.Get();
            ppBarriers[ppCount].Transition.StateBefore = m_ssaoState;
            ppBarriers[ppCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ppBarriers[ppCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++ppCount;
            m_ssaoState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (m_ssrColor && m_ssrState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            ppBarriers[ppCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ppBarriers[ppCount].Transition.pResource = m_ssrColor.Get();
            ppBarriers[ppCount].Transition.StateBefore = m_ssrState;
            ppBarriers[ppCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ppBarriers[ppCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++ppCount;
            m_ssrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (m_velocityBuffer && m_velocityState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            ppBarriers[ppCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ppBarriers[ppCount].Transition.pResource = m_velocityBuffer.Get();
            ppBarriers[ppCount].Transition.StateBefore = m_velocityState;
            ppBarriers[ppCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ppBarriers[ppCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++ppCount;
            m_velocityState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (m_taaIntermediate && m_taaIntermediateState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            ppBarriers[ppCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ppBarriers[ppCount].Transition.pResource = m_taaIntermediate.Get();
            ppBarriers[ppCount].Transition.StateBefore = m_taaIntermediateState;
            ppBarriers[ppCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ppBarriers[ppCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++ppCount;
            m_taaIntermediateState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (m_rtReflectionColor && m_rtReflectionState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            ppBarriers[ppCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ppBarriers[ppCount].Transition.pResource = m_rtReflectionColor.Get();
            ppBarriers[ppCount].Transition.StateBefore = m_rtReflectionState;
            ppBarriers[ppCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ppBarriers[ppCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++ppCount;
            m_rtReflectionState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (m_gbufferNormalRoughness && m_gbufferNormalRoughnessState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            ppBarriers[ppCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            ppBarriers[ppCount].Transition.pResource = m_gbufferNormalRoughness.Get();
            ppBarriers[ppCount].Transition.StateBefore = m_gbufferNormalRoughnessState;
            ppBarriers[ppCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            ppBarriers[ppCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++ppCount;
            m_gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }

        if (ppCount > 0) {
            m_commandList->ResourceBarrier(ppCount, ppBarriers);
        }
    }

    // Transition back buffer to present state if it was used as a render
    // target this frame. When post-process or voxel paths are disabled, the
    // swap-chain buffer may remain in PRESENT state for the entire frame.
    if (m_backBufferUsedAsRTThisFrame) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_window->GetCurrentBackBuffer();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);
    }

    // Close and execute command list
    m_commandList->Close();
    m_commandListOpen = false;
    m_commandQueue->ExecuteCommandList(m_commandList.Get());

    // Present
    m_window->Present();

    // Surface device-removed errors as close to present as possible. This
    // helps isolate hangs that occur in swap-chain or late-frame work.
    if (m_device && m_device->GetDevice()) {
        HRESULT reason = m_device->GetDevice()->GetDeviceRemovedReason();
        if (reason != S_OK) {
            CORTEX_REPORT_DEVICE_REMOVED("EndFrame_Present", reason);
            return;
        }
    }

    // Signal fence for this frame
    m_fenceValues[m_frameIndex] = m_commandQueue->Signal();
}

void Renderer::UpdateFrameConstants(float deltaTime, Scene::ECS_Registry* registry) {
    FrameConstants frameData = {};
    glm::vec3 cameraPos(0.0f);
    glm::vec3 cameraForward(0.0f, 0.0f, 1.0f);
    float camNear = 0.1f;
    float camFar = 1000.0f;
    float fovY = glm::radians(60.0f);

    // Reset per-frame local light shadow state; will be populated below if we
    // find suitable shadow-casting spotlights. We keep the budget-warning
    // flag sticky so we do not spam logs every frame.
    m_hasLocalShadow = false;
    m_localShadowCount = 0;

    // Find active camera
    auto cameraView = registry->View<Scene::CameraComponent, Scene::TransformComponent>();
    bool foundCamera = false;

    for (auto entity : cameraView) {
        auto& camera = cameraView.get<Scene::CameraComponent>(entity);
        auto& transform = cameraView.get<Scene::TransformComponent>(entity);

        if (camera.isActive) {
            // Respect camera orientation from its transform
            frameData.viewMatrix = camera.GetViewMatrix(transform);
            frameData.projectionMatrix = camera.GetProjectionMatrix(m_window->GetAspectRatio());
            cameraPos = transform.position;
            cameraForward = glm::normalize(transform.rotation * glm::vec3(0.0f, 0.0f, 1.0f));
            frameData.cameraPosition = glm::vec4(cameraPos, 1.0f);
            camNear = camera.nearPlane;
            camFar = camera.farPlane;
            fovY = glm::radians(camera.fov);
            foundCamera = true;
            // Active camera found; skip per-frame debug spam to keep logs clean
            break;
        }
    }

    // Default camera if none found
    if (!foundCamera) {
        spdlog::warn("No active camera found, using default");
        cameraPos = glm::vec3(0.0f, 2.0f, 5.0f);
        glm::vec3 target(0.0f, 0.0f, 0.0f);
        glm::vec3 up(0.0f, 1.0f, 0.0f);

        frameData.viewMatrix = glm::lookAtLH(cameraPos, target, up);
        frameData.projectionMatrix = glm::perspectiveLH_ZO(
            fovY,
            m_window->GetAspectRatio(),
            camNear,
            camFar
        );
        cameraForward = glm::normalize(target - cameraPos);
        frameData.cameraPosition = glm::vec4(cameraPos, 1.0f);
    }

    // Cache camera parameters for culling and RT use.
    m_cameraPositionWS = cameraPos;
    m_cameraForwardWS  = cameraForward;
    m_cameraNearPlane  = camNear;
    m_cameraFarPlane   = camFar;
    if (m_rayTracingContext) {
        m_rayTracingContext->SetCameraParams(cameraPos, cameraForward, camNear, camFar);
    }

    // Temporal AA jitter (in pixels) and corresponding UV delta for history
    // sampling. When an internal supersampling scale is active, base these
    // values on the HDR render target size rather than the window size so
    // jitter and post-process texel steps line up with the actual buffers.
    float internalWidth  = static_cast<float>(m_window->GetWidth());
    float internalHeight = static_cast<float>(m_window->GetHeight());
    if (m_hdrColor) {
        D3D12_RESOURCE_DESC hdrDesc = m_hdrColor->GetDesc();
        internalWidth  = static_cast<float>(hdrDesc.Width);
        internalHeight = static_cast<float>(hdrDesc.Height);
    }
    float invWidth  = 1.0f / std::max(1.0f, internalWidth);
    float invHeight = 1.0f / std::max(1.0f, internalHeight);

    glm::vec2 jitterPixels(0.0f);
    if (m_taaEnabled) {
        m_taaJitterPrevPixels = m_taaJitterCurrPixels;
        float jx = Halton(m_taaSampleIndex + 1, 2) - 0.5f;
        float jy = Halton(m_taaSampleIndex + 1, 3) - 0.5f;
        m_taaSampleIndex++;
        // Scale jitter so per-frame shifts are small and objects remain
        // stable while still providing enough subpixel coverage for TAA.
        float jitterScale = 0.15f;
        if (!m_cameraIsMoving) {
            // When the camera is effectively stationary, disable jitter so
            // the image converges to a sharp, stable result without
            // "double-exposed" edges.
            jitterScale = 0.0f;
        }
        jitterPixels = glm::vec2(jx, jy) * jitterScale;
        m_taaJitterCurrPixels = jitterPixels;
    } else {
        m_taaJitterPrevPixels = glm::vec2(0.0f);
        m_taaJitterCurrPixels = glm::vec2(0.0f);
    }

    // Compute a non-jittered view-projection matrix for RT reconstruction and
    // motion vector generation before applying TAA offsets. This keeps RT
    // rays and motion vectors stable while the raster path still benefits
    // from jitter.
    glm::mat4 vpNoJitter = frameData.projectionMatrix * frameData.viewMatrix;
    frameData.viewProjectionNoJitter = vpNoJitter;
    frameData.invViewProjectionNoJitter = glm::inverse(vpNoJitter);

    // Apply jitter to projection (NDC space).
    if (m_taaEnabled) {
        float jitterNdcX = (2.0f * jitterPixels.x) * invWidth;
        float jitterNdcY = (2.0f * jitterPixels.y) * invHeight;
        // Offset projection center; DirectX-style clip space uses [x,y] in row 2, column 0/1.
        frameData.projectionMatrix[2][0] += jitterNdcX;
        frameData.projectionMatrix[2][1] += jitterNdcY;
    }

    // Final view-projection with jitter applied.
    frameData.viewProjectionMatrix = frameData.projectionMatrix * frameData.viewMatrix;

    // Precompute inverse projection for SSAO and other screen-space effects.
    frameData.invProjectionMatrix = glm::inverse(frameData.projectionMatrix);

    // Time/exposure and lighting state (w = bloom intensity, disabled if bloom SRV missing)
    float bloom = (m_bloomCombinedSRV.IsValid() ? m_bloomIntensity : 0.0f);
    frameData.timeAndExposure = glm::vec4(m_totalTime, deltaTime, m_exposure, bloom);

    glm::vec3 ambient = m_ambientLightColor * m_ambientLightIntensity;
    frameData.ambientColor = glm::vec4(ambient, 0.0f);

    // Fill forward light array (light 0 = directional sun)
    glm::vec3 dirToLight = glm::normalize(m_directionalLightDirection);
    glm::vec3 sunColor = m_directionalLightColor * m_directionalLightIntensity;

    uint32_t lightCount = 0;

    // Track up to kMaxShadowedLocalLights shadow-casting spotlights. Each one
    // gets its own slice in the shared shadow-map atlas and a matching entry
    // in the lightViewProjection array for shading.
    glm::vec3 localLightPos[kMaxShadowedLocalLights]{};
    glm::vec3 localLightDir[kMaxShadowedLocalLights]{};
    float     localLightRange[kMaxShadowedLocalLights]{};
    float     localOuterDegrees[kMaxShadowedLocalLights]{};

    // Light 0: directional sun (unshadowed here; shadows are handled via cascades)
    frameData.lightCount = glm::uvec4(0u);
    frameData.lights[0].position_type = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // type 0 = directional
    frameData.lights[0].direction_cosInner = glm::vec4(dirToLight, 0.0f);
    frameData.lights[0].color_range = glm::vec4(sunColor, 0.0f);
    frameData.lights[0].params = glm::vec4(0.0f);
    lightCount = 1;

    // Populate additional lights from LightComponent (point/spot). We support
    // up to kMaxForwardLights-1 additional lights beyond the sun.
    auto lightView = registry->View<Scene::LightComponent, Scene::TransformComponent>();
    for (auto entity : lightView) {
        if (lightCount >= kMaxForwardLights) {
            break;
        }
        auto& lightComp = lightView.get<Scene::LightComponent>(entity);
        auto& lightXform = lightView.get<Scene::TransformComponent>(entity);

        auto type = lightComp.type;
        if (type == Scene::LightType::Directional) {
            // Directional lights are handled by the global sun for now
            continue;
        }

        glm::vec3 color = glm::max(lightComp.color, glm::vec3(0.0f));
        float intensity = std::max(lightComp.intensity, 0.0f);
        glm::vec3 radiance = color * intensity;

        Light& outLight = frameData.lights[lightCount];
        float gpuType = 1.0f;
        if (type == Scene::LightType::Point) {
            gpuType = 1.0f;
        } else if (type == Scene::LightType::Spot) {
            gpuType = 2.0f;
        } else if (type == Scene::LightType::AreaRect) {
            gpuType = 3.0f;
        }
        outLight.position_type = glm::vec4(lightXform.position, gpuType);

        glm::vec3 forwardLS = lightXform.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 dir = glm::normalize(forwardLS);
        float innerRad = glm::radians(lightComp.innerConeDegrees);
        float outerRad = glm::radians(lightComp.outerConeDegrees);
        float cosInner = std::cos(innerRad);
        float cosOuter = std::cos(outerRad);

        outLight.direction_cosInner = glm::vec4(dir, cosInner);
        outLight.color_range = glm::vec4(radiance, lightComp.range);

        // Default to "no local shadow" for this light. We reserve params.y as
        // a shadow-map slice index when using local light shadows.
        float shadowIndex = -1.0f;

        if (m_shadowsEnabled &&
            lightComp.castsShadows &&
            type == Scene::LightType::Spot)
        {
            if (m_localShadowCount < kMaxShadowedLocalLights) {
                uint32_t localIndex = m_localShadowCount;
                uint32_t slice = kShadowCascadeCount + localIndex;

                shadowIndex = static_cast<float>(slice);
                localLightPos[localIndex] = lightXform.position;
                localLightDir[localIndex] = dir;
                localLightRange[localIndex] = lightComp.range;
                localOuterDegrees[localIndex] = lightComp.outerConeDegrees;

                ++m_localShadowCount;
            } else if (!m_localShadowBudgetWarningEmitted) {
                std::string nameUtf8 = "<unnamed>";
                if (registry && registry->HasComponent<Scene::TagComponent>(entity)) {
                    const auto& tag = registry->GetComponent<Scene::TagComponent>(entity).tag;
                    if (!tag.empty()) {
                        nameUtf8 = tag;
                    }
                }
                spdlog::warn(
                    "Local shadow budget exceeded ({} lights); '{}' will render without local shadows. "
                    "Consider disabling 'castsShadows' on some lights or enabling safe lighting rigs.",
                    m_localShadowCount,
                    nameUtf8);
                m_localShadowBudgetWarningEmitted = true;
            }
        }

        // For rect area lights we encode the half-size in params.zw so that
        // the shader can approximate their footprint. Other light types
        // leave these components at zero.
        glm::vec2 areaHalfSize(0.0f);
        if (type == Scene::LightType::AreaRect) {
            areaHalfSize = 0.5f * glm::max(lightComp.areaSize, glm::vec2(0.0f));
        }

        outLight.params = glm::vec4(cosOuter, shadowIndex, areaHalfSize.x, areaHalfSize.y);

        ++lightCount;
    }

    // Zero any remaining lights
    for (uint32_t i = lightCount; i < kMaxForwardLights; ++i) {
        frameData.lights[i].position_type = glm::vec4(0.0f);
        frameData.lights[i].direction_cosInner = glm::vec4(0.0f);
        frameData.lights[i].color_range = glm::vec4(0.0f);
        frameData.lights[i].params = glm::vec4(0.0f);
    }

    frameData.lightCount = glm::uvec4(lightCount, 0u, 0u, 0u);

    // Camera-followed light view for cascades
    glm::vec3 sceneCenter = cameraPos + cameraForward * ((camNear + camFar) * 0.5f);
    glm::vec3 lightDirFromLightToScene = -dirToLight;
    float lightDistance = camFar;
    glm::vec3 lightPos = sceneCenter - lightDirFromLightToScene * lightDistance;

    glm::vec3 lightUp(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(lightUp, lightDirFromLightToScene)) > 0.99f) {
        lightUp = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    m_lightViewMatrix = glm::lookAtLH(lightPos, sceneCenter, lightUp);

    // Compute cascade splits (practical split scheme)
    const uint32_t cascadeCount = kShadowCascadeCount;
    float splits[kShadowCascadeCount] = {};
    for (uint32_t i = 0; i < cascadeCount; ++i) {
        float si = static_cast<float>(i + 1) / static_cast<float>(cascadeCount);
        float logSplit = camNear * std::pow(camFar / camNear, si);
        float linSplit = camNear + (camFar - camNear) * si;
        splits[i] = m_cascadeSplitLambda * logSplit + (1.0f - m_cascadeSplitLambda) * linSplit;
        m_cascadeSplits[i] = splits[i];
    }

    frameData.cascadeSplits = glm::vec4(
        splits[0],
        splits[1],
        splits[2],
        camFar
    );

    // Build per-cascade light view-projection matrices
    const float aspect = m_window->GetAspectRatio();
    const float tanHalfFovY = std::tan(fovY * 0.5f);
    const float tanHalfFovX = tanHalfFovY * aspect;
    glm::mat4 invView = glm::inverse(frameData.viewMatrix);

    for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex) {
        float cascadeNear = (cascadeIndex == 0) ? camNear : splits[cascadeIndex - 1];
        float cascadeFar = splits[cascadeIndex];

        float xn = cascadeNear * tanHalfFovX;
        float yn = cascadeNear * tanHalfFovY;
        float xf = cascadeFar * tanHalfFovX;
        float yf = cascadeFar * tanHalfFovY;

        glm::vec3 frustumCornersVS[8] = {
            { -xn,  yn, cascadeNear },
            {  xn,  yn, cascadeNear },
            {  xn, -yn, cascadeNear },
            { -xn, -yn, cascadeNear },
            { -xf,  yf, cascadeFar },
            {  xf,  yf, cascadeFar },
            {  xf, -yf, cascadeFar },
            { -xf, -yf, cascadeFar }
        };

        glm::vec3 minLS( std::numeric_limits<float>::max());
        glm::vec3 maxLS(-std::numeric_limits<float>::max());

        for (auto& cornerVS : frustumCornersVS) {
            glm::vec4 world = invView * glm::vec4(cornerVS, 1.0f);
            glm::vec3 ls = glm::vec3(m_lightViewMatrix * world);
            minLS = glm::min(minLS, ls);
            maxLS = glm::max(maxLS, ls);
        }

        glm::vec3 extent = (maxLS - minLS) * 0.5f;
        glm::vec3 centerLS = minLS + extent;

        // Slightly expand the light-space extents so large objects near the
        // camera frustum edges stay inside the shadow map, reducing edge flicker.
        extent.x *= 1.1f;
        extent.y *= 1.1f;

        // Texel snapping to reduce shimmering (per-cascade resolution scaling)
        float effectiveResX = m_shadowMapSize * m_cascadeResolutionScale[cascadeIndex];
        float effectiveResY = m_shadowMapSize * m_cascadeResolutionScale[cascadeIndex];
        float texelSizeX = (extent.x * 2.0f) / std::max(effectiveResX, 1.0f);
        float texelSizeY = (extent.y * 2.0f) / std::max(effectiveResY, 1.0f);
        if (texelSizeX > 0.0f) {
            centerLS.x = std::floor(centerLS.x / texelSizeX) * texelSizeX;
        }
        if (texelSizeY > 0.0f) {
            centerLS.y = std::floor(centerLS.y / texelSizeY) * texelSizeY;
        }

        float minX = centerLS.x - extent.x;
        float maxX = centerLS.x + extent.x;
        float minY = centerLS.y - extent.y;
        float maxY = centerLS.y + extent.y;

        float minZ = minLS.z;
        float maxZ = maxLS.z;
        float nearPlane = std::max(0.0f, minZ);
        float farPlane = maxZ;

        m_lightProjectionMatrices[cascadeIndex] = glm::orthoLH_ZO(minX, maxX, minY, maxY, nearPlane, farPlane);
        m_lightViewProjectionMatrices[cascadeIndex] = m_lightProjectionMatrices[cascadeIndex] * m_lightViewMatrix;
        frameData.lightViewProjection[cascadeIndex] = m_lightViewProjectionMatrices[cascadeIndex];
    }

    // Build spot-light shadow view-projection matrices for any selected local
    // lights and store them in the shared lightViewProjection array starting
    // at index kShadowCascadeCount.
    if (m_localShadowCount > 0)
    {
        m_hasLocalShadow = true;

        for (uint32_t i = 0; i < m_localShadowCount; ++i)
        {
            if (localLightRange[i] <= 0.0f)
            {
                continue;
            }

            glm::vec3 dir = glm::normalize(localLightDir[i]);
            if (!std::isfinite(dir.x) || !std::isfinite(dir.y) || !std::isfinite(dir.z) ||
                glm::length2(dir) < 1e-6f)
            {
                dir = glm::vec3(0.0f, -1.0f, 0.0f);
            }

            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(up, dir)) > 0.99f) {
                up = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            glm::mat4 lightView = glm::lookAtLH(localLightPos[i], localLightPos[i] + dir, up);

            float nearPlane = 0.1f;
            float farPlane = std::max(localLightRange[i], 1.0f);

            // Treat the outer cone angle as a half-FOV for the spotlight.
            float outerRad = glm::radians(localOuterDegrees[i]);
            float fovYLocal = outerRad * 2.0f;
            fovYLocal = glm::clamp(fovYLocal, glm::radians(10.0f), glm::radians(170.0f));

            glm::mat4 lightProj = glm::perspectiveLH_ZO(fovYLocal, 1.0f, nearPlane, farPlane);
            glm::mat4 lightViewProj = lightProj * lightView;

            m_localLightViewProjMatrices[i] = lightViewProj;

            uint32_t slice = kShadowCascadeCount + i;
            if (slice < kShadowArraySize)
            {
                frameData.lightViewProjection[slice] = lightViewProj;
            }
        }

        // Clear out any unused local shadow slots in the constant buffer.
        for (uint32_t i = m_localShadowCount; i < kMaxShadowedLocalLights; ++i)
        {
            uint32_t slice = kShadowCascadeCount + i;
            if (slice < kShadowArraySize)
            {
                frameData.lightViewProjection[slice] = glm::mat4(1.0f);
            }
        }
    }
    else
    {
        m_hasLocalShadow = false;
        for (uint32_t i = 0; i < kMaxShadowedLocalLights; ++i)
        {
            uint32_t slice = kShadowCascadeCount + i;
            if (slice < kShadowArraySize)
            {
                frameData.lightViewProjection[slice] = glm::mat4(1.0f);
            }
        }
    }

    frameData.shadowParams = glm::vec4(m_shadowBias, m_shadowPCFRadius, m_shadowsEnabled ? 1.0f : 0.0f, m_pcssEnabled ? 1.0f : 0.0f);

    float overlayFlag = m_debugOverlayVisible ? 1.0f : 0.0f;
    float selectedNorm = 0.0f;
    if (m_debugOverlayVisible) {
        // Normalize selected row (0..14) into 0..1 for the shader.
        selectedNorm = glm::clamp(static_cast<float>(m_debugOverlaySelectedRow) / 14.0f, 0.0f, 1.0f);
    }
    // debugMode.w is used as a coarse "RT history valid" flag across the
    // shading and post-process passes. Treat history as valid once any of
    // the RT pipelines (shadows, GI, reflections) has produced at least one
    // frame of data so temporal filtering can stabilize without requiring
    // every RT feature to be active at the same time.
    float rtHistoryValid =
        (m_rtHasHistory || m_rtGIHasHistory || m_rtReflHasHistory) ? 1.0f : 0.0f;
    frameData.debugMode = glm::vec4(
        static_cast<float>(m_debugViewMode),
        overlayFlag,
        selectedNorm,
        rtHistoryValid);

    // Post-process parameters: reciprocal resolution, FXAA flag, and an extra
    // channel used as a simple runtime toggle for ray-traced sun shadows in
    // the shading path (when DXR is available and the RT pipeline is valid).
    float fxaaFlag = (m_taaEnabled ? 0.0f : (m_fxaaEnabled ? 1.0f : 0.0f));
    bool rtPipelineReady =
        m_rayTracingSupported &&
        m_rayTracingEnabled &&
        m_rayTracingContext &&
        m_rayTracingContext->HasPipeline();
    // postParams.w represents "RT sun shadows enabled" per ShaderTypes.h line 102.
    // This flag gates the RT shadow mask sampling in Basic.hlsl (line 878).
    // RT shadows are always active when the RT pipeline is ready, unlike
    // reflections/GI which have separate feature toggles.
    float rtShadowsToggle = rtPipelineReady ? 1.0f : 0.0f;
    frameData.postParams = glm::vec4(invWidth, invHeight, fxaaFlag, rtShadowsToggle);

    // Image-based lighting parameters
    float iblEnabled = m_iblEnabled ? 1.0f : 0.0f;
    frameData.envParams = glm::vec4(
        m_iblDiffuseIntensity,
        m_iblSpecularIntensity,
        iblEnabled,
        static_cast<float>(m_currentEnvironment));

    // Color grading parameters (warm/cool) for post-process. We repurpose
    // colorGrade.z as a simple scalar for volumetric sun shafts so the
    // intensity of "god rays" can be tuned from the UI without adding a new
    // constant buffer field.
    frameData.colorGrade = glm::vec4(m_colorGradeWarm, m_colorGradeCool, m_godRayIntensity, 0.0f);

    // Exponential height fog parameters
    frameData.fogParams = glm::vec4(
        m_fogDensity,
        m_fogHeight,
        m_fogFalloff,
        m_fogEnabled ? 1.0f : 0.0f);

    // SSAO parameters packed into aoParams. Disable sampling if the SSAO
    // resources are unavailable so post-process does not read null SRVs.
    const bool ssaoResourcesReady = (m_ssaoTex && m_ssaoSRV.IsValid());
    frameData.aoParams = glm::vec4(
        (m_ssaoEnabled && ssaoResourcesReady) ? 1.0f : 0.0f,
        m_ssaoRadius,
        m_ssaoBias,
        m_ssaoIntensity);

    // Bloom shaping parameters. The w component is used as a small bitmask for
    // post-process feature toggles so the shader can safely gate optional
    // sampling without relying on other unrelated flags:
    //   bit0: SSR enabled
    //   bit1: RT reflections enabled
    //   bit2: RT reflection history valid
    uint32_t postFxFlags = 0u;
    if (m_ssrEnabled) {
        postFxFlags |= 1u;
    }
    if (rtPipelineReady && m_rtReflectionsEnabled) {
        postFxFlags |= 2u;
    }
    if (m_rtReflHasHistory) {
        postFxFlags |= 4u;
    }
    frameData.bloomParams = glm::vec4(
        m_bloomThreshold,
        m_bloomSoftKnee,
        m_bloomMaxContribution,
        static_cast<float>(postFxFlags));

    // TAA parameters: history UV offset from jitter delta and blend factor / enable flag.
    // Only enable TAA in the shader once we have a valid history buffer;
    // this avoids sampling uninitialized history and causing color flashes
    // on the first frame after startup or resize. When the camera is nearly
    // stationary we reduce jitter and blend strength to keep edges crisp and
    // minimize residual ghosting.
    glm::vec2 jitterDeltaPixels = m_taaJitterPrevPixels - m_taaJitterCurrPixels;
    glm::vec2 jitterDeltaUV = glm::vec2(jitterDeltaPixels.x * invWidth, jitterDeltaPixels.y * invHeight);
    const bool taaActiveThisFrame = m_taaEnabled && m_hasHistory;
    float blendForThisFrame = m_taaBlendFactor;
    if (!m_cameraIsMoving) {
        // When the camera is effectively stationary, reduce blend strength
        // so history converges but does not dominate the image.
        blendForThisFrame *= 0.5f;
    }
    frameData.taaParams = glm::vec4(
        jitterDeltaUV.x,
        jitterDeltaUV.y,
        blendForThisFrame,
        taaActiveThisFrame ? 1.0f : 0.0f);

    // Water parameters shared with shaders (see ShaderTypes.h / Basic.hlsl).
    frameData.waterParams0 = glm::vec4(
        m_waterWaveAmplitude,
        m_waterWaveLength,
        m_waterWaveSpeed,
        m_waterLevelY);
    frameData.waterParams1 = glm::vec4(
        m_waterPrimaryDir.x,
        m_waterPrimaryDir.y,
        m_waterSecondaryAmplitude,
        m_waterSteepness);

    // Previous and inverse view-projection matrices for TAA reprojection and
    // motion vectors. We store the *non-jittered* view-projection from the
    // previous frame so that motion vectors do not encode TAA jitter; jitter
    // is handled separately via g_TAAParams.xy in the post-process.
    if (m_hasPrevViewProj) {
        frameData.prevViewProjectionMatrix = m_prevViewProjMatrix;
    } else {
        frameData.prevViewProjectionMatrix = vpNoJitter;
    }

    frameData.invViewProjectionMatrix = glm::inverse(frameData.viewProjectionMatrix);

    // Update history for next frame (non-jittered)
    m_prevViewProjMatrix = vpNoJitter;
    m_hasPrevViewProj = true;

    // Reset RT temporal history when the camera moves significantly to
    // avoid smearing old GI/shadow data across new viewpoints. We also track
    // a softer motion flag used for TAA jitter/blend tuning.
    if (m_hasPrevCamera) {
        float posDelta = glm::length(cameraPos - m_prevCameraPos);
        float fwdDot = glm::clamp(
            glm::dot(glm::normalize(cameraForward), glm::normalize(m_prevCameraForward)),
            -1.0f,
            1.0f);
        float angleDelta = std::acos(fwdDot);

        // Hard thresholds for RT history invalidation. These should only fire
        // during significant camera jumps (teleports, cut scenes) to avoid
        // constantly resetting temporal accumulation during normal navigation.
        // The per-pixel rejection in RT shaders handles edge cases like
        // shadow boundaries and moving objects more gracefully.
        const float posThreshold   = 5.0f;
        const float angleThreshold = glm::radians(45.0f);

        // Soft thresholds for "camera is moving" used to gate jitter and TAA
        // blend strength. These fire during normal navigation to keep edges
        // sharp and reduce temporal lag.
        const float softPosThreshold   = 0.1f;
        const float softAngleThreshold = glm::radians(3.0f);
        m_cameraIsMoving = (posDelta > softPosThreshold || angleDelta > softAngleThreshold);

        if (posDelta > posThreshold || angleDelta > angleThreshold) {
            m_rtHasHistory      = false;
            m_rtGIHasHistory    = false;
            m_rtReflHasHistory  = false;
            // Let TAA resolve handle large changes via per-pixel color and
            // depth checks rather than nuking history globally; this avoids
            // sudden full-scene flicker when orbiting the camera.
        }
    } else {
        m_cameraIsMoving = true;
    }
    m_prevCameraPos     = cameraPos;
    m_prevCameraForward = cameraForward;
    m_hasPrevCamera     = true;

    m_frameDataCPU = frameData;
    m_frameConstantBuffer.UpdateData(m_frameDataCPU);
}

void Renderer::RenderSkybox() {
    // Only render a skybox when HDR + IBL are active and we have a pipeline.
    if (!m_skyboxPipeline || !m_hdrColor || !m_iblEnabled) {
        return;
    }

    // Root signature and descriptor heap should already be bound in PrepareMainPass,
    // but re-binding the pipeline and critical root params keeps this self-contained.
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_skyboxPipeline->GetPipelineState());

    // Frame constants (b1)
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

    // Shadow + environment descriptor table (t4-t6)
    if (m_shadowAndEnvDescriptors[0].IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowAndEnvDescriptors[0].gpu);
    }

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);
}

void Renderer::RenderSSR() {
    if (!m_ssrPipeline || !m_ssrColor || !m_hdrColor || !m_gbufferNormalRoughness || !m_depthBuffer) {
        return;
    }

    // Transition resources to appropriate states
    D3D12_RESOURCE_BARRIER barriers[4] = {};
    UINT barrierCount = 0;

    if (m_ssrState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_ssrColor.Get();
        barriers[barrierCount].Transition.StateBefore = m_ssrState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_ssrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (m_hdrState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_hdrColor.Get();
        barriers[barrierCount].Transition.StateBefore = m_hdrState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (m_gbufferNormalRoughness && m_gbufferNormalRoughnessState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferNormalRoughness.Get();
        barriers[barrierCount].Transition.StateBefore = m_gbufferNormalRoughnessState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (m_depthState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_depthBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_depthState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_depthState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (barrierCount > 0) {
        m_commandList->ResourceBarrier(barrierCount, barriers);
    }

    // Bind SSR render target
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_ssrRTV.cpu;
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_RESOURCE_DESC hdrDesc = m_hdrColor->GetDesc();

    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(hdrDesc.Width);
    viewport.Height = static_cast<float>(hdrDesc.Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(hdrDesc.Width);
    scissorRect.bottom = static_cast<LONG>(hdrDesc.Height);

    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissorRect);

    // Clear SSR buffer
    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    // Bind pipeline and resources
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_ssrPipeline->GetPipelineState());

    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    // Frame constants
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

    // Allocate transient descriptors for HDR (t0), depth (t1), normal/roughness (t2)
    auto hdrHandleResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
    if (hdrHandleResult.IsErr()) {
        spdlog::warn("RenderSSR: failed to allocate transient HDR SRV: {}", hdrHandleResult.Error());
        return;
    }
    DescriptorHandle hdrHandle = hdrHandleResult.Value();

    m_device->GetDevice()->CopyDescriptorsSimple(
        1,
        hdrHandle.cpu,
        m_hdrSRV.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );

    auto depthHandleResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
    if (depthHandleResult.IsErr()) {
        spdlog::warn("RenderSSR: failed to allocate transient depth SRV: {}", depthHandleResult.Error());
        return;
    }
    DescriptorHandle depthHandle = depthHandleResult.Value();

    m_device->GetDevice()->CopyDescriptorsSimple(
        1,
        depthHandle.cpu,
        m_depthSRV.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );

    auto gbufHandleResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
    if (gbufHandleResult.IsErr()) {
        spdlog::warn("RenderSSR: failed to allocate transient normal/roughness SRV: {}", gbufHandleResult.Error());
        return;
    }
    DescriptorHandle gbufHandle = gbufHandleResult.Value();

    m_device->GetDevice()->CopyDescriptorsSimple(
        1,
        gbufHandle.cpu,
        m_gbufferNormalRoughnessSRV.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );

    // Bind SRV table at slot 3 (t0-t2)
    m_commandList->SetGraphicsRootDescriptorTable(3, hdrHandle.gpu);

    // Shadow + environment descriptor table (space1) for potential future SSR IBL fallback
    if (m_shadowAndEnvDescriptors[0].IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowAndEnvDescriptors[0].gpu);
    }

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);
}

void Renderer::RenderTAA() {
    // Dedicated HDR TAA resolve pass. Operates on the main HDR color target
    // and writes into an intermediate HDR buffer before copying the result
    // back into the primary HDR target and updating the TAA history buffer.
    if (!m_taaEnabled || !m_taaPipeline || !m_hdrColor || !m_taaIntermediate || !m_window) {
        // Ensure HDR is in a readable state for subsequent passes even when TAA
        // is disabled so SSR/post-process can still sample it.
        if (m_hdrColor && m_hdrState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barrier.Transition.pResource = m_hdrColor.Get();
            barrier.Transition.StateBefore = m_hdrState;
            barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
            barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            m_commandList->ResourceBarrier(1, &barrier);
            m_hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        }
        // History is no longer meaningful once TAA has been disabled.
        m_hasHistory = false;
        return;
    }

    ID3D12Device* device = m_device->GetDevice();
    if (!device || !m_commandList) {
        return;
    }

    // If we do not yet have valid history (first frame after resize or after
    // a large camera jump), skip reprojection and simply seed the history
    // buffer with the current HDR frame.
    if (!m_historyColor || !m_historySRV.IsValid() || !m_hasHistory) {
        // Transition HDR to COPY_SOURCE and history to COPY_DEST.
        if (m_hdrState != D3D12_RESOURCE_STATE_COPY_SOURCE || m_historyState != D3D12_RESOURCE_STATE_COPY_DEST) {
            D3D12_RESOURCE_BARRIER initBarriers[2] = {};
            UINT initCount = 0;

            if (m_hdrState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
                initBarriers[initCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                initBarriers[initCount].Transition.pResource = m_hdrColor.Get();
                initBarriers[initCount].Transition.StateBefore = m_hdrState;
                initBarriers[initCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
                initBarriers[initCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                ++initCount;
                m_hdrState = D3D12_RESOURCE_STATE_COPY_SOURCE;
            }

            if (m_historyState != D3D12_RESOURCE_STATE_COPY_DEST) {
                initBarriers[initCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                initBarriers[initCount].Transition.pResource = m_historyColor.Get();
                initBarriers[initCount].Transition.StateBefore = m_historyState;
                initBarriers[initCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
                initBarriers[initCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                ++initCount;
                m_historyState = D3D12_RESOURCE_STATE_COPY_DEST;
            }

            if (initCount > 0) {
                m_commandList->ResourceBarrier(initCount, initBarriers);
            }
        }

        m_commandList->CopyResource(m_historyColor.Get(), m_hdrColor.Get());

        // Transition HDR to PIXEL_SHADER_RESOURCE for subsequent passes and
        // history back to PIXEL_SHADER_RESOURCE for future TAA frames.
        D3D12_RESOURCE_BARRIER postCopy[2] = {};

        postCopy[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postCopy[0].Transition.pResource = m_hdrColor.Get();
        postCopy[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        postCopy[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        postCopy[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        postCopy[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postCopy[1].Transition.pResource = m_historyColor.Get();
        postCopy[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        postCopy[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        postCopy[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_commandList->ResourceBarrier(2, postCopy);
        m_hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_hasHistory = true;
        return;
    }

    // Transition resources to appropriate states for the TAA draw.
    D3D12_RESOURCE_BARRIER barriers[5] = {};
    UINT barrierCount = 0;

    if (m_taaIntermediateState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_taaIntermediate.Get();
        barriers[barrierCount].Transition.StateBefore = m_taaIntermediateState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_taaIntermediateState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (m_hdrState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_hdrColor.Get();
        barriers[barrierCount].Transition.StateBefore = m_hdrState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (m_depthBuffer && m_depthState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_depthBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_depthState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_depthState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (m_gbufferNormalRoughness && m_gbufferNormalRoughnessState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_gbufferNormalRoughness.Get();
        barriers[barrierCount].Transition.StateBefore = m_gbufferNormalRoughnessState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (m_velocityBuffer && m_velocityState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_velocityBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_velocityState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_velocityState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (m_historyState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_historyColor.Get();
        barriers[barrierCount].Transition.StateBefore = m_historyState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (barrierCount > 0) {
        m_commandList->ResourceBarrier(barrierCount, barriers);
    }

    // Bind TAA render target (no depth).
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_taaIntermediateRTV.cpu;
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_RESOURCE_DESC hdrDesc = m_hdrColor->GetDesc();

    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(hdrDesc.Width);
    viewport.Height = static_cast<float>(hdrDesc.Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(hdrDesc.Width);
    scissorRect.bottom = static_cast<LONG>(hdrDesc.Height);

    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissorRect);

    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_taaPipeline->GetPipelineState());

    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    // Frame constants
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

    // Allocate transient descriptors mirroring the layout used in the
    // post-process pass so bindings remain consistent:
    // t0 = HDR scene color, t1 = bloom (unused here), t2 = SSAO (unused),
    // t3 = TAA history, t4 = depth, t5 = normal/roughness, t6 = SSR (unused),
    // t7 = velocity.
    if (m_taaResolveSrvTableValid) {
        UpdateTAAResolveDescriptorTable();
        m_commandList->SetGraphicsRootDescriptorTable(3, m_taaResolveSrvTable[0].gpu);
    } else {
        auto hdrHandleResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (hdrHandleResult.IsErr()) {
            spdlog::warn("RenderTAA: failed to allocate transient HDR SRV: {}", hdrHandleResult.Error());
            return;
        }
        DescriptorHandle hdrHandle = hdrHandleResult.Value();

        device->CopyDescriptorsSimple(
            1,
            hdrHandle.cpu,
            m_hdrSRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );

        // t1: bloom (unused)
        auto bloomAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (bloomAllocResult.IsOk() && m_bloomCombinedSRV.IsValid()) {
            DescriptorHandle bloomHandle = bloomAllocResult.Value();
            device->CopyDescriptorsSimple(
                1,
                bloomHandle.cpu,
                m_bloomCombinedSRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        } else {
            // Allocate a dummy descriptor to keep layout consistent even if bloom is missing.
            (void)bloomAllocResult;
        }

        // t2: SSAO (unused in TAA but keep slot)
        auto ssaoAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (ssaoAllocResult.IsOk() && m_ssaoSRV.IsValid()) {
            DescriptorHandle ssaoHandle = ssaoAllocResult.Value();
            device->CopyDescriptorsSimple(
                1,
                ssaoHandle.cpu,
                m_ssaoSRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        } else {
            (void)ssaoAllocResult;
        }

        // t3: TAA history
        auto historyAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (historyAllocResult.IsErr()) {
            spdlog::warn("RenderTAA: failed to allocate transient history SRV: {}", historyAllocResult.Error());
            return;
        }
        DescriptorHandle historyHandle = historyAllocResult.Value();
        device->CopyDescriptorsSimple(
            1,
            historyHandle.cpu,
            m_historySRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );

        // t4: depth
        auto depthAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (depthAllocResult.IsErr()) {
            spdlog::warn("RenderTAA: failed to allocate transient depth SRV: {}", depthAllocResult.Error());
            return;
        }
        DescriptorHandle depthHandle = depthAllocResult.Value();
        device->CopyDescriptorsSimple(
            1,
            depthHandle.cpu,
            m_depthSRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );

        // t5: normal/roughness
        auto gbufAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (gbufAllocResult.IsErr()) {
            spdlog::warn("RenderTAA: failed to allocate transient normal/roughness SRV: {}", gbufAllocResult.Error());
            return;
        }
        DescriptorHandle gbufHandle = gbufAllocResult.Value();
        device->CopyDescriptorsSimple(
            1,
            gbufHandle.cpu,
            m_gbufferNormalRoughnessSRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );

        // t6: SSR (unused)
        (void)m_descriptorManager->AllocateTransientCBV_SRV_UAV();

        // t7: velocity
        if (m_velocitySRV.IsValid() && m_velocityBuffer) {
            auto velAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
            if (velAllocResult.IsOk()) {
                DescriptorHandle velHandle = velAllocResult.Value();
                device->CopyDescriptorsSimple(
                    1,
                    velHandle.cpu,
                    m_velocitySRV.cpu,
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                );
            } else {
                spdlog::warn("RenderTAA: failed to allocate transient velocity SRV; TAA reprojection will be disabled this frame");
            }
        }

        m_commandList->SetGraphicsRootDescriptorTable(3, hdrHandle.gpu);
    }

    if (m_shadowAndEnvDescriptors[0].IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowAndEnvDescriptors[0].gpu);
    }

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);

    // Copy TAA-resolved HDR back into the primary HDR target so downstream
    // passes (SSR, bloom, post-process) see a stabilized image.
    D3D12_RESOURCE_BARRIER copyBarriers[3] = {};
    UINT copyCount = 0;

    if (m_taaIntermediateState != D3D12_RESOURCE_STATE_COPY_SOURCE) {
        copyBarriers[copyCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        copyBarriers[copyCount].Transition.pResource = m_taaIntermediate.Get();
        copyBarriers[copyCount].Transition.StateBefore = m_taaIntermediateState;
        copyBarriers[copyCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        copyBarriers[copyCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++copyCount;
        m_taaIntermediateState = D3D12_RESOURCE_STATE_COPY_SOURCE;
    }

    if (m_hdrState != D3D12_RESOURCE_STATE_COPY_DEST) {
        copyBarriers[copyCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        copyBarriers[copyCount].Transition.pResource = m_hdrColor.Get();
        copyBarriers[copyCount].Transition.StateBefore = m_hdrState;
        copyBarriers[copyCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        copyBarriers[copyCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++copyCount;
        m_hdrState = D3D12_RESOURCE_STATE_COPY_DEST;
    }

    if (copyCount > 0) {
        m_commandList->ResourceBarrier(copyCount, copyBarriers);
    }

    m_commandList->CopyResource(m_hdrColor.Get(), m_taaIntermediate.Get());

    // Prepare HDR for sampling by downstream passes and at the same time copy
    // the resolved HDR into the history buffer for the next frame.
    D3D12_RESOURCE_BARRIER postTaa[3] = {};
    UINT postCount = 0;

    postTaa[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    postTaa[postCount].Transition.pResource = m_taaIntermediate.Get();
    postTaa[postCount].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    postTaa[postCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    postTaa[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ++postCount;
    m_taaIntermediateState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    postTaa[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    postTaa[postCount].Transition.pResource = m_hdrColor.Get();
    postTaa[postCount].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    postTaa[postCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    postTaa[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ++postCount;

    if (m_historyState != D3D12_RESOURCE_STATE_COPY_DEST) {
        postTaa[postCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postTaa[postCount].Transition.pResource = m_historyColor.Get();
        postTaa[postCount].Transition.StateBefore = m_historyState;
        postTaa[postCount].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        postTaa[postCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++postCount;
    }

    m_commandList->ResourceBarrier(postCount, postTaa);

    m_commandList->CopyResource(m_historyColor.Get(), m_hdrColor.Get());

    // Final states: HDR as PIXEL_SHADER_RESOURCE for SSR/post-process, history
    // as PIXEL_SHADER_RESOURCE for next frame.
    D3D12_RESOURCE_BARRIER finalBarriers[2] = {};

    finalBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    finalBarriers[0].Transition.pResource = m_hdrColor.Get();
    finalBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
    finalBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    finalBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    finalBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    finalBarriers[1].Transition.pResource = m_historyColor.Get();
    finalBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    finalBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    finalBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_commandList->ResourceBarrier(2, finalBarriers);

    m_hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    m_hasHistory = true;
}

void Renderer::RenderMotionVectors() {
    if (!m_motionVectorsPipeline || !m_velocityBuffer || !m_depthBuffer) {
        return;
    }

    // Transition resources
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    UINT barrierCount = 0;

    if (m_velocityState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_velocityBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_velocityState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (m_depthState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_depthBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_depthState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_depthState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (barrierCount > 0) {
        m_commandList->ResourceBarrier(barrierCount, barriers);
    }

    // Bind render target
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_velocityRTV.cpu;
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    D3D12_RESOURCE_DESC velDesc = m_velocityBuffer->GetDesc();

    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(velDesc.Width);
    viewport.Height = static_cast<float>(velDesc.Height);
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(velDesc.Width);
    scissorRect.bottom = static_cast<LONG>(velDesc.Height);

    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissorRect);

    const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    // Bind pipeline/resources
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_motionVectorsPipeline->GetPipelineState());

    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

    auto depthHandleResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
    if (depthHandleResult.IsErr()) {
        spdlog::warn("RenderMotionVectors: failed to allocate transient depth SRV: {}", depthHandleResult.Error());
        return;
    }
    DescriptorHandle depthHandle = depthHandleResult.Value();

    m_device->GetDevice()->CopyDescriptorsSimple(
        1,
        depthHandle.cpu,
        m_depthSRV.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );

    m_commandList->SetGraphicsRootDescriptorTable(3, depthHandle.gpu);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);

    // Motion vectors will be sampled in post-process
    m_velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;
}

// =============================================================================
// GPU-Driven Rendering (Phase 1)
// =============================================================================

void Renderer::SetGPUCullingEnabled(bool enabled) {
    if (enabled && m_gpuCulling) {
        m_gpuCullingEnabled = true;
        m_indirectDrawEnabled = true;
        spdlog::info("GPU culling enabled (indirect draw active)");
    } else {
        m_gpuCullingEnabled = false;
        m_indirectDrawEnabled = false;
        if (enabled && !m_gpuCulling) {
            spdlog::warn("Cannot enable GPU culling: pipeline not initialized");
        }
    }
}

uint32_t Renderer::GetGPUCulledCount() const {
    return m_gpuCulling ? m_gpuCulling->GetVisibleCount() : 0;
}

uint32_t Renderer::GetGPUTotalInstances() const {
    return m_gpuCulling ? m_gpuCulling->GetTotalInstances() : 0;
}

void Renderer::CollectInstancesForGPUCulling(Scene::ECS_Registry* registry) {
    if (!registry || !m_gpuCulling) return;

    m_gpuInstances.clear();
    m_meshInfos.clear();

    auto view = registry->View<Scene::RenderableComponent, Scene::TransformComponent>();

    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        auto& transform = view.get<Scene::TransformComponent>(entity);

        if (!renderable.visible || !renderable.mesh) continue;
        if (IsTransparentRenderable(renderable)) continue;
        if (!renderable.mesh->gpuBuffers ||
            !renderable.mesh->gpuBuffers->vertexBuffer ||
            !renderable.mesh->gpuBuffers->indexBuffer) continue;

        GPUInstanceData inst{};
        inst.modelMatrix = transform.GetMatrix();

        // Compute bounding sphere in object space
        if (renderable.mesh->hasBounds) {
            inst.boundingSphere = glm::vec4(
                renderable.mesh->boundsCenter,
                renderable.mesh->boundsRadius
            );
        } else {
            // Default bounding sphere
            inst.boundingSphere = glm::vec4(0.0f, 0.0f, 0.0f, 10.0f);
        }

        inst.meshIndex = static_cast<uint32_t>(m_meshInfos.size());
        inst.materialIndex = 0; // Could map to material ID if needed
        inst.flags = 1; // Visible by default

        m_gpuInstances.push_back(inst);

        // Store mesh info for indirect draw
        MeshInfo meshInfo{};
        meshInfo.indexCount = static_cast<uint32_t>(renderable.mesh->indices.size());
        meshInfo.startIndex = 0;
        meshInfo.baseVertex = 0;
        meshInfo.materialIndex = 0;
        m_meshInfos.push_back(meshInfo);
    }

    // Debug logging for GPU culling collection
    static uint32_t s_frameCounter = 0;
    if (++s_frameCounter % 300 == 1) {  // Log every ~5 seconds at 60fps
        spdlog::debug("GPU Culling: Collected {} instances for culling", m_gpuInstances.size());
    }
}

void Renderer::DispatchGPUCulling() {
    if (!m_gpuCulling || m_gpuInstances.empty()) return;

    // Upload instances to GPU
    auto uploadResult = m_gpuCulling->UpdateInstances(m_commandList.Get(), m_gpuInstances);
    if (uploadResult.IsErr()) {
        spdlog::warn("GPU culling upload failed: {}", uploadResult.Error());
        return;
    }

    // Dispatch culling compute shader
    if (m_descriptorManager) {
        ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
        m_commandList->SetDescriptorHeaps(1, heaps);
    }
    auto cullResult = m_gpuCulling->DispatchCulling(
        m_commandList.Get(),
        m_frameDataCPU.viewProjectionNoJitter,
        glm::vec3(m_frameDataCPU.cameraPosition)
    );

    if (cullResult.IsErr()) {
        spdlog::warn("GPU culling dispatch failed: {}", cullResult.Error());
    }
}

void Renderer::CollectInstancesForVisibilityBuffer(Scene::ECS_Registry* registry) {
    if (!registry || !m_visibilityBuffer) return;

    m_vbInstances.clear();
    m_vbMeshDraws.clear();

    auto view = registry->View<Scene::RenderableComponent, Scene::TransformComponent>();

    // Map mesh pointers to their draw info index (to avoid duplicates)
    std::unordered_map<const Scene::MeshData*, uint32_t> meshToDrawIndex;

    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        auto& transform = view.get<Scene::TransformComponent>(entity);

        if (!renderable.visible || !renderable.mesh) continue;
        if (IsTransparentRenderable(renderable)) continue;
        if (!renderable.mesh->gpuBuffers ||
            !renderable.mesh->gpuBuffers->vertexBuffer ||
            !renderable.mesh->gpuBuffers->indexBuffer) continue;

        // Find or create mesh draw info
        uint32_t meshDrawIndex = 0;
        auto it = meshToDrawIndex.find(renderable.mesh.get());
        if (it == meshToDrawIndex.end()) {
            // First time seeing this mesh - create draw info
            meshDrawIndex = static_cast<uint32_t>(m_vbMeshDraws.size());
            meshToDrawIndex[renderable.mesh.get()] = meshDrawIndex;

            VisibilityBufferRenderer::VBMeshDrawInfo drawInfo{};
            drawInfo.vertexBuffer = renderable.mesh->gpuBuffers->vertexBuffer.Get();
            drawInfo.indexBuffer = renderable.mesh->gpuBuffers->indexBuffer.Get();
            drawInfo.vertexCount = static_cast<uint32_t>(renderable.mesh->positions.size());
            drawInfo.indexCount = static_cast<uint32_t>(renderable.mesh->indices.size());
            drawInfo.firstIndex = 0;
            drawInfo.baseVertex = 0;

            m_vbMeshDraws.push_back(drawInfo);
        } else {
            meshDrawIndex = it->second;
        }

        // Build instance data
        VBInstanceData inst{};
        inst.worldMatrix = transform.GetMatrix();
        inst.meshIndex = meshDrawIndex;  // Index into mesh draw array
        inst.materialIndex = 0; // TODO: Map to actual material
        inst.firstIndex = 0;
        inst.indexCount = static_cast<uint32_t>(renderable.mesh->indices.size());
        inst.baseVertex = 0;

        m_vbInstances.push_back(inst);
    }

    // Upload instance data to visibility buffer
    auto uploadResult = m_visibilityBuffer->UpdateInstances(m_commandList.Get(), m_vbInstances);
    if (uploadResult.IsErr()) {
        spdlog::warn("Failed to update visibility buffer instances: {}", uploadResult.Error());
    }
}

void Renderer::RenderVisibilityBufferPath(Scene::ECS_Registry* registry) {
    if (!m_visibilityBuffer || !m_visibilityBufferEnabled) {
        spdlog::warn("VB: Disabled or not initialized");
        return;
    }

    // Collect and upload instance data + mesh draw info
    CollectInstancesForVisibilityBuffer(registry);

    if (m_vbInstances.empty() || m_vbMeshDraws.empty()) {
        spdlog::warn("VB: No instances collected (instances={}, meshDraws={})",
                     m_vbInstances.size(), m_vbMeshDraws.size());
        return;
    }

    // One-time debug log for first frame
    static bool firstFrame = true;
    if (firstFrame) {
        spdlog::info("VB: First frame - rendering {} instances across {} unique meshes",
                     m_vbInstances.size(), m_vbMeshDraws.size());
        // Log mesh indices to verify deduplication
        std::unordered_map<uint32_t, uint32_t> meshIndexCounts;
        for (const auto& inst : m_vbInstances) {
            meshIndexCounts[inst.meshIndex]++;
        }
        for (const auto& [meshIdx, count] : meshIndexCounts) {
            spdlog::info("  Mesh {} has {} instances", meshIdx, count);
        }
        firstFrame = false;
    }

    // Phase 1: Render visibility buffer (triangle IDs)
    auto visResult = m_visibilityBuffer->RenderVisibilityPass(
        m_commandList.Get(),
        m_depthBuffer.Get(),
        m_depthStencilView.cpu,
        m_frameDataCPU.viewProjectionMatrix,
        m_vbMeshDraws
    );

    if (visResult.IsErr()) {
        spdlog::error("Visibility pass failed: {}", visResult.Error());
        return;
    }

    // Phase 2: Resolve materials via compute shader
    auto resolveResult = m_visibilityBuffer->ResolveMaterials(
        m_commandList.Get(),
        m_depthBuffer.Get(),
        m_depthSRV.cpu,
        m_vbMeshDraws,
        m_frameDataCPU.viewProjectionMatrix
    );

    if (resolveResult.IsErr()) {
        spdlog::error("Material resolve failed: {}", resolveResult.Error());
        return;
    }

    // One-time debug for material resolve
    static bool firstResolve = true;
    if (firstResolve) {
        spdlog::info("VB: Material resolve completed successfully");
        firstResolve = false;
    }

    // ========================================================================
    // TEMPORARY: Blit G-buffer albedo to HDR buffer for visualization
    // TODO Phase 2.3: Replace with full deferred lighting pass
    // ========================================================================

    // Transition HDR buffer to render target
    if (m_hdrState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_hdrColor.Get();
        barrier.Transition.StateBefore = m_hdrState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);
        m_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    // Phase 3: Temporarily use debug blit until descriptor table issue is resolved
    // TODO: Fix descriptor table allocation for deferred lighting
    auto blitResult = m_visibilityBuffer->DebugBlitAlbedoToHDR(
        m_commandList.Get(),
        m_hdrColor.Get(),
        m_hdrRTV.cpu
    );
    if (blitResult.IsErr()) {
        spdlog::warn("Debug blit failed: {}", blitResult.Error());
    }
}

void Renderer::RenderSceneIndirect(Scene::ECS_Registry* registry) {
    if (!registry || !m_gpuCulling) {
        RenderScene(registry);
        return;
    }

    auto* cmdSig = m_gpuCulling->GetCommandSignature();
    if (!cmdSig) {
        spdlog::warn("RenderSceneIndirect: missing command signature or command buffers");
        RenderScene(registry);
        return;
    }

    const bool forceVisible = (std::getenv("CORTEX_GPUCULL_FORCE_VISIBLE") != nullptr);
    const bool bypassCompaction = (std::getenv("CORTEX_GPUCULL_BYPASS_COMPACTION") != nullptr);
    const bool dumpCommands = (std::getenv("CORTEX_GPUCULL_DUMP_COMMANDS") != nullptr);

    m_gpuCulling->SetForceVisible(forceVisible);

    ID3D12Resource* argBuffer = bypassCompaction
        ? m_gpuCulling->GetAllCommandBuffer()
        : m_gpuCulling->GetVisibleCommandBuffer();
    ID3D12Resource* countBuffer = bypassCompaction
        ? nullptr
        : m_gpuCulling->GetCommandCountBuffer();

    if (!argBuffer || (!bypassCompaction && !countBuffer)) {
        spdlog::warn("RenderSceneIndirect: missing command signature or command buffers");
        RenderScene(registry);
        return;
    }

    // Setup render state
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_pipeline->GetPipelineState());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Bind descriptor heap (legacy CBV/SRV/UAV heap). This keeps the
    // shadow/IBL descriptor table valid for the main shading path.
    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

    if (m_shadowAndEnvDescriptors[0].IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowAndEnvDescriptors[0].gpu);
    }
    if (m_fallbackMaterialDescriptors[0].IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(3, m_fallbackMaterialDescriptors[0].gpu);
    }

    m_gpuInstances.clear();
    std::vector<IndirectCommand> commands;

    auto view = registry->View<Scene::RenderableComponent, Scene::TransformComponent>();
    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        auto& transform = view.get<Scene::TransformComponent>(entity);

        if (!renderable.visible || !renderable.mesh) {
            continue;
        }
        if (IsTransparentRenderable(renderable)) {
            continue;
        }
        if (!renderable.mesh->gpuBuffers ||
            !renderable.mesh->gpuBuffers->vertexBuffer ||
            !renderable.mesh->gpuBuffers->indexBuffer) {
            continue;
        }

        EnsureMaterialTextures(renderable);

        MaterialConstants materialData = {};
        materialData.albedo = renderable.albedoColor;
        materialData.metallic = glm::clamp(renderable.metallic, 0.0f, 1.0f);
        materialData.roughness = glm::clamp(renderable.roughness, 0.0f, 1.0f);
        materialData.ao = glm::clamp(renderable.ao, 0.0f, 1.0f);

        const auto hasAlbedoMap = renderable.textures.albedo && renderable.textures.albedo != m_placeholderAlbedo;
        const auto hasNormalMap = renderable.textures.normal && renderable.textures.normal != m_placeholderNormal;
        const auto hasMetallicMap = renderable.textures.metallic && renderable.textures.metallic != m_placeholderMetallic;
        const auto hasRoughnessMap = renderable.textures.roughness && renderable.textures.roughness != m_placeholderRoughness;

        materialData.mapFlags = glm::uvec4(
            hasAlbedoMap ? 1u : 0u,
            hasNormalMap ? 1u : 0u,
            hasMetallicMap ? 1u : 0u,
            hasRoughnessMap ? 1u : 0u
        );

        FillMaterialTextureIndices(renderable, materialData);

        // Match the full material parameter setup used by the forward path.
        materialData.fractalParams0 = glm::vec4(
            m_fractalAmplitude,
            m_fractalFrequency,
            m_fractalOctaves,
            (m_fractalAmplitude > 0.0f ? 1.0f : 0.0f));
        materialData.fractalParams1 = glm::vec4(
            m_fractalCoordMode,
            m_fractalScaleX,
            m_fractalScaleZ,
            0.0f);
        materialData.fractalParams2 = glm::vec4(
            m_fractalLacunarity,
            m_fractalGain,
            m_fractalWarpStrength,
            m_fractalNoiseType);

        float clearCoat = 0.0f;
        float clearCoatRoughness = 0.2f;
        float sheenWeight = 0.0f;
        float sssWrap = 0.0f;

        float materialType = 0.0f;
        if (!renderable.presetName.empty()) {
            std::string presetLower = renderable.presetName;
            std::transform(presetLower.begin(),
                           presetLower.end(),
                           presetLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (presetLower.find("glass") != std::string::npos) {
                materialType = 1.0f;
            } else if (presetLower.find("mirror") != std::string::npos) {
                materialType = 2.0f;
            } else if (presetLower.find("plastic") != std::string::npos) {
                materialType = 3.0f;
            } else if (presetLower.find("brick") != std::string::npos) {
                materialType = 4.0f;
            } else if (presetLower.find("brushed_metal") != std::string::npos) {
                materialType = 6.0f;
            } else if (presetLower.find("wood_floor") != std::string::npos) {
                materialType = 7.0f;
            } else if (presetLower.find("emissive") != std::string::npos ||
                       presetLower.find("neon") != std::string::npos ||
                       presetLower.find("light") != std::string::npos) {
                materialType = 5.0f;
            }

            if (presetLower.find("painted_plastic") != std::string::npos ||
                presetLower.find("plastic") != std::string::npos)
            {
                clearCoat = 1.0f;
                clearCoatRoughness = 0.15f;
            }
            else if (presetLower.find("polished_metal") != std::string::npos ||
                     presetLower.find("chrome") != std::string::npos)
            {
                clearCoat = 0.6f;
                clearCoatRoughness = 0.08f;
            }

            if (presetLower.find("cloth") != std::string::npos ||
                presetLower.find("velvet") != std::string::npos)
            {
                clearCoat = 0.0f;
                sheenWeight = 1.0f;
            }

            if (presetLower.find("skin_ish") != std::string::npos)
            {
                sssWrap = 0.25f;
            }
            else if (presetLower.find("skin") != std::string::npos)
            {
                sssWrap = 0.35f;
            }
        }
        materialData.fractalParams1.w = materialType;
        materialData.coatParams = glm::vec4(clearCoat, clearCoatRoughness, sheenWeight, sssWrap);

        ObjectConstants objectData = {};
        objectData.modelMatrix = transform.GetMatrix();
        objectData.normalMatrix = transform.GetNormalMatrix();

        D3D12_GPU_VIRTUAL_ADDRESS objectCB = m_objectConstantBuffer.AllocateAndWrite(objectData);
        D3D12_GPU_VIRTUAL_ADDRESS materialCB = m_materialConstantBuffer.AllocateAndWrite(materialData);

        GPUInstanceData inst{};
        inst.modelMatrix = transform.GetMatrix();
        if (renderable.mesh->hasBounds) {
            inst.boundingSphere = glm::vec4(
                renderable.mesh->boundsCenter,
                renderable.mesh->boundsRadius
            );
        } else {
            inst.boundingSphere = glm::vec4(0.0f, 0.0f, 0.0f, 10.0f);
        }
        inst.meshIndex = 0;
        inst.materialIndex = 0;
        inst.flags = 1;
        m_gpuInstances.push_back(inst);

        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = renderable.mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
        vbv.SizeInBytes = static_cast<UINT>(renderable.mesh->positions.size() * sizeof(Vertex));
        vbv.StrideInBytes = sizeof(Vertex);

        D3D12_INDEX_BUFFER_VIEW ibv = {};
        ibv.BufferLocation = renderable.mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
        ibv.SizeInBytes = static_cast<UINT>(renderable.mesh->indices.size() * sizeof(uint32_t));
        ibv.Format = DXGI_FORMAT_R32_UINT;

        IndirectCommand cmd{};
        cmd.objectCBV = objectCB;
        cmd.materialCBV = materialCB;
        cmd.vertexBuffer = vbv;
        cmd.indexBuffer = ibv;
        cmd.draw.indexCountPerInstance = static_cast<uint32_t>(renderable.mesh->indices.size());
        cmd.draw.instanceCount = 1;
        cmd.draw.startIndexLocation = 0;
        cmd.draw.baseVertexLocation = 0;
        cmd.draw.startInstanceLocation = 0;
        commands.push_back(cmd);
    }

    if (commands.empty()) {
        return;
    }

    if (dumpCommands && !bypassCompaction) {
        static uint64_t s_lastDumpFrame = 0;
        if ((m_renderFrameCounter % 120) == 0 && m_renderFrameCounter != s_lastDumpFrame) {
            const uint32_t maxLog = std::min<uint32_t>(static_cast<uint32_t>(commands.size()), 2u);
            for (uint32_t i = 0; i < maxLog; ++i) {
                const auto& cmd = commands[i];
                spdlog::info(
                    "CPU Cmd[{}]: objectCBV=0x{:016X} materialCBV=0x{:016X} "
                    "VBV(addr=0x{:016X} size={} stride={}) "
                    "IBV(addr=0x{:016X} size={} fmt={}) "
                    "draw(indexCount={} instanceCount={} startIndex={} baseVertex={} startInstance={})",
                    i,
                    static_cast<uint64_t>(cmd.objectCBV),
                    static_cast<uint64_t>(cmd.materialCBV),
                    static_cast<uint64_t>(cmd.vertexBuffer.BufferLocation),
                    cmd.vertexBuffer.SizeInBytes,
                    cmd.vertexBuffer.StrideInBytes,
                    static_cast<uint64_t>(cmd.indexBuffer.BufferLocation),
                    cmd.indexBuffer.SizeInBytes,
                    static_cast<unsigned int>(cmd.indexBuffer.Format),
                    cmd.draw.indexCountPerInstance,
                    cmd.draw.instanceCount,
                    cmd.draw.startIndexLocation,
                    cmd.draw.baseVertexLocation,
                    cmd.draw.startInstanceLocation);
            }
            s_lastDumpFrame = m_renderFrameCounter;
            m_gpuCulling->RequestCommandReadback(maxLog);
        }
    }

    auto commandResult = m_gpuCulling->UpdateIndirectCommands(m_commandList.Get(), commands);
    if (commandResult.IsErr()) {
        spdlog::warn("RenderSceneIndirect: failed to upload commands: {}", commandResult.Error());
        RenderScene(registry);
        return;
    }

    if (!bypassCompaction) {
        auto uploadResult = m_gpuCulling->UpdateInstances(m_commandList.Get(), m_gpuInstances);
        if (uploadResult.IsErr()) {
            spdlog::warn("RenderSceneIndirect: failed to upload instances: {}", uploadResult.Error());
            RenderScene(registry);
            return;
        }

        auto cullResult = m_gpuCulling->DispatchCulling(
            m_commandList.Get(),
            m_frameDataCPU.viewProjectionNoJitter,
            glm::vec3(m_frameDataCPU.cameraPosition)
        );
        if (cullResult.IsErr()) {
            spdlog::warn("RenderSceneIndirect: culling dispatch failed: {}", cullResult.Error());
            RenderScene(registry);
            return;
        }
    } else {
        auto prepResult = m_gpuCulling->PrepareAllCommandsForExecuteIndirect(m_commandList.Get());
        if (prepResult.IsErr()) {
            spdlog::warn("RenderSceneIndirect: failed to prepare all-commands buffer: {}", prepResult.Error());
            RenderScene(registry);
            return;
        }
    }

    // Compute dispatch changes the root signature/pipeline; restore graphics state
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_pipeline->GetPipelineState());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->SetDescriptorHeaps(1, heaps);
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);
    if (m_shadowAndEnvDescriptors[0].IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowAndEnvDescriptors[0].gpu);
    }
    if (m_fallbackMaterialDescriptors[0].IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(3, m_fallbackMaterialDescriptors[0].gpu);
    }

    const UINT maxCommands = static_cast<UINT>(commands.size());
    m_commandList->ExecuteIndirect(
        cmdSig,
        maxCommands,
        argBuffer,
        0,
        countBuffer,
        0
    );

    static uint64_t s_lastCullingLogFrame = 0;
    if ((m_renderFrameCounter % 300) == 0 && m_renderFrameCounter != s_lastCullingLogFrame) {
        const uint32_t total = m_gpuCulling->GetTotalInstances();
        const uint32_t visible = m_gpuCulling->GetVisibleCount();
        const float visiblePct = (total > 0) ? (100.0f * static_cast<float>(visible) / total) : 0.0f;
        spdlog::info("GPU Culling: total={}, visible={} ({:.1f}% visible)", total, visible, visiblePct);
        s_lastCullingLogFrame = m_renderFrameCounter;
    }
}

void Renderer::RenderScene(Scene::ECS_Registry* registry) {
    // Ensure graphics pipeline and root signature are bound after any compute work
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_pipeline->GetPipelineState());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    // Bind frame constants
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

    // Bind shadow map + environment descriptor table if available (t4-t6)
    if (m_shadowAndEnvDescriptors[0].IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowAndEnvDescriptors[0].gpu);
    }

    // Render all entities with Renderable and Transform components
    auto view = registry->View<Scene::RenderableComponent, Scene::TransformComponent>();

    int entityCount = 0;
    int drawnCount = 0;

    for (auto entity : view) {
        entityCount++;
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        auto& transform = view.get<Scene::TransformComponent>(entity);

        if (!renderable.visible || !renderable.mesh) {
            continue;
        }
        // Transparent / glass materials are rendered in a dedicated blended
        // pass after all opaque geometry so depth testing and composition
        // behave correctly. Skip them here.
        if (IsTransparentRenderable(renderable)) {
            continue;
        }

        // Simple frustum/near-far culling using a bounding sphere derived
        // from the mesh's object-space bounds and the entity transform. This
        // avoids submitting obviously off-screen objects in large scenes
        // such as the RT showcase gallery without changing visibility for
        // anything inside the camera frustum.
        const auto& meshData = *renderable.mesh;
        if (meshData.hasBounds) {
            const glm::vec3 centerWS = glm::vec3(transform.worldMatrix * glm::vec4(meshData.boundsCenter, 1.0f));
            const float maxScale = glm::compMax(glm::abs(transform.scale));
            const float radiusWS = meshData.boundsRadius * maxScale;

            const glm::vec3 toCenter = centerWS - m_cameraPositionWS;
            const float distAlongFwd = glm::dot(toCenter, glm::normalize(m_cameraForwardWS));

            // Cull objects entirely behind the near plane or far beyond the
            // far plane, with a small radius cushion.
            if (distAlongFwd + radiusWS < m_cameraNearPlane ||
                distAlongFwd - radiusWS > m_cameraFarPlane) {
                continue;
            }
        }

        EnsureMaterialTextures(renderable);

        // Update material constants
        MaterialConstants materialData = {};
        materialData.albedo = renderable.albedoColor;
        materialData.metallic = glm::clamp(renderable.metallic, 0.0f, 1.0f);
        materialData.roughness = glm::clamp(renderable.roughness, 0.0f, 1.0f);
        materialData.ao = glm::clamp(renderable.ao, 0.0f, 1.0f);

        const auto hasAlbedoMap = renderable.textures.albedo && renderable.textures.albedo != m_placeholderAlbedo;
        const auto hasNormalMap = renderable.textures.normal && renderable.textures.normal != m_placeholderNormal;
        const auto hasMetallicMap = renderable.textures.metallic && renderable.textures.metallic != m_placeholderMetallic;
        const auto hasRoughnessMap = renderable.textures.roughness && renderable.textures.roughness != m_placeholderRoughness;
        materialData.mapFlags = glm::uvec4(
            hasAlbedoMap ? 1u : 0u,
            hasNormalMap ? 1u : 0u,
            hasMetallicMap ? 1u : 0u,
            hasRoughnessMap ? 1u : 0u
        );

        FillMaterialTextureIndices(renderable, materialData);

        // Global fractal parameters (applied uniformly to all materials)
        materialData.fractalParams0 = glm::vec4(
            m_fractalAmplitude,
            m_fractalFrequency,
            m_fractalOctaves,
            (m_fractalAmplitude > 0.0f ? 1.0f : 0.0f));
        materialData.fractalParams1 = glm::vec4(
            m_fractalCoordMode,
            m_fractalScaleX,
            m_fractalScaleZ,
            0.0f);
        materialData.fractalParams2 = glm::vec4(
            m_fractalLacunarity,
            m_fractalGain,
            m_fractalWarpStrength,
            m_fractalNoiseType);

        // Clear-coat / sheen / SSS parameters used by the shader to add thin
        // glossy or cloth-like layers over the base BRDF.
        // x = coat weight, y = coat roughness, z = sheen weight, w = SSS wrap.
        float clearCoat = 0.0f;
        float clearCoatRoughness = 0.2f;
        float sheenWeight = 0.0f;
        float sssWrap = 0.0f;

        // Encode a simple material "type" into fractalParams1.w so the
        // shader can specialize behavior for glass / mirror / plastic /
        // brick without changing the MaterialConstants layout.
        //
        // 0 = default (opaque)
        // 1 = glass-like dielectric (strong specular, very little diffuse)
        // 2 = mirror-like metal (polished conductor)
        // 3 = plastic
        // 4 = brick / masonry
        // 5 = emissive / neon surface
        // 6 = anisotropic metal (brushed)
        // 7 = anisotropic wood
        // 6 = anisotropic metal (brushed)
        // 7 = anisotropic wood
        float materialType = 0.0f;
        if (!renderable.presetName.empty()) {
            std::string presetLower = renderable.presetName;
            std::transform(presetLower.begin(),
                           presetLower.end(),
                           presetLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (presetLower.find("glass") != std::string::npos) {
                materialType = 1.0f;
            } else if (presetLower.find("mirror") != std::string::npos) {
                materialType = 2.0f;
            } else if (presetLower.find("plastic") != std::string::npos) {
                materialType = 3.0f;
            } else if (presetLower.find("brick") != std::string::npos) {
                materialType = 4.0f;
            } else if (presetLower.find("brushed_metal") != std::string::npos) {
                materialType = 6.0f;
            } else if (presetLower.find("wood_floor") != std::string::npos) {
                materialType = 7.0f;
            } else if (presetLower.find("emissive") != std::string::npos ||
                       presetLower.find("neon") != std::string::npos ||
                       presetLower.find("light") != std::string::npos) {
                materialType = 5.0f;
            }

            // Heuristic clear-coat: painted plastics and polished metals
            // get a thin glossy top layer for stronger, tighter highlights.
            if (presetLower.find("painted_plastic") != std::string::npos ||
                presetLower.find("plastic") != std::string::npos)
            {
                clearCoat = 1.0f;
                clearCoatRoughness = 0.15f;
            }
            else if (presetLower.find("polished_metal") != std::string::npos ||
                     presetLower.find("chrome") != std::string::npos)
            {
                clearCoat = 0.6f;
                clearCoatRoughness = 0.08f;
            }

            // Cloth / velvet-style presets get a soft sheen lobe instead of
            // a strong clear-coat highlight.
            if (presetLower.find("cloth") != std::string::npos ||
                presetLower.find("velvet") != std::string::npos)
            {
                clearCoat = 0.0f;
                sheenWeight = 1.0f;
            }

            // Skin-like presets get a gentle wrap-diffuse term for a very
            // simple subsurface scattering approximation.
            if (presetLower.find("skin_ish") != std::string::npos)
            {
                sssWrap = 0.25f;
            }
            else if (presetLower.find("skin") != std::string::npos)
            {
                sssWrap = 0.35f;
            }
        }
        materialData.fractalParams1.w = materialType;
        materialData.coatParams = glm::vec4(clearCoat, clearCoatRoughness, sheenWeight, sssWrap);

        // Update object constants
        ObjectConstants objectData = {};
        objectData.modelMatrix = transform.GetMatrix();
        objectData.normalMatrix = transform.GetNormalMatrix();

        D3D12_GPU_VIRTUAL_ADDRESS objectCB = m_objectConstantBuffer.AllocateAndWrite(objectData);
        D3D12_GPU_VIRTUAL_ADDRESS materialCB = m_materialConstantBuffer.AllocateAndWrite(materialData);

        // Bind constants
        m_commandList->SetGraphicsRootConstantBufferView(0, objectCB);
        m_commandList->SetGraphicsRootConstantBufferView(2, materialCB);

        // Select pipeline: dedicated water pipeline when available and entity
        // is tagged as a water surface; otherwise use the default PBR pipeline.
        // Re-set topology defensively after pipeline switch to guard against
        // future changes where water might use a different topology.
        const bool isWater = registry->HasComponent<Scene::WaterSurfaceComponent>(entity);
        if (isWater && m_waterPipeline) {
            m_commandList->SetPipelineState(m_waterPipeline->GetPipelineState());
        } else {
            m_commandList->SetPipelineState(m_pipeline->GetPipelineState());
        }
        // Defensive topology reset after any pipeline switch
        m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

        RefreshMaterialDescriptors(renderable);
        if (!renderable.textures.gpuState || !renderable.textures.gpuState->descriptors[0].IsValid()) {
            continue;
        }
        m_commandList->SetGraphicsRootDescriptorTable(3, renderable.textures.gpuState->descriptors[0].gpu);

        // Bind vertex and index buffers
        if (renderable.mesh->gpuBuffers && renderable.mesh->gpuBuffers->vertexBuffer && renderable.mesh->gpuBuffers->indexBuffer) {
            D3D12_VERTEX_BUFFER_VIEW vbv = {};
            vbv.BufferLocation = renderable.mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
            vbv.SizeInBytes = static_cast<UINT>(renderable.mesh->positions.size() * sizeof(Vertex));
            vbv.StrideInBytes = sizeof(Vertex);

            D3D12_INDEX_BUFFER_VIEW ibv = {};
            ibv.BufferLocation = renderable.mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
            ibv.SizeInBytes = static_cast<UINT>(renderable.mesh->indices.size() * sizeof(uint32_t));
            ibv.Format = DXGI_FORMAT_R32_UINT;

            m_commandList->IASetVertexBuffers(0, 1, &vbv);
            m_commandList->IASetIndexBuffer(&ibv);

            m_commandList->DrawIndexedInstanced(static_cast<UINT>(renderable.mesh->indices.size()), 1, 0, 0, 0);
            drawnCount++;
        } else {
            // Log this warning only once to avoid spamming the console every
            // frame if the scene contains placeholder entities without mesh
            // data (for example, when scene setup fails part-way through).
            if (!m_missingBufferWarningLogged) {
                spdlog::warn("  Entity {} has no vertex/index buffers", entityCount);
                m_missingBufferWarningLogged = true;
            }
        }
    }
 
    if (drawnCount == 0 && entityCount > 0 && !m_zeroDrawWarningLogged) {
        spdlog::warn("RenderScene: Found {} entities but drew 0!", entityCount);
        m_zeroDrawWarningLogged = true;
    }
}

void Renderer::RenderTransparent(Scene::ECS_Registry* registry) {
    if (!registry || !m_transparentPipeline) {
        return;
    }

    auto view = registry->View<Scene::RenderableComponent, Scene::TransformComponent>();
    if (view.begin() == view.end()) {
        return;
    }

    struct TransparentDraw {
        entt::entity entity;
        float depth;
    };

    std::vector<TransparentDraw> drawList;
    drawList.reserve(static_cast<size_t>(view.size_hint()));

    const glm::vec3 cameraPos = glm::vec3(m_frameDataCPU.cameraPosition);

    // Collect transparent entities and compute a simple distance-based depth
    // for back-to-front sorting.
    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        auto& transform  = view.get<Scene::TransformComponent>(entity);

        if (!renderable.visible || !renderable.mesh) {
            continue;
        }
        if (!IsTransparentRenderable(renderable)) {
            continue;
        }

        glm::vec3 worldPos = glm::vec3(transform.GetMatrix()[3]);
        float depth = glm::length2(worldPos - cameraPos);
        drawList.push_back(TransparentDraw{entity, depth});
    }

    if (drawList.empty()) {
        return;
    }

    std::sort(drawList.begin(), drawList.end(),
              [](const TransparentDraw& a, const TransparentDraw& b) {
                  // Draw far-to-near for correct alpha blending.
                  return a.depth > b.depth;
              });

    // Root signature, pipeline, descriptor heap, and primitive topology for
    // main geometry were already set in PrepareMainPass. We rebind the
    // transparent pipeline and frame constants to be explicit.
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_transparentPipeline->GetPipelineState());
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

    if (m_shadowAndEnvDescriptors[0].IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowAndEnvDescriptors[0].gpu);
    }

    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandList->SetDescriptorHeaps(1, heaps);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (const auto& draw : drawList) {
        auto entity = draw.entity;
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        auto& transform  = view.get<Scene::TransformComponent>(entity);

        if (!renderable.visible || !renderable.mesh) {
            continue;
        }

        EnsureMaterialTextures(renderable);

        MaterialConstants materialData = {};
        materialData.albedo    = renderable.albedoColor;
        materialData.metallic  = glm::clamp(renderable.metallic, 0.0f, 1.0f);
        materialData.roughness = glm::clamp(renderable.roughness, 0.0f, 1.0f);
        materialData.ao        = glm::clamp(renderable.ao, 0.0f, 1.0f);

        const auto hasAlbedoMap    = renderable.textures.albedo && renderable.textures.albedo != m_placeholderAlbedo;
        const auto hasNormalMap    = renderable.textures.normal && renderable.textures.normal != m_placeholderNormal;
        const auto hasMetallicMap  = renderable.textures.metallic && renderable.textures.metallic != m_placeholderMetallic;
        const auto hasRoughnessMap = renderable.textures.roughness && renderable.textures.roughness != m_placeholderRoughness;

        materialData.mapFlags = glm::uvec4(
            hasAlbedoMap ? 1u : 0u,
            hasNormalMap ? 1u : 0u,
            hasMetallicMap ? 1u : 0u,
            hasRoughnessMap ? 1u : 0u);

        FillMaterialTextureIndices(renderable, materialData);

        materialData.fractalParams0 = glm::vec4(
            m_fractalAmplitude,
            m_fractalFrequency,
            m_fractalOctaves,
            (m_fractalAmplitude > 0.0f ? 1.0f : 0.0f));
        materialData.fractalParams1 = glm::vec4(
            m_fractalCoordMode,
            m_fractalScaleX,
            m_fractalScaleZ,
            0.0f);
        materialData.fractalParams2 = glm::vec4(
            m_fractalLacunarity,
            m_fractalGain,
            m_fractalWarpStrength,
            m_fractalNoiseType);

        float clearCoat = 0.0f;
        float clearCoatRoughness = 0.2f;
        float sheenWeight = 0.0f;
        float sssWrap = 0.0f;

        float materialType = 0.0f;
        if (!renderable.presetName.empty()) {
            std::string presetLower = renderable.presetName;
            std::transform(presetLower.begin(),
                           presetLower.end(),
                           presetLower.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (presetLower.find("glass") != std::string::npos) {
                materialType = 1.0f;
            } else if (presetLower.find("mirror") != std::string::npos) {
                materialType = 2.0f;
            } else if (presetLower.find("plastic") != std::string::npos) {
                materialType = 3.0f;
            } else if (presetLower.find("brick") != std::string::npos) {
                materialType = 4.0f;
            } else if (presetLower.find("brushed_metal") != std::string::npos) {
                materialType = 6.0f;
            } else if (presetLower.find("wood_floor") != std::string::npos) {
                materialType = 7.0f;
            } else if (presetLower.find("emissive") != std::string::npos ||
                       presetLower.find("neon") != std::string::npos ||
                       presetLower.find("light") != std::string::npos) {
                materialType = 5.0f;
            }

            if (presetLower.find("painted_plastic") != std::string::npos ||
                presetLower.find("plastic") != std::string::npos)
            {
                clearCoat = 1.0f;
                clearCoatRoughness = 0.15f;
            }
            else if (presetLower.find("polished_metal") != std::string::npos ||
                     presetLower.find("chrome") != std::string::npos)
            {
                clearCoat = 0.6f;
                clearCoatRoughness = 0.08f;
            }

            if (presetLower.find("cloth") != std::string::npos ||
                presetLower.find("velvet") != std::string::npos)
            {
                clearCoat = 0.0f;
                sheenWeight = 1.0f;
            }

            if (presetLower.find("skin_ish") != std::string::npos)
            {
                sssWrap = 0.25f;
            }
            else if (presetLower.find("skin") != std::string::npos)
            {
                sssWrap = 0.35f;
            }
        }
        materialData.fractalParams1.w = materialType;
        materialData.coatParams = glm::vec4(clearCoat, clearCoatRoughness, sheenWeight, sssWrap);

        ObjectConstants objectData = {};
        objectData.modelMatrix  = transform.GetMatrix();
        objectData.normalMatrix = transform.GetNormalMatrix();

        D3D12_GPU_VIRTUAL_ADDRESS objectCB =
            m_objectConstantBuffer.AllocateAndWrite(objectData);
        D3D12_GPU_VIRTUAL_ADDRESS materialCB =
            m_materialConstantBuffer.AllocateAndWrite(materialData);

        m_commandList->SetGraphicsRootConstantBufferView(0, objectCB);
        m_commandList->SetGraphicsRootConstantBufferView(2, materialCB);

        RefreshMaterialDescriptors(renderable);
        if (!renderable.textures.gpuState ||
            !renderable.textures.gpuState->descriptors[0].IsValid()) {
            continue;
        }

        m_commandList->SetGraphicsRootDescriptorTable(
            3, renderable.textures.gpuState->descriptors[0].gpu);

        if (renderable.mesh->gpuBuffers &&
            renderable.mesh->gpuBuffers->vertexBuffer &&
            renderable.mesh->gpuBuffers->indexBuffer) {
            D3D12_VERTEX_BUFFER_VIEW vbv{};
            vbv.BufferLocation =
                renderable.mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
            vbv.SizeInBytes = static_cast<UINT>(
                renderable.mesh->positions.size() * sizeof(Vertex));
            vbv.StrideInBytes = sizeof(Vertex);

            D3D12_INDEX_BUFFER_VIEW ibv{};
            ibv.BufferLocation =
                renderable.mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
            ibv.SizeInBytes = static_cast<UINT>(
                renderable.mesh->indices.size() * sizeof(uint32_t));
            ibv.Format = DXGI_FORMAT_R32_UINT;

            m_commandList->IASetVertexBuffers(0, 1, &vbv);
            m_commandList->IASetIndexBuffer(&ibv);

            m_commandList->DrawIndexedInstanced(
                static_cast<UINT>(renderable.mesh->indices.size()), 1, 0, 0, 0);
        }
    }
}

void Renderer::RenderDepthPrepass(Scene::ECS_Registry* registry) {
    if (!registry || !m_depthBuffer || !m_depthOnlyPipeline) {
        return;
    }

    // Ensure depth buffer is writable for the prepass.
    if (m_depthState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER depthBarrier{};
        depthBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        depthBarrier.Transition.pResource = m_depthBuffer.Get();
        depthBarrier.Transition.StateBefore = m_depthState;
        depthBarrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        depthBarrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &depthBarrier);
        m_depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    // Bind depth stencil only; no color targets for this pass.
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_depthStencilView.cpu;
    m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

    // Clear depth to far plane.
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Set viewport and scissor to match the depth buffer / window.
    D3D12_VIEWPORT viewport{};
    viewport.Width    = static_cast<float>(m_window->GetWidth());
    viewport.Height   = static_cast<float>(m_window->GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect{};
    scissorRect.left   = 0;
    scissorRect.top    = 0;
    scissorRect.right  = static_cast<LONG>(m_window->GetWidth());
    scissorRect.bottom = static_cast<LONG>(m_window->GetHeight());

    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissorRect);

    // Bind root signature and depth-only pipeline.
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_depthOnlyPipeline->GetPipelineState());

    // Frame constants (b1)
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    auto view = registry->View<Scene::RenderableComponent, Scene::TransformComponent>();

    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        auto& transform  = view.get<Scene::TransformComponent>(entity);

        if (!renderable.visible || !renderable.mesh) {
            continue;
        }

        if (!renderable.mesh->gpuBuffers ||
            !renderable.mesh->gpuBuffers->vertexBuffer ||
            !renderable.mesh->gpuBuffers->indexBuffer) {
            continue;
        }

        // Object constants (b0); material/texture data are not needed for a
        // pure depth pass so we skip b2 and descriptor tables.
        ObjectConstants objectData = {};
        objectData.modelMatrix  = transform.GetMatrix();
        objectData.normalMatrix = transform.GetNormalMatrix();

        D3D12_GPU_VIRTUAL_ADDRESS objectCB =
            m_objectConstantBuffer.AllocateAndWrite(objectData);
        m_commandList->SetGraphicsRootConstantBufferView(0, objectCB);

        D3D12_VERTEX_BUFFER_VIEW vbv{};
        vbv.BufferLocation = renderable.mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
        vbv.SizeInBytes    = static_cast<UINT>(renderable.mesh->positions.size() * sizeof(Vertex));
        vbv.StrideInBytes  = sizeof(Vertex);

        D3D12_INDEX_BUFFER_VIEW ibv{};
        ibv.BufferLocation = renderable.mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
        ibv.SizeInBytes    = static_cast<UINT>(renderable.mesh->indices.size() * sizeof(uint32_t));
        ibv.Format         = DXGI_FORMAT_R32_UINT;

        m_commandList->IASetVertexBuffers(0, 1, &vbv);
        m_commandList->IASetIndexBuffer(&ibv);

        m_commandList->DrawIndexedInstanced(
            static_cast<UINT>(renderable.mesh->indices.size()), 1, 0, 0, 0);
    }
}

Result<void> Renderer::UploadMesh(std::shared_ptr<Scene::MeshData> mesh) {
    if (m_deviceRemoved) {
        return Result<void>::Err("DX12 device has been removed; cannot upload mesh");
    }

    if (!mesh) {
        return Result<void>::Err("Invalid mesh pointer");
    }

    if (mesh->positions.empty() || mesh->indices.empty()) {
        return Result<void>::Err("Mesh has no vertex or index data");
    }

    // Interleave vertex data (position, normal, tangent, texcoord)
    std::vector<Vertex> vertices;
    vertices.reserve(mesh->positions.size());

    // Generate tangents for normal mapping
    std::vector<glm::vec3> tangents(mesh->positions.size(), glm::vec3(0.0f));
    std::vector<glm::vec3> bitangents(mesh->positions.size(), glm::vec3(0.0f));
    for (size_t i = 0; i + 2 < mesh->indices.size(); i += 3) {
        const uint32_t i0 = mesh->indices[i + 0];
        const uint32_t i1 = mesh->indices[i + 1];
        const uint32_t i2 = mesh->indices[i + 2];

        const glm::vec3& p0 = mesh->positions[i0];
        const glm::vec3& p1 = mesh->positions[i1];
        const glm::vec3& p2 = mesh->positions[i2];

        const glm::vec2 uv0 = i0 < mesh->texCoords.size() ? mesh->texCoords[i0] : glm::vec2(0.0f);
        const glm::vec2 uv1 = i1 < mesh->texCoords.size() ? mesh->texCoords[i1] : glm::vec2(0.0f);
        const glm::vec2 uv2 = i2 < mesh->texCoords.size() ? mesh->texCoords[i2] : glm::vec2(0.0f);

        const glm::vec3 edge1 = p1 - p0;
        const glm::vec3 edge2 = p2 - p0;
        const glm::vec2 dUV1 = uv1 - uv0;
        const glm::vec2 dUV2 = uv2 - uv0;

        const float denom = (dUV1.x * dUV2.y - dUV1.y * dUV2.x);
        if (std::abs(denom) < 1e-6f) {
            continue;
        }
        const float f = 1.0f / denom;
        // Standard tangent/bitangent from partial derivatives to preserve handedness for mirrored UVs
        glm::vec3 tangent = f * (edge1 * dUV2.y - edge2 * dUV1.y);
        glm::vec3 bitangent = f * (edge2 * dUV1.x - edge1 * dUV2.x);

        tangents[i0] += tangent;
        tangents[i1] += tangent;
        tangents[i2] += tangent;

        bitangents[i0] += bitangent;
        bitangents[i1] += bitangent;
        bitangents[i2] += bitangent;
    }

    for (size_t i = 0; i < mesh->positions.size(); ++i) {
        Vertex v;
        v.position = mesh->positions[i];
        v.normal = i < mesh->normals.size() ? mesh->normals[i] : glm::vec3(0, 1, 0);
        glm::vec3 tangent = tangents[i];
        glm::vec3 bitangent = bitangents[i];
        if (glm::length2(tangent) < 1e-6f) {
            // Build an arbitrary orthogonal tangent if UVs were degenerate
            glm::vec3 up = std::abs(v.normal.y) > 0.9f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
            tangent = glm::normalize(glm::cross(up, v.normal));
            bitangent = glm::cross(v.normal, tangent);
        } else {
            tangent = glm::normalize(tangent - v.normal * glm::dot(v.normal, tangent));
        }
        float sign = 1.0f;
        if (glm::length2(bitangent) > 1e-6f) {
            sign = glm::dot(glm::cross(v.normal, tangent), glm::normalize(bitangent)) < 0.0f ? -1.0f : 1.0f;
        }
        v.tangent = glm::vec4(tangent, sign);
        v.texCoord = i < mesh->texCoords.size() ? mesh->texCoords[i] : glm::vec2(0, 0);
        vertices.push_back(v);
    }

    auto* device = m_device ? m_device->GetDevice() : nullptr;
    if (!device || !m_commandQueue) {
        return Result<void>::Err("Renderer is not initialized");
    }

    const UINT64 vertexCount = static_cast<UINT64>(vertices.size());
    const UINT64 indexCount  = static_cast<UINT64>(mesh->indices.size());
    const UINT64 vbSize = vertexCount * static_cast<UINT64>(sizeof(Vertex));
    const UINT64 ibSize = indexCount  * static_cast<UINT64>(sizeof(uint32_t));

    if (vbSize == 0 || ibSize == 0) {
        spdlog::error(
            "UploadMesh called with empty geometry: vertices={} indices={}",
            static_cast<uint64_t>(vertexCount),
            static_cast<uint64_t>(indexCount));
        return Result<void>::Err("Mesh has no vertices or indices");
    }

    // Log per-mesh GPU buffer footprint to help diagnose large assets.
    const double vbMB = static_cast<double>(vbSize) / (1024.0 * 1024.0);
    const double ibMB = static_cast<double>(ibSize) / (1024.0 * 1024.0);
    spdlog::info(
        "UploadMesh: vertices={} indices={} (VB≈{:.2f} MB, IB≈{:.2f} MB)",
        static_cast<uint64_t>(vertexCount),
        static_cast<uint64_t>(indexCount),
        vbMB,
        ibMB);

    // Hard guardrails for pathological meshes so a single glTF cannot
    // allocate multi-GB vertex/index buffers and trigger device-removed.
    constexpr UINT64 kMaxMeshVertices = 10'000'000ull;  // ~10M verts
    constexpr UINT64 kMaxMeshIndices  = 30'000'000ull;  // ~10M tris
    constexpr UINT64 kMaxMeshVBBytes  = 512ull * 1024ull * 1024ull; // ~512 MB
    constexpr UINT64 kMaxMeshIBBytes  = 512ull * 1024ull * 1024ull; // ~512 MB

    if (vertexCount > kMaxMeshVertices ||
        indexCount  > kMaxMeshIndices  ||
        vbSize      > kMaxMeshVBBytes  ||
        ibSize      > kMaxMeshIBBytes) {
        spdlog::error(
            "UploadMesh: mesh exceeds conservative GPU upload budget; "
            "skipping upload to avoid device-removed (verts={} indices={} VB≈{:.2f} MB IB≈{:.2f} MB)",
            static_cast<uint64_t>(vertexCount),
            static_cast<uint64_t>(indexCount),
            vbMB,
            ibMB);
        return Result<void>::Err("Mesh exceeds GPU upload size budget; not uploaded");
    }

    // For robustness on 8 GB-class GPUs, keep mesh vertex/index buffers in
    // UPLOAD heap memory. This avoids additional copy/transition command
    // lists during scene builds, removing a common source of device-removed
    // faults while the renderer is under active development. The cost is a
    // modest reduction in peak geometry throughput, which is acceptable for
    // the current content size.
    D3D12_HEAP_PROPERTIES uploadHeap = {};
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    uploadHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    uploadHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    uploadHeap.CreationNodeMask = 1;
    uploadHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = vbSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.Format = DXGI_FORMAT_UNKNOWN;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = ibSize;

    auto gpuBuffers = std::make_shared<MeshBuffers>();

    ComPtr<ID3D12Resource> vertexBuffer;
    HRESULT hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertexBuffer)
    );
    if (FAILED(hr)) {
        spdlog::error(
            "CreateCommittedResource for vertex buffer failed: hr=0x{:08X}, vbSize={}, vertices={}",
            static_cast<unsigned int>(hr),
            vbSize,
            vertices.size());

        CORTEX_REPORT_DEVICE_REMOVED("UploadMesh_CreateVertexBuffer", hr);
        return Result<void>::Err("Failed to create upload-heap vertex buffer");
    }

    ComPtr<ID3D12Resource> indexBuffer;
    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&indexBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create upload-heap index buffer");
    }

    // Copy CPU data directly into the upload-heap GPU buffers.
    D3D12_RANGE readRange = { 0, 0 };
    void* mappedData = nullptr;
    hr = vertexBuffer->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map vertex buffer");
    }
    memcpy(mappedData, vertices.data(), vbSize);
    vertexBuffer->Unmap(0, nullptr);

    hr = indexBuffer->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map index buffer");
    }
    memcpy(mappedData, mesh->indices.data(), ibSize);
    indexBuffer->Unmap(0, nullptr);

    // Store GPU buffers with lifetime tied to mesh
    gpuBuffers->vertexBuffer = vertexBuffer;
    gpuBuffers->indexBuffer = indexBuffer;
    mesh->gpuBuffers = gpuBuffers;

    // Register approximate geometry footprint in the asset registry so the
    // memory inspector can surface heavy meshes, and cache the mapping from
    // MeshData pointer to asset key for later ref-count rebuild / BLAS pruning.
    {
        char keyBuf[64];
        std::snprintf(keyBuf, sizeof(keyBuf), "mesh@%p", static_cast<const void*>(mesh.get()));
        const std::string key(keyBuf);
        m_assetRegistry.RegisterMesh(key, vbSize, ibSize);
        m_meshAssetKeys[mesh.get()] = key;
    }

    // Register geometry with the ray tracing context and enqueue a BLAS build
    // job so RT acceleration structures can converge incrementally. When ray
    // tracing is disabled at runtime we skip BLAS work entirely to avoid
    // consuming acceleration-structure memory on 8 GB-class GPUs.
    if (m_rayTracingSupported && m_rayTracingContext && m_rayTracingEnabled) {
        m_rayTracingContext->RebuildBLASForMesh(mesh);

        GpuJob job{};
        job.type = GpuJobType::BuildBLAS;
        job.blasMeshKey = mesh.get();
        job.label = "BLAS";
        m_gpuJobQueue.push_back(job);
        ++m_pendingBLASJobs;
    }

    spdlog::info("Mesh uploaded to upload heap: {} vertices, {} indices", vertices.size(), mesh->indices.size());
    return Result<void>::Ok();
}

Result<void> Renderer::EnqueueMeshUpload(const std::shared_ptr<Scene::MeshData>& mesh,
                                         const char* label)
{
    if (m_deviceRemoved) {
        return Result<void>::Err("DX12 device has been removed; cannot enqueue mesh upload");
    }

    if (!mesh) {
        return Result<void>::Err("EnqueueMeshUpload called with null mesh");
    }

    GpuJob job;
    job.type = GpuJobType::MeshUpload;
    job.mesh = mesh;
    job.label = label ? label : "MeshUpload";
    m_gpuJobQueue.push_back(std::move(job));
    ++m_pendingMeshJobs;

    return Result<void>::Ok();
}

Result<std::shared_ptr<DX12Texture>> Renderer::LoadTextureFromFile(
    const std::string& path,
    bool useSRGB,
    AssetRegistry::TextureKind kind) {
    if (path.empty()) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Empty texture path");
    }

    if (!m_device || !m_commandQueue || !m_descriptorManager) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Renderer is not initialized");
    }

    // CRITICAL: Check cache first to prevent duplicate texture loads and GPU memory exhaustion
    std::string cacheKey = path + (useSRGB ? "_srgb" : "_linear");
    auto cacheIt = m_textureCache.find(cacheKey);
    if (cacheIt != m_textureCache.end()) {
        return Result<std::shared_ptr<DX12Texture>>::Ok(cacheIt->second);
    }

    // Prefer pre-compressed DDS textures when available so that BCn blocks
    // can be uploaded directly without expanding to RGBA8 in system memory.
    // The compressed path is now hardened (validated mip sizes, single
    // DIRECT queue for copy+barrier) and is enabled by default again so
    // RTShowcase and other hero scenes can use BC7/BC5/BC6H assets.
    constexpr bool kEnableCompressedDDS = true;

    auto getLowerExt = [](const std::string& p) -> std::string {
        auto pos = p.find_last_of('.');
        if (pos == std::string::npos) return {};
        std::string ext = p.substr(pos);
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return ext;
    };

    DX12Texture texture;
    auto ext = getLowerExt(path);
    bool usedCompressedPath = false;

    // If the caller explicitly requested a DDS, always try the compressed
    // path. Otherwise, check for a sibling .dds next to the requested file
    // so assets can be converted incrementally without touching call sites.
    if (kEnableCompressedDDS && ext == ".dds") {
        auto ddsResult = TextureLoader::LoadDDSCompressed(path);
        if (!ddsResult.IsErr()) {
            const auto& img = ddsResult.Value();

            auto toDXGI = [](TextureLoader::CompressedFormat fmt) -> DXGI_FORMAT {
                using F = TextureLoader::CompressedFormat;
                switch (fmt) {
                case F::BC1_UNORM:        return DXGI_FORMAT_BC1_UNORM;
                case F::BC1_UNORM_SRGB:   return DXGI_FORMAT_BC1_UNORM_SRGB;
                case F::BC3_UNORM:        return DXGI_FORMAT_BC3_UNORM;
                case F::BC3_UNORM_SRGB:   return DXGI_FORMAT_BC3_UNORM_SRGB;
                case F::BC5_UNORM:        return DXGI_FORMAT_BC5_UNORM;
                case F::BC6H_UF16:        return DXGI_FORMAT_BC6H_UF16;
                case F::BC7_UNORM:        return DXGI_FORMAT_BC7_UNORM;
                case F::BC7_UNORM_SRGB:   return DXGI_FORMAT_BC7_UNORM_SRGB;
                default:                  return DXGI_FORMAT_UNKNOWN;
                }
            };

            DXGI_FORMAT compressedFormat = toDXGI(img.format);
            if (compressedFormat != DXGI_FORMAT_UNKNOWN) {
                auto initCompressed = texture.InitializeFromCompressedMipChain(
                    m_device->GetDevice(),
                    m_uploadQueue ? m_uploadQueue->GetCommandQueue() : nullptr,
                    m_commandQueue->GetCommandQueue(),
                    img.mipData,
                    img.width,
                    img.height,
                    compressedFormat,
                    path
                );
                if (!initCompressed.IsErr()) {
                    usedCompressedPath = true;
                } else {
                    spdlog::warn("Failed to initialize compressed texture '{}': {}", path, initCompressed.Error());
                }
            } else {
                spdlog::warn("Unsupported compressed DDS format for '{}'", path);
            }
        } else {
            spdlog::warn("Failed to load compressed DDS '{}': {}", path, ddsResult.Error());
        }
    } else {
        // Prefer compressed sibling if present: <name>.dds next to the source.
        if (kEnableCompressedDDS) {
        std::filesystem::path original(path);
        std::filesystem::path sibling = original;
        sibling.replace_extension(".dds");
        if (std::filesystem::exists(sibling)) {
            std::string siblingStr = sibling.string();
            auto ddsResult = TextureLoader::LoadDDSCompressed(siblingStr);
            if (!ddsResult.IsErr()) {
                const auto& img = ddsResult.Value();

                auto toDXGI = [](TextureLoader::CompressedFormat fmt) -> DXGI_FORMAT {
                    using F = TextureLoader::CompressedFormat;
                    switch (fmt) {
                    case F::BC1_UNORM:        return DXGI_FORMAT_BC1_UNORM;
                    case F::BC1_UNORM_SRGB:   return DXGI_FORMAT_BC1_UNORM_SRGB;
                    case F::BC3_UNORM:        return DXGI_FORMAT_BC3_UNORM;
                    case F::BC3_UNORM_SRGB:   return DXGI_FORMAT_BC3_UNORM_SRGB;
                    case F::BC5_UNORM:        return DXGI_FORMAT_BC5_UNORM;
                    case F::BC6H_UF16:        return DXGI_FORMAT_BC6H_UF16;
                    case F::BC7_UNORM:        return DXGI_FORMAT_BC7_UNORM;
                    case F::BC7_UNORM_SRGB:   return DXGI_FORMAT_BC7_UNORM_SRGB;
                    default:                  return DXGI_FORMAT_UNKNOWN;
                    }
                };

                DXGI_FORMAT compressedFormat = toDXGI(img.format);
                if (compressedFormat != DXGI_FORMAT_UNKNOWN) {
                    auto initCompressed = texture.InitializeFromCompressedMipChain(
                        m_device->GetDevice(),
                        nullptr, // use graphics queue for copy + transitions
                        m_commandQueue->GetCommandQueue(),
                        img.mipData,
                        img.width,
                        img.height,
                        compressedFormat,
                        siblingStr
                    );
                    if (!initCompressed.IsErr()) {
                        usedCompressedPath = true;
                        spdlog::info("Loaded compressed sibling '{}' for texture '{}'", siblingStr, path);
                    } else {
                        spdlog::warn("Failed to initialize compressed sibling '{}' for '{}': {}; falling back to RGBA path",
                                     siblingStr, path, initCompressed.Error());
                    }
                } else {
                    spdlog::warn("Unsupported compressed DDS format for sibling '{}' (source '{}'); falling back to RGBA path",
                                 siblingStr, path);
                }
            } else {
                spdlog::warn("Failed to load compressed sibling '{}' for '{}': {}; falling back to RGBA path",
                             siblingStr, path, ddsResult.Error());
            }
            }
        }
    }

    // If compressed loading failed or was not requested, fall back to the
    // generic RGBA path. DDS files are handled exclusively via the
    // compressed loader; if that fails we deliberately fall back to a
    // placeholder instead of sending .dds through stb_image (which just
    // spams load failures).
    if (!usedCompressedPath) {
        auto imagePath = path;
        std::string extLower = getLowerExt(path);
        if (extLower == ".dds") {
            // Placeholder-only fallback for DDS when compressed loading
            // fails; return a small white texture so materials remain
            // renderable without spamming errors every frame.
            float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            auto placeholderResult = DX12Texture::CreatePlaceholder(
                m_device->GetDevice(),
                nullptr,
                m_commandQueue->GetCommandQueue(),
                2,
                2,
                white);
            if (placeholderResult.IsErr()) {
                return Result<std::shared_ptr<DX12Texture>>::Err(
                    "Failed to create placeholder texture for DDS '" + path +
                    "': " + placeholderResult.Error());
            }
            texture = std::move(placeholderResult.Value());
        } else {
            auto imageResult = TextureLoader::LoadImageRGBAWithMips(imagePath, true);
            if (imageResult.IsErr()) {
                return Result<std::shared_ptr<DX12Texture>>::Err(imageResult.Error());
            }

            std::vector<std::vector<uint8_t>> mipData;
            uint32_t width = imageResult.Value().front().width;
            uint32_t height = imageResult.Value().front().height;
            for (const auto& mip : imageResult.Value()) {
                mipData.push_back(mip.pixels);
            }
            auto initResult = texture.InitializeFromMipChain(
                m_device->GetDevice(),
                nullptr, // use graphics queue for copy + transitions
                m_commandQueue->GetCommandQueue(),
                mipData,
                width,
                height,
                useSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM,
                imagePath
            );
            if (initResult.IsErr()) {
                return Result<std::shared_ptr<DX12Texture>>::Err(initResult.Error());
            }
        }
    }

    // Use staging heap for persistent texture SRVs (will be copied to shader-visible heap)
    auto srvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Failed to allocate staging SRV for texture " + path + ": " + srvResult.Error());
    }

    auto createResult = texture.CreateSRV(m_device->GetDevice(), srvResult.Value());
    if (createResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(createResult.Error());
    }

    auto texPtr = std::make_shared<DX12Texture>(std::move(texture));

    // Approximate per-texture GPU memory footprint and register with the
    // asset registry for diagnostics. This is intentionally conservative.
    auto estimateTextureBytes = [](uint32_t width,
                                   uint32_t height,
                                   uint32_t mipLevels,
                                   DXGI_FORMAT format) -> uint64_t {
        if (width == 0 || height == 0 || mipLevels == 0) {
            return 0;
        }

        const auto isBC = [](DXGI_FORMAT fmt) {
            switch (fmt) {
            case DXGI_FORMAT_BC1_UNORM:
            case DXGI_FORMAT_BC1_UNORM_SRGB:
            case DXGI_FORMAT_BC3_UNORM:
            case DXGI_FORMAT_BC3_UNORM_SRGB:
            case DXGI_FORMAT_BC5_UNORM:
            case DXGI_FORMAT_BC6H_UF16:
            case DXGI_FORMAT_BC7_UNORM:
            case DXGI_FORMAT_BC7_UNORM_SRGB:
                return true;
            default:
                return false;
            }
        };

        const auto blockSize = [](DXGI_FORMAT fmt) -> uint32_t {
            switch (fmt) {
            case DXGI_FORMAT_BC1_UNORM:
            case DXGI_FORMAT_BC1_UNORM_SRGB:
                return 8;
            case DXGI_FORMAT_BC3_UNORM:
            case DXGI_FORMAT_BC3_UNORM_SRGB:
            case DXGI_FORMAT_BC5_UNORM:
            case DXGI_FORMAT_BC6H_UF16:
            case DXGI_FORMAT_BC7_UNORM:
            case DXGI_FORMAT_BC7_UNORM_SRGB:
                return 16;
            default:
                return 16;
            }
        };

        const auto bytesPerPixel = [](DXGI_FORMAT fmt) -> uint32_t {
            switch (fmt) {
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
                return 4;
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
            case DXGI_FORMAT_R16G16B16A16_UNORM:
                return 8;
            default:
                return 4;
            }
        };

        uint64_t total = 0;
        uint32_t w = width;
        uint32_t h = height;
        for (uint32_t mip = 0; mip < mipLevels; ++mip) {
            if (isBC(format)) {
                const uint32_t bw = (w + 3u) / 4u;
                const uint32_t bh = (h + 3u) / 4u;
                total += static_cast<uint64_t>(bw) * static_cast<uint64_t>(bh) * blockSize(format);
            } else {
                total += static_cast<uint64_t>(w) * static_cast<uint64_t>(h) * bytesPerPixel(format);
            }
            w = std::max(1u, w >> 1);
            h = std::max(1u, h >> 1);
        }
        return total;
    };

    const uint64_t bytes = estimateTextureBytes(
        texPtr->GetWidth(),
        texPtr->GetHeight(),
        texPtr->GetMipLevels(),
        texPtr->GetFormat());
    if (bytes > 0) {
        m_assetRegistry.RegisterTexture(path, bytes, kind);
    }

    // Register in bindless heap for SM6.6 ResourceDescriptorHeap access
    if (m_bindlessManager && texPtr->GetResource()) {
        auto bindlessResult = texPtr->CreateBindlessSRV(m_bindlessManager.get());
        if (bindlessResult.IsErr()) {
            spdlog::warn("Failed to register texture '{}' in bindless heap: {}", path, bindlessResult.Error());
        }
    }

    // CRITICAL: Add to cache to prevent duplicate loads
    m_textureCache[cacheKey] = texPtr;

    return Result<std::shared_ptr<DX12Texture>>::Ok(texPtr);
}

Result<std::shared_ptr<DX12Texture>> Renderer::CreateTextureFromRGBA(
    const uint8_t* data,
    uint32_t width,
    uint32_t height,
    bool useSRGB,
    const std::string& debugName)
{
    if (!data || width == 0 || height == 0) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Invalid texture data for Dreamer texture");
    }

    if (!m_device || !m_commandQueue || !m_descriptorManager) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Renderer is not initialized");
    }

    DX12Texture texture;
    auto initResult = texture.InitializeFromData(
        m_device->GetDevice(),
        nullptr, // use graphics queue for copy + transitions
        m_commandQueue->GetCommandQueue(),
        data,
        width,
        height,
        useSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM,
        debugName
    );
    if (initResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(initResult.Error());
    }

    // Use staging heap for persistent Dreamer texture SRVs
    auto srvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(
            "Failed to allocate staging SRV for Dreamer texture '" + debugName + "': " + srvResult.Error());
    }

    auto createResult = texture.CreateSRV(m_device->GetDevice(), srvResult.Value());
    if (createResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(createResult.Error());
    }

    auto texPtr = std::make_shared<DX12Texture>(std::move(texture));

    // Register in bindless heap for SM6.6 ResourceDescriptorHeap access
    if (m_bindlessManager && texPtr->GetResource()) {
        auto bindlessResult = texPtr->CreateBindlessSRV(m_bindlessManager.get());
        if (bindlessResult.IsErr()) {
            spdlog::warn("Failed to register Dreamer texture '{}' in bindless heap: {}", debugName, bindlessResult.Error());
        }
    }

    return Result<std::shared_ptr<DX12Texture>>::Ok(texPtr);
}

void Renderer::ToggleShadows() {
    m_shadowsEnabled = !m_shadowsEnabled;
    spdlog::info("Shadows {}", m_shadowsEnabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetTAAEnabled(bool enabled) {
    if (m_taaEnabled == enabled) {
        return;
    }
    m_taaEnabled = enabled;
    // When toggling TAA, reset sample index so the Halton sequence
    // restarts cleanly and avoid sudden large jumps in jitter.
    m_taaSampleIndex = 0;
    m_taaJitterPrevPixels = glm::vec2(0.0f);
    m_taaJitterCurrPixels = glm::vec2(0.0f);
    // Force history to be re-seeded on the next frame so we do not mix
    // incompatible LDR/HDR or pre/post-teleport data.
    m_hasHistory = false;
    spdlog::info("TAA {}", m_taaEnabled ? "ENABLED" : "DISABLED");
}

void Renderer::ToggleTAA() {
    SetTAAEnabled(!m_taaEnabled);
}

void Renderer::SetSSREnabled(bool enabled) {
    if (m_ssrEnabled == enabled) {
        return;
    }
    m_ssrEnabled = enabled;
    spdlog::info("SSR {}", m_ssrEnabled ? "ENABLED" : "DISABLED");
}

void Renderer::ToggleSSR() {
    SetSSREnabled(!m_ssrEnabled);
}

void Renderer::CycleScreenSpaceEffectsDebug() {
    // Determine current state from flags:
    // 0 = both on, 1 = SSR only, 2 = SSAO only, 3 = both off
    uint32_t state = 0;
    if (m_ssrEnabled && m_ssaoEnabled) {
        state = 0;
    } else if (m_ssrEnabled && !m_ssaoEnabled) {
        state = 1;
    } else if (!m_ssrEnabled && m_ssaoEnabled) {
        state = 2;
    } else {
        state = 3;
    }

    uint32_t next = (state + 1u) % 4u;
    bool ssrOn = (next == 0u || next == 1u);
    bool ssaoOn = (next == 0u || next == 2u);

    SetSSREnabled(ssrOn);
    SetSSAOEnabled(ssaoOn);

    const char* label = nullptr;
    switch (next) {
        case 0: label = "Both SSR and SSAO ENABLED"; break;
        case 1: label = "SSR ONLY (SSAO disabled)"; break;
        case 2: label = "SSAO ONLY (SSR disabled)"; break;
        case 3: label = "Both SSR and SSAO DISABLED"; break;
        default: label = "Unknown"; break;
    }
    spdlog::info("Screen-space effects debug state: {}", label);
}

void Renderer::SetFogEnabled(bool enabled) {
    if (m_fogEnabled == enabled) {
        return;
    }
    m_fogEnabled = enabled;
    spdlog::info("Fog {}", m_fogEnabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetFogParams(float density, float height, float falloff) {
    float d = std::max(density, 0.0f);
    float f = std::max(falloff, 0.0f);
    if (std::abs(d - m_fogDensity) < 1e-6f &&
        std::abs(height - m_fogHeight) < 1e-6f &&
        std::abs(f - m_fogFalloff) < 1e-6f) {
        return;
    }
    m_fogDensity = d;
    m_fogHeight = height;
    m_fogFalloff = f;
    spdlog::info("Fog params: density={}, height={}, falloff={}", m_fogDensity, m_fogHeight, m_fogFalloff);
}

void Renderer::SetGodRayIntensity(float intensity) {
    float clamped = glm::clamp(intensity, 0.0f, 5.0f);
    if (std::abs(clamped - m_godRayIntensity) < 1e-3f) {
        return;
    }
    m_godRayIntensity = clamped;
    spdlog::info("God-ray intensity set to {}", m_godRayIntensity);
}

void Renderer::SetAreaLightSizeScale(float scale) {
    float clamped = glm::clamp(scale, 0.25f, 4.0f);
    if (std::abs(clamped - m_areaLightSizeScale) < 1e-3f) {
        return;
    }
    m_areaLightSizeScale = clamped;
    spdlog::info("Area light size scale set to {}", m_areaLightSizeScale);
}

void Renderer::CycleDebugViewMode() {
    // 0  = shaded, 1 = normals, 2 = roughness, 3 = metallic, 4 = albedo,
    // 5  = cascades, 6  = debug screen (post-process / HUD focus),
    // 7  = fractal height,
    // 8  = IBL diffuse only, 9  = IBL specular only, 10 = env direction/UV,
    // 11 = Fresnel (Fibl), 12 = specular mip debug,
    // 13 = SSAO only, 14 = SSAO overlay, 15 = SSR only, 16 = SSR overlay,
    // 17 = forward light debug (heatmap / count),
    // 18 = RT shadow mask debug, 19 = RT shadow history debug,
    // 20 = RT reflection buffer debug (post-process),
    // 21 = RT GI buffer debug,
    // 22 = shaded with RT GI disabled,
    // 23 = shaded with RT reflections disabled (SSR only),
    // 24 = SDF debug / RT reflection ray direction,
    // 25 = TAA history weight debug,
    // 26 = material layer debug (coat / sheen / SSS),
    // 27 = anisotropy debug,
    // 28 = fog factor debug,
    // 29 = water debug.
    m_debugViewMode = (m_debugViewMode + 1) % 30;
    const char* label = nullptr;
    switch (m_debugViewMode) {
        case 0: label = "Shaded"; break;
        case 1: label = "Normals"; break;
        case 2: label = "Roughness"; break;
        case 3: label = "Metallic"; break;
        case 4: label = "Albedo"; break;
        case 5: label = "Cascades"; break;
        case 6: label = "DebugScreen"; break;
        case 7: label = "FractalHeight"; break;
        case 8: label = "IBL_Diffuse"; break;
        case 9: label = "IBL_Specular"; break;
        case 10: label = "EnvDirection"; break;
        case 11: label = "Fresnel"; break;
        case 12: label = "SpecularMip"; break;
        case 13: label = "SSAO_Only"; break;
        case 14: label = "SSAO_Overlay"; break;
        case 15: label = "SSR_Only"; break;
        case 16: label = "SSR_Overlay"; break;
        case 17: label = "Light_Debug"; break;
        case 18: label = "RT_ShadowMask"; break;
        case 19: label = "RT_ShadowHistory"; break;
        case 20: label = "RT_ReflectionBuffer"; break;
        case 21: label = "RT_GI_Buffer"; break;
        case 22: label = "Shaded_NoRTGI"; break;
        case 23: label = "Shaded_NoRTRefl"; break;
        case 24: label = "SDF_Debug"; break;
        case 25: label = "TAA_HistoryWeight"; break;
        case 26: label = "MaterialLayers"; break;
        case 27: label = "Anisotropy_Debug"; break;
        case 28: label = "Fog_Factor"; break;
        case 29: label = "Water_Debug"; break;
        default: label = "Unknown"; break;
    }
    spdlog::info("Debug view mode: {}", label);
}

void Renderer::AdjustShadowBias(float delta) {
    m_shadowBias = glm::clamp(m_shadowBias + delta, 0.00001f, 0.01f);
    spdlog::info("Shadow bias set to {}", m_shadowBias);
}

void Renderer::AdjustShadowPCFRadius(float delta) {
    m_shadowPCFRadius = glm::clamp(m_shadowPCFRadius + delta, 0.5f, 8.0f);
    spdlog::info("Shadow PCF radius set to {}", m_shadowPCFRadius);
}

void Renderer::AdjustCascadeSplitLambda(float delta) {
    m_cascadeSplitLambda = glm::clamp(m_cascadeSplitLambda + delta, 0.0f, 1.0f);
    spdlog::info("Cascade split lambda set to {}", m_cascadeSplitLambda);
}

void Renderer::AdjustCascadeResolutionScale(uint32_t cascadeIndex, float delta) {
    if (cascadeIndex >= kShadowCascadeCount) {
        return;
    }
    if (std::abs(delta) < 1e-6f) {
        return;
    }
    m_cascadeResolutionScale[cascadeIndex] = glm::clamp(m_cascadeResolutionScale[cascadeIndex] + delta, 0.25f, 2.0f);
    spdlog::info("Cascade {} resolution scale set to {}", cascadeIndex, m_cascadeResolutionScale[cascadeIndex]);
}

void Renderer::SetExposure(float exposure) {
    float clamped = std::max(exposure, 0.01f);
    if (std::abs(clamped - m_exposure) < 1e-6f) {
        return;
    }
    m_exposure = clamped;
    spdlog::info("Renderer exposure set to {}", m_exposure);
}

void Renderer::SetShadowsEnabled(bool enabled) {
    if (m_shadowsEnabled == enabled) {
        return;
    }
    m_shadowsEnabled = enabled;
    spdlog::info("Renderer shadows {}", m_shadowsEnabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetDebugViewMode(int mode) {
    // Clamp to the full range of supported debug modes.
    int clamped = std::max(0, std::min(mode, 25));
    if (static_cast<uint32_t>(clamped) == m_debugViewMode) {
        return;
    }
    m_debugViewMode = static_cast<uint32_t>(clamped);
    spdlog::info("Renderer debug view mode set to {}", clamped);
}

void Renderer::SetShadowBias(float bias) {
    float clamped = glm::clamp(bias, 0.00001f, 0.01f);
    if (std::abs(clamped - m_shadowBias) < 1e-9f) {
        return;
    }
    m_shadowBias = clamped;
    spdlog::info("Renderer shadow bias set to {}", m_shadowBias);
}

void Renderer::SetShadowPCFRadius(float radius) {
    float clamped = glm::clamp(radius, 0.5f, 8.0f);
    if (std::abs(clamped - m_shadowPCFRadius) < 1e-6f) {
        return;
    }
    m_shadowPCFRadius = clamped;
    spdlog::info("Renderer shadow PCF radius set to {}", m_shadowPCFRadius);
}

void Renderer::SetCascadeSplitLambda(float lambda) {
    float clamped = glm::clamp(lambda, 0.0f, 1.0f);
    if (std::abs(clamped - m_cascadeSplitLambda) < 1e-6f) {
        return;
    }
    m_cascadeSplitLambda = clamped;
    spdlog::info("Renderer cascade split lambda set to {}", m_cascadeSplitLambda);
}

void Renderer::SetBloomIntensity(float intensity) {
    float clamped = glm::clamp(intensity, 0.0f, 5.0f);
    if (std::abs(clamped - m_bloomIntensity) < 1e-6f) {
        return;
    }
    m_bloomIntensity = clamped;
    spdlog::info("Renderer bloom intensity set to {}", m_bloomIntensity);
}

void Renderer::SetWaterParams(float levelY, float amplitude, float waveLength, float speed,
                              float dirX, float dirZ, float secondaryAmplitude, float steepness) {
    m_waterLevelY = levelY;
    m_waterWaveAmplitude = amplitude;
    m_waterWaveLength = (waveLength <= 0.0f) ? 1.0f : waveLength;
    m_waterWaveSpeed = speed;
    glm::vec2 dir(dirX, dirZ);
    if (glm::length2(dir) < 1e-4f) {
        dir = glm::vec2(1.0f, 0.0f);
    }
    m_waterPrimaryDir = glm::normalize(dir);
    m_waterSecondaryAmplitude = glm::max(0.0f, secondaryAmplitude);
    m_waterSteepness = glm::clamp(steepness, 0.0f, 1.0f);
}

float Renderer::SampleWaterHeightAt(const glm::vec2& worldXZ) const {
    const float amplitude = m_waterWaveAmplitude;
    const float waveLen   = (m_waterWaveLength <= 0.0f) ? 1.0f : m_waterWaveLength;
    const float speed     = m_waterWaveSpeed;
    const float waterY    = m_waterLevelY;

    glm::vec2 dir = m_waterPrimaryDir;
    if (glm::length2(dir) < 1e-4f) {
        dir = glm::vec2(1.0f, 0.0f);
    } else {
        dir = glm::normalize(dir);
    }
    const glm::vec2 dir2(-dir.y, dir.x);

    const float k = 2.0f * glm::pi<float>() / waveLen;
    const float t = m_totalTime;

    const float phase0 = glm::dot(dir, worldXZ) * k + speed * t;
    const float h0 = amplitude * std::sin(phase0);

    const float phase1 = glm::dot(dir2, worldXZ) * k * 1.3f + speed * 0.8f * t;
    const float h1 = m_waterSecondaryAmplitude * std::sin(phase1);

    return waterY + h0 + h1;
}
void Renderer::SetRayTracingEnabled(bool enabled) {
    bool newValue = enabled && m_rayTracingSupported;
    if (m_rayTracingEnabled == newValue) {
        return;
    }
    if (enabled && !m_rayTracingSupported) {
        spdlog::info("Ray tracing toggle requested, but DXR is not supported on this device.");
        return;
    }
    m_rayTracingEnabled = newValue;
    spdlog::info("Ray tracing {}", m_rayTracingEnabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetFractalParams(float amplitude, float frequency, float octaves,
                                float coordMode, float scaleX, float scaleZ,
                                float lacunarity, float gain,
                                float warpStrength, float noiseType) {
    float amp = glm::clamp(amplitude, 0.0f, 0.5f);
    float freq = glm::clamp(frequency, 0.1f, 4.0f);
    float oct = glm::clamp(octaves, 1.0f, 6.0f);
    float mode = (coordMode >= 0.5f) ? 1.0f : 0.0f;
    float sx = glm::clamp(scaleX, 0.1f, 4.0f);
    float sz = glm::clamp(scaleZ, 0.1f, 4.0f);
    float lac = glm::clamp(lacunarity, 1.0f, 4.0f);
    float gn = glm::clamp(gain, 0.1f, 0.9f);
    float warp = glm::clamp(warpStrength, 0.0f, 1.0f);
    int nt = static_cast<int>(noiseType + 0.5f);
    if (nt < 0) nt = 0;
    if (nt > 3) nt = 3;

    if (std::abs(amp - m_fractalAmplitude) < 1e-6f &&
        std::abs(freq - m_fractalFrequency) < 1e-6f &&
        std::abs(oct - m_fractalOctaves) < 1e-6f &&
        std::abs(mode - m_fractalCoordMode) < 1e-6f &&
        std::abs(sx - m_fractalScaleX) < 1e-6f &&
        std::abs(sz - m_fractalScaleZ) < 1e-6f &&
        std::abs(lac - m_fractalLacunarity) < 1e-6f &&
        std::abs(gn - m_fractalGain) < 1e-6f &&
        std::abs(warp - m_fractalWarpStrength) < 1e-6f &&
        nt == static_cast<int>(m_fractalNoiseType + 0.5f)) {
        return;
    }

    m_fractalAmplitude = amp;
    m_fractalFrequency = freq;
    m_fractalOctaves = oct;
    m_fractalCoordMode = mode;
    m_fractalScaleX = sx;
    m_fractalScaleZ = sz;
    m_fractalLacunarity = lac;
    m_fractalGain = gn;
    m_fractalWarpStrength = warp;
    m_fractalNoiseType = static_cast<float>(nt);

    const char* typeLabel = (nt == 0) ? "FBM" : (nt == 1 ? "Ridged" : (nt == 2 ? "Turbulence" : "Cellular"));
    spdlog::info("Fractal params: amp={} freq={} oct={} mode={} scale=({}, {}), lacunarity={}, gain={}, warp={}, type={}",
                 m_fractalAmplitude, m_fractalFrequency, m_fractalOctaves,
                 (m_fractalCoordMode > 0.5f ? "WorldXZ" : "UV"),
                 m_fractalScaleX, m_fractalScaleZ,
                 m_fractalLacunarity, m_fractalGain, m_fractalWarpStrength, typeLabel);
}

void Renderer::ApplyLightingRig(LightingRig rig, Scene::ECS_Registry* registry) {
    if (!registry) {
        spdlog::warn("ApplyLightingRig called with null registry");
        return;
    }

    // Clear existing non-directional lights so rigs start from a known state.
    auto& enttReg = registry->GetRegistry();
    {
        auto view = enttReg.view<Scene::LightComponent>();
        std::vector<entt::entity> toDestroy;
        for (auto entity : view) {
            const auto& light = view.get<Scene::LightComponent>(entity);
            if (light.type == Scene::LightType::Directional) {
                continue;
            }
            toDestroy.push_back(entity);
        }
        for (auto e : toDestroy) {
            enttReg.destroy(e);
        }
    }

    // Reset global sun/ambient to reasonable defaults for each rig; this keeps
    // behavior stable even if previous state was extreme.
    m_directionalLightDirection = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
    m_directionalLightColor = glm::vec3(1.0f);
    m_directionalLightIntensity = 5.0f;
    m_ambientLightColor = glm::vec3(0.04f);
    m_ambientLightIntensity = 1.0f;

    // On 8 GB-class adapters, optionally select a "safe" variant of each rig
    // with reduced intensities and fewer local shadow-casting lights. This
    // helps keep RTShowcase and other heavy scenes within budget.
    bool useSafeRig = false;
    if (m_device && m_useSafeLightingRigOnLowVRAM) {
        const std::uint64_t bytes = m_device->GetDedicatedVideoMemoryBytes();
        const std::uint64_t mb = bytes / (1024ull * 1024ull);
        if (mb > 0 && mb <= 8192ull) {
            useSafeRig = true;
        }
    }

    switch (rig) {
    case LightingRig::Custom:
        spdlog::info("Lighting rig: Custom (no preset applied)");
        return;

    case LightingRig::StudioThreePoint: {
        // Key light - strong, warm spotlight from front-right
        {
            entt::entity e = enttReg.create();
            enttReg.emplace<Scene::TagComponent>(e, "KeyLight");
            auto& t = enttReg.emplace<Scene::TransformComponent>(e);
            t.position = glm::vec3(3.0f, 4.0f, -4.0f);
            glm::vec3 dir = glm::normalize(glm::vec3(-0.6f, -0.8f, 0.7f));
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(up, dir)) > 0.99f) {
                up = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            t.rotation = glm::quatLookAt(dir, up);

            auto& l = enttReg.emplace<Scene::LightComponent>(e);
            l.type = Scene::LightType::Spot;
            l.color = glm::vec3(1.0f, 0.95f, 0.85f);
            l.intensity = useSafeRig ? 10.0f : 14.0f;
            l.range = useSafeRig ? 18.0f : 25.0f;
            l.innerConeDegrees = 20.0f;
            l.outerConeDegrees = 35.0f;
            l.castsShadows = true;
        }
        // Fill light - softer, cooler point light from front-left
        {
            entt::entity e = enttReg.create();
            enttReg.emplace<Scene::TagComponent>(e, "FillLight");
            auto& t = enttReg.emplace<Scene::TransformComponent>(e);
            t.position = glm::vec3(-3.0f, 2.0f, -3.0f);

            auto& l = enttReg.emplace<Scene::LightComponent>(e);
            l.type = Scene::LightType::Point;
            l.color = glm::vec3(0.8f, 0.85f, 1.0f);
            l.intensity = useSafeRig ? 3.0f : 5.0f;
            l.range = useSafeRig ? 14.0f : 20.0f;
            l.castsShadows = false;
        }
        // Rim light - dimmer spotlight from behind
        {
            entt::entity e = enttReg.create();
            enttReg.emplace<Scene::TagComponent>(e, "RimLight");
            auto& t = enttReg.emplace<Scene::TransformComponent>(e);
            t.position = glm::vec3(0.0f, 3.0f, 4.0f);
            glm::vec3 dir = glm::normalize(glm::vec3(0.0f, -0.5f, -1.0f));
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(up, dir)) > 0.99f) {
                up = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            t.rotation = glm::quatLookAt(dir, up);

            auto& l = enttReg.emplace<Scene::LightComponent>(e);
            l.type = Scene::LightType::Spot;
            l.color = glm::vec3(0.9f, 0.9f, 1.0f);
            l.intensity = useSafeRig ? 5.0f : 8.0f;
            l.range = useSafeRig ? 18.0f : 25.0f;
            l.innerConeDegrees = 25.0f;
            l.outerConeDegrees = 40.0f;
            l.castsShadows = false;
        }
        spdlog::info("Applied lighting rig: StudioThreePoint");
        break;
    }

    case LightingRig::TopDownWarehouse: {
        // Cooler sun, higher ambient, and a grid of overhead point lights.
        m_directionalLightDirection = glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f));
        m_directionalLightColor = glm::vec3(0.9f, 0.95f, 1.0f);
        m_directionalLightIntensity = useSafeRig ? 2.5f : 3.5f;
        m_ambientLightColor = glm::vec3(0.08f, 0.09f, 0.1f);
        m_ambientLightIntensity = useSafeRig ? 1.0f : 1.5f;

        const int countX = 3;
        const int countZ = 3;
        const float spacing = 6.0f;
        const float startX = -spacing;
        const float startZ = -spacing;
        int index = 0;

        for (int ix = 0; ix < countX; ++ix) {
            for (int iz = 0; iz < countZ; ++iz) {
                entt::entity e = enttReg.create();
                std::string name = "WarehouseLight_" + std::to_string(index++);
                enttReg.emplace<Scene::TagComponent>(e, name);

                auto& t = enttReg.emplace<Scene::TransformComponent>(e);
                t.position = glm::vec3(startX + ix * spacing, 8.0f, startZ + iz * spacing);

                auto& l = enttReg.emplace<Scene::LightComponent>(e);
                l.type = Scene::LightType::Point;
                l.color = glm::vec3(0.9f, 0.95f, 1.0f);
                l.intensity = useSafeRig ? 7.0f : 10.0f;
                l.range = useSafeRig ? 8.0f : 10.0f;
                // On safe rigs keep the center light unshadowed; rely on
                // cascades and ambient for structure.
                l.castsShadows = (!useSafeRig && ix == 1 && iz == 1);
            }
        }
        spdlog::info("Applied lighting rig: TopDownWarehouse");
        break;
    }

    case LightingRig::HorrorSideLight: {
        // Reduce ambient and use a single harsh side light plus a dim back fill.
        m_directionalLightDirection = glm::normalize(glm::vec3(-0.2f, 1.0f, 0.0f));
        m_directionalLightColor = glm::vec3(0.8f, 0.7f, 0.6f);
        m_directionalLightIntensity = useSafeRig ? 1.5f : 2.0f;
        m_ambientLightColor = glm::vec3(0.01f, 0.01f, 0.02f);
        m_ambientLightIntensity = useSafeRig ? 0.4f : 0.5f;

        // Strong side spotlight
        {
            entt::entity e = enttReg.create();
            enttReg.emplace<Scene::TagComponent>(e, "HorrorKey");
            auto& t = enttReg.emplace<Scene::TransformComponent>(e);
            t.position = glm::vec3(-5.0f, 2.0f, 0.0f);
            glm::vec3 dir = glm::normalize(glm::vec3(1.0f, -0.2f, 0.1f));
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(up, dir)) > 0.99f) {
                up = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            t.rotation = glm::quatLookAt(dir, up);

            auto& l = enttReg.emplace<Scene::LightComponent>(e);
            l.type = Scene::LightType::Spot;
            l.color = glm::vec3(1.0f, 0.85f, 0.7f);
            l.intensity = useSafeRig ? 13.0f : 18.0f;
            l.range = useSafeRig ? 16.0f : 20.0f;
            l.innerConeDegrees = 18.0f;
            l.outerConeDegrees = 30.0f;
            l.castsShadows = true;
        }

        // Dim back fill so the dark side isn't completely black
        {
            entt::entity e = enttReg.create();
            enttReg.emplace<Scene::TagComponent>(e, "HorrorFill");
            auto& t = enttReg.emplace<Scene::TransformComponent>(e);
            t.position = glm::vec3(3.0f, 1.5f, -4.0f);

            auto& l = enttReg.emplace<Scene::LightComponent>(e);
            l.type = Scene::LightType::Point;
            l.color = glm::vec3(0.4f, 0.5f, 0.8f);
            l.intensity = useSafeRig ? 2.0f : 3.0f;
            l.range = useSafeRig ? 8.0f : 10.0f;
            l.castsShadows = false;
        }

        spdlog::info("Applied lighting rig: HorrorSideLight");
        break;
    }

    case LightingRig::StreetLanterns: {
        // Night-time street / alley rig: dim directional light, subtle ambient,
        // and a row of strong warm street lanterns that actually light the
        // environment. A subset of lights cast shadows to keep performance
        // reasonable while still giving good occlusion cues.
        m_directionalLightDirection = glm::normalize(glm::vec3(-0.1f, -1.0f, 0.1f));
        m_directionalLightColor = glm::vec3(0.5f, 0.55f, 0.65f);
        m_directionalLightIntensity = useSafeRig ? 1.0f : 1.5f;
        m_ambientLightColor = glm::vec3(0.02f, 0.03f, 0.05f);
        m_ambientLightIntensity = useSafeRig ? 0.5f : 0.7f;

        const int lightCount = 8;
        const float spacing = 7.5f;
        const float startX = -((lightCount - 1) * spacing * 0.5f);
        const float zPos = -6.0f;
        const float height = 5.0f;

        for (int i = 0; i < lightCount; ++i) {
            entt::entity e = enttReg.create();
            std::string name = "StreetLantern_" + std::to_string(i);
            enttReg.emplace<Scene::TagComponent>(e, name);

            auto& t = enttReg.emplace<Scene::TransformComponent>(e);
            t.position = glm::vec3(startX + i * spacing, height, zPos);

            auto& l = enttReg.emplace<Scene::LightComponent>(e);
            l.type = Scene::LightType::Point;
            // Warm sodium-vapor style color
            l.color = glm::vec3(1.0f, 0.85f, 0.55f);
            // Strong intensity and generous range so they fill the street.
            l.intensity = useSafeRig ? 15.0f : 24.0f;
            l.range = useSafeRig ? 14.0f : 18.0f;
            // Let every second lantern cast shadows in the high variant; in
            // the safe variant only every fourth lantern is shadowed.
            if (useSafeRig) {
                l.castsShadows = (i % 4 == 0);
            } else {
                l.castsShadows = (i % 2 == 0);
            }
        }

        spdlog::info("Applied lighting rig: StreetLanterns ({} lights)", lightCount);
        break;
    }
    }
}

void Renderer::SetEnvironmentPreset(const std::string& name) {
    if (m_environmentMaps.empty()) {
        spdlog::warn("No environments loaded");
        return;
    }

    // Search for environment by name (case-insensitive partial match)
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    size_t targetIndex = m_currentEnvironment;
    bool found = false;

    for (size_t i = 0; i < m_environmentMaps.size(); ++i) {
        std::string envNameLower = m_environmentMaps[i].name;
        std::transform(envNameLower.begin(), envNameLower.end(), envNameLower.begin(),
                      [](unsigned char c) { return std::tolower(c); });

        if (envNameLower.find(lowerName) != std::string::npos) {
            targetIndex = i;
            found = true;
            break;
        }
    }

    if (!found) {
        spdlog::warn("Environment '{}' not found, keeping current environment", name);
        return;
    }

    if (targetIndex == m_currentEnvironment) {
        return;
    }

    m_currentEnvironment = targetIndex;
    UpdateEnvironmentDescriptorTable();

    spdlog::info("Environment preset set to '{}'", m_environmentMaps[m_currentEnvironment].name);
}

std::string Renderer::GetCurrentEnvironmentName() const {
    if (m_environmentMaps.empty()) {
        return m_iblEnabled ? "None" : "None";
    }

    size_t index = m_currentEnvironment;
    if (index >= m_environmentMaps.size()) {
        index = 0;
    }

    return m_environmentMaps[index].name;
}

void Renderer::SetIBLIntensity(float diffuseIntensity, float specularIntensity) {
    float diff = std::max(diffuseIntensity, 0.0f);
    float spec = std::max(specularIntensity, 0.0f);
    if (std::abs(diff - m_iblDiffuseIntensity) < 1e-6f &&
        std::abs(spec - m_iblSpecularIntensity) < 1e-6f) {
        return;
    }
    m_iblDiffuseIntensity = diff;
    m_iblSpecularIntensity = spec;
    spdlog::info("IBL intensity set to diffuse={}, specular={}", m_iblDiffuseIntensity, m_iblSpecularIntensity);
}

void Renderer::SetIBLEnabled(bool enabled) {
    if (m_iblEnabled == enabled) {
        return;
    }
    m_iblEnabled = enabled;
    spdlog::info("Image-based lighting {}", m_iblEnabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetSunDirection(const glm::vec3& dir) {
    glm::vec3 d = dir;
    if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z) ||
        glm::length2(d) < 1e-6f) {
        spdlog::warn("SetSunDirection: invalid direction, ignoring");
        return;
    }
    m_directionalLightDirection = glm::normalize(d);
    spdlog::info("Sun direction set to ({:.2f}, {:.2f}, {:.2f})",
                 m_directionalLightDirection.x,
                 m_directionalLightDirection.y,
                 m_directionalLightDirection.z);
}

void Renderer::SetSunColor(const glm::vec3& color) {
    glm::vec3 c = glm::max(color, glm::vec3(0.0f));
    m_directionalLightColor = c;
    spdlog::info("Sun color set to ({:.2f}, {:.2f}, {:.2f})",
                 m_directionalLightColor.x,
                 m_directionalLightColor.y,
                 m_directionalLightColor.z);
}

void Renderer::SetSunIntensity(float intensity) {
    float v = std::max(intensity, 0.0f);
    m_directionalLightIntensity = v;
    spdlog::info("Sun intensity set to {:.2f}", m_directionalLightIntensity);
}

void Renderer::CycleEnvironmentPreset() {
    if (m_environmentMaps.empty()) {
        spdlog::warn("No environments loaded to cycle through");
        return;
    }

    // Treat "no IBL" as an extra preset in the cycle:
    //   env0 -> env1 -> ... -> envN-1 -> None -> env0 -> ...
    if (!m_iblEnabled) {
        // Currently in "no IBL" mode; re-enable and jump to the first environment.
        SetIBLEnabled(true);
        m_currentEnvironment = 0;
        UpdateEnvironmentDescriptorTable();

        const std::string& name = m_environmentMaps[m_currentEnvironment].name;
        spdlog::info("Environment cycled to '{}' ({}/{})", name, m_currentEnvironment + 1, m_environmentMaps.size());
        return;
    }

    if (m_currentEnvironment + 1 < m_environmentMaps.size()) {
        // Advance to the next environment preset.
        m_currentEnvironment++;
        UpdateEnvironmentDescriptorTable();

        const std::string& name = m_environmentMaps[m_currentEnvironment].name;
        spdlog::info("Environment cycled to '{}' ({}/{})", name, m_currentEnvironment + 1, m_environmentMaps.size());
    } else {
        // Wrapped past the last preset: switch to a neutral "no IBL" mode.
        SetIBLEnabled(false);
        spdlog::info("Environment cycled to 'None' (no IBL)");
    }
}

void Renderer::SetColorGrade(float warm, float cool) {
    // Clamp to a reasonable range to keep grading subtle.
    float clampedWarm = glm::clamp(warm, -1.0f, 1.0f);
    float clampedCool = glm::clamp(cool, -1.0f, 1.0f);
    if (std::abs(clampedWarm - m_colorGradeWarm) < 1e-3f &&
        std::abs(clampedCool - m_colorGradeCool) < 1e-3f) {
        return;
    }
    m_colorGradeWarm = clampedWarm;
    m_colorGradeCool = clampedCool;
    spdlog::info("Color grade warm/cool set to ({}, {})", m_colorGradeWarm, m_colorGradeCool);
}

void Renderer::EnsureMaterialTextures(Scene::RenderableComponent& renderable) {
    auto tryLoad = [&](std::string& path, std::shared_ptr<DX12Texture>& slot, bool useSRGB, const std::shared_ptr<DX12Texture>& placeholder) {
        const bool isPlaceholder = slot == nullptr || slot == placeholder;
        // Only load from disk when we currently have no texture or a placeholder.
        if (!path.empty() && isPlaceholder) {
            auto loaded = LoadTextureFromFile(path, useSRGB);
            if (loaded.IsOk()) {
                slot = loaded.Value();
                if (renderable.textures.gpuState) {
                    renderable.textures.gpuState->descriptorsReady = false;
                }
            } else {
                // One-shot failure: clear the path and fall back to the
                // placeholder so we do not keep spamming load attempts (and
                // reallocating resources) every frame for the same asset.
                spdlog::warn("Failed to load texture '{}': {}", path, loaded.Error());
                path.clear();
                slot = placeholder;
                if (renderable.textures.gpuState) {
                    renderable.textures.gpuState->descriptorsReady = false;
                }
            }
        } else if (path.empty() && slot && slot != placeholder) {
            slot = placeholder;
            if (renderable.textures.gpuState) {
                renderable.textures.gpuState->descriptorsReady = false;
            }
        }
    };

    tryLoad(renderable.textures.albedoPath, renderable.textures.albedo, true, m_placeholderAlbedo);
    tryLoad(renderable.textures.normalPath, renderable.textures.normal, false, m_placeholderNormal);
    tryLoad(renderable.textures.metallicPath, renderable.textures.metallic, false, m_placeholderMetallic);
    tryLoad(renderable.textures.roughnessPath, renderable.textures.roughness, false, m_placeholderRoughness);

    if (!renderable.textures.albedo) {
        renderable.textures.albedo = m_placeholderAlbedo;
    }
    if (!renderable.textures.normal) {
        renderable.textures.normal = m_placeholderNormal;
    }
    if (!renderable.textures.metallic) {
        renderable.textures.metallic = m_placeholderMetallic;
    }
    if (!renderable.textures.roughness) {
        renderable.textures.roughness = m_placeholderRoughness;
    }
}

void Renderer::FillMaterialTextureIndices(const Scene::RenderableComponent& renderable,
                                          MaterialConstants& materialData) const {
    uint32_t texIndices[4] = {
        kInvalidBindlessIndex,
        kInvalidBindlessIndex,
        kInvalidBindlessIndex,
        kInvalidBindlessIndex
    };

    uint32_t effectiveMapFlags[4] = {
        materialData.mapFlags.x,
        materialData.mapFlags.y,
        materialData.mapFlags.z,
        materialData.mapFlags.w
    };

    if (renderable.textures.gpuState) {
        for (int i = 0; i < 4; ++i) {
            const bool hasMap = (effectiveMapFlags[i] != 0u);
            if (hasMap && renderable.textures.gpuState->descriptors[i].IsValid()) {
                texIndices[i] = renderable.textures.gpuState->descriptors[i].index;
            } else {
                // Descriptor isn't ready (or map missing). Treat as no-map so
                // shaders use constant material values instead of placeholders.
                effectiveMapFlags[i] = 0u;
                texIndices[i] = kInvalidBindlessIndex;
            }
        }
    } else {
        for (int i = 0; i < 4; ++i) {
            effectiveMapFlags[i] = 0u;
            texIndices[i] = kInvalidBindlessIndex;
        }
    }

    materialData.mapFlags = glm::uvec4(
        effectiveMapFlags[0],
        effectiveMapFlags[1],
        effectiveMapFlags[2],
        effectiveMapFlags[3]
    );

    materialData.textureIndices = glm::uvec4(
        texIndices[0],
        texIndices[1],
        texIndices[2],
        texIndices[3]
    );
}

void Renderer::PrewarmMaterialDescriptors(Scene::ECS_Registry* registry) {
    if (!registry || !m_descriptorManager) {
        return;
    }

    auto view = registry->View<Scene::RenderableComponent, Scene::TransformComponent>();
    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);

        if (!renderable.mesh) {
            continue;
        }

        EnsureMaterialTextures(renderable);

        bool texturesChanged = false;
        if (renderable.textures.gpuState) {
            std::array<std::shared_ptr<DX12Texture>, 4> sources = {
                renderable.textures.albedo ? renderable.textures.albedo : m_placeholderAlbedo,
                renderable.textures.normal ? renderable.textures.normal : m_placeholderNormal,
                renderable.textures.metallic ? renderable.textures.metallic : m_placeholderMetallic,
                renderable.textures.roughness ? renderable.textures.roughness : m_placeholderRoughness
            };

            for (size_t i = 0; i < sources.size(); ++i) {
                auto previous = renderable.textures.gpuState->sourceTextures[i].lock();
                if (previous != sources[i]) {
                    texturesChanged = true;
                    break;
                }
            }
        } else {
            texturesChanged = true;
        }

        const bool hasDescriptors = renderable.textures.gpuState &&
                                    renderable.textures.gpuState->descriptors[0].IsValid();
        const bool needsRefresh = !hasDescriptors ||
                                  texturesChanged ||
                                  (renderable.textures.gpuState &&
                                   !renderable.textures.gpuState->descriptorsReady);
        if (needsRefresh) {
            RefreshMaterialDescriptors(renderable);
        }
    }
}

void Renderer::RefreshMaterialDescriptors(Scene::RenderableComponent& renderable) {
    auto& tex = renderable.textures;
    if (!tex.gpuState) {
        tex.gpuState = std::make_shared<MaterialGPUState>();
    }
    auto& state = *tex.gpuState;

    ID3D12Device* device = m_device ? m_device->GetDevice() : nullptr;
    if (!device || !m_descriptorManager) {
        return;
    }

    // Allocate descriptors once per material and reuse them; textures can change,
    // but we simply overwrite the descriptor contents.
    if (!state.descriptors[0].IsValid()) {
        for (int i = 0; i < 4; ++i) {
            auto handleResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                spdlog::error("Failed to allocate material descriptor: {}", handleResult.Error());
                return;
            }
            state.descriptors[i] = handleResult.Value();
        }
    }

    std::array<std::shared_ptr<DX12Texture>, 4> sources = {
        tex.albedo ? tex.albedo : m_placeholderAlbedo,
        tex.normal ? tex.normal : m_placeholderNormal,
        tex.metallic ? tex.metallic : m_placeholderMetallic,
        tex.roughness ? tex.roughness : m_placeholderRoughness
    };

    for (size_t i = 0; i < sources.size(); ++i) {
        auto fallback = (i == 0) ? m_placeholderAlbedo :
                        (i == 1) ? m_placeholderNormal :
                        (i == 2) ? m_placeholderMetallic :
                                   m_placeholderRoughness;

        DescriptorHandle srcHandle;
        if (sources[i] && sources[i]->GetSRV().IsValid()) {
            srcHandle = sources[i]->GetSRV();
        } else if (fallback && fallback->GetSRV().IsValid()) {
            srcHandle = fallback->GetSRV();
        }

        if (srcHandle.IsValid()) {
            device->CopyDescriptorsSimple(
                1,
                state.descriptors[i].cpu,
                srcHandle.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        } else {
            // No real or placeholder texture available: create a null SRV so
            // shaders can safely sample without dereferencing an invalid
            // descriptor. Use a simple 2D RGBA8 layout, which is compatible
            // with how placeholder textures are normally created.
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;

            device->CreateShaderResourceView(
                nullptr,
                &srvDesc,
                state.descriptors[i].cpu
            );
        }
    }

    for (size_t i = 0; i < sources.size(); ++i) {
        state.sourceTextures[i] = sources[i];
    }
    state.descriptorsReady = true;
}

Result<void> Renderer::CreateDepthBuffer() {
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    // Allocate the hardware depth buffer at the window resolution. Internal
    // renderScale is applied logically in shaders/VRAM estimates rather than
    // through frequent depth reallocations, which has proven more stable on
    // 8 GB-class GPUs.
    const float scale = 1.0f;
    depthDesc.Width = std::max(1u, static_cast<UINT>(m_window->GetWidth()));
    depthDesc.Height = std::max(1u, static_cast<UINT>(m_window->GetHeight()));
    depthDesc.DepthOrArraySize = 1;
    depthDesc.MipLevels = 1;
    depthDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    depthDesc.SampleDesc.Count = 1;
    depthDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &depthDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_depthBuffer)
    );

    if (FAILED(hr)) {
        m_depthBuffer.Reset();
        m_depthStencilView = {};
        m_depthSRV = {};

        CORTEX_REPORT_DEVICE_REMOVED("CreateDepthBuffer", hr);

        char buf[64];
        sprintf_s(buf, "0x%08X", static_cast<unsigned int>(hr));
        char dim[64];
        sprintf_s(dim, "%ux%u", depthDesc.Width, depthDesc.Height);
        return Result<void>::Err(std::string("Failed to create depth buffer (")
                                 + dim + ", scale=" + std::to_string(scale)
                                 + ", hr=" + buf + ")");
    }

    m_depthState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    // Create DSV
    auto dsvResult = m_descriptorManager->AllocateDSV();
    if (dsvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate DSV: " + dsvResult.Error());
    }

    m_depthStencilView = dsvResult.Value();

    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
    dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

    m_device->GetDevice()->CreateDepthStencilView(
        m_depthBuffer.Get(),
        &dsvDesc,
        m_depthStencilView.cpu
    );

    // Create SRV for depth sampling (SSAO) - use staging heap for persistent descriptors
    auto srvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate staging SRV for depth buffer: " + srvResult.Error());
    }
    m_depthSRV = srvResult.Value();

    D3D12_SHADER_RESOURCE_VIEW_DESC depthSrvDesc = {};
    depthSrvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    depthSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    depthSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    depthSrvDesc.Texture2D.MipLevels = 1;

    m_device->GetDevice()->CreateShaderResourceView(
        m_depthBuffer.Get(),
        &depthSrvDesc,
        m_depthSRV.cpu
    );

    spdlog::info("Depth buffer created");
    return Result<void>::Ok();
}

Result<void> Renderer::CreateShadowMapResources() {
    if (!m_device || !m_descriptorManager) {
        return Result<void>::Err("Renderer not initialized for shadow map creation");
    }

    const UINT shadowDim = static_cast<UINT>(m_shadowMapSize);

    D3D12_RESOURCE_DESC shadowDesc = {};
    shadowDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    shadowDesc.Width = shadowDim;
    shadowDesc.Height = shadowDim;
    // Allocate enough array slices for all cascades plus a small number of
    // local shadow-casting lights that share the same atlas.
    shadowDesc.DepthOrArraySize = kShadowArraySize;
    shadowDesc.MipLevels = 1;
    shadowDesc.Format = DXGI_FORMAT_R32_TYPELESS;
    shadowDesc.SampleDesc.Count = 1;
    shadowDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_D32_FLOAT;
    clearValue.DepthStencil.Depth = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &shadowDesc,
        D3D12_RESOURCE_STATE_DEPTH_WRITE,
        &clearValue,
        IID_PPV_ARGS(&m_shadowMap)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create shadow map resource");
    }

    m_shadowMapState = D3D12_RESOURCE_STATE_DEPTH_WRITE;

    // Create DSVs for each array slice (cascades + local lights)
    for (uint32_t i = 0; i < kShadowArraySize; ++i) {
        auto dsvResult = m_descriptorManager->AllocateDSV();
        if (dsvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate DSV for shadow cascade: " + dsvResult.Error());
        }
        m_shadowMapDSVs[i] = dsvResult.Value();

        D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc = {};
        dsvDesc.Format = DXGI_FORMAT_D32_FLOAT;
        dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
        dsvDesc.Texture2DArray.MipSlice = 0;
        dsvDesc.Texture2DArray.FirstArraySlice = i;
        dsvDesc.Texture2DArray.ArraySize = 1;
        dsvDesc.Flags = D3D12_DSV_FLAG_NONE;

        m_device->GetDevice()->CreateDepthStencilView(
            m_shadowMap.Get(),
            &dsvDesc,
            m_shadowMapDSVs[i].cpu
        );
    }

    // Create SRV for sampling shadow map - use staging heap for persistent resources
    auto srvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate staging SRV for shadow map: " + srvResult.Error());
    }
    m_shadowMapSRV = srvResult.Value();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R32_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2DArray.MipLevels = 1;
    srvDesc.Texture2DArray.MostDetailedMip = 0;
    srvDesc.Texture2DArray.FirstArraySlice = 0;
    srvDesc.Texture2DArray.ArraySize = kShadowArraySize;

    m_device->GetDevice()->CreateShaderResourceView(
        m_shadowMap.Get(),
        &srvDesc,
        m_shadowMapSRV.cpu
    );

    // Shadow viewport/scissor
    m_shadowViewport.TopLeftX = 0.0f;
    m_shadowViewport.TopLeftY = 0.0f;
    m_shadowViewport.Width = static_cast<float>(shadowDim);
    m_shadowViewport.Height = static_cast<float>(shadowDim);
    m_shadowViewport.MinDepth = 0.0f;
    m_shadowViewport.MaxDepth = 1.0f;

    m_shadowScissor.left = 0;
    m_shadowScissor.top = 0;
    m_shadowScissor.right = static_cast<LONG>(shadowDim);
    m_shadowScissor.bottom = static_cast<LONG>(shadowDim);

    spdlog::info("Shadow map created ({}x{})", shadowDim, shadowDim);

    // Shadow SRV changed; refresh the combined shadow + environment descriptor table.
    UpdateEnvironmentDescriptorTable();
    return Result<void>::Ok();
}

void Renderer::RecreateShadowMapResourcesForCurrentSize() {
    if (!m_device || !m_descriptorManager) {
        return;
    }
    if (!m_shadowMap) {
        // Nothing to recreate yet.
        return;
    }

    D3D12_RESOURCE_DESC currentDesc = m_shadowMap->GetDesc();
    const UINT desiredDim = static_cast<UINT>(m_shadowMapSize);

    // Only recreate when the current atlas is larger than the new safe size.
    if (currentDesc.Width <= desiredDim && currentDesc.Height <= desiredDim) {
        return;
    }

    m_shadowMap.Reset();
    m_shadowMapSRV = {};
    for (auto& dsv : m_shadowMapDSVs) {
        dsv = {};
    }

    auto result = CreateShadowMapResources();
    if (result.IsErr()) {
        spdlog::warn("Renderer: failed to recreate shadow map at safe size: {}", result.Error());
        m_shadowsEnabled = false;
    }
}

Result<void> Renderer::CreateRTShadowMask() {
    if (!m_device || !m_descriptorManager || !m_window) {
        return Result<void>::Err("Renderer not initialized for RT shadow mask creation");
    }

    const UINT width = m_window->GetWidth();
    const UINT height = m_window->GetHeight();

    if (width == 0 || height == 0) {
        return Result<void>::Err("Window size is zero; cannot create RT shadow mask");
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    // RT shadow mask: single-channel 0..1, UAV for DXR writes.
    m_rtShadowMask.Reset();
    m_rtShadowMaskSRV = {};
    m_rtShadowMaskUAV = {};
    m_rtShadowMaskState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_rtShadowMask));
    if (FAILED(hr)) {
        m_rtShadowMask.Reset();
        return Result<void>::Err("Failed to create RT shadow mask texture");
    }

    m_rtShadowMaskState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // SRV for sampling in the PBR shader - use staging heap for persistent resources
    auto srvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        m_rtShadowMask.Reset();
        return Result<void>::Err("Failed to allocate staging SRV for RT shadow mask: " + srvResult.Error());
    }
    m_rtShadowMaskSRV = srvResult.Value();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_device->GetDevice()->CreateShaderResourceView(
        m_rtShadowMask.Get(),
        &srvDesc,
        m_rtShadowMaskSRV.cpu);

    // UAV for DXR writes - use staging heap for persistent resources
    auto uavResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (uavResult.IsErr()) {
        m_rtShadowMask.Reset();
        m_rtShadowMaskSRV = {};
        return Result<void>::Err("Failed to allocate staging UAV for RT shadow mask: " + uavResult.Error());
    }
    m_rtShadowMaskUAV = uavResult.Value();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = desc.Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    m_device->GetDevice()->CreateUnorderedAccessView(
        m_rtShadowMask.Get(),
        nullptr,
        &uavDesc,
        m_rtShadowMaskUAV.cpu);

    // History texture for simple temporal smoothing of RT shadows.
    m_rtShadowMaskHistory.Reset();
    m_rtShadowMaskHistorySRV = {};
    m_rtShadowMaskHistoryState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC historyDesc = desc;
    historyDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &historyDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&m_rtShadowMaskHistory));
    if (FAILED(hr)) {
        m_rtShadowMaskHistory.Reset();
        return Result<void>::Err("Failed to create RT shadow mask history texture");
    }

    m_rtShadowMaskHistoryState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // Use staging heap for persistent history SRV
    auto historySrvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (historySrvResult.IsErr()) {
        m_rtShadowMaskHistory.Reset();
        m_rtShadowMaskHistorySRV = {};
        return Result<void>::Err("Failed to allocate staging SRV for RT shadow mask history: " + historySrvResult.Error());
    }
    m_rtShadowMaskHistorySRV = historySrvResult.Value();

    D3D12_SHADER_RESOURCE_VIEW_DESC historySrvDesc{};
    historySrvDesc.Format = historyDesc.Format;
    historySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    historySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    historySrvDesc.Texture2D.MipLevels = 1;

    m_device->GetDevice()->CreateShaderResourceView(
        m_rtShadowMaskHistory.Get(),
        &historySrvDesc,
        m_rtShadowMaskHistorySRV.cpu);

    // If the combined shadow + environment descriptor table has already been
    // allocated, copy the SRVs into slots 3 and 4 (t3, t4, space1) so they
    // are visible to the PBR shader when RT mode is active.
    if (m_shadowAndEnvDescriptors[0].IsValid()) {
        ID3D12Device* device = m_device->GetDevice();
        device->CopyDescriptorsSimple(
            1,
            m_shadowAndEnvDescriptors[3].cpu,
            m_rtShadowMaskSRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        if (m_rtShadowMaskHistorySRV.IsValid()) {
            device->CopyDescriptorsSimple(
                1,
                m_shadowAndEnvDescriptors[4].cpu,
                m_rtShadowMaskHistorySRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }
    }

    // Any time we (re)create the RT shadow targets, history is invalid until
    // we have copied a freshly written mask into it at the end of a frame.
    m_rtHasHistory = false;

    return Result<void>::Ok();
}

Result<void> Renderer::CreateRTGIResources() {
    if (!m_device || !m_descriptorManager || !m_window) {
        return Result<void>::Err("Renderer not initialized for RT GI creation");
    }

    const UINT fullWidth  = m_window->GetWidth();
    const UINT fullHeight = m_window->GetHeight();

    if (fullWidth == 0 || fullHeight == 0) {
        return Result<void>::Err("Window size is zero; cannot create RT GI buffer");
    }

    // Allocate RT GI at half-resolution relative to the main render target.
    // This substantially reduces VRAM usage and ray dispatch cost while the
    // subsequent spatial + temporal filters in the shader hide most of the
    // resolution loss.
    const UINT width  = std::max(1u, fullWidth  / 2u);
    const UINT height = std::max(1u, fullHeight / 2u);

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    m_rtGIColor.Reset();
    m_rtGISRV = {};
    m_rtGIUAV = {};
    m_rtGIState = D3D12_RESOURCE_STATE_COMMON;

    m_rtGIHistory.Reset();
    m_rtGIHistorySRV = {};
    m_rtGIHistoryState = D3D12_RESOURCE_STATE_COMMON;
    m_rtGIHasHistory = false;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

      HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
          &heapProps,
          D3D12_HEAP_FLAG_NONE,
          &desc,
          D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
          nullptr,
          IID_PPV_ARGS(&m_rtGIColor));
    if (FAILED(hr)) {
        m_rtGIColor.Reset();
        return Result<void>::Err("Failed to create RT GI buffer");
    }

    m_rtGIState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

      // SRV for sampling in the PBR shader - use staging heap
    auto srvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        m_rtGIColor.Reset();
        return Result<void>::Err("Failed to allocate staging SRV for RT GI buffer: " + srvResult.Error());
    }
    m_rtGISRV = srvResult.Value();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_device->GetDevice()->CreateShaderResourceView(
        m_rtGIColor.Get(),
        &srvDesc,
        m_rtGISRV.cpu);

      // UAV for DXR writes - use staging heap
    auto uavResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (uavResult.IsErr()) {
        m_rtGIColor.Reset();
        m_rtGISRV = {};
        return Result<void>::Err("Failed to allocate staging UAV for RT GI buffer: " + uavResult.Error());
    }
    m_rtGIUAV = uavResult.Value();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = desc.Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    m_device->GetDevice()->CreateUnorderedAccessView(
        m_rtGIColor.Get(),
        nullptr,
        &uavDesc,
        m_rtGIUAV.cpu);

      // Allocate history buffer (SRV only; written via CopyResource). Match
      // the half-resolution size used for the main GI buffer.
      ComPtr<ID3D12Resource> history;
      hr = m_device->GetDevice()->CreateCommittedResource(
          &heapProps,
          D3D12_HEAP_FLAG_NONE,
          &desc,
          D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
          nullptr,
          IID_PPV_ARGS(&history));
      if (FAILED(hr)) {
          m_rtGIColor.Reset();
          m_rtGISRV = {};
          m_rtGIUAV = {};
          return Result<void>::Err("Failed to create RT GI history buffer");
      }
      m_rtGIHistory = history;
      m_rtGIHistoryState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

      // Use staging heap for persistent GI history SRV
      auto historySrvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
      if (historySrvResult.IsErr()) {
          m_rtGIColor.Reset();
          m_rtGISRV = {};
          m_rtGIUAV = {};
          m_rtGIHistory.Reset();
          return Result<void>::Err("Failed to allocate staging SRV for RT GI history buffer: " + historySrvResult.Error());
      }
      m_rtGIHistorySRV = historySrvResult.Value();

      D3D12_SHADER_RESOURCE_VIEW_DESC historySrvDesc{};
      historySrvDesc.Format = desc.Format;
      historySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
      historySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
      historySrvDesc.Texture2D.MipLevels = 1;

      m_device->GetDevice()->CreateShaderResourceView(
          m_rtGIHistory.Get(),
          &historySrvDesc,
          m_rtGIHistorySRV.cpu);

      // If the combined shadow + environment descriptor table has already been
      // allocated, copy the SRVs into slots 5 (RT GI) and 6 (RT GI history)
      // so they are visible to the PBR shader when RT mode is active.
      if (m_shadowAndEnvDescriptors[0].IsValid() && m_rtGISRV.IsValid()) {
          ID3D12Device* device = m_device->GetDevice();
          device->CopyDescriptorsSimple(
              1,
              m_shadowAndEnvDescriptors[5].cpu,
              m_rtGISRV.cpu,
              D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
          if (m_rtGIHistorySRV.IsValid()) {
              device->CopyDescriptorsSimple(
                  1,
                  m_shadowAndEnvDescriptors[6].cpu,
                  m_rtGIHistorySRV.cpu,
                  D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
          }
      }

      return Result<void>::Ok();
}

Result<void> Renderer::CreateRTReflectionResources() {
    if (!m_device || !m_descriptorManager || !m_window) {
        return Result<void>::Err("Renderer not initialized for RT reflection creation");
    }

    UINT baseWidth  = m_window->GetWidth();
    UINT baseHeight = m_window->GetHeight();

    // Prefer to match the HDR render target size so RT reflections stay in
    // lockstep with the actual shading resolution when renderScale is used.
    if (m_hdrColor) {
        D3D12_RESOURCE_DESC hdrDesc = m_hdrColor->GetDesc();
        baseWidth  = static_cast<UINT>(hdrDesc.Width);
        baseHeight = static_cast<UINT>(hdrDesc.Height);
    }

    if (baseWidth == 0 || baseHeight == 0) {
        return Result<void>::Err("Render target size is zero; cannot create RT reflection buffer");
    }

    // Allocate RT reflections at half-resolution relative to the main render
    // target. The hybrid SSR/RT composition and temporal filtering smooth
    // out the reduced resolution while significantly lowering VRAM usage.
    const UINT width  = std::max(1u, baseWidth  / 2u);
    const UINT height = std::max(1u, baseHeight / 2u);

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    m_rtReflectionColor.Reset();
    m_rtReflectionSRV = {};
    m_rtReflectionUAV = {};
    m_rtReflectionState = D3D12_RESOURCE_STATE_COMMON;
    m_rtReflectionHistory.Reset();
    m_rtReflectionHistorySRV = {};
    m_rtReflectionHistoryState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.SampleDesc.Quality = 0;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        nullptr,
        IID_PPV_ARGS(&m_rtReflectionColor));
    if (FAILED(hr)) {
        m_rtReflectionColor.Reset();
        return Result<void>::Err("Failed to create RT reflection color buffer");
    }

    m_rtReflectionState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;

    // SRV for sampling in post-process - use staging heap
    auto srvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        m_rtReflectionColor.Reset();
        return Result<void>::Err("Failed to allocate staging SRV for RT reflection buffer: " + srvResult.Error());
    }
    m_rtReflectionSRV = srvResult.Value();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
    srvDesc.Format = desc.Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_device->GetDevice()->CreateShaderResourceView(
        m_rtReflectionColor.Get(),
        &srvDesc,
        m_rtReflectionSRV.cpu);

    // UAV for DXR writes - use staging heap
    auto uavResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (uavResult.IsErr()) {
        m_rtReflectionColor.Reset();
        m_rtReflectionSRV = {};
        return Result<void>::Err("Failed to allocate staging UAV for RT reflection buffer: " + uavResult.Error());
    }
    m_rtReflectionUAV = uavResult.Value();

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
    uavDesc.Format = desc.Format;
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

    m_device->GetDevice()->CreateUnorderedAccessView(
        m_rtReflectionColor.Get(),
        nullptr,
        &uavDesc,
        m_rtReflectionUAV.cpu);

    // Create a matching history buffer for temporal accumulation. This is
    // sampled as an SRV only and written via CopyResource at the end of each
    // frame after the DXR pass has produced the current RT reflection color.
    ComPtr<ID3D12Resource> reflHistory;
    hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&reflHistory));
    if (FAILED(hr)) {
        m_rtReflectionColor.Reset();
        m_rtReflectionSRV = {};
        m_rtReflectionUAV = {};
        return Result<void>::Err("Failed to create RT reflection history buffer");
    }
    m_rtReflectionHistory = reflHistory;
    m_rtReflectionHistoryState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

    // Use staging heap for persistent reflection history SRV
    auto reflHistorySrvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (reflHistorySrvResult.IsErr()) {
        m_rtReflectionColor.Reset();
        m_rtReflectionSRV = {};
        m_rtReflectionUAV = {};
        m_rtReflectionHistory.Reset();
        return Result<void>::Err("Failed to allocate staging SRV for RT reflection history buffer: " + reflHistorySrvResult.Error());
    }
    m_rtReflectionHistorySRV = reflHistorySrvResult.Value();

    D3D12_SHADER_RESOURCE_VIEW_DESC reflHistorySrvDesc{};
    reflHistorySrvDesc.Format = desc.Format;
    reflHistorySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    reflHistorySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    reflHistorySrvDesc.Texture2D.MipLevels = 1;

    m_device->GetDevice()->CreateShaderResourceView(
        m_rtReflectionHistory.Get(),
        &reflHistorySrvDesc,
        m_rtReflectionHistorySRV.cpu);

    // Initialize both the current and history reflection buffers to black so
    // any sampling before the first successful DXR pass yields a neutral
    // result instead of undefined VRAM contents.
    m_rtReflHasHistory = false;

    return Result<void>::Ok();
}

Result<void> Renderer::CreateHDRTarget() {
    if (!m_device || !m_descriptorManager) {
        return Result<void>::Err("Renderer not initialized for HDR target creation");
    }

    // Allocate the HDR target at the window resolution. Internal renderScale
    // is handled logically in the shading paths; keeping the underlying HDR
    // resource size fixed avoids large reallocations when renderScale
    // changes and reduces the risk of device-removed faults on memory-
    // constrained GPUs.
    const float scale = 1.0f;
    const UINT width  = std::max(1u, static_cast<UINT>(m_window->GetWidth()));
    const UINT height = std::max(1u, static_cast<UINT>(m_window->GetHeight()));

    if (width == 0 || height == 0) {
        return Result<void>::Err("Window size is zero; cannot create HDR target");
    }

    D3D12_RESOURCE_DESC desc = {};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    desc.SampleDesc.Count = 1;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

    D3D12_CLEAR_VALUE clearValue = {};
    clearValue.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    clearValue.Color[0] = 0.0f;
    clearValue.Color[1] = 0.0f;
    clearValue.Color[2] = 0.0f;
    clearValue.Color[3] = 1.0f;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &desc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &clearValue,
        IID_PPV_ARGS(&m_hdrColor)
    );

    if (FAILED(hr)) {
        m_hdrColor.Reset();
        m_hdrRTV = {};
        m_hdrSRV = {};

        CORTEX_REPORT_DEVICE_REMOVED("CreateHDRTarget", hr);

        char buf[64];
        sprintf_s(buf, "0x%08X", static_cast<unsigned int>(hr));
        char dim[64];
        sprintf_s(dim, "%ux%u", width, height);
        return Result<void>::Err(std::string("Failed to create HDR color target (")
                                 + dim + ", scale=" + std::to_string(scale)
                                 + ", hr=" + buf + ")");
    }

    m_hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;

    // RTV
    auto rtvResult = m_descriptorManager->AllocateRTV();
    if (rtvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate RTV for HDR target: " + rtvResult.Error());
    }
    m_hdrRTV = rtvResult.Value();

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    m_device->GetDevice()->CreateRenderTargetView(
        m_hdrColor.Get(),
        &rtvDesc,
        m_hdrRTV.cpu
    );

    // SRV - use staging heap for persistent descriptors
    auto srvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate staging SRV for HDR target: " + srvResult.Error());
    }
    m_hdrSRV = srvResult.Value();

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    m_device->GetDevice()->CreateShaderResourceView(
        m_hdrColor.Get(),
        &srvDesc,
        m_hdrSRV.cpu
    );

    spdlog::info("HDR target created: {}x{} (scale {:.2f})", width, height, scale);

    // Normal/roughness G-buffer target (full resolution, matched to HDR)
    m_gbufferNormalRoughness.Reset();
    m_gbufferNormalRoughnessRTV = {};
    m_gbufferNormalRoughnessSRV = {};
    m_gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC gbufDesc = desc;
    gbufDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

    D3D12_CLEAR_VALUE gbufClear = {};
    gbufClear.Format = gbufDesc.Format;
    gbufClear.Color[0] = 0.5f; // Encoded normal (0,0,1) -> (0.5,0.5,1.0)
    gbufClear.Color[1] = 0.5f;
    gbufClear.Color[2] = 1.0f;
    gbufClear.Color[3] = 1.0f; // Roughness default

    hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &gbufDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &gbufClear,
        IID_PPV_ARGS(&m_gbufferNormalRoughness)
    );

    if (FAILED(hr)) {
        spdlog::warn("Failed to create normal/roughness G-buffer target");
    } else {
        m_gbufferNormalRoughnessState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        // RTV for G-buffer
        auto gbufRtvResult = m_descriptorManager->AllocateRTV();
        if (gbufRtvResult.IsErr()) {
            spdlog::warn("Failed to allocate RTV for normal/roughness G-buffer: {}", gbufRtvResult.Error());
        } else {
            m_gbufferNormalRoughnessRTV = gbufRtvResult.Value();

            D3D12_RENDER_TARGET_VIEW_DESC gbufRtvDesc = {};
            gbufRtvDesc.Format = gbufDesc.Format;
            gbufRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            m_device->GetDevice()->CreateRenderTargetView(
                m_gbufferNormalRoughness.Get(),
                &gbufRtvDesc,
                m_gbufferNormalRoughnessRTV.cpu
            );
        }

        // SRV for sampling G-buffer in SSR/post - use staging heap for persistent descriptors
        auto gbufSrvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (gbufSrvResult.IsErr()) {
            spdlog::warn("Failed to allocate staging SRV for normal/roughness G-buffer: {}", gbufSrvResult.Error());
        } else {
            m_gbufferNormalRoughnessSRV = gbufSrvResult.Value();

            D3D12_SHADER_RESOURCE_VIEW_DESC gbufSrvDesc = {};
            gbufSrvDesc.Format = gbufDesc.Format;
            gbufSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            gbufSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            gbufSrvDesc.Texture2D.MipLevels = 1;

            m_device->GetDevice()->CreateShaderResourceView(
                m_gbufferNormalRoughness.Get(),
                &gbufSrvDesc,
                m_gbufferNormalRoughnessSRV.cpu
            );
        }
    }

    // (Re)create history color buffer for temporal AA in HDR space. This
    // matches the main HDR target format so TAA operates on linear lighting
    // before tonemapping and late post-effects.
    m_historyColor.Reset();
    m_historySRV = {};
    m_historyState = D3D12_RESOURCE_STATE_COMMON;
    m_hasHistory = false;

    D3D12_RESOURCE_DESC historyDesc = {};
    historyDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    historyDesc.Width = width;
    historyDesc.Height = height;
    historyDesc.DepthOrArraySize = 1;
    historyDesc.MipLevels = 1;
    historyDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    historyDesc.SampleDesc.Count = 1;
    historyDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &historyDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
        nullptr,
        IID_PPV_ARGS(&m_historyColor)
    );

    if (FAILED(hr)) {
        spdlog::warn("Failed to create TAA history buffer");
    } else {
        m_historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;

        if (!m_historySRV.IsValid()) {
            // Use staging heap for persistent TAA history SRV
            auto historySrvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (historySrvResult.IsErr()) {
                spdlog::warn("Failed to allocate staging SRV for TAA history: {}", historySrvResult.Error());
            } else {
                m_historySRV = historySrvResult.Value();

                D3D12_SHADER_RESOURCE_VIEW_DESC historySrvDesc = {};
                historySrvDesc.Format = historyDesc.Format;
                historySrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                historySrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                historySrvDesc.Texture2D.MipLevels = 1;

                m_device->GetDevice()->CreateShaderResourceView(
                    m_historyColor.Get(),
                    &historySrvDesc,
                    m_historySRV.cpu
                );
            }
        }
    }

    // (Re)create intermediate TAA resolve target (matches HDR resolution/format).
    m_taaIntermediate.Reset();
    m_taaIntermediateRTV = {};
    m_taaIntermediateState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC taaDesc = desc;
    D3D12_CLEAR_VALUE taaClear = {};
    taaClear.Format = taaDesc.Format;
    taaClear.Color[0] = 0.0f;
    taaClear.Color[1] = 0.0f;
    taaClear.Color[2] = 0.0f;
    taaClear.Color[3] = 1.0f;

    hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &taaDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &taaClear,
        IID_PPV_ARGS(&m_taaIntermediate)
    );

    if (FAILED(hr)) {
        spdlog::warn("Failed to create TAA intermediate HDR target");
    } else {
        m_taaIntermediateState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        auto taaRtvResult = m_descriptorManager->AllocateRTV();
        if (taaRtvResult.IsErr()) {
            spdlog::warn("Failed to allocate RTV for TAA intermediate: {}", taaRtvResult.Error());
        } else {
            m_taaIntermediateRTV = taaRtvResult.Value();

            D3D12_RENDER_TARGET_VIEW_DESC taaRtvDesc = {};
            taaRtvDesc.Format = taaDesc.Format;
            taaRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            m_device->GetDevice()->CreateRenderTargetView(
                m_taaIntermediate.Get(),
                &taaRtvDesc,
                m_taaIntermediateRTV.cpu
            );
        }
    }

    // (Re)create SSR color buffer (matches HDR resolution/format)
    m_ssrColor.Reset();
    m_ssrRTV = {};
    m_ssrSRV = {};
    m_ssrState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC ssrDesc = desc;
    ssrDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

    D3D12_CLEAR_VALUE ssrClear = {};
    ssrClear.Format = ssrDesc.Format;
    ssrClear.Color[0] = 0.0f;
    ssrClear.Color[1] = 0.0f;
    ssrClear.Color[2] = 0.0f;
    ssrClear.Color[3] = 0.0f;

    hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &ssrDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &ssrClear,
        IID_PPV_ARGS(&m_ssrColor)
    );

    if (FAILED(hr)) {
        spdlog::warn("Failed to create SSR color buffer");
    } else {
        m_ssrState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        auto ssrRtvResult = m_descriptorManager->AllocateRTV();
        if (ssrRtvResult.IsErr()) {
            spdlog::warn("Failed to allocate RTV for SSR buffer: {}", ssrRtvResult.Error());
        } else {
            m_ssrRTV = ssrRtvResult.Value();

            D3D12_RENDER_TARGET_VIEW_DESC ssrRtvDesc = {};
            ssrRtvDesc.Format = ssrDesc.Format;
            ssrRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            m_device->GetDevice()->CreateRenderTargetView(
                m_ssrColor.Get(),
                &ssrRtvDesc,
                m_ssrRTV.cpu
            );
        }

        // Use staging heap for persistent SSR SRV (copied in post-process)
        auto ssrSrvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (ssrSrvResult.IsErr()) {
            spdlog::warn("Failed to allocate staging SRV for SSR buffer: {}", ssrSrvResult.Error());
        } else {
            m_ssrSRV = ssrSrvResult.Value();

            D3D12_SHADER_RESOURCE_VIEW_DESC ssrSrvDesc = {};
            ssrSrvDesc.Format = ssrDesc.Format;
            ssrSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            ssrSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            ssrSrvDesc.Texture2D.MipLevels = 1;

            m_device->GetDevice()->CreateShaderResourceView(
                m_ssrColor.Get(),
                &ssrSrvDesc,
                m_ssrSRV.cpu
            );
        }
    }

    // (Re)create motion vector buffer (camera-only velocity in UV space)
    m_velocityBuffer.Reset();
    m_velocityRTV = {};
    m_velocitySRV = {};
    m_velocityState = D3D12_RESOURCE_STATE_COMMON;

    D3D12_RESOURCE_DESC velDesc = desc;
    velDesc.Format = DXGI_FORMAT_R16G16_FLOAT;

    D3D12_CLEAR_VALUE velClear = {};
    velClear.Format = velDesc.Format;
    velClear.Color[0] = 0.0f;
    velClear.Color[1] = 0.0f;
    velClear.Color[2] = 0.0f;
    velClear.Color[3] = 0.0f;

    hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &velDesc,
        D3D12_RESOURCE_STATE_RENDER_TARGET,
        &velClear,
        IID_PPV_ARGS(&m_velocityBuffer)
    );

    if (FAILED(hr)) {
        spdlog::warn("Failed to create motion vector buffer");
    } else {
        m_velocityState = D3D12_RESOURCE_STATE_RENDER_TARGET;

        auto velRtvResult = m_descriptorManager->AllocateRTV();
        if (velRtvResult.IsErr()) {
            spdlog::warn("Failed to allocate RTV for motion vector buffer: {}", velRtvResult.Error());
        } else {
            m_velocityRTV = velRtvResult.Value();

            D3D12_RENDER_TARGET_VIEW_DESC velRtvDesc = {};
            velRtvDesc.Format = velDesc.Format;
            velRtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

            m_device->GetDevice()->CreateRenderTargetView(
                m_velocityBuffer.Get(),
                &velRtvDesc,
                m_velocityRTV.cpu
            );
        }

        // Use staging heap for persistent velocity SRV (used in TAA)
        auto velSrvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (velSrvResult.IsErr()) {
            spdlog::warn("Failed to allocate staging SRV for motion vector buffer: {}", velSrvResult.Error());
        } else {
            m_velocitySRV = velSrvResult.Value();

            D3D12_SHADER_RESOURCE_VIEW_DESC velSrvDesc = {};
            velSrvDesc.Format = velDesc.Format;
            velSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            velSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            velSrvDesc.Texture2D.MipLevels = 1;

            m_device->GetDevice()->CreateShaderResourceView(
                m_velocityBuffer.Get(),
                &velSrvDesc,
                m_velocitySRV.cpu
            );
        }
    }

    // (Re)create bloom render targets that depend on HDR size
    auto bloomResult = CreateBloomResources();
    if (bloomResult.IsErr()) {
        spdlog::warn("Failed to create bloom resources: {}", bloomResult.Error());
    }

    // SSAO target depends on window size as well
    auto ssaoResult = CreateSSAOResources();
    if (ssaoResult.IsErr()) {
        spdlog::warn("Failed to create SSAO resources: {}", ssaoResult.Error());
    }

    return Result<void>::Ok();
}

Result<void> Renderer::CreateCommandList() {
    HRESULT hr = m_device->GetDevice()->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandAllocators[0].Get(),
        nullptr,
        IID_PPV_ARGS(&m_commandList)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command list");
    }

    // Close the command list (will be reset in BeginFrame)
    m_commandList->Close();

    return Result<void>::Ok();
}

  Result<void> Renderer::CompileShaders() {
    // Compile shaders
    auto vsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/Basic.hlsl",
        "VSMain",
        "vs_5_1"
    );

    if (vsResult.IsErr()) {
        return Result<void>::Err("Failed to compile vertex shader: " + vsResult.Error());
    }

    auto psResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/Basic.hlsl",
        "PSMain",
        "ps_5_1"
    );

    if (psResult.IsErr()) {
        return Result<void>::Err("Failed to compile pixel shader: " + psResult.Error());
    }

    auto skyboxVsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/Basic.hlsl",
        "SkyboxVS",
        "vs_5_1"
    );

    auto skyboxPsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/Basic.hlsl",
        "SkyboxPS",
        "ps_5_1"
    );

    auto shadowVsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/Basic.hlsl",
        "VSShadow",
        "vs_5_1"
    );

    if (shadowVsResult.IsErr()) {
        return Result<void>::Err("Failed to compile shadow vertex shader: " + shadowVsResult.Error());
    }

    auto postVsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/PostProcess.hlsl",
        "VSMain",
        "vs_5_1"
    );

    if (postVsResult.IsErr()) {
        return Result<void>::Err("Failed to compile post-process vertex shader: " + postVsResult.Error());
    }

    auto postPsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/PostProcess.hlsl",
        "PSMain",
        "ps_5_1"
    );

    if (postPsResult.IsErr()) {
        return Result<void>::Err("Failed to compile post-process pixel shader: " + postPsResult.Error());
    }

    // Experimental voxel raymarch pixel shader. Uses the same fullscreen
    // vertex shader as the post-process path (SV_VertexID triangle) and the
    // shared FrameConstants layout so that camera and lighting state remain
    // consistent with the classic renderer.
    auto voxelPsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/VoxelRaymarch.hlsl",
        "PSMain",
        "ps_5_1"
    );
    if (voxelPsResult.IsErr()) {
        spdlog::warn("Failed to compile voxel raymarch pixel shader: {}", voxelPsResult.Error());
    }

    // HDR TAA resolve pixel shader (operates on HDR lighting before tonemapping).
    auto taaPsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/PostProcess.hlsl",
        "TAAResolvePS",
        "ps_5_1"
    );
    if (taaPsResult.IsErr()) {
        spdlog::warn("Failed to compile TAA HDR pixel shader: {}", taaPsResult.Error());
    }

    auto ssaoVsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/SSAO.hlsl",
        "VSMain",
        "vs_5_1"
    );
    if (ssaoVsResult.IsErr()) {
        spdlog::warn("Failed to compile SSAO vertex shader: {}", ssaoVsResult.Error());
    }

    auto ssaoPsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/SSAO.hlsl",
        "PSMain",
        "ps_5_1"
    );
    if (ssaoPsResult.IsErr()) {
        spdlog::warn("Failed to compile SSAO pixel shader: {}", ssaoPsResult.Error());
    }

    // SSR shaders (fullscreen reflections pass)
    auto ssrVsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/SSR.hlsl",
        "VSMain",
        "vs_5_1"
    );
    if (ssrVsResult.IsErr()) {
        spdlog::warn("Failed to compile SSR vertex shader: {}", ssrVsResult.Error());
    }

    auto ssrPsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/SSR.hlsl",
        "SSRPS",
        "ps_5_1"
    );
    if (ssrPsResult.IsErr()) {
        spdlog::warn("Failed to compile SSR pixel shader: {}", ssrPsResult.Error());
    }

    // Motion vector pass (camera-only velocity)
    auto motionVsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/MotionVectors.hlsl",
        "VSMain",
        "vs_5_1"
    );
    if (motionVsResult.IsErr()) {
        spdlog::warn("Failed to compile motion vector vertex shader: {}", motionVsResult.Error());
    }

    auto motionPsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/MotionVectors.hlsl",
        "PSMain",
        "ps_5_1"
    );
    if (motionPsResult.IsErr()) {
        spdlog::warn("Failed to compile motion vector pixel shader: {}", motionPsResult.Error());
    }

    // Water surface shaders (optional). If compilation fails, we simply skip
    // creating a dedicated water pipeline and render water with the default
    // PBR path instead.
    auto waterVsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/Water.hlsl",
        "WaterVS",
        "vs_5_1"
    );
    if (waterVsResult.IsErr()) {
        spdlog::warn("Failed to compile water vertex shader: {}", waterVsResult.Error());
    }

    auto waterPsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/Water.hlsl",
        "WaterPS",
        "ps_5_1"
    );
    if (waterPsResult.IsErr()) {
        spdlog::warn("Failed to compile water pixel shader: {}", waterPsResult.Error());
    }

    // Store compiled shaders (we'll use them in CreatePipeline)
    // For now, we'll just recreate the root signature and pipeline

    m_rootSignature = std::make_unique<DX12RootSignature>();
    auto rsResult = m_rootSignature->Initialize(m_device->GetDevice());
    if (rsResult.IsErr()) {
        return Result<void>::Err("Failed to create root signature: " + rsResult.Error());
    }
    if (m_gpuCulling) {
        auto sigResult = m_gpuCulling->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
        if (sigResult.IsErr()) {
            spdlog::warn("GPU Culling command signature setup failed: {}", sigResult.Error());
        }
    }

    // Create compute root signature for compute pipelines
    m_computeRootSignature = std::make_unique<DX12ComputeRootSignature>();
    auto computeRsResult = m_computeRootSignature->Initialize(m_device->GetDevice());
    if (computeRsResult.IsErr()) {
        spdlog::warn("Failed to create compute root signature: {}", computeRsResult.Error());
        m_computeRootSignature.reset();
    } else {
        spdlog::info("Compute root signature created successfully");
    }

      // Create pipeline
      m_pipeline = std::make_unique<DX12Pipeline>();

    PipelineDesc pipelineDesc = {};
    pipelineDesc.vertexShader = vsResult.Value();
    pipelineDesc.pixelShader = psResult.Value();
    pipelineDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pipelineDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineDesc.numRenderTargets = 2;

    // Define input layout
    pipelineDesc.inputLayout = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };

    auto pipelineResult = m_pipeline->Initialize(
        m_device->GetDevice(),
        m_rootSignature->GetRootSignature(),
        pipelineDesc
    );

    if (pipelineResult.IsErr()) {
        return Result<void>::Err("Failed to create pipeline: " + pipelineResult.Error());
    }

    // Transparent variant of the main PBR pipeline for glass/alpha
    // materials. Uses the same shaders and input layout but enables
    // alpha blending and disables depth writes so transparent surfaces
    // can be rendered over the opaque scene in a separate pass.
    m_transparentPipeline = std::make_unique<DX12Pipeline>();
    PipelineDesc transparentDesc = pipelineDesc;
    transparentDesc.blendEnabled = true;
    transparentDesc.depthWriteEnabled = false;

    auto transparentResult = m_transparentPipeline->Initialize(
        m_device->GetDevice(),
        m_rootSignature->GetRootSignature(),
        transparentDesc);
    if (transparentResult.IsErr()) {
        spdlog::warn("Failed to create transparent pipeline: {}", transparentResult.Error());
        m_transparentPipeline.reset();
    }

    // Depth-only pipeline for prepass: reuse the main vertex shader and
    // input layout, but omit a pixel shader and disable color render
    // targets so we only populate the depth buffer.
    m_depthOnlyPipeline = std::make_unique<DX12Pipeline>();
    PipelineDesc depthDesc = {};
    depthDesc.vertexShader = vsResult.Value();
    depthDesc.pixelShader  = {}; // empty = no PS
    depthDesc.inputLayout  = pipelineDesc.inputLayout;
    depthDesc.rtvFormat    = DXGI_FORMAT_UNKNOWN;
    depthDesc.dsvFormat    = DXGI_FORMAT_D32_FLOAT;
    depthDesc.numRenderTargets = 0;
    depthDesc.depthTestEnabled  = true;
    depthDesc.depthWriteEnabled = true;
    depthDesc.cullMode = D3D12_CULL_MODE_BACK;
    depthDesc.blendEnabled = false;

    auto depthPipelineResult = m_depthOnlyPipeline->Initialize(
        m_device->GetDevice(),
        m_rootSignature->GetRootSignature(),
        depthDesc
    );
    if (depthPipelineResult.IsErr()) {
        spdlog::warn("Failed to create depth-only pipeline: {}", depthPipelineResult.Error());
        m_depthOnlyPipeline.reset();
    }

    // Optional dedicated water pipeline: uses the same input layout and root
    // signature as the main PBR pipeline but a tailored shader pair.
    if (waterVsResult.IsOk() && waterPsResult.IsOk()) {
        m_waterPipeline = std::make_unique<DX12Pipeline>();

        PipelineDesc waterDesc = {};
        waterDesc.vertexShader = waterVsResult.Value();
        waterDesc.pixelShader  = waterPsResult.Value();
        waterDesc.inputLayout = pipelineDesc.inputLayout;
        waterDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        waterDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        waterDesc.numRenderTargets = 2;
        waterDesc.depthTestEnabled = true;
        waterDesc.depthWriteEnabled = true;
        waterDesc.cullMode = D3D12_CULL_MODE_BACK;
        waterDesc.blendEnabled = false;

        auto waterPipelineResult = m_waterPipeline->Initialize(
            m_device->GetDevice(),
            m_rootSignature->GetRootSignature(),
            waterDesc);
        if (waterPipelineResult.IsErr()) {
            spdlog::warn("Failed to create water pipeline: {}", waterPipelineResult.Error());
            m_waterPipeline.reset();
        }
    }

    // Particle pipeline: instanced camera-facing quads rendered into the HDR
    // buffer. Uses a minimal vertex format (position/UV + per-instance data)
    // and simple alpha blending.
    auto particleVsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/Particles.hlsl",
        "VSMain",
        "vs_5_1");
    if (particleVsResult.IsErr()) {
        spdlog::warn("Failed to compile particle vertex shader: {}", particleVsResult.Error());
    }
    auto particlePsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/Particles.hlsl",
        "PSMain",
        "ps_5_1");
    if (particlePsResult.IsErr()) {
        spdlog::warn("Failed to compile particle pixel shader: {}", particlePsResult.Error());
    }

    if (particleVsResult.IsOk() && particlePsResult.IsOk()) {
        m_particlePipeline = std::make_unique<DX12Pipeline>();

        PipelineDesc particleDesc = {};
        particleDesc.vertexShader = particleVsResult.Value();
        particleDesc.pixelShader  = particlePsResult.Value();
        // Quad vertex buffer in slot 0
        D3D12_INPUT_ELEMENT_DESC posElem{};
        posElem.SemanticName = "POSITION";
        posElem.SemanticIndex = 0;
        posElem.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        posElem.InputSlot = 0;
        posElem.AlignedByteOffset = 0;
        posElem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        posElem.InstanceDataStepRate = 0;

        D3D12_INPUT_ELEMENT_DESC uvElem{};
        uvElem.SemanticName = "TEXCOORD";
        uvElem.SemanticIndex = 0;
        uvElem.Format = DXGI_FORMAT_R32G32_FLOAT;
        uvElem.InputSlot = 0;
        uvElem.AlignedByteOffset = 12;
        uvElem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
        uvElem.InstanceDataStepRate = 0;

        // Instance data in slot 1: position (TEXCOORD1), size (TEXCOORD2), color (COLOR0)
        D3D12_INPUT_ELEMENT_DESC instPos{};
        instPos.SemanticName = "TEXCOORD";
        instPos.SemanticIndex = 1;
        instPos.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        instPos.InputSlot = 1;
        instPos.AlignedByteOffset = 0;
        instPos.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        instPos.InstanceDataStepRate = 1;

        D3D12_INPUT_ELEMENT_DESC instSize{};
        instSize.SemanticName = "TEXCOORD";
        instSize.SemanticIndex = 2;
        instSize.Format = DXGI_FORMAT_R32_FLOAT;
        instSize.InputSlot = 1;
        instSize.AlignedByteOffset = 12;
        instSize.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        instSize.InstanceDataStepRate = 1;

        D3D12_INPUT_ELEMENT_DESC instColor{};
        instColor.SemanticName = "COLOR";
        instColor.SemanticIndex = 0;
        instColor.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        instColor.InputSlot = 1;
        instColor.AlignedByteOffset = 16;
        instColor.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        instColor.InstanceDataStepRate = 1;

        particleDesc.inputLayout = { posElem, uvElem, instPos, instSize, instColor };
        particleDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        particleDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        particleDesc.numRenderTargets = 1;
        particleDesc.depthTestEnabled = true;
        particleDesc.depthWriteEnabled = false;
        particleDesc.cullMode = D3D12_CULL_MODE_NONE;
        particleDesc.blendEnabled = true;

        auto particlePipelineResult = m_particlePipeline->Initialize(
            m_device->GetDevice(),
            m_rootSignature->GetRootSignature(),
            particleDesc);
        if (particlePipelineResult.IsErr()) {
            spdlog::warn("Failed to create particle pipeline: {}", particlePipelineResult.Error());
            m_particlePipeline.reset();
        }
    }

    // Skybox pipeline (fullscreen triangle; no depth)
    if (skyboxVsResult.IsOk() && skyboxPsResult.IsOk()) {
        m_skyboxPipeline = std::make_unique<DX12Pipeline>();

        PipelineDesc skyDesc = {};
        skyDesc.vertexShader = skyboxVsResult.Value();
        skyDesc.pixelShader = skyboxPsResult.Value();
        skyDesc.inputLayout = {}; // SV_VertexID-driven triangle
        skyDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        skyDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        skyDesc.numRenderTargets = 1;
        skyDesc.depthTestEnabled = false;
        skyDesc.depthWriteEnabled = false;
        skyDesc.cullMode = D3D12_CULL_MODE_NONE;
        skyDesc.blendEnabled = false;

        auto skyboxPipelineResult = m_skyboxPipeline->Initialize(
            m_device->GetDevice(),
            m_rootSignature->GetRootSignature(),
            skyDesc
        );
        if (skyboxPipelineResult.IsErr()) {
            spdlog::warn("Failed to create skybox pipeline: {}", skyboxPipelineResult.Error());
            m_skyboxPipeline.reset();
        }
    } else {
        spdlog::warn("Skybox shaders did not compile; environment will be lighting-only");
    }

    // Depth-only pipeline for directional shadow map
    m_shadowPipeline = std::make_unique<DX12Pipeline>();

    PipelineDesc shadowDesc = {};
    shadowDesc.vertexShader = shadowVsResult.Value();
    // depth-only: no pixel shader, no color target
    shadowDesc.inputLayout = pipelineDesc.inputLayout;
    shadowDesc.rtvFormat = DXGI_FORMAT_UNKNOWN;
    shadowDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    shadowDesc.numRenderTargets = 0;
    shadowDesc.depthTestEnabled = true;
    shadowDesc.depthWriteEnabled = true;
    shadowDesc.cullMode = D3D12_CULL_MODE_BACK;
    shadowDesc.wireframe = false;
    shadowDesc.blendEnabled = false;

    auto shadowPipelineResult = m_shadowPipeline->Initialize(
        m_device->GetDevice(),
        m_rootSignature->GetRootSignature(),
        shadowDesc
    );

    if (shadowPipelineResult.IsErr()) {
        return Result<void>::Err("Failed to create shadow pipeline: " + shadowPipelineResult.Error());
    }

    // Post-process pipeline (fullscreen pass)
    m_postProcessPipeline = std::make_unique<DX12Pipeline>();

    PipelineDesc postDesc = {};
    postDesc.vertexShader = postVsResult.Value();
    postDesc.pixelShader = postPsResult.Value();
    postDesc.inputLayout = {}; // fullscreen triangle via SV_VertexID
    postDesc.rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    postDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
    postDesc.numRenderTargets = 1;
    postDesc.depthTestEnabled = false;
    postDesc.depthWriteEnabled = false;
    postDesc.cullMode = D3D12_CULL_MODE_NONE;
    postDesc.blendEnabled = false;

    auto postPipelineResult = m_postProcessPipeline->Initialize(
        m_device->GetDevice(),
        m_rootSignature->GetRootSignature(),
        postDesc
    );

    if (postPipelineResult.IsErr()) {
        return Result<void>::Err("Failed to create post-process pipeline: " + postPipelineResult.Error());
    }

    // Voxel renderer pipeline: fullscreen triangle rendered directly into
    // the swap chain back buffer. This keeps the experimental voxel backend
    // independent from the HDR/SSR/RT path while still sharing the same
    // root signature and FrameConstants layout.
    if (voxelPsResult.IsOk()) {
        m_voxelPipeline = std::make_unique<DX12Pipeline>();

        PipelineDesc voxelDesc = {};
        voxelDesc.vertexShader = postVsResult.Value();
        voxelDesc.pixelShader  = voxelPsResult.Value();
        voxelDesc.inputLayout  = {}; // SV_VertexID triangle
        voxelDesc.rtvFormat    = DXGI_FORMAT_R8G8B8A8_UNORM;
        voxelDesc.dsvFormat    = DXGI_FORMAT_UNKNOWN;
        voxelDesc.numRenderTargets = 1;
        voxelDesc.depthTestEnabled  = false;
        voxelDesc.depthWriteEnabled = false;
        voxelDesc.cullMode = D3D12_CULL_MODE_NONE;
        voxelDesc.blendEnabled = false;

        auto voxelPipelineResult = m_voxelPipeline->Initialize(
            m_device->GetDevice(),
            m_rootSignature->GetRootSignature(),
            voxelDesc
        );
        if (voxelPipelineResult.IsErr()) {
            spdlog::warn("Failed to create voxel renderer pipeline: {}", voxelPipelineResult.Error());
            m_voxelPipeline.reset();
        } else {
            spdlog::info("Voxel renderer pipeline created successfully (rtvFormat=R8G8B8A8_UNORM).");
        }
    } else {
        spdlog::warn("Voxel raymarch pixel shader compilation failed; experimental voxel backend disabled.");
    }

    // HDR TAA resolve pipeline (fullscreen, writes into HDR intermediate)
    if (taaPsResult.IsOk()) {
        m_taaPipeline = std::make_unique<DX12Pipeline>();

        PipelineDesc taaDesc = {};
        taaDesc.vertexShader = postVsResult.Value();
        taaDesc.pixelShader  = taaPsResult.Value();
        taaDesc.inputLayout = {}; // fullscreen triangle via SV_VertexID
        taaDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        taaDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
        taaDesc.numRenderTargets = 1;
        taaDesc.depthTestEnabled = false;
        taaDesc.depthWriteEnabled = false;
        taaDesc.cullMode = D3D12_CULL_MODE_NONE;
        taaDesc.blendEnabled = false;

        auto taaPipelineResult = m_taaPipeline->Initialize(
            m_device->GetDevice(),
            m_rootSignature->GetRootSignature(),
            taaDesc
        );
        if (taaPipelineResult.IsErr()) {
            spdlog::warn("Failed to create TAA pipeline: {}", taaPipelineResult.Error());
            m_taaPipeline.reset();
        }
    }

    // SSAO pipeline (fullscreen pass, single-channel target)
    if (ssaoVsResult.IsOk() && ssaoPsResult.IsOk()) {
        m_ssaoPipeline = std::make_unique<DX12Pipeline>();

        PipelineDesc ssaoDesc = {};
        ssaoDesc.vertexShader = ssaoVsResult.Value();
        ssaoDesc.pixelShader  = ssaoPsResult.Value();
        ssaoDesc.inputLayout = {}; // fullscreen triangle via SV_VertexID
        ssaoDesc.rtvFormat = DXGI_FORMAT_R8_UNORM;
        ssaoDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
        ssaoDesc.numRenderTargets = 1;
        ssaoDesc.depthTestEnabled = false;
        ssaoDesc.depthWriteEnabled = false;
        ssaoDesc.cullMode = D3D12_CULL_MODE_NONE;
        ssaoDesc.blendEnabled = false;

        auto ssaoPipelineResult = m_ssaoPipeline->Initialize(
            m_device->GetDevice(),
            m_rootSignature->GetRootSignature(),
            ssaoDesc
        );
        if (ssaoPipelineResult.IsErr()) {
            spdlog::warn("Failed to create SSAO pipeline: {}", ssaoPipelineResult.Error());
            m_ssaoPipeline.reset();
        }
    }

    // SSAO compute pipeline (async compute version)
    if (m_asyncComputeSupported && m_computeRootSignature) {
        auto ssaoComputeResult = ShaderCompiler::CompileFromFile(
            "assets/shaders/SSAO_Compute.hlsl",
            "CSMain",
            "cs_5_1"
        );
        if (ssaoComputeResult.IsOk()) {
            m_ssaoComputePipeline = std::make_unique<DX12ComputePipeline>();
            auto computePipelineResult = m_ssaoComputePipeline->Initialize(
                m_device->GetDevice(),
                m_computeRootSignature->GetRootSignature(),  // Use compute root signature!
                ssaoComputeResult.Value()
            );
            if (computePipelineResult.IsErr()) {
                spdlog::warn("Failed to create SSAO compute pipeline: {}", computePipelineResult.Error());
                m_ssaoComputePipeline.reset();
            } else {
                spdlog::info("SSAO async compute pipeline created successfully");
            }
        } else {
            spdlog::warn("Failed to compile SSAO compute shader: {}", ssaoComputeResult.Error());
        }
    }

    // SSR pipeline (fullscreen reflections into dedicated buffer)
    if (ssrVsResult.IsOk() && ssrPsResult.IsOk()) {
        m_ssrPipeline = std::make_unique<DX12Pipeline>();

        PipelineDesc ssrDesc = {};
        ssrDesc.vertexShader = ssrVsResult.Value();
        ssrDesc.pixelShader  = ssrPsResult.Value();
        ssrDesc.inputLayout = {}; // fullscreen triangle via SV_VertexID
        ssrDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        ssrDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
        ssrDesc.numRenderTargets = 1;
        ssrDesc.depthTestEnabled = false;
        ssrDesc.depthWriteEnabled = false;
        ssrDesc.cullMode = D3D12_CULL_MODE_NONE;
        ssrDesc.blendEnabled = false;

        auto ssrPipelineResult = m_ssrPipeline->Initialize(
            m_device->GetDevice(),
            m_rootSignature->GetRootSignature(),
            ssrDesc
        );
        if (ssrPipelineResult.IsErr()) {
            spdlog::warn("Failed to create SSR pipeline: {}", ssrPipelineResult.Error());
            m_ssrPipeline.reset();
        }
    }

    // Motion vectors pipeline (fullscreen pass into RG16F buffer)
    if (motionVsResult.IsOk() && motionPsResult.IsOk()) {
        m_motionVectorsPipeline = std::make_unique<DX12Pipeline>();

        PipelineDesc mvDesc = {};
        mvDesc.vertexShader = motionVsResult.Value();
        mvDesc.pixelShader  = motionPsResult.Value();
        mvDesc.inputLayout = {}; // fullscreen triangle via SV_VertexID
        mvDesc.rtvFormat = DXGI_FORMAT_R16G16_FLOAT;
        mvDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
        mvDesc.numRenderTargets = 1;
        mvDesc.depthTestEnabled = false;
        mvDesc.depthWriteEnabled = false;
        mvDesc.cullMode = D3D12_CULL_MODE_NONE;
        mvDesc.blendEnabled = false;

        auto mvPipelineResult = m_motionVectorsPipeline->Initialize(
            m_device->GetDevice(),
            m_rootSignature->GetRootSignature(),
            mvDesc
        );
        if (mvPipelineResult.IsErr()) {
            spdlog::warn("Failed to create motion vectors pipeline: {}", mvPipelineResult.Error());
            m_motionVectorsPipeline.reset();
        }
    }

    // Bloom pipelines (fullscreen passes reusing VSMain)
    // Downsample + bright-pass
    m_bloomDownsamplePipeline = std::make_unique<DX12Pipeline>();
    PipelineDesc bloomDownDesc = postDesc;
    // Bloom targets are allocated as R11G11B10_FLOAT; match the RTV format
    // here so the pipeline writes directly into those HDR RGB buffers.
    bloomDownDesc.rtvFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    bloomDownDesc.pixelShader = ShaderCompiler::CompileFromFile(
        "assets/shaders/PostProcess.hlsl",
        "BloomDownsamplePS",
        "ps_5_1"
    ).ValueOr(postPsResult.Value());
    auto bloomDownResult = m_bloomDownsamplePipeline->Initialize(
        m_device->GetDevice(),
        m_rootSignature->GetRootSignature(),
        bloomDownDesc
    );
    if (bloomDownResult.IsErr()) {
        return Result<void>::Err("Failed to create bloom downsample pipeline: " + bloomDownResult.Error());
    }

    // Horizontal blur
    m_bloomBlurHPipeline = std::make_unique<DX12Pipeline>();
    PipelineDesc bloomBlurHDesc = postDesc;
    bloomBlurHDesc.rtvFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    bloomBlurHDesc.pixelShader = ShaderCompiler::CompileFromFile(
        "assets/shaders/PostProcess.hlsl",
        "BloomBlurHPS",
        "ps_5_1"
    ).ValueOr(postPsResult.Value());
    auto bloomBlurHResult = m_bloomBlurHPipeline->Initialize(
        m_device->GetDevice(),
        m_rootSignature->GetRootSignature(),
        bloomBlurHDesc
    );
    if (bloomBlurHResult.IsErr()) {
        return Result<void>::Err("Failed to create bloom horizontal blur pipeline: " + bloomBlurHResult.Error());
    }

    // Vertical blur
    m_bloomBlurVPipeline = std::make_unique<DX12Pipeline>();
    PipelineDesc bloomBlurVDesc = postDesc;
    bloomBlurVDesc.rtvFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    bloomBlurVDesc.pixelShader = ShaderCompiler::CompileFromFile(
        "assets/shaders/PostProcess.hlsl",
        "BloomBlurVPS",
        "ps_5_1"
    ).ValueOr(postPsResult.Value());
    auto bloomBlurVResult = m_bloomBlurVPipeline->Initialize(
        m_device->GetDevice(),
        m_rootSignature->GetRootSignature(),
        bloomBlurVDesc
    );
    if (bloomBlurVResult.IsErr()) {
        return Result<void>::Err("Failed to create bloom vertical blur pipeline: " + bloomBlurVResult.Error());
    }

    // Composite / upsample (additive) into base bloom level
    m_bloomCompositePipeline = std::make_unique<DX12Pipeline>();
    PipelineDesc bloomCompositeDesc = postDesc;
    bloomCompositeDesc.rtvFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    bloomCompositeDesc.pixelShader = ShaderCompiler::CompileFromFile(
        "assets/shaders/PostProcess.hlsl",
        "BloomUpsamplePS",
        "ps_5_1"
    ).ValueOr(postPsResult.Value());
    bloomCompositeDesc.blendEnabled = true;
    auto bloomCompositeResult = m_bloomCompositePipeline->Initialize(
        m_device->GetDevice(),
        m_rootSignature->GetRootSignature(),
        bloomCompositeDesc
    );
    if (bloomCompositeResult.IsErr()) {
        return Result<void>::Err("Failed to create bloom composite pipeline: " + bloomCompositeResult.Error());
    }

    // Debug line pipeline (world-space lines rendered after post-process).
    // Reuse Basic.hlsl with a lightweight VS/PS pair that reads FrameConstants.
    auto debugVsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/Basic.hlsl",
        "DebugLineVS",
        "vs_5_1"
    );
    auto debugPsResult = ShaderCompiler::CompileFromFile(
        "assets/shaders/Basic.hlsl",
        "DebugLinePS",
        "ps_5_1"
    );
    if (debugVsResult.IsOk() && debugPsResult.IsOk()) {
        m_debugLinePipeline = std::make_unique<DX12Pipeline>();

        PipelineDesc dbgDesc = {};
        dbgDesc.vertexShader = debugVsResult.Value();
        dbgDesc.pixelShader  = debugPsResult.Value();
        dbgDesc.inputLayout = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };
        dbgDesc.rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        dbgDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
        dbgDesc.numRenderTargets = 1;
        dbgDesc.depthTestEnabled = false;
        dbgDesc.depthWriteEnabled = false;
        dbgDesc.cullMode = D3D12_CULL_MODE_NONE;
        dbgDesc.blendEnabled = false;
        dbgDesc.primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

        auto dbgPipelineResult = m_debugLinePipeline->Initialize(
            m_device->GetDevice(),
            m_rootSignature->GetRootSignature(),
            dbgDesc
        );
        if (dbgPipelineResult.IsErr()) {
            spdlog::warn("Failed to create debug line pipeline: {}", dbgPipelineResult.Error());
            m_debugLinePipeline.reset();
        }
    } else {
        spdlog::warn("Failed to compile debug line shaders; debug overlay will be disabled");
    }

    return Result<void>::Ok();
}

Result<void> Renderer::CreatePipeline() {
    // Already done in CompileShaders
    return Result<void>::Ok();
}

Result<void> Renderer::CreatePlaceholderTexture() {
    // Now that we've fixed the upload buffer use-after-free bugs and added
    // texture caching, we can safely create placeholder textures again.
    const float white[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const float flatNormal[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
    const float black[4] = { 0.0f, 0.0f, 0.0f, 1.0f };

    auto createAndBind = [&](const float color[4], std::shared_ptr<DX12Texture>& out) -> Result<void> {
        auto texResult = DX12Texture::CreatePlaceholder(
            m_device->GetDevice(),
            m_uploadQueue ? m_uploadQueue->GetCommandQueue() : nullptr,
            m_commandQueue->GetCommandQueue(),
            2,
            2,
            color
        );

        if (texResult.IsErr()) {
            return Result<void>::Err("Failed to create placeholder texture: " + texResult.Error());
        }

        out = std::make_shared<DX12Texture>(std::move(texResult.Value()));

        // CRITICAL: Use staging heap for placeholder textures (copied into every material!)
        auto srvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate staging SRV for placeholder: " + srvResult.Error());
        }

        auto createSRVResult = out->CreateSRV(m_device->GetDevice(), srvResult.Value());
        if (createSRVResult.IsErr()) {
            return createSRVResult;
        }
        return Result<void>::Ok();
    };

    auto albedoResult = createAndBind(white, m_placeholderAlbedo);
    if (albedoResult.IsErr()) return albedoResult;

    auto normalResult = createAndBind(flatNormal, m_placeholderNormal);
    if (normalResult.IsErr()) return normalResult;

    auto metallicResult = createAndBind(black, m_placeholderMetallic);
    if (metallicResult.IsErr()) return metallicResult;

    auto roughnessResult = createAndBind(white, m_placeholderRoughness);
    if (roughnessResult.IsErr()) return roughnessResult;

    m_commandQueue->Flush();

    if (m_descriptorManager && !m_fallbackMaterialDescriptors[0].IsValid()) {
        std::array<std::shared_ptr<DX12Texture>, 4> sources = {
            m_placeholderAlbedo,
            m_placeholderNormal,
            m_placeholderMetallic,
            m_placeholderRoughness
        };

        for (int i = 0; i < 4; ++i) {
            auto handleResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                return Result<void>::Err("Failed to allocate fallback material descriptor: " + handleResult.Error());
            }
            m_fallbackMaterialDescriptors[i] = handleResult.Value();
        }

        for (int i = 0; i < 4; ++i) {
            if (sources[i] && sources[i]->GetSRV().IsValid()) {
                m_device->GetDevice()->CopyDescriptorsSimple(
                    1,
                    m_fallbackMaterialDescriptors[i].cpu,
                    sources[i]->GetSRV().cpu,
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
                );
            }
        }
    }

    // Register placeholder textures in bindless heap at reserved slots
    // These are always valid and used as fallbacks when materials have no specific texture
    if (m_bindlessManager) {
        auto registerPlaceholder = [this](std::shared_ptr<DX12Texture>& tex) {
            if (tex && tex->GetResource()) {
                auto result = tex->CreateBindlessSRV(m_bindlessManager.get());
                if (result.IsOk()) {
                    spdlog::debug("Placeholder registered at bindless index {}", tex->GetBindlessIndex());
                } else {
                    spdlog::warn("Failed to register placeholder at bindless index: {}", result.Error());
                }
            }
        };
        registerPlaceholder(m_placeholderAlbedo);
        registerPlaceholder(m_placeholderNormal);
        registerPlaceholder(m_placeholderMetallic);
        registerPlaceholder(m_placeholderRoughness);

        auto copyToReserved = [this](const std::shared_ptr<DX12Texture>& tex, uint32_t reservedIndex) {
            if (!tex || !tex->GetSRV().IsValid()) {
                return;
            }
            D3D12_CPU_DESCRIPTOR_HANDLE dst = m_bindlessManager->GetCPUHandle(reservedIndex);
            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                dst,
                tex->GetSRV().cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        };
        copyToReserved(m_placeholderAlbedo, BindlessResourceManager::kPlaceholderAlbedoIndex);
        copyToReserved(m_placeholderNormal, BindlessResourceManager::kPlaceholderNormalIndex);
        copyToReserved(m_placeholderMetallic, BindlessResourceManager::kPlaceholderMetallicIndex);
        copyToReserved(m_placeholderRoughness, BindlessResourceManager::kPlaceholderRoughnessIndex);
    }

    spdlog::info("Placeholder textures created");
    return Result<void>::Ok();
}

void Renderer::WaitForGPU() {
    // Block until the main graphics queue has finished all submitted work so
    // large reallocations (depth/HDR/RT targets) do not temporarily overlap
    // with resources still in use on the GPU. This is used sparingly, only
    // on resolution changes, to avoid unnecessary stalls.
    if (m_commandQueue) {
        m_commandQueue->Flush();
    }
    if (m_uploadQueue) {
        m_uploadQueue->Flush();
    }
}

Result<void> Renderer::InitializeEnvironmentMaps() {
    if (!m_descriptorManager || !m_device) {
        return Result<void>::Err("Renderer not initialized for environment maps");
    }

    // Clear any existing environments
    m_environmentMaps.clear();
    m_pendingEnvironments.clear();

    // Scan assets directory for all HDR and EXR files
    namespace fs = std::filesystem;
    std::vector<fs::path> envFiles;

    const fs::path assetsDir = "assets";

    if (fs::exists(assetsDir) && fs::is_directory(assetsDir)) {
        for (const auto& entry : fs::directory_iterator(assetsDir)) {
            if (entry.is_regular_file()) {
                std::string ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return std::tolower(c); });

                if (ext == ".hdr" || ext == ".exr") {
                    envFiles.push_back(entry.path());
                }
            }
        }
    }

    std::sort(envFiles.begin(), envFiles.end());

    // On 8 GB-class GPUs, clamp the number of eagerly loaded environments
    // aggressively so a single scene does not spend hundreds of MB on IBL
    // that may never be used. Heavier adapters can afford a broader set.
    constexpr size_t kDefaultMaxStartupEnvs = 8;
    size_t maxStartupEnvs = kDefaultMaxStartupEnvs;
    bool isEightGBClass = false;
    if (m_device) {
        const std::uint64_t bytes = m_device->GetDedicatedVideoMemoryBytes();
        const std::uint64_t mb = bytes / (1024ull * 1024ull);
        if (mb > 0 && mb <= 8192ull) {
            isEightGBClass = true;
            maxStartupEnvs = 1; // studio-only on 8 GB
        }
    }

    int successCount = 0;
    bool envBudgetReached = false;
    for (size_t index = 0; index < envFiles.size(); ++index) {
        const auto& envPath = envFiles[index];
        std::string pathStr = envPath.string();
        std::string name = envPath.stem().string();

        if (!envBudgetReached && successCount < static_cast<int>(maxStartupEnvs)) {
            // Load a limited number of environments synchronously during
            // startup. On 8 GB this is typically just the studio env used
            // by RT showcase; heavier adapters can afford more variety.
            auto texResult = LoadTextureFromFile(pathStr, false, AssetRegistry::TextureKind::Environment);
            if (texResult.IsErr()) {
                spdlog::warn("Failed to load environment from '{}': {}", pathStr, texResult.Error());
                continue;
            }

            auto tex = texResult.Value();

            EnvironmentMaps env;
            env.name = name;
            env.path = pathStr;
            env.diffuseIrradiance = tex;
            env.specularPrefiltered = tex;
            m_environmentMaps.push_back(env);

            spdlog::info(
                "Environment '{}' loaded at startup from '{}': {}x{}, {} mips",
                name,
                pathStr,
                tex->GetWidth(),
                tex->GetHeight(),
                tex->GetMipLevels());

            ++successCount;

            // Once the environment memory budget has been exceeded, stop
            // eagerly loading additional skyboxes and defer them instead so
            // 8 GB-class GPUs do not spend hundreds of MB on unused IBL.
            if (m_assetRegistry.IsEnvironmentBudgetExceeded()) {
                envBudgetReached = true;
            }
        } else {
            PendingEnvironment pending;
            pending.path = pathStr;
            pending.name = name;
            m_pendingEnvironments.push_back(std::move(pending));
        }
    }

    // If no environments loaded, create a fallback placeholder environment
    if (m_environmentMaps.empty()) {
        spdlog::warn("No HDR environments loaded; using placeholder");
        EnvironmentMaps fallback;
        fallback.name = "Placeholder";

        // Build a simple 1x1 white cubemap as a safe fallback so that
        // TextureCube sampling in shaders always has a valid resource.
        std::vector<std::vector<uint8_t>> faces(6);
        for (int f = 0; f < 6; ++f) {
            faces[f].resize(4); // 1x1 RGBA8
            faces[f][0] = 255;
            faces[f][1] = 255;
            faces[f][2] = 255;
            faces[f][3] = 255;
        }

        DX12Texture tex;
        auto initCube = tex.InitializeCubeFromFaces(
            m_device->GetDevice(),
            m_commandQueue->GetCommandQueue(),
            faces,
            1,
            DXGI_FORMAT_R8G8B8A8_UNORM,
            "EnvPlaceholder"
        );
        if (initCube.IsErr()) {
            spdlog::warn("Failed to create placeholder cubemap environment: {}", initCube.Error());
            fallback.diffuseIrradiance = m_placeholderAlbedo;
            fallback.specularPrefiltered = m_placeholderAlbedo;
        } else {
            // CRITICAL: Use staging heap for environment cubemap (copied in material descriptors)
            auto srvResult = m_descriptorManager->AllocateStagingCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                spdlog::warn("Failed to allocate staging SRV for placeholder cubemap: {}", srvResult.Error());
                fallback.diffuseIrradiance = m_placeholderAlbedo;
                fallback.specularPrefiltered = m_placeholderAlbedo;
            } else {
                auto createSRVResult = tex.CreateSRV(m_device->GetDevice(), srvResult.Value());
                if (createSRVResult.IsErr()) {
                    spdlog::warn("Failed to create SRV for placeholder cubemap: {}", createSRVResult.Error());
                    fallback.diffuseIrradiance = m_placeholderAlbedo;
                    fallback.specularPrefiltered = m_placeholderAlbedo;
                } else {
                    uint64_t fence = m_uploadQueue ? m_uploadQueue->Signal() : 0;
                    if (m_uploadQueue && fence != 0) {
                        m_commandQueue->GetCommandQueue()->Wait(m_uploadQueue->GetFence(), fence);
                    }
                    auto cubePtr = std::make_shared<DX12Texture>(std::move(tex));
                    fallback.diffuseIrradiance = cubePtr;
                    fallback.specularPrefiltered = cubePtr;
                }
            }
        }

        m_environmentMaps.push_back(fallback);
    }

    // Ensure current environment index is valid
    m_currentEnvironment = 0;

    // On 8 GB-class adapters, enable the IBL residency limit by default so
    // later environment loads (via the Performance window) cannot silently
    // accumulate more than a small fixed number of skyboxes in VRAM.
    if (isEightGBClass) {
        SetIBLLimitEnabled(true);
    } else {
        // If the limit was toggled on in a previous run, leave it as-is;
        // EnforceIBLResidencyLimit below will respect the current flag.
    }

    // If an IBL residency limit is active, trim any excess environments
    // loaded at startup so that we do not immediately exceed the target
    // number of resident skyboxes on 8 GB-class GPUs.
    EnforceIBLResidencyLimit();

    // Allocate persistent descriptors for shadow + IBL + RT mask/history + RT GI
    // (space1, t0-t6) if not already created.
    if (!m_shadowAndEnvDescriptors[0].IsValid()) {
        for (int i = 0; i < 7; ++i) {
            auto handleResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                return Result<void>::Err("Failed to allocate SRV table for shadow/environment: " + handleResult.Error());
            }
            m_shadowAndEnvDescriptors[i] = handleResult.Value();
        }
    }

    UpdateEnvironmentDescriptorTable();

    spdlog::info(
        "Environment maps initialized: {} loaded eagerly, {} pending for deferred loading (8 GB-class adapter: {})",
        successCount,
        m_pendingEnvironments.size(),
        isEightGBClass ? "YES" : "NO");
    return Result<void>::Ok();
}

Result<void> Renderer::AddEnvironmentFromTexture(const std::shared_ptr<DX12Texture>& tex, const std::string& name) {
    if (!tex) {
        return Result<void>::Err("AddEnvironmentFromTexture called with null texture");
    }

    EnvironmentMaps env;
    env.name = name.empty() ? "DreamerEnv" : name;
    env.path.clear();
    env.diffuseIrradiance = tex;
    env.specularPrefiltered = tex;

    m_environmentMaps.push_back(env);
    EnforceIBLResidencyLimit();
    m_currentEnvironment = m_environmentMaps.size() - 1;

    spdlog::info("Environment '{}' registered from Dreamer texture ({}x{}, {} mips)",
                 env.name, tex->GetWidth(), tex->GetHeight(), tex->GetMipLevels());

    // Ensure descriptor table exists, then refresh bindings.
    if (!m_shadowAndEnvDescriptors[0].IsValid() && m_descriptorManager) {
        for (int i = 0; i < 7; ++i) {
            auto handleResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                return Result<void>::Err("Failed to allocate SRV table for Dreamer environment: " + handleResult.Error());
            }
            m_shadowAndEnvDescriptors[i] = handleResult.Value();
        }
    }

    UpdateEnvironmentDescriptorTable();
    return Result<void>::Ok();
}

void Renderer::UpdateEnvironmentDescriptorTable() {
    if (!m_device || !m_descriptorManager) {
        return;
    }
    if (!m_shadowAndEnvDescriptors[0].IsValid()) {
        return;
    }

    ID3D12Device* device = m_device->GetDevice();

    // Slot 0 (t4): shadow map array, or a neutral placeholder if shadows are unavailable.
    DescriptorHandle shadowSrc = m_shadowMapSRV;
    if (!shadowSrc.IsValid() && m_placeholderRoughness) {
        shadowSrc = m_placeholderRoughness->GetSRV();
    }
    if (shadowSrc.IsValid()) {
        device->CopyDescriptorsSimple(
            1,
            m_shadowAndEnvDescriptors[0].cpu,
            shadowSrc.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }

    // Environment selection
    DescriptorHandle diffuseSrc;
    DescriptorHandle specularSrc;

    if (!m_environmentMaps.empty()) {
        size_t envIndex = m_currentEnvironment;
        if (envIndex >= m_environmentMaps.size()) {
            envIndex = 0;
        }
        const EnvironmentMaps& env = m_environmentMaps[envIndex];

        if (env.diffuseIrradiance && env.diffuseIrradiance->GetSRV().IsValid()) {
            diffuseSrc = env.diffuseIrradiance->GetSRV();
        }
        if (env.specularPrefiltered && env.specularPrefiltered->GetSRV().IsValid()) {
            specularSrc = env.specularPrefiltered->GetSRV();
        }
    }

    // If no environment texture is available, fall back to placeholders when
    // present; otherwise leave the descriptors as null SRVs.
    if (!diffuseSrc.IsValid() && m_placeholderAlbedo && m_placeholderAlbedo->GetSRV().IsValid()) {
        diffuseSrc = m_placeholderAlbedo->GetSRV();
    }
    if (!specularSrc.IsValid() && m_placeholderAlbedo && m_placeholderAlbedo->GetSRV().IsValid()) {
        specularSrc = m_placeholderAlbedo->GetSRV();
    }

    if (diffuseSrc.IsValid()) {
        device->CopyDescriptorsSimple(
            1,
            m_shadowAndEnvDescriptors[1].cpu,
            diffuseSrc.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }

    if (specularSrc.IsValid()) {
        device->CopyDescriptorsSimple(
            1,
            m_shadowAndEnvDescriptors[2].cpu,
            specularSrc.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }

    // Optional RT shadow mask and history (t3, t4). When unavailable the
    // PBR shader simply reads cascaded shadows.
    if (m_rtShadowMaskSRV.IsValid()) {
        device->CopyDescriptorsSimple(
            1,
            m_shadowAndEnvDescriptors[3].cpu,
            m_rtShadowMaskSRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }
    if (m_rtShadowMaskHistorySRV.IsValid()) {
        device->CopyDescriptorsSimple(
            1,
            m_shadowAndEnvDescriptors[4].cpu,
            m_rtShadowMaskHistorySRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }

    // Optional RT diffuse GI buffer (t5). When unavailable the PBR shader
    // falls back to SSAO + ambient only.
    if (m_rtGISRV.IsValid()) {
        device->CopyDescriptorsSimple(
            1,
            m_shadowAndEnvDescriptors[5].cpu,
            m_rtGISRV.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    }
}

void Renderer::ProcessPendingEnvironmentMaps(uint32_t maxPerFrame) {
    if (maxPerFrame == 0 || m_pendingEnvironments.empty()) {
        return;
    }

    uint32_t processedThisFrame = 0;
    while (processedThisFrame < maxPerFrame && !m_pendingEnvironments.empty()) {
        PendingEnvironment pending = m_pendingEnvironments.back();
        m_pendingEnvironments.pop_back();

        auto texResult = LoadTextureFromFile(pending.path, false, AssetRegistry::TextureKind::Environment);
        if (texResult.IsErr()) {
            spdlog::warn(
                "Deferred environment load failed for '{}': {}",
                pending.path,
                texResult.Error());
            continue;
        }

        auto tex = texResult.Value();

        EnvironmentMaps env;
        env.name = pending.name;
        env.path = pending.path;
        env.diffuseIrradiance = tex;
        env.specularPrefiltered = tex;
        m_environmentMaps.push_back(env);
        EnforceIBLResidencyLimit();

        spdlog::info(
            "Deferred environment '{}' loaded from '{}': {}x{}, {} mips ({} remaining)",
            env.name,
            pending.path,
            tex->GetWidth(),
            tex->GetHeight(),
            tex->GetMipLevels(),
            m_pendingEnvironments.size());

        processedThisFrame++;
    }

    if (m_pendingEnvironments.empty()) {
        spdlog::info("All deferred environment maps loaded (total environments: {})", m_environmentMaps.size());
    }
}

void Renderer::LoadAdditionalEnvironmentMaps(uint32_t maxToLoad) {
    if (maxToLoad == 0) {
        return;
    }
    ProcessPendingEnvironmentMaps(maxToLoad);
}

void Renderer::SetIBLLimitEnabled(bool enabled) {
    if (m_iblLimitEnabled == enabled) {
        return;
    }
    m_iblLimitEnabled = enabled;
    if (m_iblLimitEnabled) {
        EnforceIBLResidencyLimit();
    }
}

void Renderer::EnforceIBLResidencyLimit() {
    if (!m_iblLimitEnabled) {
        return;
    }
    if (m_environmentMaps.size() <= kMaxIBLResident) {
        return;
    }

    bool changed = false;
    // Evict oldest environments in FIFO order while keeping the current
    // environment resident whenever possible.
    while (m_environmentMaps.size() > kMaxIBLResident) {
        if (m_environmentMaps.empty()) {
            break;
        }

        size_t victimIndex = std::numeric_limits<size_t>::max();
        for (size_t i = 0; i < m_environmentMaps.size(); ++i) {
            if (i != m_currentEnvironment) {
                victimIndex = i;
                break;
            }
        }

        if (victimIndex == std::numeric_limits<size_t>::max()) {
            // Only the current environment is resident; nothing to evict.
            break;
        }

        EnvironmentMaps victim = m_environmentMaps[victimIndex];
        spdlog::info("IBL residency limit: evicting environment '{}' (path='{}') to keep at most {} loaded",
                     victim.name,
                     victim.path,
                     kMaxIBLResident);

        // If we know the source path, push it back into the pending queue so
        // it can be reloaded later if needed.
        if (!victim.path.empty()) {
            PendingEnvironment pending;
            pending.path = victim.path;
            pending.name = victim.name;
            m_pendingEnvironments.push_back(std::move(pending));
        }

        m_environmentMaps.erase(m_environmentMaps.begin() + static_cast<std::ptrdiff_t>(victimIndex));
        changed = true;

        if (!m_environmentMaps.empty()) {
            if (victimIndex < m_currentEnvironment && m_currentEnvironment > 0) {
                --m_currentEnvironment;
            } else if (m_currentEnvironment >= m_environmentMaps.size()) {
                m_currentEnvironment = m_environmentMaps.size() - 1;
            }
        } else {
            m_currentEnvironment = 0;
        }
    }

    if (changed && !m_environmentMaps.empty()) {
        UpdateEnvironmentDescriptorTable();
    }
}

#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
Result<void> Renderer::EnsureHyperGeometryScene(Scene::ECS_Registry* registry) {
    if (m_hyperSceneBuilt || !m_hyperGeometry) {
        return Result<void>::Ok();
    }
    if (!registry) {
        return Result<void>::Err("Registry is null; cannot build hyper scene");
    }

    std::vector<std::shared_ptr<Scene::MeshData>> meshes;
    auto view = registry->View<Scene::RenderableComponent>();
    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        if (renderable.mesh) {
            meshes.push_back(renderable.mesh);
        }
    }

    if (meshes.empty()) {
        return Result<void>::Err("No meshes available for Hyper-Geometry scene");
    }

    auto buildResult = m_hyperGeometry->BuildScene(meshes);
    if (buildResult.IsErr()) {
        return buildResult;
    }

    m_hyperSceneBuilt = true;
    return Result<void>::Ok();
}
#endif

void Renderer::RenderShadowPass(Scene::ECS_Registry* registry) {
    if (!registry || !m_shadowMap || !m_shadowPipeline) {
        return;
    }

    // Transition shadow map to depth write
    if (m_shadowMapState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_shadowMap.Get();
        barrier.Transition.StateBefore = m_shadowMapState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);
        m_shadowMapState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    auto view = registry->View<Scene::RenderableComponent, Scene::TransformComponent>();

    // Set pipeline / root signature once
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_shadowPipeline->GetPipelineState());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    for (uint32_t cascadeIndex = 0; cascadeIndex < kShadowCascadeCount; ++cascadeIndex) {
        // Update shadow constants with current cascade index. Use a
        // per-cascade slice in the constant buffer so each cascade
        // sees the correct index even though all draws share a single
        // command list and execution happens later on the GPU.
        ShadowConstants shadowData{};
        shadowData.cascadeIndex = glm::uvec4(cascadeIndex, 0u, 0u, 0u);
        D3D12_GPU_VIRTUAL_ADDRESS shadowCB = m_shadowConstantBuffer.AllocateAndWrite(shadowData);

        // Bind frame constants
        m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);
        // Bind shadow constants (b3)
        m_commandList->SetGraphicsRootConstantBufferView(5, shadowCB);

        // Bind DSV for this cascade
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_shadowMapDSVs[cascadeIndex].cpu;
        m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

        // Clear shadow depth
        m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

        // Set viewport and scissor for shadow map
        m_commandList->RSSetViewports(1, &m_shadowViewport);
        m_commandList->RSSetScissorRects(1, &m_shadowScissor);

        // Draw all geometry
        for (auto entity : view) {
            auto& renderable = view.get<Scene::RenderableComponent>(entity);
            auto& transform = view.get<Scene::TransformComponent>(entity);

            if (!renderable.visible || !renderable.mesh || !renderable.mesh->gpuBuffers) {
                continue;
            }

            ObjectConstants objectData = {};
            objectData.modelMatrix = transform.GetMatrix();
            objectData.normalMatrix = transform.GetNormalMatrix();

            D3D12_GPU_VIRTUAL_ADDRESS objectCB = m_objectConstantBuffer.AllocateAndWrite(objectData);
            m_commandList->SetGraphicsRootConstantBufferView(0, objectCB);

            if (renderable.mesh->gpuBuffers->vertexBuffer && renderable.mesh->gpuBuffers->indexBuffer) {
                D3D12_VERTEX_BUFFER_VIEW vbv = {};
                vbv.BufferLocation = renderable.mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
                vbv.SizeInBytes = static_cast<UINT>(renderable.mesh->positions.size() * sizeof(Vertex));
                vbv.StrideInBytes = sizeof(Vertex);

                D3D12_INDEX_BUFFER_VIEW ibv = {};
                ibv.BufferLocation = renderable.mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
                ibv.SizeInBytes = static_cast<UINT>(renderable.mesh->indices.size() * sizeof(uint32_t));
                ibv.Format = DXGI_FORMAT_R32_UINT;

                m_commandList->IASetVertexBuffers(0, 1, &vbv);
                m_commandList->IASetIndexBuffer(&ibv);

                m_commandList->DrawIndexedInstanced(static_cast<UINT>(renderable.mesh->indices.size()), 1, 0, 0, 0);
            }
        }
    }

    // Optional local light shadows rendered into atlas slices after the
    // cascades, using the view-projection matrices prepared in
    // UpdateFrameConstants.
    if (m_hasLocalShadow && m_localShadowCount > 0) {
        uint32_t maxLocal = std::min(m_localShadowCount, kMaxShadowedLocalLights);
        for (uint32_t i = 0; i < maxLocal; ++i) {
            uint32_t slice = kShadowCascadeCount + i;
            if (slice >= kShadowArraySize) {
                break;
            }

            ShadowConstants shadowData{};
            shadowData.cascadeIndex = glm::uvec4(slice, 0u, 0u, 0u);
            D3D12_GPU_VIRTUAL_ADDRESS shadowCB = m_shadowConstantBuffer.AllocateAndWrite(shadowData);

            // Bind frame constants
            m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);
            // Bind shadow constants (b3)
            m_commandList->SetGraphicsRootConstantBufferView(5, shadowCB);

            // Bind DSV for this local light slice
            D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_shadowMapDSVs[slice].cpu;
            m_commandList->OMSetRenderTargets(0, nullptr, FALSE, &dsv);

            // Clear shadow depth
            m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

            // Set viewport and scissor for shadow map
            m_commandList->RSSetViewports(1, &m_shadowViewport);
            m_commandList->RSSetScissorRects(1, &m_shadowScissor);

            // Draw all geometry
            for (auto entity : view) {
                auto& renderable = view.get<Scene::RenderableComponent>(entity);
                auto& transform = view.get<Scene::TransformComponent>(entity);

                if (!renderable.visible || !renderable.mesh || !renderable.mesh->gpuBuffers) {
                    continue;
                }

                ObjectConstants objectData = {};
                objectData.modelMatrix = transform.GetMatrix();
                objectData.normalMatrix = transform.GetNormalMatrix();

                D3D12_GPU_VIRTUAL_ADDRESS objectCB = m_objectConstantBuffer.AllocateAndWrite(objectData);
                m_commandList->SetGraphicsRootConstantBufferView(0, objectCB);

                if (renderable.mesh->gpuBuffers->vertexBuffer && renderable.mesh->gpuBuffers->indexBuffer) {
                    D3D12_VERTEX_BUFFER_VIEW vbv = {};
                    vbv.BufferLocation = renderable.mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
                    vbv.SizeInBytes = static_cast<UINT>(renderable.mesh->positions.size() * sizeof(Vertex));
                    vbv.StrideInBytes = sizeof(Vertex);

                    D3D12_INDEX_BUFFER_VIEW ibv = {};
                    ibv.BufferLocation = renderable.mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
                    ibv.SizeInBytes = static_cast<UINT>(renderable.mesh->indices.size() * sizeof(uint32_t));
                    ibv.Format = DXGI_FORMAT_R32_UINT;

                    m_commandList->IASetVertexBuffers(0, 1, &vbv);
                    m_commandList->IASetIndexBuffer(&ibv);

                    m_commandList->DrawIndexedInstanced(static_cast<UINT>(renderable.mesh->indices.size()), 1, 0, 0, 0);
                }
            }
        }
    }

    // Transition shadow map for sampling
    if (m_shadowMapState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_shadowMap.Get();
        barrier.Transition.StateBefore = m_shadowMapState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);
        m_shadowMapState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }
}

void Renderer::RenderPostProcess() {
    if (!m_postProcessPipeline || !m_hdrColor) {
        // No HDR/post-process configured; main pass may have rendered directly to back buffer
        return;
    }

    // Transition all post-process input resources to PIXEL_SHADER_RESOURCE and back buffer to RENDER_TARGET.
    // We need to transition: HDR, SSAO, SSR, velocity, TAA intermediate, and RT reflection buffers
    // that will be sampled by the post-process shader.
    D3D12_RESOURCE_BARRIER barriers[10] = {};
    UINT barrierCount = 0;

    if (m_hdrState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_hdrColor.Get();
        barriers[barrierCount].Transition.StateBefore = m_hdrState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_hdrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (m_ssaoTex && m_ssaoState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_ssaoTex.Get();
        barriers[barrierCount].Transition.StateBefore = m_ssaoState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_ssaoState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Transition SSR color buffer (used as t6 in post-process shader)
    if (m_ssrColor && m_ssrState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_ssrColor.Get();
        barriers[barrierCount].Transition.StateBefore = m_ssrState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_ssrState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Transition velocity buffer (used as t7 in post-process shader)
    if (m_velocityBuffer && m_velocityState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_velocityBuffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_velocityState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_velocityState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Transition TAA intermediate buffer (may be sampled in post-process for debugging/effects)
    if (m_taaIntermediate && m_taaIntermediateState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_taaIntermediate.Get();
        barriers[barrierCount].Transition.StateBefore = m_taaIntermediateState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_taaIntermediateState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Transition RT reflection buffer (used as t8 in post-process shader)
    if (m_rtReflectionColor && m_rtReflectionState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_rtReflectionColor.Get();
        barriers[barrierCount].Transition.StateBefore = m_rtReflectionState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_rtReflectionState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Transition RT reflection history buffer (used as t9 in post-process shader)
    if (m_rtReflectionHistory && m_rtReflectionHistoryState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_rtReflectionHistory.Get();
        barriers[barrierCount].Transition.StateBefore = m_rtReflectionHistoryState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCount;
        m_rtReflectionHistoryState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    // Transition back buffer to render target for post-process output
    // Note: PRESENT and COMMON states are equivalent (both 0x0) in D3D12
    barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[barrierCount].Transition.pResource = m_window->GetCurrentBackBuffer();
    barriers[barrierCount].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ++barrierCount;
    m_backBufferUsedAsRTThisFrame = true;

    if (barrierCount > 0) {
        m_commandList->ResourceBarrier(barrierCount, barriers);
    }

    // Set back buffer as render target (no depth)
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_window->GetCurrentRTV();
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    // Set viewport and scissor for fullscreen pass
    D3D12_VIEWPORT viewport = {};
    viewport.Width = static_cast<float>(m_window->GetWidth());
    viewport.Height = static_cast<float>(m_window->GetHeight());
    viewport.MinDepth = 0.0f;
    viewport.MaxDepth = 1.0f;

    D3D12_RECT scissorRect = {};
    scissorRect.left = 0;
    scissorRect.top = 0;
    scissorRect.right = static_cast<LONG>(m_window->GetWidth());
    scissorRect.bottom = static_cast<LONG>(m_window->GetHeight());

    m_commandList->RSSetViewports(1, &viewport);
    m_commandList->RSSetScissorRects(1, &scissorRect);

    // Bind post-process pipeline
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_postProcessPipeline->GetPipelineState());

    // Bind descriptor heap
    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    // Bind frame constants
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

    // Bind a stable SRV table for the post-process shader (t0..t9). The shader
    // samples many slots unconditionally (e.g., RT reflections), so the table
    // must keep fixed slot indices even when certain features are disabled.
    if (!m_hdrSRV.IsValid()) {
        spdlog::error("RenderPostProcess: HDR SRV is invalid");
        return;
    }
    if (m_postProcessSrvTableValid) {
        UpdatePostProcessDescriptorTable();
        m_commandList->SetGraphicsRootDescriptorTable(3, m_postProcessSrvTable[0].gpu);
    } else {
        // Fallback: pack a fixed-width transient table.
        std::array<DescriptorHandle, 10> table{};
        for (size_t i = 0; i < table.size(); ++i) {
            auto allocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
            if (allocResult.IsErr()) {
                spdlog::error("RenderPostProcess: failed to allocate transient SRV slot {}: {}", i, allocResult.Error());
                return;
            }
            table[i] = allocResult.Value();
        }
        for (size_t i = 1; i < table.size(); ++i) {
            if (table[i].index != table[0].index + static_cast<uint32_t>(i)) {
                spdlog::error("RenderPostProcess: transient SRV slots are not contiguous (slot {} index {}, expected {})",
                              i, table[i].index, table[0].index + static_cast<uint32_t>(i));
                return;
            }
        }

        auto copyOrNull = [&](size_t slot, const DescriptorHandle& src, DXGI_FORMAT fmt) {
            if (slot >= table.size()) {
                return;
            }
            if (src.IsValid()) {
                m_device->GetDevice()->CopyDescriptorsSimple(
                    1,
                    table[slot].cpu,
                    src.cpu,
                    D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                return;
            }

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = fmt;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            m_device->GetDevice()->CreateShaderResourceView(nullptr, &srvDesc, table[slot].cpu);
        };

        copyOrNull(0, m_hdrSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
        copyOrNull(1, m_bloomCombinedSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
        copyOrNull(2, m_ssaoSRV, DXGI_FORMAT_R8_UNORM);
        copyOrNull(3, m_historySRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
        copyOrNull(4, m_depthSRV, DXGI_FORMAT_R32_FLOAT);
        copyOrNull(5, m_gbufferNormalRoughnessSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
        copyOrNull(6, m_ssrSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
        copyOrNull(7, m_velocitySRV, DXGI_FORMAT_R16G16_FLOAT);
        copyOrNull(8, m_rtReflectionSRV, DXGI_FORMAT_R16G16B16A16_FLOAT);
        copyOrNull(9, m_rtReflectionHistorySRV, DXGI_FORMAT_R16G16B16A16_FLOAT);

        m_commandList->SetGraphicsRootDescriptorTable(3, table[0].gpu);
    }

    // Bind shadow/IBL SRV table (t4-t6) for cascade visualization / skybox, if available
    if (m_shadowAndEnvDescriptors[0].IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowAndEnvDescriptors[0].gpu);
    }

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);
}

void Renderer::AddDebugLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) {
    DebugLineVertex v0{ a, color };
    DebugLineVertex v1{ b, color };
    m_debugLines.push_back(v0);
    m_debugLines.push_back(v1);
}

void Renderer::ClearDebugLines() {
    m_debugLines.clear();
}

void Renderer::RenderDebugLines() {
    if (m_deviceRemoved || m_debugLinesDisabled || !m_debugLinePipeline || m_debugLines.empty() || !m_window) {
        m_debugLines.clear();
        return;
    }

    ID3D12Device* device = m_device->GetDevice();
    if (!device || !m_commandList) {
        m_debugLines.clear();
        return;
    }

    const UINT vertexCount = static_cast<UINT>(m_debugLines.size());

    // Lazily allocate or grow the upload buffer used for debug lines. We keep
    // a single buffer and reuse it across frames to avoid constant heap
    // allocations, which can cause memory fragmentation or failures on some
    // drivers.
    const UINT requiredCapacity = vertexCount;
    const UINT minCapacity = 4096; // vertices
    UINT newCapacity = m_debugLineVertexCapacity;

    if (!m_debugLineVertexBuffer || m_debugLineVertexCapacity < requiredCapacity) {
        // CRITICAL: If replacing an existing buffer, wait for GPU to finish using it
        if (m_debugLineVertexBuffer) {
            WaitForGPU();
        }

        newCapacity = std::max(requiredCapacity, minCapacity);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<UINT64>(newCapacity) * sizeof(DebugLineVertex);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3D12Resource> newBuffer;
        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&newBuffer));

        if (FAILED(hr)) {
            spdlog::warn("RenderDebugLines: failed to allocate vertex buffer (disabling debug lines for this run)");
            m_debugLinesDisabled = true;
            m_debugLines.clear();
            return;
        }

        m_debugLineVertexBuffer = newBuffer;
        m_debugLineVertexCapacity = newCapacity;
    }

    const UINT bufferSize = vertexCount * sizeof(DebugLineVertex);

    // Upload vertex data.
    void* mapped = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    HRESULT mapHr = m_debugLineVertexBuffer->Map(0, &readRange, &mapped);
    if (SUCCEEDED(mapHr)) {
        memcpy(mapped, m_debugLines.data(), bufferSize);
        m_debugLineVertexBuffer->Unmap(0, nullptr);
    } else {
        spdlog::warn("RenderDebugLines: failed to map vertex buffer (disabling debug lines for this run)");
        CORTEX_REPORT_DEVICE_REMOVED("RenderDebugLines_MapVertexBuffer", mapHr);
        m_debugLinesDisabled = true;
        m_debugLines.clear();
        return;
    }

    // Set pipeline state and render target (back buffer).
    ID3D12Resource* backBuffer = m_window->GetCurrentBackBuffer();
    if (!backBuffer) {
        m_debugLines.clear();
        return;
    }

    m_commandList->SetPipelineState(m_debugLinePipeline->GetPipelineState());
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());

    // Frame constants are already bound by the main render path; ensure
    // object constants are valid by binding an identity transform once.
    ObjectConstants obj{};
    obj.modelMatrix  = glm::mat4(1.0f);
    obj.normalMatrix = glm::mat4(1.0f);
    auto objAddr = m_objectConstantBuffer.AllocateAndWrite(obj);
    m_commandList->SetGraphicsRootConstantBufferView(0, objAddr);

    // IA setup
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = m_debugLineVertexBuffer->GetGPUVirtualAddress();
    vbv.StrideInBytes  = sizeof(DebugLineVertex);
    vbv.SizeInBytes    = bufferSize;

    m_commandList->IASetVertexBuffers(0, 1, &vbv);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

    // Draw all lines in one call.
    m_commandList->DrawInstanced(vertexCount, 1, 0, 0);

    // Clear for next frame.
    m_debugLines.clear();
}

void Renderer::RenderVoxel(Scene::ECS_Registry* registry) {
    // Build or refresh the dense voxel grid from the current scene so the
    // voxel renderer can visualize real geometry instead of a hardcoded test
    // pattern. Errors here are non-fatal; the shader will simply render the
    // background gradient when no grid is available.
    if (registry) {
        auto voxelResult = BuildVoxelGridFromScene(registry);
        if (voxelResult.IsErr()) {
            spdlog::warn("RenderVoxel: {}", voxelResult.Error());
        }
    }

    static bool s_loggedOnce = false;
    if (!s_loggedOnce) {
        spdlog::info("RenderVoxel: voxel backend active, beginning voxel frame");
        s_loggedOnce = true;
    }

    // Minimal fullscreen voxel prototype. Renders directly into the current
    // back buffer using a fullscreen triangle and the experimental voxel
    // raymarch pixel shader. We intentionally bypass the traditional HDR
    // path here so the prototype can stay self-contained.
    if (!m_window || !m_voxelPipeline) {
        return;
    }

    ID3D12Resource* backBuffer = m_window->GetCurrentBackBuffer();
    if (!backBuffer) {
        spdlog::error("RenderVoxel: back buffer is null; skipping frame");
        return;
    }

    // Transition back buffer from PRESENT to RENDER_TARGET.
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = backBuffer;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    m_commandList->ResourceBarrier(1, &barrier);
    m_backBufferUsedAsRTThisFrame = true;

    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_window->GetCurrentRTV();
    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);

    // Clear to a bright color so we can easily confirm that the voxel path
    // is rendering even if the shader fails to draw any geometry.
    const float clearColor[4] = { 0.2f, 0.0f, 0.4f, 1.0f };
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);

    // Viewport + scissor match the window size.
    D3D12_VIEWPORT vp{};
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width    = static_cast<float>(m_window->GetWidth());
    vp.Height   = static_cast<float>(m_window->GetHeight());
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    D3D12_RECT scissor{};
    scissor.left   = 0;
    scissor.top    = 0;
    scissor.right  = static_cast<LONG>(m_window->GetWidth());
    scissor.bottom = static_cast<LONG>(m_window->GetHeight());

    m_commandList->RSSetViewports(1, &vp);
    m_commandList->RSSetScissorRects(1, &scissor);

    // Root signature and descriptor heap match the main renderer so the
    // voxel shader can read FrameConstants via the standard layout and
    // access the dense voxel grid SRV.
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    ID3D12DescriptorHeap* heaps[] = { m_descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandList->SetDescriptorHeaps(1, heaps);

    // Frame constants (b1)
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

    // Voxel grid SRV table (t0). If the grid failed to build or upload we
    // still render a gradient background; the shader simply finds no hits.
    if (m_voxelGridSRV.IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(3, m_voxelGridSRV.gpu);
    }

    // Fullscreen triangle; no vertex buffer required (SV_VertexID path).
    m_commandList->SetPipelineState(m_voxelPipeline->GetPipelineState());
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);
}

Result<void> Renderer::BuildVoxelGridFromScene(Scene::ECS_Registry* registry) {
    if (!registry || !m_device) {
        return Result<void>::Ok();
    }

    // Skip rebuild when the grid is still valid. This keeps voxelization
    // cost tied to scene changes instead of every frame.
    if (!m_voxelGridDirty && !m_voxelGridCPU.empty()) {
        return Result<void>::Ok();
    }

    const uint32_t dim = m_voxelGridDim;
    const size_t voxelCount = static_cast<size_t>(dim) * static_cast<size_t>(dim) * static_cast<size_t>(dim);
    m_voxelGridCPU.assign(voxelCount, 0u);
    m_voxelMaterialIds.clear();
    m_nextVoxelMaterialId = 1;

    // World-space bounds for the voxel volume. These must stay in sync with
    // the values used in VoxelRaymarch.hlsl so CPU voxelization and GPU
    // traversal agree on which region of space is discretized.
    // World-space voxel volume bounds. These are chosen to comfortably
    // enclose the curated hero scenes (Cornell, Dragon, RTShowcase) without
    // being so large that the 128^3 grid becomes too sparse. The same
    // numbers must be kept in sync with VoxelRaymarch.hlsl.
    const glm::vec3 gridMin(-10.0f, -2.0f, -10.0f);
    const glm::vec3 gridMax( 10.0f,  8.0f,  10.0f);
    const glm::vec3 gridSize = gridMax - gridMin;
    const glm::vec3 cellSize = gridSize / static_cast<float>(dim);

    auto view = registry->View<Scene::RenderableComponent, Scene::TransformComponent>();
    auto& rawReg = registry->GetRegistry();

    auto getMaterialId = [&](const Scene::RenderableComponent& r, entt::entity e) -> uint8_t {
        std::string key;
        if (!r.presetName.empty()) {
            key = r.presetName;
        } else {
            if (auto* tag = rawReg.try_get<Scene::TagComponent>(e)) {
                key = tag->tag;
            }
        }

        if (key.empty()) {
            key = "default";
        }

        auto it = m_voxelMaterialIds.find(key);
        if (it != m_voxelMaterialIds.end()) {
            return it->second;
        }

        uint8_t id = m_nextVoxelMaterialId;
        if (id == 0u) {
            id = 1u;
        }
        if (m_nextVoxelMaterialId < 255u) {
            ++m_nextVoxelMaterialId;
        }
        m_voxelMaterialIds.emplace(std::move(key), id);
        return id;
    };

    // Helper: stamp a single world-space point into the dense voxel grid.
    auto stampVoxel = [&](const glm::vec3& wp, uint8_t matId) {
        glm::vec3 local = (wp - gridMin) / cellSize;

        int ix = static_cast<int>(std::floor(local.x));
        int iy = static_cast<int>(std::floor(local.y));
        int iz = static_cast<int>(std::floor(local.z));

        if (ix < 0 || iy < 0 || iz < 0 ||
            ix >= static_cast<int>(dim) ||
            iy >= static_cast<int>(dim) ||
            iz >= static_cast<int>(dim)) {
            return;
        }

        const size_t idx =
            static_cast<size_t>(ix) +
            static_cast<size_t>(iy) * dim +
            static_cast<size_t>(iz) * dim * dim;

        // Only overwrite empty cells so the first material to claim a voxel
        // keeps it; this avoids excessive flicker when multiple meshes touch.
        if (m_voxelGridCPU[idx] == 0u) {
            m_voxelGridCPU[idx] = matId;
        }
    };

    // Helper: stamp a polyline between two world-space points into the grid.
    // This densifies thin geometry and small props by filling voxels along
    // triangle edges instead of marking only the original vertices.
    const float cellDiag = glm::length(cellSize);
    auto stampSegment = [&](const glm::vec3& a, const glm::vec3& b, uint8_t matId) {
        glm::vec3 delta = b - a;
        float len = glm::length(delta);
        if (len <= 1e-4f) {
            stampVoxel(a, matId);
            return;
        }

        // Choose the number of samples so that we take at least one sample
        // per voxel diagonal along the segment, with a small safety factor.
        int steps = static_cast<int>(len / cellDiag * 2.0f);
        steps = std::max(1, steps);

        for (int i = 0; i <= steps; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(steps);
            glm::vec3 p = glm::mix(a, b, t);
            stampVoxel(p, matId);
        }
    };

    // Helper: stamp interior samples for a triangle using a simple barycentric
    // grid. This significantly reduces gaps on large walls and planes by
    // marking voxels across the full triangle area instead of only its
    // edges. The cost is amortized over scene rebuilds, not per-frame.
    auto stampTriangleInterior = [&](const glm::vec3& w0,
                                     const glm::vec3& w1,
                                     const glm::vec3& w2,
                                     uint8_t matId) {
        const int kSubdiv = 6; // ~28 samples per triangle
        for (int i = 0; i <= kSubdiv; ++i) {
            float u = static_cast<float>(i) / static_cast<float>(kSubdiv);
            for (int j = 0; j <= kSubdiv - i; ++j) {
                float v = static_cast<float>(j) / static_cast<float>(kSubdiv);
                float w = 1.0f - u - v;
                if (w < 0.0f) {
                    continue;
                }
                glm::vec3 p = u * w0 + v * w1 + w * w2;
                stampVoxel(p, matId);
            }
        }
    };

    for (auto entity : view) {
        auto& renderable = view.get<Scene::RenderableComponent>(entity);
        auto& transform  = view.get<Scene::TransformComponent>(entity);
        if (!renderable.mesh || !renderable.visible) {
            continue;
        }

        const auto& mesh = *renderable.mesh;
        const auto& positions = mesh.positions;
        if (positions.empty()) {
            continue;
        }

        const glm::mat4 world = transform.worldMatrix;
        const uint8_t matId = getMaterialId(renderable, entity);

        const auto& indices = mesh.indices;

        if (!indices.empty()) {
            // Triangle-based voxelization: stamp vertices and edges for each
            // indexed triangle to get a much denser surface shell, which
            // keeps smaller props and thin features from falling apart.
            const size_t triCount = indices.size() / 3;
            for (size_t tri = 0; tri < triCount; ++tri) {
                const uint32_t i0 = indices[tri * 3 + 0];
                const uint32_t i1 = indices[tri * 3 + 1];
                const uint32_t i2 = indices[tri * 3 + 2];
                if (i0 >= positions.size() || i1 >= positions.size() || i2 >= positions.size()) {
                    continue;
                }

                glm::vec3 w0 = glm::vec3(world * glm::vec4(positions[i0], 1.0f));
                glm::vec3 w1 = glm::vec3(world * glm::vec4(positions[i1], 1.0f));
                glm::vec3 w2 = glm::vec3(world * glm::vec4(positions[i2], 1.0f));

                stampVoxel(w0, matId);
                stampVoxel(w1, matId);
                stampVoxel(w2, matId);

                stampSegment(w0, w1, matId);
                stampSegment(w1, w2, matId);
                stampSegment(w2, w0, matId);

                // Fill the triangle interior with a modest barycentric grid so
                // large planes and walls do not appear as sparse dotted lines.
                stampTriangleInterior(w0, w1, w2, matId);
            }
        } else {
            // Non-indexed meshes: fall back to stamping vertices only.
            for (const auto& p : positions) {
                glm::vec3 wp = glm::vec3(world * glm::vec4(p, 1.0f));
                stampVoxel(wp, matId);
            }
        }
    }

    // Basic diagnostics: count occupied voxels so voxel mode failures can be
    // distinguished between "no data" and shader-side issues.
    size_t filled = 0;
    for (uint32_t v : m_voxelGridCPU) {
        if (v != 0u) {
            ++filled;
        }
    }
    const double density = static_cast<double>(filled) /
        static_cast<double>(m_voxelGridCPU.size());
    spdlog::info("Voxel grid built: dim={} filled={} (density {:.6f})",
                 dim, filled, density);

    auto uploadResult = UploadVoxelGridToGPU();
    if (uploadResult.IsOk()) {
        m_voxelGridDirty = false;
    }
    return uploadResult;
}

Result<void> Renderer::UploadVoxelGridToGPU() {
    if (!m_device || m_voxelGridCPU.empty()) {
        return Result<void>::Ok();
    }

    ID3D12Device* device = m_device->GetDevice();
    if (!device) {
        return Result<void>::Err("UploadVoxelGridToGPU: device is null");
    }

    const UINT64 byteSize = static_cast<UINT64>(m_voxelGridCPU.size() * sizeof(uint32_t));

    // Create or resize the upload buffer backing the voxel grid.
    bool recreate = false;
    if (!m_voxelGridBuffer) {
        recreate = true;
    } else {
        auto desc = m_voxelGridBuffer->GetDesc();
        if (desc.Width < byteSize) {
            recreate = true;
        }
    }

    if (recreate) {
        m_voxelGridBuffer.Reset();

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = byteSize;
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_voxelGridBuffer));

        if (FAILED(hr)) {
            char buf[64];
            sprintf_s(buf, "0x%08X", static_cast<unsigned int>(hr));
            return Result<void>::Err(std::string("Failed to create voxel grid buffer (hr=") + buf + ")");
        }

        // Allocate a persistent SRV slot the first time we create the buffer.
        if (!m_voxelGridSRV.IsValid() && m_descriptorManager) {
            auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                return Result<void>::Err("Failed to allocate SRV for voxel grid: " + srvResult.Error());
            }
            m_voxelGridSRV = srvResult.Value();
        }

        if (m_voxelGridSRV.IsValid()) {
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = DXGI_FORMAT_UNKNOWN;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Buffer.FirstElement = 0;
            srvDesc.Buffer.NumElements = static_cast<UINT>(m_voxelGridCPU.size());
            srvDesc.Buffer.StructureByteStride = sizeof(uint32_t);
            srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

            device->CreateShaderResourceView(m_voxelGridBuffer.Get(), &srvDesc, m_voxelGridSRV.cpu);
        }
    }

    // Upload the CPU voxel data into the buffer.
    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    HRESULT mapHr = m_voxelGridBuffer->Map(0, &readRange, &mapped);
    if (FAILED(mapHr) || !mapped) {
        char buf[64];
        sprintf_s(buf, "0x%08X", static_cast<unsigned int>(mapHr));
        return Result<void>::Err(std::string("Failed to map voxel grid buffer (hr=") + buf + ")");
    }

    memcpy(mapped, m_voxelGridCPU.data(), static_cast<size_t>(byteSize));
    m_voxelGridBuffer->Unmap(0, nullptr);

    return Result<void>::Ok();
}
} // namespace Cortex::Graphics
