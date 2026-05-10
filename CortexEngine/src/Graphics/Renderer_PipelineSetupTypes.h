#pragma once

#include "Graphics/RHI/DX12Pipeline.h"

#include <optional>

namespace Cortex::Graphics {

struct RendererCompiledShaders {
    ShaderBytecode basicVS;
    ShaderBytecode basicPS;
    std::optional<ShaderBytecode> transparentPS;

    std::optional<ShaderBytecode> skyboxVS;
    std::optional<ShaderBytecode> skyboxPS;

    ShaderBytecode shadowVS;
    std::optional<ShaderBytecode> shadowAlphaPS;
    std::optional<ShaderBytecode> depthAlphaPS;

    ShaderBytecode postVS;
    ShaderBytecode postPS;
    std::optional<ShaderBytecode> voxelPS;
    std::optional<ShaderBytecode> taaPS;

    std::optional<ShaderBytecode> ssaoVS;
    std::optional<ShaderBytecode> ssaoPS;
    std::optional<ShaderBytecode> ssrVS;
    std::optional<ShaderBytecode> ssrPS;
    std::optional<ShaderBytecode> motionVS;
    std::optional<ShaderBytecode> motionPS;

    std::optional<ShaderBytecode> waterVS;
    std::optional<ShaderBytecode> waterPS;
};

} // namespace Cortex::Graphics
