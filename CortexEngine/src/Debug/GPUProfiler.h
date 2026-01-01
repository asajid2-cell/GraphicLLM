// GPUProfiler.h
// GPU profiling using D3D12 timestamp queries.
// Provides per-pass timing and pipeline statistics.

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <queue>
#include <d3d12.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace Cortex::Debug {

// ============================================================================
// GPU Timestamp Query
// ============================================================================

struct GPUTimestamp {
    const char* name;
    const char* category;
    uint64_t startTick;
    uint64_t endTick;
    uint32_t depth;

    // Calculate duration in nanoseconds
    double GetDurationNs(uint64_t frequency) const {
        if (frequency == 0) return 0.0;
        return static_cast<double>(endTick - startTick) / frequency * 1e9;
    }

    // Calculate duration in milliseconds
    double GetDurationMs(uint64_t frequency) const {
        return GetDurationNs(frequency) / 1e6;
    }
};

// ============================================================================
// Pipeline Statistics
// ============================================================================

struct GPUPipelineStats {
    uint64_t inputAssemblyVertices = 0;
    uint64_t inputAssemblyPrimitives = 0;
    uint64_t vertexShaderInvocations = 0;
    uint64_t geometryShaderInvocations = 0;
    uint64_t geometryShaderPrimitives = 0;
    uint64_t clipperInvocations = 0;
    uint64_t clipperPrimitives = 0;
    uint64_t pixelShaderInvocations = 0;
    uint64_t hullShaderInvocations = 0;
    uint64_t domainShaderInvocations = 0;
    uint64_t computeShaderInvocations = 0;

    // Derived metrics
    uint64_t GetTrianglesRendered() const {
        return inputAssemblyPrimitives;
    }

    uint64_t GetPixelsShaded() const {
        return pixelShaderInvocations;
    }

    double GetOverdraw() const {
        // Would need viewport size to calculate accurately
        return 0.0;
    }
};

// ============================================================================
// GPU Frame Profile
// ============================================================================

struct GPUFrameProfile {
    uint64_t frameNumber;
    uint64_t gpuFrequency;

    std::vector<GPUTimestamp> timestamps;
    GPUPipelineStats pipelineStats;

    // Get total GPU time
    double GetTotalGPUTimeMs() const {
        if (timestamps.empty()) return 0.0;

        uint64_t minStart = UINT64_MAX;
        uint64_t maxEnd = 0;

        for (const auto& ts : timestamps) {
            if (ts.depth == 0) {
                minStart = std::min(minStart, ts.startTick);
                maxEnd = std::max(maxEnd, ts.endTick);
            }
        }

        if (gpuFrequency == 0 || minStart == UINT64_MAX) return 0.0;
        return static_cast<double>(maxEnd - minStart) / gpuFrequency * 1000.0;
    }

    // Find timestamp by name
    const GPUTimestamp* FindTimestamp(const char* name) const {
        for (const auto& ts : timestamps) {
            if (strcmp(ts.name, name) == 0) return &ts;
        }
        return nullptr;
    }
};

// ============================================================================
// Query Frame (double/triple buffered)
// ============================================================================

struct QueryFrame {
    ComPtr<ID3D12QueryHeap> timestampHeap;
    ComPtr<ID3D12QueryHeap> pipelineStatsHeap;
    ComPtr<ID3D12Resource> readbackBuffer;

    uint32_t timestampCount = 0;
    uint32_t maxTimestamps = 256;

    bool pending = false;
    uint64_t fenceValue = 0;

    // Timestamp mapping (index -> name/category)
    std::vector<std::pair<const char*, const char*>> timestampNames;
    std::vector<uint32_t> timestampDepths;
    std::vector<uint32_t> scopeStack;
};

// ============================================================================
// GPU Profiler
// ============================================================================

class GPUProfiler {
public:
    // Singleton access
    static GPUProfiler& Get();

    GPUProfiler();
    ~GPUProfiler();

    // Initialize with D3D12 device
    bool Initialize(ID3D12Device* device, ID3D12CommandQueue* commandQueue);
    void Shutdown();

    // Enable/Disable
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    // Frame management
    void BeginFrame(ID3D12GraphicsCommandList* commandList);
    void EndFrame(ID3D12GraphicsCommandList* commandList);

    // Resolve queries (call after GPU work is complete)
    void ResolveQueries();

    // Scope profiling
    void BeginScope(ID3D12GraphicsCommandList* commandList, const char* name,
                     const char* category = "GPU");
    void EndScope(ID3D12GraphicsCommandList* commandList);

    // Pipeline statistics (per-frame)
    void BeginPipelineStats(ID3D12GraphicsCommandList* commandList);
    void EndPipelineStats(ID3D12GraphicsCommandList* commandList);

    // Get results
    const GPUFrameProfile* GetLastResolvedFrame() const;
    const std::vector<GPUFrameProfile>& GetFrameHistory() const { return m_frameHistory; }

    // Wait for specific frame
    void WaitForFrame(uint64_t frameNumber);

    // Statistics helpers
    double GetLastFrameGPUTimeMs() const;
    double GetAverageGPUTimeMs(size_t frameCount = 60) const;

    // Get time for specific scope
    double GetScopeTimeMs(const char* name) const;
    double GetAverageScopeTimeMs(const char* name, size_t frameCount = 60) const;

    // Get pipeline stats
    const GPUPipelineStats* GetLastPipelineStats() const;

    // Configuration
    void SetMaxFrameHistory(size_t count) { m_maxFrameHistory = count; }
    void SetBufferCount(uint32_t count);

    // Export
    bool ExportToJSON(const std::string& path) const;

    // Callbacks
    using FrameCallback = std::function<void(const GPUFrameProfile&)>;
    void SetOnFrameResolved(FrameCallback callback) { m_onFrameResolved = callback; }

private:
    // Create query resources
    bool CreateQueryFrame(QueryFrame& frame);

    // Get current query frame
    QueryFrame& GetCurrentQueryFrame();

    // Read back query results
    bool ReadbackQueries(QueryFrame& frame, GPUFrameProfile& outProfile);

    ID3D12Device* m_device = nullptr;
    ID3D12CommandQueue* m_commandQueue = nullptr;

    bool m_enabled = true;
    bool m_initialized = false;

    // Frame counter
    uint64_t m_frameNumber = 0;
    uint64_t m_gpuFrequency = 0;

    // Query frames (ring buffer)
    std::vector<QueryFrame> m_queryFrames;
    uint32_t m_currentFrameIndex = 0;
    uint32_t m_bufferCount = 3;

    // Fence for synchronization
    ComPtr<ID3D12Fence> m_fence;
    HANDLE m_fenceEvent = nullptr;
    uint64_t m_fenceValue = 0;

    // Frame history
    std::vector<GPUFrameProfile> m_frameHistory;
    size_t m_maxFrameHistory = 300;

    // Pending frames awaiting readback
    std::queue<uint32_t> m_pendingFrames;

    // Callbacks
    FrameCallback m_onFrameResolved;

    // Current state
    bool m_inFrame = false;
    bool m_pipelineStatsActive = false;
};

// ============================================================================
// Scoped GPU Profile Timer
// ============================================================================

class ScopedGPUTimer {
public:
    ScopedGPUTimer(ID3D12GraphicsCommandList* commandList, const char* name,
                    const char* category = "GPU")
        : m_commandList(commandList) {
        GPUProfiler::Get().BeginScope(commandList, name, category);
    }

    ~ScopedGPUTimer() {
        GPUProfiler::Get().EndScope(m_commandList);
    }

    // Non-copyable
    ScopedGPUTimer(const ScopedGPUTimer&) = delete;
    ScopedGPUTimer& operator=(const ScopedGPUTimer&) = delete;

private:
    ID3D12GraphicsCommandList* m_commandList;
};

// ============================================================================
// GPU Profile Macros
// ============================================================================

#ifdef CORTEX_ENABLE_GPU_PROFILING

#define GPU_PROFILE_SCOPE(cmdList, name) \
    Cortex::Debug::ScopedGPUTimer _gpuProfiler##__LINE__(cmdList, name)

#define GPU_PROFILE_SCOPE_CATEGORY(cmdList, name, category) \
    Cortex::Debug::ScopedGPUTimer _gpuProfiler##__LINE__(cmdList, name, category)

#define GPU_PROFILE_BEGIN_FRAME(cmdList) \
    Cortex::Debug::GPUProfiler::Get().BeginFrame(cmdList)

#define GPU_PROFILE_END_FRAME(cmdList) \
    Cortex::Debug::GPUProfiler::Get().EndFrame(cmdList)

#else

#define GPU_PROFILE_SCOPE(cmdList, name) ((void)0)
#define GPU_PROFILE_SCOPE_CATEGORY(cmdList, name, category) ((void)0)
#define GPU_PROFILE_BEGIN_FRAME(cmdList) ((void)0)
#define GPU_PROFILE_END_FRAME(cmdList) ((void)0)

#endif

// ============================================================================
// GPU Memory Tracker
// ============================================================================

struct GPUMemoryStats {
    uint64_t totalVideoMemory = 0;
    uint64_t availableVideoMemory = 0;
    uint64_t usedVideoMemory = 0;

    // Per-category usage
    uint64_t textureMemory = 0;
    uint64_t bufferMemory = 0;
    uint64_t renderTargetMemory = 0;
    uint64_t depthStencilMemory = 0;
    uint64_t shaderMemory = 0;

    // Query from adapter
    void Query(IDXGIAdapter3* adapter);
};

class GPUMemoryTracker {
public:
    static GPUMemoryTracker& Get();

    void Initialize(IDXGIAdapter3* adapter);
    void Update();

    const GPUMemoryStats& GetStats() const { return m_stats; }

    // Track allocations
    void TrackAllocation(const std::string& name, uint64_t size, const std::string& category);
    void TrackDeallocation(const std::string& name);

    // Get allocation info
    struct AllocationInfo {
        std::string name;
        std::string category;
        uint64_t size;
        uint64_t timestamp;
    };
    const std::vector<AllocationInfo>& GetAllocations() const { return m_allocations; }

private:
    IDXGIAdapter3* m_adapter = nullptr;
    GPUMemoryStats m_stats;
    std::vector<AllocationInfo> m_allocations;
    std::mutex m_mutex;
};

// ============================================================================
// Render Pass Timing Breakdown
// ============================================================================

struct RenderPassTiming {
    const char* passName;
    double timeMs;
    double percentOfFrame;

    // Sub-passes
    std::vector<RenderPassTiming> subPasses;
};

// Build hierarchical timing from GPU profile
std::vector<RenderPassTiming> BuildRenderPassTimings(const GPUFrameProfile& frame);

// ============================================================================
// GPU Profiler Overlay
// ============================================================================

struct GPUProfilerOverlay {
    bool visible = false;
    bool showPipelineStats = true;
    bool showTimingGraph = true;
    bool showMemoryStats = true;

    // Graph settings
    float graphHeight = 100.0f;
    float graphTimeRange = 33.33f;  // ms (30 FPS line)

    // Colors (category -> color)
    std::unordered_map<std::string, uint32_t> categoryColors;

    GPUProfilerOverlay() {
        // Default colors
        categoryColors["GPU"] = 0xFF4444FF;          // Red
        categoryColors["Shadow"] = 0xFF44FF44;       // Green
        categoryColors["GBuffer"] = 0xFFFF4444;      // Blue
        categoryColors["Lighting"] = 0xFFFFFF44;     // Cyan
        categoryColors["PostProcess"] = 0xFFFF44FF;  // Magenta
        categoryColors["UI"] = 0xFF44FFFF;           // Yellow
    }
};

} // namespace Cortex::Debug
