#include "Renderer.h"

#include <algorithm>
#include <cstring>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

#define CORTEX_REPORT_DEVICE_REMOVED(ctx, hr) \
    ReportDeviceRemoved((ctx), (hr), __FILE__, __LINE__)
void Renderer::AddDebugLine(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) {
    DebugLineVertex v0{ a, color };
    DebugLineVertex v1{ b, color };
    m_debugLineState.lines.push_back(v0);
    m_debugLineState.lines.push_back(v1);
}

void Renderer::ClearDebugLines() {
    m_debugLineState.lines.clear();
}

void Renderer::RenderDebugLines() {
    if (m_frameLifecycle.deviceRemoved || m_debugLineState.disabled || !m_pipelineState.debugLine || m_debugLineState.lines.empty() || !m_services.window) {
        m_debugLineState.lines.clear();
        return;
    }

    ID3D12Device* device = m_services.device->GetDevice();
    if (!device || !m_commandResources.graphicsList) {
        m_debugLineState.lines.clear();
        return;
    }

    const UINT vertexCount = static_cast<UINT>(m_debugLineState.lines.size());

    // Lazily allocate or grow the upload buffer used for debug lines. We keep
    // a single buffer and reuse it across frames to avoid constant heap
    // allocations, which can cause memory fragmentation or failures on some
    // drivers.
    const UINT requiredCapacity = vertexCount;
    const UINT minCapacity = 4096; // vertices
    UINT newCapacity = m_debugLineState.vertexCapacity;

    if (!m_debugLineState.vertexBuffer || m_debugLineState.vertexCapacity < requiredCapacity) {
        // CRITICAL: If replacing an existing buffer, wait for GPU to finish using it
        if (m_debugLineState.vertexBuffer) {
            WaitForGPU();
        }

        newCapacity = std::max(requiredCapacity, minCapacity);

        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD;
        heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
        heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
        heapProps.CreationNodeMask = 1;
        heapProps.VisibleNodeMask = 1;

        D3D12_RESOURCE_DESC desc = {};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = static_cast<UINT64>(newCapacity) * sizeof(DebugLineVertex);
        desc.Height = 1;
        desc.DepthOrArraySize = 1;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_UNKNOWN;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        desc.Flags = D3D12_RESOURCE_FLAG_NONE;

        Microsoft::WRL::ComPtr<ID3D12Resource> newBuffer;
        HRESULT hr = device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &desc,
            D3D12_RESOURCE_STATE_GENERIC_READ,
            nullptr,
            IID_PPV_ARGS(&newBuffer));

        if (FAILED(hr)) {
            spdlog::warn("RenderDebugLines: failed to allocate vertex buffer (disabling debug lines for this run)");
            m_debugLineState.disabled = true;
            m_debugLineState.lines.clear();
            return;
        }

        m_debugLineState.vertexBuffer = newBuffer;
        m_debugLineState.vertexCapacity = newCapacity;
    }

    const UINT bufferSize = vertexCount * sizeof(DebugLineVertex);

    // Upload vertex data.
    void* mapped = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    HRESULT mapHr = m_debugLineState.vertexBuffer->Map(0, &readRange, &mapped);
    if (SUCCEEDED(mapHr)) {
        memcpy(mapped, m_debugLineState.lines.data(), bufferSize);
        m_debugLineState.vertexBuffer->Unmap(0, nullptr);
    } else {
        spdlog::warn("RenderDebugLines: failed to map vertex buffer (disabling debug lines for this run)");
        CORTEX_REPORT_DEVICE_REMOVED("RenderDebugLines_MapVertexBuffer", mapHr);
        m_debugLineState.disabled = true;
        m_debugLineState.lines.clear();
        return;
    }

    // Set pipeline state and render target (back buffer).
    ID3D12Resource* backBuffer = m_services.window->GetCurrentBackBuffer();
    if (!backBuffer) {
        m_debugLineState.lines.clear();
        return;
    }

    m_commandResources.graphicsList->SetPipelineState(m_pipelineState.debugLine->GetPipelineState());
    m_commandResources.graphicsList->SetGraphicsRootSignature(m_pipelineState.rootSignature->GetRootSignature());

    // Frame constants are already bound by the main render path; ensure
    // object constants are valid by binding an identity transform once.
    ObjectConstants obj{};
    obj.modelMatrix  = glm::mat4(1.0f);
    obj.normalMatrix = glm::mat4(1.0f);
    auto objAddr = m_constantBuffers.object.AllocateAndWrite(obj);
    m_commandResources.graphicsList->SetGraphicsRootConstantBufferView(0, objAddr);

    // IA setup
    D3D12_VERTEX_BUFFER_VIEW vbv{};
    vbv.BufferLocation = m_debugLineState.vertexBuffer->GetGPUVirtualAddress();
    vbv.StrideInBytes  = sizeof(DebugLineVertex);
    vbv.SizeInBytes    = bufferSize;

    m_commandResources.graphicsList->IASetVertexBuffers(0, 1, &vbv);
    m_commandResources.graphicsList->IASetIndexBuffer(nullptr);
    m_commandResources.graphicsList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);

    // Draw all lines in one call.
    m_commandResources.graphicsList->DrawInstanced(vertexCount, 1, 0, 0);
    ++m_frameDiagnostics.contract.drawCounts.debugLineDraws;
    m_frameDiagnostics.contract.drawCounts.debugLineVertices += vertexCount;

    // Clear for next frame.
    m_debugLineState.lines.clear();
}

#undef CORTEX_REPORT_DEVICE_REMOVED

} // namespace Cortex::Graphics

