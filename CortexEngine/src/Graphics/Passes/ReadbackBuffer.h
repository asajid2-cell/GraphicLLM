#pragma once

#include "Graphics/RHI/D3D12Includes.h"

namespace Cortex::Graphics::ReadbackBuffer {

class ScopedMap {
public:
    ScopedMap() = default;
    ScopedMap(ID3D12Resource* resource, void* data, D3D12_RANGE writeRange);
    ScopedMap(const ScopedMap&) = delete;
    ScopedMap& operator=(const ScopedMap&) = delete;
    ScopedMap(ScopedMap&& other) noexcept;
    ScopedMap& operator=(ScopedMap&& other) noexcept;
    ~ScopedMap();

    [[nodiscard]] bool IsValid() const { return m_resource && m_data; }
    [[nodiscard]] void* Data() const { return m_data; }

    template <typename T>
    [[nodiscard]] T* As() const {
        return static_cast<T*>(m_data);
    }

    void Reset();

private:
    ID3D12Resource* m_resource = nullptr;
    void* m_data = nullptr;
    D3D12_RANGE m_writeRange{0, 0};
};

[[nodiscard]] ScopedMap MapRange(ID3D12Resource* resource,
                                 const D3D12_RANGE& readRange,
                                 const char* label);

} // namespace Cortex::Graphics::ReadbackBuffer
