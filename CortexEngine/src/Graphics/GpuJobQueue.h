#pragma once

#include <cstdint>
#include <deque>
#include <memory>
#include <string>

namespace Cortex::Scene {
struct MeshData;
}

namespace Cortex::Graphics {

enum class GpuJobType : uint32_t {
    MeshUpload = 0,
    BuildBLAS = 1
};

struct GpuJob {
    GpuJobType type = GpuJobType::MeshUpload;
    std::shared_ptr<Scene::MeshData> mesh;
    const Scene::MeshData* blasMeshKey = nullptr;
    std::string label;
};

struct GpuJobQueueState {
    std::deque<GpuJob> pendingJobs;
    uint32_t maxMeshJobsPerFrame = 16;
    uint32_t maxBLASJobsPerFrame = 4;
    uint32_t pendingMeshJobs = 0;
    uint32_t pendingBLASJobs = 0;

    [[nodiscard]] bool HasPendingJobs() const {
        return !pendingJobs.empty();
    }

    void Clear() {
        pendingJobs.clear();
        pendingMeshJobs = 0;
        pendingBLASJobs = 0;
    }
};

} // namespace Cortex::Graphics
