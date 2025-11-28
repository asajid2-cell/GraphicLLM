#include "AI/Vision/DiffusionEngine.h"
#include "AI/Vision/DreamerService.h"

#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <random>

namespace Cortex::AI::Vision {

namespace {

uint32_t HashString(const std::string& s) {
    // Simple FNV-1a hash for seeding patterns
    uint32_t hash = 2166136261u;
    for (unsigned char c : s) {
        hash ^= c;
        hash *= 16777619u;
    }
    return hash ? hash : 1u;
}

struct ColorRGBA {
    float r, g, b, a;
};

ColorRGBA Lerp(const ColorRGBA& a, const ColorRGBA& b, float t) {
    float u = std::clamp(t, 0.0f, 1.0f);
    return {
        a.r + (b.r - a.r) * u,
        a.g + (b.g - a.g) * u,
        a.b + (b.b - a.b) * u,
        a.a + (b.a - a.a) * u
    };
}

uint8_t ToByte(float v) {
    return static_cast<uint8_t>(std::round(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

} // namespace

DiffusionEngine::~DiffusionEngine() {
#if CORTEX_ENABLE_TENSORRT
    if (m_vaeLatentDevice) {
        cudaFree(m_vaeLatentDevice);
        m_vaeLatentDevice = nullptr;
    }
    if (m_vaeImageDevice) {
        cudaFree(m_vaeImageDevice);
        m_vaeImageDevice = nullptr;
    }

    if (m_unetSampleDevice)       { cudaFree(m_unetSampleDevice);       m_unetSampleDevice = nullptr; }
    if (m_unetTimestepDevice)     { cudaFree(m_unetTimestepDevice);     m_unetTimestepDevice = nullptr; }
    if (m_unetEncoderHiddenDevice){ cudaFree(m_unetEncoderHiddenDevice);m_unetEncoderHiddenDevice = nullptr; }
    if (m_unetPooledEmbedsDevice) { cudaFree(m_unetPooledEmbedsDevice); m_unetPooledEmbedsDevice = nullptr; }
    if (m_unetTimeIdsDevice)      { cudaFree(m_unetTimeIdsDevice);      m_unetTimeIdsDevice = nullptr; }
    if (m_unetOutSampleDevice)    { cudaFree(m_unetOutSampleDevice);    m_unetOutSampleDevice = nullptr; }

    // TensorRT 8/9 exposed explicit destroy() methods; TensorRT 10.x uses
    // standard C++ delete semantics. Guard these calls based on the version
    // so the engine can build against both.
    if (m_vaeContext) {
// Prefer destroy() on older TensorRT versions; delete on 10.x and later.
#if defined(NV_TENSORRT_MAJOR) && (NV_TENSORRT_MAJOR < 10)
        m_vaeContext->destroy();
#else
        delete m_vaeContext;
#endif
        m_vaeContext = nullptr;
    }
    if (m_vaeEngine) {
// Prefer destroy() on older TensorRT versions; delete on 10.x and later.
#if defined(NV_TENSORRT_MAJOR) && (NV_TENSORRT_MAJOR < 10)
        m_vaeEngine->destroy();
#else
        delete m_vaeEngine;
#endif
        m_vaeEngine = nullptr;
    }

    if (m_unetContext) {
// Prefer destroy() on older TensorRT versions; delete on 10.x and later.
#if defined(NV_TENSORRT_MAJOR) && (NV_TENSORRT_MAJOR < 10)
        m_unetContext->destroy();
#else
        delete m_unetContext;
#endif
        m_unetContext = nullptr;
    }
    if (m_unetEngine) {
// Prefer destroy() on older TensorRT versions; delete on 10.x and later.
#if defined(NV_TENSORRT_MAJOR) && (NV_TENSORRT_MAJOR < 10)
        m_unetEngine->destroy();
#else
        delete m_unetEngine;
#endif
        m_unetEngine = nullptr;
    }

    if (m_trtRuntime) {
// Prefer destroy() on older TensorRT versions; delete on 10.x and later.
#if defined(NV_TENSORRT_MAJOR) && (NV_TENSORRT_MAJOR < 10)
        m_trtRuntime->destroy();
#else
        delete m_trtRuntime;
#endif
        m_trtRuntime = nullptr;
    }
#endif
}

#if CORTEX_ENABLE_TENSORRT

namespace {

#include <NvInferVersion.h>

class TRTLogger : public nvinfer1::ILogger {
public:
    void log(Severity severity, const char* msg) noexcept override {
        if (severity <= Severity::kWARNING) {
            spdlog::info("[TensorRT] {}", msg);
        }
    }
};

inline size_t GetElementCount(const nvinfer1::Dims& d) {
    size_t count = 1;
    for (int i = 0; i < d.nbDims; ++i) {
        count *= static_cast<size_t>(d.d[i]);
    }
    return count;
}

} // namespace

Cortex::Result<void> DiffusionEngine::InitializeGPU() {
    namespace fs = std::filesystem;

    fs::path base(m_enginePath);
    fs::path vaePath = base / "sdxl_turbo_vae_decoder_768x768.engine";

    if (!fs::exists(vaePath)) {
        return Cortex::Result<void>::Err("VAE engine not found at " + vaePath.string());
    }

    TRTLogger logger;

    // Load engine blob from disk
    std::ifstream f(vaePath, std::ios::binary | std::ios::ate);
    if (!f) {
        return Cortex::Result<void>::Err("Failed to open VAE engine file: " + vaePath.string());
    }
    std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    std::vector<char> buffer(static_cast<size_t>(size));
    if (!f.read(buffer.data(), size)) {
        return Cortex::Result<void>::Err("Failed to read VAE engine file: " + vaePath.string());
    }

    m_trtRuntime = nvinfer1::createInferRuntime(logger);
    if (!m_trtRuntime) {
        return Cortex::Result<void>::Err("Failed to create TensorRT runtime");
    }

    m_vaeEngine = m_trtRuntime->deserializeCudaEngine(buffer.data(), buffer.size());
    if (!m_vaeEngine) {
        return Cortex::Result<void>::Err("Failed to deserialize VAE engine");
    }

    m_vaeContext = m_vaeEngine->createExecutionContext();
    if (!m_vaeContext) {
        return Cortex::Result<void>::Err("Failed to create VAE execution context");
    }

    // TensorRT 10.x uses name-based tensor APIs for tensor shapes.
    nvinfer1::Dims latentDims = m_vaeEngine->getTensorShape("latent");
    nvinfer1::Dims imageDims  = m_vaeEngine->getTensorShape("image");

    size_t latentElems = GetElementCount(latentDims);
    size_t imageElems  = GetElementCount(imageDims);

    // Assume FP16 engines (from --fp16); bindings hold half-precision values.
    m_vaeLatentBytes = latentElems * sizeof(uint16_t);
    m_vaeImageBytes  = imageElems  * sizeof(uint16_t);

    // Image layout is [N, C, H, W] with N=1, C=3 for SDXL VAE.
    if (imageDims.nbDims == 4) {
        m_vaeHeight = static_cast<uint32_t>(imageDims.d[2]);
        m_vaeWidth  = static_cast<uint32_t>(imageDims.d[3]);
    }

    if (cudaMalloc(&m_vaeLatentDevice, m_vaeLatentBytes) != cudaSuccess) {
        return Cortex::Result<void>::Err("cudaMalloc failed for VAE latent buffer");
    }
    if (cudaMalloc(&m_vaeImageDevice, m_vaeImageBytes) != cudaSuccess) {
        return Cortex::Result<void>::Err("cudaMalloc failed for VAE image buffer");
    }

    spdlog::info("DiffusionEngine: VAE engine initialized ({} -> {} elements)",
                 latentElems, imageElems);

    // Optionally load a UNet engine if present. We try common SDXL-Turbo sizes
    // and treat UNet as an enhancement on top of the VAE-only path.
    static constexpr std::array<const char*, 3> kUnetNames = {
        "sdxl_turbo_unet_768x768.engine",
        "sdxl_turbo_unet_512x512.engine",
        "sdxl_turbo_unet_384x384.engine"
    };

    fs::path unetPath;
    for (const char* name : kUnetNames) {
        fs::path candidate = base / name;
        if (fs::exists(candidate)) {
            unetPath = candidate;
            break;
        }
    }

    if (!unetPath.empty()) {
        spdlog::info("DiffusionEngine: attempting to load UNet engine from '{}'", unetPath.string());

        std::ifstream uf(unetPath, std::ios::binary | std::ios::ate);
        if (!uf) {
            spdlog::warn("DiffusionEngine: failed to open UNet engine file: {}", unetPath.string());
        } else {
            std::streamsize usize = uf.tellg();
            uf.seekg(0, std::ios::beg);
            std::vector<char> ubuffer(static_cast<size_t>(usize));
            if (!uf.read(ubuffer.data(), usize)) {
                spdlog::warn("DiffusionEngine: failed to read UNet engine file: {}", unetPath.string());
            } else {
                m_unetEngine = m_trtRuntime->deserializeCudaEngine(ubuffer.data(), ubuffer.size());
                if (!m_unetEngine) {
                    spdlog::warn("DiffusionEngine: failed to deserialize UNet engine");
                } else {
                    m_unetContext = m_unetEngine->createExecutionContext();
                    if (!m_unetContext) {
                        spdlog::warn("DiffusionEngine: failed to create UNet execution context");
                        m_unetEngine = nullptr;
                    } else {
                        // TensorRT 10.x name-based tensor APIs.
                        nvinfer1::Dims sampleDims   = m_unetEngine->getTensorShape("sample");
                        nvinfer1::Dims timestepDims = m_unetEngine->getTensorShape("timestep");
                        nvinfer1::Dims encDims      = m_unetEngine->getTensorShape("encoder_hidden_states");
                        nvinfer1::Dims pooledDims   = m_unetEngine->getTensorShape("pooled_text_embeds");
                        nvinfer1::Dims timeIdsDims  = m_unetEngine->getTensorShape("time_ids");
                        nvinfer1::Dims outDims      = m_unetEngine->getTensorShape("out_sample");

                        m_unetSampleBytes        = GetElementCount(sampleDims)   * sizeof(uint16_t);
                        m_unetTimestepBytes      = GetElementCount(timestepDims) * sizeof(uint16_t);
                        m_unetEncoderHiddenBytes = GetElementCount(encDims)      * sizeof(uint16_t);
                        m_unetPooledEmbedsBytes  = GetElementCount(pooledDims)   * sizeof(uint16_t);
                        m_unetTimeIdsBytes       = GetElementCount(timeIdsDims)  * sizeof(uint16_t);
                        m_unetOutSampleBytes     = GetElementCount(outDims)      * sizeof(uint16_t);

                        if (cudaMalloc(&m_unetSampleDevice, m_unetSampleBytes) != cudaSuccess ||
                            cudaMalloc(&m_unetTimestepDevice, m_unetTimestepBytes) != cudaSuccess ||
                            cudaMalloc(&m_unetEncoderHiddenDevice, m_unetEncoderHiddenBytes) != cudaSuccess ||
                            cudaMalloc(&m_unetPooledEmbedsDevice, m_unetPooledEmbedsBytes) != cudaSuccess ||
                            cudaMalloc(&m_unetTimeIdsDevice, m_unetTimeIdsBytes) != cudaSuccess ||
                            cudaMalloc(&m_unetOutSampleDevice, m_unetOutSampleBytes) != cudaSuccess) {
                            spdlog::warn("DiffusionEngine: cudaMalloc failed for one or more UNet buffers");
                        } else {
                            spdlog::info("DiffusionEngine: UNet engine initialized (latent bytes = {}, out bytes = {})",
                                         m_unetSampleBytes, m_unetOutSampleBytes);
                        }
                    }
                }
            }
        }
    } else {
        spdlog::info("DiffusionEngine: no UNet engine found under '{}'; running VAE-only GPU path", base.string());
    }

    return Cortex::Result<void>::Ok();
}

Cortex::Result<TextureResult> DiffusionEngine::RunGPU(const TextureRequest& request) {
    if (!m_vaeEngine || !m_vaeContext || !m_vaeLatentDevice || !m_vaeImageDevice) {
        return Cortex::Result<TextureResult>::Err("VAE engine not initialized");
    }

    // Determine output resolution from the VAE tensor shape so we respect the
    // actual engine configuration (e.g. 768x768 or 384x384).
    nvinfer1::Dims imageDims = m_vaeEngine->getTensorShape("image");
    uint32_t width  = (imageDims.nbDims >= 4) ? static_cast<uint32_t>(imageDims.d[3]) : m_vaeWidth;
    uint32_t height = (imageDims.nbDims >= 4) ? static_cast<uint32_t>(imageDims.d[2]) : m_vaeHeight;
    if (width == 0 || height == 0) {
        width  = m_vaeWidth  ? m_vaeWidth  : 512;
        height = m_vaeHeight ? m_vaeHeight : 512;
    }

    // Build a simple latent volume. We deliberately fill it with high-variance
    // pseudo-random values derived from the request seed/prompt so that the
    // resulting textures are visually obvious (not subtle).
    uint32_t seed = request.seed;
    if (seed == 0) {
        seed = HashString(request.prompt.empty() ? "default_gpu" : request.prompt);
    }
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint16_t> dist(0, 0xFFFFu);

    cudaStream_t stream = nullptr;
    cudaStreamCreate(&stream);

    // If we have a UNet engine, run a single denoising step there and feed the
    // result into the VAE. Otherwise we fall back to a pure random latent.
    if (m_unetEngine && m_unetContext &&
        m_unetSampleDevice && m_unetOutSampleDevice &&
        m_unetTimestepDevice && m_unetEncoderHiddenDevice &&
        m_unetPooledEmbedsDevice && m_unetTimeIdsDevice) {
        std::vector<uint16_t> sampleHost(m_unetSampleBytes / sizeof(uint16_t));
        for (auto& v : sampleHost) {
            v = dist(rng);
        }
        if (cudaMemcpy(m_unetSampleDevice, sampleHost.data(), m_unetSampleBytes, cudaMemcpyHostToDevice) != cudaSuccess) {
            cudaStreamDestroy(stream);
            return Cortex::Result<TextureResult>::Err("cudaMemcpy to UNet sample buffer failed");
        }

        // Simple deterministic conditioning derived from the prompt hash.
        auto fillCond = [&](void* devicePtr, size_t bytes, uint16_t base) {
            std::vector<uint16_t> host(bytes / sizeof(uint16_t));
            for (size_t i = 0; i < host.size(); ++i) {
                host[i] = static_cast<uint16_t>(base + static_cast<uint16_t>(i % 97u));
            }
            cudaMemcpy(devicePtr, host.data(), bytes, cudaMemcpyHostToDevice);
        };

        uint16_t baseVal = static_cast<uint16_t>(seed & 0xFFFFu);
        fillCond(m_unetTimestepDevice,      m_unetTimestepBytes,      static_cast<uint16_t>(baseVal + 13u));
        fillCond(m_unetEncoderHiddenDevice, m_unetEncoderHiddenBytes, static_cast<uint16_t>(baseVal + 31u));
        fillCond(m_unetPooledEmbedsDevice,  m_unetPooledEmbedsBytes,  static_cast<uint16_t>(baseVal + 57u));
        fillCond(m_unetTimeIdsDevice,       m_unetTimeIdsBytes,       static_cast<uint16_t>(baseVal + 89u));

        if (!m_unetContext->setInputTensorAddress("sample", m_unetSampleDevice) ||
            !m_unetContext->setInputTensorAddress("timestep", m_unetTimestepDevice) ||
            !m_unetContext->setInputTensorAddress("encoder_hidden_states", m_unetEncoderHiddenDevice) ||
            !m_unetContext->setInputTensorAddress("pooled_text_embeds", m_unetPooledEmbedsDevice) ||
            !m_unetContext->setInputTensorAddress("time_ids", m_unetTimeIdsDevice) ||
            !m_unetContext->setTensorAddress("out_sample", m_unetOutSampleDevice)) {
            cudaStreamDestroy(stream);
            return Cortex::Result<TextureResult>::Err("Failed to set UNet tensor addresses");
        }

        if (!m_unetContext->enqueueV3(stream)) {
            cudaStreamDestroy(stream);
            return Cortex::Result<TextureResult>::Err("UNet enqueueV3 failed");
        }

        // Use the UNet output as the latent that feeds into the VAE.
        if (cudaMemcpyAsync(m_vaeLatentDevice,
                            m_unetOutSampleDevice,
                            std::min(m_vaeLatentBytes, m_unetOutSampleBytes),
                            cudaMemcpyDeviceToDevice,
                            stream) != cudaSuccess) {
            cudaStreamDestroy(stream);
            return Cortex::Result<TextureResult>::Err("cudaMemcpyAsync UNet->VAE latent failed");
        }
    } else {
        // No UNet; feed pure random noise into the VAE as before.
        std::vector<uint16_t> latentHost(m_vaeLatentBytes / sizeof(uint16_t));
        for (auto& h : latentHost) {
            h = dist(rng);
        }
        if (cudaMemcpy(m_vaeLatentDevice, latentHost.data(), m_vaeLatentBytes, cudaMemcpyHostToDevice) != cudaSuccess) {
            cudaStreamDestroy(stream);
            return Cortex::Result<TextureResult>::Err("cudaMemcpy to VAE latent buffer failed");
        }
    }

    if (!m_vaeContext->setInputTensorAddress("latent", m_vaeLatentDevice)) {
        cudaStreamDestroy(stream);
        return Cortex::Result<TextureResult>::Err("Failed to set VAE latent tensor address");
    }
    if (!m_vaeContext->setTensorAddress("image", m_vaeImageDevice)) {
        cudaStreamDestroy(stream);
        return Cortex::Result<TextureResult>::Err("Failed to set VAE image tensor address");
    }

    if (!m_vaeContext->enqueueV3(stream)) {
        cudaStreamDestroy(stream);
        return Cortex::Result<TextureResult>::Err("VAE enqueueV3 failed");
    }

    cudaStreamSynchronize(stream);
    cudaStreamDestroy(stream);

    std::vector<uint16_t> imageHost(m_vaeImageBytes / sizeof(uint16_t));
    if (cudaMemcpy(imageHost.data(), m_vaeImageDevice, m_vaeImageBytes, cudaMemcpyDeviceToHost) != cudaSuccess) {
        return Cortex::Result<TextureResult>::Err("cudaMemcpy from VAE image buffer failed");
    }

    // Convert FP16 CHW image to RGBA8
    TextureResult result{};
    result.targetName = request.targetName;
    result.prompt = request.prompt;
    result.usage = request.usage;
    result.materialPreset = request.materialPreset;
    result.seed = request.seed;
    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<size_t>(width) * height * 4);

    // imageDims should be [1, C, H, W] with C=3
    // We assume channels-first and contiguous memory.
    size_t H = height;
    size_t W = width;
    size_t C = 3;
    size_t planeSize = H * W;

    auto halfToFloat = [](uint16_t h) -> float {
        // Minimal IEEE 754 half -> float conversion
        uint16_t h_exp = (h & 0x7C00u) >> 10;
        uint16_t h_sig = (h & 0x03FFu);
        uint16_t h_sgn = (h & 0x8000u);
        uint32_t f_sgn = static_cast<uint32_t>(h_sgn) << 16;
        uint32_t f_exp;
        uint32_t f_sig;
        if (h_exp == 0) {
            if (h_sig == 0) {
                f_exp = 0;
                f_sig = 0;
            } else {
                // Subnormal half; normalize
                int shift = 0;
                while ((h_sig & 0x0400u) == 0) {
                    h_sig <<= 1;
                    ++shift;
                }
                h_sig &= 0x03FFu;
                f_exp = static_cast<uint32_t>(127 - 15 - shift) << 23;
                f_sig = static_cast<uint32_t>(h_sig) << 13;
            }
        } else if (h_exp == 0x1F) {
            // NaN/Inf
            f_exp = 0xFFu << 23;
            f_sig = static_cast<uint32_t>(h_sig) << 13;
        } else {
            f_exp = static_cast<uint32_t>(h_exp + (127 - 15)) << 23;
            f_sig = static_cast<uint32_t>(h_sig) << 13;
        }
        uint32_t f = f_sgn | f_exp | f_sig;
        float out;
        std::memcpy(&out, &f, sizeof(float));
        return out;
    };

    // First decode the raw VAE output into an intermediate RGBA buffer.
    std::vector<uint8_t> vaePixels(static_cast<size_t>(width) * height * 4);
    double sum = 0.0;
    double sumSq = 0.0;
    size_t sampleCount = 0;

    for (size_t c = 0; c < C; ++c) {
        for (size_t y = 0; y < H; ++y) {
            for (size_t x = 0; x < W; ++x) {
                size_t idxCHW = c * planeSize + y * W + x;
                float v = halfToFloat(imageHost[idxCHW]);
                // SDXL VAE outputs are roughly in [-1,1]; remap to [0,1] and
                // push contrast a bit so the result is more obvious.
                float v01 = std::clamp(v * 0.5f + 0.5f, 0.0f, 1.0f);
                v01 = std::pow(v01, 0.8f);

                sum += static_cast<double>(v01);
                sumSq += static_cast<double>(v01) * static_cast<double>(v01);
                ++sampleCount;

                size_t idxRGBA = (y * W + x) * 4;
                vaePixels[idxRGBA + c] = ToByte(v01);
                if (c == 0) {
                    vaePixels[idxRGBA + 3] = 255;
                }
            }
        }
    }

    // Also generate the procedural CPU pattern so prompts like "red wood"
    // remain visually obvious even when the VAE output is subtle. We then
    // blend the two results together.
    TextureResult cpuBase{};
    bool cpuOk = false;
    {
        TextureRequest cpuReq = request;
        cpuReq.width = static_cast<uint32_t>(width);
        cpuReq.height = static_cast<uint32_t>(height);
        auto cpuResult = RunCPU(cpuReq);
        if (cpuResult.IsOk()) {
            cpuBase = cpuResult.Value();
            cpuOk = cpuBase.width == width && cpuBase.height == height &&
                    cpuBase.pixels.size() == vaePixels.size();
        } else {
            spdlog::warn("DiffusionEngine: CPU stub failed during GPU blend path: {}",
                         cpuResult.Error());
        }
    }

    if (cpuOk) {
        // Determine whether the VAE output is essentially flat. If variance is
        // extremely small, the image will look like a uniform grey wash and we
        // prefer to show only the CPU pattern for clarity.
        bool vaeFlat = false;
        if (sampleCount > 0) {
            double mean = sum / static_cast<double>(sampleCount);
            double var = (sumSq / static_cast<double>(sampleCount)) - mean * mean;
            // Heuristic thresholds in [0,1] range.
            vaeFlat = var < 1e-4;
        }

        // Blend CPU pattern (strong prompt semantics) with VAE image (neural detail).
        float vaeWeight = vaeFlat ? 0.0f : 0.3f;
        float cpuWeight = 1.0f - vaeWeight;

        for (size_t i = 0; i < vaePixels.size(); i += 4) {
            for (int c = 0; c < 3; ++c) {
                float a = static_cast<float>(cpuBase.pixels[i + c]);
                float b = static_cast<float>(vaePixels[i + c]);
                float mixed = cpuWeight * a + vaeWeight * b;
                result.pixels[i + c] = static_cast<uint8_t>(std::clamp(mixed, 0.0f, 255.0f));
        }
        result.pixels[i + 3] = 255;
    }
        result.success = true;
        if (vaeFlat) {
            result.message = "GPU VAE output flat; using CPU stub (" +
                             std::to_string(width) + "x" + std::to_string(height) + ")";
            spdlog::info("DiffusionEngine: VAE output flat; CPU-only texture for '{}' ({}x{})",
                         result.targetName, width, height);
        } else {
            result.message = "GPU UNet/VAE blended with CPU stub (" +
                             std::to_string(width) + "x" + std::to_string(height) + ")";
            spdlog::info("DiffusionEngine: GPU UNet/VAE blended texture for '{}' ({}x{})",
                         result.targetName, width, height);
        }
    } else {
        // Fallback: pure VAE image if CPU path failed for any reason.
        result.pixels = std::move(vaePixels);
        result.success = true;
        result.message = "GPU VAE-generated " + std::to_string(width) + "x" + std::to_string(height) + " texture";
        spdlog::info("DiffusionEngine: GPU VAE generated texture for '{}' ({}x{})",
                     result.targetName, width, height);
    }

    return Cortex::Result<TextureResult>::Ok(std::move(result));
}

#endif // CORTEX_ENABLE_TENSORRT

Cortex::Result<void> DiffusionEngine::Initialize(const DreamerConfig& config) {
#if CORTEX_ENABLE_TENSORRT
    m_gpuRequested = config.useGPU;
    m_enginePath = config.enginePath;

    if (!m_gpuRequested || m_enginePath.empty()) {
        spdlog::info("DiffusionEngine: GPU diffusion disabled or no engine path; using CPU stub");
        m_gpuReady = false;
        return Cortex::Result<void>::Ok();
    }

    auto gpuInit = InitializeGPU();
    if (gpuInit.IsErr()) {
        spdlog::warn("DiffusionEngine: GPU initialization failed ({}); falling back to CPU stub",
                     gpuInit.Error());
        m_gpuReady = false;
    } else {
        m_gpuReady = true;
    }
    return Cortex::Result<void>::Ok();
#else
    (void)config;
    spdlog::info("DiffusionEngine: built without TensorRT; using CPU stub only");
    return Cortex::Result<void>::Ok();
#endif
}

Cortex::Result<TextureResult> DiffusionEngine::Run(const TextureRequest& request) {
#if CORTEX_ENABLE_TENSORRT
    if (m_gpuRequested && m_gpuReady) {
        auto gpuResult = RunGPU(request);
        if (gpuResult.IsOk()) {
            return gpuResult;
        }
        spdlog::warn("DiffusionEngine: GPU path failed ({}), using CPU stub",
                     gpuResult.Error());
    }
#endif
    // Fallback: procedural CPU generator.
    return RunCPU(request);
}

Cortex::Result<TextureResult> DiffusionEngine::RunCPU(const TextureRequest& request) {
    using clock = std::chrono::high_resolution_clock;
    const auto tStart = clock::now();

    TextureResult result{};
    result.targetName = request.targetName;
    result.prompt = request.prompt;
    result.usage = request.usage;
    result.materialPreset = request.materialPreset;
    result.seed = request.seed;

    uint32_t width = request.width;
    uint32_t height = request.height;

    // DreamerConfig clamping (defaults and max sizes) are applied in
    // DreamerService::SubmitRequest, so at this point width/height should
    // already be sane. We still defensively guard against zeros here.
    if (width == 0 || height == 0) {
        return Cortex::Result<TextureResult>::Err("Invalid texture dimensions (zero width/height)");
    }

    result.width = width;
    result.height = height;
    result.pixels.resize(static_cast<size_t>(width) * height * 4);

    uint32_t seed = request.seed;
    if (seed == 0) {
        seed = HashString(request.prompt.empty() ? "default" : request.prompt);
    }
    int pattern = static_cast<int>(seed % 3u);

    // Derive base colors from seed, but bias by simple keyword cues so
    // prompts like "make everything red" or "silver chrome sphere" have
    // obvious visual impact even with this procedural CPU stub.
    float t = (seed & 0xFFu) / 255.0f;
    ColorRGBA c1{0.2f + 0.6f * t, 0.3f, 0.8f * (1.0f - t), 1.0f};
    ColorRGBA c2{0.1f, 0.6f * (1.0f - t), 0.9f * t, 1.0f};
    ColorRGBA c3{0.9f * (1.0f - t), 0.8f * t, 0.2f + 0.5f * (1.0f - t), 1.0f};

    // Very lightweight prompt semantics: map a few common color/material
    // words into strong base colors and patterns so you can *see* that
    // the Dreamer responded to the text.
    std::string promptLower = request.prompt;
    std::transform(promptLower.begin(), promptLower.end(), promptLower.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

    auto contains = [&](const char* word) -> bool {
        return promptLower.find(word) != std::string::npos;
    };

    if (contains("red")) {
        c1 = {1.0f, 0.1f, 0.1f, 1.0f};
        c2 = {0.6f, 0.0f, 0.0f, 1.0f};
        c3 = {1.0f, 0.5f, 0.3f, 1.0f};
    } else if (contains("blue")) {
        c1 = {0.1f, 0.3f, 1.0f, 1.0f};
        c2 = {0.0f, 0.0f, 0.6f, 1.0f};
        c3 = {0.4f, 0.8f, 1.0f, 1.0f};
    } else if (contains("green")) {
        c1 = {0.1f, 0.8f, 0.2f, 1.0f};
        c2 = {0.0f, 0.4f, 0.0f, 1.0f};
        c3 = {0.6f, 1.0f, 0.6f, 1.0f};
    } else if (contains("yellow")) {
        c1 = {1.0f, 0.9f, 0.3f, 1.0f};
        c2 = {0.9f, 0.7f, 0.1f, 1.0f};
        c3 = {1.0f, 1.0f, 0.6f, 1.0f};
    } else if (contains("purple")) {
        c1 = {0.7f, 0.3f, 0.9f, 1.0f};
        c2 = {0.3f, 0.0f, 0.5f, 1.0f};
        c3 = {0.9f, 0.6f, 1.0f, 1.0f};
    } else if (contains("orange")) {
        c1 = {1.0f, 0.5f, 0.1f, 1.0f};
        c2 = {0.8f, 0.3f, 0.0f, 1.0f};
        c3 = {1.0f, 0.8f, 0.4f, 1.0f};
    } else if (contains("silver") || contains("chrome") ||
               (!request.materialPreset.empty() &&
                (request.materialPreset.find("chrome") != std::string::npos ||
                 request.materialPreset.find("silver") != std::string::npos))) {
        c1 = {0.8f, 0.8f, 0.8f, 1.0f};
        c2 = {0.4f, 0.4f, 0.4f, 1.0f};
        c3 = {1.0f, 1.0f, 1.0f, 1.0f};
    }

    // Simple pattern hints
    if (contains("checker") || contains("checkboard") || contains("grid")) {
        pattern = 2;
    } else if (contains("marble") || contains("swirl")) {
        pattern = 1;
    } else if (contains("wood") || contains("plank")) {
        pattern = 0;
    }

    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            float u = static_cast<float>(x) / static_cast<float>(width - 1);
            float v = static_cast<float>(y) / static_cast<float>(height - 1);

            ColorRGBA col{};

            if (pattern == 0) {
                // Soft horizontal wood-like bands
                float bands = std::sin((v + t) * 24.0f) * 0.5f + 0.5f;
                float grain = std::sin((u * 8.0f + v * 4.0f + t * 10.0f)) * 0.5f + 0.5f;
                float mix = 0.7f * bands + 0.3f * grain;
                col = Lerp(c1, c2, mix);
            } else if (pattern == 1) {
                // Radial marble-ish swirl
                float cx = u - 0.5f;
                float cy = v - 0.5f;
                float r = std::sqrt(cx * cx + cy * cy);
                float angle = std::atan2(cy, cx);
                float swirl = std::sin(20.0f * r + angle * 4.0f + t * 6.0f) * 0.5f + 0.5f;
                col = Lerp(c2, c3, swirl);
            } else {
                // Checkerboard with smooth blend
                int checkX = static_cast<int>(std::floor(u * 8.0f));
                int checkY = static_cast<int>(std::floor(v * 8.0f));
                bool odd = ((checkX + checkY) & 1) != 0;
                float edge = std::sin((u + v + t) * 12.0f) * 0.5f + 0.5f;
                ColorRGBA base = odd ? c1 : c2;
                col = Lerp(base, c3, edge * 0.3f);
            }

            size_t idx = (static_cast<size_t>(y) * width + x) * 4;
            result.pixels[idx + 0] = ToByte(col.r);
            result.pixels[idx + 1] = ToByte(col.g);
            result.pixels[idx + 2] = ToByte(col.b);
            result.pixels[idx + 3] = ToByte(col.a);
        }
    }

    const auto tEnd = clock::now();
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(tEnd - tStart).count();

    result.success = true;
    result.message = "Generated " + std::to_string(width) + "x" + std::to_string(height) +
                     " texture in " + std::to_string(ms) + " ms";

    spdlog::info("DiffusionEngine (CPU stub) generated texture for '{}' ({}x{}, pattern={}, {} ms)",
                 result.targetName, width, height, pattern, ms);

    return Cortex::Result<TextureResult>::Ok(std::move(result));
}

} // namespace Cortex::AI::Vision
