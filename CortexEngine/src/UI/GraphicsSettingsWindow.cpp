#include "GraphicsSettingsWindow.h"

#include "Core/Engine.h"
#include "Core/ServiceLocator.h"
#include "Graphics/Renderer.h"
#include "Graphics/RendererControlApplier.h"
#include "Graphics/EnvironmentManifest.h"
#include "Graphics/RendererLightingRigControl.h"
#include "Graphics/RendererTuningState.h"
#include "Scene/ParticleEffectLibrary.h"

#include <commctrl.h>

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace Cortex::UI {

namespace {

enum ControlIdGraphics : int {
    IDC_GFX_HEALTH = 9001,
    IDC_GFX_MEMORY = 9002,
    IDC_GFX_WARNING = 9003,
    IDC_GFX_RT_SCHEDULER = 9004,

    IDC_GFX_RENDER_SCALE = 9010,
    IDC_GFX_EXPOSURE = 9011,
    IDC_GFX_BLOOM = 9012,
    IDC_GFX_SUN = 9013,
    IDC_GFX_GOD_RAYS = 9014,
    IDC_GFX_AREA_LIGHT = 9015,
    IDC_GFX_IBL_DIFFUSE = 9016,
    IDC_GFX_IBL_SPECULAR = 9017,
    IDC_GFX_SSAO_RADIUS = 9018,
    IDC_GFX_SSAO_INTENSITY = 9019,
    IDC_GFX_FOG_DENSITY = 9020,
    IDC_GFX_WATER_WAVE = 9021,
    IDC_GFX_BLOOM_THRESHOLD = 9022,
    IDC_GFX_BLOOM_KNEE = 9023,
    IDC_GFX_VIGNETTE = 9024,
    IDC_GFX_LENS_DIRT = 9025,
    IDC_GFX_BACKGROUND_EXPOSURE = 9026,
    IDC_GFX_BACKGROUND_BLUR = 9027,
    IDC_GFX_PARTICLE_DENSITY = 9028,
    IDC_GFX_RIG_STUDIO = 9029,
    IDC_GFX_RIG_WAREHOUSE = 9030,
    IDC_GFX_RIG_SIDE = 9031,
    IDC_GFX_RIG_LANTERNS = 9032,
    IDC_GFX_RT_REFL_DENOISE = 9033,
    IDC_GFX_RT_REFL_STRENGTH = 9034,
    IDC_GFX_RT_REFL_ROUGHNESS = 9050,
    IDC_GFX_RT_REFL_HISTORY_CLAMP = 9051,
    IDC_GFX_RT_REFL_FIREFLY = 9052,
    IDC_GFX_RT_REFL_SCALE = 9053,
    IDC_GFX_GRADE_NEUTRAL = 9054,
    IDC_GFX_GRADE_WARM_FILM = 9055,
    IDC_GFX_GRADE_COOL_MOON = 9056,
    IDC_GFX_GRADE_BLEACH = 9057,
    IDC_GFX_MOTION_BLUR = 9058,
    IDC_GFX_DOF = 9059,
    IDC_GFX_TONE_ACES = 9060,
    IDC_GFX_TONE_REINHARD = 9061,
    IDC_GFX_TONE_SOFT = 9062,
    IDC_GFX_TONE_PUNCHY = 9063,
    IDC_GFX_PARTICLE_QUALITY = 9064,
    IDC_GFX_PARTICLE_BLOOM = 9065,
    IDC_GFX_PARTICLE_SOFT_DEPTH = 9066,
    IDC_GFX_PARTICLE_WIND = 9067,
    IDC_GFX_ENV_ROTATION = 9068,
    IDC_GFX_RT_GI_STRENGTH = 9069,
    IDC_GFX_RT_GI_DISTANCE = 9070,
    IDC_GFX_WARM = 9035,
    IDC_GFX_COOL = 9036,
    IDC_GFX_WATER_LENGTH = 9037,
    IDC_GFX_WATER_SPEED = 9038,
    IDC_GFX_WATER_SECONDARY = 9039,
    IDC_GFX_FOG_HEIGHT = 9040,
    IDC_GFX_FOG_FALLOFF = 9041,
    IDC_GFX_SSAO_BIAS = 9042,
    IDC_GFX_SSR_DISTANCE = 9043,
    IDC_GFX_SSR_THICKNESS = 9044,
    IDC_GFX_SSR_STRENGTH = 9045,
    IDC_GFX_WATER_ROUGHNESS = 9046,
    IDC_GFX_WATER_FRESNEL = 9047,
    IDC_GFX_CONTRAST = 9048,
    IDC_GFX_SATURATION = 9049,

    IDC_GFX_TAA = 9100,
    IDC_GFX_FXAA = 9101,
    IDC_GFX_GPU_CULLING = 9102,
    IDC_GFX_RT = 9103,
    IDC_GFX_RT_REFLECTIONS = 9104,
    IDC_GFX_RT_GI = 9105,
    IDC_GFX_SSR = 9106,
    IDC_GFX_SSAO = 9107,
    IDC_GFX_PCSS = 9108,
    IDC_GFX_IBL = 9109,
    IDC_GFX_IBL_LIMIT = 9110,
    IDC_GFX_FOG = 9111,
    IDC_GFX_PARTICLES = 9112,
    IDC_GFX_BACKGROUND_VISIBLE = 9113,
    IDC_GFX_CINEMATIC_POST = 9114,
    IDC_GFX_SAFE_LIGHTING = 9115,

    IDC_GFX_SAFE_PRESET = 9200,
    IDC_GFX_HERO_BASELINE = 9201,
    IDC_GFX_RESET_FROM_RENDERER = 9202,
    IDC_GFX_ENV_NEXT = 9203,
    IDC_GFX_ENV_ONE = 9204,
    IDC_GFX_ENV_ALL = 9205,
    IDC_GFX_SAVE = 9206,
    IDC_GFX_LOAD = 9207,
    IDC_GFX_BOOKMARK_HERO = 9208,
    IDC_GFX_BOOKMARK_REFLECTION = 9209,
    IDC_GFX_BOOKMARK_MATERIALS = 9210,
    IDC_GFX_ENV_SELECT = 9211,
    IDC_GFX_ENV_REAPPLY = 9212,
    IDC_GFX_PARTICLE_EFFECT_SELECT = 9213,
};

struct SliderBinding {
    HWND hwnd = nullptr;
    float minValue = 0.0f;
    float maxValue = 1.0f;
};

struct GraphicsSettingsState {
    bool initialized = false;
    bool visible = false;
    HWND parent = nullptr;
    HWND hwnd = nullptr;
    HFONT font = nullptr;

    Graphics::RendererTuningState tuning;

    HWND txtHealth = nullptr;
    HWND txtMemory = nullptr;
    HWND txtWarning = nullptr;
    HWND txtRTScheduler = nullptr;

    SliderBinding renderScale;
    SliderBinding exposure;
    SliderBinding bloom;
    SliderBinding warm;
    SliderBinding cool;
    SliderBinding sun;
    SliderBinding godRays;
    SliderBinding areaLight;
    SliderBinding iblDiffuse;
    SliderBinding iblSpecular;
    SliderBinding backgroundExposure;
    SliderBinding backgroundBlur;
    SliderBinding environmentRotation;
    SliderBinding rtReflectionDenoise;
    SliderBinding rtReflectionStrength;
    SliderBinding rtReflectionRoughness;
    SliderBinding rtReflectionHistoryClamp;
    SliderBinding rtReflectionFirefly;
    SliderBinding rtReflectionScale;
    SliderBinding rtGIStrength;
    SliderBinding rtGIDistance;
    SliderBinding ssrDistance;
    SliderBinding ssrThickness;
    SliderBinding ssrStrength;
    SliderBinding ssaoRadius;
    SliderBinding ssaoBias;
    SliderBinding ssaoIntensity;
    SliderBinding fogDensity;
    SliderBinding fogHeight;
    SliderBinding fogFalloff;
    SliderBinding waterWave;
    SliderBinding waterLength;
    SliderBinding waterSpeed;
    SliderBinding waterSecondary;
    SliderBinding waterRoughness;
    SliderBinding waterFresnel;
    SliderBinding bloomThreshold;
    SliderBinding bloomKnee;
    SliderBinding contrast;
    SliderBinding saturation;
    SliderBinding vignette;
    SliderBinding lensDirt;
    SliderBinding motionBlur;
    SliderBinding depthOfField;
    SliderBinding particleDensity;
    SliderBinding particleQuality;
    SliderBinding particleBloom;
    SliderBinding particleSoftDepth;
    SliderBinding particleWind;

    HWND chkTAA = nullptr;
    HWND chkFXAA = nullptr;
    HWND chkGPUCulling = nullptr;
    HWND chkSafeLighting = nullptr;
    HWND chkRT = nullptr;
    HWND chkRTReflections = nullptr;
    HWND chkRTGI = nullptr;
    HWND chkSSR = nullptr;
    HWND chkSSAO = nullptr;
    HWND chkPCSS = nullptr;
    HWND chkIBL = nullptr;
    HWND chkIBLLimit = nullptr;
    HWND chkBackgroundVisible = nullptr;
    HWND chkFog = nullptr;
    HWND chkParticles = nullptr;
    HWND chkCinematicPost = nullptr;
    HWND cmbEnvironment = nullptr;
    HWND cmbParticleEffect = nullptr;
    std::vector<std::string> environmentIds;
    std::vector<std::string> particleEffectIds;

    int contentHeight = 0;
    int scrollPos = 0;
};

GraphicsSettingsState g_gfx;
const wchar_t* kGraphicsSettingsClassName = L"CortexGraphicsSettingsWindow";

std::wstring ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring out(static_cast<size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), out.data(), count);
    return out;
}

float SliderToFloat(const SliderBinding& binding) {
    if (!binding.hwnd) {
        return binding.minValue;
    }
    const int pos = static_cast<int>(SendMessageW(binding.hwnd, TBM_GETPOS, 0, 0));
    const float t = static_cast<float>(pos) / 100.0f;
    return binding.minValue + t * (binding.maxValue - binding.minValue);
}

void SetSliderFromFloat(const SliderBinding& binding, float value) {
    if (!binding.hwnd) {
        return;
    }
    float t = 0.0f;
    if (binding.maxValue > binding.minValue) {
        t = (value - binding.minValue) / (binding.maxValue - binding.minValue);
    }
    int pos = static_cast<int>(std::clamp(t, 0.0f, 1.0f) * 100.0f + 0.5f);
    SendMessageW(binding.hwnd, TBM_SETPOS, TRUE, pos);
}

void SetCheckbox(HWND hwnd, bool enabled) {
    if (hwnd) {
        SendMessageW(hwnd, BM_SETCHECK, enabled ? BST_CHECKED : BST_UNCHECKED, 0);
    }
}

bool GetCheckbox(HWND hwnd) {
    return hwnd && SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED;
}

void SyncStateFromToggles();
void SyncStateFromSliders();

void LoadEnvironmentOptions() {
    g_gfx.environmentIds.clear();
    if (!g_gfx.cmbEnvironment) {
        return;
    }

    SendMessageW(g_gfx.cmbEnvironment, CB_RESETCONTENT, 0, 0);

    auto addOption = [](const std::string& id, const std::string& displayName) {
        if (id.empty()) {
            return;
        }
        std::string label = displayName.empty() ? id : (displayName + " (" + id + ")");
        g_gfx.environmentIds.push_back(id);
        const std::wstring wideLabel = ToWide(label);
        SendMessageW(g_gfx.cmbEnvironment, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wideLabel.c_str()));
    };

    const auto manifestResult = Graphics::LoadEnvironmentManifest(Graphics::DefaultEnvironmentManifestPath());
    if (manifestResult.IsOk()) {
        for (const auto& entry : manifestResult.Value().environments) {
            if (!entry.enabled) {
                continue;
            }
            addOption(entry.id, entry.displayName);
        }
    }

    if (g_gfx.environmentIds.empty()) {
        addOption("studio", "Natural Studio");
        addOption("warm_gallery", "Mirrored Hall");
        addOption("sunset_courtyard", "Sky On Fire");
        addOption("cool_overcast", "Kloofendal Overcast");
        addOption("night_city", "Shanghai Bund Night");
        addOption("procedural_sky", "Procedural Sky Fallback");
    }

    SendMessageW(g_gfx.cmbEnvironment, CB_SETCURSEL, 0, 0);
}

void SyncEnvironmentComboFromRenderer() {
    if (!g_gfx.cmbEnvironment || g_gfx.environmentIds.empty()) {
        return;
    }

    const std::string active = g_gfx.tuning.environment.environmentId;
    std::string activeLower = active;
    std::transform(activeLower.begin(), activeLower.end(), activeLower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    int selected = -1;
    for (size_t i = 0; i < g_gfx.environmentIds.size(); ++i) {
        std::string idLower = g_gfx.environmentIds[i];
        std::transform(idLower.begin(), idLower.end(), idLower.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (idLower == activeLower || activeLower.find(idLower) != std::string::npos) {
            selected = static_cast<int>(i);
            break;
        }
    }
    if (selected >= 0) {
        SendMessageW(g_gfx.cmbEnvironment, CB_SETCURSEL, selected, 0);
    }
}

void LoadParticleEffectOptions() {
    g_gfx.particleEffectIds.clear();
    if (!g_gfx.cmbParticleEffect) {
        return;
    }

    SendMessageW(g_gfx.cmbParticleEffect, CB_RESETCONTENT, 0, 0);
    g_gfx.particleEffectIds.push_back("gallery_mix");
    SendMessageW(g_gfx.cmbParticleEffect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Gallery Mix"));

    for (const auto& descriptor : Scene::GetParticleEffectDescriptors()) {
        g_gfx.particleEffectIds.emplace_back(descriptor.id);
        const std::string label = std::string(descriptor.displayName) + " (" + std::string(descriptor.id) + ")";
        const std::wstring wideLabel = ToWide(label);
        SendMessageW(g_gfx.cmbParticleEffect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(wideLabel.c_str()));
    }

    SendMessageW(g_gfx.cmbParticleEffect, CB_SETCURSEL, 0, 0);
}

void SyncParticleEffectComboFromRenderer() {
    if (!g_gfx.cmbParticleEffect || g_gfx.particleEffectIds.empty()) {
        return;
    }

    const std::string active = g_gfx.tuning.particles.effectPreset.empty()
        ? "gallery_mix"
        : g_gfx.tuning.particles.effectPreset;
    for (size_t i = 0; i < g_gfx.particleEffectIds.size(); ++i) {
        if (g_gfx.particleEffectIds[i] == active) {
            SendMessageW(g_gfx.cmbParticleEffect, CB_SETCURSEL, static_cast<WPARAM>(i), 0);
            return;
        }
    }
    SendMessageW(g_gfx.cmbParticleEffect, CB_SETCURSEL, 0, 0);
}

void SyncSelectedParticleEffectToState() {
    if (!g_gfx.cmbParticleEffect || g_gfx.particleEffectIds.empty()) {
        return;
    }
    const int selected = static_cast<int>(SendMessageW(g_gfx.cmbParticleEffect, CB_GETCURSEL, 0, 0));
    if (selected >= 0 && static_cast<size_t>(selected) < g_gfx.particleEffectIds.size()) {
        g_gfx.tuning.particles.effectPreset = g_gfx.particleEffectIds[static_cast<size_t>(selected)];
    }
}

void ApplySelectedParticleEffectFromGraphicsUI() {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    auto* engine = Cortex::ServiceLocator::GetEngine();
    if (!renderer || renderer->IsDeviceRemoved() || !engine || !g_gfx.cmbParticleEffect) {
        return;
    }

    SyncStateFromToggles();
    SyncStateFromSliders();
    SyncSelectedParticleEffectToState();
    g_gfx.tuning.quality.dirtyFromUI = true;
    Graphics::ApplyRendererTuningState(*renderer, g_gfx.tuning);
    engine->ApplyParticleEffectPresetToScene(g_gfx.tuning.particles.effectPreset);
    g_gfx.tuning = Graphics::CaptureRendererTuningState(*renderer);
    SyncParticleEffectComboFromRenderer();
}

void ApplySelectedEnvironmentFromGraphicsUI(bool loadAllFirst = false) {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (!renderer || renderer->IsDeviceRemoved() || !g_gfx.cmbEnvironment) {
        return;
    }

    const int selected = static_cast<int>(SendMessageW(g_gfx.cmbEnvironment, CB_GETCURSEL, 0, 0));
    if (selected < 0 || static_cast<size_t>(selected) >= g_gfx.environmentIds.size()) {
        return;
    }

    SyncStateFromToggles();
    SyncStateFromSliders();
    SyncSelectedParticleEffectToState();
    g_gfx.tuning.environment.environmentId = g_gfx.environmentIds[static_cast<size_t>(selected)];
    g_gfx.tuning.quality.dirtyFromUI = true;

    if (loadAllFirst) {
        Graphics::ApplyEnvironmentResidencyLoadControl(*renderer, 64);
    }
    Graphics::ApplyEnvironmentPresetControl(*renderer, g_gfx.tuning.environment.environmentId);
    Graphics::ApplyRendererTuningState(*renderer, g_gfx.tuning);
    g_gfx.tuning = Graphics::CaptureRendererTuningState(*renderer);
    SyncEnvironmentComboFromRenderer();
}

void SyncStateFromToggles() {
    g_gfx.tuning.quality.taaEnabled = GetCheckbox(g_gfx.chkTAA);
    g_gfx.tuning.quality.fxaaEnabled = GetCheckbox(g_gfx.chkFXAA);
    g_gfx.tuning.quality.gpuCullingEnabled = GetCheckbox(g_gfx.chkGPUCulling);
    g_gfx.tuning.quality.safeLightingRigOnLowVRAM = GetCheckbox(g_gfx.chkSafeLighting);

    g_gfx.tuning.rayTracing.enabled = GetCheckbox(g_gfx.chkRT);
    g_gfx.tuning.rayTracing.reflectionsEnabled = GetCheckbox(g_gfx.chkRTReflections);
    g_gfx.tuning.rayTracing.giEnabled = GetCheckbox(g_gfx.chkRTGI);

    g_gfx.tuning.screenSpace.ssrEnabled = GetCheckbox(g_gfx.chkSSR);
    g_gfx.tuning.screenSpace.ssaoEnabled = GetCheckbox(g_gfx.chkSSAO);
    g_gfx.tuning.screenSpace.pcssEnabled = GetCheckbox(g_gfx.chkPCSS);

    g_gfx.tuning.environment.iblEnabled = GetCheckbox(g_gfx.chkIBL);
    g_gfx.tuning.environment.iblLimitEnabled = GetCheckbox(g_gfx.chkIBLLimit);
    g_gfx.tuning.environment.backgroundVisible = GetCheckbox(g_gfx.chkBackgroundVisible);

    g_gfx.tuning.atmosphere.fogEnabled = GetCheckbox(g_gfx.chkFog);
    g_gfx.tuning.particles.enabled = GetCheckbox(g_gfx.chkParticles);
    g_gfx.tuning.cinematicPost.enabled = GetCheckbox(g_gfx.chkCinematicPost);
}

void SyncStateFromSliders() {
    g_gfx.tuning.quality.renderScale = SliderToFloat(g_gfx.renderScale);
    g_gfx.tuning.lighting.exposure = SliderToFloat(g_gfx.exposure);
    g_gfx.tuning.lighting.bloomIntensity = SliderToFloat(g_gfx.bloom);
    g_gfx.tuning.lighting.warm = SliderToFloat(g_gfx.warm);
    g_gfx.tuning.lighting.cool = SliderToFloat(g_gfx.cool);
    g_gfx.tuning.lighting.sunIntensity = SliderToFloat(g_gfx.sun);
    g_gfx.tuning.lighting.godRayIntensity = SliderToFloat(g_gfx.godRays);
    g_gfx.tuning.lighting.areaLightSizeScale = SliderToFloat(g_gfx.areaLight);
    g_gfx.tuning.environment.diffuseIntensity = SliderToFloat(g_gfx.iblDiffuse);
    g_gfx.tuning.environment.specularIntensity = SliderToFloat(g_gfx.iblSpecular);
    g_gfx.tuning.environment.backgroundExposure = SliderToFloat(g_gfx.backgroundExposure);
    g_gfx.tuning.environment.backgroundBlur = SliderToFloat(g_gfx.backgroundBlur);
    g_gfx.tuning.environment.rotationDegrees = SliderToFloat(g_gfx.environmentRotation);
    g_gfx.tuning.rayTracing.reflectionDenoiseAlpha = SliderToFloat(g_gfx.rtReflectionDenoise);
    g_gfx.tuning.rayTracing.reflectionCompositionStrength = SliderToFloat(g_gfx.rtReflectionStrength);
    g_gfx.tuning.rayTracing.reflectionRoughnessThreshold = SliderToFloat(g_gfx.rtReflectionRoughness);
    g_gfx.tuning.rayTracing.reflectionHistoryMaxBlend = SliderToFloat(g_gfx.rtReflectionHistoryClamp);
    g_gfx.tuning.rayTracing.reflectionFireflyClampLuma = SliderToFloat(g_gfx.rtReflectionFirefly);
    g_gfx.tuning.rayTracing.reflectionSignalScale = SliderToFloat(g_gfx.rtReflectionScale);
    g_gfx.tuning.rayTracing.giStrength = SliderToFloat(g_gfx.rtGIStrength);
    g_gfx.tuning.rayTracing.giRayDistance = SliderToFloat(g_gfx.rtGIDistance);
    g_gfx.tuning.screenSpace.ssrMaxDistance = SliderToFloat(g_gfx.ssrDistance);
    g_gfx.tuning.screenSpace.ssrThickness = SliderToFloat(g_gfx.ssrThickness);
    g_gfx.tuning.screenSpace.ssrStrength = SliderToFloat(g_gfx.ssrStrength);
    g_gfx.tuning.screenSpace.ssaoRadius = SliderToFloat(g_gfx.ssaoRadius);
    g_gfx.tuning.screenSpace.ssaoBias = SliderToFloat(g_gfx.ssaoBias);
    g_gfx.tuning.screenSpace.ssaoIntensity = SliderToFloat(g_gfx.ssaoIntensity);
    g_gfx.tuning.atmosphere.fogDensity = SliderToFloat(g_gfx.fogDensity);
    g_gfx.tuning.atmosphere.fogHeight = SliderToFloat(g_gfx.fogHeight);
    g_gfx.tuning.atmosphere.fogFalloff = SliderToFloat(g_gfx.fogFalloff);
    g_gfx.tuning.water.waveAmplitude = SliderToFloat(g_gfx.waterWave);
    g_gfx.tuning.water.waveLength = SliderToFloat(g_gfx.waterLength);
    g_gfx.tuning.water.waveSpeed = SliderToFloat(g_gfx.waterSpeed);
    g_gfx.tuning.water.secondaryAmplitude = SliderToFloat(g_gfx.waterSecondary);
    g_gfx.tuning.water.roughness = SliderToFloat(g_gfx.waterRoughness);
    g_gfx.tuning.water.fresnelStrength = SliderToFloat(g_gfx.waterFresnel);
    g_gfx.tuning.cinematicPost.bloomThreshold = SliderToFloat(g_gfx.bloomThreshold);
    g_gfx.tuning.cinematicPost.bloomSoftKnee = SliderToFloat(g_gfx.bloomKnee);
    g_gfx.tuning.cinematicPost.contrast = SliderToFloat(g_gfx.contrast);
    g_gfx.tuning.cinematicPost.saturation = SliderToFloat(g_gfx.saturation);
    g_gfx.tuning.cinematicPost.vignette = SliderToFloat(g_gfx.vignette);
    g_gfx.tuning.cinematicPost.lensDirt = SliderToFloat(g_gfx.lensDirt);
    g_gfx.tuning.cinematicPost.motionBlur = SliderToFloat(g_gfx.motionBlur);
    g_gfx.tuning.cinematicPost.depthOfField = SliderToFloat(g_gfx.depthOfField);
    g_gfx.tuning.particles.densityScale = SliderToFloat(g_gfx.particleDensity);
    g_gfx.tuning.particles.qualityScale = SliderToFloat(g_gfx.particleQuality);
    g_gfx.tuning.particles.bloomContribution = SliderToFloat(g_gfx.particleBloom);
    g_gfx.tuning.particles.softDepthFade = SliderToFloat(g_gfx.particleSoftDepth);
    g_gfx.tuning.particles.windInfluence = SliderToFloat(g_gfx.particleWind);
}

void ApplyTuningState(bool markColorGradeCustom = false) {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (!renderer || renderer->IsDeviceRemoved()) {
        return;
    }
    SyncStateFromToggles();
    SyncStateFromSliders();
    SyncSelectedParticleEffectToState();
    if (markColorGradeCustom) {
        g_gfx.tuning.cinematicPost.colorGradePreset = "custom";
    }
    g_gfx.tuning.quality.dirtyFromUI = true;
    Graphics::ApplyRendererTuningState(*renderer, g_gfx.tuning);
    if (auto* engine = Cortex::ServiceLocator::GetEngine()) {
        engine->ApplyParticleEffectPresetToScene(g_gfx.tuning.particles.effectPreset);
    }
    g_gfx.tuning = Graphics::CaptureRendererTuningState(*renderer);
}

void ApplyLightingRigFromGraphicsUI(Graphics::Renderer::LightingRig rig) {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    auto* engine = Cortex::ServiceLocator::GetEngine();
    if (!renderer || renderer->IsDeviceRemoved() || !engine) {
        return;
    }
    Graphics::ApplyLightingRigControl(*renderer, rig, engine->GetRegistry());
    g_gfx.tuning = Graphics::CaptureRendererTuningState(*renderer);
}

void ApplyColorGradePresetFromGraphicsUI(const char* preset) {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (!renderer || renderer->IsDeviceRemoved()) {
        return;
    }
    SyncStateFromToggles();
    SyncStateFromSliders();
    SyncSelectedParticleEffectToState();
    g_gfx.tuning.cinematicPost.colorGradePreset = preset ? preset : "neutral";
    g_gfx.tuning.quality.dirtyFromUI = true;
    Graphics::ApplyRendererTuningState(*renderer, g_gfx.tuning);
    g_gfx.tuning = Graphics::CaptureRendererTuningState(*renderer);
}

void ApplyToneMapperPresetFromGraphicsUI(const char* preset) {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (!renderer || renderer->IsDeviceRemoved()) {
        return;
    }
    SyncStateFromToggles();
    SyncStateFromSliders();
    SyncSelectedParticleEffectToState();
    g_gfx.tuning.cinematicPost.toneMapperPreset = preset ? preset : "aces";
    g_gfx.tuning.quality.dirtyFromUI = true;
    Graphics::ApplyRendererTuningState(*renderer, g_gfx.tuning);
    g_gfx.tuning = Graphics::CaptureRendererTuningState(*renderer);
}

void RefreshControlsFromRenderer() {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    if (!renderer || !g_gfx.hwnd) {
        return;
    }

    g_gfx.tuning = Graphics::CaptureRendererTuningState(*renderer);

    SetSliderFromFloat(g_gfx.renderScale, g_gfx.tuning.quality.renderScale);
    SetSliderFromFloat(g_gfx.exposure, g_gfx.tuning.lighting.exposure);
    SetSliderFromFloat(g_gfx.bloom, g_gfx.tuning.lighting.bloomIntensity);
    SetSliderFromFloat(g_gfx.warm, g_gfx.tuning.lighting.warm);
    SetSliderFromFloat(g_gfx.cool, g_gfx.tuning.lighting.cool);
    SetSliderFromFloat(g_gfx.sun, g_gfx.tuning.lighting.sunIntensity);
    SetSliderFromFloat(g_gfx.godRays, g_gfx.tuning.lighting.godRayIntensity);
    SetSliderFromFloat(g_gfx.areaLight, g_gfx.tuning.lighting.areaLightSizeScale);
    SetSliderFromFloat(g_gfx.iblDiffuse, g_gfx.tuning.environment.diffuseIntensity);
    SetSliderFromFloat(g_gfx.iblSpecular, g_gfx.tuning.environment.specularIntensity);
    SetSliderFromFloat(g_gfx.backgroundExposure, g_gfx.tuning.environment.backgroundExposure);
    SetSliderFromFloat(g_gfx.backgroundBlur, g_gfx.tuning.environment.backgroundBlur);
    SetSliderFromFloat(g_gfx.environmentRotation, g_gfx.tuning.environment.rotationDegrees);
    SetSliderFromFloat(g_gfx.rtReflectionDenoise, g_gfx.tuning.rayTracing.reflectionDenoiseAlpha);
    SetSliderFromFloat(g_gfx.rtReflectionStrength, g_gfx.tuning.rayTracing.reflectionCompositionStrength);
    SetSliderFromFloat(g_gfx.rtReflectionRoughness, g_gfx.tuning.rayTracing.reflectionRoughnessThreshold);
    SetSliderFromFloat(g_gfx.rtReflectionHistoryClamp, g_gfx.tuning.rayTracing.reflectionHistoryMaxBlend);
    SetSliderFromFloat(g_gfx.rtReflectionFirefly, g_gfx.tuning.rayTracing.reflectionFireflyClampLuma);
    SetSliderFromFloat(g_gfx.rtReflectionScale, g_gfx.tuning.rayTracing.reflectionSignalScale);
    SetSliderFromFloat(g_gfx.rtGIStrength, g_gfx.tuning.rayTracing.giStrength);
    SetSliderFromFloat(g_gfx.rtGIDistance, g_gfx.tuning.rayTracing.giRayDistance);
    SetSliderFromFloat(g_gfx.ssrDistance, g_gfx.tuning.screenSpace.ssrMaxDistance);
    SetSliderFromFloat(g_gfx.ssrThickness, g_gfx.tuning.screenSpace.ssrThickness);
    SetSliderFromFloat(g_gfx.ssrStrength, g_gfx.tuning.screenSpace.ssrStrength);
    SetSliderFromFloat(g_gfx.ssaoRadius, g_gfx.tuning.screenSpace.ssaoRadius);
    SetSliderFromFloat(g_gfx.ssaoBias, g_gfx.tuning.screenSpace.ssaoBias);
    SetSliderFromFloat(g_gfx.ssaoIntensity, g_gfx.tuning.screenSpace.ssaoIntensity);
    SetSliderFromFloat(g_gfx.fogDensity, g_gfx.tuning.atmosphere.fogDensity);
    SetSliderFromFloat(g_gfx.fogHeight, g_gfx.tuning.atmosphere.fogHeight);
    SetSliderFromFloat(g_gfx.fogFalloff, g_gfx.tuning.atmosphere.fogFalloff);
    SetSliderFromFloat(g_gfx.waterWave, g_gfx.tuning.water.waveAmplitude);
    SetSliderFromFloat(g_gfx.waterLength, g_gfx.tuning.water.waveLength);
    SetSliderFromFloat(g_gfx.waterSpeed, g_gfx.tuning.water.waveSpeed);
    SetSliderFromFloat(g_gfx.waterSecondary, g_gfx.tuning.water.secondaryAmplitude);
    SetSliderFromFloat(g_gfx.waterRoughness, g_gfx.tuning.water.roughness);
    SetSliderFromFloat(g_gfx.waterFresnel, g_gfx.tuning.water.fresnelStrength);
    SetSliderFromFloat(g_gfx.bloomThreshold, g_gfx.tuning.cinematicPost.bloomThreshold);
    SetSliderFromFloat(g_gfx.bloomKnee, g_gfx.tuning.cinematicPost.bloomSoftKnee);
    SetSliderFromFloat(g_gfx.contrast, g_gfx.tuning.cinematicPost.contrast);
    SetSliderFromFloat(g_gfx.saturation, g_gfx.tuning.cinematicPost.saturation);
    SetSliderFromFloat(g_gfx.vignette, g_gfx.tuning.cinematicPost.vignette);
    SetSliderFromFloat(g_gfx.lensDirt, g_gfx.tuning.cinematicPost.lensDirt);
    SetSliderFromFloat(g_gfx.motionBlur, g_gfx.tuning.cinematicPost.motionBlur);
    SetSliderFromFloat(g_gfx.depthOfField, g_gfx.tuning.cinematicPost.depthOfField);
    SetSliderFromFloat(g_gfx.particleDensity, g_gfx.tuning.particles.densityScale);
    SetSliderFromFloat(g_gfx.particleQuality, g_gfx.tuning.particles.qualityScale);
    SetSliderFromFloat(g_gfx.particleBloom, g_gfx.tuning.particles.bloomContribution);
    SetSliderFromFloat(g_gfx.particleSoftDepth, g_gfx.tuning.particles.softDepthFade);
    SetSliderFromFloat(g_gfx.particleWind, g_gfx.tuning.particles.windInfluence);
    SyncParticleEffectComboFromRenderer();

    SetCheckbox(g_gfx.chkTAA, g_gfx.tuning.quality.taaEnabled);
    SetCheckbox(g_gfx.chkFXAA, g_gfx.tuning.quality.fxaaEnabled);
    SetCheckbox(g_gfx.chkGPUCulling, g_gfx.tuning.quality.gpuCullingEnabled);
    SetCheckbox(g_gfx.chkSafeLighting, g_gfx.tuning.quality.safeLightingRigOnLowVRAM);
    SetCheckbox(g_gfx.chkRT, g_gfx.tuning.rayTracing.enabled);
    SetCheckbox(g_gfx.chkRTReflections, g_gfx.tuning.rayTracing.reflectionsEnabled);
    SetCheckbox(g_gfx.chkRTGI, g_gfx.tuning.rayTracing.giEnabled);
    SetCheckbox(g_gfx.chkSSR, g_gfx.tuning.screenSpace.ssrEnabled);
    SetCheckbox(g_gfx.chkSSAO, g_gfx.tuning.screenSpace.ssaoEnabled);
    SetCheckbox(g_gfx.chkPCSS, g_gfx.tuning.screenSpace.pcssEnabled);
    SetCheckbox(g_gfx.chkIBL, g_gfx.tuning.environment.iblEnabled);
    SetCheckbox(g_gfx.chkIBLLimit, g_gfx.tuning.environment.iblLimitEnabled);
    SetCheckbox(g_gfx.chkBackgroundVisible, g_gfx.tuning.environment.backgroundVisible);
    SetCheckbox(g_gfx.chkFog, g_gfx.tuning.atmosphere.fogEnabled);
    SetCheckbox(g_gfx.chkParticles, g_gfx.tuning.particles.enabled);
    SetCheckbox(g_gfx.chkCinematicPost, g_gfx.tuning.cinematicPost.enabled);
    SyncEnvironmentComboFromRenderer();
}

void RefreshHealthLabels() {
    auto* renderer = Cortex::ServiceLocator::GetRenderer();
    auto* engine = Cortex::ServiceLocator::GetEngine();
    if (!renderer || !g_gfx.hwnd) {
        return;
    }

    const auto health = renderer->BuildHealthState();
    const float frameSeconds = engine ? engine->GetLastFrameTimeSeconds() : 0.0f;
    const double fps = frameSeconds > 0.0f ? (1.0 / static_cast<double>(frameSeconds)) : 0.0;
    constexpr double kToMB = 1.0 / (1024.0 * 1024.0);

    wchar_t buffer[512];
    if (g_gfx.txtHealth) {
        const std::wstring adapter = ToWide(health.adapterName);
        const std::wstring preset = ToWide(health.qualityPreset);
        const std::wstring env = ToWide(health.activeEnvironment);
        swprintf_s(buffer,
                   L"Health: %.1f FPS | %ls | preset=%ls | env=%ls | RT=%ls/%ls",
                   fps,
                   adapter.empty() ? L"adapter unknown" : adapter.c_str(),
                   preset.empty() ? L"unknown" : preset.c_str(),
                   env.empty() ? L"unknown" : env.c_str(),
                   health.rayTracingRequested ? L"requested" : L"off",
                   health.rayTracingEffective ? L"ready" : L"inactive");
        SetWindowTextW(g_gfx.txtHealth, buffer);
    }
    if (g_gfx.txtMemory) {
        swprintf_s(buffer,
                   L"Budgets: VRAM=%.0f MB | descriptors persistent=%u/%u transient=%u/%u | env resident=%u pending=%u",
                   static_cast<double>(health.estimatedVRAMBytes) * kToMB,
                   health.descriptorPersistentUsed,
                   health.descriptorPersistentBudget,
                   health.descriptorTransientUsed,
                   health.descriptorTransientBudget,
                   health.residentEnvironments,
                   health.pendingEnvironments);
        SetWindowTextW(g_gfx.txtMemory, buffer);
    }
    if (g_gfx.txtWarning) {
        const std::wstring code = ToWide(health.lastWarningCode);
        const std::wstring message = ToWide(health.lastWarningMessage);
        swprintf_s(buffer,
                   L"Warnings: frame=%u assets=%u | %ls%ls%ls",
                   health.frameWarnings,
                   health.assetFallbacks,
                   code.empty() ? L"none" : code.c_str(),
                   (!code.empty() && !message.empty()) ? L": " : L"",
                   message.empty() ? L"" : message.c_str());
        SetWindowTextW(g_gfx.txtWarning, buffer);
    }
    if (g_gfx.txtRTScheduler) {
        const auto& rt = renderer->GetFrameContract().rayTracing;
        const std::wstring profile = ToWide(rt.budgetProfile);
        const std::string reason = !rt.schedulerDisabledReason.empty()
            ? rt.schedulerDisabledReason
            : (!rt.reflectionReadinessReason.empty()
                ? rt.reflectionReadinessReason
                : (rt.dispatchReflections ? "ready" : "not_scheduled_this_frame"));
        const std::wstring reasonWide = ToWide(reason);
        swprintf_s(buffer,
                   L"RT Scheduler: %ls | TLAS %u/%u | shadows=%ls refl=%ls/%ls GI=%ls | reason=%ls",
                   profile.empty() ? L"profile unknown" : profile.c_str(),
                   rt.schedulerTLASCandidates,
                   rt.schedulerMaxTLASInstances,
                   rt.dispatchShadows ? L"scheduled" : L"skipped",
                   rt.dispatchReflections ? L"scheduled" : L"skipped",
                   rt.reflectionDispatchReady ? L"ready" : L"not ready",
                   rt.dispatchGI ? L"scheduled" : L"skipped",
                   reasonWide.empty() ? L"none" : reasonWide.c_str());
        SetWindowTextW(g_gfx.txtRTScheduler, buffer);
    }
}

void RegisterGraphicsSettingsClass() {
    static bool registered = false;
    if (registered) {
        return;
    }

    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_BAR_CLASSES;
    InitCommonControlsEx(&icc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = [](HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) -> LRESULT {
        switch (msg) {
        case WM_CREATE: {
            g_gfx.font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));

            RECT rc{};
            GetClientRect(hwnd, &rc);
            const int width = rc.right - rc.left;
            const int margin = 10;
            const int labelHeight = 18;
            const int sliderHeight = 26;
            const int rowGap = 5;
            const int checkHeight = 20;
            const int labelWidth = 160;
            const int sliderWidth = width - labelWidth - margin * 2;
            int y = margin;

            auto setFont = [](HWND h) {
                if (h) {
                    SendMessageW(h, WM_SETFONT, reinterpret_cast<WPARAM>(g_gfx.font), TRUE);
                }
            };

            auto makeStaticWithHeight = [&](int id, const wchar_t* text, int yy, int height) {
                HWND h = CreateWindowExW(0,
                                         L"STATIC",
                                         text,
                                         WS_CHILD | WS_VISIBLE,
                                         margin,
                                         yy,
                                         width - margin * 2,
                                         height,
                                         hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                         nullptr,
                                         nullptr);
                setFont(h);
                return h;
            };
            auto makeStatic = [&](int id, const wchar_t* text, int yy) {
                return makeStaticWithHeight(id, text, yy, labelHeight);
            };

            auto makeSection = [&](const wchar_t* text) {
                y += rowGap;
                makeStaticWithHeight(0, text, y, labelHeight);
                y += labelHeight + 2;
            };

            auto makeSlider = [&](int id, const wchar_t* text, SliderBinding& binding, float minValue, float maxValue) {
                makeStatic(0, text, y);
                HWND h = CreateWindowExW(0,
                                         TRACKBAR_CLASSW,
                                         L"",
                                         WS_CHILD | WS_VISIBLE | TBS_AUTOTICKS,
                                         margin + labelWidth,
                                         y - 2,
                                         sliderWidth,
                                         sliderHeight,
                                         hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                         nullptr,
                                         nullptr);
                SendMessageW(h, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
                binding = SliderBinding{h, minValue, maxValue};
                y += sliderHeight + rowGap;
                return h;
            };

            auto makeCheckbox = [&](int id, const wchar_t* text) {
                HWND h = CreateWindowExW(0,
                                         L"BUTTON",
                                         text,
                                         WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                                         margin,
                                         y,
                                         (width - margin * 2) / 2,
                                         checkHeight,
                                         hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                         nullptr,
                                         nullptr);
                setFont(h);
                y += checkHeight + rowGap;
                return h;
            };

            auto makeButton = [&](int id, const wchar_t* text, int x, int yy, int buttonWidth) {
                HWND h = CreateWindowExW(0,
                                         L"BUTTON",
                                         text,
                                         WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                                         x,
                                         yy,
                                         buttonWidth,
                                         24,
                                         hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                         nullptr,
                                         nullptr);
                setFont(h);
                return h;
            };
            auto makeCombo = [&](int id, const wchar_t* text) {
                makeStatic(0, text, y);
                HWND h = CreateWindowExW(0,
                                         L"COMBOBOX",
                                         L"",
                                         WS_CHILD | WS_VISIBLE | CBS_DROPDOWNLIST | WS_VSCROLL,
                                         margin + labelWidth,
                                         y - 3,
                                         sliderWidth,
                                         180,
                                         hwnd,
                                         reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                         nullptr,
                                         nullptr);
                setFont(h);
                y += 26 + rowGap;
                return h;
            };

            g_gfx.txtHealth = makeStatic(IDC_GFX_HEALTH, L"Health: --", y);
            y += labelHeight + rowGap;
            g_gfx.txtMemory = makeStatic(IDC_GFX_MEMORY, L"Budgets: --", y);
            y += labelHeight + rowGap;
            g_gfx.txtWarning = makeStatic(IDC_GFX_WARNING, L"Warnings: --", y);
            y += labelHeight + rowGap;

            makeSection(L"Quality");
            makeSlider(IDC_GFX_RENDER_SCALE, L"Render Scale", g_gfx.renderScale, 0.5f, 1.0f);
            g_gfx.chkTAA = makeCheckbox(IDC_GFX_TAA, L"TAA");
            g_gfx.chkFXAA = makeCheckbox(IDC_GFX_FXAA, L"FXAA");
            g_gfx.chkGPUCulling = makeCheckbox(IDC_GFX_GPU_CULLING, L"GPU Culling");
            g_gfx.chkSafeLighting = makeCheckbox(IDC_GFX_SAFE_LIGHTING, L"Safe Lighting");

            makeSection(L"Ray Tracing");
            g_gfx.chkRT = makeCheckbox(IDC_GFX_RT, L"RT Master");
            g_gfx.chkRTReflections = makeCheckbox(IDC_GFX_RT_REFLECTIONS, L"RT Reflections");
            g_gfx.chkRTGI = makeCheckbox(IDC_GFX_RT_GI, L"RT GI");
            makeSlider(IDC_GFX_RT_REFL_DENOISE, L"Reflection Denoise", g_gfx.rtReflectionDenoise, 0.02f, 1.0f);
            makeSlider(IDC_GFX_RT_REFL_STRENGTH, L"Reflection Strength", g_gfx.rtReflectionStrength, 0.0f, 1.0f);
            makeSlider(IDC_GFX_RT_REFL_ROUGHNESS, L"Reflection Roughness", g_gfx.rtReflectionRoughness, 0.05f, 1.0f);
            makeSlider(IDC_GFX_RT_REFL_HISTORY_CLAMP, L"History Clamp", g_gfx.rtReflectionHistoryClamp, 0.0f, 0.5f);
            makeSlider(IDC_GFX_RT_REFL_FIREFLY, L"Firefly Clamp", g_gfx.rtReflectionFirefly, 4.0f, 32.0f);
            makeSlider(IDC_GFX_RT_REFL_SCALE, L"Reflection Scale", g_gfx.rtReflectionScale, 0.0f, 2.0f);
            makeSlider(IDC_GFX_RT_GI_STRENGTH, L"GI Strength", g_gfx.rtGIStrength, 0.0f, 1.0f);
            makeSlider(IDC_GFX_RT_GI_DISTANCE, L"GI Ray Distance", g_gfx.rtGIDistance, 0.5f, 20.0f);
            g_gfx.txtRTScheduler = makeStaticWithHeight(
                IDC_GFX_RT_SCHEDULER,
                L"RT Scheduler: --",
                y,
                labelHeight * 2);
            y += labelHeight * 2 + rowGap;

            makeSection(L"Lighting");
            makeSlider(IDC_GFX_EXPOSURE, L"Exposure", g_gfx.exposure, 0.05f, 5.0f);
            makeSlider(IDC_GFX_BLOOM, L"Bloom", g_gfx.bloom, 0.0f, 2.0f);
            makeSlider(IDC_GFX_WARM, L"Warm Grade", g_gfx.warm, -1.0f, 1.0f);
            makeSlider(IDC_GFX_COOL, L"Cool Grade", g_gfx.cool, -1.0f, 1.0f);
            makeSlider(IDC_GFX_SUN, L"Sun Intensity", g_gfx.sun, 0.0f, 20.0f);
            makeSlider(IDC_GFX_GOD_RAYS, L"God Rays", g_gfx.godRays, 0.0f, 3.0f);
            makeSlider(IDC_GFX_AREA_LIGHT, L"Area Light Size", g_gfx.areaLight, 0.25f, 2.0f);
            {
                const int buttonWidth = (width - margin * 2 - 18) / 4;
                makeButton(IDC_GFX_RIG_STUDIO, L"Studio", margin, y, buttonWidth);
                makeButton(IDC_GFX_RIG_WAREHOUSE, L"Top Down", margin + buttonWidth + 6, y, buttonWidth);
                makeButton(IDC_GFX_RIG_SIDE, L"Side", margin + (buttonWidth + 6) * 2, y, buttonWidth);
                makeButton(IDC_GFX_RIG_LANTERNS, L"Lanterns", margin + (buttonWidth + 6) * 3, y, buttonWidth);
                y += 24 + rowGap;
            }

            makeSection(L"Environment / IBL");
            g_gfx.cmbEnvironment = makeCombo(IDC_GFX_ENV_SELECT, L"Environment");
            LoadEnvironmentOptions();
            g_gfx.chkIBL = makeCheckbox(IDC_GFX_IBL, L"IBL Enabled");
            g_gfx.chkIBLLimit = makeCheckbox(IDC_GFX_IBL_LIMIT, L"IBL Residency Limit");
            g_gfx.chkBackgroundVisible = makeCheckbox(IDC_GFX_BACKGROUND_VISIBLE, L"Background Visible");
            makeSlider(IDC_GFX_IBL_DIFFUSE, L"IBL Diffuse", g_gfx.iblDiffuse, 0.0f, 3.0f);
            makeSlider(IDC_GFX_IBL_SPECULAR, L"IBL Specular", g_gfx.iblSpecular, 0.0f, 3.0f);
            makeSlider(IDC_GFX_BACKGROUND_EXPOSURE, L"Background Exposure", g_gfx.backgroundExposure, 0.0f, 4.0f);
            makeSlider(IDC_GFX_BACKGROUND_BLUR, L"Background Blur", g_gfx.backgroundBlur, 0.0f, 1.0f);
            makeSlider(IDC_GFX_ENV_ROTATION, L"Environment Rotation", g_gfx.environmentRotation, 0.0f, 359.0f);
            {
                const int buttonWidth = (width - margin * 2 - 18) / 4;
                makeButton(IDC_GFX_ENV_NEXT, L"Next Env", margin, y, buttonWidth);
                makeButton(IDC_GFX_ENV_REAPPLY, L"Reapply", margin + buttonWidth + 6, y, buttonWidth);
                makeButton(IDC_GFX_ENV_ONE, L"Load One", margin + (buttonWidth + 6) * 2, y, buttonWidth);
                makeButton(IDC_GFX_ENV_ALL, L"Load All", margin + (buttonWidth + 6) * 3, y, buttonWidth);
                y += 24 + rowGap;
            }

            makeSection(L"Screen Space / Atmosphere");
            g_gfx.chkSSR = makeCheckbox(IDC_GFX_SSR, L"SSR");
            g_gfx.chkSSAO = makeCheckbox(IDC_GFX_SSAO, L"SSAO");
            g_gfx.chkPCSS = makeCheckbox(IDC_GFX_PCSS, L"PCSS Shadows");
            g_gfx.chkFog = makeCheckbox(IDC_GFX_FOG, L"Fog");
            makeSlider(IDC_GFX_SSR_DISTANCE, L"SSR Max Distance", g_gfx.ssrDistance, 1.0f, 120.0f);
            makeSlider(IDC_GFX_SSR_THICKNESS, L"SSR Thickness", g_gfx.ssrThickness, 0.005f, 1.0f);
            makeSlider(IDC_GFX_SSR_STRENGTH, L"SSR Strength", g_gfx.ssrStrength, 0.0f, 1.0f);
            makeSlider(IDC_GFX_SSAO_RADIUS, L"SSAO Radius", g_gfx.ssaoRadius, 0.01f, 5.0f);
            makeSlider(IDC_GFX_SSAO_BIAS, L"SSAO Bias", g_gfx.ssaoBias, 0.0f, 0.1f);
            makeSlider(IDC_GFX_SSAO_INTENSITY, L"SSAO Intensity", g_gfx.ssaoIntensity, 0.0f, 5.0f);
            makeSlider(IDC_GFX_FOG_DENSITY, L"Fog Density", g_gfx.fogDensity, 0.0f, 0.1f);
            makeSlider(IDC_GFX_FOG_HEIGHT, L"Fog Height", g_gfx.fogHeight, -100.0f, 100.0f);
            makeSlider(IDC_GFX_FOG_FALLOFF, L"Fog Falloff", g_gfx.fogFalloff, 0.01f, 10.0f);

            makeSection(L"Showcase Effects");
            g_gfx.chkParticles = makeCheckbox(IDC_GFX_PARTICLES, L"Particles");
            g_gfx.cmbParticleEffect = makeCombo(IDC_GFX_PARTICLE_EFFECT_SELECT, L"Particle Effect");
            LoadParticleEffectOptions();
            makeSlider(IDC_GFX_PARTICLE_DENSITY, L"Particle Density", g_gfx.particleDensity, 0.0f, 2.0f);
            makeSlider(IDC_GFX_PARTICLE_QUALITY, L"Particle Quality", g_gfx.particleQuality, 0.25f, 2.0f);
            makeSlider(IDC_GFX_PARTICLE_BLOOM, L"Particle Bloom", g_gfx.particleBloom, 0.0f, 2.0f);
            makeSlider(IDC_GFX_PARTICLE_SOFT_DEPTH, L"Particle Soft Depth", g_gfx.particleSoftDepth, 0.0f, 1.0f);
            makeSlider(IDC_GFX_PARTICLE_WIND, L"Particle Wind", g_gfx.particleWind, 0.0f, 2.0f);
            makeSlider(IDC_GFX_WATER_WAVE, L"Water Wave Amp", g_gfx.waterWave, 0.0f, 2.0f);
            makeSlider(IDC_GFX_WATER_LENGTH, L"Water Wavelength", g_gfx.waterLength, 0.1f, 100.0f);
            makeSlider(IDC_GFX_WATER_SPEED, L"Water Speed", g_gfx.waterSpeed, 0.0f, 20.0f);
            makeSlider(IDC_GFX_WATER_SECONDARY, L"Water Secondary", g_gfx.waterSecondary, 0.0f, 2.0f);
            makeSlider(IDC_GFX_WATER_ROUGHNESS, L"Water Roughness", g_gfx.waterRoughness, 0.01f, 1.0f);
            makeSlider(IDC_GFX_WATER_FRESNEL, L"Water Fresnel", g_gfx.waterFresnel, 0.0f, 3.0f);
            g_gfx.chkCinematicPost = makeCheckbox(IDC_GFX_CINEMATIC_POST, L"Cinematic Post");
            makeSlider(IDC_GFX_BLOOM_THRESHOLD, L"Bloom Threshold", g_gfx.bloomThreshold, 0.1f, 5.0f);
            makeSlider(IDC_GFX_BLOOM_KNEE, L"Bloom Soft Knee", g_gfx.bloomKnee, 0.0f, 1.0f);
            makeSlider(IDC_GFX_CONTRAST, L"Contrast", g_gfx.contrast, 0.5f, 1.5f);
            makeSlider(IDC_GFX_SATURATION, L"Saturation", g_gfx.saturation, 0.0f, 2.0f);
            {
                const int buttonWidth = (width - margin * 2 - 18) / 4;
                makeButton(IDC_GFX_TONE_ACES, L"ACES", margin, y, buttonWidth);
                makeButton(IDC_GFX_TONE_REINHARD, L"Reinhard", margin + buttonWidth + 6, y, buttonWidth);
                makeButton(IDC_GFX_TONE_SOFT, L"Soft", margin + (buttonWidth + 6) * 2, y, buttonWidth);
                makeButton(IDC_GFX_TONE_PUNCHY, L"Punchy", margin + (buttonWidth + 6) * 3, y, buttonWidth);
                y += 24 + rowGap;
            }
            {
                const int buttonWidth = (width - margin * 2 - 18) / 4;
                makeButton(IDC_GFX_GRADE_NEUTRAL, L"Neutral", margin, y, buttonWidth);
                makeButton(IDC_GFX_GRADE_WARM_FILM, L"Warm Film", margin + buttonWidth + 6, y, buttonWidth);
                makeButton(IDC_GFX_GRADE_COOL_MOON, L"Cool Moon", margin + (buttonWidth + 6) * 2, y, buttonWidth);
                makeButton(IDC_GFX_GRADE_BLEACH, L"Bleach", margin + (buttonWidth + 6) * 3, y, buttonWidth);
                y += 24 + rowGap;
            }
            makeSlider(IDC_GFX_VIGNETTE, L"Vignette", g_gfx.vignette, 0.0f, 1.0f);
            makeSlider(IDC_GFX_LENS_DIRT, L"Lens Dirt", g_gfx.lensDirt, 0.0f, 1.0f);
            makeSlider(IDC_GFX_MOTION_BLUR, L"Motion Blur", g_gfx.motionBlur, 0.0f, 1.0f);
            makeSlider(IDC_GFX_DOF, L"Depth of Field", g_gfx.depthOfField, 0.0f, 1.0f);
            {
                const int buttonWidth = (width - margin * 2 - 12) / 3;
                makeButton(IDC_GFX_BOOKMARK_HERO, L"Hero View", margin, y, buttonWidth);
                makeButton(IDC_GFX_BOOKMARK_REFLECTION, L"Reflection", margin + buttonWidth + 6, y, buttonWidth);
                makeButton(IDC_GFX_BOOKMARK_MATERIALS, L"Materials", margin + (buttonWidth + 6) * 2, y, buttonWidth);
                y += 24 + rowGap;
            }

            makeSection(L"Actions");
            {
                const int buttonWidth = (width - margin * 2 - 12) / 3;
                makeButton(IDC_GFX_SAFE_PRESET, L"Safe Preset", margin, y, buttonWidth);
                makeButton(IDC_GFX_HERO_BASELINE, L"Hero Baseline", margin + buttonWidth + 6, y, buttonWidth);
                makeButton(IDC_GFX_RESET_FROM_RENDERER, L"Refresh", margin + (buttonWidth + 6) * 2, y, buttonWidth);
                y += 24 + rowGap;
            }
            {
                const int buttonWidth = (width - margin * 2 - 6) / 2;
                makeButton(IDC_GFX_SAVE, L"Save Settings", margin, y, buttonWidth);
                makeButton(IDC_GFX_LOAD, L"Load Settings", margin + buttonWidth + 6, y, buttonWidth);
                y += 24 + rowGap;
            }

            g_gfx.contentHeight = y + margin;
            g_gfx.scrollPos = 0;

            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
            si.nMin = 0;
            si.nMax = std::max(g_gfx.contentHeight - 1, 0);
            si.nPage = static_cast<UINT>(std::max(rc.bottom - rc.top, 1L));
            si.nPos = 0;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);

            SetTimer(hwnd, 1, 500, nullptr);
            RefreshControlsFromRenderer();
            RefreshHealthLabels();
            return 0;
        }
        case WM_SIZE: {
            RECT rc{};
            GetClientRect(hwnd, &rc);
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_PAGE | SIF_RANGE | SIF_POS;
            si.nMin = 0;
            si.nMax = std::max(g_gfx.contentHeight - 1, 0);
            si.nPage = static_cast<UINT>(std::max(rc.bottom - rc.top, 1L));
            si.nPos = g_gfx.scrollPos;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            return 0;
        }
        case WM_VSCROLL: {
            SCROLLINFO si{};
            si.cbSize = sizeof(si);
            si.fMask = SIF_ALL;
            GetScrollInfo(hwnd, SB_VERT, &si);
            int yPos = si.nPos;
            switch (LOWORD(wParam)) {
            case SB_LINEUP: yPos -= 20; break;
            case SB_LINEDOWN: yPos += 20; break;
            case SB_PAGEUP: yPos -= static_cast<int>(si.nPage); break;
            case SB_PAGEDOWN: yPos += static_cast<int>(si.nPage); break;
            case SB_THUMBTRACK:
            case SB_THUMBPOSITION: yPos = si.nTrackPos; break;
            default: break;
            }
            yPos = std::clamp(yPos, si.nMin, std::max(si.nMin, static_cast<int>(si.nMax - si.nPage + 1)));
            si.fMask = SIF_POS;
            si.nPos = yPos;
            SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
            const int dy = g_gfx.scrollPos - yPos;
            if (dy != 0) {
                ScrollWindowEx(hwnd, 0, dy, nullptr, nullptr, nullptr, nullptr, SW_INVALIDATE | SW_SCROLLCHILDREN);
                g_gfx.scrollPos = yPos;
            }
            return 0;
        }
        case WM_TIMER:
            if (wParam == 1) {
                RefreshHealthLabels();
            }
            return 0;
        case WM_HSCROLL: {
            const int scrollCode = LOWORD(wParam);
            if (scrollCode == TB_ENDTRACK || scrollCode == TB_THUMBPOSITION) {
                const HWND source = reinterpret_cast<HWND>(lParam);
                const bool colorGradeSlider =
                    source == g_gfx.warm.hwnd ||
                    source == g_gfx.cool.hwnd ||
                    source == g_gfx.contrast.hwnd ||
                    source == g_gfx.saturation.hwnd;
                ApplyTuningState(colorGradeSlider);
                RefreshControlsFromRenderer();
                RefreshHealthLabels();
            }
            return 0;
        }
        case WM_COMMAND: {
            if (LOWORD(wParam) == IDC_GFX_ENV_SELECT && HIWORD(wParam) == CBN_SELCHANGE) {
                ApplySelectedEnvironmentFromGraphicsUI();
                RefreshControlsFromRenderer();
                RefreshHealthLabels();
                return 0;
            }
            if (LOWORD(wParam) == IDC_GFX_PARTICLE_EFFECT_SELECT && HIWORD(wParam) == CBN_SELCHANGE) {
                ApplySelectedParticleEffectFromGraphicsUI();
                RefreshControlsFromRenderer();
                RefreshHealthLabels();
                return 0;
            }
            if (HIWORD(wParam) != BN_CLICKED) {
                break;
            }

            auto* renderer = Cortex::ServiceLocator::GetRenderer();
            if (!renderer) {
                return 0;
            }

            switch (LOWORD(wParam)) {
            case IDC_GFX_SAFE_PRESET:
                Graphics::ApplySafeQualityPresetControl(*renderer);
                break;
            case IDC_GFX_HERO_BASELINE:
                Graphics::ApplyHeroVisualBaselineControls(*renderer);
                break;
            case IDC_GFX_RESET_FROM_RENDERER:
                break;
            case IDC_GFX_ENV_NEXT:
                Graphics::CycleEnvironmentPresetControl(*renderer);
                break;
            case IDC_GFX_ENV_REAPPLY:
                ApplySelectedEnvironmentFromGraphicsUI();
                break;
            case IDC_GFX_ENV_ONE:
                Graphics::ApplyEnvironmentResidencyLoadControl(*renderer, 1);
                break;
            case IDC_GFX_ENV_ALL:
                Graphics::ApplyEnvironmentResidencyLoadControl(*renderer, 64);
                break;
            case IDC_GFX_RIG_STUDIO:
                ApplyLightingRigFromGraphicsUI(Graphics::Renderer::LightingRig::StudioThreePoint);
                break;
            case IDC_GFX_RIG_WAREHOUSE:
                ApplyLightingRigFromGraphicsUI(Graphics::Renderer::LightingRig::TopDownWarehouse);
                break;
            case IDC_GFX_RIG_SIDE:
                ApplyLightingRigFromGraphicsUI(Graphics::Renderer::LightingRig::HorrorSideLight);
                break;
            case IDC_GFX_RIG_LANTERNS:
                ApplyLightingRigFromGraphicsUI(Graphics::Renderer::LightingRig::StreetLanterns);
                break;
            case IDC_GFX_GRADE_NEUTRAL:
                ApplyColorGradePresetFromGraphicsUI("neutral");
                break;
            case IDC_GFX_GRADE_WARM_FILM:
                ApplyColorGradePresetFromGraphicsUI("warm_film");
                break;
            case IDC_GFX_GRADE_COOL_MOON:
                ApplyColorGradePresetFromGraphicsUI("cool_moon");
                break;
            case IDC_GFX_GRADE_BLEACH:
                ApplyColorGradePresetFromGraphicsUI("bleach_bypass");
                break;
            case IDC_GFX_TONE_ACES:
                ApplyToneMapperPresetFromGraphicsUI("aces");
                break;
            case IDC_GFX_TONE_REINHARD:
                ApplyToneMapperPresetFromGraphicsUI("reinhard");
                break;
            case IDC_GFX_TONE_SOFT:
                ApplyToneMapperPresetFromGraphicsUI("filmic_soft");
                break;
            case IDC_GFX_TONE_PUNCHY:
                ApplyToneMapperPresetFromGraphicsUI("punchy");
                break;
            case IDC_GFX_BOOKMARK_HERO:
            case IDC_GFX_BOOKMARK_REFLECTION:
            case IDC_GFX_BOOKMARK_MATERIALS: {
                auto* engine = Cortex::ServiceLocator::GetEngine();
                if (!engine) {
                    break;
                }
                const char* bookmark =
                    (LOWORD(wParam) == IDC_GFX_BOOKMARK_HERO) ? "hero" :
                    (LOWORD(wParam) == IDC_GFX_BOOKMARK_REFLECTION) ? "reflection_closeup" :
                                                                       "material_overview";
                if (engine->ApplyShowcaseCameraBookmark(bookmark)) {
                    const std::wstring status = L"Camera bookmark applied: " + ToWide(bookmark);
                    SetWindowTextW(g_gfx.txtWarning, status.c_str());
                } else {
                    const std::wstring status = L"Camera bookmark unavailable: " + ToWide(bookmark);
                    SetWindowTextW(g_gfx.txtWarning, status.c_str());
                }
                break;
            }
            case IDC_GFX_SAVE: {
                SyncStateFromToggles();
                SyncStateFromSliders();
                SyncSelectedParticleEffectToState();
                std::string error;
                const auto path = Graphics::GetDefaultRendererTuningStatePath();
                if (Graphics::SaveRendererTuningStateFile(path, g_gfx.tuning, &error)) {
                    SetWindowTextW(g_gfx.txtWarning, L"Settings saved to user/graphics_settings.json");
                } else {
                    const std::wstring status = L"Settings save failed: " + ToWide(error);
                    SetWindowTextW(g_gfx.txtWarning, status.c_str());
                }
                break;
            }
            case IDC_GFX_LOAD: {
                std::string error;
                const auto loaded = Graphics::LoadRendererTuningStateFile(
                    Graphics::GetDefaultRendererTuningStatePath(),
                    &error);
                if (loaded) {
                    Graphics::ApplyRendererTuningState(*renderer, *loaded);
                    if (auto* engine = Cortex::ServiceLocator::GetEngine()) {
                        engine->ApplyParticleEffectPresetToScene(loaded->particles.effectPreset);
                    }
                    SetWindowTextW(g_gfx.txtWarning, L"Settings loaded from user/graphics_settings.json");
                } else {
                    const std::wstring status = error.empty()
                        ? L"No saved graphics settings found"
                        : (L"Settings load failed: " + ToWide(error));
                    SetWindowTextW(g_gfx.txtWarning, status.c_str());
                }
                break;
            }
            default:
                ApplyTuningState();
                break;
            }

            RefreshControlsFromRenderer();
            RefreshHealthLabels();
            return 0;
        }
        case WM_CLOSE:
            GraphicsSettingsWindow::SetVisible(false);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            g_gfx.hwnd = nullptr;
            g_gfx.visible = false;
            return 0;
        }

        return DefWindowProcW(hwnd, msg, wParam, lParam);
    };

    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kGraphicsSettingsClassName;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);
    registered = true;
}

void EnsureWindowCreated() {
    if (!g_gfx.initialized || g_gfx.hwnd) {
        return;
    }

    RegisterGraphicsSettingsClass();

    const int width = 620;
    const int height = 700;
    int x = (GetSystemMetrics(SM_CXSCREEN) - width) / 2;
    int y = (GetSystemMetrics(SM_CYSCREEN) - height) / 2;
    if (g_gfx.parent) {
        RECT pr{};
        GetWindowRect(g_gfx.parent, &pr);
        x = pr.left + ((pr.right - pr.left) - width) / 2;
        y = pr.top + ((pr.bottom - pr.top) - height) / 2;
    }

    g_gfx.hwnd = CreateWindowExW(WS_EX_TOOLWINDOW,
                                 kGraphicsSettingsClassName,
                                 L"Cortex Graphics Settings",
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VSCROLL,
                                 x,
                                 y,
                                 width,
                                 height,
                                 g_gfx.parent,
                                 nullptr,
                                 GetModuleHandleW(nullptr),
                                 nullptr);

    if (g_gfx.hwnd) {
        ShowWindow(g_gfx.hwnd, SW_HIDE);
        UpdateWindow(g_gfx.hwnd);
    }
}

} // namespace

void GraphicsSettingsWindow::Initialize(HWND parent) {
    g_gfx.parent = parent;
    g_gfx.initialized = true;
}

void GraphicsSettingsWindow::Shutdown() {
    if (g_gfx.hwnd) {
        DestroyWindow(g_gfx.hwnd);
        g_gfx.hwnd = nullptr;
    }
    g_gfx = GraphicsSettingsState{};
}

void GraphicsSettingsWindow::SetVisible(bool visible) {
    if (!g_gfx.initialized) {
        return;
    }

    EnsureWindowCreated();
    if (!g_gfx.hwnd) {
        return;
    }

    if (visible) {
        RefreshControlsFromRenderer();
        RefreshHealthLabels();
        ShowWindow(g_gfx.hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(g_gfx.hwnd);
        g_gfx.visible = true;
    } else {
        ShowWindow(g_gfx.hwnd, SW_HIDE);
        g_gfx.visible = false;
    }
}

void GraphicsSettingsWindow::Toggle() {
    if (!g_gfx.initialized) {
        return;
    }
    SetVisible(!g_gfx.visible);
}

bool GraphicsSettingsWindow::IsVisible() {
    return g_gfx.visible;
}

} // namespace Cortex::UI
