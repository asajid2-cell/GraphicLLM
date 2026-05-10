#include "Graphics/BudgetPlanner.h"

#include <algorithm>
#include <cstdlib>
#include <string>

namespace Cortex::Graphics {

namespace {

constexpr uint64_t kMiB = 1024ull * 1024ull;

RendererBudgetProfile ResolveProfile(uint64_t dedicatedVideoMemoryBytes, bool& forced) {
    forced = false;

    auto parseOverride = [&](const char* value) -> RendererBudgetProfile {
        const std::string profile = value ? value : "";
        forced = true;
        if (profile == "2gb" || profile == "2gb_ultra_low" || profile == "ultra_low") {
            return RendererBudgetProfile::UltraLow2GB;
        }
        if (profile == "4gb" || profile == "4gb_low" || profile == "low") {
            return RendererBudgetProfile::Low4GB;
        }
        if (profile == "8gb" || profile == "8gb_balanced" || profile == "balanced") {
            return RendererBudgetProfile::Balanced8GB;
        }
        if (profile == "high" || profile == "unlimited") {
            return RendererBudgetProfile::High;
        }
        forced = false;
        return RendererBudgetProfile::Balanced8GB;
    };

    if (const char* rendererOverride = std::getenv("CORTEX_RENDER_BUDGET_PROFILE")) {
        if (RendererBudgetProfile profile = parseOverride(rendererOverride); forced) {
            return profile;
        }
    }
    if (const char* rtOverride = std::getenv("CORTEX_RT_BUDGET_PROFILE")) {
        if (RendererBudgetProfile profile = parseOverride(rtOverride); forced) {
            return profile;
        }
    }

    const uint64_t dedicatedMiB = dedicatedVideoMemoryBytes / kMiB;
    if (dedicatedMiB > 0 && dedicatedMiB <= 2048ull) {
        return RendererBudgetProfile::UltraLow2GB;
    }
    if (dedicatedMiB > 0 && dedicatedMiB <= 4096ull) {
        return RendererBudgetProfile::Low4GB;
    }
    if (dedicatedMiB == 0 || dedicatedMiB <= 8192ull) {
        return RendererBudgetProfile::Balanced8GB;
    }
    return RendererBudgetProfile::High;
}

void ApplyProfile(RendererBudgetPlan& plan, RendererBudgetProfile profile) {
    plan.profile = profile;

    switch (profile) {
    case RendererBudgetProfile::UltraLow2GB:
        plan.profileName = "2gb_ultra_low";
        plan.targetRenderScale = 0.70f;
        plan.ssaoDivisor = 4;
        plan.shadowMapSize = 1024;
        plan.bloomLevels = 2;
        plan.iblResidentEnvironmentLimit = 1;
        plan.materialTextureMaxDimension = 1024;
        plan.materialTextureBudgetFloorDimension = 512;
        plan.textureBudgetBytes = 192ull * kMiB;
        plan.environmentBudgetBytes = 32ull * kMiB;
        plan.geometryBudgetBytes = 192ull * kMiB;
        plan.rtStructureBudgetBytes = 192ull * kMiB;
        plan.maxBLASBuildBytesPerFrame = 48ull * kMiB;
        plan.maxBLASTotalBytes = 192ull * kMiB;
        plan.maxTLASInstances = 512;
        plan.rtResolutionScale = 1.0f / 3.0f;
        plan.reflectionUpdateCadence = 3;
        plan.giUpdateCadence = 4;
        break;
    case RendererBudgetProfile::Low4GB:
        plan.profileName = "4gb_low";
        plan.targetRenderScale = 0.85f;
        plan.ssaoDivisor = 2;
        plan.shadowMapSize = 1536;
        plan.bloomLevels = 3;
        plan.iblResidentEnvironmentLimit = 1;
        plan.materialTextureMaxDimension = 2048;
        plan.materialTextureBudgetFloorDimension = 1024;
        plan.textureBudgetBytes = 384ull * kMiB;
        plan.environmentBudgetBytes = 64ull * kMiB;
        plan.geometryBudgetBytes = 384ull * kMiB;
        plan.rtStructureBudgetBytes = 384ull * kMiB;
        plan.maxBLASBuildBytesPerFrame = 96ull * kMiB;
        plan.maxBLASTotalBytes = 384ull * kMiB;
        plan.maxTLASInstances = 1024;
        plan.rtResolutionScale = 0.5f;
        plan.reflectionUpdateCadence = 2;
        plan.giUpdateCadence = 3;
        break;
    case RendererBudgetProfile::Balanced8GB:
        plan.profileName = plan.dedicatedVideoMemoryBytes == 0 ? "unknown_8gb_balanced" : "8gb_balanced";
        plan.targetRenderScale = 1.0f;
        plan.ssaoDivisor = 2;
        plan.shadowMapSize = 2048;
        plan.bloomLevels = 3;
        plan.iblResidentEnvironmentLimit = 1;
        plan.materialTextureMaxDimension = 4096;
        plan.materialTextureBudgetFloorDimension = 2048;
        plan.textureBudgetBytes = 512ull * kMiB;
        plan.environmentBudgetBytes = 128ull * kMiB;
        plan.geometryBudgetBytes = 512ull * kMiB;
        plan.rtStructureBudgetBytes = 768ull * kMiB;
        plan.maxBLASBuildBytesPerFrame = 192ull * kMiB;
        plan.maxBLASTotalBytes = 768ull * kMiB;
        plan.maxTLASInstances = 2048;
        plan.rtResolutionScale = 1.0f;
        plan.reflectionUpdateCadence = 1;
        plan.giUpdateCadence = 1;
        break;
    case RendererBudgetProfile::High:
    default:
        plan.profileName = "high";
        plan.targetRenderScale = 1.0f;
        plan.ssaoDivisor = 2;
        plan.shadowMapSize = 4096;
        plan.bloomLevels = 4;
        plan.iblResidentEnvironmentLimit = 4;
        plan.materialTextureMaxDimension = 8192;
        plan.materialTextureBudgetFloorDimension = 4096;
        plan.textureBudgetBytes = 1024ull * kMiB;
        plan.environmentBudgetBytes = 256ull * kMiB;
        plan.geometryBudgetBytes = 1024ull * kMiB;
        plan.rtStructureBudgetBytes = 1536ull * kMiB;
        plan.maxBLASBuildBytesPerFrame = 384ull * kMiB;
        plan.maxBLASTotalBytes = 1536ull * kMiB;
        plan.maxTLASInstances = 8192;
        plan.rtResolutionScale = 1.0f;
        plan.reflectionUpdateCadence = 1;
        plan.giUpdateCadence = 1;
        break;
    }
}

} // namespace

RendererBudgetPlan BudgetPlanner::BuildPlan(const RendererBudgetInputs& inputs) {
    RendererBudgetPlan plan{};
    plan.dedicatedVideoMemoryBytes = inputs.dedicatedVideoMemoryBytes;
    plan.maxRenderWidth = inputs.renderWidth;
    plan.maxRenderHeight = inputs.renderHeight;

    bool forced = false;
    const RendererBudgetProfile profile = ResolveProfile(inputs.dedicatedVideoMemoryBytes, forced);
    plan.forced = forced;
    ApplyProfile(plan, profile);

    if (plan.maxRenderWidth > 0 && plan.maxRenderHeight > 0 && plan.targetRenderScale < 1.0f) {
        plan.maxRenderWidth = std::max(1u, static_cast<uint32_t>(static_cast<float>(inputs.renderWidth) * plan.targetRenderScale));
        plan.maxRenderHeight = std::max(1u, static_cast<uint32_t>(static_cast<float>(inputs.renderHeight) * plan.targetRenderScale));
    }
    return plan;
}

RendererBudgetPlan BudgetPlanner::BuildPlan(uint64_t dedicatedVideoMemoryBytes,
                                            uint32_t renderWidth,
                                            uint32_t renderHeight) {
    RendererBudgetInputs inputs{};
    inputs.dedicatedVideoMemoryBytes = dedicatedVideoMemoryBytes;
    inputs.renderWidth = renderWidth;
    inputs.renderHeight = renderHeight;
    return BuildPlan(inputs);
}

} // namespace Cortex::Graphics
