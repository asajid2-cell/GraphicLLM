// CPUProfiler.h
// Hierarchical CPU profiling system with scope-based timing.
// Supports multi-threaded profiling and detailed frame analysis.

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>
#include <atomic>
#include <thread>
#include <memory>
#include <functional>

namespace Cortex::Debug {

// ============================================================================
// Profiler Types
// ============================================================================

using ProfileClock = std::chrono::high_resolution_clock;
using TimePoint = ProfileClock::time_point;
using Duration = std::chrono::nanoseconds;

// ============================================================================
// Profile Sample
// ============================================================================

struct ProfileSample {
    const char* name;           // Name of the scope (static string)
    const char* category;       // Category (e.g., "Render", "Physics")
    const char* file;           // Source file
    int line;                   // Line number

    TimePoint startTime;
    TimePoint endTime;

    uint32_t parentIndex;       // Index of parent sample (UINT32_MAX if root)
    uint32_t depth;             // Depth in call hierarchy
    std::thread::id threadId;   // Thread that recorded this sample

    // Calculate duration in nanoseconds
    int64_t GetDurationNs() const {
        return std::chrono::duration_cast<Duration>(endTime - startTime).count();
    }

    // Calculate duration in milliseconds
    double GetDurationMs() const {
        return GetDurationNs() / 1000000.0;
    }

    // Calculate duration in microseconds
    double GetDurationUs() const {
        return GetDurationNs() / 1000.0;
    }
};

// ============================================================================
// Frame Profile Data
// ============================================================================

struct FrameProfile {
    uint64_t frameNumber;
    TimePoint frameStart;
    TimePoint frameEnd;

    std::vector<ProfileSample> samples;

    // Per-thread sample indices
    std::unordered_map<std::thread::id, std::vector<uint32_t>> threadSamples;

    // Get frame duration
    double GetFrameTimeMs() const {
        auto duration = std::chrono::duration_cast<Duration>(frameEnd - frameStart);
        return duration.count() / 1000000.0;
    }

    // Get FPS
    double GetFPS() const {
        double frameTime = GetFrameTimeMs();
        return frameTime > 0 ? 1000.0 / frameTime : 0.0;
    }
};

// ============================================================================
// Aggregated Profile Statistics
// ============================================================================

struct ProfileStatistics {
    const char* name;
    const char* category;

    // Sample count
    uint32_t callCount = 0;

    // Timing (in nanoseconds)
    int64_t totalTime = 0;
    int64_t minTime = INT64_MAX;
    int64_t maxTime = 0;
    int64_t avgTime = 0;

    // Self time (excluding children)
    int64_t selfTime = 0;

    // Percentage of frame time
    double percentOfFrame = 0.0;

    // Get average in milliseconds
    double GetAvgMs() const { return avgTime / 1000000.0; }
    double GetMinMs() const { return minTime / 1000000.0; }
    double GetMaxMs() const { return maxTime / 1000000.0; }
    double GetTotalMs() const { return totalTime / 1000000.0; }
    double GetSelfMs() const { return selfTime / 1000000.0; }
};

// ============================================================================
// Thread Profile State
// ============================================================================

struct ThreadProfileState {
    std::thread::id threadId;
    std::string threadName;

    // Current scope stack
    std::vector<uint32_t> scopeStack;

    // Samples for current frame
    std::vector<ProfileSample> samples;

    // Get current parent index
    uint32_t GetCurrentParent() const {
        return scopeStack.empty() ? UINT32_MAX : scopeStack.back();
    }

    // Get current depth
    uint32_t GetCurrentDepth() const {
        return static_cast<uint32_t>(scopeStack.size());
    }
};

// ============================================================================
// CPU Profiler
// ============================================================================

class CPUProfiler {
public:
    // Singleton access
    static CPUProfiler& Get();

    CPUProfiler();
    ~CPUProfiler();

    // Enable/Disable profiling
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    // Frame management
    void BeginFrame();
    void EndFrame();

    // Scope profiling
    void BeginScope(const char* name, const char* category = "General",
                     const char* file = nullptr, int line = 0);
    void EndScope();

    // Thread naming
    void SetThreadName(const std::string& name);
    std::string GetThreadName(std::thread::id id) const;

    // Get current frame data
    const FrameProfile* GetCurrentFrame() const;

    // Get frame history
    const std::vector<FrameProfile>& GetFrameHistory() const { return m_frameHistory; }
    size_t GetFrameHistorySize() const { return m_frameHistory.size(); }

    // Get specific frame
    const FrameProfile* GetFrame(uint64_t frameNumber) const;
    const FrameProfile* GetPreviousFrame(size_t offset = 1) const;

    // Statistics
    void CalculateStatistics(const FrameProfile& frame,
                              std::vector<ProfileStatistics>& outStats) const;
    void CalculateAverageStatistics(size_t frameCount,
                                      std::vector<ProfileStatistics>& outStats) const;

    // Find hotspots
    std::vector<ProfileStatistics> GetHotspots(const FrameProfile& frame,
                                                  size_t maxCount = 10) const;

    // Frame time tracking
    double GetLastFrameTimeMs() const { return m_lastFrameTime; }
    double GetAverageFrameTimeMs(size_t frameCount = 60) const;
    double GetMinFrameTimeMs(size_t frameCount = 60) const;
    double GetMaxFrameTimeMs(size_t frameCount = 60) const;

    // FPS tracking
    double GetCurrentFPS() const { return m_lastFrameTime > 0 ? 1000.0 / m_lastFrameTime : 0; }
    double GetAverageFPS(size_t frameCount = 60) const;

    // Configuration
    void SetMaxFrameHistory(size_t count) { m_maxFrameHistory = count; }
    size_t GetMaxFrameHistory() const { return m_maxFrameHistory; }

    void SetMaxSamplesPerFrame(size_t count) { m_maxSamplesPerFrame = count; }
    size_t GetMaxSamplesPerFrame() const { return m_maxSamplesPerFrame; }

    // Export/Import
    bool ExportToJSON(const std::string& path) const;
    bool ExportToChrome(const std::string& path) const;  // Chrome tracing format
    bool ImportFromJSON(const std::string& path);

    // Callbacks
    using FrameCallback = std::function<void(const FrameProfile&)>;
    void SetOnFrameEnd(FrameCallback callback) { m_onFrameEnd = callback; }

    // Spike detection
    void SetSpikeThreshold(double ms) { m_spikeThreshold = ms; }
    double GetSpikeThreshold() const { return m_spikeThreshold; }
    using SpikeCallback = std::function<void(const FrameProfile&, double frameTimeMs)>;
    void SetOnSpike(SpikeCallback callback) { m_onSpike = callback; }

    // Clear all data
    void Clear();

private:
    // Get thread state (creates if not exists)
    ThreadProfileState& GetThreadState();

    // Merge thread samples into frame
    void MergeThreadSamples(FrameProfile& frame);

    bool m_enabled = true;
    std::atomic<uint64_t> m_frameNumber{0};

    // Current frame being recorded
    std::unique_ptr<FrameProfile> m_currentFrame;
    TimePoint m_frameStartTime;

    // Frame history
    std::vector<FrameProfile> m_frameHistory;
    size_t m_maxFrameHistory = 300;  // 5 seconds at 60 fps
    size_t m_maxSamplesPerFrame = 10000;

    // Per-thread state
    std::unordered_map<std::thread::id, std::unique_ptr<ThreadProfileState>> m_threadStates;
    std::mutex m_threadStateMutex;

    // Thread names
    std::unordered_map<std::thread::id, std::string> m_threadNames;
    std::mutex m_threadNameMutex;

    // Frame timing
    double m_lastFrameTime = 0.0;

    // Callbacks
    FrameCallback m_onFrameEnd;
    SpikeCallback m_onSpike;
    double m_spikeThreshold = 33.33;  // 30 FPS threshold
};

// ============================================================================
// Scoped Profile Timer
// ============================================================================

class ScopedProfileTimer {
public:
    ScopedProfileTimer(const char* name, const char* category = "General",
                        const char* file = nullptr, int line = 0) {
        CPUProfiler::Get().BeginScope(name, category, file, line);
    }

    ~ScopedProfileTimer() {
        CPUProfiler::Get().EndScope();
    }

    // Non-copyable
    ScopedProfileTimer(const ScopedProfileTimer&) = delete;
    ScopedProfileTimer& operator=(const ScopedProfileTimer&) = delete;
};

// ============================================================================
// Profile Macros
// ============================================================================

#ifdef CORTEX_ENABLE_PROFILING

// Basic profile scope
#define PROFILE_SCOPE(name) \
    Cortex::Debug::ScopedProfileTimer _profiler##__LINE__(name, "General", __FILE__, __LINE__)

// Profile scope with category
#define PROFILE_SCOPE_CATEGORY(name, category) \
    Cortex::Debug::ScopedProfileTimer _profiler##__LINE__(name, category, __FILE__, __LINE__)

// Profile function (uses function name)
#define PROFILE_FUNCTION() \
    Cortex::Debug::ScopedProfileTimer _profiler##__LINE__(__FUNCTION__, "General", __FILE__, __LINE__)

// Profile function with category
#define PROFILE_FUNCTION_CATEGORY(category) \
    Cortex::Debug::ScopedProfileTimer _profiler##__LINE__(__FUNCTION__, category, __FILE__, __LINE__)

// Named thread
#define PROFILE_SET_THREAD_NAME(name) \
    Cortex::Debug::CPUProfiler::Get().SetThreadName(name)

// Frame markers
#define PROFILE_BEGIN_FRAME() \
    Cortex::Debug::CPUProfiler::Get().BeginFrame()

#define PROFILE_END_FRAME() \
    Cortex::Debug::CPUProfiler::Get().EndFrame()

#else

// No-op when profiling disabled
#define PROFILE_SCOPE(name) ((void)0)
#define PROFILE_SCOPE_CATEGORY(name, category) ((void)0)
#define PROFILE_FUNCTION() ((void)0)
#define PROFILE_FUNCTION_CATEGORY(category) ((void)0)
#define PROFILE_SET_THREAD_NAME(name) ((void)0)
#define PROFILE_BEGIN_FRAME() ((void)0)
#define PROFILE_END_FRAME() ((void)0)

#endif

// ============================================================================
// Profile Categories (for consistent naming)
// ============================================================================

namespace ProfileCategory {
    constexpr const char* Render = "Render";
    constexpr const char* Physics = "Physics";
    constexpr const char* Audio = "Audio";
    constexpr const char* Script = "Script";
    constexpr const char* AI = "AI";
    constexpr const char* Network = "Network";
    constexpr const char* Animation = "Animation";
    constexpr const char* UI = "UI";
    constexpr const char* IO = "IO";
    constexpr const char* Memory = "Memory";
    constexpr const char* Scene = "Scene";
    constexpr const char* Editor = "Editor";
}

// ============================================================================
// Accumulating Timer (for timing repeated operations)
// ============================================================================

class AccumulatingTimer {
public:
    AccumulatingTimer(const char* name, const char* category = "General")
        : m_name(name), m_category(category) {}

    // Start timing
    void Start() {
        m_startTime = ProfileClock::now();
    }

    // Stop timing and accumulate
    void Stop() {
        auto endTime = ProfileClock::now();
        auto duration = std::chrono::duration_cast<Duration>(endTime - m_startTime);
        m_totalTime += duration.count();
        m_callCount++;
    }

    // Get accumulated time in ms
    double GetTotalMs() const { return m_totalTime / 1000000.0; }
    double GetAverageMs() const {
        return m_callCount > 0 ? GetTotalMs() / m_callCount : 0.0;
    }

    // Get call count
    uint32_t GetCallCount() const { return m_callCount; }

    // Reset
    void Reset() {
        m_totalTime = 0;
        m_callCount = 0;
    }

    // Report to profiler
    void Report() {
        if (m_callCount > 0) {
            // Would add as custom metric
        }
    }

private:
    const char* m_name;
    const char* m_category;
    TimePoint m_startTime;
    int64_t m_totalTime = 0;
    uint32_t m_callCount = 0;
};

// ============================================================================
// Frame Time Graph Data
// ============================================================================

struct FrameTimeGraph {
    std::vector<float> frameTimes;      // In milliseconds
    std::vector<float> categories[8];   // Per-category times

    size_t maxSamples = 300;
    size_t currentIndex = 0;

    void AddSample(float frameTimeMs, const float* categoryTimes = nullptr) {
        if (frameTimes.size() < maxSamples) {
            frameTimes.push_back(frameTimeMs);
            for (int i = 0; i < 8 && categoryTimes; ++i) {
                categories[i].push_back(categoryTimes[i]);
            }
        } else {
            frameTimes[currentIndex] = frameTimeMs;
            for (int i = 0; i < 8 && categoryTimes; ++i) {
                categories[i][currentIndex] = categoryTimes[i];
            }
            currentIndex = (currentIndex + 1) % maxSamples;
        }
    }

    float GetAverage() const {
        if (frameTimes.empty()) return 0.0f;
        float sum = 0.0f;
        for (float t : frameTimes) sum += t;
        return sum / frameTimes.size();
    }

    float GetMin() const {
        if (frameTimes.empty()) return 0.0f;
        float minVal = frameTimes[0];
        for (float t : frameTimes) minVal = std::min(minVal, t);
        return minVal;
    }

    float GetMax() const {
        if (frameTimes.empty()) return 0.0f;
        float maxVal = frameTimes[0];
        for (float t : frameTimes) maxVal = std::max(maxVal, t);
        return maxVal;
    }
};

} // namespace Cortex::Debug
