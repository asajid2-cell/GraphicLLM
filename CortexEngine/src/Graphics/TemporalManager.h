#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Cortex::Graphics {

enum class TemporalHistoryId : uint8_t {
    TAAColor = 0,
    RTShadow,
    RTReflection,
    RTGI,
    Count
};

struct TemporalMarkValidDesc {
    const char* rejectionMode = "none";
    float accumulationAlpha = 1.0f;
    bool usedDepthNormalRejection = false;
    bool usedVelocityReprojection = false;
    bool usedDisocclusionRejection = false;
};

struct TemporalHistoryRecord {
    TemporalHistoryId id = TemporalHistoryId::TAAColor;
    const char* name = "";
    const char* resourceName = "";
    bool valid = false;
    bool seeded = false;
    uint64_t lastValidFrame = 0;
    uint64_t lastInvalidatedFrame = 0;
    std::string invalidReason = "not_seeded";
    std::string lastResetReason = "not_seeded";
    std::string rejectionMode = "none";
    float accumulationAlpha = 1.0f;
    bool usedDepthNormalRejection = false;
    bool usedVelocityReprojection = false;
    bool usedDisocclusionRejection = false;
};

class TemporalManager {
public:
    TemporalManager();

    void Reset();
    void BeginFrame(uint64_t frameIndex);
    void Invalidate(TemporalHistoryId id, const char* reason, uint64_t frameIndex);
    void InvalidateAll(const char* reason, uint64_t frameIndex);
    void MarkValid(TemporalHistoryId id, uint64_t frameIndex, const TemporalMarkValidDesc& desc = {});

    [[nodiscard]] bool IsValid(TemporalHistoryId id) const;
    [[nodiscard]] bool CanReproject(TemporalHistoryId id) const;
    [[nodiscard]] const TemporalHistoryRecord& Get(TemporalHistoryId id) const;
    [[nodiscard]] const std::array<TemporalHistoryRecord, static_cast<size_t>(TemporalHistoryId::Count)>& Records() const {
        return m_records;
    }

private:
    [[nodiscard]] static size_t Index(TemporalHistoryId id);

    std::array<TemporalHistoryRecord, static_cast<size_t>(TemporalHistoryId::Count)> m_records{};
    uint64_t m_frameIndex = 0;
};

} // namespace Cortex::Graphics
