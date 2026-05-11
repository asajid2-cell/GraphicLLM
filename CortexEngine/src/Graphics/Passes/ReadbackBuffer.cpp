#include "ReadbackBuffer.h"

#include <spdlog/spdlog.h>

namespace Cortex::Graphics::ReadbackBuffer {

ScopedMap::ScopedMap(ID3D12Resource* resource, void* data, D3D12_RANGE writeRange)
    : m_resource(resource), m_data(data), m_writeRange(writeRange) {}

ScopedMap::ScopedMap(ScopedMap&& other) noexcept
    : m_resource(other.m_resource), m_data(other.m_data), m_writeRange(other.m_writeRange) {
    other.m_resource = nullptr;
    other.m_data = nullptr;
    other.m_writeRange = {0, 0};
}

ScopedMap& ScopedMap::operator=(ScopedMap&& other) noexcept {
    if (this != &other) {
        Reset();
        m_resource = other.m_resource;
        m_data = other.m_data;
        m_writeRange = other.m_writeRange;
        other.m_resource = nullptr;
        other.m_data = nullptr;
        other.m_writeRange = {0, 0};
    }
    return *this;
}

ScopedMap::~ScopedMap() {
    Reset();
}

void ScopedMap::Reset() {
    if (m_resource) {
        m_resource->Unmap(0, &m_writeRange);
    }
    m_resource = nullptr;
    m_data = nullptr;
    m_writeRange = {0, 0};
}

ScopedMap MapRange(ID3D12Resource* resource,
                   const D3D12_RANGE& readRange,
                   const char* label) {
    if (!resource) {
        spdlog::warn("{}: missing readback buffer", label ? label : "Readback");
        return {};
    }

    void* data = nullptr;
    const HRESULT hr = resource->Map(0, &readRange, &data);
    if (FAILED(hr) || !data) {
        spdlog::warn("{}: failed to map readback buffer", label ? label : "Readback");
        return {};
    }

    constexpr D3D12_RANGE kNoCpuWrites{0, 0};
    return ScopedMap(resource, data, kNoCpuWrites);
}

} // namespace Cortex::Graphics::ReadbackBuffer
