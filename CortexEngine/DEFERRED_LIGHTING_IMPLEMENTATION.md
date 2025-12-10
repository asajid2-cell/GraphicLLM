# Deferred Lighting Implementation Guide

This file contains the code to add deferred lighting to the visibility buffer pipeline.

## Step 1: Add Root Signature Creation

Add this to `VisibilityBuffer.cpp` in `CreateRootSignatures()` after the resolve root signature:

```cpp
// ========================================================================
// Deferred Lighting Root Signature (Graphics - Fullscreen Pass)
// Matches DeferredLighting.hlsl:
//   b0: Lighting parameters (view matrices, sun, IBL, etc.)
//   t0: G-buffer albedo SRV
//   t1: G-buffer normal+roughness SRV
//   t2: G-buffer emissive+metallic SRV
//   t3: Depth buffer SRV
//   t4: Environment map SRV (TextureCube)
//   t5: Shadow map SRV
//   s0: Linear sampler
//   s1: Shadow comparison sampler
// ========================================================================
{
    // Descriptor ranges for G-buffer SRVs (t0-t3: 4 textures)
    D3D12_DESCRIPTOR_RANGE1 gbufferRange = {};
    gbufferRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    gbufferRange.NumDescriptors = 4;
    gbufferRange.BaseShaderRegister = 0;
    gbufferRange.RegisterSpace = 0;
    gbufferRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    gbufferRange.OffsetInDescriptorsFromTableStart = 0;

    // Descriptor ranges for env + shadow (t4-t5: 2 textures)
    D3D12_DESCRIPTOR_RANGE1 envShadowRange = {};
    envShadowRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    envShadowRange.NumDescriptors = 2;
    envShadowRange.BaseShaderRegister = 4;
    envShadowRange.RegisterSpace = 0;
    envShadowRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
    envShadowRange.OffsetInDescriptorsFromTableStart = 0;

    D3D12_ROOT_PARAMETER1 params[3] = {};

    // b0: Lighting parameters (constant buffer)
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.RegisterSpace = 0;
    params[0].Constants.Num32BitValues = sizeof(DeferredLightingParams) / 4; // Size in dwords
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // t0-t3: G-buffer + depth SRVs
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &gbufferRange;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // t4-t5: Environment + shadow map SRVs
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &envShadowRange;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // Static samplers
    D3D12_STATIC_SAMPLER_DESC samplers[2] = {};

    // s0: Linear sampler
    samplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
    samplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    samplers[0].ShaderRegister = 0;
    samplers[0].RegisterSpace = 0;
    samplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    // s1: Shadow comparison sampler
    samplers[1].Filter = D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT;
    samplers[1].AddressU = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressV = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].AddressW = D3D12_TEXTURE_ADDRESS_MODE_BORDER;
    samplers[1].ComparisonFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
    samplers[1].BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
    samplers[1].ShaderRegister = 1;
    samplers[1].RegisterSpace = 0;
    samplers[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc = {};
    rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootDesc.Desc_1_1.NumParameters = 3;
    rootDesc.Desc_1_1.pParameters = params;
    rootDesc.Desc_1_1.NumStaticSamplers = 2;
    rootDesc.Desc_1_1.pStaticSamplers = samplers;
    rootDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

    ComPtr<ID3DBlob> signature, error;
    HRESULT hr = D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &error);
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to serialize deferred lighting root signature");
    }

    hr = m_device->GetDevice()->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_deferredLightingRootSignature)
    );
    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create deferred lighting root signature");
    }
}
```

## Step 2: Add Pipeline Creation

Add this to `VisibilityBuffer.cpp` in `CreatePipelines()` after the blit pipeline:

```cpp
// ========================================================================
// Deferred Lighting Pipeline (Graphics - Fullscreen)
// ========================================================================
{
    // Compile shaders
    ComPtr<ID3DBlob> vsBlob, psBlob;
    auto vsResult = m_device->CompileShader(
        L"assets/shaders/DeferredLighting.hlsl",
        "VSMain", "vs_6_6",
        {}, &vsBlob
    );
    if (vsResult.IsErr()) {
        spdlog::warn("Failed to compile deferred lighting VS: {}", vsResult.Error());
        return Result<void>::Ok();
    }

    auto psResult = m_device->CompileShader(
        L"assets/shaders/DeferredLighting.hlsl",
        "PSMain", "ps_6_6",
        {}, &psBlob
    );
    if (psResult.IsErr()) {
        spdlog::warn("Failed to compile deferred lighting PS: {}", psResult.Error());
        return Result<void>::Ok();
    }

    // Create PSO
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = m_deferredLightingRootSignature.Get();
    psoDesc.VS = {vsBlob->GetBufferPointer(), vsBlob->GetBufferSize()};
    psoDesc.PS = {psBlob->GetBufferPointer(), psBlob->GetBufferSize()};

    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.SampleMask = UINT_MAX;

    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.RasterizerState.DepthClipEnable = TRUE;

    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;

    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT; // HDR format
    psoDesc.SampleDesc.Count = 1;

    HRESULT hr = device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_deferredLightingPipeline));
    if (FAILED(hr)) {
        spdlog::warn("Failed to create deferred lighting PSO");
        return Result<void>::Ok();
    }

    spdlog::info("VisibilityBuffer: Deferred lighting pipeline created");
}
```

## Step 3: Implement ApplyDeferredLighting Function

Add this function to VisibilityBuffer.cpp:

```cpp
Result<void> VisibilityBufferRenderer::ApplyDeferredLighting(
    ID3D12GraphicsCommandList* cmdList,
    ID3D12Resource* hdrTarget,
    D3D12_CPU_DESCRIPTOR_HANDLE hdrRTV,
    ID3D12Resource* depthBuffer,
    D3D12_GPU_DESCRIPTOR_HANDLE depthSRV,
    D3D12_GPU_DESCRIPTOR_HANDLE envMapSRV,
    D3D12_GPU_DESCRIPTOR_HANDLE shadowMapSRV,
    const DeferredLightingParams& params
) {
    if (!m_deferredLightingPipeline) {
        return Result<void>::Err("Deferred lighting pipeline not initialized");
    }

    // Transition G-buffers to shader resource
    D3D12_RESOURCE_BARRIER barriers[3] = {};

    if (m_albedoState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[0].Transition.pResource = m_gbufferAlbedo.Get();
        barriers[0].Transition.StateBefore = m_albedoState;
        barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_albedoState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (m_normalRoughnessState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[1].Transition.pResource = m_gbufferNormalRoughness.Get();
        barriers[1].Transition.StateBefore = m_normalRoughnessState;
        barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_normalRoughnessState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    if (m_emissiveMetallicState != D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE) {
        barriers[2].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barriers[2].Transition.pResource = m_gbufferEmissiveMetallic.Get();
        barriers[2].Transition.StateBefore = m_emissiveMetallicState;
        barriers[2].Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        barriers[2].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        m_emissiveMetallicState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    }

    cmdList->ResourceBarrier(3, barriers);

    // Set render target
    cmdList->OMSetRenderTargets(1, &hdrRTV, FALSE, nullptr);

    // Set viewport and scissor
    D3D12_VIEWPORT viewport = {0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height), 0.0f, 1.0f};
    D3D12_RECT scissor = {0, 0, static_cast<LONG>(m_width), static_cast<LONG>(m_height)};
    cmdList->RSSetViewports(1, &viewport);
    cmdList->RSSetScissorRects(1, &scissor);

    // Set pipeline
    cmdList->SetPipelineState(m_deferredLightingPipeline.Get());
    cmdList->SetGraphicsRootSignature(m_deferredLightingRootSignature.Get());

    // Set descriptor heap
    ID3D12DescriptorHeap* heaps[] = {m_descriptorManager->GetCBV_SRV_UAV_Heap()};
    cmdList->SetDescriptorHeaps(1, heaps);

    // b0: Lighting parameters
    cmdList->SetGraphicsRoot32BitConstants(0, sizeof(DeferredLightingParams) / 4, &params, 0);

    // t0-t3: G-buffer + depth SRVs (create temporary descriptor table)
    // TODO: You'll need to allocate a descriptor table with 4 consecutive SRVs
    // For now, use individual root descriptors if supported

    // t4-t5: Environment + shadow map SRVs
    // TODO: Similar allocation needed

    // Draw fullscreen triangle
    cmdList->IASetPrimitiveTopology(D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmdList->DrawInstanced(3, 1, 0, 0);

    return Result<void>::Ok();
}
```

## Step 4: Integration

Replace the debug blit call in Renderer.cpp with:

```cpp
// Apply deferred lighting
VisibilityBufferRenderer::DeferredLightingParams lightingParams = {};
lightingParams.invViewProj = glm::inverse(m_frameDataCPU.viewProjectionMatrix);
lightingParams.viewMatrix = m_frameDataCPU.viewMatrix;
lightingParams.lightViewProj = m_frameDataCPU.lightViewProjection;
lightingParams.cameraPos = m_camera->GetPosition();
lightingParams.sunDirection = m_sunDirection;
lightingParams.sunColor = m_sunColor;
lightingParams.sunIntensity = m_sunIntensity;
lightingParams.iblDiffuseIntensity = m_iblDiffuseIntensity;
lightingParams.iblSpecularIntensity = m_iblSpecularIntensity;

auto lightingResult = m_visibilityBuffer->ApplyDeferredLighting(
    m_commandList.Get(),
    m_hdrColor.Get(),
    m_hdrRTV.cpu,
    m_depthBuffer.Get(),
    m_depthSRV.gpu,
    m_envMapSRV.gpu,      // You'll need to get this from your environment system
    m_shadowMapSRV.gpu,   // You'll need to get this from your shadow system
    lightingParams
);

if (lightingResult.IsErr()) {
    spdlog::warn("Deferred lighting failed: {}", lightingResult.Error());
}
```

## Notes

- This implementation is simplified and may need adjustments for your specific descriptor allocation system
- You'll need to handle descriptor table allocation properly
- The shader expects specific formats - make sure they match
- Test incrementally to catch issues early
