#include "Renderer.h"
#include "Core/Window.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"
#include <spdlog/spdlog.h>
#include <glm/gtc/matrix_transform.hpp>

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

    m_placeholderTexture.reset();
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
            // Look at the origin so content stays centered
            frameData.viewMatrix = glm::lookAt(
                transform.position,
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f, 1.0f, 0.0f));
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

        frameData.viewMatrix = glm::lookAt(cameraPos, target, up);
        frameData.projectionMatrix = glm::perspective(
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

        // Update material constants
        MaterialConstants materialData = {};
        materialData.albedo = renderable.albedoColor;
        materialData.metallic = renderable.metallic;
        materialData.roughness = renderable.roughness;
        materialData.ao = renderable.ao;
        m_materialConstantBuffer.UpdateData(materialData);

        // Bind constants
        m_commandList->SetGraphicsRootConstantBufferView(0, m_objectConstantBuffer.gpuAddress);
        m_commandList->SetGraphicsRootConstantBufferView(2, m_materialConstantBuffer.gpuAddress);

        // Bind texture
        auto texture = renderable.texture ? renderable.texture : m_placeholderTexture;
        if (texture && texture->GetSRV().IsValid()) {
            m_commandList->SetGraphicsRootDescriptorTable(3, texture->GetSRV().gpu);
        }

        // Bind vertex and index buffers
        if (renderable.mesh->vertexBuffer && renderable.mesh->indexBuffer) {
            D3D12_VERTEX_BUFFER_VIEW vbv = {};
            vbv.BufferLocation = static_cast<ID3D12Resource*>(renderable.mesh->vertexBuffer)->GetGPUVirtualAddress();
            vbv.SizeInBytes = static_cast<UINT>(renderable.mesh->positions.size() * sizeof(Vertex));
            vbv.StrideInBytes = sizeof(Vertex);

            D3D12_INDEX_BUFFER_VIEW ibv = {};
            ibv.BufferLocation = static_cast<ID3D12Resource*>(renderable.mesh->indexBuffer)->GetGPUVirtualAddress();
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

    // Interleave vertex data (position, normal, texcoord)
    std::vector<Vertex> vertices;
    vertices.reserve(mesh->positions.size());

    for (size_t i = 0; i < mesh->positions.size(); ++i) {
        Vertex v;
        v.position = mesh->positions[i];
        v.normal = i < mesh->normals.size() ? mesh->normals[i] : glm::vec3(0, 1, 0);
        v.texCoord = i < mesh->texCoords.size() ? mesh->texCoords[i] : glm::vec2(0, 0);
        vertices.push_back(v);
    }

    // Create vertex buffer
    size_t vbSize = vertices.size() * sizeof(Vertex);
    ComPtr<ID3D12Resource> vertexBuffer;

    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = vbSize;
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&vertexBuffer)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create vertex buffer");
    }

    // Copy data
    void* mappedData;
    D3D12_RANGE readRange = { 0, 0 };
    vertexBuffer->Map(0, &readRange, &mappedData);
    memcpy(mappedData, vertices.data(), vbSize);
    vertexBuffer->Unmap(0, nullptr);

    // Create index buffer
    size_t ibSize = mesh->indices.size() * sizeof(uint32_t);
    ComPtr<ID3D12Resource> indexBuffer;

    bufferDesc.Width = ibSize;
    hr = m_device->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &bufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&indexBuffer)
    );

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create index buffer");
    }

    indexBuffer->Map(0, &readRange, &mappedData);
    memcpy(mappedData, mesh->indices.data(), ibSize);
    indexBuffer->Unmap(0, nullptr);

    // Store GPU buffers (we need to keep them alive)
    mesh->vertexBuffer = vertexBuffer.Detach();
    mesh->indexBuffer = indexBuffer.Detach();

    spdlog::info("Mesh uploaded: {} vertices, {} indices", vertices.size(), mesh->indices.size());
    return Result<void>::Ok();
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
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
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
    // Create a white 2x2 placeholder texture
    uint32_t width = 2;
    uint32_t height = 2;
    std::vector<uint8_t> whitePixels(width * height * 4, 255);

    auto texResult = DX12Texture::CreatePlaceholder(
        m_device->GetDevice(),
        m_commandQueue->GetCommandQueue(),
        width,
        height,
        nullptr  // White
    );

    if (texResult.IsErr()) {
        return Result<void>::Err("Failed to create placeholder texture: " + texResult.Error());
    }

    m_placeholderTexture = std::make_shared<DX12Texture>(std::move(texResult.Value()));

    // Create SRV
    auto srvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
    if (srvResult.IsErr()) {
        return Result<void>::Err("Failed to allocate SRV for placeholder: " + srvResult.Error());
    }

    auto createSRVResult = m_placeholderTexture->CreateSRV(m_device->GetDevice(), srvResult.Value());
    if (createSRVResult.IsErr()) {
        return createSRVResult;
    }

    // Wait for upload to complete
    m_commandQueue->Flush();

    spdlog::info("Placeholder texture created");
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
