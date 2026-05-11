#include "Renderer.h"

#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

void Renderer::PopulateFrameLightingAndShadows(FrameConstants& frameData,
                                               float deltaTime,
                                               Scene::ECS_Registry* registry,
                                               const FrameConstantCameraState& cameraState) {
    const glm::vec3& cameraPos = cameraState.cameraPosition;
    const glm::vec3& cameraForward = cameraState.cameraForward;
    const float camNear = cameraState.nearPlane;
    const float camFar = cameraState.farPlane;
    const float fovY = cameraState.fovY;
    // Time/exposure and lighting state (w = bloom intensity, disabled if bloom SRV missing)
    float bloom = (m_bloomResources.resources.combinedSrv.IsValid() ? m_bloomResources.controls.intensity : 0.0f);
    frameData.timeAndExposure = glm::vec4(m_frameRuntime.totalTime, deltaTime, m_qualityRuntimeState.exposure, bloom);

    glm::vec3 ambient = m_lightingState.ambientColor * m_lightingState.ambientIntensity;
    frameData.ambientColor = glm::vec4(ambient, m_environmentState.backgroundBlur);

    // Fill forward light array (light 0 = directional sun)
    glm::vec3 dirToLight = glm::normalize(m_lightingState.directionalDirection);
    glm::vec3 sunColor = m_lightingState.directionalColor * m_lightingState.directionalIntensity;

    uint32_t lightCount = 0;

    // Track up to kMaxShadowedLocalLights shadow-casting spotlights. Each one
    // gets its own slice in the shared shadow-map atlas and a matching entry
    // in the lightViewProjection array for shading.
    glm::vec3 localLightPos[kMaxShadowedLocalLights]{};
    glm::vec3 localLightDir[kMaxShadowedLocalLights]{};
    float     localLightRange[kMaxShadowedLocalLights]{};
    float     localOuterDegrees[kMaxShadowedLocalLights]{};

    // Light 0: directional sun (unshadowed here; shadows are handled via cascades)
    frameData.lightCount = glm::uvec4(0u);
    frameData.lights[0].position_type = glm::vec4(0.0f, 0.0f, 0.0f, 0.0f); // type 0 = directional
    frameData.lights[0].direction_cosInner = glm::vec4(dirToLight, 0.0f);
    frameData.lights[0].color_range = glm::vec4(sunColor, 0.0f);
    frameData.lights[0].params = glm::vec4(0.0f);
    lightCount = 1;

    // Populate additional lights from LightComponent (point/spot). We support
    // up to kMaxForwardLights-1 additional lights beyond the sun.
    auto lightView = registry->View<Scene::LightComponent, Scene::TransformComponent>();
    for (auto entity : lightView) {
        if (lightCount >= kMaxForwardLights) {
            break;
        }
        auto& lightComp = lightView.get<Scene::LightComponent>(entity);
        auto& lightXform = lightView.get<Scene::TransformComponent>(entity);

        auto type = lightComp.type;
        if (type == Scene::LightType::Directional) {
            // Directional lights are handled by the global sun for now
            continue;
        }

        glm::vec3 color = glm::max(lightComp.color, glm::vec3(0.0f));
        float intensity = std::max(lightComp.intensity, 0.0f);
        glm::vec3 radiance = color * intensity;

        Light& outLight = frameData.lights[lightCount];
        float gpuType = 1.0f;
        if (type == Scene::LightType::Point) {
            gpuType = 1.0f;
        } else if (type == Scene::LightType::Spot) {
            gpuType = 2.0f;
        } else if (type == Scene::LightType::AreaRect) {
            gpuType = 3.0f;
        }
        outLight.position_type = glm::vec4(lightXform.position, gpuType);

        glm::vec3 forwardLS = lightXform.rotation * glm::vec3(0.0f, 0.0f, 1.0f);
        glm::vec3 dir = glm::normalize(forwardLS);
        float innerRad = glm::radians(lightComp.innerConeDegrees);
        float outerRad = glm::radians(lightComp.outerConeDegrees);
        float cosInner = std::cos(innerRad);
        float cosOuter = std::cos(outerRad);

        outLight.direction_cosInner = glm::vec4(dir, cosInner);
        outLight.color_range = glm::vec4(radiance, lightComp.range);

        // Default to "no local shadow" for this light. We reserve params.y as
        // a shadow-map slice index when using local light shadows.
        float shadowIndex = -1.0f;

        if (m_shadowResources.enabled &&
            lightComp.castsShadows &&
            type == Scene::LightType::Spot)
        {
            if (m_localShadowState.count < kMaxShadowedLocalLights) {
                uint32_t localIndex = m_localShadowState.count;
                uint32_t slice = kShadowCascadeCount + localIndex;

                shadowIndex = static_cast<float>(slice);
                m_localShadowState.entities[localIndex] = entity;
                localLightPos[localIndex] = lightXform.position;
                localLightDir[localIndex] = dir;
                localLightRange[localIndex] = lightComp.range;
                localOuterDegrees[localIndex] = lightComp.outerConeDegrees;

                ++m_localShadowState.count;
            } else if (!m_localShadowState.budgetWarningEmitted) {
                std::string nameUtf8 = "<unnamed>";
                if (registry && registry->HasComponent<Scene::TagComponent>(entity)) {
                    const auto& tag = registry->GetComponent<Scene::TagComponent>(entity).tag;
                    if (!tag.empty()) {
                        nameUtf8 = tag;
                    }
                }
                spdlog::warn(
                    "Local shadow budget exceeded ({} lights); '{}' will render without local shadows. "
                    "Consider disabling 'castsShadows' on some lights or enabling safe lighting rigs.",
                    m_localShadowState.count,
                    nameUtf8);
                m_localShadowState.budgetWarningEmitted = true;
            }
        }

        // For rect area lights we encode the half-size in params.zw so that
        // the shader can approximate their footprint. Other light types
        // leave these components at zero.
        glm::vec2 areaHalfSize(0.0f);
        if (type == Scene::LightType::AreaRect) {
            areaHalfSize =
                0.5f * glm::max(lightComp.areaSize, glm::vec2(0.0f)) * m_lightingState.areaLightSizeScale;
        }

        outLight.params = glm::vec4(cosOuter, shadowIndex, areaHalfSize.x, areaHalfSize.y);

        ++lightCount;
    }

    // Zero any remaining lights
    for (uint32_t i = lightCount; i < kMaxForwardLights; ++i) {
        frameData.lights[i].position_type = glm::vec4(0.0f);
        frameData.lights[i].direction_cosInner = glm::vec4(0.0f);
        frameData.lights[i].color_range = glm::vec4(0.0f);
        frameData.lights[i].params = glm::vec4(0.0f);
    }

    frameData.lightCount = glm::uvec4(lightCount, 0u, 0u, 0u);

    // Camera-followed light view for cascades
    glm::vec3 sceneCenter = cameraPos + cameraForward * ((camNear + camFar) * 0.5f);
    glm::vec3 lightDirFromLightToScene = -dirToLight;
    float lightDistance = camFar;
    glm::vec3 lightPos = sceneCenter - lightDirFromLightToScene * lightDistance;

    glm::vec3 lightUp(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(lightUp, lightDirFromLightToScene)) > 0.99f) {
        lightUp = glm::vec3(0.0f, 0.0f, 1.0f);
    }

    m_shadowCascadeState.lightViewMatrix = glm::lookAtLH(lightPos, sceneCenter, lightUp);

    // Compute cascade splits (practical split scheme)
    const uint32_t cascadeCount = kShadowCascadeCount;
    float splits[kShadowCascadeCount] = {};
    for (uint32_t i = 0; i < cascadeCount; ++i) {
        float si = static_cast<float>(i + 1) / static_cast<float>(cascadeCount);
        float logSplit = camNear * std::pow(camFar / camNear, si);
        float linSplit = camNear + (camFar - camNear) * si;
        splits[i] = m_shadowResources.cascadeSplitLambda * logSplit +
                    (1.0f - m_shadowResources.cascadeSplitLambda) * linSplit;
        m_shadowCascadeState.cascadeSplits[i] = splits[i];
    }

    frameData.cascadeSplits = glm::vec4(
        splits[0],
        splits[1],
        splits[2],
        camFar
    );

    // Build per-cascade light view-projection matrices
    const float aspect = m_services.window->GetAspectRatio();
    const float tanHalfFovY = std::tan(fovY * 0.5f);
    const float tanHalfFovX = tanHalfFovY * aspect;
    glm::mat4 invView = glm::inverse(frameData.viewMatrix);

    for (uint32_t cascadeIndex = 0; cascadeIndex < cascadeCount; ++cascadeIndex) {
        float cascadeNear = (cascadeIndex == 0) ? camNear : splits[cascadeIndex - 1];
        float cascadeFar = splits[cascadeIndex];

        float xn = cascadeNear * tanHalfFovX;
        float yn = cascadeNear * tanHalfFovY;
        float xf = cascadeFar * tanHalfFovX;
        float yf = cascadeFar * tanHalfFovY;

        glm::vec3 frustumCornersVS[8] = {
            { -xn,  yn, cascadeNear },
            {  xn,  yn, cascadeNear },
            {  xn, -yn, cascadeNear },
            { -xn, -yn, cascadeNear },
            { -xf,  yf, cascadeFar },
            {  xf,  yf, cascadeFar },
            {  xf, -yf, cascadeFar },
            { -xf, -yf, cascadeFar }
        };

        glm::vec3 minLS( std::numeric_limits<float>::max());
        glm::vec3 maxLS(-std::numeric_limits<float>::max());

        for (auto& cornerVS : frustumCornersVS) {
            glm::vec4 world = invView * glm::vec4(cornerVS, 1.0f);
            glm::vec3 ls = glm::vec3(m_shadowCascadeState.lightViewMatrix * world);
            minLS = glm::min(minLS, ls);
            maxLS = glm::max(maxLS, ls);
        }

        glm::vec3 extent = (maxLS - minLS) * 0.5f;
        glm::vec3 centerLS = minLS + extent;

        // Slightly expand the light-space extents so large objects near the
        // camera frustum edges stay inside the shadow map, reducing edge flicker.
        extent.x *= 1.1f;
        extent.y *= 1.1f;

        // Texel snapping to reduce shimmering (per-cascade resolution scaling)
        float effectiveResX = m_shadowResources.mapSize * m_shadowResources.cascadeResolutionScale[cascadeIndex];
        float effectiveResY = m_shadowResources.mapSize * m_shadowResources.cascadeResolutionScale[cascadeIndex];
        float texelSizeX = (extent.x * 2.0f) / std::max(effectiveResX, 1.0f);
        float texelSizeY = (extent.y * 2.0f) / std::max(effectiveResY, 1.0f);
        if (texelSizeX > 0.0f) {
            centerLS.x = std::floor(centerLS.x / texelSizeX) * texelSizeX;
        }
        if (texelSizeY > 0.0f) {
            centerLS.y = std::floor(centerLS.y / texelSizeY) * texelSizeY;
        }

        float minX = centerLS.x - extent.x;
        float maxX = centerLS.x + extent.x;
        float minY = centerLS.y - extent.y;
        float maxY = centerLS.y + extent.y;

        float minZ = minLS.z;
        float maxZ = maxLS.z;
        float nearPlane = std::max(0.0f, minZ);
        float farPlane = maxZ;

        m_shadowCascadeState.lightProjectionMatrices[cascadeIndex] = glm::orthoLH_ZO(minX, maxX, minY, maxY, nearPlane, farPlane);
        m_shadowCascadeState.lightViewProjectionMatrices[cascadeIndex] = m_shadowCascadeState.lightProjectionMatrices[cascadeIndex] * m_shadowCascadeState.lightViewMatrix;
        frameData.lightViewProjection[cascadeIndex] = m_shadowCascadeState.lightViewProjectionMatrices[cascadeIndex];
    }

    // Build spot-light shadow view-projection matrices for any selected local
    // lights and store them in the shared lightViewProjection array starting
    // at index kShadowCascadeCount.
    if (m_localShadowState.count > 0)
    {
        m_localShadowState.hasShadow = true;

        for (uint32_t i = 0; i < m_localShadowState.count; ++i)
        {
            if (localLightRange[i] <= 0.0f)
            {
                continue;
            }

            glm::vec3 dir = glm::normalize(localLightDir[i]);
            if (!std::isfinite(dir.x) || !std::isfinite(dir.y) || !std::isfinite(dir.z) ||
                glm::length2(dir) < 1e-6f)
            {
                dir = glm::vec3(0.0f, -1.0f, 0.0f);
            }

            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(up, dir)) > 0.99f) {
                up = glm::vec3(0.0f, 0.0f, 1.0f);
            }

            glm::mat4 spotLightView = glm::lookAtLH(localLightPos[i], localLightPos[i] + dir, up);

            float nearPlane = 0.1f;
            float farPlane = std::max(localLightRange[i], 1.0f);

            // Treat the outer cone angle as a half-FOV for the spotlight.
            float outerRad = glm::radians(localOuterDegrees[i]);
            float fovYLocal = outerRad * 2.0f;
            fovYLocal = glm::clamp(fovYLocal, glm::radians(10.0f), glm::radians(170.0f));

            glm::mat4 lightProj = glm::perspectiveLH_ZO(fovYLocal, 1.0f, nearPlane, farPlane);
            glm::mat4 lightViewProj = lightProj * spotLightView;

            m_localShadowState.lightViewProjMatrices[i] = lightViewProj;

            uint32_t slice = kShadowCascadeCount + i;
            if (slice < kShadowArraySize)
            {
                frameData.lightViewProjection[slice] = lightViewProj;
            }
        }

        // Clear out any unused local shadow slots in the constant buffer.
        for (uint32_t i = m_localShadowState.count; i < kMaxShadowedLocalLights; ++i)
        {
            uint32_t slice = kShadowCascadeCount + i;
            if (slice < kShadowArraySize)
            {
                frameData.lightViewProjection[slice] = glm::mat4(1.0f);
            }
        }
    }
    else
    {
        m_localShadowState.hasShadow = false;
        for (uint32_t i = 0; i < kMaxShadowedLocalLights; ++i)
        {
            uint32_t slice = kShadowCascadeCount + i;
            if (slice < kShadowArraySize)
            {
                frameData.lightViewProjection[slice] = glm::mat4(1.0f);
            }
        }
    }

    frameData.shadowParams = glm::vec4(
        m_shadowResources.bias,
        m_shadowResources.pcfRadius,
        m_shadowResources.enabled ? 1.0f : 0.0f,
        m_shadowResources.pcssEnabled ? 1.0f : 0.0f);

}

} // namespace Cortex::Graphics
