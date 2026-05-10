#include "Renderer.h"

#include "Graphics/RendererGeometryUtils.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cstring>
#include <vector>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)
void Renderer::RenderParticles(Scene::ECS_Registry* registry) {
    m_particleState.ResetFrameStats();
    if (m_frameLifecycle.deviceRemoved || !registry || !m_pipelineState.particle || !m_mainTargets.hdrColor || m_particleState.instanceMapFailed) {
        return;
    }

    // Cap the number of particles we draw in a single frame to keep the
    // per-frame instance buffer small and avoid pathological memory usage if
    // an emitter accidentally spawns an excessive number of particles.
    static constexpr uint32_t kMaxParticleInstances = 4096;
    m_particleState.frameMaxInstances = kMaxParticleInstances;

    auto view = registry->View<Scene::ParticleEmitterComponent, Scene::TransformComponent>();
    if (view.begin() == view.end()) {
        return;
    }

    std::vector<ParticleInstance> instances;
    instances.reserve(1024);

    const FrustumPlanes frustum = ExtractFrustumPlanesCPU(m_constantBuffers.frameCPU.viewProjectionNoJitter);

    for (auto entity : view) {
        auto& emitter   = view.get<Scene::ParticleEmitterComponent>(entity);
        auto& transform = view.get<Scene::TransformComponent>(entity);
        ++m_particleState.frameEmitterCount;

        glm::vec3 emitterWorldPos = glm::vec3(transform.worldMatrix[3]);

        // Conservative per-emitter frustum culling. This is most meaningful for
        // local-space emitters; for world-space emitters we still do per-particle
        // culling below.
        if (emitter.localSpace) {
            const float maxSpeed =
                glm::length(emitter.initialVelocity) + glm::length(emitter.velocityRandom);
            const float conservativeRadius =
                glm::max(0.5f, maxSpeed * emitter.lifetime + glm::max(emitter.sizeStart, emitter.sizeEnd));
            if (!SphereIntersectsFrustumCPU(frustum, emitterWorldPos, conservativeRadius)) {
                continue;
            }
        }

        for (const auto& p : emitter.particles) {
            if (p.age >= p.lifetime) {
                continue;
            }
            ++m_particleState.frameLiveParticles;

            if (instances.size() >= kMaxParticleInstances) {
                m_particleState.frameCapped = true;
                break;
            }

            ParticleInstance inst{};
            inst.position = emitter.localSpace ? (emitterWorldPos + p.position) : p.position;
            inst.size     = p.size;
            inst.color    = p.color;

            if (!SphereIntersectsFrustumCPU(frustum, inst.position, glm::max(0.01f, inst.size))) {
                ++m_particleState.frameFrustumCulled;
                continue;
            }

            instances.push_back(inst);
        }

        if (instances.size() >= kMaxParticleInstances) {
            break;
        }
    }

    if (instances.empty()) {
        return;
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return;
    }

    const UINT instanceCount = static_cast<UINT>(instances.size());
    m_particleState.frameSubmittedInstances = instanceCount;
    const UINT requiredCapacity = instanceCount;
    const UINT minCapacity = 256;

    if (!m_particleState.instanceBuffer || m_particleState.instanceCapacity < requiredCapacity) {
        // CRITICAL: If replacing an existing buffer, wait for GPU to finish using it
        if (m_particleState.instanceBuffer) {
            WaitForGPU();
        }

        UINT newCapacity = std::max(requiredCapacity, minCapacity);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<UINT64>(newCapacity) * sizeof(ParticleInstance);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        ComPtr<ID3D12Resource> buffer;
        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&buffer));

        if (FAILED(hr)) {
            spdlog::warn("RenderParticles: failed to allocate instance buffer");
            return;
        }

        m_particleState.instanceBuffer = buffer;
        m_particleState.instanceCapacity = newCapacity;
    }

    // Upload instance data
    void* mapped = nullptr;
    D3D12_RANGE readRange{0, 0};
    const UINT bufferSize = instanceCount * sizeof(ParticleInstance);
    HRESULT mapHr = m_particleState.instanceBuffer->Map(0, &readRange, &mapped);
    if (SUCCEEDED(mapHr)) {
        memcpy(mapped, instances.data(), bufferSize);
        m_particleState.instanceBuffer->Unmap(0, nullptr);
    } else {
        spdlog::warn("RenderParticles: failed to map instance buffer (hr=0x{:08X}); disabling particles for this run",
                     static_cast<unsigned int>(mapHr));
        // Map failures are one of the first places a hung device surfaces.
        // Capture rich diagnostics so we can see which pass/frame triggered
        // device removal.
        CORTEX_REPORT_DEVICE_REMOVED("RenderParticles_MapInstanceBuffer", mapHr);
        m_particleState.instanceMapFailed = true;
        return;
    }

    // Persistent quad vertex buffer in an upload heap; tiny and self-contained.
    struct QuadVertex { float px, py, pz; float u, v; };
    static const QuadVertex kQuadVertices[4] = {
        { -0.5f, -0.5f, 0.0f, 0.0f, 1.0f },
        { -0.5f,  0.5f, 0.0f, 0.0f, 0.0f },
        {  0.5f, -0.5f, 0.0f, 1.0f, 1.0f },
        {  0.5f,  0.5f, 0.0f, 1.0f, 0.0f },
    };

    if (!m_particleState.quadVertexBuffer) {
        D3D12_HEAP_PROPERTIES quadHeapProps = {};
        quadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        quadHeapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        quadHeapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        quadHeapProps.CreationNodeMask = 1;
        quadHeapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC vbDesc = {};
        vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        vbDesc.Width = sizeof(kQuadVertices);
        vbDesc.Height = 1;
        vbDesc.DepthOrArraySize = 1;
        vbDesc.MipLevels = 1;
        vbDesc.Format = DXGI_FORMAT_UNKNOWN;
        vbDesc.SampleDesc.Count = 1;
        vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        vbDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

        HRESULT hrVB = device->CreateCommittedResource(
            &quadHeapProps,
            D3D12_HEAP_FLAG_NONE,
            &vbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&m_particleState.quadVertexBuffer));
        if (FAILED(hrVB)) {
            spdlog::warn("RenderParticles: failed to allocate quad vertex buffer (hr=0x{:08X})",
                         static_cast<unsigned int>(hrVB));
            CORTEX_REPORT_DEVICE_REMOVED("RenderParticles_CreateQuadVB", hrVB);
            return;
        }

        void* quadMapped = nullptr;
        HRESULT mapQuadHr = m_particleState.quadVertexBuffer->Map(0, &readRange, &quadMapped);
        if (SUCCEEDED(mapQuadHr)) {
            memcpy(quadMapped, kQuadVertices, sizeof(kQuadVertices));
            m_particleState.quadVertexBuffer->Unmap(0, nullptr);
        } else {
            spdlog::warn("RenderParticles: failed to map quad vertex buffer (hr=0x{:08X})",
                         static_cast<unsigned int>(mapQuadHr));
            CORTEX_REPORT_DEVICE_REMOVED("RenderParticles_MapQuadVB", mapQuadHr);
            m_particleState.quadVertexBuffer.Reset();
            return;
        }
    }

    // --- FIX: Bind render targets with depth buffer BEFORE setting pipeline ---
    // The particle pipeline expects DXGI_FORMAT_D32_FLOAT depth, so we MUST bind the DSV

    // 1. Transition depth buffer to write state if needed
    // 2. NEW FIX: Transition HDR Color to RENDER_TARGET (may be in PIXEL_SHADER_RESOURCE from previous pass)
    D3D12_RESOURCE_BARRIER barriers[2] = {};
    uint32_t barrierCount = 0;

    if (m_depthResources.buffer && m_depthResources.resourceState != D3D12_RESOURCE_STATE_DEPTH_WRITE) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_depthResources.buffer.Get();
        barriers[barrierCount].Transition.StateBefore = m_depthResources.resourceState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_depthResources.resourceState = D3D12_RESOURCE_STATE_DEPTH_WRITE;
    }

    if (m_mainTargets.hdrColor && m_mainTargets.hdrState != D3D12_RESOURCE_STATE_RENDER_TARGET) {
        barriers[barrierCount].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[barrierCount].Transition.pResource = m_mainTargets.hdrColor.Get();
        barriers[barrierCount].Transition.StateBefore = m_mainTargets.hdrState;
        barriers[barrierCount].Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barriers[barrierCount].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrierCount++;
        m_mainTargets.hdrState = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }

    if (barrierCount > 0) {
        m_commandResources.graphicsList->ResourceBarrier(barrierCount, barriers);
    }

    // 3. Bind render targets (HDR color + depth)
    D3D12_CPU_DESCRIPTOR_HANDLE rtv = m_mainTargets.hdrRTV.cpu;
    D3D12_CPU_DESCRIPTOR_HANDLE dsv = m_depthResources.dsv.cpu;
    m_commandResources.graphicsList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

    m_commandResources.graphicsList->SetGraphicsRootSignature(m_pipelineState.rootSignature->GetRootSignature());
    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.particle->GetPipelineState());

    if (m_environmentState.shadowAndEnvDescriptors[0].IsValid()) {
        m_commandResources.graphicsList->SetGraphicsRootDescriptorTable(4, m_environmentState.shadowAndEnvDescriptors[0].gpu);
    }

    ID3D12DescriptorHeap* heaps[] = { m_services.descriptorManager->GetCBV_SRV_UAV_Heap() };
    m_commandResources.graphicsList->SetDescriptorHeaps(1, heaps);

    ObjectConstants obj{};
    obj.modelMatrix  = glm::mat4(1.0f);
    obj.normalMatrix = glm::mat4(1.0f);
    auto objAddr = m_constantBuffers.object.AllocateAndWrite(obj);
    m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(0, objAddr);

    D3D12_VERTEX_BUFFER_VIEW vbViews[2] = {};
    vbViews[0].BufferLocation = m_particleState.quadVertexBuffer->GetGPUVirtualAddress();
    vbViews[0].StrideInBytes  = sizeof(QuadVertex);
    vbViews[0].SizeInBytes    = sizeof(kQuadVertices);

    vbViews[1].BufferLocation = m_particleState.instanceBuffer->GetGPUVirtualAddress();
    vbViews[1].StrideInBytes  = sizeof(ParticleInstance);
    vbViews[1].SizeInBytes    = bufferSize;

    m_commandResources.graphicsList->IASetVertexBuffers(0, 2, vbViews);
    m_commandResources.graphicsList->IASetIndexBuffer(nullptr);
    m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    m_commandResources.graphicsList->DrawInstanced(4, instanceCount, 0, 0);
    m_particleState.frameExecuted = true;
    ++m_frameDiagnostics.contract.drawCounts.particleDraws;
    m_frameDiagnostics.contract.drawCounts.particleInstances += instanceCount;
}

// ============================================================================
// Vegetation Rendering System
// ============================================================================

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics

