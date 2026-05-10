#include "Renderer.h"

#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

namespace Cortex::Graphics {

    void Renderer::RenderSceneIndirect(Scene::ECS_Registry *registry)
    {
        if (/* !m_indirectCommandSignature || */ !m_services.gpuCulling) return;

        bool dumpCommands = (std::getenv("CORTEX_DUMP_INDIRECT") != nullptr);
        bool bypassCompaction = (std::getenv("CORTEX_NO_CULL_COMPACTION") != nullptr);
        bool freezeCulling = (std::getenv("CORTEX_GPUCULL_FREEZE") != nullptr) || m_gpuCullingState.freeze;
        bool freezeCullingEnv = (std::getenv("CORTEX_GPUCULL_FREEZE") != nullptr);

        RendererSceneSnapshot fallbackSnapshot{};
        const RendererSceneSnapshot* snapshot = &m_framePlanning.sceneSnapshot;
        if (!snapshot->IsValidForFrame(m_frameLifecycle.renderFrameCounter)) {
            fallbackSnapshot = BuildRendererSceneSnapshot(registry, m_frameLifecycle.renderFrameCounter);
            snapshot = &fallbackSnapshot;
        }

        std::vector<IndirectCommand> commands;
        commands.reserve(snapshot->depthWritingIndices.size());

        m_gpuCullingState.instances.clear();
        ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };

        for (uint32_t entryIndex : snapshot->depthWritingIndices) {
            if (entryIndex >= snapshot->entries.size()) continue;
            const RendererSceneRenderable& entry = snapshot->entries[entryIndex];
            auto* renderablePtr = entry.renderable;
            if (!entry.visible || !entry.hasMesh || !entry.hasTransform || !entry.hasGpuBuffers || !renderablePtr) continue;
            auto entity = entry.entity;
            auto& renderable = *renderablePtr;

            EnsureMaterialTextures(renderable);
            auto getOrAllocateCullingId = [](entt::entity e) { return static_cast<uint32_t>(e); };

        const MaterialTextureFallbacks materialFallbacks{
            m_materialFallbacks.albedo.get(),
            m_materialFallbacks.normal.get(),
            m_materialFallbacks.metallic.get(),
            m_materialFallbacks.roughness.get()
        };
        const MaterialModel materialModel = MaterialResolver::ResolveRenderable(renderable, materialFallbacks);
        MaterialConstants materialData = MaterialResolver::BuildMaterialConstants(materialModel);

        FillMaterialTextureIndices(renderable, materialData);

        // Match the full material parameter setup used by the forward path.
        const auto& fractal = m_fractalSurfaceState;
        materialData.fractalParams0 = glm::vec4(
            fractal.amplitude,
            fractal.frequency,
            fractal.octaves,
            (fractal.amplitude > 0.0f ? 1.0f : 0.0f));
        materialData.fractalParams1 = glm::vec4(
            fractal.coordMode,
            fractal.scaleX,
            fractal.scaleZ,
            materialModel.materialType);
        materialData.fractalParams2 = glm::vec4(
            fractal.lacunarity,
            fractal.gain,
            fractal.warpStrength,
            fractal.noiseType);

        if (!renderable.mesh->hasBounds) {
            renderable.mesh->UpdateBounds();
        }

        glm::mat4 modelMatrix = entry.worldMatrix;
        const uint32_t stableKey = static_cast<uint32_t>(entity);
        const AutoDepthSeparation sep =
            ComputeAutoDepthSeparationForThinSurfaces(renderable, modelMatrix, stableKey);
        ApplyAutoDepthOffset(modelMatrix, sep.worldOffset);

        ObjectConstants objectData = {};
        objectData.modelMatrix = modelMatrix;
        objectData.normalMatrix = entry.normalMatrix;
        objectData.depthBiasNdc = sep.depthBiasNdc;

        D3D12_GPU_VIRTUAL_ADDRESS objectCB = m_constantBuffers.object.AllocateAndWrite(objectData);
        D3D12_GPU_VIRTUAL_ADDRESS materialCB = m_constantBuffers.material.AllocateAndWrite(materialData);

        GPUInstanceData inst{};
        inst.modelMatrix = modelMatrix;
        glm::vec3 centerWS = glm::vec3(modelMatrix[3]);
        if (renderable.mesh->hasBounds) {
            inst.boundingSphere = glm::vec4(
                renderable.mesh->boundsCenter,
                renderable.mesh->boundsRadius
            );
            centerWS = glm::vec3(modelMatrix * glm::vec4(renderable.mesh->boundsCenter, 1.0f));
        } else {
            inst.boundingSphere = glm::vec4(0.0f, 0.0f, 0.0f, 10.0f);
        }
        inst.meshIndex = 0;
        inst.materialIndex = 0;
        inst.flags = 1;
        inst.cullingId = getOrAllocateCullingId(entity);
        {
            auto prevIt = m_gpuCullingState.previousCenterByEntity.find(entity);
            const glm::vec3 prev = (prevIt != m_gpuCullingState.previousCenterByEntity.end()) ? prevIt->second : centerWS;
            inst.prevCenterWS = glm::vec4(prev.x, prev.y, prev.z, 0.0f);
            m_gpuCullingState.previousCenterByEntity[entity] = centerWS;
        }
        m_gpuCullingState.instances.push_back(inst);

        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = renderable.mesh->gpuBuffers->vertexBuffer->GetGPUVirtualAddress();
        vbv.SizeInBytes = static_cast<UINT>(renderable.mesh->positions.size() * sizeof(Vertex));
        vbv.StrideInBytes = sizeof(Vertex);

        D3D12_INDEX_BUFFER_VIEW ibv = {};
        ibv.BufferLocation = renderable.mesh->gpuBuffers->indexBuffer->GetGPUVirtualAddress();
        ibv.SizeInBytes = static_cast<UINT>(renderable.mesh->indices.size() * sizeof(uint32_t));
        ibv.Format = DXGI_FORMAT_R32_UINT;

        IndirectCommand cmd{};
        cmd.objectCBV = objectCB;
        cmd.materialCBV = materialCB;
        cmd.vertexBuffer = vbv;
        cmd.indexBuffer = ibv;
        cmd.draw.indexCountPerInstance = static_cast<uint32_t>(renderable.mesh->indices.size());
        cmd.draw.instanceCount = 1;
        cmd.draw.startIndexLocation = 0;
        cmd.draw.baseVertexLocation = 0;
        cmd.draw.startInstanceLocation = 0;
        commands.push_back(cmd);
    }

    if (commands.empty()) {
        return;
    }

    if (dumpCommands && !bypassCompaction) {
        static uint64_t s_lastDumpFrame = 0;
        if ((m_frameLifecycle.renderFrameCounter % 120) == 0 && m_frameLifecycle.renderFrameCounter != s_lastDumpFrame) {
            const uint32_t maxLog = std::min<uint32_t>(static_cast<uint32_t>(commands.size()), 2u);
            for (uint32_t i = 0; i < maxLog; ++i) {
                const auto& cmd = commands[i];
                spdlog::info(
                    "CPU Cmd[{}]: objectCBV=0x{:016X} materialCBV=0x{:016X} "
                    "VBV(addr=0x{:016X} size={} stride={}) "
                    "IBV(addr=0x{:016X} size={} fmt={}) "
                    "draw(indexCount={} instanceCount={} startIndex={} baseVertex={} startInstance={})",
                    i,
                    static_cast<uint64_t>(cmd.objectCBV),
                    static_cast<uint64_t>(cmd.materialCBV),
                    static_cast<uint64_t>(cmd.vertexBuffer.BufferLocation),
                    cmd.vertexBuffer.SizeInBytes,
                    cmd.vertexBuffer.StrideInBytes,
                    static_cast<uint64_t>(cmd.indexBuffer.BufferLocation),
                    cmd.indexBuffer.SizeInBytes,
                    static_cast<unsigned int>(cmd.indexBuffer.Format),
                    cmd.draw.indexCountPerInstance,
                    cmd.draw.instanceCount,
                    cmd.draw.startIndexLocation,
                    cmd.draw.baseVertexLocation,
                    cmd.draw.startInstanceLocation);
            }
            s_lastDumpFrame = m_frameLifecycle.renderFrameCounter;
            m_services.gpuCulling->RequestCommandReadback(maxLog);
        }
    }

    auto commandResult = m_services.gpuCulling->UpdateIndirectCommands(m_commandResources.graphicsList.Get(), commands);
    if (commandResult.IsErr()) {
        spdlog::warn("RenderSceneIndirect: failed to upload commands: {}", commandResult.Error());
        RenderScene(registry);
        return;
    }

    if (!bypassCompaction) {
        auto uploadResult = m_services.gpuCulling->UpdateInstances(m_commandResources.graphicsList.Get(), m_gpuCullingState.instances);
        if (uploadResult.IsErr()) {
            spdlog::warn("RenderSceneIndirect: failed to upload instances: {}", uploadResult.Error());
            RenderScene(registry);
            return;
        }

        glm::mat4 viewProjForCulling = m_constantBuffers.frameCPU.viewProjectionNoJitter;
        glm::vec3 cameraPosForCulling = glm::vec3(m_constantBuffers.frameCPU.cameraPosition);

        if (!freezeCulling) {
            m_gpuCullingState.freezeCaptured = false;
        } else {
            if (!m_gpuCullingState.freezeCaptured) {
                m_gpuCullingState.freezeCaptured = true;
                m_gpuCullingState.frozenViewProj = viewProjForCulling;
                m_gpuCullingState.frozenCameraPos = cameraPosForCulling;
                spdlog::warn("GPU culling freeze enabled ({}): capturing view on frame {}",
                             freezeCullingEnv ? "env CORTEX_GPUCULL_FREEZE=1" : "K toggle",
                             m_frameLifecycle.renderFrameCounter);
            }
            viewProjForCulling = m_gpuCullingState.frozenViewProj;
            cameraPosForCulling = m_gpuCullingState.frozenCameraPos;
        }

        // Optional HZB occlusion culling. We build the HZB late in the frame
        // and consume it on the next frame's culling dispatch.
        static bool s_checkedGpuCullHzbEnv = false;
        static bool s_disableGpuCullHzb = false;
        if (!s_checkedGpuCullHzbEnv) {
            s_checkedGpuCullHzbEnv = true;
            s_disableGpuCullHzb = (std::getenv("CORTEX_DISABLE_GPUCULL_HZB") != nullptr);
            if (s_disableGpuCullHzb) {
                spdlog::info("GPU culling: HZB occlusion disabled (CORTEX_DISABLE_GPUCULL_HZB=1)");
            }
        }

        bool useHzbOcclusion = false;
        if (!s_disableGpuCullHzb &&
            m_hzbResources.valid && m_hzbResources.captureValid && m_hzbResources.texture && m_hzbResources.mipCount > 0) {
            // Require the HZB capture to be from the immediately previous frame.
            if (m_hzbResources.captureFrameCounter + 1u == m_frameLifecycle.renderFrameCounter) {
                const bool strictGate = (std::getenv("CORTEX_GPUCULL_HZB_STRICT_GATE") != nullptr);
                if (!strictGate) {
                    // Motion robustness is handled conservatively in the shader
                    // via inflated footprints + mip bias; do not hard-disable
                    // occlusion on camera movement by default.
                    useHzbOcclusion = true;
                } else {
                    const float dist = glm::length(m_cameraState.positionWS - m_hzbResources.captureCameraPosWS);
                    const glm::vec3 fwdNow = glm::normalize(m_cameraState.forwardWS);
                    const glm::vec3 fwdThen = glm::normalize(m_hzbResources.captureCameraForwardWS);
                    const float dotFwd = glm::clamp(glm::dot(fwdNow, fwdThen), -1.0f, 1.0f);
                    // Conservative gates: allow only small camera movement/rotation.
                    constexpr float kMaxHzbDist = 0.35f;          // meters/units
                    constexpr float kMaxHzbAngleDeg = 2.0f;       // degrees
                    const float angleDeg = std::acos(dotFwd) * (180.0f / glm::pi<float>());
                    useHzbOcclusion = (dist <= kMaxHzbDist) && (angleDeg <= kMaxHzbAngleDeg);
                }
            }

        }
        // When culling is frozen for debugging, keep the result stable by
        // disabling HZB occlusion (the HZB itself continues updating with the
        // real camera, which can otherwise change occlusion outcomes).
        if (freezeCulling) {
            useHzbOcclusion = false;
        }
        m_gpuCullingState.hzbOcclusionUsedThisFrame = useHzbOcclusion;

        m_services.gpuCulling->SetHZBForOcclusion(
            useHzbOcclusion ? m_hzbResources.texture.Get() : nullptr,
            m_hzbResources.width,
            m_hzbResources.height,
            m_hzbResources.mipCount,
            m_hzbResources.captureViewMatrix,
            m_hzbResources.captureViewProjMatrix,
            m_hzbResources.captureCameraPosWS,
            m_hzbResources.captureNearPlane,
            m_hzbResources.captureFarPlane,
            useHzbOcclusion);

        {
            // Ensure the HZB resource is in an SRV-readable state for compute.
            if (useHzbOcclusion &&
                (m_hzbResources.resourceState & D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE) == 0) {
                D3D12_RESOURCE_BARRIER barrier{};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = m_hzbResources.texture.Get();
                barrier.Transition.StateBefore = m_hzbResources.resourceState;
                barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
                barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
                m_commandResources.graphicsList->ResourceBarrier(1, &barrier);
                m_hzbResources.resourceState = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            }

            auto cullResult = m_services.gpuCulling->DispatchCulling(
                m_commandResources.graphicsList.Get(),
                viewProjForCulling,
                cameraPosForCulling
            );
            if (cullResult.IsErr()) {
                spdlog::warn("RenderSceneIndirect: culling dispatch failed: {}", cullResult.Error());
                RenderScene(registry);
                return;
            }
        }
    } else {
        auto prepResult = m_services.gpuCulling->PrepareAllCommandsForExecuteIndirect(m_commandResources.graphicsList.Get());
        if (prepResult.IsErr()) {
            spdlog::warn("RenderSceneIndirect: failed to prepare all-commands buffer: {}", prepResult.Error());
            RenderScene(registry);
            return;
        }
    }

    // Compute dispatch changes the root signature/pipeline; restore graphics state
    m_commandResources.graphicsList->SetGraphicsRootSignature(m_pipelineState.rootSignature->GetRootSignature());
    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.geometry->GetPipelineState());
    m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);
    m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(1, m_constantBuffers.currentFrameGPU);
    if (m_environmentState.shadowAndEnvDescriptors[0].IsValid()) {
        m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(4, m_environmentState.shadowAndEnvDescriptors[0].gpu);
    }
    if (m_materialFallbacks.descriptorTable[0].IsValid()) {
        m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(3, m_materialFallbacks.descriptorTable[0].gpu);
    }
    if (m_constantBuffers.biomeMaterialsValid) {
        m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(7, m_constantBuffers.biomeMaterials.gpuAddress);
    }

    const UINT maxCommands = static_cast<UINT>(commands.size());
    ID3D12CommandSignature* cmdSig = nullptr; // m_indirectCommandSignature.Get(); // FIX ME: identifier not found
    ID3D12Resource* argBuffer = nullptr; // m_services.gpuCulling->GetArgumentBuffer();
    ID3D12Resource* countBuffer = nullptr; // m_services.gpuCulling->GetCountBuffer();

    if (cmdSig && argBuffer && countBuffer) {
        m_commandResources.graphicsList->ExecuteIndirect(
            cmdSig,
            maxCommands,
            argBuffer,
            0,
            countBuffer,
            0
        );
        ++m_frameDiagnostics.contract.drawCounts.indirectExecuteCalls;
        m_frameDiagnostics.contract.drawCounts.indirectCommands += maxCommands;
    }

    static uint64_t s_lastCullingLogFrame = 0;
    if ((m_frameLifecycle.renderFrameCounter % 300) == 0 && m_frameLifecycle.renderFrameCounter != s_lastCullingLogFrame) {
        const uint32_t total = m_services.gpuCulling->GetTotalInstances();
        const uint32_t visible = m_services.gpuCulling->GetVisibleCount();
        const float visiblePct = (total > 0) ? (100.0f * static_cast<float>(visible) / total) : 0.0f;
        spdlog::info("GPU Culling: total={}, visible={} ({:.1f}% visible)", total, visible, visiblePct);
        s_lastCullingLogFrame = m_frameLifecycle.renderFrameCounter;
    }
}

} // namespace Cortex::Graphics
