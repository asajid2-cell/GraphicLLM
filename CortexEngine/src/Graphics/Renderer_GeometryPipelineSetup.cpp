#include "Renderer.h"

#include <memory>
#include <vector>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

namespace {

std::vector<D3D12_INPUT_ELEMENT_DESC> CreateStandardMeshInputLayout() {
    return {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TANGENT", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 24, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 40, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 48, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
    };
}

} // namespace

Result<void> Renderer::CreateGeometryPipelineStates(const RendererCompiledShaders& shaders) {
    m_pipelineState.geometry = std::make_unique<DX12Pipeline>();

    PipelineDesc pipelineDesc = {};
    pipelineDesc.vertexShader = shaders.basicVS;
    pipelineDesc.pixelShader = shaders.basicPS;
    pipelineDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
    pipelineDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    pipelineDesc.numRenderTargets = 2;
    pipelineDesc.inputLayout = CreateStandardMeshInputLayout();

    auto pipelineResult = m_pipelineState.geometry->Initialize(
        m_services.device->GetDevice(),
        m_pipelineState.rootSignature->GetRootSignature(),
        pipelineDesc);
    if (pipelineResult.IsErr()) {
        return Result<void>::Err("Failed to create pipeline: " + pipelineResult.Error());
    }

    if (shaders.transparentPS) {
        m_pipelineState.transparent = std::make_unique<DX12Pipeline>();
        PipelineDesc transparentDesc = pipelineDesc;
        transparentDesc.pixelShader = *shaders.transparentPS;
        transparentDesc.numRenderTargets = 1;
        transparentDesc.blendEnabled = true;
        transparentDesc.depthWriteEnabled = false;
        transparentDesc.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

        auto transparentResult = m_pipelineState.transparent->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            transparentDesc);
        if (transparentResult.IsErr()) {
            spdlog::warn("Failed to create transparent pipeline: {}", transparentResult.Error());
            m_pipelineState.transparent.reset();
        }
    } else {
        m_pipelineState.transparent.reset();
    }

    if (shaders.transparentPS) {
        m_pipelineState.overlay = std::make_unique<DX12Pipeline>();
        PipelineDesc overlayDesc = pipelineDesc;
        overlayDesc.pixelShader = *shaders.transparentPS;
        overlayDesc.numRenderTargets = 1;
        overlayDesc.blendEnabled = false;
        overlayDesc.depthWriteEnabled = false;
        overlayDesc.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        overlayDesc.depthBias = -2000;
        overlayDesc.slopeScaledDepthBias = -2.0f;

        auto overlayResult = m_pipelineState.overlay->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            overlayDesc);
        if (overlayResult.IsErr()) {
            spdlog::warn("Failed to create overlay pipeline: {}", overlayResult.Error());
            m_pipelineState.overlay.reset();
        }
    } else {
        m_pipelineState.overlay.reset();
    }

    if (!shaders.waterVS || !shaders.waterPS) {
        m_pipelineState.water.reset();
        m_pipelineState.waterOverlay.reset();
    }

    m_pipelineState.depthOnly = std::make_unique<DX12Pipeline>();
    PipelineDesc depthDesc = {};
    depthDesc.vertexShader = shaders.basicVS;
    depthDesc.pixelShader = {};
    depthDesc.inputLayout = pipelineDesc.inputLayout;
    depthDesc.rtvFormat = DXGI_FORMAT_UNKNOWN;
    depthDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    depthDesc.numRenderTargets = 0;
    depthDesc.depthTestEnabled = true;
    depthDesc.depthWriteEnabled = true;
    depthDesc.cullMode = D3D12_CULL_MODE_BACK;
    depthDesc.blendEnabled = false;

    auto depthPipelineResult = m_pipelineState.depthOnly->Initialize(
        m_services.device->GetDevice(),
        m_pipelineState.rootSignature->GetRootSignature(),
        depthDesc);
    if (depthPipelineResult.IsErr()) {
        spdlog::warn("Failed to create depth-only pipeline: {}", depthPipelineResult.Error());
        m_pipelineState.depthOnly.reset();
    }

    m_pipelineState.depthOnlyDoubleSided = std::make_unique<DX12Pipeline>();
    PipelineDesc depthDoubleSidedDesc = depthDesc;
    depthDoubleSidedDesc.cullMode = D3D12_CULL_MODE_NONE;
    auto depthDoubleSidedResult = m_pipelineState.depthOnlyDoubleSided->Initialize(
        m_services.device->GetDevice(),
        m_pipelineState.rootSignature->GetRootSignature(),
        depthDoubleSidedDesc);
    if (depthDoubleSidedResult.IsErr()) {
        spdlog::warn("Failed to create double-sided depth-only pipeline: {}", depthDoubleSidedResult.Error());
        m_pipelineState.depthOnlyDoubleSided.reset();
    }

    if (shaders.depthAlphaPS) {
        m_pipelineState.depthOnlyAlpha = std::make_unique<DX12Pipeline>();
        PipelineDesc depthAlphaDesc = depthDesc;
        depthAlphaDesc.pixelShader = *shaders.depthAlphaPS;
        auto depthAlphaResult = m_pipelineState.depthOnlyAlpha->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            depthAlphaDesc);
        if (depthAlphaResult.IsErr()) {
            spdlog::warn("Failed to create alpha-tested depth-only pipeline: {}", depthAlphaResult.Error());
            m_pipelineState.depthOnlyAlpha.reset();
        }

        m_pipelineState.depthOnlyAlphaDoubleSided = std::make_unique<DX12Pipeline>();
        PipelineDesc depthAlphaDoubleSidedDesc = depthAlphaDesc;
        depthAlphaDoubleSidedDesc.cullMode = D3D12_CULL_MODE_NONE;
        auto depthAlphaDoubleSidedResult = m_pipelineState.depthOnlyAlphaDoubleSided->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            depthAlphaDoubleSidedDesc);
        if (depthAlphaDoubleSidedResult.IsErr()) {
            spdlog::warn(
                "Failed to create alpha-tested double-sided depth-only pipeline: {}",
                depthAlphaDoubleSidedResult.Error());
            m_pipelineState.depthOnlyAlphaDoubleSided.reset();
        }
    }

    if (shaders.waterVS && shaders.waterPS) {
        m_pipelineState.water = std::make_unique<DX12Pipeline>();
        m_pipelineState.waterOverlay = std::make_unique<DX12Pipeline>();

        PipelineDesc waterDesc = {};
        waterDesc.vertexShader = *shaders.waterVS;
        waterDesc.pixelShader = *shaders.waterPS;
        waterDesc.inputLayout = pipelineDesc.inputLayout;
        waterDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        waterDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        waterDesc.numRenderTargets = 2;
        waterDesc.depthTestEnabled = true;
        waterDesc.depthWriteEnabled = true;
        waterDesc.cullMode = D3D12_CULL_MODE_BACK;
        waterDesc.blendEnabled = false;

        auto waterPipelineResult = m_pipelineState.water->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            waterDesc);
        if (waterPipelineResult.IsErr()) {
            spdlog::warn("Failed to create water pipeline: {}", waterPipelineResult.Error());
            m_pipelineState.water.reset();
        }

        PipelineDesc waterOverlayDesc = waterDesc;
        waterOverlayDesc.numRenderTargets = 1;
        waterOverlayDesc.depthWriteEnabled = false;
        waterOverlayDesc.depthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        waterOverlayDesc.blendEnabled = true;
        waterOverlayDesc.depthBias = -2000;
        waterOverlayDesc.slopeScaledDepthBias = -2.0f;

        auto waterOverlayResult = m_pipelineState.waterOverlay->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            waterOverlayDesc);
        if (waterOverlayResult.IsErr()) {
            spdlog::warn("Failed to create water overlay pipeline: {}", waterOverlayResult.Error());
            m_pipelineState.waterOverlay.reset();
        }
    }

    auto particleVsResult = ShaderCompiler::CompileFromFile("assets/shaders/Particles.hlsl", "VSMain", "vs_5_1");
    if (particleVsResult.IsErr()) {
        spdlog::warn("Failed to compile particle vertex shader: {}", particleVsResult.Error());
    }
    auto particlePsResult = ShaderCompiler::CompileFromFile("assets/shaders/Particles.hlsl", "PSMain", "ps_5_1");
    if (particlePsResult.IsErr()) {
        spdlog::warn("Failed to compile particle pixel shader: {}", particlePsResult.Error());
    }
    auto particlePrepareCsResult =
        ShaderCompiler::CompileFromFile("assets/shaders/ParticleInstancePrepare.hlsl", "CSMain", "cs_5_1");
    if (particlePrepareCsResult.IsErr()) {
        spdlog::warn("Failed to compile particle GPU prepare compute shader: {}", particlePrepareCsResult.Error());
    }
    auto particleLifecycleCsResult =
        ShaderCompiler::CompileFromFile("assets/shaders/ParticleEmitterLifecycle.hlsl", "CSMain", "cs_5_1");
    if (particleLifecycleCsResult.IsErr()) {
        spdlog::warn("Failed to compile particle GPU lifecycle compute shader: {}", particleLifecycleCsResult.Error());
    }

    if (particleVsResult.IsOk() && particlePsResult.IsOk()) {
        m_pipelineState.particle = std::make_unique<DX12Pipeline>();

        D3D12_INPUT_ELEMENT_DESC posElem{};
        posElem.SemanticName = "POSITION";
        posElem.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        posElem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

        D3D12_INPUT_ELEMENT_DESC uvElem{};
        uvElem.SemanticName = "TEXCOORD";
        uvElem.Format = DXGI_FORMAT_R32G32_FLOAT;
        uvElem.AlignedByteOffset = 12;
        uvElem.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;

        D3D12_INPUT_ELEMENT_DESC instPos{};
        instPos.SemanticName = "TEXCOORD";
        instPos.SemanticIndex = 1;
        instPos.Format = DXGI_FORMAT_R32G32B32_FLOAT;
        instPos.InputSlot = 1;
        instPos.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        instPos.InstanceDataStepRate = 1;

        D3D12_INPUT_ELEMENT_DESC instSize{};
        instSize.SemanticName = "TEXCOORD";
        instSize.SemanticIndex = 2;
        instSize.Format = DXGI_FORMAT_R32_FLOAT;
        instSize.InputSlot = 1;
        instSize.AlignedByteOffset = 12;
        instSize.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        instSize.InstanceDataStepRate = 1;

        D3D12_INPUT_ELEMENT_DESC instColor{};
        instColor.SemanticName = "COLOR";
        instColor.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
        instColor.InputSlot = 1;
        instColor.AlignedByteOffset = 16;
        instColor.InputSlotClass = D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA;
        instColor.InstanceDataStepRate = 1;

        PipelineDesc particleDesc = {};
        particleDesc.vertexShader = particleVsResult.Value();
        particleDesc.pixelShader = particlePsResult.Value();
        particleDesc.inputLayout = { posElem, uvElem, instPos, instSize, instColor };
        particleDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        particleDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        particleDesc.numRenderTargets = 1;
        particleDesc.depthTestEnabled = true;
        particleDesc.depthWriteEnabled = false;
        particleDesc.cullMode = D3D12_CULL_MODE_NONE;
        particleDesc.blendEnabled = true;

        auto particlePipelineResult = m_pipelineState.particle->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            particleDesc);
        if (particlePipelineResult.IsErr()) {
            spdlog::warn("Failed to create particle pipeline: {}", particlePipelineResult.Error());
            m_pipelineState.particle.reset();
        }
    }

    if (particlePrepareCsResult.IsOk() && m_pipelineState.singleSrvUavComputeRootSignature) {
        m_pipelineState.particlePrepareCompute = std::make_unique<DX12ComputePipeline>();
        auto preparePipelineResult = m_pipelineState.particlePrepareCompute->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.singleSrvUavComputeRootSignature.Get(),
            particlePrepareCsResult.Value());
        if (preparePipelineResult.IsErr()) {
            spdlog::warn("Failed to create particle GPU prepare compute pipeline: {}",
                         preparePipelineResult.Error());
            m_pipelineState.particlePrepareCompute.reset();
        }
    } else {
        m_pipelineState.particlePrepareCompute.reset();
    }

    if (particleLifecycleCsResult.IsOk() && m_pipelineState.singleSrvUavComputeRootSignature) {
        m_pipelineState.particleLifecycleCompute = std::make_unique<DX12ComputePipeline>();
        auto lifecyclePipelineResult = m_pipelineState.particleLifecycleCompute->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.singleSrvUavComputeRootSignature.Get(),
            particleLifecycleCsResult.Value());
        if (lifecyclePipelineResult.IsErr()) {
            spdlog::warn("Failed to create particle GPU lifecycle compute pipeline: {}",
                         lifecyclePipelineResult.Error());
            m_pipelineState.particleLifecycleCompute.reset();
        }
    } else {
        m_pipelineState.particleLifecycleCompute.reset();
    }

    if (shaders.skyboxVS && shaders.skyboxPS) {
        m_pipelineState.skybox = std::make_unique<DX12Pipeline>();

        PipelineDesc skyDesc = {};
        skyDesc.vertexShader = *shaders.skyboxVS;
        skyDesc.pixelShader = *shaders.skyboxPS;
        skyDesc.inputLayout = {};
        skyDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        skyDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        skyDesc.numRenderTargets = 1;
        skyDesc.depthTestEnabled = false;
        skyDesc.depthWriteEnabled = false;
        skyDesc.cullMode = D3D12_CULL_MODE_NONE;
        skyDesc.blendEnabled = false;

        auto skyboxPipelineResult = m_pipelineState.skybox->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            skyDesc);
        if (skyboxPipelineResult.IsErr()) {
            spdlog::warn("Failed to create skybox pipeline: {}", skyboxPipelineResult.Error());
            m_pipelineState.skybox.reset();
        }
    } else {
        spdlog::warn("Skybox shaders did not compile; environment will be lighting-only");
    }

    auto proceduralSkyVsResult =
        ShaderCompiler::CompileFromFile("assets/shaders/ProceduralSky.hlsl", "VSMain", "vs_5_1");
    auto proceduralSkyPsResult =
        ShaderCompiler::CompileFromFile("assets/shaders/ProceduralSky.hlsl", "PSMain", "ps_5_1");

    if (proceduralSkyVsResult.IsOk() && proceduralSkyPsResult.IsOk()) {
        m_pipelineState.proceduralSky = std::make_unique<DX12Pipeline>();

        PipelineDesc procSkyDesc = {};
        procSkyDesc.vertexShader = proceduralSkyVsResult.Value();
        procSkyDesc.pixelShader = proceduralSkyPsResult.Value();
        procSkyDesc.inputLayout = {};
        procSkyDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        procSkyDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
        procSkyDesc.numRenderTargets = 1;
        procSkyDesc.depthTestEnabled = false;
        procSkyDesc.depthWriteEnabled = false;
        procSkyDesc.cullMode = D3D12_CULL_MODE_NONE;
        procSkyDesc.blendEnabled = false;

        auto procSkyPipelineResult = m_pipelineState.proceduralSky->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            procSkyDesc);
        if (procSkyPipelineResult.IsErr()) {
            spdlog::warn("Failed to create procedural sky pipeline: {}", procSkyPipelineResult.Error());
            m_pipelineState.proceduralSky.reset();
        } else {
            spdlog::info("Procedural sky pipeline created successfully");
        }
    } else {
        spdlog::warn("Procedural sky shaders did not compile");
        if (proceduralSkyVsResult.IsErr()) {
            spdlog::warn("  VS: {}", proceduralSkyVsResult.Error());
        }
        if (proceduralSkyPsResult.IsErr()) {
            spdlog::warn("  PS: {}", proceduralSkyPsResult.Error());
        }
    }

    m_pipelineState.shadow = std::make_unique<DX12Pipeline>();

    PipelineDesc shadowDesc = {};
    shadowDesc.vertexShader = shaders.shadowVS;
    shadowDesc.inputLayout = pipelineDesc.inputLayout;
    shadowDesc.rtvFormat = DXGI_FORMAT_UNKNOWN;
    shadowDesc.dsvFormat = DXGI_FORMAT_D32_FLOAT;
    shadowDesc.numRenderTargets = 0;
    shadowDesc.depthTestEnabled = true;
    shadowDesc.depthWriteEnabled = true;
    shadowDesc.cullMode = D3D12_CULL_MODE_BACK;
    shadowDesc.wireframe = false;
    shadowDesc.blendEnabled = false;

    auto shadowPipelineResult = m_pipelineState.shadow->Initialize(
        m_services.device->GetDevice(),
        m_pipelineState.rootSignature->GetRootSignature(),
        shadowDesc);
    if (shadowPipelineResult.IsErr()) {
        return Result<void>::Err("Failed to create shadow pipeline: " + shadowPipelineResult.Error());
    }

    m_pipelineState.shadowDoubleSided = std::make_unique<DX12Pipeline>();
    PipelineDesc shadowDoubleDesc = shadowDesc;
    shadowDoubleDesc.cullMode = D3D12_CULL_MODE_NONE;
    auto dsResult = m_pipelineState.shadowDoubleSided->Initialize(
        m_services.device->GetDevice(),
        m_pipelineState.rootSignature->GetRootSignature(),
        shadowDoubleDesc);
    if (dsResult.IsErr()) {
        spdlog::warn("Failed to create double-sided shadow pipeline: {}", dsResult.Error());
        m_pipelineState.shadowDoubleSided.reset();
    }

    if (shaders.shadowAlphaPS) {
        m_pipelineState.shadowAlpha = std::make_unique<DX12Pipeline>();
        PipelineDesc shadowAlphaDesc = shadowDesc;
        shadowAlphaDesc.pixelShader = *shaders.shadowAlphaPS;
        auto alphaResult = m_pipelineState.shadowAlpha->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            shadowAlphaDesc);
        if (alphaResult.IsErr()) {
            spdlog::warn("Failed to create alpha-tested shadow pipeline: {}", alphaResult.Error());
            m_pipelineState.shadowAlpha.reset();
        }

        m_pipelineState.shadowAlphaDoubleSided = std::make_unique<DX12Pipeline>();
        PipelineDesc shadowAlphaDsDesc = shadowAlphaDesc;
        shadowAlphaDsDesc.cullMode = D3D12_CULL_MODE_NONE;
        auto alphaDsResult = m_pipelineState.shadowAlphaDoubleSided->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            shadowAlphaDsDesc);
        if (alphaDsResult.IsErr()) {
            spdlog::warn("Failed to create alpha-tested double-sided shadow pipeline: {}", alphaDsResult.Error());
            m_pipelineState.shadowAlphaDoubleSided.reset();
        }
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
