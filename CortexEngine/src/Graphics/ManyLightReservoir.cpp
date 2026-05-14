#include "Graphics/ManyLightReservoir.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <unordered_set>

namespace Cortex::Graphics {
namespace {

uint64_t Mix(uint64_t value) {
    value ^= value >> 30;
    value *= 0xbf58476d1ce4e5b9ull;
    value ^= value >> 27;
    value *= 0x94d049bb133111ebull;
    value ^= value >> 31;
    return value;
}

float LightWeight(const ManyLightSampleInput& light) {
    const float radius = std::max(0.05f, light.radius);
    const float motionBoost = light.dynamic ? 1.25f : 1.0f;
    return std::max(0.0f, light.intensity) * radius * radius * motionBoost;
}

} // namespace

ManyLightReservoirResult BuildManyLightReservoir(const std::vector<ManyLightSampleInput>& lights,
                                                 uint32_t sampleCount,
                                                 uint64_t seed) {
    ManyLightReservoirResult result;
    result.inputLightCount = static_cast<uint32_t>(lights.size());
    result.requestedSampleCount = sampleCount;
    result.usedReservoirSampling = lights.size() > sampleCount;
    if (lights.empty() || sampleCount == 0) {
        return result;
    }

    std::vector<std::pair<float, size_t>> ranked;
    ranked.reserve(lights.size());
    for (size_t index = 0; index < lights.size(); ++index) {
        const float weight = LightWeight(lights[index]);
        result.totalWeight += weight;
        const uint64_t mixed = Mix(seed ^ (static_cast<uint64_t>(index) + 0x9e3779b97f4a7c15ull));
        const float jitter = static_cast<float>((mixed >> 40) & 0xffffffu) / static_cast<float>(0xffffffu);
        const float key = weight + jitter * std::max(0.001f, weight) * 0.01f;
        ranked.push_back({key, index});
    }

    std::sort(ranked.begin(), ranked.end(), [](const auto& left, const auto& right) {
        if (left.first == right.first) {
            return left.second < right.second;
        }
        return left.first > right.first;
    });

    const uint32_t takeCount = std::min<uint32_t>(sampleCount, static_cast<uint32_t>(ranked.size()));
    result.samples.reserve(takeCount);
    for (uint32_t i = 0; i < takeCount; ++i) {
        const auto& light = lights[ranked[i].second];
        const float weight = LightWeight(light);
        ManyLightReservoirSample sample;
        sample.lightId = light.id;
        sample.selectionProbability = result.totalWeight > 0.0f ? weight / result.totalWeight : 0.0f;
        sample.contributionWeight = weight;
        result.samples.push_back(std::move(sample));
    }

    return result;
}

std::string RunManyLightReservoirSelfTestJson() {
    std::vector<ManyLightSampleInput> lights;
    lights.reserve(4096);
    for (uint32_t i = 0; i < 4096; ++i) {
        ManyLightSampleInput light;
        light.id = "neon_particle_emissive_" + std::to_string(i);
        light.intensity = 0.05f + static_cast<float>((i * 37u) % 211u) / 20.0f;
        light.radius = 0.1f + static_cast<float>((i * 17u) % 29u) / 10.0f;
        light.dynamic = (i % 7u) == 0u;
        lights.push_back(std::move(light));
    }

    const auto first = BuildManyLightReservoir(lights, 64, 328u);
    const auto second = BuildManyLightReservoir(lights, 64, 328u);
    std::unordered_set<std::string> unique;
    float probabilitySum = 0.0f;
    bool sameOrder = first.samples.size() == second.samples.size();
    for (size_t i = 0; i < first.samples.size(); ++i) {
        unique.insert(first.samples[i].lightId);
        probabilitySum += first.samples[i].selectionProbability;
        if (sameOrder && first.samples[i].lightId != second.samples[i].lightId) {
            sameOrder = false;
        }
    }

    const bool pass =
        first.usedReservoirSampling &&
        first.inputLightCount == 4096 &&
        first.samples.size() == 64 &&
        unique.size() == first.samples.size() &&
        probabilitySum > 0.0f &&
        sameOrder;

    nlohmann::json report;
    report["schema"] = "cortex.many_light_reservoir.self_test.v1";
    report["pass"] = pass;
    report["input_lights"] = first.inputLightCount;
    report["sample_count"] = first.samples.size();
    report["requested_sample_count"] = first.requestedSampleCount;
    report["used_reservoir_sampling"] = first.usedReservoirSampling;
    report["unique_samples"] = unique.size();
    report["deterministic_replay"] = sameOrder;
    report["probability_sum"] = probabilitySum;
    report["total_weight"] = first.totalWeight;
    report["top_sample"] = first.samples.empty() ? "" : first.samples.front().lightId;
    return report.dump(2);
}

} // namespace Cortex::Graphics
