#pragma once

#include "Graphics/RHI/D3D12Includes.h"

namespace Cortex::Scene {
struct MeshData;
}

namespace Cortex::Graphics::MeshDrawPass {

struct DrawResult {
    bool submitted = false;
    UINT indexCount = 0;
};

[[nodiscard]] DrawResult DrawIndexedMesh(ID3D12GraphicsCommandList* commandList,
                                         const Cortex::Scene::MeshData& mesh);

} // namespace Cortex::Graphics::MeshDrawPass
