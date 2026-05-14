#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Cortex::Graphics {

struct ManyLightSampleInput {
    std::string id;
    float intensity = 0.0f;
    float radius = 1.0f;
    bool dynamic = false;
};

struct ManyLightReservoirSample {
    std::string lightId;
    float selectionProbability = 0.0f;
    float contributionWeight = 0.0f;
};

struct ManyLightReservoirResult {
    std::vector<ManyLightReservoirSample> samples;
    uint32_t inputLightCount = 0;
    uint32_t requestedSampleCount = 0;
    float totalWeight = 0.0f;
    bool usedReservoirSampling = false;
};

[[nodiscard]] ManyLightReservoirResult BuildManyLightReservoir(const std::vector<ManyLightSampleInput>& lights,
                                                               uint32_t sampleCount,
                                                               uint64_t seed);
[[nodiscard]] std::string RunManyLightReservoirSelfTestJson();

} // namespace Cortex::Graphics
