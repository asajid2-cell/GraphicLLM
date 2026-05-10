#include "Renderer.h"

#include <algorithm>
#include <cmath>

#include <glm/gtx/norm.hpp>

namespace Cortex::Graphics {

void Renderer::PublishFrameConstants(FrameConstants& frameData,
                                     const FrameConstantCameraState& cameraState) {
    const glm::mat4& vpNoJitter = cameraState.viewProjectionNoJitter;
    const glm::vec3& cameraPos = cameraState.cameraPosition;
    const glm::vec3& cameraForward = cameraState.cameraForward;
    // Previous and inverse view-projection matrices for TAA reprojection and
    // motion vectors. We store the *non-jittered* view-projection from the
    // previous frame so that motion vectors do not encode TAA jitter; jitter
    // is handled separately via g_TAAParams.xy in the post-process.
    if (m_temporalAAState.hasPrevViewProj) {
        frameData.prevViewProjectionMatrix = m_temporalAAState.prevViewProjMatrix;
    } else {
        frameData.prevViewProjectionMatrix = vpNoJitter;
    }

    frameData.invViewProjectionMatrix = glm::inverse(frameData.viewProjectionMatrix);

    // Update history for next frame (non-jittered)
    m_temporalAAState.prevViewProjMatrix = vpNoJitter;
    m_temporalAAState.hasPrevViewProj = true;

    // Reset RT temporal history when the camera moves significantly to
    // avoid smearing old GI/shadow data across new viewpoints.
    // NOTE: m_temporalAAState.cameraIsMoving is now computed earlier (before jitter decision)
    // to avoid 1-frame lag that caused flickering at standstill.
    if (m_cameraState.hasPrevious) {
        float posDelta = glm::length(cameraPos - m_cameraState.prevPositionWS);
        float fwdDot = glm::clamp(
            glm::dot(glm::normalize(cameraForward), glm::normalize(m_cameraState.prevForwardWS)),
            -1.0f, 1.0f);
        float angleDelta = std::acos(fwdDot);

        // Hard thresholds for RT history invalidation. These should only fire
        // during significant camera jumps (teleports, cut scenes) to avoid
        // constantly resetting temporal accumulation during normal navigation.
        const float posThreshold   = 5.0f;
        const float angleThreshold = glm::radians(45.0f);

        if (posDelta > posThreshold || angleDelta > angleThreshold) {
            InvalidateRTShadowHistory("camera_cut");
            InvalidateRTGIHistory("camera_cut");
            InvalidateRTReflectionHistory("camera_cut");
        }
    }
    m_cameraState.prevPositionWS = cameraPos;
    m_cameraState.prevForwardWS = cameraForward;
    m_cameraState.hasPrevious = true;

    m_constantBuffers.frameCPU = frameData;
    // Use frame-indexed slot to ensure each frame writes to its own slot
    // This prevents race conditions where GPU reads frame N while CPU writes frame N+1
    m_constantBuffers.currentFrameGPU = m_constantBuffers.frame.WriteToSlot(m_constantBuffers.frameCPU, m_frameRuntime.frameIndex);
}

} // namespace Cortex::Graphics
