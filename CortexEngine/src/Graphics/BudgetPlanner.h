#pragma once

#include <cstdint>
#include <string>

namespace Cortex::Graphics {

enum class RendererBudgetProfile : uint32_t {
    UltraLow2GB = 0,
    Low4GB,
    Balanced8GB,
    High
};

struct RendererBudgetInputs {
    uint64_t dedicatedVideoMemoryBytes = 0;
    uint32_t renderWidth = 0;
    uint32_t renderHeight = 0;
};

struct RendererBudgetPlan {
    RendererBudgetProfile profile = RendererBudgetProfile::Balanced8GB;
    std::string profileName = "8gb_balanced";
    bool forced = false;
    uint64_t dedicatedVideoMemoryBytes = 0;

    float targetRenderScale = 1.0f;
    uint32_t maxRenderWidth = 0;
    uint32_t maxRenderHeight = 0;
    uint32_t ssaoDivisor = 2;
    uint32_t shadowMapSize = 2048;
    uint32_t bloomLevels = 3;
    uint32_t iblResidentEnvironmentLimit = 2;
    uint32_t materialTextureMaxDimension = 4096;
    uint32_t materialTextureBudgetFloorDimension = 2048;

    uint64_t textureBudgetBytes = 512ull * 1024ull * 1024ull;
    uint64_t environmentBudgetBytes = 128ull * 1024ull * 1024ull;
    uint64_t geometryBudgetBytes = 512ull * 1024ull * 1024ull;
    uint64_t rtStructureBudgetBytes = 768ull * 1024ull * 1024ull;

    uint64_t maxBLASBuildBytesPerFrame = 192ull * 1024ull * 1024ull;
    uint64_t maxBLASTotalBytes = 768ull * 1024ull * 1024ull;
    uint32_t maxTLASInstances = 2048;
    float rtResolutionScale = 1.0f;
    uint32_t reflectionUpdateCadence = 1;
    uint32_t giUpdateCadence = 1;
    bool denoiseShadows = true;
    bool denoiseReflections = true;
    bool denoiseGI = true;
};

class BudgetPlanner {
public:
    [[nodiscard]] static RendererBudgetPlan BuildPlan(const RendererBudgetInputs& inputs);
    [[nodiscard]] static RendererBudgetPlan BuildPlan(uint64_t dedicatedVideoMemoryBytes,
                                                      uint32_t renderWidth,
                                                      uint32_t renderHeight);
};

} // namespace Cortex::Graphics
