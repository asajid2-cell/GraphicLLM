#include "LLMService.h"

#include <spdlog/spdlog.h>

#include <chrono>

namespace Cortex::LLM {

LLMService::~LLMService() {
    Shutdown();
}

Result<void> LLMService::Initialize(const LLMConfig& config) {
    m_config = config;
    m_backendInitialized = false;
    m_shuttingDown.store(false);
    m_isBusy.store(false);
    spdlog::info("LLM Service initialized (mock mode - llama backend disabled at build time)");
    return Result<void>::Ok();
}

void LLMService::Shutdown() {
    m_shuttingDown.store(true);
    m_workerRunning.store(false);
    m_isBusy.store(false);
    m_activeJobs.store(0);

    {
        std::lock_guard<std::mutex> lock(m_jobMutex);
        std::queue<Job> empty;
        std::swap(m_jobQueue, empty);
    }

    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        std::queue<std::pair<LLMCallback, LLMResponse>> empty;
        std::swap(m_pendingCallbacks, empty);
    }

    if (m_workerThread.joinable()) {
        m_jobCv.notify_all();
        m_workerThread.join();
    }
}

void LLMService::SubmitPrompt(const std::string& prompt,
                              const std::string& /*sceneSummary*/,
                              bool /*hasShowcase*/,
                              LLMCallback callback) {
    if (!callback) {
        return;
    }

    const auto start = std::chrono::steady_clock::now();
    LLMResponse response{};
    response.success = true;
    response.text =
        R"({"commands":[],"note":"Local llama backend is disabled in this build; configure with -DCORTEX_ENABLE_LLM_BACKEND=ON to enable natural-language scene commands."})";
    if (prompt.empty()) {
        response.text = R"({"commands":[]})";
    }
    const auto end = std::chrono::steady_clock::now();
    response.inferenceTime =
        std::chrono::duration<float>(end - start).count();

    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        m_pendingCallbacks.emplace(std::move(callback), std::move(response));
    }
}

void LLMService::PumpCallbacks() {
    std::queue<std::pair<LLMCallback, LLMResponse>> callbacks;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        std::swap(callbacks, m_pendingCallbacks);
    }

    while (!callbacks.empty()) {
        auto [callback, response] = std::move(callbacks.front());
        callbacks.pop();
        if (callback) {
            callback(response);
        }
    }
}

std::string LLMService::GetModelInfo() const {
    return "LLM backend disabled (mock service)";
}

void LLMService::ProcessJob(std::string userPrompt,
                            std::string /*fullPrompt*/,
                            LLMCallback callback) {
    SubmitPrompt(userPrompt, "", false, std::move(callback));
}

void LLMService::WorkerLoop() {
}

std::string LLMService::GetSystemPrompt() const {
    return {};
}

} // namespace Cortex::LLM
