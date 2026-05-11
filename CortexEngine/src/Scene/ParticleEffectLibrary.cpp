#include "Scene/ParticleEffectLibrary.h"

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace Cortex::Scene {

namespace {

ParticleEmitterComponent MakeEmitter(ParticleEmitterType type,
                                     float rate,
                                     float lifetime,
                                     const glm::vec3& initialVelocity,
                                     const glm::vec3& velocityRandom,
                                     float sizeStart,
                                     float sizeEnd,
                                     const glm::vec4& colorStart,
                                     const glm::vec4& colorEnd,
                                     float gravity) {
    ParticleEmitterComponent emitter;
    emitter.type = type;
    emitter.rate = rate;
    emitter.lifetime = lifetime;
    emitter.initialVelocity = initialVelocity;
    emitter.velocityRandom = velocityRandom;
    emitter.sizeStart = sizeStart;
    emitter.sizeEnd = sizeEnd;
    emitter.colorStart = colorStart;
    emitter.colorEnd = colorEnd;
    emitter.gravity = gravity;
    emitter.localSpace = true;
    return emitter;
}

} // namespace

const std::vector<ParticleEffectDescriptor>& GetParticleEffectDescriptors() {
    static const std::vector<ParticleEffectDescriptor> descriptors = {
        {
            "fire",
            "Fire",
            "procedural_billboard",
            MakeEmitter(ParticleEmitterType::Fire,
                        95.0f,
                        0.85f,
                        glm::vec3(0.2f, 2.8f, 0.75f),
                        glm::vec3(0.65f, 0.9f, 0.65f),
                        0.055f,
                        0.34f,
                        glm::vec4(3.3f, 1.35f, 0.35f, 0.9f),
                        glm::vec4(0.75f, 0.04f, 0.0f, 0.0f),
                        0.7f)
        },
        {
            "smoke",
            "Smoke",
            "procedural_billboard",
            MakeEmitter(ParticleEmitterType::Smoke,
                        48.0f,
                        4.4f,
                        glm::vec3(0.0f, 0.22f, 0.0f),
                        glm::vec3(0.18f, 0.28f, 0.18f),
                        0.04f,
                        0.17f,
                        glm::vec4(0.65f, 0.88f, 1.0f, 0.28f),
                        glm::vec4(0.35f, 0.55f, 1.0f, 0.0f),
                        -0.08f)
        },
        {
            "dust",
            "Dust Motes",
            "procedural_billboard",
            MakeEmitter(ParticleEmitterType::Dust,
                        36.0f,
                        5.2f,
                        glm::vec3(0.0f, 0.18f, 0.02f),
                        glm::vec3(0.24f, 0.18f, 0.24f),
                        0.035f,
                        0.11f,
                        glm::vec4(0.9f, 0.82f, 0.62f, 0.22f),
                        glm::vec4(0.75f, 0.68f, 0.46f, 0.0f),
                        -0.04f)
        },
        {
            "sparks",
            "Sparks",
            "procedural_billboard",
            MakeEmitter(ParticleEmitterType::Sparks,
                        28.0f,
                        0.55f,
                        glm::vec3(0.3f, 2.7f, 0.25f),
                        glm::vec3(1.0f, 1.2f, 1.0f),
                        0.025f,
                        0.055f,
                        glm::vec4(4.0f, 2.2f, 0.55f, 1.0f),
                        glm::vec4(1.2f, 0.22f, 0.02f, 0.0f),
                        -1.2f)
        },
        {
            "embers",
            "Embers",
            "procedural_billboard",
            MakeEmitter(ParticleEmitterType::Embers,
                        34.0f,
                        2.6f,
                        glm::vec3(0.0f, 0.75f, 0.1f),
                        glm::vec3(0.45f, 0.45f, 0.45f),
                        0.035f,
                        0.10f,
                        glm::vec4(2.4f, 0.74f, 0.18f, 0.75f),
                        glm::vec4(0.65f, 0.08f, 0.02f, 0.0f),
                        0.05f)
        },
        {
            "mist",
            "Mist",
            "procedural_billboard",
            MakeEmitter(ParticleEmitterType::Mist,
                        32.0f,
                        4.8f,
                        glm::vec3(0.0f, 0.34f, 0.0f),
                        glm::vec3(0.35f, 0.20f, 0.35f),
                        0.10f,
                        0.42f,
                        glm::vec4(0.72f, 0.86f, 1.0f, 0.18f),
                        glm::vec4(0.72f, 0.86f, 1.0f, 0.0f),
                        -0.03f)
        },
        {
            "rain",
            "Rain",
            "procedural_billboard",
            MakeEmitter(ParticleEmitterType::Rain,
                        54.0f,
                        1.4f,
                        glm::vec3(0.0f, -2.4f, 0.25f),
                        glm::vec3(0.20f, 0.35f, 0.20f),
                        0.018f,
                        0.035f,
                        glm::vec4(0.58f, 0.74f, 1.0f, 0.42f),
                        glm::vec4(0.46f, 0.62f, 0.95f, 0.0f),
                        -0.7f)
        },
        {
            "snow",
            "Snow",
            "procedural_billboard",
            MakeEmitter(ParticleEmitterType::Snow,
                        30.0f,
                        4.6f,
                        glm::vec3(0.0f, -0.42f, 0.0f),
                        glm::vec3(0.32f, 0.16f, 0.32f),
                        0.035f,
                        0.075f,
                        glm::vec4(0.94f, 0.98f, 1.0f, 0.38f),
                        glm::vec4(0.88f, 0.94f, 1.0f, 0.0f),
                        -0.06f)
        }
    };
    return descriptors;
}

const ParticleEffectDescriptor* FindParticleEffectDescriptor(std::string_view id) {
    for (const auto& descriptor : GetParticleEffectDescriptors()) {
        if (descriptor.id == id) {
            return &descriptor;
        }
    }
    return nullptr;
}

bool ApplyParticleEffectDescriptor(std::string_view id, ParticleEmitterComponent& emitter) {
    const ParticleEffectDescriptor* descriptor = FindParticleEffectDescriptor(id);
    if (!descriptor) {
        return false;
    }
    emitter = descriptor->emitter;
    emitter.effectPresetId = std::string(id);
    return true;
}

} // namespace Cortex::Scene
