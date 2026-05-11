#include "Renderer.h"

#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <glm/gtx/norm.hpp>

namespace Cortex::Graphics {

namespace {

const char* LightingRigId(Renderer::LightingRig rig) {
    switch (rig) {
    case Renderer::LightingRig::StudioThreePoint: return "studio_three_point";
    case Renderer::LightingRig::TopDownWarehouse: return "top_down_warehouse";
    case Renderer::LightingRig::HorrorSideLight: return "horror_side_light";
    case Renderer::LightingRig::StreetLanterns: return "street_lanterns";
    case Renderer::LightingRig::Custom:
    default: return "custom";
    }
}

} // namespace

float Renderer::GetIBLDiffuseIntensity() const {
    return GetFeatureState().iblDiffuseIntensity;
}

float Renderer::GetIBLSpecularIntensity() const {
    return GetFeatureState().iblSpecularIntensity;
}

float Renderer::GetSunIntensity() const {
    return GetFeatureState().sunIntensity;
}

void Renderer::SetUseSafeLightingRigOnLowVRAM(bool enabled) {
    m_lightingState.useSafeRigOnLowVRAM = enabled;
}

bool Renderer::GetUseSafeLightingRigOnLowVRAM() const {
    return GetFeatureState().useSafeLightingRigOnLowVRAM;
}

void Renderer::SetLightingRigContract(std::string rigId, std::string source, bool safeVariantActive) {
    if (rigId.empty()) {
        rigId = "custom";
    }
    if (source.empty()) {
        source = "manual";
    }
    m_lightingState.activeRigId = std::move(rigId);
    m_lightingState.activeRigSource = std::move(source);
    m_lightingState.safeRigVariantActive = safeVariantActive;
}

void Renderer::ApplyLightingRig(LightingRig rig, Scene::ECS_Registry* registry) {
    if (!registry) {
        spdlog::warn("ApplyLightingRig called with null registry");
        return;
    }

    // Clear existing non-directional lights so rigs start from a known state.
    auto& enttReg = registry->GetRegistry();
    {
        auto view = enttReg.view<Scene::LightComponent>();
        std::vector<entt::entity> toDestroy;
        for (auto entity : view) {
            const auto& light = view.get<Scene::LightComponent>(entity);
            if (light.type == Scene::LightType::Directional) {
                continue;
            }
            toDestroy.push_back(entity);
        }
        for (auto e : toDestroy) {
            enttReg.destroy(e);
        }
    }

    // Reset global sun/ambient to reasonable defaults for each rig; this keeps
    // behavior stable even if previous state was extreme.
    m_lightingState.directionalDirection = glm::normalize(glm::vec3(0.5f, 1.0f, 0.3f));
    m_lightingState.directionalColor = glm::vec3(1.0f);
    m_lightingState.directionalIntensity = 5.0f;
    m_lightingState.ambientColor = glm::vec3(0.04f);
    m_lightingState.ambientIntensity = 1.0f;

    // On 8 GB-class adapters, optionally select a "safe" variant of each rig
    // with reduced intensities and fewer local shadow-casting lights. This
    // helps keep RTShowcase and other heavy scenes within budget.
    bool useSafeRig = false;
    if (m_services.device && m_lightingState.useSafeRigOnLowVRAM) {
        const std::uint64_t bytes = m_services.device->GetDedicatedVideoMemoryBytes();
        const std::uint64_t mb = bytes / (1024ull * 1024ull);
        if (mb > 0 && mb <= 8192ull) {
            useSafeRig = true;
        }
    }

    SetLightingRigContract(LightingRigId(rig), "renderer_rig", useSafeRig);

    switch (rig) {
    case LightingRig::Custom:
        spdlog::info("Lighting rig: Custom (no preset applied)");
        return;

    case LightingRig::StudioThreePoint: {
        // Key light - strong, warm spotlight from front-right
        {
            entt::entity e = enttReg.create();
            enttReg.emplace<Scene::TagComponent>(e, "KeyLight");
            auto& t = enttReg.emplace<Scene::TransformComponent>(e);
            t.position = glm::vec3(3.0f, 4.0f, -4.0f);
            glm::vec3 dir = glm::normalize(glm::vec3(-0.6f, -0.8f, 0.7f));
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(up, dir)) > 0.99f) {
                up = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            t.rotation = glm::quatLookAtLH(dir, up);

            auto& l = enttReg.emplace<Scene::LightComponent>(e);
            l.type = Scene::LightType::Spot;
            l.color = glm::vec3(1.0f, 0.95f, 0.85f);
            l.intensity = useSafeRig ? 10.0f : 14.0f;
            l.range = useSafeRig ? 18.0f : 25.0f;
            l.innerConeDegrees = 20.0f;
            l.outerConeDegrees = 35.0f;
            l.castsShadows = true;
        }
        // Fill light - softer, cooler point light from front-left
        {
            entt::entity e = enttReg.create();
            enttReg.emplace<Scene::TagComponent>(e, "FillLight");
            auto& t = enttReg.emplace<Scene::TransformComponent>(e);
            t.position = glm::vec3(-3.0f, 2.0f, -3.0f);

            auto& l = enttReg.emplace<Scene::LightComponent>(e);
            l.type = Scene::LightType::Point;
            l.color = glm::vec3(0.8f, 0.85f, 1.0f);
            l.intensity = useSafeRig ? 3.0f : 5.0f;
            l.range = useSafeRig ? 14.0f : 20.0f;
            l.castsShadows = false;
        }
        // Rim light - dimmer spotlight from behind
        {
            entt::entity e = enttReg.create();
            enttReg.emplace<Scene::TagComponent>(e, "RimLight");
            auto& t = enttReg.emplace<Scene::TransformComponent>(e);
            t.position = glm::vec3(0.0f, 3.0f, 4.0f);
            glm::vec3 dir = glm::normalize(glm::vec3(0.0f, -0.5f, -1.0f));
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(up, dir)) > 0.99f) {
                up = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            t.rotation = glm::quatLookAtLH(dir, up);

            auto& l = enttReg.emplace<Scene::LightComponent>(e);
            l.type = Scene::LightType::Spot;
            l.color = glm::vec3(0.9f, 0.9f, 1.0f);
            l.intensity = useSafeRig ? 5.0f : 8.0f;
            l.range = useSafeRig ? 18.0f : 25.0f;
            l.innerConeDegrees = 25.0f;
            l.outerConeDegrees = 40.0f;
            l.castsShadows = false;
        }
        spdlog::info("Applied lighting rig: StudioThreePoint");
        break;
    }

    case LightingRig::TopDownWarehouse: {
        // Cooler sun, higher ambient, and a grid of overhead point lights.
        m_lightingState.directionalDirection = glm::normalize(glm::vec3(0.2f, 1.0f, 0.1f));
        m_lightingState.directionalColor = glm::vec3(0.9f, 0.95f, 1.0f);
        m_lightingState.directionalIntensity = useSafeRig ? 2.5f : 3.5f;
        m_lightingState.ambientColor = glm::vec3(0.08f, 0.09f, 0.1f);
        m_lightingState.ambientIntensity = useSafeRig ? 1.0f : 1.5f;

        const int countX = 3;
        const int countZ = 3;
        const float spacing = 6.0f;
        const float startX = -spacing;
        const float startZ = -spacing;
        int index = 0;

        for (int ix = 0; ix < countX; ++ix) {
            for (int iz = 0; iz < countZ; ++iz) {
                entt::entity e = enttReg.create();
                std::string name = "WarehouseLight_" + std::to_string(index++);
                enttReg.emplace<Scene::TagComponent>(e, name);

                auto& t = enttReg.emplace<Scene::TransformComponent>(e);
                t.position = glm::vec3(startX + ix * spacing, 8.0f, startZ + iz * spacing);

                auto& l = enttReg.emplace<Scene::LightComponent>(e);
                l.type = Scene::LightType::Point;
                l.color = glm::vec3(0.9f, 0.95f, 1.0f);
                l.intensity = useSafeRig ? 7.0f : 10.0f;
                l.range = useSafeRig ? 8.0f : 10.0f;
                // On safe rigs keep the center light unshadowed; rely on
                // cascades and ambient for structure.
                l.castsShadows = (!useSafeRig && ix == 1 && iz == 1);
            }
        }
        spdlog::info("Applied lighting rig: TopDownWarehouse");
        break;
    }

    case LightingRig::HorrorSideLight: {
        // Reduce ambient and use a single harsh side light plus a dim back fill.
        m_lightingState.directionalDirection = glm::normalize(glm::vec3(-0.2f, 1.0f, 0.0f));
        m_lightingState.directionalColor = glm::vec3(0.8f, 0.7f, 0.6f);
        m_lightingState.directionalIntensity = useSafeRig ? 1.5f : 2.0f;
        m_lightingState.ambientColor = glm::vec3(0.01f, 0.01f, 0.02f);
        m_lightingState.ambientIntensity = useSafeRig ? 0.4f : 0.5f;

        // Strong side spotlight
        {
            entt::entity e = enttReg.create();
            enttReg.emplace<Scene::TagComponent>(e, "HorrorKey");
            auto& t = enttReg.emplace<Scene::TransformComponent>(e);
            t.position = glm::vec3(-5.0f, 2.0f, 0.0f);
            glm::vec3 dir = glm::normalize(glm::vec3(1.0f, -0.2f, 0.1f));
            glm::vec3 up(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(up, dir)) > 0.99f) {
                up = glm::vec3(0.0f, 0.0f, 1.0f);
            }
            t.rotation = glm::quatLookAtLH(dir, up);

            auto& l = enttReg.emplace<Scene::LightComponent>(e);
            l.type = Scene::LightType::Spot;
            l.color = glm::vec3(1.0f, 0.85f, 0.7f);
            l.intensity = useSafeRig ? 13.0f : 18.0f;
            l.range = useSafeRig ? 16.0f : 20.0f;
            l.innerConeDegrees = 18.0f;
            l.outerConeDegrees = 30.0f;
            l.castsShadows = true;
        }

        // Dim back fill so the dark side isn't completely black
        {
            entt::entity e = enttReg.create();
            enttReg.emplace<Scene::TagComponent>(e, "HorrorFill");
            auto& t = enttReg.emplace<Scene::TransformComponent>(e);
            t.position = glm::vec3(3.0f, 1.5f, -4.0f);

            auto& l = enttReg.emplace<Scene::LightComponent>(e);
            l.type = Scene::LightType::Point;
            l.color = glm::vec3(0.4f, 0.5f, 0.8f);
            l.intensity = useSafeRig ? 2.0f : 3.0f;
            l.range = useSafeRig ? 8.0f : 10.0f;
            l.castsShadows = false;
        }

        spdlog::info("Applied lighting rig: HorrorSideLight");
        break;
    }

    case LightingRig::StreetLanterns: {
        // Night-time street / alley rig: dim directional light, subtle ambient,
        // and a row of strong warm street lanterns that actually light the
        // environment. A subset of lights cast shadows to keep performance
        // reasonable while still giving good occlusion cues.
        m_lightingState.directionalDirection = glm::normalize(glm::vec3(-0.1f, -1.0f, 0.1f));
        m_lightingState.directionalColor = glm::vec3(0.5f, 0.55f, 0.65f);
        m_lightingState.directionalIntensity = useSafeRig ? 1.0f : 1.5f;
        m_lightingState.ambientColor = glm::vec3(0.02f, 0.03f, 0.05f);
        m_lightingState.ambientIntensity = useSafeRig ? 0.5f : 0.7f;

        const int lightCount = 8;
        const float spacing = 7.5f;
        const float startX = -((lightCount - 1) * spacing * 0.5f);
        const float zPos = -6.0f;
        const float height = 5.0f;

        for (int i = 0; i < lightCount; ++i) {
            entt::entity e = enttReg.create();
            std::string name = "StreetLantern_" + std::to_string(i);
            enttReg.emplace<Scene::TagComponent>(e, name);

            auto& t = enttReg.emplace<Scene::TransformComponent>(e);
            t.position = glm::vec3(startX + i * spacing, height, zPos);

            auto& l = enttReg.emplace<Scene::LightComponent>(e);
            l.type = Scene::LightType::Point;
            // Warm sodium-vapor style color
            l.color = glm::vec3(1.0f, 0.85f, 0.55f);
            // Strong intensity and generous range so they fill the street.
            l.intensity = useSafeRig ? 15.0f : 24.0f;
            l.range = useSafeRig ? 14.0f : 18.0f;
            // Let every second lantern cast shadows in the high variant; in
            // the safe variant only every fourth lantern is shadowed.
            if (useSafeRig) {
                l.castsShadows = (i % 4 == 0);
            } else {
                l.castsShadows = (i % 2 == 0);
            }
        }

        spdlog::info("Applied lighting rig: StreetLanterns ({} lights)", lightCount);
        break;
    }
    }
}

void Renderer::SetEnvironmentPreset(const std::string& name) {
    if (m_environmentState.maps.empty()) {
        spdlog::warn("No environments loaded");
        m_environmentState.selectionFallbackUsed = true;
        m_environmentState.requestedEnvironment = name;
        m_environmentState.fallbackReason = "no_resident_environments";
        return;
    }

    // Search for environment by name (case-insensitive partial match)
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                  [](unsigned char c) { return std::tolower(c); });

    size_t targetIndex = m_environmentState.currentIndex;
    bool found = false;

    auto matchesRequestedName = [&](const std::string& environmentName) {
        std::string envNameLower = environmentName;
        std::transform(envNameLower.begin(), envNameLower.end(), envNameLower.begin(),
                       [](unsigned char c) { return std::tolower(c); });
        return envNameLower.find(lowerName) != std::string::npos;
    };

    for (size_t i = 0; i < m_environmentState.maps.size(); ++i) {
        if (matchesRequestedName(m_environmentState.maps[i].name)) {
            targetIndex = i;
            found = true;
            break;
        }
    }

    if (!found) {
        auto pendingIt = std::find_if(
            m_environmentState.pending.begin(),
            m_environmentState.pending.end(),
            [&](const PendingEnvironment& pending) {
                return matchesRequestedName(pending.name);
            });

        if (pendingIt != m_environmentState.pending.end()) {
            PendingEnvironment pending = *pendingIt;
            m_environmentState.pending.erase(pendingIt);

            auto texResult = LoadTextureFromFile(pending.path, false, AssetRegistry::TextureKind::Environment);
            if (texResult.IsErr()) {
                spdlog::warn("Environment '{}' requested but failed to load '{}': {}",
                             name,
                             pending.path,
                             texResult.Error());
                m_environmentState.selectionFallbackUsed = true;
                m_environmentState.requestedEnvironment = name;
                m_environmentState.fallbackReason = "requested_environment_load_failed";
                return;
            }

            EnvironmentMaps env;
            env.name = pending.name;
            env.path = pending.path;
            env.budgetClass = pending.budgetClass;
            env.maxRuntimeDimension = pending.maxRuntimeDimension;
            env.defaultDiffuseIntensity = pending.defaultDiffuseIntensity;
            env.defaultSpecularIntensity = pending.defaultSpecularIntensity;
            env.diffuseIrradiance = texResult.Value();
            env.specularPrefiltered = env.diffuseIrradiance;

            m_environmentState.maps.push_back(std::move(env));
            m_environmentState.currentIndex = m_environmentState.maps.size() - 1;
            m_environmentState.selectionFallbackUsed = false;
            m_environmentState.requestedEnvironment = name;
            m_environmentState.fallbackReason.clear();
            EnforceIBLResidencyLimit();
            UpdateEnvironmentDescriptorTable();

            if (const EnvironmentMaps* current = m_environmentState.ActiveEnvironment()) {
                SetIBLIntensity(current->defaultDiffuseIntensity, current->defaultSpecularIntensity);
                spdlog::info("Environment preset '{}' loaded on demand from '{}'",
                             current->name,
                             current->path);
            }
            return;
        }

        if (m_environmentState.selectionFallbackUsed &&
            m_environmentState.requestedEnvironment == name &&
            m_environmentState.fallbackReason == "requested_environment_load_failed") {
            spdlog::warn("Environment '{}' still unavailable after earlier load failure; keeping current environment",
                         name);
            return;
        }

        spdlog::warn("Environment '{}' not found, keeping current environment", name);
        m_environmentState.selectionFallbackUsed = true;
        m_environmentState.requestedEnvironment = name;
        m_environmentState.fallbackReason = "requested_environment_not_found";
        return;
    }

    if (targetIndex == m_environmentState.currentIndex) {
        m_environmentState.selectionFallbackUsed = false;
        m_environmentState.requestedEnvironment = name;
        m_environmentState.fallbackReason.clear();
        if (const EnvironmentMaps* current = m_environmentState.ActiveEnvironment()) {
            SetIBLIntensity(current->defaultDiffuseIntensity, current->defaultSpecularIntensity);
        }
        return;
    }

    m_environmentState.currentIndex = targetIndex;
    m_environmentState.selectionFallbackUsed = false;
    m_environmentState.requestedEnvironment = name;
    m_environmentState.fallbackReason.clear();
    UpdateEnvironmentDescriptorTable();
    const EnvironmentMaps* current = m_environmentState.ActiveEnvironment();
    if (current) {
        SetIBLIntensity(current->defaultDiffuseIntensity, current->defaultSpecularIntensity);
    }

    spdlog::info("Environment preset set to '{}'", current ? current->name : "None");
}

std::string Renderer::GetCurrentEnvironmentName() const {
    return m_environmentState.ActiveEnvironmentName();
}

void Renderer::SetIBLIntensity(float diffuseIntensity, float specularIntensity) {
    float diff = std::max(diffuseIntensity, 0.0f);
    float spec = std::max(specularIntensity, 0.0f);
    if (std::abs(diff - m_environmentState.diffuseIntensity) < 1e-6f &&
        std::abs(spec - m_environmentState.specularIntensity) < 1e-6f) {
        return;
    }
    m_environmentState.diffuseIntensity = diff;
    m_environmentState.specularIntensity = spec;
    spdlog::info("IBL intensity set to diffuse={}, specular={}", m_environmentState.diffuseIntensity, m_environmentState.specularIntensity);
}

void Renderer::SetIBLEnabled(bool enabled) {
    if (m_environmentState.enabled == enabled) {
        return;
    }
    m_environmentState.enabled = enabled;
    spdlog::info("Image-based lighting {}", m_environmentState.enabled ? "ENABLED" : "DISABLED");
}

void Renderer::SetBackgroundPresentation(bool visible, float exposure, float blur) {
    const float clampedExposure = glm::clamp(exposure, 0.0f, 4.0f);
    const float clampedBlur = glm::clamp(blur, 0.0f, 1.0f);
    if (m_environmentState.backgroundVisible == visible &&
        std::abs(m_environmentState.backgroundExposure - clampedExposure) < 1e-4f &&
        std::abs(m_environmentState.backgroundBlur - clampedBlur) < 1e-4f) {
        return;
    }

    m_environmentState.backgroundVisible = visible;
    m_environmentState.backgroundExposure = clampedExposure;
    m_environmentState.backgroundBlur = clampedBlur;
    spdlog::info("Background presentation: visible={} exposure={} blur={}",
                 visible,
                 m_environmentState.backgroundExposure,
                 m_environmentState.backgroundBlur);
}

void Renderer::SetSunDirection(const glm::vec3& dir) {
    glm::vec3 d = dir;
    if (!std::isfinite(d.x) || !std::isfinite(d.y) || !std::isfinite(d.z) ||
        glm::length2(d) < 1e-6f) {
        spdlog::warn("SetSunDirection: invalid direction, ignoring");
        return;
    }
    m_lightingState.directionalDirection = glm::normalize(d);
    // Debug level to avoid log spam when called every frame
    spdlog::debug("Sun direction set to ({:.2f}, {:.2f}, {:.2f})",
                 m_lightingState.directionalDirection.x,
                 m_lightingState.directionalDirection.y,
                 m_lightingState.directionalDirection.z);
}

void Renderer::SetSunColor(const glm::vec3& color) {
    glm::vec3 c = glm::max(color, glm::vec3(0.0f));
    m_lightingState.directionalColor = c;
    // Debug level to avoid log spam when called every frame
    spdlog::debug("Sun color set to ({:.2f}, {:.2f}, {:.2f})",
                 m_lightingState.directionalColor.x,
                 m_lightingState.directionalColor.y,
                 m_lightingState.directionalColor.z);
}

void Renderer::SetSunIntensity(float intensity) {
    float v = std::max(intensity, 0.0f);
    m_lightingState.directionalIntensity = v;
    // Debug level to avoid log spam when called every frame
    spdlog::debug("Sun intensity set to {:.2f}", m_lightingState.directionalIntensity);
}

void Renderer::CycleEnvironmentPreset() {
    if (m_environmentState.maps.empty()) {
        spdlog::warn("No environments loaded to cycle through");
        return;
    }

    // Treat "no IBL" as an extra preset in the cycle:
    //   env0 -> env1 -> ... -> envN-1 -> None -> env0 -> ...
    if (!m_environmentState.enabled) {
        // Currently in "no IBL" mode; re-enable and jump to the first environment.
        SetIBLEnabled(true);
        m_environmentState.currentIndex = 0;
        m_environmentState.selectionFallbackUsed = false;
        m_environmentState.requestedEnvironment = m_environmentState.ActiveEnvironmentName();
        m_environmentState.fallbackReason.clear();
        UpdateEnvironmentDescriptorTable();

        const std::string name = m_environmentState.ActiveEnvironmentName();
        spdlog::info("Environment cycled to '{}' ({}/{})", name, m_environmentState.currentIndex + 1, m_environmentState.maps.size());
        return;
    }

    if (m_environmentState.currentIndex + 1 < m_environmentState.maps.size()) {
        // Advance to the next environment preset.
        m_environmentState.currentIndex++;
        m_environmentState.selectionFallbackUsed = false;
        m_environmentState.requestedEnvironment = m_environmentState.ActiveEnvironmentName();
        m_environmentState.fallbackReason.clear();
        UpdateEnvironmentDescriptorTable();

        const std::string name = m_environmentState.ActiveEnvironmentName();
        spdlog::info("Environment cycled to '{}' ({}/{})", name, m_environmentState.currentIndex + 1, m_environmentState.maps.size());
    } else {
        // Wrapped past the last preset: switch to a neutral "no IBL" mode.
        SetIBLEnabled(false);
        spdlog::info("Environment cycled to 'None' (no IBL)");
    }
}

} // namespace Cortex::Graphics
