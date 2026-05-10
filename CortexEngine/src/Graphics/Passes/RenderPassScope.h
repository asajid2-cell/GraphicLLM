#pragma once

namespace Cortex::Graphics {

template <typename T>
class ScopedRenderPassValue {
public:
    ScopedRenderPassValue(T& target, T value)
        : m_target(target)
        , m_previous(target) {
        m_target = value;
    }

    ~ScopedRenderPassValue() {
        m_target = m_previous;
    }

    ScopedRenderPassValue(const ScopedRenderPassValue&) = delete;
    ScopedRenderPassValue& operator=(const ScopedRenderPassValue&) = delete;

private:
    T& m_target;
    T m_previous;
};

} // namespace Cortex::Graphics
