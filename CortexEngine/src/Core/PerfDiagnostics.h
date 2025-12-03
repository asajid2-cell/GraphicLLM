#pragma once

#include <stdint.h>
#include <vector>

namespace Cortex {
class Engine;
}

namespace Cortex::Graphics {
class Renderer;
}

namespace Cortex {

struct PerfSnapshot {
    double frameMs = 0.0;
    double mainMs  = 0.0;
    double rtMs    = 0.0;
    double postMs  = 0.0;

    struct Memory {
        double texMB  = 0.0;
        double envMB  = 0.0;
        double geomMB = 0.0;
        double rtMB   = 0.0;
    } mem;

    struct Jobs {
        uint32_t meshJobs   = 0;
        uint32_t blasJobs   = 0;
        bool     rtWarmingUp = false;
    } jobs;

    struct Governors {
        bool  vramGovernorFired = false;
        bool  perfGovernorFired = false;
        bool  rtGIOff           = false;
        bool  rtReflOff         = false;
        bool  ssrOff            = false;
        float renderScale       = 1.0f;
        bool  texBudgetExceeded  = false;
        bool  envBudgetExceeded  = false;
        bool  geomBudgetExceeded = false;
        bool  rtBudgetExceeded   = false;
    } governors;
};

class PerfDiagnostics {
public:
    PerfDiagnostics() = default;

    void Update(const Engine& engine, const Graphics::Renderer& renderer);

    [[nodiscard]] const PerfSnapshot& GetLast() const { return m_last; }

    // Optional history for offline analysis / JSON reports.
    [[nodiscard]] std::vector<PerfSnapshot> GetHistory() const { return m_history; }

private:
    static constexpr size_t kMaxHistory = 120;

    PerfSnapshot            m_last{};
    std::vector<PerfSnapshot> m_history;
};

} // namespace Cortex
