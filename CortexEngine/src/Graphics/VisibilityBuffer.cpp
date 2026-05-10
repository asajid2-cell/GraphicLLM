#include "VisibilityBuffer.h"
#include "RHI/DX12Device.h"
#include "RHI/DescriptorHeap.h"
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

Result<void> VisibilityBufferRenderer::Initialize(
    DX12Device* device,
    DescriptorHeapManager* descriptorManager,
    BindlessResourceManager* bindlessManager,
    uint32_t width,
    uint32_t height
) {
    if (!device || !descriptorManager) {
        return Result<void>::Err("VisibilityBuffer requires device and descriptor manager");
    }

    m_device = device;
    m_descriptorManager = descriptorManager;
    m_bindlessManager = bindlessManager;
    m_width = width;
    m_height = height;

    // Create visibility buffer
    auto vbResult = CreateVisibilityBuffer();
    if (vbResult.IsErr()) {
        return vbResult;
    }

    // Create G-buffer outputs
    auto gbResult = CreateGBuffers();
    if (gbResult.IsErr()) {
        return gbResult;
    }

    auto allocateDescriptorTable = [&](auto& table, const char* name) -> Result<void> {
        for (uint32_t frame = 0; frame < kVBFrameCount; ++frame) {
            for (uint32_t slot = 0; slot < static_cast<uint32_t>(table[frame].size()); ++slot) {
                auto descriptorResult = m_descriptorManager->AllocateCBV_SRV_UAV();
                if (descriptorResult.IsErr()) {
                    return Result<void>::Err(
                        std::string("Failed to allocate ") + name + " descriptor table frame " +
                        std::to_string(frame) + " slot " + std::to_string(slot) + ": " +
                        descriptorResult.Error()
                    );
                }
                table[frame][slot] = descriptorResult.Value();

                if (slot > 0 && table[frame][slot].index != table[frame][slot - 1].index + 1) {
                    return Result<void>::Err(
                        std::string(name) + " descriptor table is not contiguous at frame " +
                        std::to_string(frame)
                    );
                }
            }
        }

        return Result<void>::Ok();
    };

    auto resolveVisDepthResult = allocateDescriptorTable(m_resolveVisDepthTables, "VB resolve visibility/depth");
    if (resolveVisDepthResult.IsErr()) {
        return resolveVisDepthResult;
    }

    auto resolveGBufferUavResult = allocateDescriptorTable(m_resolveGBufferUavTables, "VB resolve G-buffer UAV");
    if (resolveGBufferUavResult.IsErr()) {
        return resolveGBufferUavResult;
    }

    for (uint32_t frame = 0; frame < kVBFrameCount; ++frame) {
        auto velocityUavResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (velocityUavResult.IsErr()) {
            return Result<void>::Err(
                "Failed to allocate VB motion velocity UAV descriptor table frame " +
                std::to_string(frame) + ": " + velocityUavResult.Error()
            );
        }
        m_motionVelocityUavTables[frame] = velocityUavResult.Value();

        auto debugDepthSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (debugDepthSrvResult.IsErr()) {
            return Result<void>::Err(
                "Failed to allocate VB debug depth SRV descriptor table frame " +
                std::to_string(frame) + ": " + debugDepthSrvResult.Error()
            );
        }
        m_debugDepthSrvTables[frame] = debugDepthSrvResult.Value();
    }

    auto deferredGBufferResult = allocateDescriptorTable(m_deferredGBufferSrvTables, "VB deferred G-buffer SRV");
    if (deferredGBufferResult.IsErr()) {
        return deferredGBufferResult;
    }

    auto deferredEnvShadowResult = allocateDescriptorTable(m_deferredEnvShadowSrvTables, "VB deferred env/shadow SRV");
    if (deferredEnvShadowResult.IsErr()) {
        return deferredEnvShadowResult;
    }

    // Create triple-buffered instance buffers (one per frame in flight)
    // This prevents race conditions where CPU writes frame N+1 while GPU reads frame N
    D3D12_HEAP_PROPERTIES heapProps = {};
    heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC bufferDesc = {};
    bufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    bufferDesc.Width = m_maxInstances * sizeof(VBInstanceData);
    bufferDesc.Height = 1;
    bufferDesc.DepthOrArraySize = 1;
    bufferDesc.MipLevels = 1;
    bufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    bufferDesc.SampleDesc.Count = 1;
    bufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    for (uint32_t i = 0; i < kVBFrameCount; ++i) {
        HRESULT hr = m_device->GetDevice()->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_instanceBuffer[i])
        );
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to create visibility buffer instance buffer " + std::to_string(i));
        }
        std::wstring bufferName = L"VB_InstanceBuffer_" + std::to_wstring(i);
        m_instanceBuffer[i]->SetName(bufferName.c_str());

        // Persistently map the instance buffer (upload heap allows this)
        D3D12_RANGE readRange = {0, 0}; // We won't read from this buffer on CPU
        hr = m_instanceBuffer[i]->Map(0, &readRange, reinterpret_cast<void**>(&m_instanceBufferMapped[i]));
        if (FAILED(hr)) {
            return Result<void>::Err("Failed to persistently map instance buffer " + std::to_string(i));
        }

        // Create instance buffer SRV for this frame
        auto instanceSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (instanceSrvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate instance SRV " + std::to_string(i) + ": " + instanceSrvResult.Error());
        }
        m_instanceSRV[i] = instanceSrvResult.Value();

        D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_UNKNOWN;
        srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srvDesc.Buffer.FirstElement = 0;
        srvDesc.Buffer.NumElements = m_maxInstances;
        srvDesc.Buffer.StructureByteStride = sizeof(VBInstanceData);
        m_device->GetDevice()->CreateShaderResourceView(
            m_instanceBuffer[i].Get(), &srvDesc, m_instanceSRV[i].cpu
        );
    }
    spdlog::info("Created {} triple-buffered VB instance buffers", kVBFrameCount);

    // Create triple-buffered material buffers (upload heap, persistently mapped).
    // This is populated per-frame by the renderer and consumed by the material resolve compute shader.
    {
        D3D12_HEAP_PROPERTIES matHeapProps{};
        matHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC matDesc{};
        matDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        matDesc.Width = static_cast<UINT64>(m_maxMaterials) * sizeof(VBMaterialConstants);
        matDesc.Height = 1;
        matDesc.DepthOrArraySize = 1;
        matDesc.MipLevels = 1;
        matDesc.Format = DXGI_FORMAT_UNKNOWN;
        matDesc.SampleDesc.Count = 1;
        matDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        for (uint32_t i = 0; i < kVBFrameCount; ++i) {
            HRESULT hrMat = m_device->GetDevice()->CreateCommittedResource(
                &matHeapProps,
                D3D12_HEAP_FLAG_NONE,
                &matDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&m_materialBuffer[i])
            );
            if (FAILED(hrMat)) {
                return Result<void>::Err("Failed to create VB material constants buffer " + std::to_string(i));
            }
            std::wstring bufferName = L"VB_MaterialBuffer_" + std::to_wstring(i);
            m_materialBuffer[i]->SetName(bufferName.c_str());

            D3D12_RANGE readRangeMat{0, 0};
            hrMat = m_materialBuffer[i]->Map(0, &readRangeMat, reinterpret_cast<void**>(&m_materialBufferMapped[i]));
            if (FAILED(hrMat)) {
                return Result<void>::Err("Failed to persistently map VB material constants buffer " + std::to_string(i));
            }
        }
        m_materialCount = 0;
        spdlog::info("Created {} triple-buffered VB material buffers", kVBFrameCount);
    }

    // Create a reflection probe table buffer (upload heap, persistently mapped).
    // This is populated per-frame by the renderer and accessed via bindless
    // index (ResourceDescriptorHeap[]) in deferred lighting.
    {
        D3D12_HEAP_PROPERTIES probeHeapProps{};
        probeHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC probeDesc{};
        probeDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        probeDesc.Width = static_cast<UINT64>(m_maxReflectionProbes) * sizeof(VBReflectionProbe);
        probeDesc.Height = 1;
        probeDesc.DepthOrArraySize = 1;
        probeDesc.MipLevels = 1;
        probeDesc.Format = DXGI_FORMAT_UNKNOWN;
        probeDesc.SampleDesc.Count = 1;
        probeDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT hrProbe = m_device->GetDevice()->CreateCommittedResource(
            &probeHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &probeDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_reflectionProbeBuffer)
        );
        if (FAILED(hrProbe)) {
            return Result<void>::Err("Failed to create VB reflection probe buffer");
        }
        m_reflectionProbeBuffer->SetName(L"VB_ReflectionProbeBuffer");

        D3D12_RANGE readRangeProbe{0, 0};
        hrProbe = m_reflectionProbeBuffer->Map(0, &readRangeProbe, reinterpret_cast<void**>(&m_reflectionProbeBufferMapped));
        if (FAILED(hrProbe)) {
            return Result<void>::Err("Failed to persistently map VB reflection probe buffer");
        }

        auto probeSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (probeSrvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate VB reflection probe SRV: " + probeSrvResult.Error());
        }
        m_reflectionProbeSRV = probeSrvResult.Value();

        D3D12_SHADER_RESOURCE_VIEW_DESC probeSrvDesc{};
        probeSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        probeSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        probeSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        probeSrvDesc.Buffer.FirstElement = 0;
        probeSrvDesc.Buffer.NumElements = m_maxReflectionProbes;
        probeSrvDesc.Buffer.StructureByteStride = sizeof(VBReflectionProbe);
        probeSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        m_device->GetDevice()->CreateShaderResourceView(
            m_reflectionProbeBuffer.Get(), &probeSrvDesc, m_reflectionProbeSRV.cpu
        );

        m_reflectionProbeCount = 0;
    }

    // Create triple-buffered mesh table buffers (upload heap, persistently mapped).
    // This is populated per-frame by ResolveMaterials and consumed by compute shaders.
    // Triple-buffering prevents GPU race conditions when CPU updates mesh table.
    {
        D3D12_HEAP_PROPERTIES meshHeapProps{};
        meshHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC meshDesc{};
        meshDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        meshDesc.Width = static_cast<UINT64>(m_maxMeshes) * sizeof(VBMeshTableEntry);
        meshDesc.Height = 1;
        meshDesc.DepthOrArraySize = 1;
        meshDesc.MipLevels = 1;
        meshDesc.Format = DXGI_FORMAT_UNKNOWN;
        meshDesc.SampleDesc.Count = 1;
        meshDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        for (uint32_t i = 0; i < kVBFrameCount; ++i) {
            HRESULT hrMesh = m_device->GetDevice()->CreateCommittedResource(
                &meshHeapProps,
                D3D12_HEAP_FLAG_NONE,
                &meshDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&m_meshTableBuffer[i])
            );
            if (FAILED(hrMesh)) {
                return Result<void>::Err("Failed to create VB mesh table buffer " + std::to_string(i));
            }
            std::wstring meshName = L"VB_MeshTableBuffer_" + std::to_wstring(i);
            m_meshTableBuffer[i]->SetName(meshName.c_str());

            D3D12_RANGE readRangeMesh{0, 0};
            hrMesh = m_meshTableBuffer[i]->Map(0, &readRangeMesh, reinterpret_cast<void**>(&m_meshTableBufferMapped[i]));
            if (FAILED(hrMesh)) {
                return Result<void>::Err("Failed to persistently map VB mesh table buffer " + std::to_string(i));
            }
        }
        m_meshCount = 0;
    }

    // Reserve persistent SRV slots for clustered-light resources so shaders can
    // access them via ResourceDescriptorHeap[] without per-frame allocations.
    {
        auto localLightsSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (localLightsSrvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate VB local lights SRV: " + localLightsSrvResult.Error());
        }
        m_localLightsSRV = localLightsSrvResult.Value();

        auto clusterRangesSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (clusterRangesSrvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate VB cluster ranges SRV: " + clusterRangesSrvResult.Error());
        }
        m_clusterRangesSRV = clusterRangesSrvResult.Value();

        auto clusterIndicesSrvResult = m_descriptorManager->AllocateCBV_SRV_UAV();
        if (clusterIndicesSrvResult.IsErr()) {
            return Result<void>::Err("Failed to allocate VB cluster indices SRV: " + clusterIndicesSrvResult.Error());
        }
        m_clusterLightIndicesSRV = clusterIndicesSrvResult.Value();

        // Initialize descriptors to null; they'll be updated once resources exist.
        D3D12_SHADER_RESOURCE_VIEW_DESC nullSrv{};
        nullSrv.Format = DXGI_FORMAT_UNKNOWN;
        nullSrv.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        nullSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullSrv.Buffer.FirstElement = 0;
        nullSrv.Buffer.NumElements = 1;
        nullSrv.Buffer.StructureByteStride = sizeof(uint32_t);
        nullSrv.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        m_device->GetDevice()->CreateShaderResourceView(nullptr, &nullSrv, m_localLightsSRV.cpu);
        m_device->GetDevice()->CreateShaderResourceView(nullptr, &nullSrv, m_clusterRangesSRV.cpu);
        m_device->GetDevice()->CreateShaderResourceView(nullptr, &nullSrv, m_clusterLightIndicesSRV.cpu);
    }

    // Create pipelines
    auto pipelineResult = CreatePipelines();
    if (pipelineResult.IsErr()) {
        return pipelineResult;
    }

    // Dummy cull mask buffer (4 bytes). Used when no GPU culling mask is
    // available so the visibility pass can still bind a valid SRV root
    // descriptor (binding 0 is invalid for root SRVs).
    {
        D3D12_HEAP_PROPERTIES dummyHeapProps{};
        dummyHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

        D3D12_RESOURCE_DESC dummyBufferDesc{};
        dummyBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        dummyBufferDesc.Width = sizeof(uint32_t);
        dummyBufferDesc.Height = 1;
        dummyBufferDesc.DepthOrArraySize = 1;
        dummyBufferDesc.MipLevels = 1;
        dummyBufferDesc.SampleDesc.Count = 1;
        dummyBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

        HRESULT dummyHr = m_device->GetDevice()->CreateCommittedResource(
            &dummyHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &dummyBufferDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_dummyCullMaskBuffer)
        );
        if (FAILED(dummyHr)) {
            return Result<void>::Err("Failed to create VB dummy cull mask buffer");
        }
        m_dummyCullMaskBuffer->SetName(L"VB_DummyCullMask");

        uint32_t* mapped = nullptr;
        D3D12_RANGE dummyReadRange{0, 0};
        dummyHr = m_dummyCullMaskBuffer->Map(0, &dummyReadRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(dummyHr) || !mapped) {
            return Result<void>::Err("Failed to map VB dummy cull mask buffer");
        }
        *mapped = 1u;
        m_dummyCullMaskBuffer->Unmap(0, nullptr);
    }

    spdlog::info("VisibilityBuffer initialized ({}x{}, max {} instances)",
                 m_width, m_height, m_maxInstances);

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics

