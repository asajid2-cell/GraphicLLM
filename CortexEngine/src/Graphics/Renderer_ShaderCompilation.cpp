#include "Renderer.h"

#include <utility>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

Result<RendererCompiledShaders> Renderer::CompileRendererPipelineShaders() {
    RendererCompiledShaders shaders{};

    auto vsResult = ShaderCompiler::CompileFromFile("assets/shaders/Basic.hlsl", "VSMain", "vs_5_1");
    if (vsResult.IsErr()) {
        return Result<RendererCompiledShaders>::Err("Failed to compile vertex shader: " + vsResult.Error());
    }
    shaders.basicVS = std::move(vsResult).Value();

    auto psResult = ShaderCompiler::CompileFromFile("assets/shaders/Basic.hlsl", "PSMain", "ps_5_1");
    if (psResult.IsErr()) {
        return Result<RendererCompiledShaders>::Err("Failed to compile pixel shader: " + psResult.Error());
    }
    shaders.basicPS = std::move(psResult).Value();

    auto psTransparentResult =
        ShaderCompiler::CompileFromFile("assets/shaders/Basic.hlsl", "PSMainTransparent", "ps_5_1");
    if (psTransparentResult.IsOk()) {
        shaders.transparentPS = std::move(psTransparentResult).Value();
    } else {
        spdlog::warn("Failed to compile transparent pixel shader: {}", psTransparentResult.Error());
    }

    auto skyboxVsResult = ShaderCompiler::CompileFromFile("assets/shaders/Basic.hlsl", "SkyboxVS", "vs_5_1");
    if (skyboxVsResult.IsOk()) {
        shaders.skyboxVS = std::move(skyboxVsResult).Value();
    }

    auto skyboxPsResult = ShaderCompiler::CompileFromFile("assets/shaders/Basic.hlsl", "SkyboxPS", "ps_5_1");
    if (skyboxPsResult.IsOk()) {
        shaders.skyboxPS = std::move(skyboxPsResult).Value();
    }

    auto shadowVsResult = ShaderCompiler::CompileFromFile("assets/shaders/Basic.hlsl", "VSShadow", "vs_5_1");
    if (shadowVsResult.IsErr()) {
        return Result<RendererCompiledShaders>::Err(
            "Failed to compile shadow vertex shader: " + shadowVsResult.Error());
    }
    shaders.shadowVS = std::move(shadowVsResult).Value();

    auto shadowPsAlphaResult =
        ShaderCompiler::CompileFromFile("assets/shaders/Basic.hlsl", "PSShadowAlphaTest", "ps_5_1");
    if (shadowPsAlphaResult.IsOk()) {
        shaders.shadowAlphaPS = std::move(shadowPsAlphaResult).Value();
    } else {
        spdlog::warn("Failed to compile alpha-tested shadow pixel shader: {}", shadowPsAlphaResult.Error());
    }

    auto depthPsAlphaResult =
        ShaderCompiler::CompileFromFile("assets/shaders/Basic.hlsl", "PSDepthAlphaTest", "ps_5_1");
    if (depthPsAlphaResult.IsOk()) {
        shaders.depthAlphaPS = std::move(depthPsAlphaResult).Value();
    } else {
        spdlog::warn("Failed to compile alpha-tested depth pixel shader: {}", depthPsAlphaResult.Error());
    }

    auto postVsResult = ShaderCompiler::CompileFromFile("assets/shaders/PostProcess.hlsl", "VSMain", "vs_5_1");
    if (postVsResult.IsErr()) {
        return Result<RendererCompiledShaders>::Err(
            "Failed to compile post-process vertex shader: " + postVsResult.Error());
    }
    shaders.postVS = std::move(postVsResult).Value();

    auto postPsResult = ShaderCompiler::CompileFromFile("assets/shaders/PostProcess.hlsl", "PSMain", "ps_5_1");
    if (postPsResult.IsErr()) {
        return Result<RendererCompiledShaders>::Err(
            "Failed to compile post-process pixel shader: " + postPsResult.Error());
    }
    shaders.postPS = std::move(postPsResult).Value();

    auto voxelPsResult =
        ShaderCompiler::CompileFromFile("assets/shaders/VoxelRaymarch.hlsl", "PSMain", "ps_5_1");
    if (voxelPsResult.IsOk()) {
        shaders.voxelPS = std::move(voxelPsResult).Value();
    } else {
        spdlog::warn("Failed to compile voxel raymarch pixel shader: {}", voxelPsResult.Error());
    }

    auto taaPsResult =
        ShaderCompiler::CompileFromFile("assets/shaders/PostProcess.hlsl", "TAAResolvePS", "ps_5_1");
    if (taaPsResult.IsOk()) {
        shaders.taaPS = std::move(taaPsResult).Value();
    } else {
        spdlog::warn("Failed to compile TAA HDR pixel shader: {}", taaPsResult.Error());
    }

    auto ssaoVsResult = ShaderCompiler::CompileFromFile("assets/shaders/SSAO.hlsl", "VSMain", "vs_5_1");
    if (ssaoVsResult.IsOk()) {
        shaders.ssaoVS = std::move(ssaoVsResult).Value();
    } else {
        spdlog::warn("Failed to compile SSAO vertex shader: {}", ssaoVsResult.Error());
    }

    auto ssaoPsResult = ShaderCompiler::CompileFromFile("assets/shaders/SSAO.hlsl", "PSMain", "ps_5_1");
    if (ssaoPsResult.IsOk()) {
        shaders.ssaoPS = std::move(ssaoPsResult).Value();
    } else {
        spdlog::warn("Failed to compile SSAO pixel shader: {}", ssaoPsResult.Error());
    }

    auto ssrVsResult = ShaderCompiler::CompileFromFile("assets/shaders/SSR.hlsl", "VSMain", "vs_5_1");
    if (ssrVsResult.IsOk()) {
        shaders.ssrVS = std::move(ssrVsResult).Value();
    } else {
        spdlog::warn("Failed to compile SSR vertex shader: {}", ssrVsResult.Error());
    }

    auto ssrPsResult = ShaderCompiler::CompileFromFile("assets/shaders/SSR.hlsl", "SSRPS", "ps_5_1");
    if (ssrPsResult.IsOk()) {
        shaders.ssrPS = std::move(ssrPsResult).Value();
    } else {
        spdlog::warn("Failed to compile SSR pixel shader: {}", ssrPsResult.Error());
    }

    auto motionVsResult =
        ShaderCompiler::CompileFromFile("assets/shaders/MotionVectors.hlsl", "VSMain", "vs_5_1");
    if (motionVsResult.IsOk()) {
        shaders.motionVS = std::move(motionVsResult).Value();
    } else {
        spdlog::warn("Failed to compile motion vector vertex shader: {}", motionVsResult.Error());
    }

    auto motionPsResult =
        ShaderCompiler::CompileFromFile("assets/shaders/MotionVectors.hlsl", "PSMain", "ps_5_1");
    if (motionPsResult.IsOk()) {
        shaders.motionPS = std::move(motionPsResult).Value();
    } else {
        spdlog::warn("Failed to compile motion vector pixel shader: {}", motionPsResult.Error());
    }

    auto waterVsResult = ShaderCompiler::CompileFromFile("assets/shaders/Water.hlsl", "WaterVS", "vs_5_1");
    if (waterVsResult.IsOk()) {
        shaders.waterVS = std::move(waterVsResult).Value();
    } else {
        spdlog::warn("Failed to compile water vertex shader: {}", waterVsResult.Error());
    }

    auto waterPsResult = ShaderCompiler::CompileFromFile("assets/shaders/Water.hlsl", "WaterPS", "ps_5_1");
    if (waterPsResult.IsOk()) {
        shaders.waterPS = std::move(waterPsResult).Value();
    } else {
        spdlog::warn("Failed to compile water pixel shader: {}", waterPsResult.Error());
    }

    return Result<RendererCompiledShaders>::Ok(std::move(shaders));
}

} // namespace Cortex::Graphics
