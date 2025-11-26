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
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>

namespace Cortex::Graphics {

Renderer::~Renderer() {
    Shutdown();
}

Result<void> Renderer::Initialize(DX12Device* device, Window* window) {
    if (!device || !window) {
        return Result<void>::Err("Invalid device or window pointer");
    }

    m_device = device;
    m_window = window;

    spdlog::info("Initializing Renderer...");

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

    cbResult = m_shadowConstantBuffer.Initialize(device->GetDevice());
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

    spdlog::info("Renderer initialized successfully");
    return Result<void>::Ok();
}

void Renderer::Shutdown() {
    if (m_commandQueue) {
        m_commandQueue->Flush();
    }

    m_placeholderAlbedo.reset();
    m_placeholderNormal.reset();
    m_placeholderMetallic.reset();
    m_placeholderRoughness.reset();
    m_depthBuffer.Reset();
    m_shadowMap.Reset();
    m_hdrColor.Reset();
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
    m_totalTime += deltaTime;

    BeginFrame();
    UpdateFrameConstants(deltaTime, registry);

    // First pass: render depth from directional light
    if (m_shadowsEnabled && m_shadowMap && m_shadowPipeline) {
        RenderShadowPass(registry);
    }

    // Main scene pass
    PrepareMainPass();

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

    // Bloom passes operating on HDR buffer (if available)
    RenderBloom();

    // Post-process HDR -> back buffer (or no-op if HDR disabled)
    RenderPostProcess();
    EndFrame();
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
    // Main pass renders into HDR target when available, otherwise directly to back buffer
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = {};
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_depthStencilView.cpu;

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
        rtv = m_hdrRTV.cpu;
    } else {
        // Fallback: render directly to back buffer
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = m_window->GetCurrentBackBuffer();
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_commandList->ResourceBarrier(1, &barrier);
        rtv = m_window->GetCurrentRTV();
    }

    m_commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    // Clear render target and depth buffer
    const float clearColor[] = { 0.1f, 0.1f, 0.15f, 1.0f };  // Dark blue
    m_commandList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
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
            frameData.viewProjectionMatrix = frameData.projectionMatrix * frameData.viewMatrix;
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
        frameData.viewProjectionMatrix = frameData.projectionMatrix * frameData.viewMatrix;
        cameraForward = glm::normalize(target - cameraPos);
        frameData.cameraPosition = glm::vec4(cameraPos, 1.0f);
    }

    // Time/exposure and lighting state (w = bloom intensity, disabled if bloom SRV missing)
    float bloom = (m_bloomSRV[0].IsValid() ? m_bloomIntensity : 0.0f);
    frameData.timeAndExposure = glm::vec4(m_totalTime, deltaTime, m_exposure, bloom);

    glm::vec3 ambient = m_ambientLightColor * m_ambientLightIntensity;
    frameData.ambientColor = glm::vec4(ambient, 0.0f);

    // Fill forward light array (light 0 = directional sun)
    glm::vec3 dirToLight = glm::normalize(m_directionalLightDirection);
    glm::vec3 sunColor = m_directionalLightColor * m_directionalLightIntensity;

    uint32_t lightCount = 0;

    // Light 0: directional sun
    frameData.lightCount = glm::uvec4(0u);
    frameData.lights[0].position_type = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // type 0 = directional
    frameData.lights[0].direction_cosInner = glm::vec4(dirToLight, 0.0f);
    frameData.lights[0].color_range = glm::vec4(sunColor, 0.0f);
    frameData.lights[0].params = glm::vec4(0.0f);
    lightCount = 1;

    // Populate additional lights from LightComponent (point/spot)
    auto lightView = registry->View<Scene::LightComponent, Scene::TransformComponent>();
    for (auto entity : lightView) {
        if (lightCount >= 4) {
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
        outLight.params = glm::vec4(cosOuter, 0.0f, 0.0f, 0.0f);

        ++lightCount;
    }

    // Zero any remaining lights
    for (uint32_t i = lightCount; i < 4; ++i) {
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

    frameData.shadowParams = glm::vec4(m_shadowBias, m_shadowPCFRadius, m_shadowsEnabled ? 1.0f : 0.0f, m_pcssEnabled ? 1.0f : 0.0f);
    frameData.debugMode = glm::vec4(static_cast<float>(m_debugViewMode), 0.0f, 0.0f, 0.0f);

    // Post-process parameters: reciprocal resolution and FXAA flag
    float invWidth = 1.0f / std::max(1.0f, static_cast<float>(m_window->GetWidth()));
    float invHeight = 1.0f / std::max(1.0f, static_cast<float>(m_window->GetHeight()));
    frameData.postParams = glm::vec4(invWidth, invHeight, m_fxaaEnabled ? 1.0f : 0.0f, 0.0f);

    m_frameDataCPU = frameData;
    m_frameConstantBuffer.UpdateData(m_frameDataCPU);
}

void Renderer::RenderScene(Scene::ECS_Registry* registry) {
    // Ensure graphics pipeline and root signature are bound after any compute work
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_pipeline->GetPipelineState());

    // Bind frame constants
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

    // Bind shadow map if available
    if (m_shadowMapSRV.IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowMapSRV.gpu);
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

void Renderer::ToggleShadows() {
    m_shadowsEnabled = !m_shadowsEnabled;
    spdlog::info("Shadows {}", m_shadowsEnabled ? "ENABLED" : "DISABLED");
}

void Renderer::CycleDebugViewMode() {
    // 0 = shaded, 1 = normals, 2 = roughness, 3 = metallic, 4 = albedo,
    // 5 = cascades, 6 = debug screen (post-process / HUD focus), 7 = fractal height
    m_debugViewMode = (m_debugViewMode + 1) % 8;
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
    int clamped = std::max(0, std::min(mode, 7));
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
    depthDesc.Format = DXGI_FORMAT_D32_FLOAT;
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

    // Create DSV
    auto dsvResult = m_descriptorManager->AllocateDSV();
    if (dsvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate DSV: " + dsvResult.Error());
    }

    m_depthStencilView = dsvResult.Value();

    m_device->GetDevice()->CreateDepthStencilView(
        m_depthBuffer.Get(),
        nullptr,
        m_depthStencilView.cpu
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
    shadowDesc.DepthOrArraySize = kShadowCascadeCount;
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

    // Create DSVs for each cascade slice
    for (uint32_t i = 0; i < kShadowCascadeCount; ++i) {
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
    srvDesc.Texture2DArray.ArraySize = kShadowCascadeCount;

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

    // (Re)create bloom render targets that depend on HDR size
    auto bloomResult = CreateBloomResources();
    if (bloomResult.IsErr()) {
        spdlog::warn("Failed to create bloom resources: {}", bloomResult.Error());
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

    // Bloom pipelines (fullscreen passes reusing VSMain)
    // Downsample + bright-pass
    m_bloomDownsamplePipeline = std::make_unique<DX12Pipeline>();
    PipelineDesc bloomDownDesc = postDesc;
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
        // Update shadow constants with current cascade index
        ShadowConstants shadowData{};
        shadowData.cascadeIndex = glm::uvec4(cascadeIndex, 0u, 0u, 0u);
        m_shadowConstantBuffer.UpdateData(shadowData);

        // Bind frame constants
        m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);
        // Bind shadow constants (b3)
        m_commandList->SetGraphicsRootConstantBufferView(5, m_shadowConstantBuffer.gpuAddress);

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

    // Transition HDR to shader resource and back buffer to render target
    D3D12_RESOURCE_BARRIER barriers[2] = {};
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

    // Allocate transient descriptors for HDR (t0) and bloom (t1)
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

    // Optional bloom SRV (t1) – use final blurred bloom texture if available
    DescriptorHandle bloomHandle = {};
    if (m_bloomSRV[0].IsValid()) {
        auto bloomAllocResult = m_descriptorManager->AllocateTransientCBV_SRV_UAV();
        if (bloomAllocResult.IsOk()) {
            bloomHandle = bloomAllocResult.Value();
            m_device->GetDevice()->CopyDescriptorsSimple(
                1,
                bloomHandle.cpu,
                m_bloomSRV[0].cpu,
                D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV
            );
        } else {
            spdlog::warn("RenderPostProcess: failed to allocate transient bloom SRV, disabling bloom for this frame");
            // Ensure post-process shader sees bloomIntensity = 0 so it won't sample t1.
            m_frameDataCPU.timeAndExposure.w = 0.0f;
            m_frameConstantBuffer.UpdateData(m_frameDataCPU);
        }
    }

    // Bind SRV table starting at t0
    m_commandList->SetGraphicsRootDescriptorTable(3, hdrHandle.gpu);

    // Bind shadow map SRV for cascade visualization (if available)
    if (m_shadowMapSRV.IsValid()) {
        m_commandList->SetGraphicsRootDescriptorTable(4, m_shadowMapSRV.gpu);
    }

    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->DrawInstanced(3, 1, 0, 0);
}
} // namespace Cortex::Graphics
