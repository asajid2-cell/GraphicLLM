#pragma once

#include "Scene/Components.h"

#include <string_view>
#include <vector>

namespace Cortex::Scene {

struct ParticleEffectDescriptor {
    std::string_view id;
    std::string_view displayName;
    std::string_view fallbackTexture;
    ParticleEmitterComponent emitter;
};

[[nodiscard]] const std::vector<ParticleEffectDescriptor>& GetParticleEffectDescriptors();
[[nodiscard]] const ParticleEffectDescriptor* FindParticleEffectDescriptor(std::string_view id);
[[nodiscard]] bool ApplyParticleEffectDescriptor(std::string_view id, ParticleEmitterComponent& emitter);

} // namespace Cortex::Scene
