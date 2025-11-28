#include "AI/Vision/DreamerService.h"
#include "AI/Vision/DiffusionEngine.h"
#include <spdlog/spdlog.h>

namespace Cortex::AI::Vision {

void DiffusionEngineDeleter::operator()(DiffusionEngine* ptr) const noexcept {
    delete ptr;
}

DreamerService::~DreamerService() {
    Shutdown();
}

Result<void> DreamerService::Initialize(const DreamerConfig& config) {
    m_config = config;

    if (!m_diffusion) {
        m_diffusion = std::unique_ptr<DiffusionEngine, DiffusionEngineDeleter>(
            new DiffusionEngine());
    }

    if (m_diffusion) {
        auto diffInit = m_diffusion->Initialize(m_config);
        if (diffInit.IsErr()) {
            spdlog::warn("DiffusionEngine initialization failed: {}", diffInit.Error());
        }
    }

    if (m_running.load()) {
        return Result<void>::Ok();
    }

    m_running = true;
    m_worker = std::thread([this]() { WorkerLoop(); });

    spdlog::info("DreamerService initialized (async CPU texture generator)");
    return Result<void>::Ok();
}

void DreamerService::Shutdown() {
    if (!m_running.exchange(false)) {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
    }
    m_requestCv.notify_all();

    if (m_worker.joinable()) {
        m_worker.join();
    }

    {
        std::lock_guard<std::mutex> lockReq(m_requestMutex);
        std::queue<TextureRequest> emptyReq;
        std::swap(m_requests, emptyReq);
    }
    {
        std::lock_guard<std::mutex> lockRes(m_resultMutex);
        std::queue<TextureResult> emptyRes;
        std::swap(m_results, emptyRes);
    }

    spdlog::info("DreamerService shut down");
}

void DreamerService::SubmitRequest(const TextureRequest& request) {
    if (!m_running.load()) {
        spdlog::warn("DreamerService is not running; ignoring request");
        return;
    }

    TextureRequest clamped = request;

    // When a GPU diffusion backend is configured we currently operate at the
    // fixed resolution the engines were built for (typically 768x768). Force
    // requests to that size so the VAE/UNet tensors and RGBA outputs line up.
    if (m_config.useGPU) {
        clamped.width = m_config.defaultWidth;
        clamped.height = m_config.defaultHeight;
    } else {
        if (clamped.width == 0) {
            clamped.width = m_config.defaultWidth;
        }
        if (clamped.height == 0) {
            clamped.height = m_config.defaultHeight;
        }
    }

    clamped.width = std::max<uint32_t>(64, std::min(clamped.width,  m_config.maxWidth));
    clamped.height = std::max<uint32_t>(64, std::min(clamped.height, m_config.maxHeight));

    {
        std::lock_guard<std::mutex> lock(m_requestMutex);
        m_requests.push(std::move(clamped));
    }
    m_requestCv.notify_one();
}

std::vector<TextureResult> DreamerService::ConsumeFinished() {
    std::vector<TextureResult> out;
    std::lock_guard<std::mutex> lock(m_resultMutex);
    while (!m_results.empty()) {
        out.push_back(std::move(m_results.front()));
        m_results.pop();
    }
    return out;
}

void DreamerService::WorkerLoop() {
    while (true) {
        TextureRequest job;
        {
            std::unique_lock<std::mutex> lock(m_requestMutex);
            m_requestCv.wait(lock, [this]() {
                return !m_running.load() || !m_requests.empty();
            });

            if (!m_running.load() && m_requests.empty()) {
                break;
            }

            job = std::move(m_requests.front());
            m_requests.pop();
        }

        auto result = GenerateTexture(job);

        {
            std::lock_guard<std::mutex> lock(m_resultMutex);
            m_results.push(std::move(result));
        }
    }
}

TextureResult DreamerService::GenerateTexture(const TextureRequest& request) {
    TextureResult fallback{};
    fallback.targetName = request.targetName;
    fallback.prompt = request.prompt;
    fallback.usage = request.usage;
    fallback.materialPreset = request.materialPreset;
    fallback.seed = request.seed;

    if (!m_diffusion) {
        fallback.success = false;
        fallback.message = "DiffusionEngine not initialized";
        return fallback;
    }

    auto res = m_diffusion->Run(request);
    if (res.IsErr()) {
        fallback.success = false;
        fallback.message = res.Error();
        return fallback;
    }

    return res.Value();
}

} // namespace Cortex::AI::Vision
