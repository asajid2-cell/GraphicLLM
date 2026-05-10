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

Result<void> DX12RaytracingContext::InitializeGIPipeline() {
    HRESULT hr = S_OK;

    // Load precompiled DXR library for diffuse GI. This path is optional; if
    // it fails, RT shadows/reflections remain available and the GI pipeline
    // is simply disabled.
    std::filesystem::path giRequestedPath("assets/shaders/RaytracedGI.dxil");
    std::filesystem::path giResolvedPath = giRequestedPath;
    if (!Utils::FileExists(giResolvedPath)) {
        std::filesystem::path cwd = std::filesystem::current_path();
        std::filesystem::path candidate = cwd / giRequestedPath;
        if (Utils::FileExists(candidate)) {
            giResolvedPath = candidate;
        } else {
            candidate = cwd.parent_path() / giRequestedPath;
            if (Utils::FileExists(candidate)) {
                giResolvedPath = candidate;
            } else {
                candidate = cwd.parent_path().parent_path() / giRequestedPath;
                if (Utils::FileExists(candidate)) {
                    giResolvedPath = candidate;
                }
            }
        }
    }

    auto giLibResult = Utils::ReadBinaryFile(giResolvedPath);
    if (giLibResult.IsErr()) {
        spdlog::warn("DX12RaytracingContext: failed to load RaytracedGI.dxil: {}", giLibResult.Error());
        spdlog::info("DX12RaytracingContext: RT GI disabled (shadows/reflections remain available)");
        return Result<void>::Ok();
    }
    const std::vector<uint8_t>& giLibBytes = giLibResult.Value();

    // Build DXR state object (pipeline) for diffuse GI.
    {
        D3D12_DXIL_LIBRARY_DESC libDesc{};
        D3D12_SHADER_BYTECODE libBytecode{};
        libBytecode.pShaderBytecode = giLibBytes.data();
        libBytecode.BytecodeLength = giLibBytes.size();
        libDesc.DXILLibrary = libBytecode;

        D3D12_EXPORT_DESC exports[3] = {};
        exports[0].Name = L"RayGen_GI";
        exports[0].ExportToRename = nullptr;
        exports[0].Flags = D3D12_EXPORT_FLAG_NONE;

        exports[1].Name = L"Miss_GI";
        exports[1].ExportToRename = nullptr;
        exports[1].Flags = D3D12_EXPORT_FLAG_NONE;

        exports[2].Name = L"ClosestHit_GI";
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
        hitGroup.HitGroupExport = L"GIHitGroup";
        hitGroup.Type = D3D12_HIT_GROUP_TYPE_TRIANGLES;
        hitGroup.ClosestHitShaderImport = L"ClosestHit_GI";
        hitGroup.AnyHitShaderImport = nullptr;
        hitGroup.IntersectionShaderImport = nullptr;

        subobjects[1].Type = D3D12_STATE_SUBOBJECT_TYPE_HIT_GROUP;
        subobjects[1].pDesc = &hitGroup;

        // Subobject 2: Shader config (payload + attributes)
        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
        shaderConfig.MaxPayloadSizeInBytes = 4; // single float occlusion factor
        shaderConfig.MaxAttributeSizeInBytes = 8; // barycentrics

        subobjects[2].Type = D3D12_STATE_SUBOBJECT_TYPE_RAYTRACING_SHADER_CONFIG;
        subobjects[2].pDesc = &shaderConfig;

        // Subobject 3: Global root signature (shared with other RT pipelines)
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

        hr = m_device5->CreateStateObject(&soDesc, IID_PPV_ARGS(&m_rtGIStateObject));
        if (FAILED(hr) || !m_rtGIStateObject) {
            spdlog::warn("DXR GI state object creation failed (hr=0x{:08X}); RT GI disabled",
                         static_cast<unsigned int>(hr));
            m_rtGIStateObject.Reset();
            m_rtGIStateProps.Reset();
            m_rtGIShaderTable.Reset();
            return Result<void>::Ok();
        }

        hr = m_rtGIStateObject.As(&m_rtGIStateProps);
        if (FAILED(hr) || !m_rtGIStateProps) {
            spdlog::warn("DXR GI state object properties query failed (hr=0x{:08X}); RT GI disabled",
                         static_cast<unsigned int>(hr));
            m_rtGIStateProps.Reset();
            m_rtGIStateObject.Reset();
            m_rtGIShaderTable.Reset();
            return Result<void>::Ok();
        }
    }

    // Create shader table with 1 raygen, 1 miss, 1 hit group record for GI.
    {
        const UINT idSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        // Use 64-byte alignment for shader table start addresses
        m_rtGIShaderTableStride = AlignTo(idSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        const UINT tableSize = m_rtGIShaderTableStride * 3;

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
            IID_PPV_ARGS(&m_rtGIShaderTable));
        if (FAILED(hr)) {
            m_rtGIShaderTable.Reset();
            m_rtGIStateProps.Reset();
            m_rtGIStateObject.Reset();
            spdlog::warn("DXR GI shader table creation failed (hr=0x{:08X}); RT GI disabled",
                         static_cast<unsigned int>(hr));
            return Result<void>::Ok();
        }

        uint8_t* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        hr = m_rtGIShaderTable->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr) || !mapped) {
            m_rtGIShaderTable.Reset();
            m_rtGIStateProps.Reset();
            m_rtGIStateObject.Reset();
            spdlog::warn("DXR GI shader table map failed (hr=0x{:08X}); RT GI disabled",
                         static_cast<unsigned int>(hr));
            return Result<void>::Ok();
        }

        const void* raygenId = m_rtGIStateProps->GetShaderIdentifier(L"RayGen_GI");
        const void* missId   = m_rtGIStateProps->GetShaderIdentifier(L"Miss_GI");
        const void* hitId    = m_rtGIStateProps->GetShaderIdentifier(L"GIHitGroup");

        if (!raygenId || !missId || !hitId) {
            m_rtGIShaderTable->Unmap(0, nullptr);
            m_rtGIShaderTable.Reset();
            m_rtGIStateProps.Reset();
            m_rtGIStateObject.Reset();
            spdlog::warn("DXR GI shader identifiers missing; RT GI disabled");
            return Result<void>::Ok();
        }

        // Record 0: raygen
        std::memcpy(mapped + 0 * m_rtGIShaderTableStride, raygenId, idSize);
        // Record 1: miss
        std::memcpy(mapped + 1 * m_rtGIShaderTableStride, missId, idSize);
        // Record 2: hit group
        std::memcpy(mapped + 2 * m_rtGIShaderTableStride, hitId, idSize);

        m_rtGIShaderTable->Unmap(0, nullptr);
    }

    spdlog::info("DX12RaytracingContext GI pipeline initialized (optional RT GI ready)");
    return Result<void>::Ok();

}

} // namespace Cortex::Graphics