#include "Renderer.h"

#include <memory>

#include <spdlog/spdlog.h>

namespace Cortex::Graphics {

Result<void> Renderer::CreateScreenSpacePipelineStates(const RendererCompiledShaders& shaders) {
    m_pipelineState.postProcess = std::make_unique<DX12Pipeline>();

    PipelineDesc postDesc = {};
    postDesc.vertexShader = shaders.postVS;
    postDesc.pixelShader = shaders.postPS;
    postDesc.inputLayout = {};
    postDesc.rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
    postDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
    postDesc.numRenderTargets = 1;
    postDesc.depthTestEnabled = false;
    postDesc.depthWriteEnabled = false;
    postDesc.cullMode = D3D12_CULL_MODE_NONE;
    postDesc.blendEnabled = false;

    auto postPipelineResult = m_pipelineState.postProcess->Initialize(
        m_services.device->GetDevice(),
        m_pipelineState.rootSignature->GetRootSignature(),
        postDesc);
    if (postPipelineResult.IsErr()) {
        return Result<void>::Err("Failed to create post-process pipeline: " + postPipelineResult.Error());
    }

    if (shaders.voxelPS) {
        m_pipelineState.voxel = std::make_unique<DX12Pipeline>();

        PipelineDesc voxelDesc = {};
        voxelDesc.vertexShader = shaders.postVS;
        voxelDesc.pixelShader = *shaders.voxelPS;
        voxelDesc.inputLayout = {};
        voxelDesc.rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        voxelDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
        voxelDesc.numRenderTargets = 1;
        voxelDesc.depthTestEnabled = false;
        voxelDesc.depthWriteEnabled = false;
        voxelDesc.cullMode = D3D12_CULL_MODE_NONE;
        voxelDesc.blendEnabled = false;

        auto voxelPipelineResult = m_pipelineState.voxel->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            voxelDesc);
        if (voxelPipelineResult.IsErr()) {
            spdlog::warn("Failed to create voxel renderer pipeline: {}", voxelPipelineResult.Error());
            m_pipelineState.voxel.reset();
        } else {
            spdlog::info("Voxel renderer pipeline created successfully (rtvFormat=R8G8B8A8_UNORM).");
        }
    } else {
        spdlog::warn("Voxel raymarch pixel shader compilation failed; experimental voxel backend disabled.");
    }

    if (shaders.taaPS) {
        m_pipelineState.taa = std::make_unique<DX12Pipeline>();

        PipelineDesc taaDesc = {};
        taaDesc.vertexShader = shaders.postVS;
        taaDesc.pixelShader = *shaders.taaPS;
        taaDesc.inputLayout = {};
        taaDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        taaDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
        taaDesc.numRenderTargets = 1;
        taaDesc.depthTestEnabled = false;
        taaDesc.depthWriteEnabled = false;
        taaDesc.cullMode = D3D12_CULL_MODE_NONE;
        taaDesc.blendEnabled = false;

        auto taaPipelineResult = m_pipelineState.taa->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            taaDesc);
        if (taaPipelineResult.IsErr()) {
            spdlog::warn("Failed to create TAA pipeline: {}", taaPipelineResult.Error());
            m_pipelineState.taa.reset();
        }
    }

    if (shaders.ssaoVS && shaders.ssaoPS) {
        m_pipelineState.ssao = std::make_unique<DX12Pipeline>();

        PipelineDesc ssaoDesc = {};
        ssaoDesc.vertexShader = *shaders.ssaoVS;
        ssaoDesc.pixelShader = *shaders.ssaoPS;
        ssaoDesc.inputLayout = {};
        ssaoDesc.rtvFormat = DXGI_FORMAT_R8_UNORM;
        ssaoDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
        ssaoDesc.numRenderTargets = 1;
        ssaoDesc.depthTestEnabled = false;
        ssaoDesc.depthWriteEnabled = false;
        ssaoDesc.cullMode = D3D12_CULL_MODE_NONE;
        ssaoDesc.blendEnabled = false;

        auto ssaoPipelineResult = m_pipelineState.ssao->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            ssaoDesc);
        if (ssaoPipelineResult.IsErr()) {
            spdlog::warn("Failed to create SSAO pipeline: {}", ssaoPipelineResult.Error());
            m_pipelineState.ssao.reset();
        }
    }

    if (m_frameRuntime.asyncComputeSupported && m_pipelineState.computeRootSignature) {
        auto ssaoComputeResult =
            ShaderCompiler::CompileFromFile("assets/shaders/SSAO_Compute.hlsl", "CSMain", "cs_5_1");
        if (ssaoComputeResult.IsOk()) {
            m_pipelineState.ssaoCompute = std::make_unique<DX12ComputePipeline>();
            auto computePipelineResult = m_pipelineState.ssaoCompute->Initialize(
                m_services.device->GetDevice(),
                m_pipelineState.singleSrvUavComputeRootSignature
                    ? m_pipelineState.singleSrvUavComputeRootSignature.Get()
                    : m_pipelineState.computeRootSignature->GetRootSignature(),
                ssaoComputeResult.Value());
            if (computePipelineResult.IsErr()) {
                spdlog::warn("Failed to create SSAO compute pipeline: {}", computePipelineResult.Error());
                m_pipelineState.ssaoCompute.reset();
            } else {
                spdlog::info("SSAO async compute pipeline created successfully");
            }
        } else {
            spdlog::warn("Failed to compile SSAO compute shader: {}", ssaoComputeResult.Error());
        }
    }

    if (m_pipelineState.computeRootSignature) {
        static const char* kHzbInitCS = R"(
Texture2D<float> g_Depth : register(t0);
RWTexture2D<float> g_OutMip : register(u0);

cbuffer FrameConstants : register(b1)
{
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4x4 g_InvProjectionMatrix;
};

static float ReconstructViewZ(float2 uv, float depth)
{
    depth = saturate(depth);
    if (depth >= 1.0f - 1e-4f || depth <= 0.0f)
    {
        return 0.0f;
    }

    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;
    float4 clip = float4(x, y, depth, 1.0f);
    float4 view = mul(g_InvProjectionMatrix, clip);
    float w = max(abs(view.w), 1e-6f);
    return view.z / w;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint w, h;
    g_OutMip.GetDimensions(w, h);
    if (dispatchThreadId.x >= w || dispatchThreadId.y >= h) return;

    float d = g_Depth.Load(int3(dispatchThreadId.xy, 0));
    float2 uv = (float2(dispatchThreadId.xy) + 0.5f) / float2((float)w, (float)h);
    g_OutMip[dispatchThreadId.xy] = ReconstructViewZ(uv, d);
}
)";

        static const char* kHzbDownsampleCS = R"(
Texture2D<float> g_InMip : register(t0);
RWTexture2D<float> g_OutMip : register(u0);

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint outW, outH;
    g_OutMip.GetDimensions(outW, outH);
    if (dispatchThreadId.x >= outW || dispatchThreadId.y >= outH) return;

    uint inW, inH;
    g_InMip.GetDimensions(inW, inH);
    const int2 inMax = int2(int(inW) - 1, int(inH) - 1);

    int2 base = int2(dispatchThreadId.xy) * 2;
    int2 c0 = clamp(base, int2(0, 0), inMax);
    int2 c1 = clamp(base + int2(1, 0), int2(0, 0), inMax);
    int2 c2 = clamp(base + int2(0, 1), int2(0, 0), inMax);
    int2 c3 = clamp(base + int2(1, 1), int2(0, 0), inMax);

    float d0 = g_InMip.Load(int3(c0, 0));
    float d1 = g_InMip.Load(int3(c1, 0));
    float d2 = g_InMip.Load(int3(c2, 0));
    float d3 = g_InMip.Load(int3(c3, 0));

    g_OutMip[dispatchThreadId.xy] = max(max(d0, d1), max(d2, d3));
}
)";

        auto hzbInitResult = ShaderCompiler::CompileFromSource(kHzbInitCS, "CSMain", "cs_5_1");
        if (hzbInitResult.IsOk()) {
            m_pipelineState.hzbInit = std::make_unique<DX12ComputePipeline>();
            auto initResult = m_pipelineState.hzbInit->Initialize(
                m_services.device->GetDevice(),
                m_pipelineState.singleSrvUavComputeRootSignature
                    ? m_pipelineState.singleSrvUavComputeRootSignature.Get()
                    : m_pipelineState.computeRootSignature->GetRootSignature(),
                hzbInitResult.Value());
            if (initResult.IsErr()) {
                spdlog::warn("Failed to create HZB init compute pipeline: {}", initResult.Error());
                m_pipelineState.hzbInit.reset();
            }
        } else {
            spdlog::warn("Failed to compile HZB init compute shader: {}", hzbInitResult.Error());
        }

        auto hzbDownResult = ShaderCompiler::CompileFromSource(kHzbDownsampleCS, "CSMain", "cs_5_1");
        if (hzbDownResult.IsOk()) {
            m_pipelineState.hzbDownsample = std::make_unique<DX12ComputePipeline>();
            auto downResult = m_pipelineState.hzbDownsample->Initialize(
                m_services.device->GetDevice(),
                m_pipelineState.singleSrvUavComputeRootSignature
                    ? m_pipelineState.singleSrvUavComputeRootSignature.Get()
                    : m_pipelineState.computeRootSignature->GetRootSignature(),
                hzbDownResult.Value());
            if (downResult.IsErr()) {
                spdlog::warn("Failed to create HZB downsample compute pipeline: {}", downResult.Error());
                m_pipelineState.hzbDownsample.reset();
            }
        } else {
            spdlog::warn("Failed to compile HZB downsample compute shader: {}", hzbDownResult.Error());
        }
    }

    if (shaders.ssrVS && shaders.ssrPS) {
        m_pipelineState.ssr = std::make_unique<DX12Pipeline>();

        PipelineDesc ssrDesc = {};
        ssrDesc.vertexShader = *shaders.ssrVS;
        ssrDesc.pixelShader = *shaders.ssrPS;
        ssrDesc.inputLayout = {};
        ssrDesc.rtvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
        ssrDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
        ssrDesc.numRenderTargets = 1;
        ssrDesc.depthTestEnabled = false;
        ssrDesc.depthWriteEnabled = false;
        ssrDesc.cullMode = D3D12_CULL_MODE_NONE;
        ssrDesc.blendEnabled = false;

        auto ssrPipelineResult = m_pipelineState.ssr->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            ssrDesc);
        if (ssrPipelineResult.IsErr()) {
            spdlog::warn("Failed to create SSR pipeline: {}", ssrPipelineResult.Error());
            m_pipelineState.ssr.reset();
        }
    }

    if (shaders.motionVS && shaders.motionPS) {
        m_pipelineState.motionVectors = std::make_unique<DX12Pipeline>();

        PipelineDesc mvDesc = {};
        mvDesc.vertexShader = *shaders.motionVS;
        mvDesc.pixelShader = *shaders.motionPS;
        mvDesc.inputLayout = {};
        mvDesc.rtvFormat = DXGI_FORMAT_R16G16_FLOAT;
        mvDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
        mvDesc.numRenderTargets = 1;
        mvDesc.depthTestEnabled = false;
        mvDesc.depthWriteEnabled = false;
        mvDesc.cullMode = D3D12_CULL_MODE_NONE;
        mvDesc.blendEnabled = false;

        auto mvPipelineResult = m_pipelineState.motionVectors->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            mvDesc);
        if (mvPipelineResult.IsErr()) {
            spdlog::warn("Failed to create motion vectors pipeline: {}", mvPipelineResult.Error());
            m_pipelineState.motionVectors.reset();
        }
    }

    m_pipelineState.bloomDownsample = std::make_unique<DX12Pipeline>();
    PipelineDesc bloomDownDesc = postDesc;
    bloomDownDesc.rtvFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    bloomDownDesc.pixelShader =
        ShaderCompiler::CompileFromFile("assets/shaders/PostProcess.hlsl", "BloomDownsamplePS", "ps_5_1")
            .ValueOr(shaders.postPS);
    auto bloomDownResult = m_pipelineState.bloomDownsample->Initialize(
        m_services.device->GetDevice(),
        m_pipelineState.rootSignature->GetRootSignature(),
        bloomDownDesc);
    if (bloomDownResult.IsErr()) {
        return Result<void>::Err("Failed to create bloom downsample pipeline: " + bloomDownResult.Error());
    }

    m_pipelineState.bloomBlurH = std::make_unique<DX12Pipeline>();
    PipelineDesc bloomBlurHDesc = postDesc;
    bloomBlurHDesc.rtvFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    bloomBlurHDesc.pixelShader =
        ShaderCompiler::CompileFromFile("assets/shaders/PostProcess.hlsl", "BloomBlurHPS", "ps_5_1")
            .ValueOr(shaders.postPS);
    auto bloomBlurHResult = m_pipelineState.bloomBlurH->Initialize(
        m_services.device->GetDevice(),
        m_pipelineState.rootSignature->GetRootSignature(),
        bloomBlurHDesc);
    if (bloomBlurHResult.IsErr()) {
        return Result<void>::Err("Failed to create bloom horizontal blur pipeline: " + bloomBlurHResult.Error());
    }

    m_pipelineState.bloomBlurV = std::make_unique<DX12Pipeline>();
    PipelineDesc bloomBlurVDesc = postDesc;
    bloomBlurVDesc.rtvFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    bloomBlurVDesc.pixelShader =
        ShaderCompiler::CompileFromFile("assets/shaders/PostProcess.hlsl", "BloomBlurVPS", "ps_5_1")
            .ValueOr(shaders.postPS);
    auto bloomBlurVResult = m_pipelineState.bloomBlurV->Initialize(
        m_services.device->GetDevice(),
        m_pipelineState.rootSignature->GetRootSignature(),
        bloomBlurVDesc);
    if (bloomBlurVResult.IsErr()) {
        return Result<void>::Err("Failed to create bloom vertical blur pipeline: " + bloomBlurVResult.Error());
    }

    m_pipelineState.bloomComposite = std::make_unique<DX12Pipeline>();
    PipelineDesc bloomCompositeDesc = postDesc;
    bloomCompositeDesc.rtvFormat = DXGI_FORMAT_R11G11B10_FLOAT;
    bloomCompositeDesc.pixelShader =
        ShaderCompiler::CompileFromFile("assets/shaders/PostProcess.hlsl", "BloomUpsamplePS", "ps_5_1")
            .ValueOr(shaders.postPS);
    bloomCompositeDesc.blendEnabled = true;
    auto bloomCompositeResult = m_pipelineState.bloomComposite->Initialize(
        m_services.device->GetDevice(),
        m_pipelineState.rootSignature->GetRootSignature(),
        bloomCompositeDesc);
    if (bloomCompositeResult.IsErr()) {
        return Result<void>::Err("Failed to create bloom composite pipeline: " + bloomCompositeResult.Error());
    }

    auto debugVsResult = ShaderCompiler::CompileFromFile("assets/shaders/Basic.hlsl", "DebugLineVS", "vs_5_1");
    auto debugPsResult = ShaderCompiler::CompileFromFile("assets/shaders/Basic.hlsl", "DebugLinePS", "ps_5_1");
    if (debugVsResult.IsOk() && debugPsResult.IsOk()) {
        m_pipelineState.debugLine = std::make_unique<DX12Pipeline>();

        PipelineDesc dbgDesc = {};
        dbgDesc.vertexShader = debugVsResult.Value();
        dbgDesc.pixelShader = debugPsResult.Value();
        dbgDesc.inputLayout = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
        };
        dbgDesc.rtvFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
        dbgDesc.dsvFormat = DXGI_FORMAT_UNKNOWN;
        dbgDesc.numRenderTargets = 1;
        dbgDesc.depthTestEnabled = false;
        dbgDesc.depthWriteEnabled = false;
        dbgDesc.cullMode = D3D12_CULL_MODE_NONE;
        dbgDesc.blendEnabled = false;
        dbgDesc.primitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;

        auto dbgPipelineResult = m_pipelineState.debugLine->Initialize(
            m_services.device->GetDevice(),
            m_pipelineState.rootSignature->GetRootSignature(),
            dbgDesc);
        if (dbgPipelineResult.IsErr()) {
            spdlog::warn("Failed to create debug line pipeline: {}", dbgPipelineResult.Error());
            m_pipelineState.debugLine.reset();
        }
    } else {
        spdlog::warn("Failed to compile debug line shaders; debug overlay will be disabled");
    }

    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
