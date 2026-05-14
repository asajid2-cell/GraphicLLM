#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Cortex::Graphics {

struct DiffuseRadianceProbe {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    uint32_t historyFrames = 0;
};

struct DiffuseRadianceCacheConfig {
    uint32_t probeCountX = 0;
    uint32_t probeCountZ = 0;
    float spacingMeters = 1.0f;
    float historyBlend = 0.85f;
};

struct DiffuseRadianceCacheReport {
    uint32_t probeCount = 0;
    bool bruteForcePathTracingRequired = false;
    bool stableHistory = false;
    float maxDelta = 0.0f;
};

class DiffuseRadianceCache {
public:
    explicit DiffuseRadianceCache(DiffuseRadianceCacheConfig config);

    void InjectDynamicLight(float x, float z, float r, float g, float b, float radiusMeters);
    [[nodiscard]] DiffuseRadianceCacheReport UpdateFrame();
    [[nodiscard]] const std::vector<DiffuseRadianceProbe>& Probes() const { return m_probes; }

private:
    DiffuseRadianceCacheConfig m_config;
    std::vector<DiffuseRadianceProbe> m_probes;
    std::vector<DiffuseRadianceProbe> m_pending;
};

[[nodiscard]] std::string RunDiffuseRadianceCacheSelfTestJson();

} // namespace Cortex::Graphics
