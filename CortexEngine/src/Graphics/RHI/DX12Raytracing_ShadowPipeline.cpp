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

Result<void> DX12RaytracingContext::InitializeShadowPipeline() {
    HRESULT hr = S_OK;

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
    }

    // Create shader table with 1 raygen, 1 miss, 1 hit group record for shadows.
    {
        const UINT idSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        // Use D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT (64) instead of
        // D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT (32) to ensure that
        // the MissShaderTable.StartAddress and HitGroupTable.StartAddress are
        // also 64-byte aligned, not just the stride.
        m_rtShaderTableStride = AlignTo(idSize, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);
        const UINT tableSize = m_rtShaderTableStride * 3;

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
            IID_PPV_ARGS(&m_rtShaderTable));
        if (FAILED(hr)) {
            m_rtShaderTable.Reset();
            spdlog::warn("DXR shader table creation failed (hr=0x{:08X}); RT shadows disabled",
                         static_cast<unsigned int>(hr));
            m_rtStateProps.Reset();
            m_rtStateObject.Reset();
            m_rtGlobalRootSignature.Reset();
            spdlog::info("DX12RaytracingContext initialized (DXR device detected; BLAS/TLAS only, no RT pipeline)");
            return Result<void>::Ok();
        }

        uint8_t* mapped = nullptr;
        D3D12_RANGE readRange{0, 0};
        hr = m_rtShaderTable->Map(0, &readRange, reinterpret_cast<void**>(&mapped));
        if (FAILED(hr) || !mapped) {
            m_rtShaderTable.Reset();
            m_rtStateProps.Reset();
            m_rtStateObject.Reset();
            m_rtGlobalRootSignature.Reset();
            spdlog::warn("DXR shader table map failed (hr=0x{:08X}); RT shadows disabled",
                         static_cast<unsigned int>(hr));
            spdlog::info("DX12RaytracingContext initialized (DXR device detected; BLAS/TLAS only, no RT pipeline)");
            return Result<void>::Ok();
        }

        const void* raygenId = m_rtStateProps->GetShaderIdentifier(L"RayGen_Shadow");
        const void* missId = m_rtStateProps->GetShaderIdentifier(L"Miss_Shadow");
        const void* hitId = m_rtStateProps->GetShaderIdentifier(L"ShadowHitGroup");

        if (!raygenId || !missId || !hitId) {
            m_rtShaderTable->Unmap(0, nullptr);
            m_rtShaderTable.Reset();
            m_rtStateProps.Reset();
            m_rtStateObject.Reset();
            m_rtGlobalRootSignature.Reset();
            spdlog::warn("DXR shader identifiers missing; RT shadows disabled");
            spdlog::info("DX12RaytracingContext initialized (DXR device detected; BLAS/TLAS only, no RT pipeline)");
            return Result<void>::Ok();
        }

        // Record 0: raygen
        std::memcpy(mapped + 0 * m_rtShaderTableStride, raygenId, idSize);
        // Record 1: miss
        std::memcpy(mapped + 1 * m_rtShaderTableStride, missId, idSize);
        // Record 2: hit group
        std::memcpy(mapped + 2 * m_rtShaderTableStride, hitId, idSize);

        m_rtShaderTable->Unmap(0, nullptr);
    }

    spdlog::info("DX12RaytracingContext initialized (DXR device + RT shadow pipeline ready)");


    return Result<void>::Ok();
}

} // namespace Cortex::Graphics