#pragma once

#include "Utils/Result.h"
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

namespace Cortex::AI::Vision {

// How a generated texture will be used in the renderer.
enum class TextureUsage : uint32_t {
    Albedo    = 0,
    Normal    = 1,
    Roughness = 2,
    Metalness = 3,
    Environment = 4, // IBL envmap / skybox
    Skybox      = 5
};

// Basic configuration for the Dreamer async texture generator.
struct DreamerConfig {
    uint32_t defaultWidth  = 512;
    uint32_t defaultHeight = 512;
    uint32_t maxWidth      = 1024;
    uint32_t maxHeight     = 1024;

    // Optional GPU diffusion backend (TensorRT / CUDA). When true and the
    // engine is built with CORTEX_ENABLE_TENSORRT, DiffusionEngine will
    // attempt to load TensorRT engines from enginePath and run generation
    // on the GPU. Otherwise, the CPU procedural stub is used.
    //
    // By convention, enginePath is a directory like "models/dreamer" that
    // contains SDXL-Turbo engines named:
    //   sdxl_turbo_unet_768x768.engine
    //   sdxl_turbo_vae_decoder_768x768.engine
    bool useGPU = false;
    std::string enginePath;
};

// Request for a generated texture targeting a specific tagged entity.
struct TextureRequest {
    std::string targetName;   // TagComponent::tag of the entity to receive the texture
    std::string prompt;       // Free-form description, used to seed the pattern

    // How the texture will be bound (albedo/normal/env/skybox, etc.).
    TextureUsage usage = TextureUsage::Albedo;

    // Optional high-level material preset name, so the Architect can request
    // "wet_cobblestone", "brushed_metal", etc. and the Dreamer can bias style.
    std::string materialPreset;

    // Optional explicit seed; when 0, a seed is derived from the prompt.
    uint32_t seed = 0;

    uint32_t width  = 0;      // Optional; 0 uses DreamerConfig defaults
    uint32_t height = 0;
};

// Completed texture generation result (CPU-side RGBA8 pixels).
struct TextureResult {
    std::string targetName;
    std::string prompt;
    TextureUsage usage = TextureUsage::Albedo;
    std::string materialPreset;
    uint32_t seed = 0;
    bool success = false;
    std::string message;
    uint32_t width  = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels; // RGBA8, row-major
};

class DiffusionEngine;

struct DiffusionEngineDeleter {
    void operator()(DiffusionEngine* ptr) const noexcept;
};

// Phase 3: The Dreamer - async CPU-side texture generator.
//
// This does not talk to the GPU directly. Instead, it produces RGBA8 pixels
// on a worker thread and the Engine uploads them to GPU textures on the
// main thread via the Renderer, keeping GPU usage single-threaded.
class DreamerService {
public:
    DreamerService() = default;
    ~DreamerService();

    // Initialize the Dreamer worker.
    Result<void> Initialize(const DreamerConfig& config);

    // Shutdown worker thread and clear pending jobs/results.
    void Shutdown();

    // Submit a texture generation request (thread-safe, returns immediately).
    void SubmitRequest(const TextureRequest& request);

    // Drain all finished results into a vector for processing on the caller
    // thread (typically the main thread in Engine::Update).
    std::vector<TextureResult> ConsumeFinished();

private:
    DreamerConfig m_config{};

    std::atomic<bool> m_running{false};
    std::thread m_worker;

    std::mutex m_requestMutex;
    std::condition_variable m_requestCv;
    std::queue<TextureRequest> m_requests;

    std::mutex m_resultMutex;
    std::queue<TextureResult> m_results;

    std::unique_ptr<DiffusionEngine, DiffusionEngineDeleter> m_diffusion;

    void WorkerLoop();
    TextureResult GenerateTexture(const TextureRequest& request);
};

} // namespace Cortex::AI::Vision
