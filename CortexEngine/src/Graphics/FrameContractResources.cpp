#include "Graphics/FrameContractResources.h"

#include <string>

namespace Cortex::Graphics {

uint32_t BytesPerPixelForContract(DXGI_FORMAT format) {
    switch (format) {
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R32G32B32A32_UINT:
    case DXGI_FORMAT_R32G32B32A32_SINT:
        return 16;
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_UNORM:
    case DXGI_FORMAT_R16G16B16A16_SNORM:
    case DXGI_FORMAT_R32G32_FLOAT:
    case DXGI_FORMAT_R32G32_UINT:
    case DXGI_FORMAT_R32G32_SINT:
        return 8;
    case DXGI_FORMAT_R11G11B10_FLOAT:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R16G16_FLOAT:
    case DXGI_FORMAT_R16G16_UNORM:
    case DXGI_FORMAT_R32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:
    case DXGI_FORMAT_D32_FLOAT:
        return 4;
    case DXGI_FORMAT_R8_UNORM:
    case DXGI_FORMAT_R8_UINT:
        return 1;
    default:
        return 4;
    }
}

const char* ExpectedReadStateClass(const char* name) {
    if (!name) {
        return "unknown";
    }
    const std::string resource = name;
    if (resource == "depth") {
        return "depth_read_or_shader_resource";
    }
    if (resource == "frame_constants") {
        return "constant_buffer";
    }
    if (resource == "renderables" || resource == "environment" ||
        resource == "acceleration_structures") {
        return "shader_resource";
    }
    if (resource == "back_buffer") {
        return "render_target_existing_contents";
    }
    return "shader_resource";
}

const char* ExpectedWriteStateClass(const char* name) {
    if (!name) {
        return "unknown";
    }
    const std::string resource = name;
    if (resource == "depth" || resource == "shadow_map") {
        return "depth_write";
    }
    if (resource == "back_buffer") {
        return "render_target_to_present";
    }
    if (resource == "rt_shadow_mask" || resource == "rt_gi" ||
        resource == "rt_reflection" || resource == "hzb" ||
        resource == "temporal_rejection_mask_stats" ||
        resource == "rt_reflection_signal_stats" ||
        resource == "rt_reflection_history_signal_stats") {
        return "unordered_access";
    }
    if (resource == "acceleration_structures") {
        return "acceleration_structure_build";
    }
    if (resource == "taa_history") {
        return "copy_dest_or_shader_resource";
    }
    return "render_target_or_unordered_access";
}

} // namespace Cortex::Graphics
