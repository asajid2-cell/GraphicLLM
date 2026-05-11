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
    return (cascadeIndex < kShadowCascadeCount) ? m_shadowResources.controls.cascadeResolutionScale[cascadeIndex] : 1.0f;
}

void Renderer::ToggleShadows() {
    m_shadowResources.controls.enabled = !m_shadowResources.controls.enabled;
    spdlog::info("Shadows {}", m_shadowResources.controls.enabled ? "ENABLED" : "DISABLED");
}

void Renderer::AdjustShadowBias(float delta) {
    m_shadowResources.controls.bias = glm::clamp(m_shadowResources.controls.bias + delta, 0.00001f, 0.01f);
    spdlog::info("Shadow bias set to {}", m_shadowResources.controls.bias);
}

void Renderer::AdjustShadowPCFRadius(float delta) {
    m_shadowResources.controls.pcfRadius = glm::clamp(m_shadowResources.controls.pcfRadius + delta, 0.5f, 8.0f);
    spdlog::info("Shadow PCF radius set to {}", m_shadowResources.controls.pcfRadius);
}

void Renderer::AdjustCascadeSplitLambda(float delta) {
    m_shadowResources.controls.cascadeSplitLambda = glm::clamp(m_shadowResources.controls.cascadeSplitLambda + delta, 0.0f, 1.0f);
    spdlog::info("Cascade split lambda set to {}", m_shadowResources.controls.cascadeSplitLambda);
}

void Renderer::AdjustCascadeResolutionScale(uint32_t cascadeIndex, float delta) {
    if (cascadeIndex >= kShadowCascadeCount) {
        return;
    }
    if (std::abs(delta) < 1e-6f) {
        return;
    }
    m_shadowResources.controls.cascadeResolutionScale[cascadeIndex] =
        glm::clamp(m_shadowResources.controls.cascadeResolutionScale[cascadeIndex] + delta, 0.25f, 2.0f);
    spdlog::info("Cascade {} resolution scale set to {}",
                 cascadeIndex,
                 m_shadowResources.controls.cascadeResolutionScale[cascadeIndex]);
}

void Renderer::SetShadowsEnabled(bool enabled) {
    if (m_shadowResources.controls.enabled == enabled) {
        return;
    }
    m_shadowResources.controls.enabled = enabled;
    spdlog::info("Renderer shadows {}", m_shadowResources.controls.enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetShadowBias(float bias) {
    float clamped = glm::clamp(bias, 0.00001f, 0.01f);
    if (std::abs(clamped - m_shadowResources.controls.bias) < 1e-9f) {
        return;
    }
    m_shadowResources.controls.bias = clamped;
    spdlog::info("Renderer shadow bias set to {}", m_shadowResources.controls.bias);
}

void Renderer::SetShadowPCFRadius(float radius) {
    float clamped = glm::clamp(radius, 0.5f, 8.0f);
    if (std::abs(clamped - m_shadowResources.controls.pcfRadius) < 1e-6f) {
        return;
    }
    m_shadowResources.controls.pcfRadius = clamped;
    spdlog::info("Renderer shadow PCF radius set to {}", m_shadowResources.controls.pcfRadius);
}

void Renderer::SetCascadeSplitLambda(float lambda) {
    float clamped = glm::clamp(lambda, 0.0f, 1.0f);
    if (std::abs(clamped - m_shadowResources.controls.cascadeSplitLambda) < 1e-6f) {
        return;
    }
    m_shadowResources.controls.cascadeSplitLambda = clamped;
    spdlog::info("Renderer cascade split lambda set to {}", m_shadowResources.controls.cascadeSplitLambda);
}

} // namespace Cortex::Graphics
