#include "DX12Raytracing.h"

#include "DX12Device.h"
#include "DescriptorHeap.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"
#include "../ShaderTypes.h"
#include "../MeshBuffers.h"
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

    // Coarse safety limits for RT acceleration structure memory usage to avoid
    // exhausting VRAM on 8 GB GPUs in heavy scenes. These are deliberately
    // conservative: once the BLAS budget is reached, additional meshes fall
    // back to raster-only lighting for RT passes.
    constexpr uint64_t kMaxBLASBytesTotal = 2ull * 1024ull * 1024ull * 1024ull;      // ~2 GB
    constexpr uint64_t kMaxBLASBuildBytesPerFrame = 256ull * 1024ull * 1024ull;      // ~256 MB/frame

    // Mesh-level guardrails for RT participation. Extremely large meshes are
    // rendered normally but skipped for BLAS building to avoid huge single
    // allocations and long RT build times.
    constexpr uint64_t kMaxRTTrianglesPerMesh = 1'000'000ull;                        // ~1M tris
    constexpr uint64_t kMaxRTMeshBytes = 64ull * 1024ull * 1024ull;                  // ~64 MB per VB/IB
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

    // Build global root signature for the RT pipelines (shadows + reflections + GI).
    {
        D3D12_ROOT_PARAMETER rootParams[5] = {};

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

        // u0, space2: shadow mask / reflection / GI UAV
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

        // t0-t6, space1: shadow map + IBL + RT buffers (matches Basic.hlsl)
        D3D12_DESCRIPTOR_RANGE shadowEnvRange{};
        shadowEnvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        shadowEnvRange.NumDescriptors = 7;
        shadowEnvRange.BaseShaderRegister = 0;
        shadowEnvRange.RegisterSpace = 1;
        shadowEnvRange.OffsetInDescriptorsFromTableStart = 0;

        rootParams[4].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParams[4].DescriptorTable.NumDescriptorRanges = 1;
        rootParams[4].DescriptorTable.pDescriptorRanges = &shadowEnvRange;
        rootParams[4].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        // Static sampler (s0) for environment/IBL sampling.
        D3D12_STATIC_SAMPLER_DESC samplerDesc{};
        samplerDesc.Filter = D3D12_FILTER_ANISOTROPIC;
        samplerDesc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
        samplerDesc.MipLODBias = 0.0f;
        samplerDesc.MaxAnisotropy = 8;
        samplerDesc.ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
        samplerDesc.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
        samplerDesc.MinLOD = 0.0f;
        samplerDesc.MaxLOD = D3D12_FLOAT32_MAX;
        samplerDesc.ShaderRegister = 0;
        samplerDesc.RegisterSpace = 0;
        samplerDesc.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc{};
        rsDesc.NumParameters = _countof(rootParams);
        rsDesc.pParameters = rootParams;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers = &samplerDesc;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

        ComPtr<ID3DBlob> sigBlob;
        ComPtr<ID3DBlob> errorBlob;
        hr = D3D12SerializeRootSignature(
            &rsDesc,
            D3D_ROOT_SIGNATURE_VERSION_1,
            &sigBlob,
            &errorBlob);
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
    }

    // Create shader table with 1 raygen, 1 miss, 1 hit group record for shadows.
    {
        const UINT idSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
        m_rtShaderTableStride = AlignTo(idSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
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

        // Subobject 2: Shader config (payload + attributes)
        D3D12_RAYTRACING_SHADER_CONFIG shaderConfig{};
        shaderConfig.MaxPayloadSizeInBytes = 16; // float3 color + hit flag
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
        m_rtReflShaderTableStride = AlignTo(idSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
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
        m_rtGIShaderTableStride = AlignTo(idSize, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);
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

void DX12RaytracingContext::Shutdown() {
    if (m_device5) {
        spdlog::info("DX12RaytracingContext shutdown");
    }

    m_rtShaderTable.Reset();
    m_rtReflShaderTable.Reset();
    m_rtGIShaderTable.Reset();
    m_rtStateProps.Reset();
    m_rtReflStateProps.Reset();
    m_rtGIStateProps.Reset();
    m_rtStateObject.Reset();
    m_rtReflStateObject.Reset();
    m_rtGIStateObject.Reset();
    m_rtGlobalRootSignature.Reset();

    m_tlasScratch.Reset();
    m_tlas.Reset();
    m_instanceBuffer.Reset();
    m_blasCache.clear();
    m_totalBLASBytes = 0;
    m_totalTLASBytes = 0;

    m_device5.Reset();
    m_descriptors = nullptr;
    m_rtxWidth = 0;
    m_rtxHeight = 0;
}

void DX12RaytracingContext::OnResize(uint32_t width, uint32_t height) {
    if (m_rtxWidth == width && m_rtxHeight == height) {
        return;
    }
    m_rtxWidth = width;
    m_rtxHeight = height;
}

void DX12RaytracingContext::SetCameraParams(const glm::vec3& positionWS,
                                            const glm::vec3& forwardWS,
                                            float nearPlane,
                                            float farPlane) {
    m_cameraPosWS     = positionWS;
    float fwdLenSq    = glm::dot(forwardWS, forwardWS);
    m_cameraForwardWS = (fwdLenSq > 0.0f)
        ? glm::normalize(forwardWS)
        : glm::vec3(0.0f, 0.0f, 1.0f);
    m_cameraNearPlane = nearPlane;
    m_cameraFarPlane  = farPlane;
    m_hasCamera       = true;
}

void DX12RaytracingContext::RebuildBLASForMesh(const std::shared_ptr<Scene::MeshData>& mesh) {
    if (!m_device5 || !mesh) {
        return;
    }

    // We require GPU buffers to be populated by the renderer before we can
    // describe geometry for DXR. Without them we cannot build a BLAS.
    if (!mesh->gpuBuffers ||
        !mesh->gpuBuffers->vertexBuffer ||
        !mesh->gpuBuffers->indexBuffer ||
        mesh->positions.empty() ||
        mesh->indices.empty()) {
        return;
    }

    const size_t indexCount = mesh->indices.size();
    const size_t vertexCount = mesh->positions.size();
    const uint64_t triCount = static_cast<uint64_t>(indexCount / 3u);

    if (triCount == 0 || vertexCount == 0) {
        return;
    }

    // Guard against extremely large meshes that would require very large BLAS
    // buffers. These meshes will still render in the raster path but will not
    // participate in RT shadows/reflections/GI.
    const uint64_t approxVertexBytes = static_cast<uint64_t>(vertexCount) * sizeof(Vertex);
    const uint64_t approxIndexBytes  = static_cast<uint64_t>(indexCount) * sizeof(uint32_t);
    if (triCount > kMaxRTTrianglesPerMesh ||
        approxVertexBytes > kMaxRTMeshBytes ||
        approxIndexBytes > kMaxRTMeshBytes) {
        spdlog::warn(
            "DX12RaytracingContext: skipping BLAS for large mesh (verts={}, indices={}, tris≈{})",
            static_cast<uint64_t>(vertexCount),
            static_cast<uint64_t>(indexCount),
            triCount);
        return;
    }

    const Scene::MeshData* key = mesh.get();
    auto& entry = m_blasCache[key];

    D3D12_RAYTRACING_GEOMETRY_DESC geom{};
    geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
    geom.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

    D3D12_RAYTRACING_GEOMETRY_TRIANGLES_DESC& tri = geom.Triangles;
    tri.Transform3x4 = 0; // No per-geometry transform; instances use world matrices.
    tri.IndexFormat = DXGI_FORMAT_R32_UINT;
    tri.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
    tri.IndexCount = static_cast<UINT>(mesh->indices.size());
    tri.VertexCount = static_cast<UINT>(mesh->positions.size());
    tri.IndexBuffer = mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
    tri.VertexBuffer.StartAddress = mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
    tri.VertexBuffer.StrideInBytes = static_cast<UINT>(sizeof(Vertex));

    entry.geometryDesc = geom;
    entry.hasGeometry = (tri.VertexCount > 0 && tri.IndexCount > 0);
    entry.built = false;
}

void DX12RaytracingContext::BuildTLAS(Scene::ECS_Registry* registry,
                                      ID3D12GraphicsCommandList4* cmdList) {
    if (!m_device5 || !registry || !cmdList) {
        return;
    }

    m_instanceDescs.clear();

    // Track the amount of BLAS memory we build in this call so that very
    // heavy scenes can converge over several frames instead of spiking GPU
    // work and VRAM usage on the first RT frame.
    uint64_t bytesBuiltThisFrame = 0;

    auto view = registry->View<Scene::TransformComponent, Scene::RenderableComponent>();
    uint32_t instanceIndex = 0;

    for (auto entity : view) {
        auto& transform  = view.get<Scene::TransformComponent>(entity);
        auto& renderable = view.get<Scene::RenderableComponent>(entity);

        if (!renderable.visible || !renderable.mesh || !renderable.mesh->gpuBuffers) {
            continue;
        }

        const Scene::MeshData* key = renderable.mesh.get();
        auto it = m_blasCache.find(key);
        if (it == m_blasCache.end() || !it->second.hasGeometry) {
            continue;
        }

        BLASEntry& blasEntry = it->second;

        // Optional near/far culling using a simple bounding sphere based on
        // the mesh's object-space bounds and the world transform. This keeps
        // the TLAS smaller for large scenes without changing any visible
        // geometry.
        if (renderable.mesh->hasBounds && m_hasCamera) {
            const glm::vec3 centerWS =
                glm::vec3(transform.worldMatrix * glm::vec4(renderable.mesh->boundsCenter, 1.0f));
            const glm::vec3 absScale = glm::abs(transform.scale);
            const float maxScale = std::max(absScale.x, std::max(absScale.y, absScale.z));
            const float radiusWS = renderable.mesh->boundsRadius * maxScale;

            const glm::vec3 toCenter = centerWS - m_cameraPosWS;
            const float distAlongFwd = glm::dot(toCenter, m_cameraForwardWS);

            if (distAlongFwd + radiusWS < m_cameraNearPlane ||
                distAlongFwd - radiusWS > m_cameraFarPlane) {
                continue;
            }
        }

        if (!blasEntry.blas && blasEntry.hasGeometry) {
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
            inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            inputs.NumDescs = 1;
            inputs.pGeometryDescs = &blasEntry.geometryDesc;

            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
            m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
            if (prebuild.ResultDataMaxSizeInBytes == 0) {
                continue;
            }

            blasEntry.blasSize = prebuild.ResultDataMaxSizeInBytes;
            blasEntry.scratchSize = prebuild.ScratchDataSizeInBytes;

            const uint64_t candidateBytes =
                static_cast<uint64_t>(prebuild.ResultDataMaxSizeInBytes) +
                static_cast<uint64_t>(prebuild.ScratchDataSizeInBytes);
            if (candidateBytes == 0) {
                continue;
            }

            // Global memory budget for all BLAS buffers: once reached, meshes
            // beyond this limit are rendered raster-only for RT passes.
            if (m_totalBLASBytes + candidateBytes > kMaxBLASBytesTotal) {
                spdlog::warn(
                    "DX12RaytracingContext: BLAS memory budget reached; "
                    "skipping additional BLAS builds for remaining meshes");
                continue;
            }

            // Per-frame build budget: allow the scene to converge over
            // multiple frames instead of issuing all BLAS builds at once.
            if (bytesBuiltThisFrame + candidateBytes > kMaxBLASBuildBytesPerFrame) {
                // Defer building this mesh to a subsequent frame.
                continue;
            }

            D3D12_HEAP_PROPERTIES heapProps{};
            heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;
            heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
            heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
            heapProps.CreationNodeMask = 1;
            heapProps.VisibleNodeMask = 1;

            D3D12_RESOURCE_DESC asDesc{};
            asDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            asDesc.Alignment = 0;
            asDesc.Width = prebuild.ResultDataMaxSizeInBytes;
            asDesc.Height = 1;
            asDesc.DepthOrArraySize = 1;
            asDesc.MipLevels = 1;
            asDesc.Format = DXGI_FORMAT_UNKNOWN;
            asDesc.SampleDesc.Count = 1;
            asDesc.SampleDesc.Quality = 0;
            asDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            asDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

            HRESULT hr = m_device5->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &asDesc,
                D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
                nullptr,
                IID_PPV_ARGS(&blasEntry.blas));
            if (FAILED(hr)) {
                blasEntry.blas.Reset();
                continue;
            }

            D3D12_RESOURCE_DESC scratchDesc = asDesc;
            scratchDesc.Width = prebuild.ScratchDataSizeInBytes;

            hr = m_device5->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &scratchDesc,
                D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                nullptr,
                IID_PPV_ARGS(&blasEntry.scratch));
            if (FAILED(hr)) {
                blasEntry.scratch.Reset();
                blasEntry.blas.Reset();
                continue;
            }

            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
            buildDesc.Inputs = inputs;
            buildDesc.DestAccelerationStructureData = blasEntry.blas->GetGPUVirtualAddress();
            buildDesc.ScratchAccelerationStructureData = blasEntry.scratch->GetGPUVirtualAddress();

            cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

            D3D12_RESOURCE_BARRIER barrier{};
            barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
            barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
            barrier.UAV.pResource = blasEntry.blas.Get();
            cmdList->ResourceBarrier(1, &barrier);

            blasEntry.built = true;

            m_totalBLASBytes += candidateBytes;
            bytesBuiltThisFrame += candidateBytes;
        }

        if (!blasEntry.blas) {
            continue;
        }

        D3D12_RAYTRACING_INSTANCE_DESC inst{};
        inst.InstanceID = instanceIndex++;
        inst.InstanceMask = 0xFF;
        inst.InstanceContributionToHitGroupIndex = 0;
        inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        inst.AccelerationStructure = blasEntry.blas->GetGPUVirtualAddress();

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

    {
        D3D12_RANGE readRange{0, 0};
        void* mapped = nullptr;
        HRESULT hr = m_instanceBuffer->Map(0, &readRange, &mapped);
        if (FAILED(hr) || !mapped) {
            return;
        }

        std::memcpy(mapped, m_instanceDescs.data(), instanceBufferBytes);
        m_instanceBuffer->Unmap(0, nullptr);
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
    inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
    // TLAS favors build speed to keep the first RT frame responsive in heavy
    // scenes; BLAS still prefers fast trace for per-mesh performance.
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
    inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
    inputs.NumDescs = numInstances;
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
            m_tlasSize = 0;
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
            m_tlasScratchSize = 0;
            return;
        }
        m_tlasScratchSize = scratchDesc.Width;
    }

    D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
    buildDesc.Inputs = inputs;
    buildDesc.DestAccelerationStructureData = m_tlas->GetGPUVirtualAddress();
    buildDesc.ScratchAccelerationStructureData = m_tlasScratch->GetGPUVirtualAddress();

    cmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.UAV.pResource = m_tlas.Get();
    cmdList->ResourceBarrier(1, &barrier);

    // Update TLAS memory accounting based on the currently allocated TLAS +
    // scratch buffers so the renderer can fold this into VRAM estimates.
    if (m_tlas && m_tlasScratch) {
        const auto tlasDescCur = m_tlas->GetDesc();
        const auto scratchDescCur = m_tlasScratch->GetDesc();
        m_totalTLASBytes =
            static_cast<uint64_t>(tlasDescCur.Width) +
            static_cast<uint64_t>(scratchDescCur.Width);
    }
}

void DX12RaytracingContext::DispatchRayTracing(
    ID3D12GraphicsCommandList4* cmdList,
    const DescriptorHandle& depthSrv,
    const DescriptorHandle& shadowMaskUav,
    D3D12_GPU_VIRTUAL_ADDRESS frameCBAddress,
    const DescriptorHandle& shadowEnvTable) {
    if (!m_device5 || !cmdList || !m_tlas || !m_rtStateObject ||
        !m_rtStateProps || !m_rtShaderTable ||
        !m_rtGlobalRootSignature || !m_descriptors) {
        return;
    }

    if (!depthSrv.IsValid() || !shadowMaskUav.IsValid() ||
        !m_rtTlasSrv.IsValid() || !m_rtDepthSrv.IsValid() || !m_rtMaskUav.IsValid()) {
        return;
    }

    ID3D12Device* device = m_device5.Get();
    if (!device) {
        return;
    }

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

    device->CopyDescriptorsSimple(
        1,
        m_rtDepthSrv.cpu,
        depthSrv.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    device->CopyDescriptorsSimple(
        1,
        m_rtMaskUav.cpu,
        shadowMaskUav.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12DescriptorHeap* heaps[] = { m_descriptors->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetComputeRootSignature(m_rtGlobalRootSignature.Get());
    cmdList->SetPipelineState1(m_rtStateObject.Get());

    cmdList->SetComputeRootConstantBufferView(0, frameCBAddress);
    cmdList->SetComputeRootDescriptorTable(1, m_rtTlasSrv.gpu);
    cmdList->SetComputeRootDescriptorTable(2, m_rtDepthSrv.gpu);
    cmdList->SetComputeRootDescriptorTable(3, m_rtMaskUav.gpu);
    if (shadowEnvTable.IsValid()) {
        cmdList->SetComputeRootDescriptorTable(4, shadowEnvTable.gpu);
    }

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

void DX12RaytracingContext::DispatchReflections(
    ID3D12GraphicsCommandList4* cmdList,
    const DescriptorHandle& depthSrv,
    const DescriptorHandle& reflectionUav,
    D3D12_GPU_VIRTUAL_ADDRESS frameCBAddress,
    const DescriptorHandle& shadowEnvTable) {
    if (!m_device5 || !cmdList || !m_tlas || !m_rtReflStateObject ||
        !m_rtReflStateProps || !m_rtReflShaderTable ||
        !m_rtGlobalRootSignature || !m_descriptors) {
        return;
    }

    if (!depthSrv.IsValid() || !reflectionUav.IsValid() ||
        !m_rtTlasSrv.IsValid() || !m_rtDepthSrv.IsValid() || !m_rtMaskUav.IsValid()) {
        return;
    }

    ID3D12Device* device = m_device5.Get();
    if (!device) {
        return;
    }

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

    device->CopyDescriptorsSimple(
        1,
        m_rtDepthSrv.cpu,
        depthSrv.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    device->CopyDescriptorsSimple(
        1,
        m_rtMaskUav.cpu,
        reflectionUav.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12DescriptorHeap* heaps[] = { m_descriptors->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetComputeRootSignature(m_rtGlobalRootSignature.Get());
    cmdList->SetPipelineState1(m_rtReflStateObject.Get());

    cmdList->SetComputeRootConstantBufferView(0, frameCBAddress);
    cmdList->SetComputeRootDescriptorTable(1, m_rtTlasSrv.gpu);
    cmdList->SetComputeRootDescriptorTable(2, m_rtDepthSrv.gpu);
    cmdList->SetComputeRootDescriptorTable(3, m_rtMaskUav.gpu);
    if (shadowEnvTable.IsValid()) {
        cmdList->SetComputeRootDescriptorTable(4, shadowEnvTable.gpu);
    }

    const D3D12_GPU_VIRTUAL_ADDRESS shaderTableVA = m_rtReflShaderTable->GetGPUVirtualAddress();

    D3D12_DISPATCH_RAYS_DESC desc{};
    desc.RayGenerationShaderRecord.StartAddress = shaderTableVA + 0 * m_rtReflShaderTableStride;
    desc.RayGenerationShaderRecord.SizeInBytes = m_rtReflShaderTableStride;

    desc.MissShaderTable.StartAddress = shaderTableVA + 1 * m_rtReflShaderTableStride;
    desc.MissShaderTable.SizeInBytes = m_rtReflShaderTableStride;
    desc.MissShaderTable.StrideInBytes = m_rtReflShaderTableStride;

    desc.HitGroupTable.StartAddress = shaderTableVA + 2 * m_rtReflShaderTableStride;
    desc.HitGroupTable.SizeInBytes = m_rtReflShaderTableStride;
    desc.HitGroupTable.StrideInBytes = m_rtReflShaderTableStride;

    desc.Width = m_rtxWidth;
    desc.Height = m_rtxHeight;
    desc.Depth = 1;

    cmdList->DispatchRays(&desc);
}

void DX12RaytracingContext::DispatchGI(
    ID3D12GraphicsCommandList4* cmdList,
    const DescriptorHandle& depthSrv,
    const DescriptorHandle& giUav,
    D3D12_GPU_VIRTUAL_ADDRESS frameCBAddress,
    const DescriptorHandle& shadowEnvTable) {
    if (!m_device5 || !cmdList || !m_tlas || !m_rtGIStateObject ||
        !m_rtGIStateProps || !m_rtGIShaderTable ||
        !m_rtGlobalRootSignature || !m_descriptors) {
        return;
    }

    if (!depthSrv.IsValid() || !giUav.IsValid() ||
        !m_rtTlasSrv.IsValid() || !m_rtDepthSrv.IsValid() || !m_rtMaskUav.IsValid()) {
        return;
    }

    ID3D12Device* device = m_device5.Get();
    if (!device) {
        return;
    }

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

    device->CopyDescriptorsSimple(
        1,
        m_rtDepthSrv.cpu,
        depthSrv.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    device->CopyDescriptorsSimple(
        1,
        m_rtMaskUav.cpu,
        giUav.cpu,
        D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

    ID3D12DescriptorHeap* heaps[] = { m_descriptors->GetCBV_SRV_UAV_Heap() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetComputeRootSignature(m_rtGlobalRootSignature.Get());
    cmdList->SetPipelineState1(m_rtGIStateObject.Get());

    cmdList->SetComputeRootConstantBufferView(0, frameCBAddress);
    cmdList->SetComputeRootDescriptorTable(1, m_rtTlasSrv.gpu);
    cmdList->SetComputeRootDescriptorTable(2, m_rtDepthSrv.gpu);
    cmdList->SetComputeRootDescriptorTable(3, m_rtMaskUav.gpu);
    if (shadowEnvTable.IsValid()) {
        cmdList->SetComputeRootDescriptorTable(4, shadowEnvTable.gpu);
    }

    const D3D12_GPU_VIRTUAL_ADDRESS shaderTableVA = m_rtGIShaderTable->GetGPUVirtualAddress();

    D3D12_DISPATCH_RAYS_DESC desc{};
    desc.RayGenerationShaderRecord.StartAddress = shaderTableVA + 0 * m_rtGIShaderTableStride;
    desc.RayGenerationShaderRecord.SizeInBytes = m_rtGIShaderTableStride;

    desc.MissShaderTable.StartAddress = shaderTableVA + 1 * m_rtGIShaderTableStride;
    desc.MissShaderTable.SizeInBytes = m_rtGIShaderTableStride;
    desc.MissShaderTable.StrideInBytes = m_rtGIShaderTableStride;

    desc.HitGroupTable.StartAddress = shaderTableVA + 2 * m_rtGIShaderTableStride;
    desc.HitGroupTable.SizeInBytes = m_rtGIShaderTableStride;
    desc.HitGroupTable.StrideInBytes = m_rtGIShaderTableStride;

    desc.Width = m_rtxWidth;
    desc.Height = m_rtxHeight;
    desc.Depth = 1;

    cmdList->DispatchRays(&desc);
}

} // namespace Cortex::Graphics
