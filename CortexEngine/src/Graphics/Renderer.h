#pragma once

#include <memory>
#include <unordered_map>
#include "RHI/DX12Device.h"
#include "RHI/DX12CommandQueue.h"
#include "RHI/DX12Pipeline.h"
#include "RHI/DX12Texture.h"
#include "RHI/DescriptorHeap.h"
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
#include "Graphics/HyperGeometry/HyperGeometryEngine.h"
#endif
#include "ShaderTypes.h"
#include "Utils/Result.h"

namespace Cortex {
    class Window;
    namespace Scene {
        class ECS_Registry;
        struct MeshData;
    }
}

namespace Cortex::Graphics {

// Constant buffer wrapper
template<typename T>
struct ConstantBuffer {
    ComPtr<ID3D12Resource> buffer;
    D3D12_GPU_VIRTUAL_ADDRESS gpuAddress = 0;
    T* mappedData = nullptr;

    Result<void> Initialize(ID3D12Device* device) {
        // Create upload heap buffer
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        // Align to 256 bytes (required for CBVs)
        size_t bufferSize = (sizeof(T) + 255) & ~255;

        D3D12_RESOURCE_DESC bufferDesc = {};
        bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufferDesc.Width = bufferSize;
        bufferDesc.Height = 1;
        bufferDesc.DepthOrArraySize = 1;
        bufferDesc.MipLevels = 1;
        bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufferDesc.SampleDesc.Count = 1;
        bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufferDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&buffer)
        );

        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create constant buffer");
        }

        gpuAddress = buffer->GetGPUVirtualAddress();

        // Map persistently (upload heap allows this)
        D3D12_RANGE readRange = { 0, 0 };
        hr = buffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to map constant buffer");
        }

        return Result<void>::Ok();
    }

    void UpdateData(const T& data) {
        if (mappedData) {
            memcpy(mappedData, &data, sizeof(T));
        }
    }

    ~ConstantBuffer() {
        if (buffer && mappedData) {
            buffer->Unmap(0, nullptr);
            mappedData = nullptr;
        }
    }
};

// Main renderer class
class Renderer {
public:
    Renderer() = default;
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    // Initialize renderer
    Result<void> Initialize(DX12Device* device, Window* window);
    void Shutdown();

    // Main render function
    void Render(Scene::ECS_Registry* registry, float deltaTime);

    // Upload mesh to GPU
    Result<void> UploadMesh(std::shared_ptr<Scene::MeshData> mesh);

    // Get default placeholder texture
    std::shared_ptr<DX12Texture> GetPlaceholderTexture() const { return m_placeholderTexture; }

private:
    void BeginFrame();
    void EndFrame();

    void RenderScene(Scene::ECS_Registry* registry);
    void UpdateFrameConstants(float deltaTime, Scene::ECS_Registry* registry);
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    Result<void> EnsureHyperGeometryScene(Scene::ECS_Registry* registry);
#endif

    Result<void> CreateDepthBuffer();
    Result<void> CreateCommandList();
    Result<void> CompileShaders();
    Result<void> CreatePipeline();
    Result<void> CreatePlaceholderTexture();

    // Graphics resources
    DX12Device* m_device = nullptr;
    Window* m_window = nullptr;

    std::unique_ptr<DX12CommandQueue> m_commandQueue;
    std::unique_ptr<DescriptorHeapManager> m_descriptorManager;
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    std::unique_ptr<HyperGeometry::HyperGeometryEngine> m_hyperGeometry;
#endif

    ComPtr<ID3D12CommandAllocator> m_commandAllocators[3];  // One per frame
    ComPtr<ID3D12GraphicsCommandList> m_commandList;
    uint32_t m_frameIndex = 0;

    // Pipeline state
    std::unique_ptr<DX12RootSignature> m_rootSignature;
    std::unique_ptr<DX12Pipeline> m_pipeline;

    // Constant buffers
    ConstantBuffer<FrameConstants> m_frameConstantBuffer;
    ConstantBuffer<ObjectConstants> m_objectConstantBuffer;
    ConstantBuffer<MaterialConstants> m_materialConstantBuffer;

    // Depth buffer
    ComPtr<ID3D12Resource> m_depthBuffer;
    DescriptorHandle m_depthStencilView;

    // Default resources
    std::shared_ptr<DX12Texture> m_placeholderTexture;

    // Frame state
    float m_totalTime = 0.0f;
    uint64_t m_fenceValues[3] = { 0, 0, 0 };
#ifdef CORTEX_ENABLE_HYPER_EXPERIMENT
    bool m_hyperSceneBuilt = false;
#endif
};

} // namespace Cortex::Graphics
