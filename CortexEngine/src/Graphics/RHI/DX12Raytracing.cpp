#include "DX12Raytracing.h"

#include "DX12Device.h"
#include "DescriptorHeap.h"
#include "Scene/ECS_Registry.h"
#include "../ShaderTypes.h"
#include "../Renderer.h"
#include "Utils/FileUtils.h"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <filesystem>

namespace Cortex::Graphics {

namespace {
    constexpr UINT AlignTo(UINT value, UINT alignment) {
        return (value + alignment - 1u) & ~(alignment - 1u);
    }
}

Result<void> DX12RaytracingContext::Initialize(DX12Device* device, DescriptorHeapManager* descriptors) {
    if (!device) {
        return Result<void>::Err("DX12RaytracingContext::Initialize: device is null");
    }

    ID3D12Device* baseDevice = device->GetDevice();
    if (!baseDevice) {
        return Result<void>::Err("DX12RaytracingContext::Initialize: base D3D12 device is null");
    }

    HRESULT hr = baseDevice->QueryInterface(IID_PPV_ARGS(&m_device5));
    if (FAILED(hr) || !m_device5) {
        return Result<void>::Err("DX12RaytracingContext::Initialize: DXR ID3D12Device5 not available");
    }

    m_descriptors = descriptors;
    m_rtxWidth = 0;
    m_rtxHeight = 0;

    // Allocate persistent descriptors for TLAS, depth, and RT mask if we have
    // a descriptor manager. The renderer will copy the actual SRVs/UAV into
    // these slots before dispatch.
    if (m_descriptors) {
        auto tlasHandle = m_descriptors->AllocateCBV_SRV_UAV();
        auto depthHandle = m_descriptors->AllocateCBV_SRV_UAV();
        auto maskHandle = m_descriptors->AllocateCBV_SRV_UAV();
        if (tlasHandle.IsErr() || depthHandle.IsErr() || maskHandle.IsErr()) {
            spdlog::warn("DX12RaytracingContext: failed to allocate RT descriptor slots; DXR shadows will be disabled");
            m_rtTlasSrv = {};
            m_rtDepthSrv = {};
            m_rtMaskUav = {};
        } else {
            m_rtTlasSrv = tlasHandle.Value();
            m_rtDepthSrv = depthHandle.Value();
            m_rtMaskUav = maskHandle.Value();
        }
    }

    // Load precompiled DXR library for sun shadows.
    std::filesystem::path requestedPath("assets/shaders/RaytracedShadows.dxil");
    std::filesystem::path resolvedPath = requestedPath;
    if (!Utils::FileExists(resolvedPath)) {
        std::filesystem::path cwd = std::filesystem::current_path();
        std::filesystem::path candidate = cwd / requestedPath;
        if (Utils::FileExists(candidate)) {
            resolvedPath = candidate;
        } else {
            candidate = cwd.parent_path() / requestedPath;
            if (Utils::FileExists(candidate)) {
                resolvedPath = candidate;
            } else {
                candidate = cwd.parent_path().parent_path() / requestedPath;
                if (Utils::FileExists(candidate)) {
                    resolvedPath = candidate;
                }
            }
        }
    }

    auto libResult = Utils::ReadBinaryFile(resolvedPath);
    if (libResult.IsErr()) {
        spdlog::warn("DX12RaytracingContext: failed to load RaytracedShadows.dxil: {}", libResult.Error());
        spdlog::info("DX12RaytracingContext initialized (DXR device detected; BLAS/TLAS only, no RT pipeline)");
        return Result<void>::Ok();
    }
    const std::vector<uint8_t>& libBytes = libResult.Value();

    // Build global root signature for the RT shadow pipeline.
    {
        D3D12_ROOT_PARAMETER rootParams[4] = {};

        // b0, space0: FrameConstants
        rootParams[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
        rootParams[0].Descriptor.ShaderRegister = 0;
        rootParams[0].Descriptor.RegisterSpace = 0;
        rootParams[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t0, space2: TLAS SRV
        D3D12_DESCRIPTOR_RANGE tlasRange{};
        tlasRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        tlasRange.NumDescriptors = 1;
        tlasRange.BaseShaderRegister = 0;
        tlasRange.RegisterSpace = 2;
        tlasRange.OffsetInDescriptorsFromTableStart = 0;

        rootParams[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[1].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[1].DescriptorTable.pDescriptorRanges = &tlasRange;
        rootParams[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // t1, space2: depth SRV
        D3D12_DESCRIPTOR_RANGE depthRange{};
        depthRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        depthRange.NumDescriptors = 1;
        depthRange.BaseShaderRegister = 1;
        depthRange.RegisterSpace = 2;
        depthRange.OffsetInDescriptorsFromTableStart = 0;

        rootParams[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[2].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[2].DescriptorTable.pDescriptorRanges = &depthRange;
        rootParams[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // u0, space2: RT shadow mask UAV
        D3D12_DESCRIPTOR_RANGE maskRange{};
        maskRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
        maskRange.NumDescriptors = 1;
        maskRange.BaseShaderRegister = 0;
        maskRange.RegisterSpace = 2;
        maskRange.OffsetInDescriptorsFromTableStart = 0;

        rootParams[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[3].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[3].DescriptorTable.pDescriptorRanges = &maskRange;
        rootParams[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = _countof(rootParams);
        rsDesc.pParameters = rootParams;
        rsDesc.NumStaticSamplers = 0;
        rsDesc.pStaticSamplers = nullptr;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sigBlob;
        ComPtr<ID3DBlob> errorBlob;
        hr = D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &errorBlob);
        if (FAILED(hr)) {
            if (errorBlob) {
                spdlog::warn("DXR global root signature serialization failed: {}",
                             static_cast<const char*>(errorBlob->GetBufferPointer()));
            } else {
                spdlog::warn("DXR global root signature serialization failed (hr=0x{:08X})",
                             static_cast<unsigned int>(hr));
            }
            spdlog::info("DX12RaytracingContext initialized (DXR device detected; BLAS/TLAS only, no RT pipeline)");
            return Result<void>::Ok();
        }

        hr = m_device5->CreateRootSignature(
            0,
            sigBlob->GetBufferPointer(),
            sigBlob->GetBufferSize(),
            IID_PPV_ARGS(&m_rtGlobalRootSignature));
        if (FAILED(hr)) {
            spdlog::warn("DXR global root signature creation failed (hr=0x{:08X})",
                         static_cast<unsigned int>(hr));
            spdlog::info("DX12RaytracingContext initialized (DXR device detected; BLAS/TLAS only, no RT pipeline)");
            return Result<void>::Ok();
        }
    }

    // Build DXR state object (pipeline) for sun shadows.
    {
        // DXIL library
        D3D12_DXIL_LIBRARY_DESC libDesc{};
        D3D12_SHADER_BYTECODE libBytecode{};
        libBytecode.pShaderBytecode = libBytes.data();
        libBytecode.BytecodeLength = libBytes.size();
        libDesc.DXILLibrary = libBytecode;

        D3D12_EXPORT_DESC exports[3] = {};
        exports[0].Name = L"RayGen_Shadow";
        exports[0].ExportToRename = nullptr;
        exports[0].Flags = D3D12_EXPORT_FLAG_NONE;

        exports[1].Name = L"Miss_Shadow";
        exports[1].ExportToRename = nullptr;
        exports[1].Flags = D3D12_EXPORT_FLAG_NONE;

        exports[2].Name = L"ClosestHit_Shadow";
        exports[2].ExportToRename = nullptr;
        exports[2].Flags = D3D12_EXPORT_FLAG_NONE;

        libDesc.NumExports = _countof(exports);
        libDesc.pExports = exports;

        D3D12_STATE_SUBOBJECT subobjects[5] = {};

        // Subobject 0: DXIL library
        subobjects[0].Type = D3D12_STATE_SUBOBJECT_TYPE_DXIL_LIBRARY;
        subobjects[0].pDesc = &libDesc;

        // Subobject 1: Hit group
        D3D12_HIT_GROUP_DESC hitGroup{};
        hitGroup.HitGroupExport = L"ShadowHitGroup";
        hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hitGroup.ClosestHitShaderImport = L"ClosestHit_Shadow";
        hitGroup.AnyHitShaderImport = nullptr;
        hitGroup.IntersectionShaderImport = nullptr;

        subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        subobjects[1].pDesc = &hitGroup;

        // Subobject 2: Shader config (payload + attributes)
        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
        shaderConfig.MaxPayloadSizeInBytes = 4; // bool occluded
        shaderConfig.MaxAttributeSizeInBytes = 8; // barycentrics

        subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        subobjects[2].pDesc = &shaderConfig;

        // Subobject 3: Global root signature
        ID3D12RootSignature* globalRootSig = m_rtGlobalRootSignature.Get();
        subobjects[3].Type = D3D12_STATE_SUBOBJECT_TYPE_GLOBAL_ROOT_SIGNATURE;
        subobjects[3].pDesc = &globalRootSig;

        // Subobject 4: Pipeline config (max recursion)
        D3D12_RAYTRACING_PIPELINE_CONFIG pipelineConfig{};
        pipelineConfig.MaxTraceRecursionDepth = 1;

        subobjects[4].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_PIPELINE_CONFIG;
        subobjects[4].pDesc = &pipelineConfig;

        D3D12_STATE_OBJECT_DESC soDesc{};
        soDesc.Type = D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE;
        soDesc.NumSubobjects = _countof(subobjects);
        soDesc.pSubobjects = subobjects;

        hr = m_device5->CreateStateObject(&soDesc, IID_PPV_ARGS(&m_rtStateObject));
        if (FAILED(hr) || !m_rtStateObject) {
            spdlog::warn("DXR state object creation failed (hr=0x{:08X}); RT shadows disabled",
                         static_cast<unsigned int>(hr));
            m_rtStateObject.Reset();
            m_rtGlobalRootSignature.Reset();
            spdlog::info("DX12RaytracingContext initialized (DXR device detected; BLAS/TLAS only, no RT pipeline)");
            return Result<void>::Ok();
        }

        hr = m_rtStateObject.As(&m_rtStateProps);
        if (FAILED(hr) || !m_rtStateProps) {
            spdlog::warn("DXR state object properties query failed (hr=0x{:08X}); RT shadows disabled",
                         static_cast<unsigned int>(hr));
            m_rtStateProps.Reset();
            m_rtStateObject.Reset();
            m_rtGlobalRootSignature.Reset();
            spdlog::info("DX12RaytracingContext initialized (DXR device detected; BLAS/TLAS only, no RT pipeline)");
            return Result<void>::Ok();
        }

        // Build shader table: [raygen][miss][hitgroup]
        const UINT shaderIdSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        m_rtShaderTableStride = AlignTo(shaderIdSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
        const UINT recordCount = 3;
        const UINT64 tableSize = static_cast<UINT64>(m_rtShaderTableStride) * recordCount;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC tableDesc{};
        tableDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        tableDesc.Alignment = 0;
        tableDesc.Width = tableSize;
        tableDesc.Height = 1;
        tableDesc.DepthOrArraySize = 1;
        tableDesc.MipLevels = 1;
        tableDesc.Format = DXGI_FORMAT_UNKNOWN;
        tableDesc.SampleDesc.Count = 1;
        tableDesc.SampleDesc.Quality = 0;
        tableDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        tableDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        hr = m_device5->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &tableDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_rtShaderTable));
        if (FAILED(hr)) {
            spdlog::warn("DXR shader table allocation failed (hr=0x{:08X}); RT shadows disabled",
                         static_cast<unsigned int>(hr));
            m_rtShaderTable.Reset();
            m_rtStateProps.Reset();
            m_rtStateObject.Reset();
            m_rtGlobalRootSignature.Reset();
            spdlog::info("DX12RaytracingContext initialized (DXR device detected; BLAS/TLAS only, no RT pipeline)");
            return Result<void>::Ok();
        }

        uint8_t* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        if (FAILED(m_rtShaderTable->Map(0, &readRange, reinterpret_cast<void**>(&mapped)))) {
            m_rtShaderTable.Reset();
            m_rtStateProps.Reset();
            m_rtStateObject.Reset();
            m_rtGlobalRootSignature.Reset();
            spdlog::warn("DXR shader table map failed; RT shadows disabled");
            spdlog::info("DX12RaytracingContext initialized (DXR device detected; BLAS/TLAS only, no RT pipeline)");
            return Result<void>::Ok();
        }

        auto writeRecord = [&](UINT recordIndex, const wchar_t* exportName) {
            void* id = m_rtStateProps->GetShaderIdentifier(exportName);
            if (!id) {
                return;
            }
            uint8_t* dst = mapped + static_cast<UINT64>(m_rtShaderTableStride) * recordIndex;
            memcpy(dst, id, shaderIdSize);
            // Zero any remaining padding in the record.
            if (m_rtShaderTableStride > shaderIdSize) {
                memset(dst + shaderIdSize, 0, m_rtShaderTableStride - shaderIdSize);
            }
        };

        writeRecord(0, L"RayGen_Shadow");
        writeRecord(1, L"Miss_Shadow");
        writeRecord(2, L"ShadowHitGroup");

        m_rtShaderTable->Unmap(0, nullptr);
    }

    spdlog::info("DX12RaytracingContext initialized (DXR pipeline + AS builds ready)");
    return Result<void>::Ok();
}

void DX12RaytracingContext::Shutdown() {
    if (m_device5) {
        spdlog::info("DX12RaytracingContext shutdown");
    }

    m_device5.Reset();
    m_descriptors = nullptr;
    m_rtxWidth = 0;
    m_rtxHeight = 0;

    m_blasCache.clear();
    m_tlas.Reset();
    m_tlasScratch.Reset();
    m_instanceBuffer.Reset();
    m_tlasSize = 0;
    m_tlasScratchSize = 0;
    m_instanceBufferSize = 0;
    m_instanceDescs.clear();

    m_rtGlobalRootSignature.Reset();
    m_rtStateObject.Reset();
    m_rtStateProps.Reset();
    m_rtShaderTable.Reset();
    m_rtShaderTableStride = 0;
    m_rtTlasSrv = {};
    m_rtDepthSrv = {};
    m_rtMaskUav = {};
}

void DX12RaytracingContext::OnResize(uint32_t width, uint32_t height) {
    // Avoid redundant work and log noise when the dimensions have not changed.
    if (m_rtxWidth == width && m_rtxHeight == height) {
        return;
    }
    m_rtxWidth = width;
    m_rtxHeight = height;
}

void DX12RaytracingContext::RebuildBLASForMesh(const std::shared_ptr<Scene::MeshData>& mesh) {
    if (!m_device5 || !mesh) {
        return;
    }

    if (!mesh->gpuBuffers ||
        !mesh->gpuBuffers->vertexBuffer ||
        !mesh->gpuBuffers->indexBuffer ||
        mesh->positions.empty() ||
        mesh->indices.empty()) {
        // Mesh is not fully resident on the GPU yet; skip BLAS registration.
        return;
    }

    const Scene::MeshData* key = mesh.get();
    BLASEntry& entry = m_blasCache[key];

    // Fill geometry description for this mesh (single triangle geometry).
    D3D12_RAYTRACING_GEOMETRY_DESC geom{};
    geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    const UINT vertexCount = static_cast<UINT>(mesh->positions.size());
    const UINT indexCount = static_cast<UINT>(mesh->indices.size());

    geom.Triangles.VertexBuffer.StartAddress = mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
    geom.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
    geom.Triangles.VertexCount = vertexCount;
    geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; // position.xyz

    geom.Triangles.IndexBuffer = mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
    geom.Triangles.IndexCount = indexCount;
    geom.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;

    geom.Triangles.Transform3x4 = 0; // identity

    entry.geomDesc = geom;
    entry.hasGeometry = true;

    // Release previous BLAS so it will be rebuilt on next TLAS update.
    entry.blas.Reset();
    entry.scratch.Reset();
}

void DX12RaytracingContext::BuildBLASIfNeeded(BLASEntry& entry, ID3D12GraphicsCommandList4* cmdList) {
    if (!m_device5 || !cmdList || !entry.hasGeometry) {
        return;
    }

    // Describe BLAS build inputs for a single triangle geometry.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = 1;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.pGeometryDescs = &entry.geomDesc;

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
    m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
    if (prebuild.ResultDataMaxSizeInBytes == 0) {
        return;
    }

    // Allocate or grow the BLAS result buffer.
    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC blasDesc{};
    blasDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    blasDesc.Alignment = 0;
    blasDesc.Width = prebuild.ResultDataMaxSizeInBytes;
    blasDesc.Height = 1;
    blasDesc.DepthOrArraySize = 1;
    blasDesc.MipLevels = 1;
    blasDesc.Format = DXGI_FORMAT_UNKNOWN;
    blasDesc.SampleDesc.Count = 1;
    blasDesc.SampleDesc.Quality = 0;
    blasDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    blasDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (!entry.blas || entry.blas->GetDesc().Width < blasDesc.Width) {
        entry.blas.Reset();
        HRESULT hr = m_device5->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &blasDesc,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            nullptr,
            IID_PPV_ARGS(&entry.blas));
        if (FAILED(hr)) {
            entry.blas.Reset();
            return;
        }
    }

    // Allocate or grow scratch buffer.
    D3D12_RESOURCE_DESC scratchDesc = blasDesc;
    scratchDesc.Width = prebuild.ScratchDataSizeInBytes;

    if (!entry.scratch || entry.scratch->GetDesc().Width < scratchDesc.Width) {
        entry.scratch.Reset();
        HRESULT hr = m_device5->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &scratchDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&entry.scratch));
        if (FAILED(hr)) {
            entry.scratch.Reset();
            entry.blas.Reset();
            return;
        }
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
    buildDesc.Inputs = inputs;
    buildDesc.DestAccelerationStructureData = entry.blas->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData = entry.scratch->GetGPUVirtualAddress();

    cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    // Ensure the BLAS is visible to subsequent GPU work.
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = entry.blas.Get();
    cmdList->ResourceBarrier(1, &barrier);
}

void DX12RaytracingContext::BuildTLAS(Scene::ECS_Registry* registry,
                                      ID3D12GraphicsCommandList4* cmdList) {
    if (!m_device5 || !registry || !cmdList) {
        return;
    }

    // Build instances from all visible renderables that have a registered BLAS.
    m_instanceDescs.clear();

    auto view = registry->View<Scene::TransformComponent, Scene::RenderableComponent>();
    uint32_t instanceIndex = 0;

    for (auto entity : view) {
        auto& transform = view.get<Scene::TransformComponent>(entity);
        auto& renderable = view.get<Scene::RenderableComponent>(entity);

        if (!renderable.visible || !renderable.mesh) {
            continue;
        }

        const Scene::MeshData* key = renderable.mesh.get();
        auto it = m_blasCache.find(key);
        if (it == m_blasCache.end() || !it->second.hasGeometry) {
            continue;
        }

        BLASEntry& blasEntry = it->second;
        BuildBLASIfNeeded(blasEntry, cmdList);
        if (!blasEntry.blas) {
            continue;
        }

        D3D12_RAYTRACING_INSTANCE_DESC inst{};
        inst.InstanceID = instanceIndex++;
        inst.InstanceMask = 0xFF;
        inst.InstanceContributionToHitGroupIndex = 0;
        inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        inst.AccelerationStructure = blasEntry.blas->GetGPUVirtualAddress();

        // DXR expects row-major 3x4 transform. Our worldMatrix is column-major;
        // transpose before extracting rows.
        const glm::mat4 world = transform.worldMatrix;
        const glm::mat4 rowMajor = glm::transpose(world);

        inst.Transform[0][0] = rowMajor[0][0];
        inst.Transform[0][1] = rowMajor[1][0];
        inst.Transform[0][2] = rowMajor[2][0];
        inst.Transform[0][3] = rowMajor[3][0];

        inst.Transform[1][0] = rowMajor[0][1];
        inst.Transform[1][1] = rowMajor[1][1];
        inst.Transform[1][2] = rowMajor[2][1];
        inst.Transform[1][3] = rowMajor[3][1];

        inst.Transform[2][0] = rowMajor[0][2];
        inst.Transform[2][1] = rowMajor[1][2];
        inst.Transform[2][2] = rowMajor[2][2];
        inst.Transform[2][3] = rowMajor[3][2];

        m_instanceDescs.push_back(inst);
    }

    if (m_instanceDescs.empty()) {
        return;
    }

    const UINT numInstances = static_cast<UINT>(m_instanceDescs.size());
    const UINT64 instanceBufferBytes =
        static_cast<UINT64>(numInstances) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);

    // Lazily allocate or grow the instance buffer (upload heap for simplicity).
    if (!m_instanceBuffer || instanceBufferBytes > m_instanceBufferSize) {
        m_instanceBuffer.Reset();
        m_instanceBufferSize = instanceBufferBytes;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC bufDesc{};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Alignment = 0;
        bufDesc.Width = instanceBufferBytes;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.SampleDesc.Quality = 0;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hr = m_device5->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_instanceBuffer));
        if (FAILED(hr)) {
            m_instanceBuffer.Reset();
            m_instanceBufferSize = 0;
            return;
        }
    }

    // Upload instance descriptors.
    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    if (FAILED(m_instanceBuffer->Map(0, &readRange, &mapped))) {
        return;
    }
    memcpy(mapped, m_instanceDescs.data(), static_cast<size_t>(instanceBufferBytes));
    m_instanceBuffer->Unmap(0, nullptr);

    // Describe TLAS build.
    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
    inputs.NumDescs = numInstances;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.InstanceDescs = m_instanceBuffer->GetGPUVirtualAddress();

    D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
    m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
    if (prebuild.ResultDataMaxSizeInBytes == 0) {
        return;
    }

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    D3D12_RESOURCE_DESC tlasDesc{};
    tlasDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    tlasDesc.Alignment = 0;
    tlasDesc.Width = prebuild.ResultDataMaxSizeInBytes;
    tlasDesc.Height = 1;
    tlasDesc.DepthOrArraySize = 1;
    tlasDesc.MipLevels = 1;
    tlasDesc.Format = DXGI_FORMAT_UNKNOWN;
    tlasDesc.SampleDesc.Count = 1;
    tlasDesc.SampleDesc.Quality = 0;
    tlasDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    tlasDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    if (!m_tlas || m_tlas->GetDesc().Width < tlasDesc.Width) {
        m_tlas.Reset();
        HRESULT hr = m_device5->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &tlasDesc,
            D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
            nullptr,
            IID_PPV_ARGS(&m_tlas));
        if (FAILED(hr)) {
            m_tlas.Reset();
            return;
        }
        m_tlasSize = tlasDesc.Width;
    }

    D3D12_RESOURCE_DESC scratchDesc = tlasDesc;
    scratchDesc.Width = prebuild.ScratchDataSizeInBytes;

    if (!m_tlasScratch || m_tlasScratch->GetDesc().Width < scratchDesc.Width) {
        m_tlasScratch.Reset();
        HRESULT hr = m_device5->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &scratchDesc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            nullptr,
            IID_PPV_ARGS(&m_tlasScratch));
        if (FAILED(hr)) {
            m_tlasScratch.Reset();
            m_tlas.Reset();
            m_tlasSize = 0;
            return;
        }
        m_tlasScratchSize = scratchDesc.Width;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
    buildDesc.Inputs = inputs;
    buildDesc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData = m_tlasScratch->GetGPUVirtualAddress();

    cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    // Ensure TLAS writes are visible to subsequent ray dispatches.
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = m_tlas.Get();
    cmdList->ResourceBarrier(1, &barrier);
}

void DX12RaytracingContext::DispatchRayTracing(
    ID3D12GraphicsCommandList4* cmdList,
    const DescriptorHandle& depthSrv,
    const DescriptorHandle& shadowMaskUav,
    D3D12_GPU_VIRTUAL_ADDRESS frameCBAddress) {
    if (!m_device5 || !cmdList || !m_tlas || !m_rtStateObject ||
        !m_rtStateProps || !m_rtShaderTable ||
        !m_rtGlobalRootSignature || !m_descriptors) {
        return;
    }

    // We require the renderer to have valid persistent descriptors for depth
    // and the RT shadow mask; without them we cannot bind the pipeline.
    if (!depthSrv.IsValid() || !shadowMaskUav.IsValid() ||
        !m_rtTlasSrv.IsValid() || !m_rtDepthSrv.IsValid() || !m_rtMaskUav.IsValid()) {
        return;
    }

    ID3D12Device* device = m_device5.Get();
    if (!device) {
        return;
    }

    // Create / update SRV for TLAS (raytracing AS SRV)
    {
        D3D12_SHADER_RESOURCE_VIEW_DESC asSrvDesc{};
        asSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
        asSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
        asSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        asSrvDesc.RaytracingAccelerationStructure.Location = m_tlas->GetGPUVirtualAddress();

        device->CreateShaderResourceView(
            nullptr,
            &asSrvDesc,
            m_rtTlasSrv.cpu);
    }

    // Depth SRV: copy from renderer's persistent depth SRV descriptor.
    device->CopyDescriptorsSimple(
        1,
        m_rtDepthSrv.cpu,
        depthSrv.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Shadow mask UAV: copy from renderer's UAV descriptor.
    device->CopyDescriptorsSimple(
        1,
        m_rtMaskUav.cpu,
        shadowMaskUav.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    // Bind descriptor heap (renderer is expected to have it set already; we
    // simply ensure it remains the active CBV/SRV/UAV heap).
    ID3D12DescriptorHeap* heaps[] = { m_descriptors->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetComputeRootSignature(m_rtGlobalRootSignature.Get());
    cmdList->SetPipelineState1(m_rtStateObject.Get());

    // Root param 0: frame constants
    cmdList->SetComputeRootConstantBufferView(0, frameCBAddress);
    // Root param 1: TLAS SRV table (t0, space2)
    cmdList->SetComputeRootDescriptorTable(1, m_rtTlasSrv.gpu);
    // Root param 2: depth SRV table (t1, space2)
    cmdList->SetComputeRootDescriptorTable(2, m_rtDepthSrv.gpu);
    // Root param 3: shadow mask UAV (u0, space2)
    cmdList->SetComputeRootDescriptorTable(3, m_rtMaskUav.gpu);

    const D3D12_GPU_VIRTUAL_ADDRESS shaderTableVA = m_rtShaderTable->GetGPUVirtualAddress();

    D3D12_DISPATCH_RAYS_DESC desc{};
    desc.RayGenerationShaderRecord.StartAddress = shaderTableVA + 0 * m_rtShaderTableStride;
    desc.RayGenerationShaderRecord.SizeInBytes = m_rtShaderTableStride;

    desc.MissShaderTable.StartAddress = shaderTableVA + 1 * m_rtShaderTableStride;
    desc.MissShaderTable.SizeInBytes = m_rtShaderTableStride;
    desc.MissShaderTable.StrideInBytes = m_rtShaderTableStride;

    desc.HitGroupTable.StartAddress = shaderTableVA + 2 * m_rtShaderTableStride;
    desc.HitGroupTable.SizeInBytes = m_rtShaderTableStride;
    desc.HitGroupTable.StrideInBytes = m_rtShaderTableStride;

    desc.Width = m_rtxWidth;
    desc.Height = m_rtxHeight;
    desc.Depth = 1;

    cmdList->DispatchRays(&desc);
}

} // namespace Cortex::Graphics
