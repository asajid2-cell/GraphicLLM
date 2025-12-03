#include "Core/PerfDiagnostics.h"

#include "Core/Engine.h"
#include "Graphics/Renderer.h"

namespace Cortex {

void PerfDiagnostics::Update(const Engine& engine, const Graphics::Renderer& renderer) {
    PerfSnapshot snap{};

    // Frame and pass timings
    snap.frameMs = static_cast<double>(engine.GetLastFrameTimeSeconds()) * 1000.0;
    snap.mainMs  = static_cast<double>(renderer.GetLastMainPassTimeMS());
    snap.rtMs    = static_cast<double>(renderer.GetLastRTTimeMS());
    snap.postMs  = static_cast<double>(renderer.GetLastPostTimeMS());

    // Memory breakdown from the asset registry (MB)
    auto mem = renderer.GetAssetMemoryBreakdown();
    constexpr double kToMB = 1.0 / (1024.0 * 1024.0);
    snap.mem.texMB  = static_cast<double>(mem.textureBytes) * kToMB;
    snap.mem.envMB  = static_cast<double>(mem.environmentBytes) * kToMB;
    snap.mem.geomMB = static_cast<double>(mem.geometryBytes) * kToMB;
    snap.mem.rtMB   = static_cast<double>(mem.rtStructureBytes) * kToMB;

    // GPU job queue status
    snap.jobs.meshJobs    = renderer.GetPendingMeshJobs();
    snap.jobs.blasJobs    = renderer.GetPendingBLASJobs();
    snap.jobs.rtWarmingUp = renderer.IsRTWarmingUp();

    // Governors and quality state
    const auto& registry = renderer.GetAssetRegistry();
    snap.governors.vramGovernorFired = engine.DidVRAMGovernorReduce();
    snap.governors.perfGovernorFired = engine.DidPerfGovernorAdjust();
    snap.governors.rtGIOff           = engine.WasPerfRTGIDisabled();
    snap.governors.rtReflOff         = engine.WasPerfRTReflectionsDisabled();
    snap.governors.ssrOff            = engine.WasPerfSSROff();
    snap.governors.renderScale       = renderer.GetRenderScale();
    snap.governors.texBudgetExceeded  = registry.IsTextureBudgetExceeded();
    snap.governors.envBudgetExceeded  = registry.IsEnvironmentBudgetExceeded();
    snap.governors.geomBudgetExceeded = registry.IsGeometryBudgetExceeded();
    snap.governors.rtBudgetExceeded   = registry.IsRTBudgetExceeded();

    m_last = snap;

    // Append to short ring buffer for optional offline analysis.
    if (m_history.size() >= kMaxHistory) {
        m_history.erase(m_history.begin());
    }
    m_history.push_back(snap);
}

} // namespace Cortex
