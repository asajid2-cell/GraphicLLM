#include "Graphics/RendererGeometryUtils.h"

#include <algorithm>
#include <cmath>

namespace Cortex::Graphics {

FrustumPlanes ExtractFrustumPlanesCPU(const glm::mat4& viewProj) {
    FrustumPlanes planes{};

    planes.planes[0] = glm::vec4(
        viewProj[0][3] + viewProj[0][0],
        viewProj[1][3] + viewProj[1][0],
        viewProj[2][3] + viewProj[2][0],
        viewProj[3][3] + viewProj[3][0]);

    planes.planes[1] = glm::vec4(
        viewProj[0][3] - viewProj[0][0],
        viewProj[1][3] - viewProj[1][0],
        viewProj[2][3] - viewProj[2][0],
        viewProj[3][3] - viewProj[3][0]);

    planes.planes[2] = glm::vec4(
        viewProj[0][3] + viewProj[0][1],
        viewProj[1][3] + viewProj[1][1],
        viewProj[2][3] + viewProj[2][1],
        viewProj[3][3] + viewProj[3][1]);

    planes.planes[3] = glm::vec4(
        viewProj[0][3] - viewProj[0][1],
        viewProj[1][3] - viewProj[1][1],
        viewProj[2][3] - viewProj[2][1],
        viewProj[3][3] - viewProj[3][1]);

    // D3D-style depth (LH_ZO): near plane is row2, far is row4-row2.
    planes.planes[4] = glm::vec4(
        viewProj[0][2],
        viewProj[1][2],
        viewProj[2][2],
        viewProj[3][2]);

    planes.planes[5] = glm::vec4(
        viewProj[0][3] - viewProj[0][2],
        viewProj[1][3] - viewProj[1][2],
        viewProj[2][3] - viewProj[2][2],
        viewProj[3][3] - viewProj[3][2]);

    for (int i = 0; i < 6; ++i) {
        float len = glm::length(glm::vec3(planes.planes[i]));
        if (len > 0.0001f) {
            planes.planes[i] /= len;
        }
    }

    return planes;
}

bool SphereIntersectsFrustumCPU(const FrustumPlanes& frustum, const glm::vec3& center, float radius) {
    for (int i = 0; i < 6; ++i) {
        const glm::vec4 p = frustum.planes[i];
        const float dist = glm::dot(glm::vec3(p), center) + p.w;
        if (dist < -radius) {
            return false;
        }
    }
    return true;
}

float GetMaxWorldScale(const glm::mat4& worldMatrix) {
    const glm::vec3 col0 = glm::vec3(worldMatrix[0]);
    const glm::vec3 col1 = glm::vec3(worldMatrix[1]);
    const glm::vec3 col2 = glm::vec3(worldMatrix[2]);
    return glm::max(glm::max(glm::length(col0), glm::length(col1)), glm::length(col2));
}

AutoDepthSeparation ComputeAutoDepthSeparationForThinSurfaces(
    const Scene::RenderableComponent& renderable,
    const glm::mat4& modelMatrix,
    uint32_t stableKey) {
    AutoDepthSeparation sep{};

    if (!renderable.mesh || !renderable.mesh->hasBounds) {
        return sep;
    }

    const glm::vec3 ext = glm::max(renderable.mesh->boundsMax - renderable.mesh->boundsMin, glm::vec3(0.0f));
    const float maxDim = std::max(ext.x, std::max(ext.y, ext.z));
    if (!(maxDim > 0.0f)) {
        return sep;
    }
    const float minDim = std::min(ext.x, std::min(ext.y, ext.z));

    constexpr float kThinAbs = 5e-4f;
    constexpr float kThinRel = 0.03f;
    if (minDim > glm::max(kThinAbs, maxDim * kThinRel)) {
        return sep;
    }

    int thinAxis = 0;
    if (ext.y <= ext.x && ext.y <= ext.z) {
        thinAxis = 1;
    } else if (ext.z <= ext.x && ext.z <= ext.y) {
        thinAxis = 2;
    }

    glm::vec3 axisWS = glm::vec3(modelMatrix[thinAxis]);
    const float axisLen2 = glm::dot(axisWS, axisWS);
    if (axisLen2 < 1e-8f) {
        return sep;
    }
    axisWS /= std::sqrt(axisLen2);

    // Separate along the actual thin axis in world space so vertical panels,
    // glass, water, and floor decals all get deterministic z-fight relief.
    const glm::vec3 boundsCenterWS =
        glm::vec3(modelMatrix * glm::vec4(renderable.mesh->boundsCenter, 1.0f));
    if (glm::dot(axisWS, boundsCenterWS) < 0.0f) {
        axisWS = -axisWS;
    }

    const float maxScale = GetMaxWorldScale(modelMatrix);
    const float worldMaxDim = maxDim * maxScale;

    constexpr float kBiasScale = 4e-4f;
    float eps = glm::clamp(worldMaxDim * kBiasScale, 1e-4f, 2e-2f);

    uint32_t h = stableKey * 2654435761u;
    uint32_t layer = (h >> 29u) & 7u;
    float layerScale = 1.0f + static_cast<float>(layer) * 0.10f;

    const bool forwardLayer =
        renderable.renderLayer == Scene::RenderableComponent::RenderLayer::Overlay ||
        renderable.alphaMode == Scene::RenderableComponent::AlphaMode::Blend;
    const float direction = forwardLayer ? 1.0f : -1.0f;
    sep.worldOffset = axisWS * (direction * eps * layerScale);

    if (renderable.renderLayer != Scene::RenderableComponent::RenderLayer::Overlay) {
        constexpr float kNdcBiasBase = 2.5e-5f;
        sep.depthBiasNdc = glm::clamp(kNdcBiasBase * layerScale, 0.0f, 5e-4f);
    }

    return sep;
}

void ApplyAutoDepthOffset(glm::mat4& modelMatrix, const glm::vec3& offset) {
    if (offset == glm::vec3(0.0f)) {
        return;
    }
    modelMatrix[3] += glm::vec4(offset, 0.0f);
}

} // namespace Cortex::Graphics
