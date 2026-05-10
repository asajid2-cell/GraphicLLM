#include "Renderer.h"

namespace Cortex::Graphics {

Result<void> Renderer::CreateCommandList() {
    HRESULT hr = m_services.device->GetDevice()->CreateCommandList(
        0,
        D3D12_COMMAND_LIST_TYPE_DIRECT,
        m_commandResources.graphicsAllocators[0].Get(),
        nullptr,
        IID_PPV_ARGS(&m_commandResources.graphicsList));

    if (FAILED(hr)) {
        return Result<void>::Err("Failed to create command list");
    }

    m_commandResources.graphicsList->Close();

    return Result<void>::Ok();
}

Result<void> Renderer::CompileShaders() {
    auto shadersResult = CompileRendererPipelineShaders();
    if (shadersResult.IsErr()) {
        return Result<void>::Err(shadersResult.Error());
    }

    auto rootResult = CreateRendererRootSignaturesAndComputePasses();
    if (rootResult.IsErr()) {
        return rootResult;
    }

    const RendererCompiledShaders& shaders = shadersResult.Value();

    auto geometryResult = CreateGeometryPipelineStates(shaders);
    if (geometryResult.IsErr()) {
        return geometryResult;
    }

    auto screenResult = CreateScreenSpacePipelineStates(shaders);
    if (screenResult.IsErr()) {
        return screenResult;
    }

    return Result<void>::Ok();
}

Result<void> Renderer::CreatePipeline() {
    return Result<void>::Ok();
}

} // namespace Cortex::Graphics
