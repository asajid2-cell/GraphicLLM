#include "Renderer.h"

#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void Renderer::SetVegetationEnabled(bool enabled) {
    m_vegetationState.enabled = enabled;
}

bool Renderer::IsVegetationEnabled() const {
    return GetFeatureState().vegetationEnabled;
}

void Renderer::SetWindParams(const Scene::WindParams& params) {
    m_vegetationState.wind = params;
}

const Scene::WindParams& Renderer::GetWindParams() const {
    return m_vegetationState.wind;
}

const Scene::VegetationStats& Renderer::GetVegetationStats() const {
    return m_vegetationState.stats;
}

Result<void> Renderer::CreateVegetationPipelines() {
    // Vegetation pipelines will be created when needed
    // The shaders are: VegetationMesh.hlsl and VegetationBillboard.hlsl
    return Result<void>::Ok();
}

Result<void> Renderer::CreateVegetationInstanceBuffer(UINT capacity) {
    if (m_frameLifecycle.deviceRemoved || !m_services.device) {
        return Result<void>::Err("Device not available");
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return Result<void>::Err("D3D12 device is null");
    }

    // Wait for GPU if replacing existing buffer
    if (m_vegetationState.meshInstances.buffer) {
        WaitForGPU();
    }

    return m_vegetationState.meshInstances.CreateStructuredUploadBuffer<VegetationInstanceGPU>(
        device,
        m_services.descriptorManager.get(),
        capacity,
        "vegetation");
}

Result<void> Renderer::CreateBillboardInstanceBuffer(UINT capacity) {
    if (m_frameLifecycle.deviceRemoved || !m_services.device) {
        return Result<void>::Err("Device not available");
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return Result<void>::Err("D3D12 device is null");
    }

    if (m_vegetationState.billboardInstances.buffer) {
        WaitForGPU();
    }

    return m_vegetationState.billboardInstances.CreateStructuredUploadBuffer<BillboardInstanceGPU>(
        device,
        m_services.descriptorManager.get(),
        capacity,
        "billboard");
}

Result<void> Renderer::CreateGrassInstanceBuffer(UINT capacity) {
    if (m_frameLifecycle.deviceRemoved || !m_services.device) {
        return Result<void>::Err("Device not available");
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device) {
        return Result<void>::Err("D3D12 device is null");
    }

    if (m_vegetationState.grassInstances.buffer) {
        WaitForGPU();
    }

    return m_vegetationState.grassInstances.CreateStructuredUploadBuffer<GrassInstanceGPU>(
        device,
        m_services.descriptorManager.get(),
        capacity,
        "grass");
}

void Renderer::UpdateVegetationConstantBuffer(const glm::mat4& viewProj, const glm::mat4& view,
                                               const glm::vec3& cameraPos, const glm::vec3& cameraRight,
                                               const glm::vec3& cameraUp) {
    VegetationConstants constants{};
    constants.viewProj = viewProj;
    constants.view = view;
    constants.cameraPosition = glm::vec4(cameraPos, m_frameRuntime.totalTime);
    constants.cameraRight = glm::vec4(cameraRight, 0.0f);
    constants.cameraUp = glm::vec4(cameraUp, 0.0f);
    constants.windDirection = glm::vec4(m_vegetationState.wind.direction,
                                        m_vegetationState.wind.speed,
                                        m_vegetationState.wind.time);
    constants.windParams = glm::vec4(m_vegetationState.wind.gustStrength,
                                     m_vegetationState.wind.gustFrequency,
                                     m_vegetationState.wind.turbulence,
                                     0.0f);
    constants.lodDistances = glm::vec4(50.0f, 100.0f, 200.0f, 500.0f);  // Default LOD distances
    constants.fadeParams = glm::vec4(5.0f, 1.0f, 0.0005f, 0.0f);  // crossfade, dither, shadow bias

    m_vegetationState.constants.UpdateData(constants);
}

void Renderer::UpdateVegetationInstances(const std::vector<Scene::VegetationInstance>& instances,
                                          const std::vector<Scene::VegetationPrototype>& prototypes,
                                          const glm::vec3& cameraPos) {
    (void)cameraPos;
    if (instances.empty()) {
        m_vegetationState.meshInstances.count = 0;
        return;
    }

    const UINT instanceCount = static_cast<UINT>(instances.size());
    const UINT minCapacity = 1024;
    const UINT requiredCapacity = std::max(instanceCount, minCapacity);

    // Resize buffer if needed
    if (!m_vegetationState.meshInstances.buffer || m_vegetationState.meshInstances.capacity < requiredCapacity) {
        auto result = CreateVegetationInstanceBuffer(requiredCapacity);
        if (!result.IsOk()) {
            spdlog::warn("Failed to create vegetation instance buffer: {}", result.Error());
            return;
        }
    }

    // Build GPU instance data
    std::vector<VegetationInstanceGPU> gpuInstances;
    gpuInstances.reserve(instanceCount);

    for (const auto& inst : instances) {
        if (!inst.IsVisible()) continue;

        VegetationInstanceGPU gpuInst{};

        // Build world matrix from position, rotation, scale
        glm::mat4 translation = glm::translate(glm::mat4(1.0f), inst.position);
        glm::mat4 rotation = glm::mat4_cast(inst.rotation);
        glm::mat4 scale = glm::scale(glm::mat4(1.0f), inst.scale);
        gpuInst.worldMatrix = translation * rotation * scale;

        // Color tint (use default white if no variation)
        gpuInst.colorTint = glm::vec4(1.0f);

        // Wind parameters - use distance to camera to vary phase
        float phase = glm::dot(inst.position, glm::vec3(0.1f, 0.0f, 0.1f));
        float windStrength = inst.IsWindAffected() ? 1.0f : 0.0f;

        // Calculate fade alpha based on LOD distance
        float lodFade = 1.0f;
        if (inst.prototypeIndex < prototypes.size()) {
            const auto& proto = prototypes[inst.prototypeIndex];
            float dist = inst.distanceToCamera;

            // Calculate fade based on current LOD transition
            if (inst.currentLOD == Scene::VegetationLOD::Full && dist > proto.lodDistance0 - proto.crossfadeRange) {
                lodFade = 1.0f - (dist - (proto.lodDistance0 - proto.crossfadeRange)) / proto.crossfadeRange;
            } else if (inst.currentLOD == Scene::VegetationLOD::Medium && dist > proto.lodDistance1 - proto.crossfadeRange) {
                lodFade = 1.0f - (dist - (proto.lodDistance1 - proto.crossfadeRange)) / proto.crossfadeRange;
            } else if (inst.currentLOD == Scene::VegetationLOD::Low && dist > proto.lodDistance2 - proto.crossfadeRange) {
                lodFade = 1.0f - (dist - (proto.lodDistance2 - proto.crossfadeRange)) / proto.crossfadeRange;
            }
        }

        gpuInst.windParams = glm::vec4(phase, windStrength, 1.0f, lodFade);
        gpuInst.prototypeIndex = inst.prototypeIndex;
        gpuInst.lodLevel = static_cast<uint32_t>(inst.currentLOD);
        gpuInst.fadeAlpha = glm::clamp(lodFade, 0.0f, 1.0f);
        gpuInst.padding = 0.0f;

        gpuInstances.push_back(gpuInst);
    }

    if (gpuInstances.empty()) {
        m_vegetationState.meshInstances.count = 0;
        return;
    }

    auto uploadResult = m_vegetationState.meshInstances.Upload(gpuInstances, "vegetation");
    if (uploadResult.IsErr()) {
        spdlog::warn("{}", uploadResult.Error());
    }

    // Update stats
    m_vegetationState.stats.totalInstances = static_cast<uint32_t>(instances.size());
    m_vegetationState.stats.visibleInstances = m_vegetationState.meshInstances.count;
}

void Renderer::UpdateBillboardInstances(const std::vector<Scene::VegetationInstance>& instances,
                                         const std::vector<Scene::VegetationPrototype>& prototypes) {
    // Filter to only billboard LOD instances
    std::vector<BillboardInstanceGPU> gpuInstances;

    for (const auto& inst : instances) {
        if (!inst.IsVisible() || inst.currentLOD != Scene::VegetationLOD::Billboard) {
            continue;
        }

        BillboardInstanceGPU gpuInst{};
        gpuInst.position = inst.position;
        gpuInst.padding0 = 0.0f;

        // Get size from prototype or use default
        float width = 2.0f;
        float height = 4.0f;
        if (inst.prototypeIndex < prototypes.size()) {
            // Use average scale as size multiplier
            float avgScale = (inst.scale.x + inst.scale.y + inst.scale.z) / 3.0f;
            width *= avgScale;
            height *= avgScale;
        }
        gpuInst.size = glm::vec2(width, height);
        gpuInst.padding1 = glm::vec2(0.0f);

        // Default UV bounds (full atlas region)
        gpuInst.uvBounds = glm::vec4(0.0f, 0.0f, 1.0f, 1.0f);
        gpuInst.colorTint = glm::vec4(1.0f);

        // Random rotation based on position
        gpuInst.rotation = glm::fract(inst.position.x * 0.1f + inst.position.z * 0.1f) * 6.28318f;
        gpuInst.windPhase = glm::dot(inst.position, glm::vec3(0.1f, 0.0f, 0.1f));
        gpuInst.windStrength = inst.IsWindAffected() ? 0.5f : 0.0f;
        gpuInst.padding2 = 0.0f;

        gpuInstances.push_back(gpuInst);
    }

    const UINT instanceCount = static_cast<UINT>(gpuInstances.size());
    if (instanceCount == 0) {
        m_vegetationState.billboardInstances.count = 0;
        return;
    }

    const UINT requiredCapacity = std::max(instanceCount, 1024u);
    if (!m_vegetationState.billboardInstances.buffer || m_vegetationState.billboardInstances.capacity < requiredCapacity) {
        auto result = CreateBillboardInstanceBuffer(requiredCapacity);
        if (!result.IsOk()) {
            spdlog::warn("Failed to create billboard instance buffer");
            return;
        }
    }

    auto uploadResult = m_vegetationState.billboardInstances.Upload(gpuInstances, "billboard");
    if (uploadResult.IsErr()) {
        spdlog::warn("{}", uploadResult.Error());
    }

    m_vegetationState.stats.billboardCount = instanceCount;
}

void Renderer::UpdateGrassInstances(const std::vector<Scene::VegetationInstance>& instances) {
    std::vector<GrassInstanceGPU> gpuInstances;

    for (const auto& inst : instances) {
        if (!inst.IsVisible()) continue;

        GrassInstanceGPU gpuInst{};
        glm::mat4 translation = glm::translate(glm::mat4(1.0f), inst.position);
        glm::mat4 rotation = glm::mat4_cast(inst.rotation);
        glm::mat4 scale = glm::scale(glm::mat4(1.0f), inst.scale);
        gpuInst.worldMatrix = translation * rotation * scale;
        gpuInst.colorTint = glm::vec4(0.3f, 0.6f, 0.2f, 1.0f);  // Grass green
        gpuInst.windPhase = glm::dot(inst.position, glm::vec3(0.1f, 0.0f, 0.1f));
        gpuInst.windStrength = inst.IsWindAffected() ? 1.0f : 0.0f;
        gpuInst.height = inst.scale.y;
        gpuInst.bend = 0.1f;

        gpuInstances.push_back(gpuInst);
    }

    const UINT instanceCount = static_cast<UINT>(gpuInstances.size());
    if (instanceCount == 0) {
        m_vegetationState.grassInstances.count = 0;
        return;
    }

    const UINT requiredCapacity = std::max(instanceCount, 4096u);
    if (!m_vegetationState.grassInstances.buffer || m_vegetationState.grassInstances.capacity < requiredCapacity) {
        auto result = CreateGrassInstanceBuffer(requiredCapacity);
        if (!result.IsOk()) {
            spdlog::warn("Failed to create grass instance buffer");
            return;
        }
    }

    auto uploadResult = m_vegetationState.grassInstances.Upload(gpuInstances, "grass");
    if (uploadResult.IsErr()) {
        spdlog::warn("{}", uploadResult.Error());
    }
}

Result<void> Renderer::LoadVegetationAtlas(const std::string& path) {
    auto result = LoadTextureFromFile(path, true, AssetRegistry::TextureKind::Generic);
    if (!result.IsOk()) {
        return Result<void>::Err(result.Error());
    }
    m_vegetationState.atlas = result.Value();
    return Result<void>::Ok();
}

void Renderer::RenderVegetation(Scene::ECS_Registry* registry) {
    (void)registry;
    if (!m_vegetationState.enabled || m_frameLifecycle.deviceRemoved) {
        return;
    }

    // Update wind time
    m_vegetationState.wind.time += 0.016f;  // Approximate delta time

    // Render instanced vegetation meshes
    RenderVegetationMeshes();

    // Render billboards
    RenderVegetationBillboards();

    // Render grass cards
    RenderGrassCards();
}

void Renderer::RenderVegetationMeshes() {
    if (m_vegetationState.meshInstances.count == 0 || !m_vegetationState.meshPipeline) {
        return;
    }

    // TODO: When vegetation mesh pipeline is fully implemented:
    // 1. Set pipeline state
    // 2. Bind instance buffer as SRV
    // 3. Draw instanced for each prototype/LOD batch

    // For now, this is a placeholder that will be filled in when
    // the full vegetation mesh rendering is integrated
}

void Renderer::RenderVegetationBillboards() {
    if (m_vegetationState.billboardInstances.count == 0 || !m_vegetationState.billboardPipeline) {
        return;
    }

    // TODO: When billboard pipeline is fully implemented:
    // 1. Set billboard pipeline state
    // 2. Bind billboard instance buffer
    // 3. Bind vegetation atlas
    // 4. Draw using vertex shader expansion (4 vertices per billboard)
}

void Renderer::RenderGrassCards() {
    if (m_vegetationState.grassInstances.count == 0 || !m_vegetationState.grassCardPipeline) {
        return;
    }

    // TODO: When grass card pipeline is fully implemented:
    // 1. Set grass pipeline state
    // 2. Bind grass instance buffer
    // 3. Draw instanced grass cards with wind animation
}

void Renderer::RenderVegetationShadows(Scene::ECS_Registry* registry) {
    (void)registry;
    if (!m_vegetationState.enabled || m_frameLifecycle.deviceRemoved || !m_vegetationState.meshShadowPipeline) {
        return;
    }

    // Shadow pass for vegetation - similar to mesh shadow rendering
    // but with instanced vegetation data

    // TODO: Implement shadow rendering for vegetation when shadow
    // pipeline is fully integrated
}

// ============================================================================

} // namespace Cortex::Graphics

