#include "DX12Raytracing.h"

#include "DX12Device.h"
#include "../MaterialModel.h"
#include "../MeshBuffers.h"
#include "../ShaderTypes.h"
#include "../SurfaceClassification.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void DX12RaytracingContext::BuildTLAS(Scene::ECS_Registry* registry,
                                      ID3D12GraphicsCommandList4* cmdList) {
    if (!registry) {
        m_lastTLASStats = {};
        return;
    }

    std::vector<TLASBuildInput> inputs;
    auto view = registry->View<Scene::TransformComponent, Scene::RenderableComponent>();
    for (auto entity : view) {
        const auto& transform = view.get<Scene::TransformComponent>(entity);
        const auto& renderable = view.get<Scene::RenderableComponent>(entity);

        TLASBuildInput input{};
        input.stableId = static_cast<uint32_t>(entity);
        input.renderable = &renderable;
        input.worldMatrix = transform.worldMatrix;
        const glm::vec3 absScale = glm::abs(transform.scale);
        input.maxWorldScale = std::max(absScale.x, std::max(absScale.y, absScale.z));
        inputs.push_back(input);
    }

    BuildTLAS(inputs, cmdList);
}

void DX12RaytracingContext::BuildTLAS(const std::vector<TLASBuildInput>& tlasInputs,
                                      ID3D12GraphicsCommandList4* cmdList) {
    m_lastTLASStats = {};

    if (!m_device5 || !cmdList) {
        return;
    }

    m_instanceDescs.clear();
    m_rtMaterials.clear();

    // Track the amount of BLAS memory we build in this call so that very
    // heavy scenes can converge over several frames instead of spiking GPU
    // work and VRAM usage on the first RT frame.
    uint64_t bytesBuiltThisFrame = 0;

    uint32_t instanceIndex = 0;

    for (const TLASBuildInput& input : tlasInputs) {
        ++m_lastTLASStats.candidates;

        if (!input.renderable) {
            ++m_lastTLASStats.skippedInvisibleOrInvalid;
            continue;
        }
        const Scene::RenderableComponent& renderable = *input.renderable;
        if (!renderable.visible || !renderable.mesh || !renderable.mesh->gpuBuffers) {
            ++m_lastTLASStats.skippedInvisibleOrInvalid;
            continue;
        }

        const Scene::MeshData* key = renderable.mesh.get();
        auto it = m_blasCache.find(key);
        if (it == m_blasCache.end() || !it->second.hasGeometry) {
            RebuildBLASForMesh(renderable.mesh);
            it = m_blasCache.find(key);
            if (it == m_blasCache.end() || !it->second.hasGeometry) {
                ++m_lastTLASStats.missingGeometry;
                continue;
            }
        }

        BLASEntry& blasEntry = it->second;

        // RT visibility is broader than raster visibility. Reflections,
        // shadows, and diffuse GI can depend on off-screen contributors, so
        // only reject geometry that is far outside the camera's trace range.
        if (renderable.mesh->hasBounds && m_hasCamera) {
            const glm::vec3 centerWS =
                glm::vec3(input.worldMatrix * glm::vec4(renderable.mesh->boundsCenter, 1.0f));
            const float radiusWS = renderable.mesh->boundsRadius * std::max(input.maxWorldScale, 0.0001f);

            const glm::vec3 toCenter = centerWS - m_cameraPosWS;
            const float distanceToCamera = glm::length(toCenter);
            const float rtCullDistance = std::max(m_cameraFarPlane, 128.0f);

            if (distanceToCamera - radiusWS > rtCullDistance) {
                ++m_lastTLASStats.distanceCulled;
                continue;
            }
        }

        if (!blasEntry.blas && blasEntry.hasGeometry && !blasEntry.buildRequested) {
            blasEntry.buildRequested = true;
            ++m_lastTLASStats.blasBuildRequested;
        }

        if (!blasEntry.blas && blasEntry.hasGeometry && blasEntry.buildRequested) {
            D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs{};
            inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
            inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
            inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
            inputs.NumDescs = 1;
            inputs.pGeometryDescs = &blasEntry.geometryDesc;

            D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild{};
            m_device5->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild);
            if (prebuild.ResultDataMaxSizeInBytes == 0) {
                ++m_lastTLASStats.blasBuildFailed;
                continue;
            }

            blasEntry.blasSize = prebuild.ResultDataMaxSizeInBytes;
            blasEntry.scratchSize = prebuild.ScratchDataSizeInBytes;

            const uint64_t candidateBytes =
                static_cast<uint64_t>(prebuild.ResultDataMaxSizeInBytes) +
                static_cast<uint64_t>(prebuild.ScratchDataSizeInBytes);
            if (candidateBytes == 0) {
                ++m_lastTLASStats.blasBuildFailed;
                continue;
            }

            if (m_totalBLASBytes + candidateBytes > m_maxBLASBytesTotal) {
                ++m_lastTLASStats.blasTotalBudgetSkipped;
                spdlog::warn(
                    "DX12RaytracingContext: BLAS memory budget reached; "
                    "skipping additional BLAS builds for remaining meshes");
                continue;
            }

            if (bytesBuiltThisFrame + candidateBytes > m_maxBLASBuildBytesPerFrame) {
                ++m_lastTLASStats.blasBuildBudgetDeferred;
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
                ++m_lastTLASStats.blasBuildFailed;
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
                ++m_lastTLASStats.blasBuildFailed;
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
            blasEntry.buildRequested = false;
            blasEntry.buildFrameIndex = m_currentFrameIndex;

            m_totalBLASBytes += candidateBytes;
            bytesBuiltThisFrame += candidateBytes;

            // NOTE: Do NOT release the scratch buffer here! The GPU hasn't
            // executed BuildRaytracingAccelerationStructure yet - the command
            // is only recorded in the command list. Releasing the scratch
            // buffer now causes #921 OBJECT_DELETED_WHILE_STILL_IN_USE.
            // ReleaseScratchBuffers() will be called in BeginFrame with a
            // completedFrameIndex that ensures the GPU has finished using
            // this scratch buffer before it's released.
        }

        if (!blasEntry.blas) {
            continue;
        }

        D3D12_RAYTRACING_INSTANCE_DESC inst{};
        inst.InstanceID = instanceIndex;
        inst.InstanceMask = 0xFF;
        inst.InstanceContributionToHitGroupIndex = 0;
        inst.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_NONE;
        inst.AccelerationStructure = blasEntry.blas->GetGPUVirtualAddress();

        const glm::mat4 world = input.worldMatrix;
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

        const MaterialModel materialModel =
            MaterialResolver::ResolveRenderable(renderable, MaterialTextureFallbacks{});
        const SurfaceClass surfaceClass = ClassifySurface(materialModel);
        const RTMaterialGPU material =
            BuildRTMaterialGPU(materialModel, ToSurfaceClassId(surfaceClass));
        switch (surfaceClass) {
        case SurfaceClass::Glass:
            ++m_lastTLASStats.surfaceGlass;
            break;
        case SurfaceClass::Mirror:
            ++m_lastTLASStats.surfaceMirror;
            break;
        case SurfaceClass::Plastic:
            ++m_lastTLASStats.surfacePlastic;
            break;
        case SurfaceClass::Masonry:
            ++m_lastTLASStats.surfaceMasonry;
            break;
        case SurfaceClass::Emissive:
            ++m_lastTLASStats.surfaceEmissive;
            break;
        case SurfaceClass::BrushedMetal:
            ++m_lastTLASStats.surfaceBrushedMetal;
            break;
        case SurfaceClass::Wood:
            ++m_lastTLASStats.surfaceWood;
            break;
        case SurfaceClass::Water:
            ++m_lastTLASStats.surfaceWater;
            break;
        case SurfaceClass::Default:
        default:
            ++m_lastTLASStats.surfaceDefault;
            break;
        }
        m_rtMaterials.push_back(material);
        ++instanceIndex;
    }

    m_lastTLASStats.emittedInstances = static_cast<uint32_t>(m_instanceDescs.size());
    m_lastTLASStats.materialRecords = static_cast<uint32_t>(m_rtMaterials.size());

    if (m_instanceDescs.empty()) {
        return;
    }

    const UINT numInstances = static_cast<UINT>(m_instanceDescs.size());
    const UINT64 instanceBufferBytes =
        static_cast<UINT64>(numInstances) * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);

    if (!m_instanceBuffer || instanceBufferBytes > m_instanceBufferSize) {
        // CRITICAL: If the instance buffer exists but is too small, we MUST
        // wait for GPU to finish before destroying and reallocating.
        if (m_instanceBuffer && instanceBufferBytes > m_instanceBufferSize) {
            if (m_flushCallback) {
                spdlog::debug("DX12RaytracingContext: Flushing GPU before instance buffer resize ({} -> {} bytes)",
                             m_instanceBufferSize, instanceBufferBytes);
                m_flushCallback();
            }
        }

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

    const UINT64 materialBufferBytes =
        static_cast<UINT64>(std::max<size_t>(m_rtMaterials.size(), 1u)) * sizeof(RTMaterialGPU);
    m_lastTLASStats.materialBufferBytes = materialBufferBytes;
    if (!m_rtMaterialBuffer || materialBufferBytes > m_rtMaterialBufferSize) {
        if (m_rtMaterialBuffer && materialBufferBytes > m_rtMaterialBufferSize && m_flushCallback) {
            spdlog::debug("DX12RaytracingContext: Flushing GPU before RT material buffer resize ({} -> {} bytes)",
                         m_rtMaterialBufferSize,
                         materialBufferBytes);
            m_flushCallback();
        }

        m_rtMaterialBuffer.Reset();
        m_rtMaterialBufferSize = materialBufferBytes;

        D3D12_HEAP_PROPERTIES heapProps{};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC bufDesc{};
        bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        bufDesc.Alignment = 0;
        bufDesc.Width = materialBufferBytes;
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
            IID_PPV_ARGS(&m_rtMaterialBuffer));
        if (FAILED(hr)) {
            m_rtMaterialBuffer.Reset();
            m_rtMaterialBufferSize = 0;
            return;
        }
    }

    {
        D3D12_RANGE readRange{0, 0};
        void* mapped = nullptr;
        HRESULT hr = m_rtMaterialBuffer->Map(0, &readRange, &mapped);
        if (FAILED(hr) || !mapped) {
            return;
        }

        std::memcpy(mapped, m_rtMaterials.data(), m_rtMaterials.size() * sizeof(RTMaterialGPU));
        m_rtMaterialBuffer->Unmap(0, nullptr);
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
        // CRITICAL: If the TLAS buffer exists but is too small, we MUST
        // wait for GPU to finish before destroying and reallocating.
        if (m_tlas && m_tlas->GetDesc().Width < tlasDesc.Width) {
            if (m_flushCallback) {
                spdlog::debug("DX12RaytracingContext: Flushing GPU before TLAS buffer resize ({} -> {} bytes)",
                             m_tlas->GetDesc().Width, tlasDesc.Width);
                m_flushCallback();
            }
        }

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
        // CRITICAL: If the TLAS scratch buffer exists but is too small, we MUST
        // wait for GPU to finish before destroying and reallocating.
        if (m_tlasScratch && m_tlasScratch->GetDesc().Width < scratchDesc.Width) {
            if (m_flushCallback) {
                spdlog::debug("DX12RaytracingContext: Flushing GPU before TLAS scratch buffer resize ({} -> {} bytes)",
                             m_tlasScratch->GetDesc().Width, scratchDesc.Width);
                m_flushCallback();
            }
        }

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

} // namespace Cortex::Graphics
