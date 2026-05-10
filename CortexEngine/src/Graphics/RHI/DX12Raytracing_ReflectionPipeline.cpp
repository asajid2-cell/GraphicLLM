#include "DX12Raytracing.h"

#include "Utils/FileUtils.h"

#include <cstring>
#include <filesystem>
#include <vector>

namespace Cortex::Graphics {

namespace {

constexpr UINT AlignTo(UINT value, UINT alignment) {
    return (value + alignment - 1u) & ~(alignment - 1u);
}

} // namespace

Result<void> DX12RaytracingContext::InitializeReflectionPipeline() {
    HRESULT hr = S_OK;

    // Load precompiled DXR library for reflections. This path is optional; if
    // it fails, RT shadows remain available and the reflection pipeline is
    // simply disabled.
    std::filesystem::path reflRequestedPath("assets/shaders/RaytracedReflections.dxil");
    std::filesystem::path reflResolvedPath = reflRequestedPath;
    if (!Utils::FileExists(reflResolvedPath)) {
        std::filesystem::path cwd = std::filesystem::current_path();
        std::filesystem::path candidate = cwd / reflRequestedPath;
        if (Utils::FileExists(candidate)) {
            reflResolvedPath = candidate;
        } else {
            candidate = cwd.parent_path() / reflRequestedPath;
            if (Utils::FileExists(candidate)) {
                reflResolvedPath = candidate;
            } else {
                candidate = cwd.parent_path().parent_path() / reflRequestedPath;
                if (Utils::FileExists(candidate)) {
                    reflResolvedPath = candidate;
                }
            }
        }
    }

    auto reflLibResult = Utils::ReadBinaryFile(reflResolvedPath);
    if (reflLibResult.IsErr()) {
        spdlog::warn("DX12RaytracingContext: failed to load RaytracedReflections.dxil: {}", reflLibResult.Error());
        spdlog::info("DX12RaytracingContext: RT reflections disabled (shadows pipeline remains available)");
        return Result<void>::Ok();
    }
    const std::vector<uint8_t>& reflLibBytes = reflLibResult.Value();

    // Build DXR state object (pipeline) for reflections.
    {
        D3D12_DXIL_LIBRARY_DESC libDesc{};
        D3D12_SHADER_BYTECODE libBytecode{};
        libBytecode.pShaderBytecode = reflLibBytes.data();
        libBytecode.BytecodeLength = reflLibBytes.size();
        libDesc.DXILLibrary = libBytecode;

        D3D12_EXPORT_DESC exports[3] = {};
        exports[0].Name = L"RayGen_Reflection";
        exports[0].ExportToRename = nullptr;
        exports[0].Flags = D3D12_EXPORT_FLAG_NONE;

        exports[1].Name = L"Miss_Reflection";
        exports[1].ExportToRename = nullptr;
        exports[1].Flags = D3D12_EXPORT_FLAG_NONE;

        exports[2].Name = L"ClosestHit_Reflection";
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
        hitGroup.HitGroupExport = L"ReflectionHitGroup";
        hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hitGroup.ClosestHitShaderImport = L"ClosestHit_Reflection";
        hitGroup.AnyHitShaderImport = nullptr;
        hitGroup.IntersectionShaderImport = nullptr;

        subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        subobjects[1].pDesc = &hitGroup;

        // Subobject 2: Shader config (payload + attributes). The reflection
        // payload carries color, normal, compact material fields, distance,
        // transmission/clearcoat fields, and hit state. Keep this comfortably
        // above the HLSL struct size so regenerated DXIL state objects remain
        // valid when the reflection shader evolves.
        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
        shaderConfig.MaxPayloadSizeInBytes = 96;
        shaderConfig.MaxAttributeSizeInBytes = 8; // barycentrics

        subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        subobjects[2].pDesc = &shaderConfig;

        // Subobject 3: Global root signature (shared with shadow pipeline)
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

        hr = m_device5->CreateStateObject(&soDesc, IID_PPV_ARGS(&m_rtReflStateObject));
        if (FAILED(hr) || !m_rtReflStateObject) {
            spdlog::warn("DXR reflections state object creation failed (hr=0x{:08X}); RT reflections disabled",
                         static_cast<unsigned int>(hr));
            m_rtReflStateObject.Reset();
            m_rtReflStateProps.Reset();
            m_rtReflShaderTable.Reset();
            spdlog::info("DX12RaytracingContext: RT shadows remain available without reflections.");
            return Result<void>::Ok();
        }

        hr = m_rtReflStateObject.As(&m_rtReflStateProps);
        if (FAILED(hr) || !m_rtReflStateProps) {
            spdlog::warn("DXR reflections state object properties query failed (hr=0x{:08X}); RT reflections disabled",
                         static_cast<unsigned int>(hr));
            m_rtReflStateProps.Reset();
            m_rtReflStateObject.Reset();
            m_rtReflShaderTable.Reset();
            spdlog::info("DX12RaytracingContext: RT shadows remain available without reflections.");
            return Result<void>::Ok();
        }
    }

    // Create shader table with 1 raygen, 1 miss, 1 hit group record for reflections.
    {
        const UINT idSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        // Use 64-byte alignment for shader table start addresses
        m_rtReflShaderTableStride = AlignTo(idSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        const UINT tableSize = m_rtReflShaderTableStride * 3;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC bufDesc{};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Alignment = 0;
        bufDesc.Width = tableSize;
        bufDesc.Height = 1;
        bufDesc.DepthOrArraySize = 1;
        bufDesc.MipLevels = 1;
        bufDesc.Format = DXGI_FORMAT_UNKNOWN;
        bufDesc.SampleDesc.Count = 1;
        bufDesc.SampleDesc.Quality = 0;
        bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        bufDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        hr = m_device5->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &bufDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_rtReflShaderTable));
        if (FAILED(hr)) {
            m_rtReflShaderTable.Reset();
            m_rtReflStateProps.Reset();
            m_rtReflStateObject.Reset();
            spdlog::warn("DXR reflections shader table creation failed (hr=0x{:08X}); RT reflections disabled",
                         static_cast<unsigned int>(hr));
            spdlog::info("DX12RaytracingContext: RT shadows remain available without reflections.");
            return Result<void>::Ok();
        }

        uint8_t* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        hr = m_rtReflShaderTable->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr) || !mapped) {
            m_rtReflShaderTable.Reset();
            m_rtReflStateProps.Reset();
            m_rtReflStateObject.Reset();
            spdlog::warn("DXR reflections shader table map failed (hr=0x{:08X}); RT reflections disabled",
                         static_cast<unsigned int>(hr));
            spdlog::info("DX12RaytracingContext: RT shadows remain available without reflections.");
            return Result<void>::Ok();
        }

        const void* raygenId = m_rtReflStateProps->GetShaderIdentifier(L"RayGen_Reflection");
        const void* missId   = m_rtReflStateProps->GetShaderIdentifier(L"Miss_Reflection");
        const void* hitId    = m_rtReflStateProps->GetShaderIdentifier(L"ReflectionHitGroup");

        if (!raygenId || !missId || !hitId) {
            m_rtReflShaderTable->Unmap(0, nullptr);
            m_rtReflShaderTable.Reset();
            m_rtReflStateProps.Reset();
            m_rtReflStateObject.Reset();
            spdlog::warn("DXR reflections shader identifiers missing; RT reflections disabled");
            spdlog::info("DX12RaytracingContext: RT shadows remain available without reflections.");
            return Result<void>::Ok();
        }

        // Record 0: raygen
        std::memcpy(mapped + 0 * m_rtReflShaderTableStride, raygenId, idSize);
        // Record 1: miss
        std::memcpy(mapped + 1 * m_rtReflShaderTableStride, missId, idSize);
        // Record 2: hit group
        std::memcpy(mapped + 2 * m_rtReflShaderTableStride, hitId, idSize);

        m_rtReflShaderTable->Unmap(0, nullptr);
    }

    spdlog::info("DX12RaytracingContext reflections pipeline initialized (optional RT reflections ready)");


    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
