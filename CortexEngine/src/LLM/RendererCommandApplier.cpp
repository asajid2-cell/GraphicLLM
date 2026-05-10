#include "RendererCommandApplier.h"

#include "Graphics/RendererControlApplier.h"
#include "Graphics/RendererLightingRigControl.h"
#include "SceneCommands.h"
#include "Graphics/Renderer.h"
#include "Scene/ECS_Registry.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace Cortex::LLM {

RendererCommandApplyResult ApplyModifyRendererCommand(ModifyRendererCommand& cmd,
                                                      Graphics::Renderer& renderer,
                                                      Scene::ECS_Registry* registry) {
    std::ostringstream summary;
    summary << "renderer: ";
    bool touched = false;

    if (cmd.setExposure) {
        Graphics::ApplyExposureControl(renderer, cmd.exposure);
        summary << "exposure=" << cmd.exposure << " ";
        touched = true;
    }
    if (cmd.setShadowsEnabled) {
        Graphics::ApplyFeatureToggleControl(renderer, Graphics::RendererFeatureToggle::Shadows, cmd.shadowsEnabled);
        summary << "shadows=" << (cmd.shadowsEnabled ? "on" : "off") << " ";
        touched = true;
    }
    if (cmd.setDebugMode) {
        Graphics::ApplyDebugViewModeControl(renderer, cmd.debugMode);
        summary << "debug_mode=" << cmd.debugMode << " ";
        touched = true;
    }
    if (cmd.setShadowBias) {
        Graphics::ApplyShadowBiasControl(renderer, cmd.shadowBias);
        summary << "bias=" << cmd.shadowBias << " ";
        touched = true;
    }
    if (cmd.setShadowPCFRadius) {
        Graphics::ApplyShadowPCFRadiusControl(renderer, cmd.shadowPCFRadius);
        summary << "pcf=" << cmd.shadowPCFRadius << " ";
        touched = true;
    }
    if (cmd.setCascadeSplitLambda) {
        Graphics::ApplyCascadeSplitLambdaControl(renderer, cmd.cascadeSplitLambda);
        summary << "lambda=" << cmd.cascadeSplitLambda << " ";
        touched = true;
    }
    if (cmd.setEnvironment) {
        Graphics::ApplyEnvironmentPresetControl(renderer, cmd.environment);
        summary << "environment=" << cmd.environment << " ";
        touched = true;
    }
    if (cmd.setIBLEnabled) {
        Graphics::ApplyFeatureToggleControl(renderer, Graphics::RendererFeatureToggle::IBL, cmd.iblEnabled);
        summary << "ibl=" << (cmd.iblEnabled ? "on" : "off") << " ";
        touched = true;
    }
    if (cmd.setIBLIntensity) {
        Graphics::ApplyIBLIntensityControl(renderer, cmd.iblDiffuseIntensity, cmd.iblSpecularIntensity);
        summary << "ibl_intensity=[" << cmd.iblDiffuseIntensity << "," << cmd.iblSpecularIntensity << "] ";
        touched = true;
    }
    if (cmd.setColorGrade) {
        Graphics::ApplyColorGradeControl(renderer, cmd.colorGradeWarm, cmd.colorGradeCool);
        summary << "grade=(" << cmd.colorGradeWarm << "," << cmd.colorGradeCool << ") ";
        touched = true;
    }
    if (cmd.setSSAOEnabled) {
        Graphics::ApplyFeatureToggleControl(renderer, Graphics::RendererFeatureToggle::SSAO, cmd.ssaoEnabled);
        summary << "ssao=" << (cmd.ssaoEnabled ? "on" : "off") << " ";
        touched = true;
    }
    if (cmd.setSSAOParams) {
        Graphics::ApplySSAOParamsControl(renderer, cmd.ssaoRadius, cmd.ssaoBias, cmd.ssaoIntensity);
        summary << "ssao_params=(r:" << cmd.ssaoRadius << ",b:" << cmd.ssaoBias << ",i:" << cmd.ssaoIntensity << ") ";
        touched = true;
    }
    if (cmd.setFogEnabled) {
        Graphics::ApplyFeatureToggleControl(renderer, Graphics::RendererFeatureToggle::Fog, cmd.fogEnabled);
        summary << "fog=" << (cmd.fogEnabled ? "on" : "off") << " ";
        touched = true;
    }
    if (cmd.setFogParams) {
        Graphics::ApplyFogParamsControl(renderer, cmd.fogDensity, cmd.fogHeight, cmd.fogFalloff);
        summary << "fog_params=(d:" << cmd.fogDensity << ",h:" << cmd.fogHeight << ",f:" << cmd.fogFalloff << ") ";
        touched = true;
    }
    if (cmd.setSunDirection) {
        Graphics::ApplySunDirectionControl(renderer, cmd.sunDirection);
        summary << "sun_dir=(" << cmd.sunDirection.x << "," << cmd.sunDirection.y << "," << cmd.sunDirection.z << ") ";
        touched = true;
    }
    if (cmd.setSunColor) {
        Graphics::ApplySunColorControl(renderer, cmd.sunColor);
        summary << "sun_color=(" << cmd.sunColor.r << "," << cmd.sunColor.g << "," << cmd.sunColor.b << ") ";
        touched = true;
    }
    if (cmd.setSunIntensity) {
        Graphics::ApplySunIntensityControl(renderer, cmd.sunIntensity);
        summary << "sun_intensity=" << cmd.sunIntensity << " ";
        touched = true;
    }
    if (cmd.setWaterLevel || cmd.setWaterWaveAmplitude || cmd.setWaterWaveLength ||
        cmd.setWaterWaveSpeed || cmd.setWaterSecondaryAmplitude) {
        const auto water = renderer.GetWaterState();
        float level     = water.levelY;
        float amp       = water.waveAmplitude;
        float waveLen   = water.waveLength;
        float speed     = water.waveSpeed;
        float secondary = water.secondaryAmplitude;

        if (cmd.setWaterLevel)              level     = cmd.waterLevel;
        if (cmd.setWaterWaveAmplitude)      amp       = cmd.waterWaveAmplitude;
        if (cmd.setWaterWaveLength)         waveLen   = cmd.waterWaveLength;
        if (cmd.setWaterWaveSpeed)          speed     = cmd.waterWaveSpeed;
        if (cmd.setWaterSecondaryAmplitude) secondary = cmd.waterSecondaryAmplitude;

        Graphics::ApplyWaterStateControl(renderer, level, amp, waveLen, speed, secondary);
        summary << "water(level=" << level
                << ",amp=" << amp
                << ",len=" << waveLen
                << ",speed=" << speed
                << ",secondary=" << secondary << ") ";
        touched = true;
    }
    if (cmd.setLightingRig && registry) {
        std::string name = cmd.lightingRig;
        std::transform(name.begin(), name.end(), name.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        Graphics::Renderer::LightingRig rig = Graphics::Renderer::LightingRig::Custom;
        if (name == "studio_three_point" || name == "studio" || name == "three_point") {
            rig = Graphics::Renderer::LightingRig::StudioThreePoint;
        } else if (name == "warehouse" || name == "top_down_warehouse" || name == "topdown_warehouse") {
            rig = Graphics::Renderer::LightingRig::TopDownWarehouse;
        } else if (name == "horror_side" || name == "horror" || name == "horror_side_light") {
            rig = Graphics::Renderer::LightingRig::HorrorSideLight;
        } else if (name == "street_lanterns" || name == "streetlights" || name == "street_lights" ||
                   name == "alley_lights" || name == "road_lights") {
            rig = Graphics::Renderer::LightingRig::StreetLanterns;
        }

        if (rig != Graphics::Renderer::LightingRig::Custom) {
            Graphics::ApplyLightingRigControl(renderer, rig, registry);
            summary << "lighting_rig=" << name << " ";
            touched = true;
        }
    }

    if (!touched) {
        return {false, "modify_renderer had no effect (no fields set)"};
    }
    return {true, summary.str()};
}

} // namespace Cortex::LLM
