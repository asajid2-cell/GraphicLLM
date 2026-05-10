#include "Renderer.h"

#include "Graphics/MaterialModel.h"
#include "Graphics/MaterialState.h"
#include "Graphics/RenderableClassification.h"
#include "Graphics/RendererGeometryUtils.h"
#include "Graphics/SurfaceClassification.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <spdlog/spdlog.h>
namespace Cortex::Graphics {

Renderer::VisibilityBufferDeferredLightingInputs
Renderer::PrepareVisibilityBufferDeferredLighting(Scene::ECS_Registry* registry) {
    VisibilityBufferDeferredLightingInputs inputs{};
    {
        auto lightView = registry->View<Scene::LightComponent, Scene::TransformComponent>();
        for (auto entity : lightView) {
            auto& lc = lightView.get<Scene::LightComponent>(entity);
            auto& tc = lightView.get<Scene::TransformComponent>(entity);
            if (lc.type == Scene::LightType::Directional) continue;

            Light light{};
            light.position_type = glm::vec4(tc.position, static_cast<float>(lc.type));
            glm::vec3 forward = tc.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
            light.direction_cosInner = glm::vec4(forward, std::cos(glm::radians(lc.innerConeDegrees)));
            light.color_range = glm::vec4(lc.color * lc.intensity, lc.range);
            float outerCos = std::cos(glm::radians(lc.outerConeDegrees));
            light.params = glm::vec4(outerCos, -1.0f, 0.0f, 0.0f);
            inputs.localLights.push_back(light);
        }
    }

    auto lightsResult = m_services.visibilityBuffer->UpdateLocalLights(m_commandResources.graphicsList.Get(), inputs.localLights);
    if (lightsResult.IsErr()) {
        spdlog::warn("VB local lights update failed: {}", lightsResult.Error());
    }

    auto& deferredParams = inputs.params;
    deferredParams.invViewProj = glm::inverse(m_constantBuffers.frameCPU.viewProjectionMatrix);
    deferredParams.viewMatrix = m_constantBuffers.frameCPU.viewMatrix;
    for (int i = 0; i < 6; ++i) {
        deferredParams.lightViewProjection[i] = m_constantBuffers.frameCPU.lightViewProjection[i];
    }
    deferredParams.cameraPosition = m_constantBuffers.frameCPU.cameraPosition;
    deferredParams.sunDirection = glm::vec4(m_lightingState.directionalDirection, 0.0f);
    deferredParams.sunRadiance =
        glm::vec4(m_lightingState.directionalColor * m_lightingState.directionalIntensity, 0.0f);
    deferredParams.cascadeSplits = m_constantBuffers.frameCPU.cascadeSplits;
    deferredParams.shadowParams = glm::vec4(
        m_shadowResources.bias,
        m_shadowResources.pcfRadius,
        m_shadowResources.enabled ? 1.0f : 0.0f,
        m_shadowResources.pcssEnabled ? 1.0f : 0.0f);
    deferredParams.envParams = glm::vec4(
        m_environmentState.diffuseIntensity, m_environmentState.specularIntensity, m_environmentState.enabled ? 1.0f : 0.0f, 0.0f);
    float invShadowDim = 1.0f / static_cast<float>(m_shadowResources.mapSize);
    deferredParams.shadowInvSizeAndSpecMaxMip = glm::vec4(invShadowDim, invShadowDim, 8.0f, 0.0f);
    float nearZ = 0.1f, farZ = 1000.0f;
    deferredParams.projectionParams = glm::vec4(
        m_constantBuffers.frameCPU.projectionMatrix[0][0], m_constantBuffers.frameCPU.projectionMatrix[1][1], nearZ, farZ);
    uint32_t screenW = m_services.window ? GetInternalRenderWidth() : 1280;
    uint32_t screenH = m_services.window ? GetInternalRenderHeight() : 720;
    deferredParams.screenAndCluster = glm::uvec4(screenW, screenH, 16, 9);
    deferredParams.clusterParams = glm::uvec4(24, 128, static_cast<uint32_t>(inputs.localLights.size()), 0);
    deferredParams.reflectionProbeParams = glm::uvec4(0, 0, m_debugViewState.mode, 0);

    if (!m_environmentState.maps.empty() && m_environmentState.currentIndex < m_environmentState.maps.size()) {
        auto& env = m_environmentState.maps[m_environmentState.currentIndex];
        if (env.diffuseIrradiance) {
            inputs.envDiffuseResource = env.diffuseIrradiance->GetResource();
            inputs.envFormat = env.diffuseIrradiance->GetFormat();
        }
        if (env.specularPrefiltered) {
            inputs.envSpecularResource = env.specularPrefiltered->GetResource();
        }
    }
    if (!inputs.envDiffuseResource && m_materialFallbacks.albedo) {
        inputs.envDiffuseResource = m_materialFallbacks.albedo->GetResource();
    }
    if (!inputs.envSpecularResource && m_materialFallbacks.albedo) {
        inputs.envSpecularResource = m_materialFallbacks.albedo->GetResource();
    }

    return inputs;
}

void Renderer::ApplyVisibilityBufferDeferredLighting(const VisibilityBufferDeferredLightingInputs& inputs) {
    auto lightingResult = m_services.visibilityBuffer->ApplyDeferredLighting(
        m_commandResources.graphicsList.Get(),
        m_mainTargets.hdrColor.Get(),
        m_mainTargets.hdrRTV.cpu,
        m_depthResources.buffer.Get(),
        m_depthResources.srv,
        inputs.envDiffuseResource,
        inputs.envSpecularResource,
        inputs.envFormat,
        m_shadowResources.srv,
        inputs.params);
    if (lightingResult.IsErr()) {
        spdlog::warn("VB deferred lighting failed: {}", lightingResult.Error());
    }

    m_visibilityBufferState.renderedThisFrame = true;
}

void Renderer::RenderVisibilityBufferDeferredLightingStage(Scene::ECS_Registry* registry) {
    const auto inputs = PrepareVisibilityBufferDeferredLighting(registry);
    ApplyVisibilityBufferDeferredLighting(inputs);
}

} // namespace Cortex::Graphics
