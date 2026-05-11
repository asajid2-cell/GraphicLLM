#include "Renderer.h"

#include "Graphics/RenderableClassification.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <limits>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>
#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

float Halton(uint32_t index, uint32_t base) {
    float result = 0.0f;
    float f = 1.0f / static_cast<float>(base);
    uint32_t i = index;
    while (i > 0) {
        result += f * static_cast<float>(i % base);
        i /= base;
        f /= static_cast<float>(base);
    }
    return result;
}

} // namespace
void Renderer::UpdateFrameConstants(float deltaTime, Scene::ECS_Registry* registry) {
    FrameConstants frameData = {};
    glm::vec3 cameraPos(0.0f);
    glm::vec3 cameraForward(0.0f, 0.0f, 1.0f);
    float camNear = 0.1f;
    float camFar = 1000.0f;
    float fovY = glm::radians(60.0f);

    // Reset per-frame local light shadow state; will be populated below if we
    // find suitable shadow-casting spotlights. We keep the budget-warning
    // flag sticky so we do not spam logs every frame.
    m_localShadowState.ResetFrame();

    // Find active camera
    auto cameraView = registry->View<Scene::CameraComponent, Scene::TransformComponent>();
    bool foundCamera = false;

    for (auto entity : cameraView) {
        auto& camera = cameraView.get<Scene::CameraComponent>(entity);
        auto& transform = cameraView.get<Scene::TransformComponent>(entity);

        if (camera.isActive) {
            // Respect camera orientation from its transform
            frameData.viewMatrix = camera.GetViewMatrix(transform);
            frameData.projectionMatrix = camera.GetProjectionMatrix(m_services.window->GetAspectRatio());
            cameraPos = transform.position;
            cameraForward = glm::normalize(transform.rotation * glm::vec3(0.0f, 0.0f, 1.0f));
            frameData.cameraPosition = glm::vec4(cameraPos, 1.0f);
            camNear = camera.nearPlane;
            camFar = camera.farPlane;
            fovY = glm::radians(camera.fov);
            foundCamera = true;
            // Active camera found; skip per-frame debug spam to keep logs clean
            break;
        }
    }

    // Default camera if none found
    if (!foundCamera) {
        spdlog::warn("No active camera found, using default");
        cameraPos = glm::vec3(0.0f, 2.0f, 5.0f);
        glm::vec3 target(0.0f, 0.0f, 0.0f);
        glm::vec3 up(0.0f, 1.0f, 0.0f);

        frameData.viewMatrix = glm::lookAtLH(cameraPos, target, up);
        frameData.projectionMatrix = glm::perspectiveLH_ZO(
            fovY,
            m_services.window->GetAspectRatio(),
            camNear,
            camFar
        );
        cameraForward = glm::normalize(target - cameraPos);
        frameData.cameraPosition = glm::vec4(cameraPos, 1.0f);
    }

    // Cache camera parameters for culling and RT use.
    m_cameraState.positionWS = cameraPos;
    m_cameraState.forwardWS  = cameraForward;
    m_cameraState.nearPlane  = camNear;
    m_cameraState.farPlane   = camFar;
    if (m_services.rayTracingContext) {
        m_services.rayTracingContext->SetCameraParams(cameraPos, cameraForward, camNear, camFar);
    }

    // Detect camera movement BEFORE jitter decision to avoid 1-frame lag.
    // This ensures m_temporalAAState.cameraIsMoving reflects the CURRENT frame's state when
    // we decide whether to apply TAA jitter, preventing flickering at standstill.
    if (m_cameraState.hasPrevious) {
        float posDelta = glm::length(cameraPos - m_cameraState.prevPositionWS);
        float fwdDot = glm::clamp(
            glm::dot(glm::normalize(cameraForward), glm::normalize(m_cameraState.prevForwardWS)),
            -1.0f, 1.0f);
        float angleDelta = std::acos(fwdDot);
        const float softPosThreshold   = 0.1f;
        const float softAngleThreshold = glm::radians(3.0f);
        m_temporalAAState.cameraIsMoving = (posDelta > softPosThreshold || angleDelta > softAngleThreshold);
    } else {
        m_temporalAAState.cameraIsMoving = true;
    }

    // Temporal AA jitter (in pixels) and corresponding UV delta for history
    // sampling. When an internal supersampling scale is active, base these
    // values on the HDR render target size rather than the window size so
    // jitter and post-process texel steps line up with the actual buffers.
    float internalWidth  = static_cast<float>(m_services.window->GetWidth());
    float internalHeight = static_cast<float>(m_services.window->GetHeight());
    if (m_mainTargets.hdr.resources.color) {
        D3D12_RESOURCE_DESC hdrDesc = m_mainTargets.hdr.resources.color->GetDesc();
        internalWidth  = static_cast<float>(hdrDesc.Width);
        internalHeight = static_cast<float>(hdrDesc.Height);
    }
    float invWidth  = 1.0f / std::max(1.0f, internalWidth);
    float invHeight = 1.0f / std::max(1.0f, internalHeight);

    glm::vec2 jitterPixels(0.0f);
    if (m_temporalAAState.enabled) {
        static bool s_checkedForceNoJitter = false;
        static bool s_forceNoJitter = false;
        if (!s_checkedForceNoJitter) {
            s_checkedForceNoJitter = true;
            if (std::getenv("CORTEX_TAA_FORCE_NO_JITTER")) {
                s_forceNoJitter = true;
                spdlog::warn("Renderer: CORTEX_TAA_FORCE_NO_JITTER set; disabling TAA jitter for debugging");
            }
        }

        m_temporalAAState.jitterPrevPixels = m_temporalAAState.jitterCurrPixels;
        float jx = 0.0f;
        float jy = 0.0f;
        if (!s_forceNoJitter) {
            jx = Halton(m_temporalAAState.sampleIndex + 1, 2) - 0.5f;
            jy = Halton(m_temporalAAState.sampleIndex + 1, 3) - 0.5f;
            m_temporalAAState.sampleIndex++;
        }
        // Scale jitter so per-frame shifts are small and objects remain
        // stable while still providing enough subpixel coverage for TAA.
        float jitterScale = 0.15f;
        if (s_forceNoJitter) {
            jitterScale = 0.0f;
        }
        if (!m_temporalAAState.cameraIsMoving) {
            // When the camera is effectively stationary, disable jitter so
            // the image converges to a sharp, stable result without
            // "double-exposed" edges.
            jitterScale = 0.0f;
        }
        jitterPixels = glm::vec2(jx, jy) * jitterScale;
        m_temporalAAState.jitterCurrPixels = jitterPixels;
    } else {
        m_temporalAAState.jitterPrevPixels = glm::vec2(0.0f);
        m_temporalAAState.jitterCurrPixels = glm::vec2(0.0f);
    }

    // Compute a non-jittered view-projection matrix for RT reconstruction and
    // motion vector generation before applying TAA offsets. This keeps RT
    // rays and motion vectors stable while the raster path still benefits
    // from jitter.
    glm::mat4 vpNoJitter = frameData.projectionMatrix * frameData.viewMatrix;
    frameData.viewProjectionNoJitter = vpNoJitter;
    frameData.invViewProjectionNoJitter = glm::inverse(vpNoJitter);

    // Apply jitter to projection (NDC space).
    if (m_temporalAAState.enabled) {
        float jitterNdcX = (2.0f * jitterPixels.x) * invWidth;
        float jitterNdcY = (2.0f * jitterPixels.y) * invHeight;
        // Offset projection center; DirectX-style clip space uses [x,y] in row 2, column 0/1.
        frameData.projectionMatrix[2][0] += jitterNdcX;
        frameData.projectionMatrix[2][1] += jitterNdcY;
    }

    // Final view-projection with jitter applied.
    frameData.viewProjectionMatrix = frameData.projectionMatrix * frameData.viewMatrix;

    // Precompute inverse projection for SSAO and other screen-space effects.
    frameData.invProjectionMatrix = glm::inverse(frameData.projectionMatrix);

    const FrameConstantCameraState cameraState{
        cameraPos,
        cameraForward,
        camNear,
        camFar,
        fovY,
        invWidth,
        invHeight,
        vpNoJitter
    };

    PopulateFrameLightingAndShadows(frameData, deltaTime, registry, cameraState);
    PopulateFrameDebugAndPostConstants(frameData, registry, cameraState);
    PublishFrameConstants(frameData, cameraState);
}

} // namespace Cortex::Graphics

