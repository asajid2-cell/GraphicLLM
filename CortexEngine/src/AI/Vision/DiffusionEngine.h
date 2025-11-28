#pragma once

#include "Utils/Result.h"
#include <string>

#if CORTEX_ENABLE_TENSORRT
#include <NvInfer.h>
#include <cuda_runtime.h>
#include <filesystem>
#endif

namespace Cortex::AI::Vision {

struct TextureRequest;
struct TextureResult;
struct DreamerConfig;

// Phase 3: DiffusionEngine abstraction.
//
// This is the seam where GPU / CUDA / TensorRT / DirectML backends will plug
// in later. For now it hosts the CPU-side procedural texture generator that
// was previously in DreamerService::GenerateTexture, with an optional hook
// for a GPU backend behind CORTEX_ENABLE_TENSORRT.
class DiffusionEngine {
public:
    DiffusionEngine() = default;
    ~DiffusionEngine();

    // Optional one-time initialization with Dreamer configuration. This is
    // where a TensorRT engine path and resolution would be wired up when
    // GPU diffusion is enabled.
    Cortex::Result<void> Initialize(const DreamerConfig& config);

    Cortex::Result<TextureResult> Run(const TextureRequest& request);

private:
    // CPU-only procedural generator used as the default implementation and
    // as a fallback when GPU diffusion is unavailable.
    Cortex::Result<TextureResult> RunCPU(const TextureRequest& request);

#if CORTEX_ENABLE_TENSORRT
    // TensorRT runtime / engine handles (created on demand). We currently wire
    // up a SDXL-Turbo VAE decoder and (optionally) UNet. If anything fails, we
    // fall back to the CPU stub.
    bool m_gpuRequested = false;
    bool m_gpuReady = false;
    std::string m_enginePath;   // Directory containing SDXL-Turbo engines

    nvinfer1::IRuntime*       m_trtRuntime   = nullptr;

    // VAE decoder (latent -> image)
    nvinfer1::ICudaEngine*    m_vaeEngine    = nullptr;
    nvinfer1::IExecutionContext* m_vaeContext = nullptr;

    void* m_vaeLatentDevice = nullptr;
    void* m_vaeImageDevice  = nullptr;
    size_t m_vaeLatentBytes = 0;
    size_t m_vaeImageBytes  = 0;
    uint32_t m_vaeWidth  = 0;
    uint32_t m_vaeHeight = 0;

    // UNet (noise latent + conditioning -> denoised latent). This is optional:
    // if the UNet engine fails to load we still run a VAE-only path.
    nvinfer1::ICudaEngine*       m_unetEngine    = nullptr;
    nvinfer1::IExecutionContext* m_unetContext   = nullptr;

    void*  m_unetSampleDevice          = nullptr;
    void*  m_unetTimestepDevice        = nullptr;
    void*  m_unetEncoderHiddenDevice   = nullptr;
    void*  m_unetPooledEmbedsDevice    = nullptr;
    void*  m_unetTimeIdsDevice         = nullptr;
    void*  m_unetOutSampleDevice       = nullptr;
    size_t m_unetSampleBytes           = 0;
    size_t m_unetTimestepBytes         = 0;
    size_t m_unetEncoderHiddenBytes    = 0;
    size_t m_unetPooledEmbedsBytes     = 0;
    size_t m_unetTimeIdsBytes          = 0;
    size_t m_unetOutSampleBytes        = 0;

    Cortex::Result<void> InitializeGPU();
    Cortex::Result<TextureResult> RunGPU(const TextureRequest& request);
#endif
};

} // namespace Cortex::AI::Vision
