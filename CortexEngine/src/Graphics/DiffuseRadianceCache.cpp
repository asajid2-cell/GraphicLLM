#include "Graphics/DiffuseRadianceCache.h"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>

namespace Cortex::Graphics {
namespace {

float Distance2D(float ax, float az, float bx, float bz) {
    const float dx = ax - bx;
    const float dz = az - bz;
    return std::sqrt(dx * dx + dz * dz);
}

float ProbeDelta(const DiffuseRadianceProbe& a, const DiffuseRadianceProbe& b) {
    return std::max({std::abs(a.r - b.r), std::abs(a.g - b.g), std::abs(a.b - b.b)});
}

} // namespace

DiffuseRadianceCache::DiffuseRadianceCache(DiffuseRadianceCacheConfig config)
    : m_config(config) {
    const uint32_t countX = std::max(1u, config.probeCountX);
    const uint32_t countZ = std::max(1u, config.probeCountZ);
    m_probes.reserve(static_cast<size_t>(countX) * countZ);
    for (uint32_t z = 0; z < countZ; ++z) {
        for (uint32_t x = 0; x < countX; ++x) {
            DiffuseRadianceProbe probe;
            probe.x = static_cast<float>(x) * config.spacingMeters;
            probe.y = 1.0f;
            probe.z = static_cast<float>(z) * config.spacingMeters;
            m_probes.push_back(probe);
        }
    }
    m_pending = m_probes;
}

void DiffuseRadianceCache::InjectDynamicLight(float x, float z, float r, float g, float b, float radiusMeters) {
    const float safeRadius = std::max(0.1f, radiusMeters);
    for (auto& probe : m_pending) {
        const float distance = Distance2D(probe.x, probe.z, x, z);
        const float falloff = std::max(0.0f, 1.0f - distance / safeRadius);
        probe.r += r * falloff;
        probe.g += g * falloff;
        probe.b += b * falloff;
    }
}

DiffuseRadianceCacheReport DiffuseRadianceCache::UpdateFrame() {
    DiffuseRadianceCacheReport report;
    report.probeCount = static_cast<uint32_t>(m_probes.size());
    const float blend = std::clamp(m_config.historyBlend, 0.0f, 0.99f);
    const float fresh = 1.0f - blend;
    for (size_t i = 0; i < m_probes.size(); ++i) {
        const auto before = m_probes[i];
        m_probes[i].r = before.r * blend + m_pending[i].r * fresh;
        m_probes[i].g = before.g * blend + m_pending[i].g * fresh;
        m_probes[i].b = before.b * blend + m_pending[i].b * fresh;
        m_probes[i].historyFrames = before.historyFrames + 1;
        report.maxDelta = std::max(report.maxDelta, ProbeDelta(before, m_probes[i]));
        m_pending[i].r = 0.0f;
        m_pending[i].g = 0.0f;
        m_pending[i].b = 0.0f;
    }
    report.stableHistory = report.maxDelta < 0.35f;
    return report;
}

std::string RunDiffuseRadianceCacheSelfTestJson() {
    DiffuseRadianceCacheConfig config;
    config.probeCountX = 32;
    config.probeCountZ = 32;
    config.spacingMeters = 2.0f;
    config.historyBlend = 0.88f;

    DiffuseRadianceCache cache(config);
    DiffuseRadianceCacheReport first;
    DiffuseRadianceCacheReport last;
    for (uint32_t frame = 0; frame < 12; ++frame) {
        cache.InjectDynamicLight(12.0f + static_cast<float>(frame) * 0.3f, 18.0f, 1.0f, 0.45f, 0.12f, 20.0f);
        cache.InjectDynamicLight(34.0f, 28.0f, 0.2f, 0.45f, 1.0f, 14.0f);
        auto report = cache.UpdateFrame();
        if (frame == 0) {
            first = report;
        }
        last = report;
    }

    const bool historyAdvanced =
        !cache.Probes().empty() &&
        cache.Probes().front().historyFrames == 12;
    const bool pass =
        first.probeCount == 1024 &&
        last.probeCount == 1024 &&
        historyAdvanced &&
        last.stableHistory &&
        !last.bruteForcePathTracingRequired;

    nlohmann::json report;
    report["schema"] = "cortex.diffuse_radiance_cache.self_test.v1";
    report["pass"] = pass;
    report["probe_count"] = last.probeCount;
    report["history_frames"] = cache.Probes().empty() ? 0 : cache.Probes().front().historyFrames;
    report["stable_history"] = last.stableHistory;
    report["max_delta"] = last.maxDelta;
    report["brute_force_path_tracing_required"] = last.bruteForcePathTracingRequired;
    return report.dump(2);
}

} // namespace Cortex::Graphics
