#include "Debug/GPUProfiler.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <cstring>
#include <spdlog/spdlog.h>

namespace Cortex::Debug {

GPUProfiler& GPUProfiler::Get() {
    static GPUProfiler profiler;
    return profiler;
}

GPUProfiler::GPUProfiler() = default;

GPUProfiler::~GPUProfiler() {
    Shutdown();
}

bool GPUProfiler::Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue) {
    if (!device || !commandQueue) {
        return false;
    }

    Shutdown();
    m_device = device;
    m_commandQueue = commandQueue;

    if (FAILED(m_commandQueue->GetTimestampFrequency(&m_gpuFrequency)) || m_gpuFrequency == 0) {
        spdlog::warn("GPUProfiler: command queue timestamp frequency unavailable");
        return false;
    }

    m_queryFrames.resize(m_bufferCount);
    for (QueryFrame& frame : m_queryFrames) {
        if (!CreateQueryFrame(frame)) {
            Shutdown();
            return false;
        }
    }

    if (FAILED(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&m_fence)))) {
        spdlog::warn("GPUProfiler: failed to create readback fence");
        Shutdown();
        return false;
    }
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (!m_fenceEvent) {
        spdlog::warn("GPUProfiler: failed to create readback fence event");
        Shutdown();
        return false;
    }

    m_initialized = true;
    m_enabled = true;
    spdlog::info("GPUProfiler initialized (frequency={} Hz, buffers={})",
                 m_gpuFrequency,
                 m_bufferCount);
    return true;
}

void GPUProfiler::Shutdown() {
    for (QueryFrame& frame : m_queryFrames) {
        frame.timestampHeap.Reset();
        frame.pipelineStatsHeap.Reset();
        frame.readbackBuffer.Reset();
        frame.timestampNames.clear();
        frame.timestampDepths.clear();
        frame.timestampEndIndices.clear();
        frame.scopeStack.clear();
        frame.timestampCount = 0;
        frame.pending = false;
    }

    while (!m_pendingFrames.empty()) {
        m_pendingFrames.pop();
    }

    if (m_fenceEvent) {
        CloseHandle(m_fenceEvent);
        m_fenceEvent = nullptr;
    }
    m_fence.Reset();
    m_frameHistory.clear();
    m_initialized = false;
    m_inFrame = false;
    m_pipelineStatsActive = false;
    m_device = nullptr;
    m_commandQueue = nullptr;
}

void GPUProfiler::SetBufferCount(uint32_t count) {
    if (m_initialized) {
        return;
    }
    m_bufferCount = std::max<uint32_t>(2, count);
}

bool GPUProfiler::CreateQueryFrame(QueryFrame& frame) {
    D3D12_QUERY_HEAP_DESC timestampDesc{};
    timestampDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    timestampDesc.Count = frame.maxTimestamps;
    timestampDesc.NodeMask = 0;

    if (FAILED(m_device->CreateQueryHeap(&timestampDesc, IID_PPV_ARGS(&frame.timestampHeap)))) {
        spdlog::warn("GPUProfiler: failed to create timestamp query heap");
        return false;
    }

    D3D12_RESOURCE_DESC readbackDesc{};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = static_cast<UINT64>(frame.maxTimestamps) * sizeof(uint64_t);
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.Format = DXGI_FORMAT_UNKNOWN;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_READBACK;
    heapProps.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
    heapProps.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
    heapProps.CreationNodeMask = 1;
    heapProps.VisibleNodeMask = 1;

    if (FAILED(m_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &readbackDesc,
            D3D12_RESOURCE_STATE_COPY_DEST,
            nullptr,
            IID_PPV_ARGS(&frame.readbackBuffer)))) {
        spdlog::warn("GPUProfiler: failed to create timestamp readback buffer");
        return false;
    }

    frame.timestampNames.resize(frame.maxTimestamps);
    frame.timestampDepths.resize(frame.maxTimestamps);
    frame.timestampEndIndices.resize(frame.maxTimestamps, UINT32_MAX);
    return true;
}

QueryFrame& GPUProfiler::GetCurrentQueryFrame() {
    return m_queryFrames[m_currentFrameIndex % static_cast<uint32_t>(m_queryFrames.size())];
}

void GPUProfiler::BeginFrame(ID3D12GraphicsCommandList* commandList) {
    if (!m_initialized || !m_enabled || !commandList || m_queryFrames.empty()) {
        return;
    }

    ResolveQueries();

    m_currentFrameIndex = static_cast<uint32_t>(m_frameNumber % m_queryFrames.size());
    QueryFrame& frame = GetCurrentQueryFrame();
    if (frame.pending) {
        if (m_fence && frame.fenceValue != 0 && m_fence->GetCompletedValue() < frame.fenceValue) {
            if (SUCCEEDED(m_fence->SetEventOnCompletion(frame.fenceValue, m_fenceEvent))) {
                WaitForSingleObject(m_fenceEvent, INFINITE);
            }
        }

        GPUFrameProfile profile{};
        if (ReadbackQueries(frame, profile)) {
            if (m_frameHistory.size() >= m_maxFrameHistory) {
                m_frameHistory.erase(m_frameHistory.begin());
            }
            m_frameHistory.push_back(profile);
            if (m_onFrameResolved) {
                m_onFrameResolved(profile);
            }
        } else {
            spdlog::warn("GPUProfiler: dropping unresolved timestamp frame before query slot reuse");
            frame.pending = false;
        }
    }
    frame.timestampCount = 0;
    frame.scopeStack.clear();
    std::fill(frame.timestampNames.begin(), frame.timestampNames.end(), std::pair<const char*, const char*>{nullptr, nullptr});
    std::fill(frame.timestampDepths.begin(), frame.timestampDepths.end(), 0u);
    std::fill(frame.timestampEndIndices.begin(), frame.timestampEndIndices.end(), UINT32_MAX);
    frame.pending = false;
    frame.fenceValue = 0;
    frame.frameNumber = 0;
    m_inFrame = true;
}

void GPUProfiler::EndFrame(ID3D12GraphicsCommandList* commandList) {
    if (!m_initialized || !m_enabled || !m_inFrame || !commandList) {
        return;
    }

    QueryFrame& frame = GetCurrentQueryFrame();
    while (!frame.scopeStack.empty()) {
        EndScope(commandList);
    }

    if (frame.timestampCount > 0) {
        commandList->ResolveQueryData(
            frame.timestampHeap.Get(),
            D3D12_QUERY_TYPE_TIMESTAMP,
            0,
            frame.timestampCount,
            frame.readbackBuffer.Get(),
            0);
        frame.pending = true;
        frame.fenceValue = 0;
        frame.frameNumber = m_frameNumber;
        m_pendingFrames.push(m_currentFrameIndex);
    }

    ++m_frameNumber;
    m_inFrame = false;
}

void GPUProfiler::NotifyFrameSubmitted() {
    if (!m_initialized || !m_enabled || !m_commandQueue || !m_fence || m_queryFrames.empty()) {
        return;
    }

    QueryFrame& frame = GetCurrentQueryFrame();
    if (!frame.pending || frame.fenceValue != 0) {
        return;
    }

    const uint64_t signalValue = ++m_fenceValue;
    if (FAILED(m_commandQueue->Signal(m_fence.Get(), signalValue))) {
        spdlog::warn("GPUProfiler: failed to signal timestamp readback fence");
        return;
    }
    frame.fenceValue = signalValue;
}

void GPUProfiler::BeginScope(ID3D12GraphicsCommandList* commandList, const char* name, const char* category) {
    if (!m_initialized || !m_enabled || !m_inFrame || !commandList || !name) {
        return;
    }

    QueryFrame& frame = GetCurrentQueryFrame();
    if (frame.timestampCount + 2 > frame.maxTimestamps) {
        return;
    }

    const uint32_t startIndex = frame.timestampCount++;
    frame.timestampNames[startIndex] = {name, category ? category : "GPU"};
    frame.timestampDepths[startIndex] = static_cast<uint32_t>(frame.scopeStack.size());
    frame.scopeStack.push_back(startIndex);
    commandList->EndQuery(frame.timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, startIndex);
}

void GPUProfiler::EndScope(ID3D12GraphicsCommandList* commandList) {
    if (!m_initialized || !m_enabled || !m_inFrame || !commandList) {
        return;
    }

    QueryFrame& frame = GetCurrentQueryFrame();
    if (frame.scopeStack.empty() || frame.timestampCount >= frame.maxTimestamps) {
        return;
    }

    const uint32_t startIndex = frame.scopeStack.back();
    frame.scopeStack.pop_back();
    const uint32_t endIndex = frame.timestampCount++;
    frame.timestampEndIndices[startIndex] = endIndex;
    commandList->EndQuery(frame.timestampHeap.Get(), D3D12_QUERY_TYPE_TIMESTAMP, endIndex);
}

void GPUProfiler::BeginPipelineStats(ID3D12GraphicsCommandList*) {}
void GPUProfiler::EndPipelineStats(ID3D12GraphicsCommandList*) {}

bool GPUProfiler::ReadbackQueries(QueryFrame& frame, GPUFrameProfile& outProfile) {
    if (!frame.pending || frame.timestampCount == 0 || !frame.readbackBuffer) {
        return false;
    }

    D3D12_RANGE readRange{0, static_cast<SIZE_T>(frame.timestampCount) * sizeof(uint64_t)};
    uint64_t* mapped = nullptr;
    if (FAILED(frame.readbackBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mapped))) || !mapped) {
        return false;
    }

    outProfile = GPUFrameProfile{};
    outProfile.frameNumber = frame.frameNumber;
    outProfile.gpuFrequency = m_gpuFrequency;

    for (uint32_t i = 0; i < frame.timestampCount; ++i) {
        const auto& name = frame.timestampNames[i];
        if (!name.first) {
            continue;
        }

        const uint32_t endIndex = frame.timestampEndIndices[i];
        if (endIndex == UINT32_MAX || endIndex >= frame.timestampCount) {
            continue;
        }

        GPUTimestamp ts{};
        ts.name = name.first;
        ts.category = name.second ? name.second : "GPU";
        ts.startTick = mapped[i];
        ts.endTick = mapped[endIndex];
        ts.depth = frame.timestampDepths[i];
        if (ts.endTick >= ts.startTick) {
            outProfile.timestamps.push_back(ts);
        }
    }

    D3D12_RANGE writeRange{0, 0};
    frame.readbackBuffer->Unmap(0, &writeRange);
    frame.pending = false;
    return !outProfile.timestamps.empty();
}

void GPUProfiler::ResolveQueries() {
    if (!m_initialized || !m_enabled) {
        return;
    }

    const size_t pendingCount = m_pendingFrames.size();
    for (size_t n = 0; n < pendingCount; ++n) {
        const uint32_t frameIndex = m_pendingFrames.front();
        m_pendingFrames.pop();

        QueryFrame& frame = m_queryFrames[frameIndex % static_cast<uint32_t>(m_queryFrames.size())];
        if (frame.pending) {
            if (!m_fence || frame.fenceValue == 0 || m_fence->GetCompletedValue() < frame.fenceValue) {
                m_pendingFrames.push(frameIndex);
                continue;
            }
        }

        GPUFrameProfile profile{};
        if (ReadbackQueries(frame, profile)) {
            if (m_frameHistory.size() >= m_maxFrameHistory) {
                m_frameHistory.erase(m_frameHistory.begin());
            }
            m_frameHistory.push_back(profile);
            if (m_onFrameResolved) {
                m_onFrameResolved(profile);
            }
        } else if (frame.pending) {
            m_pendingFrames.push(frameIndex);
        }
    }
}

const GPUFrameProfile* GPUProfiler::GetLastResolvedFrame() const {
    if (m_frameHistory.empty()) {
        return nullptr;
    }
    return &m_frameHistory.back();
}

double GPUProfiler::GetLastFrameGPUTimeMs() const {
    const GPUFrameProfile* frame = GetLastResolvedFrame();
    return frame ? frame->GetTotalGPUTimeMs() : 0.0;
}

double GPUProfiler::GetAverageGPUTimeMs(size_t frameCount) const {
    if (m_frameHistory.empty() || frameCount == 0) {
        return 0.0;
    }
    const size_t count = std::min(frameCount, m_frameHistory.size());
    double total = 0.0;
    for (size_t i = m_frameHistory.size() - count; i < m_frameHistory.size(); ++i) {
        total += m_frameHistory[i].GetTotalGPUTimeMs();
    }
    return total / static_cast<double>(count);
}

double GPUProfiler::GetScopeTimeMs(const char* name) const {
    const GPUFrameProfile* frame = GetLastResolvedFrame();
    if (!frame || !name) {
        return 0.0;
    }
    const GPUTimestamp* ts = frame->FindTimestamp(name);
    return ts ? ts->GetDurationMs(frame->gpuFrequency) : 0.0;
}

double GPUProfiler::GetAverageScopeTimeMs(const char* name, size_t frameCount) const {
    if (!name || m_frameHistory.empty() || frameCount == 0) {
        return 0.0;
    }
    const size_t count = std::min(frameCount, m_frameHistory.size());
    double total = 0.0;
    uint32_t hits = 0;
    for (size_t i = m_frameHistory.size() - count; i < m_frameHistory.size(); ++i) {
        const GPUTimestamp* ts = m_frameHistory[i].FindTimestamp(name);
        if (ts) {
            total += ts->GetDurationMs(m_frameHistory[i].gpuFrequency);
            ++hits;
        }
    }
    return hits > 0 ? total / static_cast<double>(hits) : 0.0;
}

const GPUPipelineStats* GPUProfiler::GetLastPipelineStats() const {
    const GPUFrameProfile* frame = GetLastResolvedFrame();
    return frame ? &frame->pipelineStats : nullptr;
}

void GPUProfiler::WaitForFrame(uint64_t) {}

bool GPUProfiler::ExportToJSON(const std::string& path) const {
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }

    out << "{\n  \"frames\": [\n";
    for (size_t i = 0; i < m_frameHistory.size(); ++i) {
        const GPUFrameProfile& frame = m_frameHistory[i];
        out << "    {\"frame\": " << frame.frameNumber
            << ", \"gpu_ms\": " << frame.GetTotalGPUTimeMs()
            << ", \"passes\": [";
        for (size_t p = 0; p < frame.timestamps.size(); ++p) {
            const GPUTimestamp& ts = frame.timestamps[p];
            out << "{\"name\":\"" << ts.name << "\",\"category\":\"" << ts.category
                << "\",\"ms\":" << ts.GetDurationMs(frame.gpuFrequency)
                << ",\"depth\":" << ts.depth << "}";
            if (p + 1 < frame.timestamps.size()) {
                out << ",";
            }
        }
        out << "]}";
        if (i + 1 < m_frameHistory.size()) {
            out << ",";
        }
        out << "\n";
    }
    out << "  ]\n}\n";
    return true;
}

void GPUMemoryStats::Query(IDXGIAdapter3* adapter) {
    if (!adapter) {
        return;
    }
    DXGI_QUERY_VIDEO_MEMORY_INFO info{};
    if (SUCCEEDED(adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info))) {
        usedVideoMemory = static_cast<uint64_t>(info.CurrentUsage);
        totalVideoMemory = static_cast<uint64_t>(info.Budget);
        availableVideoMemory = static_cast<uint64_t>(info.AvailableForReservation);
    }
}

GPUMemoryTracker& GPUMemoryTracker::Get() {
    static GPUMemoryTracker tracker;
    return tracker;
}

void GPUMemoryTracker::Initialize(IDXGIAdapter3* adapter) {
    m_adapter = adapter;
}

void GPUMemoryTracker::Update() {
    m_stats.Query(m_adapter);
}

void GPUMemoryTracker::TrackAllocation(const std::string& name, uint64_t size, const std::string& category) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allocations.push_back({name, category, size, 0});
}

void GPUMemoryTracker::TrackDeallocation(const std::string& name) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_allocations.erase(
        std::remove_if(m_allocations.begin(), m_allocations.end(),
                       [&](const AllocationInfo& info) { return info.name == name; }),
        m_allocations.end());
}

std::vector<RenderPassTiming> BuildRenderPassTimings(const GPUFrameProfile& frame) {
    std::vector<RenderPassTiming> out;
    const double total = std::max(0.0001, frame.GetTotalGPUTimeMs());
    for (const GPUTimestamp& ts : frame.timestamps) {
        RenderPassTiming timing{};
        timing.passName = ts.name;
        timing.timeMs = ts.GetDurationMs(frame.gpuFrequency);
        timing.percentOfFrame = (timing.timeMs / total) * 100.0;
        out.push_back(timing);
    }
    return out;
}

} // namespace Cortex::Debug
