#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "RHI/DX12Texture.h"
#include "RHI/DescriptorHeap.h"

namespace Cortex::Graphics {

struct EnvironmentMaps {
    std::string name;
    std::string path;
    std::string budgetClass = "small";
    uint32_t maxRuntimeDimension = 2048;
    float defaultDiffuseIntensity = 1.0f;
    float defaultSpecularIntensity = 1.0f;
    std::shared_ptr<DX12Texture> diffuseIrradiance;
    std::shared_ptr<DX12Texture> specularPrefiltered;
    DescriptorHandle diffuseIrradianceSRV{};
    DescriptorHandle specularPrefilteredSRV{};
};

struct PendingEnvironment {
    std::string path;
    std::string name;
    std::string budgetClass = "small";
    uint32_t maxRuntimeDimension = 2048;
    float defaultDiffuseIntensity = 1.0f;
    float defaultSpecularIntensity = 1.0f;
};

struct EnvironmentLightingState {
    static constexpr uint32_t kMaxIBLResident = 4;

    // Shadow + environment descriptor table (space1):
    //   t0 = shadow map array
    //   t1 = diffuse IBL
    //   t2 = specular IBL
    //   t3 = RT shadow mask (optional, DXR)
    //   t4 = RT shadow mask history (optional, DXR)
    //   t5 = RT diffuse GI buffer (optional, DXR)
    //   t6 = RT diffuse GI history buffer (optional, DXR)
    std::array<DescriptorHandle, 7> shadowAndEnvDescriptors{};

    std::vector<EnvironmentMaps> maps;
    std::vector<PendingEnvironment> pending;
    size_t currentIndex = 0;

    bool limitEnabled = false;
    float diffuseIntensity = 1.1f;
    float specularIntensity = 1.3f;
    bool enabled = true;

    void ResetMaps() {
        maps.clear();
        pending.clear();
        currentIndex = 0;
    }
};

} // namespace Cortex::Graphics
