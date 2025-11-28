#include "Renderer.h"
#include "Core/Window.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/MaterialState.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <array>
#include <limits>
#include <filesystem>
#include <algorithm>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>

namespace Cortex::Graphics {

Renderer::~Renderer() {
    Shutdown();
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
Result<void> Renderer::Initialize(DX12Device* device, Window* window) {
    if (!device || !window) {
        return Result<void>::Err("Invalid device or window pointer");
    }

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

    // Initialize swap chain (now that we have a command queue)
    auto swapChainResult = window->InitializeSwapChain(device, m_commandQueue.get());
    if (swapChainResult.IsErr()) {
        return Result<void>::Err("Failed to initialize swap chain: " + swapChainResult.Error());
    }

    // Create descriptor heaps
    m_descriptorManager = std::make_unique<DescriptorHeapManager>();
    auto heapResult = m_descriptorManager->Initialize(device->GetDevice());
    if (heapResult.IsErr()) {
        return Result<void>::Err("Failed to create descriptor heaps: " + heapResult.Error());
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

    spdlog::info("Renderer initialized successfully");
    return Result<void>::Ok();
}

void Renderer::Shutdown() {
    if (m_commandQueue) {
        m_commandQueue->Flush();
    }

    if (m_rayTracingContext) {
        m_rayTracingContext->Shutdown();
        m_rayTracingContext.reset();
    }

    m_placeholderAlbedo.reset();
    m_placeholderNormal.reset();
    m_placeholderMetallic.reset();
    m_placeholderRoughness.reset();
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
    if (!m_window || !m_window->GetCurrentBackBuffer()) {
        spdlog::error("Renderer::Render called without a valid back buffer; skipping frame");
        return;
    }

    m_totalTime += deltaTime;

    // Ensure all environment maps are loaded before rendering the scene. This
    // trades a slightly longer startup for stable frame times once the scene
    // becomes interactive.
    ProcessPendingEnvironmentMaps(std::numeric_limits<uint32_t>::max());

    BeginFrame();
    UpdateFrameConstants(deltaTime, registry);

    // Optional ray tracing path (DXR). In this pass we only exercise the
    // plumbing to build a stub TLAS and dispatch a no-op ray pass when
    // both support and the runtime toggle are enabled.
    if (m_rayTracingSupported && m_rayTracingEnabled && m_rayTracingContext) {
        RenderRayTracing(registry);
    }

    // First pass: render depth from directional light
    if (m_shadowsEnabled && m_shadowMap && m_shadowPipeline) {
        RenderShadowPass(registry);
    }

    // Main scene pass
    PrepareMainPass();

    // Draw environment background (skybox) into the HDR target before geometry.
    RenderSkybox();

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
        RenderScene(registry);
    }

    // Screen-space reflections using HDR + depth + G-buffer (optional).
    if (m_ssrEnabled && m_ssrPipeline && m_ssrColor && m_hdrColor && m_gbufferNormalRoughness) {
        // Dedicated helper keeps SSR logic contained.
        RenderSSR();
    }

    // Camera motion vectors for TAA/motion blur (from depth + matrices).
    if (m_motionVectorsPipeline && m_velocityBuffer && m_depthBuffer) {
        RenderMotionVectors();
    }

    // Screen-space ambient occlusion from depth buffer (if enabled)
    RenderSSAO();

    // Bloom passes operating on HDR buffer (if available)
    RenderBloom();

    // Post-process HDR -> back buffer (or no-op if HDR disabled)
    RenderPostProcess();

    // Debug overlay lines rendered after all post-processing so they are not
    // affected by tone mapping, bloom, or TAA.
    RenderDebugLines();

    EndFrame();
}

void Renderer::RenderRayTracing(Scene::ECS_Registry* registry) {
    if (!m_rayTracingSupported || !m_rayTracingEnabled || !m_rayTracingContext || !registry) {
        return;
    }

    ComPtr<ID3D12GraphicsCommandList4> rtCmdList;
    HRESULT hr = m_commandList.As(&rtCmdList);
    if (SUCCEEDED(hr) && rtCmdList) {
        // For now, just exercise the plumbing: build a stub TLAS and dispatch.
        m_rayTracingContext->BuildTLAS(registry, rtCmdList.Get());
        m_rayTracingContext->DispatchRayTracing(rtCmdList.Get());
    }
}

void Renderer::BeginFrame() {
    // Handle window resize: recreate depth buffer when size changes
    if (m_depthBuffer && (m_window->GetWidth() != m_depthBuffer->GetDesc().Width || m_window->GetHeight() != m_depthBuffer->GetDesc().Height)) {
        m_depthBuffer.Reset();
        auto depthResult = CreateDepthBuffer();
        if (depthResult.IsErr()) {
            spdlog::error("Failed to recreate depth buffer on resize: {}", depthResult.Error());
        }
    }
    // Handle HDR target resize
    if (m_hdrColor && (m_window->GetWidth() != m_hdrColor->GetDesc().Width || m_window->GetHeight() != m_hdrColor->GetDesc().Height)) {
        m_hdrColor.Reset();
        auto hdrResult = CreateHDRTarget();
        if (hdrResult.IsErr()) {
            spdlog::error("Failed to recreate HDR target on resize: {}", hdrResult.Error());
        }
    }
    // Handle SSAO target resize (SSAO is rendered at half resolution).
    if (m_ssaoTex) {
        D3D12_RESOURCE_DESC ssaoDesc = m_ssaoTex->GetDesc();
        UINT expectedWidth  = std::max<UINT>(1, m_window->GetWidth()  / 2);
        UINT expectedHeight = std::max<UINT>(1, m_window->GetHeight() / 2);
        if (ssaoDesc.Width != expectedWidth || ssaoDesc.Height != expectedHeight) {
            m_ssaoTex.Reset();
            auto ssaoResult = CreateSSAOResources();
            if (ssaoResult.IsErr()) {
                spdlog::error("Failed to recreate SSAO target on resize: {}", ssaoResult.Error());
                m_ssaoEnabled = false;
            }
        }
    }
    // Propagate resize to ray tracing context so it can adjust any RT targets.
    if (m_rayTracingContext && m_window) {
        m_rayTracingContext->OnResize(m_window->GetWidth(), m_window->GetHeight());
    }

    // Reset dynamic constant buffer offsets (safe because we fence each frame)
    m_objectConstantBuffer.ResetOffset();
    m_materialConstantBuffer.ResetOffset();

    // Reset descriptor heap ring buffer to prevent descriptor aliasing (matches CB approach)
    m_descriptorManager->ResetFrameHeaps();

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

    // Wait for this frame's command allocator to be available
    m_frameIndex = m_window->GetCurrentBackBufferIndex();

    if (m_fenceValues[m_frameIndex] != 0) {
        m_commandQueue->WaitForFenceValue(m_fenceValues[m_frameIndex]);
    }

    // Reset command allocator and list
    m_commandAllocators[m_frameIndex]->Reset();
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), nullptr);
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
        rtvs[numRtvs++] = m_window->GetCurrentRTV();
    }

    m_commandList->OMSetRenderTargets(numRtvs, rtvs, FALSE, &dsv);

    // Clear render targets and depth buffer
    const float clearColor[] = { 0.1f, 0.1f, 0.15f, 1.0f };  // Dark blue
    for (UINT i = 0; i < numRtvs; ++i) {
        m_commandList->ClearRenderTargetView(rtvs[i], clearColor, 0, nullptr);
    }
    m_commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    // Set viewport and scissor
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
    // Transition back buffer to present state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_window->GetCurrentBackBuffer();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_commandList->ResourceBarrier(1, &barrier);

    // Close and execute command list
    m_commandList->Close();
    m_commandQueue->ExecuteCommandList(m_commandList.Get());

    // Present
    m_window->Present();

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
    // find suitable shadow-casting spotlights.
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

    // Temporal AA jitter (in pixels) and corresponding UV delta for history sampling.
    float invWidth = 1.0f / std::max(1.0f, static_cast<float>(m_window->GetWidth()));
    float invHeight = 1.0f / std::max(1.0f, static_cast<float>(m_window->GetHeight()));

    glm::vec2 jitterPixels(0.0f);
    if (m_taaEnabled) {
        m_taaJitterPrevPixels = m_taaJitterCurrPixels;
        float jx = Halton(m_taaSampleIndex + 1, 2) - 0.5f;
        float jy = Halton(m_taaSampleIndex + 1, 3) - 0.5f;
        m_taaSampleIndex++;
        // Scale jitter down so per-frame shifts are smaller and objects
        // appear more stable while still providing subpixel coverage.
        const float jitterScale = 0.5f; // 50% of original amplitude
        jitterPixels = glm::vec2(jx, jy) * jitterScale;
        m_taaJitterCurrPixels = jitterPixels;
    } else {
        m_taaJitterPrevPixels = glm::vec2(0.0f);
        m_taaJitterCurrPixels = glm::vec2(0.0f);
    }

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
        outLight.position_type = glm::vec4(lightXform.position, type == Scene::LightType::Point ? 1.0f : 2.0f);

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
            type == Scene::LightType::Spot &&
            m_localShadowCount < kMaxShadowedLocalLights)
        {
            uint32_t localIndex = m_localShadowCount;
            uint32_t slice = kShadowCascadeCount + localIndex;

            shadowIndex = static_cast<float>(slice);
            localLightPos[localIndex] = lightXform.position;
            localLightDir[localIndex] = dir;
            localLightRange[localIndex] = lightComp.range;
            localOuterDegrees[localIndex] = lightComp.outerConeDegrees;

            ++m_localShadowCount;
        }

        outLight.params = glm::vec4(cosOuter, shadowIndex, 0.0f, 0.0f);

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
    frameData.debugMode = glm::vec4(static_cast<float>(m_debugViewMode), 0.0f, 0.0f, 0.0f);

    // Post-process parameters: reciprocal resolution and FXAA flag
    frameData.postParams = glm::vec4(
        invWidth,
        invHeight,
        (m_taaEnabled ? 0.0f : (m_fxaaEnabled ? 1.0f : 0.0f)),
        0.0f);

    // Image-based lighting parameters
    float iblEnabled = m_iblEnabled ? 1.0f : 0.0f;
    frameData.envParams = glm::vec4(
        m_iblDiffuseIntensity,
        m_iblSpecularIntensity,
        iblEnabled,
        static_cast<float>(m_currentEnvironment));

    // Color grading parameters (warm/cool) for post-process
    frameData.colorGrade = glm::vec4(m_colorGradeWarm, m_colorGradeCool, 0.0f, 0.0f);

    // Exponential height fog parameters
    frameData.fogParams = glm::vec4(
        m_fogDensity,
        m_fogHeight,
        m_fogFalloff,
        m_fogEnabled ? 1.0f : 0.0f);

    // SSAO parameters packed into aoParams
    frameData.aoParams = glm::vec4(
        m_ssaoEnabled ? 1.0f : 0.0f,
        m_ssaoRadius,
        m_ssaoBias,
        m_ssaoIntensity);

    // Bloom shaping parameters
    frameData.bloomParams = glm::vec4(
        m_bloomThreshold,
        m_bloomSoftKnee,
        m_bloomMaxContribution,
        0.0f);

    // TAA parameters: history UV offset from jitter delta and blend factor / enable flag.
    // Only enable TAA in the shader once we have a valid history buffer;
    // this avoids sampling uninitialized history and causing color flashes
    // on the first frame after startup or resize.
    glm::vec2 jitterDeltaPixels = m_taaJitterPrevPixels - m_taaJitterCurrPixels;
    glm::vec2 jitterDeltaUV = glm::vec2(jitterDeltaPixels.x * invWidth, jitterDeltaPixels.y * invHeight);
    const bool taaActiveThisFrame = m_taaEnabled && m_hasHistory;
    frameData.taaParams = glm::vec4(
        jitterDeltaUV.x,
        jitterDeltaUV.y,
        m_taaBlendFactor,
        taaActiveThisFrame ? 1.0f : 0.0f);

    // Previous and inverse view-projection matrices for TAA reprojection
    if (m_hasPrevViewProj) {
        frameData.prevViewProjectionMatrix = m_prevViewProjMatrix;
    } else {
        frameData.prevViewProjectionMatrix = frameData.viewProjectionMatrix;
    }
    frameData.invViewProjectionMatrix = glm::inverse(frameData.viewProjectionMatrix);

    // Update history for next frame
    m_prevViewProjMatrix = frameData.viewProjectionMatrix;
    m_hasPrevViewProj = true;

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

void Renderer::RenderScene(Scene::ECS_Registry* registry) {
    // Ensure graphics pipeline and root signature are bound after any compute work
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_pipeline->GetPipelineState());

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

        // Update object constants
        ObjectConstants objectData = {};
        objectData.modelMatrix = transform.GetMatrix();
        objectData.normalMatrix = transform.GetNormalMatrix();

        D3D12_GPU_VIRTUAL_ADDRESS objectCB = m_objectConstantBuffer.AllocateAndWrite(objectData);
        D3D12_GPU_VIRTUAL_ADDRESS materialCB = m_materialConstantBuffer.AllocateAndWrite(materialData);

        // Bind constants
        m_commandList->SetGraphicsRootConstantBufferView(0, objectCB);
        m_commandList->SetGraphicsRootConstantBufferView(2, materialCB);

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
            spdlog::warn("  Entity {} has no vertex/index buffers", entityCount);
        }
    }

    if (drawnCount == 0 && entityCount > 0) {
        spdlog::warn("RenderScene: Found {} entities but drew 0!", entityCount);
    }
}

Result<void> Renderer::UploadMesh(std::shared_ptr<Scene::MeshData> mesh) {
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

    const UINT64 vbSize = static_cast<UINT64>(vertices.size() * sizeof(Vertex));
    const UINT64 ibSize = static_cast<UINT64>(mesh->indices.size() * sizeof(uint32_t));

    if (vbSize == 0 || ibSize == 0) {
        spdlog::error(
            "UploadMesh called with empty geometry: vertices={} indices={}",
            vertices.size(),
            mesh->indices.size());
        return Result<void>::Err("Mesh has no vertices or indices");
    }

    // Default heap resources that will be used at draw time
    D3D12_HEAP_PROPERTIES defaultHeap = {};
    defaultHeap.Type = D3D12_HEAP_TYPE_DEFAULT;
    defaultHeap.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    defaultHeap.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    defaultHeap.CreationNodeMask = 1;
    defaultHeap.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC vbDesc = {};
    vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    vbDesc.Width = vbSize;
    vbDesc.Height = 1;
    vbDesc.DepthOrArraySize = 1;
    vbDesc.MipLevels = 1;
    vbDesc.Format = DXGI_FORMAT_UNKNOWN;
    vbDesc.SampleDesc.Count = 1;
    vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    auto gpuBuffers = std::make_shared<MeshBuffers>();
    ComPtr<ID3D12Resource> vertexBuffer;
    HRESULT hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &vbDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&vertexBuffer)
    );
    if (FAILED(hr)) {
        spdlog::error(
            "CreateCommittedResource for vertex buffer failed: hr=0x{:08X}, vbSize={}, vertices={}",
            static_cast<unsigned int>(hr),
            vbSize,
            vertices.size());

        // If the device was removed, log the reason to help diagnosis.
        if (auto* dxDevice = m_device ? m_device->GetDevice() : nullptr) {
            HRESULT removed = dxDevice->GetDeviceRemovedReason();
            if (removed != S_OK) {
                spdlog::error(
                    "DX12 device removed before/while creating vertex buffer: reason=0x{:08X}",
                    static_cast<unsigned int>(removed));
            }
        }

        return Result<void>::Err("Failed to create default-heap vertex buffer");
    }

    D3D12_RESOURCE_DESC ibDesc = vbDesc;
    ibDesc.Width = ibSize;

    ComPtr<ID3D12Resource> indexBuffer;
    hr = device->CreateCommittedResource(
        &defaultHeap,
        D3D12_HEAP_FLAG_NONE,
        &ibDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&indexBuffer)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create default-heap index buffer");
    }

    // Upload buffers (CPU-visible staging)
    D3D12_HEAP_PROPERTIES uploadHeap = defaultHeap;
    uploadHeap.Type = D3D12_HEAP_TYPE_UPLOAD;

    ComPtr<ID3D12Resource> vbUpload;
    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &vbDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vbUpload)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create vertex upload buffer");
    }

    ComPtr<ID3D12Resource> ibUpload;
    hr = device->CreateCommittedResource(
        &uploadHeap,
        D3D12_HEAP_FLAG_NONE,
        &ibDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&ibUpload)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create index upload buffer");
    }

    // Copy CPU data into upload buffers
    D3D12_RANGE readRange = { 0, 0 };
    void* mappedData = nullptr;
    hr = vbUpload->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map vertex upload buffer");
    }
    memcpy(mappedData, vertices.data(), vbSize);
    vbUpload->Unmap(0, nullptr);

    hr = ibUpload->Map(0, &readRange, &mappedData);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to map index upload buffer");
    }
    memcpy(mappedData, mesh->indices.data(), ibSize);
    ibUpload->Unmap(0, nullptr);

    // Record copy + transition commands using pooled upload lists
    uint32_t allocatorIndex = m_uploadAllocatorIndex++ % kUploadPoolSize;
    auto allocatorToUse = m_uploadCommandAllocators[allocatorIndex];
    auto listToUse = m_uploadCommandLists[allocatorIndex];
    if (!allocatorToUse || !listToUse) {
        return Result<void>::Err("Upload command list not initialized");
    }
    // Ensure allocator isn't in-flight
    if (m_uploadFences[allocatorIndex] != 0 && m_uploadQueue && !m_uploadQueue->IsFenceComplete(m_uploadFences[allocatorIndex])) {
        m_uploadQueue->WaitForFenceValue(m_uploadFences[allocatorIndex]);
    }
    allocatorToUse->Reset();
    listToUse->Reset(allocatorToUse.Get(), nullptr);

    listToUse->CopyBufferRegion(vertexBuffer.Get(), 0, vbUpload.Get(), 0, vbSize);
    listToUse->CopyBufferRegion(indexBuffer.Get(), 0, ibUpload.Get(), 0, ibSize);
    listToUse->Close();

    m_uploadQueue->ExecuteCommandList(listToUse.Get());
    uint64_t uploadFence = m_uploadQueue->Signal();
    m_uploadFences[allocatorIndex] = uploadFence;

    // Transition resources on the graphics queue after copy completes (no flush; defer sync to render loop)
    ComPtr<ID3D12CommandAllocator> transitionAllocator;
    HRESULT hrAlloc = device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&transitionAllocator));
    if (FAILED(hrAlloc)) {
        return Result<void>::Err("Failed to create transition command allocator");
    }
    ComPtr<ID3D12GraphicsCommandList> transitionList;
    HRESULT hrList = device->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        transitionAllocator.Get(),
        nullptr,
        IID_PPV_ARGS(&transitionList)
    );
    if (FAILED(hrList)) {
        return Result<void>::Err("Failed to create transition command list");
    }

    D3D12_RESOURCE_BARRIER barriers[2] = {};

    barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[0].Transition.pResource = vertexBuffer.Get();
    barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[1].Transition.pResource = indexBuffer.Get();
    barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    transitionList->ResourceBarrier(2, barriers);
    transitionList->Close();

    // Ensure transition list waits for copy completion, then wait for graphics completion to keep staging buffers alive
    m_commandQueue->GetCommandQueue()->Wait(m_uploadQueue->GetFence(), uploadFence);
    m_commandQueue->ExecuteCommandList(transitionList.Get());
    const uint64_t gfxFence = m_commandQueue->Signal();
    m_commandQueue->WaitForFenceValue(gfxFence);
    m_pendingUploadFence = uploadFence;

    // Store GPU buffers with lifetime tied to mesh
    gpuBuffers->vertexBuffer = vertexBuffer;
    gpuBuffers->indexBuffer = indexBuffer;
    mesh->gpuBuffers = gpuBuffers;

    spdlog::info("Mesh uploaded to default heap: {} vertices, {} indices", vertices.size(), mesh->indices.size());
    return Result<void>::Ok();
}

Result<std::shared_ptr<DX12Texture>> Renderer::LoadTextureFromFile(const std::string& path, bool useSRGB) {
    if (path.empty()) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Empty texture path");
    }

    if (!m_device || !m_commandQueue || !m_descriptorManager) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Renderer is not initialized");
    }

    auto imageResult = TextureLoader::LoadImageRGBAWithMips(path, true);
    if (imageResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(imageResult.Error());
    }

    DX12Texture texture;
    std::vector<std::vector<uint8_t>> mipData;
    uint32_t width = imageResult.Value().front().width;
    uint32_t height = imageResult.Value().front().height;
    for (const auto& mip : imageResult.Value()) {
        mipData.push_back(mip.pixels);
    }
    auto initResult = texture.InitializeFromMipChain(
        m_device->GetDevice(),
        m_uploadQueue ? m_uploadQueue->GetCommandQueue() : nullptr,
        m_commandQueue->GetCommandQueue(),
        mipData,
        width,
        height,
        useSRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM,
        path
    );
    if (initResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(initResult.Error());
    }

    auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err("Failed to allocate SRV for texture " + path + ": " + srvResult.Error());
    }

    auto createResult = texture.CreateSRV(m_device->GetDevice(), srvResult.Value());
    if (createResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(createResult.Error());
    }

    // Ensure upload completion before using on graphics queue
    uint64_t fence = m_uploadQueue ? m_uploadQueue->Signal() : 0;
    if (m_uploadQueue && fence != 0) {
        m_commandQueue->GetCommandQueue()->Wait(m_uploadQueue->GetFence(), fence);
    }
    return Result<std::shared_ptr<DX12Texture>>::Ok(std::make_shared<DX12Texture>(std::move(texture)));
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
        m_uploadQueue ? m_uploadQueue->GetCommandQueue() : nullptr,
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

    auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(
            "Failed to allocate SRV for Dreamer texture '" + debugName + "': " + srvResult.Error());
    }

    auto createResult = texture.CreateSRV(m_device->GetDevice(), srvResult.Value());
    if (createResult.IsErr()) {
        return Result<std::shared_ptr<DX12Texture>>::Err(createResult.Error());
    }

    // Ensure upload completion before using on graphics queue
    uint64_t fence = m_uploadQueue ? m_uploadQueue->Signal() : 0;
    if (m_uploadQueue && fence != 0) {
        m_commandQueue->GetCommandQueue()->Wait(m_uploadQueue->GetFence(), fence);
    }

    return Result<std::shared_ptr<DX12Texture>>::Ok(std::make_shared<DX12Texture>(std::move(texture)));
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

void Renderer::CycleDebugViewMode() {
    // 0 = shaded, 1 = normals, 2 = roughness, 3 = metallic, 4 = albedo,
    // 5 = cascades, 6 = debug screen (post-process / HUD focus), 7 = fractal height,
    // 8 = IBL diffuse only, 9 = IBL specular only, 10 = env direction/UV,
    // 11 = Fresnel (Fibl), 12 = specular mip debug,
    // 13 = SSAO only, 14 = SSAO overlay, 15 = SSR only, 16 = SSR overlay,
    // 17 = forward light debug (heatmap / count), 18 = scene graph / debug lines
    m_debugViewMode = (m_debugViewMode + 1) % 19;
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
        case 18: label = "SceneGraph"; break;
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
    int clamped = std::max(0, std::min(mode, 18));
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
            l.intensity = 14.0f;
            l.range = 25.0f;
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
            l.intensity = 5.0f;
            l.range = 20.0f;
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
            l.intensity = 8.0f;
            l.range = 25.0f;
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
        m_directionalLightIntensity = 3.5f;
        m_ambientLightColor = glm::vec3(0.08f, 0.09f, 0.1f);
        m_ambientLightIntensity = 1.5f;

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
                l.intensity = 10.0f;
                l.range = 10.0f;
                l.castsShadows = (ix == 1 && iz == 1); // center light may cast shadows
            }
        }
        spdlog::info("Applied lighting rig: TopDownWarehouse");
        break;
    }

    case LightingRig::HorrorSideLight: {
        // Reduce ambient and use a single harsh side light plus a dim back fill.
        m_directionalLightDirection = glm::normalize(glm::vec3(-0.2f, 1.0f, 0.0f));
        m_directionalLightColor = glm::vec3(0.8f, 0.7f, 0.6f);
        m_directionalLightIntensity = 2.0f;
        m_ambientLightColor = glm::vec3(0.01f, 0.01f, 0.02f);
        m_ambientLightIntensity = 0.5f;

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
            l.intensity = 18.0f;
            l.range = 20.0f;
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
            l.intensity = 3.0f;
            l.range = 10.0f;
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
        m_directionalLightIntensity = 1.5f;
        m_ambientLightColor = glm::vec3(0.02f, 0.03f, 0.05f);
        m_ambientLightIntensity = 0.7f;

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
            l.intensity = 24.0f;
            l.range = 18.0f;
            // Let every second lantern cast shadows; the renderer will pick up
            // to kMaxShadowedLocalLights of these for actual shadow maps.
            l.castsShadows = (i % 2 == 0);
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
                spdlog::warn("Failed to load texture '{}': {}", path, loaded.Error());
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

void Renderer::RefreshMaterialDescriptors(Scene::RenderableComponent& renderable) {
    auto& tex = renderable.textures;
    if (!tex.gpuState) {
        tex.gpuState = std::make_shared<MaterialGPUState>();
    }
    auto& state = *tex.gpuState;

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
        auto srcHandle = sources[i] && sources[i]->GetSRV().IsValid() ? sources[i]->GetSRV() : fallback->GetSRV();

        m_device->GetDevice()->CopyDescriptorsSimple(
            1,
            state.descriptors[i].cpu,
            srcHandle.cpu,
            D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
        );
    }

    state.descriptorsReady = true;
}

Result<void> Renderer::CreateDepthBuffer() {
    D3D12_RESOURCE_DESC depthDesc = {};
    depthDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    depthDesc.Width = m_window->GetWidth();
    depthDesc.Height = m_window->GetHeight();
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
        return Result<void>::Err("Failed to create depth buffer");
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

    // Create SRV for depth sampling (SSAO)
    auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate SRV for depth buffer: " + srvResult.Error());
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

    // Create SRV for sampling shadow map
    auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate SRV for shadow map: " + srvResult.Error());
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

Result<void> Renderer::CreateHDRTarget() {
    if (!m_device || !m_descriptorManager) {
        return Result<void>::Err("Renderer not initialized for HDR target creation");
    }

    const UINT width = m_window->GetWidth();
    const UINT height = m_window->GetHeight();

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
        return Result<void>::Err("Failed to create HDR color target");
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

    // SRV
    auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate SRV for HDR target: " + srvResult.Error());
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

    spdlog::info("HDR target created: {}x{}", width, height);

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

        // SRV for sampling G-buffer in SSR/post
        auto gbufSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (gbufSrvResult.IsErr()) {
            spdlog::warn("Failed to allocate SRV for normal/roughness G-buffer: {}", gbufSrvResult.Error());
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

    // (Re)create history color buffer for temporal AA (LDR, back-buffer format)
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
    historyDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    historyDesc.SampleDesc.Count = 1;
    historyDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &historyDesc,
        D3D12_RESOURCE_STATE_COPY_DEST,
        nullptr,
        IID_PPV_ARGS(&m_historyColor)
    );

    if (FAILED(hr)) {
        spdlog::warn("Failed to create TAA history buffer");
    } else {
        m_historyState = D3D12_RESOURCE_STATE_COPY_DEST;

        if (!m_historySRV.IsValid()) {
            auto historySrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (historySrvResult.IsErr()) {
                spdlog::warn("Failed to allocate SRV for TAA history: {}", historySrvResult.Error());
            } else {
                m_historySRV = historySrvResult.Value();

                D3D12_SHADER_RESOURCE_VIEW_DESC historySrvDesc = {};
                historySrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
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

        auto ssrSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (ssrSrvResult.IsErr()) {
            spdlog::warn("Failed to allocate SRV for SSR buffer: {}", ssrSrvResult.Error());
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

        auto velSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (velSrvResult.IsErr()) {
            spdlog::warn("Failed to allocate SRV for motion vector buffer: {}", velSrvResult.Error());
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

    // Store compiled shaders (we'll use them in CreatePipeline)
    // For now, we'll just recreate the root signature and pipeline

    m_rootSignature = std::make_unique<DX12RootSignature>();
    auto rsResult = m_rootSignature->Initialize(m_device->GetDevice());
    if (rsResult.IsErr()) {
        return Result<void>::Err("Failed to create root signature: " + rsResult.Error());
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
    bloomDownDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
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
    bloomBlurHDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
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
    bloomBlurVDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
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
    bloomCompositeDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
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

        auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (srvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate SRV for placeholder: " + srvResult.Error());
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

    spdlog::info("Placeholder textures created");
    return Result<void>::Ok();
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

    int successCount = 0;
    for (size_t index = 0; index < envFiles.size(); ++index) {
        const auto& envPath = envFiles[index];
        std::string pathStr = envPath.string();
        std::string name = envPath.stem().string();

        // Load all environments synchronously during startup so that by the
        // time the scene becomes interactive, HDR backgrounds and IBL are
        // fully available and won't cause hitches while moving the camera.
        auto texResult = LoadTextureFromFile(pathStr, false);
        if (texResult.IsErr()) {
            spdlog::warn("Failed to load environment from '{}': {}", pathStr, texResult.Error());
            continue;
        }

        auto tex = texResult.Value();

        EnvironmentMaps env;
        env.name = name;
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
            auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (srvResult.IsErr()) {
                spdlog::warn("Failed to allocate SRV for placeholder cubemap: {}", srvResult.Error());
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

    // Allocate persistent descriptors for shadow + IBL (t4-t6) if not already created.
    if (!m_shadowAndEnvDescriptors[0].IsValid()) {
        for (int i = 0; i < 3; ++i) {
            auto handleResult = m_descriptorManager->AllocateCBV_SRV_UAV();
            if (handleResult.IsErr()) {
                return Result<void>::Err("Failed to allocate SRV table for shadow/environment: " + handleResult.Error());
            }
            m_shadowAndEnvDescriptors[i] = handleResult.Value();
        }
    }

    UpdateEnvironmentDescriptorTable();

    spdlog::info(
        "Environment maps initialized: {} loaded eagerly, 0 pending for deferred loading",
        successCount);
    return Result<void>::Ok();
}

Result<void> Renderer::AddEnvironmentFromTexture(const std::shared_ptr<DX12Texture>& tex, const std::string& name) {
    if (!tex) {
        return Result<void>::Err("AddEnvironmentFromTexture called with null texture");
    }

    EnvironmentMaps env;
    env.name = name.empty() ? "DreamerEnv" : name;
    env.diffuseIrradiance = tex;
    env.specularPrefiltered = tex;

    m_environmentMaps.push_back(env);
    m_currentEnvironment = m_environmentMaps.size() - 1;

    spdlog::info("Environment '{}' registered from Dreamer texture ({}x{}, {} mips)",
                 env.name, tex->GetWidth(), tex->GetHeight(), tex->GetMipLevels());

    // Ensure descriptor table exists, then refresh bindings.
    if (!m_shadowAndEnvDescriptors[0].IsValid() && m_descriptorManager) {
        for (int i = 0; i < 3; ++i) {
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
    size_t envIndex = m_currentEnvironment;
    if (m_environmentMaps.empty() || envIndex >= m_environmentMaps.size()) {
        envIndex = 0;
    }
    const EnvironmentMaps& env = m_environmentMaps[envIndex];

    DescriptorHandle diffuseSrc =
        (env.diffuseIrradiance && env.diffuseIrradiance->GetSRV().IsValid())
            ? env.diffuseIrradiance->GetSRV()
            : (m_placeholderAlbedo ? m_placeholderAlbedo->GetSRV() : DescriptorHandle{});

    DescriptorHandle specularSrc =
        (env.specularPrefiltered && env.specularPrefiltered->GetSRV().IsValid())
            ? env.specularPrefiltered->GetSRV()
            : (m_placeholderAlbedo ? m_placeholderAlbedo->GetSRV() : DescriptorHandle{});

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
}

void Renderer::ProcessPendingEnvironmentMaps(uint32_t maxPerFrame) {
    if (maxPerFrame == 0 || m_pendingEnvironments.empty()) {
        return;
    }

    uint32_t processedThisFrame = 0;
    while (processedThisFrame < maxPerFrame && !m_pendingEnvironments.empty()) {
        PendingEnvironment pending = m_pendingEnvironments.back();
        m_pendingEnvironments.pop_back();

        auto texResult = LoadTextureFromFile(pending.path, false);
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
        env.diffuseIrradiance = tex;
        env.specularPrefiltered = tex;
        m_environmentMaps.push_back(env);

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

    // Transition HDR/SSAO to shader resource and back buffer to render target
    D3D12_RESOURCE_BARRIER barriers[3] = {};
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

    barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barriers[barrierCount].Transition.pResource = m_window->GetCurrentBackBuffer();
    barriers[barrierCount].Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ++barrierCount;

    m_commandList->ResourceBarrier(barrierCount, barriers);

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

    // Allocate transient descriptors for HDR (t0), bloom (t1), SSAO (t2), and optional TAA history (t3)
    if (!m_hdrSRV.IsValid()) {
        spdlog::error("RenderPostProcess: HDR SRV is invalid");
        return;
    }

    auto hdrHandleResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
    if (hdrHandleResult.IsErr()) {
        spdlog::error("RenderPostProcess: failed to allocate transient HDR SRV: {}", hdrHandleResult.Error());
        return;
    }
    DescriptorHandle hdrHandle = hdrHandleResult.Value();

    m_device->GetDevice()->CopyDescriptorsSimple(
        1,
        hdrHandle.cpu,
        m_hdrSRV.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
    );

    // Optional bloom SRV (t1) - use final blurred bloom texture if available
    DescriptorHandle bloomHandle = {};
    if (m_bloomCombinedSRV.IsValid()) {
        auto bloomAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (bloomAllocResult.IsOk()) {
            bloomHandle = bloomAllocResult.Value();
            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                bloomHandle.cpu,
                m_bloomCombinedSRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        } else {
            spdlog::warn("RenderPostProcess: failed to allocate transient bloom SRV, disabling bloom for this frame");
            // Ensure post-process shader sees bloomIntensity = 0 so it won't sample t1.
            m_frameDataCPU.timeAndExposure.w = 0.0f;
            m_frameConstantBuffer.UpdateData(m_frameDataCPU);
        }
    }

    // Optional SSAO SRV (t2)
    if (m_ssaoSRV.IsValid() && m_ssaoTex) {
        auto ssaoAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (ssaoAllocResult.IsOk()) {
            DescriptorHandle ssaoHandle = ssaoAllocResult.Value();
            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                ssaoHandle.cpu,
                m_ssaoSRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        } else {
            spdlog::warn("RenderPostProcess: failed to allocate transient SSAO SRV, disabling SSAO for this frame");
            m_frameDataCPU.aoParams.x = 0.0f;
            m_frameConstantBuffer.UpdateData(m_frameDataCPU);
        }
    } else {
        // No SSAO texture; mark AO as disabled so shader skips sampling.
        m_frameDataCPU.aoParams.x = 0.0f;
        m_frameConstantBuffer.UpdateData(m_frameDataCPU);
    }

    // Optional TAA history SRV (t3)
    if (m_taaEnabled && m_hasHistory && m_historySRV.IsValid()) {
        auto historyAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (historyAllocResult.IsOk()) {
            DescriptorHandle historyHandle = historyAllocResult.Value();
            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                historyHandle.cpu,
                m_historySRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        } else {
            // Descriptor heap exhaustion is rare, but if it happens we must
            // ensure the shader does not sample an uninitialized history SRV.
            spdlog::warn("RenderPostProcess: failed to allocate transient history SRV, disabling TAA for this frame");
            m_hasHistory = false;
            m_frameDataCPU.taaParams.w = 0.0f;
            m_frameConstantBuffer.UpdateData(m_frameDataCPU);
        }
    }

    // Depth SRV (t4) for TAA reprojection and debug visualizations.
    if (m_depthSRV.IsValid() && m_depthBuffer) {
        auto depthAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (depthAllocResult.IsOk()) {
            DescriptorHandle depthHandle = depthAllocResult.Value();
            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                depthHandle.cpu,
                m_depthSRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        } else {
            spdlog::warn("RenderPostProcess: failed to allocate transient depth SRV; TAA reprojection will fall back to jitter-only");
        }
    }

    // Normal/roughness G-buffer SRV (t5) for SSR/compositing.
    if (m_gbufferNormalRoughnessSRV.IsValid() && m_gbufferNormalRoughness) {
        auto gbufAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (gbufAllocResult.IsOk()) {
            DescriptorHandle gbufHandle = gbufAllocResult.Value();
            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                gbufHandle.cpu,
                m_gbufferNormalRoughnessSRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        } else {
            spdlog::warn("RenderPostProcess: failed to allocate transient normal/roughness SRV; SSR compositing debug will be limited");
        }
    }

    // SSR color buffer SRV (t6) holding reflection color (rgb) and weight (a).
    if (m_ssrSRV.IsValid() && m_ssrColor) {
        auto ssrAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (ssrAllocResult.IsOk()) {
            DescriptorHandle ssrHandle = ssrAllocResult.Value();
            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                ssrHandle.cpu,
                m_ssrSRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        } else {
            spdlog::warn("RenderPostProcess: failed to allocate transient SSR SRV; reflections will be disabled this frame");
        }
    }

    // Motion vector buffer SRV (t7) for motion-aware TAA and blur.
    if (m_velocitySRV.IsValid() && m_velocityBuffer) {
        auto velAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (velAllocResult.IsOk()) {
            DescriptorHandle velHandle = velAllocResult.Value();
            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                velHandle.cpu,
                m_velocitySRV.cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        } else {
            spdlog::warn("RenderPostProcess: failed to allocate transient velocity SRV; motion-aware TAA/blur will be disabled this frame");
        }
    }

    // Bind SRV table starting at t0
    m_commandList->SetGraphicsRootDescriptorTable(3, hdrHandle.gpu);

    // Bind shadow/IBL SRV table (t4-t6) for cascade visualization / skybox, if available
    if (m_shadowAndEnvDescriptors[0].IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowAndEnvDescriptors[0].gpu);
    }

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);

    // After post-process, copy the LDR back buffer into the history buffer for next frame's TAA.
    if (m_taaEnabled && m_historyColor && m_historySRV.IsValid()) {
        D3D12_RESOURCE_BARRIER barriers[2] = {};
        UINT barrierCountHistory = 0;

        if (m_historyState != D3D12_RESOURCE_STATE_COPY_DEST) {
            barriers[barrierCountHistory].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            barriers[barrierCountHistory].Transition.pResource = m_historyColor.Get();
            barriers[barrierCountHistory].Transition.StateBefore = m_historyState;
            barriers[barrierCountHistory].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
            barriers[barrierCountHistory].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ++barrierCountHistory;
        }

        // Back buffer RT -> COPY_SOURCE for the copy
        barriers[barrierCountHistory].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCountHistory].Transition.pResource = m_window->GetCurrentBackBuffer();
        barriers[barrierCountHistory].Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[barrierCountHistory].Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
        barriers[barrierCountHistory].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        ++barrierCountHistory;

        m_commandList->ResourceBarrier(barrierCountHistory, barriers);

        m_commandList->CopyResource(m_historyColor.Get(), m_window->GetCurrentBackBuffer());

        // Transition back buffer back to RENDER_TARGET and history to PIXEL_SHADER_RESOURCE
        D3D12_RESOURCE_BARRIER postCopyBarriers[2] = {};

        postCopyBarriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postCopyBarriers[0].Transition.pResource = m_window->GetCurrentBackBuffer();
        postCopyBarriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_SOURCE;
        postCopyBarriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        postCopyBarriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        postCopyBarriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        postCopyBarriers[1].Transition.pResource = m_historyColor.Get();
        postCopyBarriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        postCopyBarriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        postCopyBarriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

        m_commandList->ResourceBarrier(2, postCopyBarriers);
        m_historyState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        m_hasHistory = true;
    }
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
    if (m_debugLinesDisabled || !m_debugLinePipeline || m_debugLines.empty() || !m_window) {
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
    if (SUCCEEDED(m_debugLineVertexBuffer->Map(0, &readRange, &mapped))) {
        memcpy(mapped, m_debugLines.data(), bufferSize);
        m_debugLineVertexBuffer->Unmap(0, nullptr);
    } else {
        spdlog::warn("RenderDebugLines: failed to map vertex buffer (disabling debug lines for this run)");
        m_debugLinesDisabled = true;
        m_debugLines.clear();
        return;
    }

    // Set pipeline state and render target (back buffer).
    auto* backBuffer = m_window->GetCurrentBackBuffer();
    if (!backBuffer) {
        m_debugLines.clear();
        return;
    }

    D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
    rtvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;

    // We already transitioned the back buffer in EndFrame; assume it is in
    // RENDER_TARGET state here after RenderPostProcess.

    m_commandList->SetPipelineState(m_debugLinePipeline->GetPipelineState());
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());

    // Frame constants are already bound; ensure object/material CBVs are valid
    // by binding identity constants once.
    ObjectConstants obj{};
    obj.modelMatrix = glm::mat4(1.0f);
    obj.normalMatrix = glm::mat4(1.0f);
    auto objAddr = m_objectConstantBuffer.AllocateAndWrite(obj);
    m_commandList->SetGraphicsRootConstantBufferView(0, objAddr);

    // IA setup
    D3D12_VERTEX_BUFFER_VIEW vbv = {};
    vbv.BufferLocation = m_debugLineVertexBuffer->GetGPUVirtualAddress();
    vbv.StrideInBytes = sizeof(DebugLineVertex);
    vbv.SizeInBytes = bufferSize;

    m_commandList->IASetVertexBuffers(0, 1, &vbv);
    m_commandList->IASetIndexBuffer(nullptr);
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

    // Draw all lines in one call.
    m_commandList->DrawInstanced(vertexCount, 1, 0, 0);

    m_debugLines.clear();
}
} // namespace Cortex::Graphics
