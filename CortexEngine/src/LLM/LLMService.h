#pragma once

#include "Utils/Result.h"
#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <thread>

namespace Cortex::LLM {

// Configuration for LLM inference
struct LLMConfig {
    std::string modelPath;
    int contextSize = 2048;
    int threads = 4;
    float temperature = 0.1f;
    int maxTokens = 128;
    // GPU offload: number of transformer layers to place on GPU (0 = CPU only)
    int gpuLayers = 999;
};

// Response from LLM
struct LLMResponse {
    std::string text;
    bool success = false;
    float inferenceTime = 0.0f;
};

// Async callback for LLM completion
using LLMCallback = std::function<void(const LLMResponse&)>;

/**
 * The Architect - Natural Language Scene Controller
 *
 * Async loop running at ~1-5 seconds per inference
 * Converts natural language prompts into scene manipulation commands
 *
 * Phase 2: Using llama.cpp for local inference
 * Future: Can swap to remote APIs or other backends
 */
class LLMService {
public:
    LLMService() = default;
    ~LLMService();

    // Initialize the LLM with a model file
    Result<void> Initialize(const LLMConfig& config);

    // Shutdown and cleanup
    void Shutdown();

    // Submit a prompt for async inference
    // Returns immediately, calls callback when done
    void SubmitPrompt(const std::string& prompt, LLMCallback callback);

    // Pump completed jobs on the calling thread (main thread) to execute callbacks safely
    void PumpCallbacks();

    // Check if currently processing
    bool IsBusy() const { return m_isBusy.load(); }

    // Get current model info
    std::string GetModelInfo() const;

private:
    void* m_model = nullptr;      // llama_model*
    void* m_context = nullptr;    // llama_context*
    LLMConfig m_config;
    std::atomic<bool> m_isBusy{false};
    std::atomic<int> m_activeJobs{0};
    std::atomic<bool> m_shuttingDown{false};
    std::mutex m_waitMutex;
    std::condition_variable m_waitCv;
    std::mutex m_callbackMutex;
    std::queue<std::pair<LLMCallback, LLMResponse>> m_pendingCallbacks;
    std::queue<std::pair<std::string, LLMCallback>> m_jobQueue;
    std::mutex m_jobMutex;
    std::condition_variable m_jobCv;
    std::thread m_workerThread;
    std::atomic<bool> m_workerRunning{false};

    // Worker entry point (runs on background thread)
    void ProcessJob(std::string promptCopy, LLMCallback callback);
    void WorkerLoop();

    // System prompt for scene understanding
    std::string GetSystemPrompt() const;
};

} // namespace Cortex::LLM
