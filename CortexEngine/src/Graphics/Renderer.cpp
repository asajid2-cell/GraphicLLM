#include "Renderer.h"
#include "Core/Window.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"
#include "Graphics/TextureLoader.h"
#include "Graphics/MaterialState.h"
#include <spdlog/spdlog.h>
#include <cmath>
#include <array>
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

    // Create constant buffers
    auto cbResult = m_frameConstantBuffer.Initialize(device->GetDevice());
    if (cbResult.IsErr()) {
        return Result<void>::Err("Failed to create frame constant buffer: " + cbResult.Error());
    }

    cbResult = m_objectConstantBuffer.Initialize(device->GetDevice());
    if (cbResult.IsErr()) {
        return Result<void>::Err("Failed to create object constant buffer: " + cbResult.Error());
    }

    cbResult = m_materialConstantBuffer.Initialize(device->GetDevice());
    if (cbResult.IsErr()) {
        return Result<void>::Err("Failed to create material constant buffer: " + cbResult.Error());
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
    m_commandList.Reset();
    for (auto& allocator : m_commandAllocators) {
        allocator.Reset();
    }

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
    m_commandList->Reset(m_commandAllocators[m_frameIndex].Get(), m_pipeline->GetPipelineState());

    // Transition back buffer to render target state
    D3D12_RESOURCE_BARRIER barrier = {};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = m_window->GetCurrentBackBuffer();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;

    m_commandList->ResourceBarrier(1, &barrier);

    // Set render targets
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_window->GetCurrentRTV();
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_depthStencilView.cpu;

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
            frameData.cameraPosition = glm::vec4(transform.position, 1.0f);
            foundCamera = true;
            // Active camera found; skip per-frame debug spam to keep logs clean
            break;
        }
    }

    // Default camera if none found
    if (!foundCamera) {
        spdlog::warn("No active camera found, using default");
        glm::vec3 cameraPos(0.0f, 2.0f, 5.0f);
        glm::vec3 target(0.0f, 0.0f, 0.0f);
        glm::vec3 up(0.0f, 1.0f, 0.0f);

        frameData.viewMatrix = glm::lookAtLH(cameraPos, target, up);
        frameData.projectionMatrix = glm::perspectiveLH_ZO(
            glm::radians(60.0f),
            m_window->GetAspectRatio(),
            0.1f,
            1000.0f
        );
        frameData.viewProjectionMatrix = frameData.projectionMatrix * frameData.viewMatrix;
        frameData.cameraPosition = glm::vec4(cameraPos, 1.0f);
    }

    frameData.time = m_totalTime;
    frameData.deltaTime = deltaTime;

    m_frameConstantBuffer.UpdateData(frameData);
}

void Renderer::RenderScene(Scene::ECS_Registry* registry) {
    // Ensure graphics pipeline and root signature are bound after any compute work
    m_commandList->SetGraphicsRootSignature(m_rootSignature->GetRootSignature());
    m_commandList->SetPipelineState(m_pipeline->GetPipelineState());

    // Bind frame constants
    m_commandList->SetGraphicsRootConstantBufferView(1, m_frameConstantBuffer.gpuAddress);

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

        // Update object constants
        ObjectConstants objectData = {};
        objectData.modelMatrix = transform.GetMatrix();
        objectData.normalMatrix = transform.GetNormalMatrix();
        m_objectConstantBuffer.UpdateData(objectData);

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
        m_materialConstantBuffer.UpdateData(materialData);

        // Bind constants
        m_commandList->SetGraphicsRootConstantBufferView(0, m_objectConstantBuffer.gpuAddress);
        m_commandList->SetGraphicsRootConstantBufferView(2, m_materialConstantBuffer.gpuAddress);

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

void Renderer::EnsureMaterialTextures(Scene::RenderableComponent& renderable) {
    auto tryLoad = [&](std::string& path, std::shared_ptr<DX12Texture>& slot, bool useSRGB, const std::shared_ptr<DX12Texture>& placeholder) {
        const bool isPlaceholder = slot == nullptr || slot == placeholder;
        if (!path.empty()) {
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
    if (!state.descriptorsReady) {
        // If descriptors exist, reuse them; otherwise allocate new ones.
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
        if (!state.descriptors[i].IsValid()) {
            spdlog::error("Material descriptor handle {} is invalid", i);
            return;
        }
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

} // namespace Cortex::Graphics
