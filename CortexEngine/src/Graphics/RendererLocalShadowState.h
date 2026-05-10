#pragma once

#include <array>
#include <cstdint>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

namespace Cortex::Graphics {

template <size_t MaxLocalLights>
struct RendererLocalShadowState {
    bool hasShadow = false;
    uint32_t count = 0;
    bool budgetWarningEmitted = false;
    std::array<glm::mat4, MaxLocalLights> lightViewProjMatrices{};
    std::array<entt::entity, MaxLocalLights> entities{};

    void ResetFrame() {
        hasShadow = false;
        count = 0;
        entities.fill(entt::null);
    }
};

} // namespace Cortex::Graphics
