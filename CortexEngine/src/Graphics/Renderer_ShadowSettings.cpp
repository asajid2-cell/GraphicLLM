#include "Renderer.h"

#include <algorithm>
#include <cmath>

#include <glm/gtx/norm.hpp>

namespace Cortex::Graphics {

bool Renderer::GetShadowsEnabled() const {
    return GetQualityState().shadowsEnabled;
}

float Renderer::GetShadowBias() const {
    return GetQualityState().shadowBias;
}

float Renderer::GetShadowPCFRadius() const {
    return GetQualityState().shadowPCFRadius;
}

float Renderer::GetCascadeSplitLambda() const {
    return GetQualityState().cascadeSplitLambda;
}

float Renderer::GetCascadeResolutionScale(uint32_t cascadeIndex) const {
    if (cascadeIndex == 0) {
        return GetQualityState().cascade0ResolutionScale;
    }
    return (cascadeIndex < kShadowCascadeCount) ? m_shadowResources.cascadeResolutionScale[cascadeIndex] : 1.0f;
}

void Renderer::ToggleShadows() {
    m_shadowResources.enabled = !m_shadowResources.enabled;
    spdlog::info("Shadows {}", m_shadowResources.enabled ? "ENABLED" : "DISABLED");
}

void Renderer::AdjustShadowBias(float delta) {
    m_shadowResources.bias = glm::clamp(m_shadowResources.bias + delta, 0.00001f, 0.01f);
    spdlog::info("Shadow bias set to {}", m_shadowResources.bias);
}

void Renderer::AdjustShadowPCFRadius(float delta) {
    m_shadowResources.pcfRadius = glm::clamp(m_shadowResources.pcfRadius + delta, 0.5f, 8.0f);
    spdlog::info("Shadow PCF radius set to {}", m_shadowResources.pcfRadius);
}

void Renderer::AdjustCascadeSplitLambda(float delta) {
    m_shadowResources.cascadeSplitLambda = glm::clamp(m_shadowResources.cascadeSplitLambda + delta, 0.0f, 1.0f);
    spdlog::info("Cascade split lambda set to {}", m_shadowResources.cascadeSplitLambda);
}

void Renderer::AdjustCascadeResolutionScale(uint32_t cascadeIndex, float delta) {
    if (cascadeIndex >= kShadowCascadeCount) {
        return;
    }
    if (std::abs(delta) < 1e-6f) {
        return;
    }
    m_shadowResources.cascadeResolutionScale[cascadeIndex] =
        glm::clamp(m_shadowResources.cascadeResolutionScale[cascadeIndex] + delta, 0.25f, 2.0f);
    spdlog::info("Cascade {} resolution scale set to {}",
                 cascadeIndex,
                 m_shadowResources.cascadeResolutionScale[cascadeIndex]);
}

void Renderer::SetShadowsEnabled(bool enabled) {
    if (m_shadowResources.enabled == enabled) {
        return;
    }
    m_shadowResources.enabled = enabled;
    spdlog::info("Renderer shadows {}", m_shadowResources.enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetShadowBias(float bias) {
    float clamped = glm::clamp(bias, 0.00001f, 0.01f);
    if (std::abs(clamped - m_shadowResources.bias) < 1e-9f) {
        return;
    }
    m_shadowResources.bias = clamped;
    spdlog::info("Renderer shadow bias set to {}", m_shadowResources.bias);
}

void Renderer::SetShadowPCFRadius(float radius) {
    float clamped = glm::clamp(radius, 0.5f, 8.0f);
    if (std::abs(clamped - m_shadowResources.pcfRadius) < 1e-6f) {
        return;
    }
    m_shadowResources.pcfRadius = clamped;
    spdlog::info("Renderer shadow PCF radius set to {}", m_shadowResources.pcfRadius);
}

void Renderer::SetCascadeSplitLambda(float lambda) {
    float clamped = glm::clamp(lambda, 0.0f, 1.0f);
    if (std::abs(clamped - m_shadowResources.cascadeSplitLambda) < 1e-6f) {
        return;
    }
    m_shadowResources.cascadeSplitLambda = clamped;
    spdlog::info("Renderer cascade split lambda set to {}", m_shadowResources.cascadeSplitLambda);
}

} // namespace Cortex::Graphics
