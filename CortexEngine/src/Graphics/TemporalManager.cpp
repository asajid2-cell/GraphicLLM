#include "Graphics/TemporalManager.h"

#include <algorithm>

namespace Cortex::Graphics {

namespace {

TemporalHistoryRecord MakeRecord(TemporalHistoryId id, const char* name, const char* resourceName) {
    TemporalHistoryRecord record{};
    record.id = id;
    record.name = name;
    record.resourceName = resourceName;
    return record;
}

const char* NormalizeReason(const char* reason) {
    return (reason && reason[0] != '\0') ? reason : "invalidated";
}

} // namespace

TemporalManager::TemporalManager() {
    Reset();
}

void TemporalManager::Reset() {
    m_records[Index(TemporalHistoryId::TAAColor)] =
        MakeRecord(TemporalHistoryId::TAAColor, "taa_color", "taa_history");
    m_records[Index(TemporalHistoryId::RTShadow)] =
        MakeRecord(TemporalHistoryId::RTShadow, "rt_shadow_mask", "rt_shadow_history");
    m_records[Index(TemporalHistoryId::RTReflection)] =
        MakeRecord(TemporalHistoryId::RTReflection, "rt_reflection", "rt_reflection_history");
    m_records[Index(TemporalHistoryId::RTGI)] =
        MakeRecord(TemporalHistoryId::RTGI, "rt_gi", "rt_gi_history");

    for (auto& record : m_records) {
        record.valid = false;
        record.seeded = false;
        record.lastValidFrame = 0;
        record.lastInvalidatedFrame = 0;
        record.invalidReason = "not_seeded";
        record.lastResetReason = "not_seeded";
        record.rejectionMode = "none";
        record.accumulationAlpha = 1.0f;
        record.usedDepthNormalRejection = false;
        record.usedVelocityReprojection = false;
        record.usedDisocclusionRejection = false;
    }
}

void TemporalManager::BeginFrame(uint64_t frameIndex) {
    m_frameIndex = frameIndex;
}

void TemporalManager::Invalidate(TemporalHistoryId id, const char* reason, uint64_t frameIndex) {
    TemporalHistoryRecord& record = m_records[Index(id)];
    const char* normalized = NormalizeReason(reason);
    record.valid = false;
    record.invalidReason = normalized;
    record.lastResetReason = normalized;
    record.lastInvalidatedFrame = frameIndex;
    record.rejectionMode = "none";
    record.accumulationAlpha = 1.0f;
    record.usedDepthNormalRejection = false;
    record.usedVelocityReprojection = false;
    record.usedDisocclusionRejection = false;
}

void TemporalManager::InvalidateAll(const char* reason, uint64_t frameIndex) {
    for (size_t i = 0; i < m_records.size(); ++i) {
        Invalidate(static_cast<TemporalHistoryId>(i), reason, frameIndex);
    }
}

void TemporalManager::MarkValid(TemporalHistoryId id, uint64_t frameIndex, const TemporalMarkValidDesc& desc) {
    TemporalHistoryRecord& record = m_records[Index(id)];
    record.valid = true;
    record.seeded = true;
    record.lastValidFrame = frameIndex;
    record.invalidReason.clear();
    record.rejectionMode = desc.rejectionMode ? desc.rejectionMode : "none";
    record.accumulationAlpha = std::max(0.0f, desc.accumulationAlpha);
    record.usedDepthNormalRejection = desc.usedDepthNormalRejection;
    record.usedVelocityReprojection = desc.usedVelocityReprojection;
    record.usedDisocclusionRejection = desc.usedDisocclusionRejection;
}

bool TemporalManager::IsValid(TemporalHistoryId id) const {
    return m_records[Index(id)].valid;
}

bool TemporalManager::CanReproject(TemporalHistoryId id) const {
    return IsValid(id);
}

const TemporalHistoryRecord& TemporalManager::Get(TemporalHistoryId id) const {
    return m_records[Index(id)];
}

size_t TemporalManager::Index(TemporalHistoryId id) {
    const size_t index = static_cast<size_t>(id);
    return std::min(index, static_cast<size_t>(TemporalHistoryId::Count) - 1u);
}

} // namespace Cortex::Graphics
