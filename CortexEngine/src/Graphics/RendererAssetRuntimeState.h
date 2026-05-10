#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Graphics/AssetRegistry.h"
#include "Graphics/GpuJobQueue.h"
#include "Graphics/RendererTextureUploadState.h"
#include "Graphics/RHI/DX12Texture.h"

namespace Cortex::Graphics {

struct RendererAssetRuntimeState {
    mutable AssetRegistry registry;
    TextureUploadRuntimeState textureUploads;
    std::unordered_map<const Scene::MeshData*, std::string> meshAssetKeys;
    GpuJobQueueState gpuJobs;
    std::unordered_map<std::string, std::shared_ptr<DX12Texture>> textureCache;
};

} // namespace Cortex::Graphics
