#include "Graphics/RenderableClassification.h"

#include "Graphics/MaterialPresetRegistry.h"
#include "Scene/ECS_Registry.h"

namespace Cortex::Graphics {

bool IsTransparentRenderable(const Scene::RenderableComponent& renderable) {
    if (renderable.alphaMode == Scene::RenderableComponent::AlphaMode::Blend) {
        return true;
    }

    if (renderable.transmissionFactor > 0.001f) {
        return true;
    }

    if (MaterialPresetRegistry::ContainsToken(renderable.presetName, "glass")) {
        return true;
    }

    return false;
}

RenderableDepthClass ClassifyRenderableDepth(
    const Scene::ECS_Registry* registry,
    entt::entity entity,
    const Scene::RenderableComponent& renderable) {
    if (!renderable.visible || !renderable.mesh) {
        return RenderableDepthClass::Invalid;
    }

    if (registry && registry->HasComponent<Scene::WaterSurfaceComponent>(entity)) {
        return RenderableDepthClass::WaterDepthTestedNoWrite;
    }

    if (renderable.renderLayer == Scene::RenderableComponent::RenderLayer::Overlay) {
        return RenderableDepthClass::OverlayDepthTestedNoWrite;
    }

    if (IsTransparentRenderable(renderable)) {
        return RenderableDepthClass::TransparentDepthTested;
    }

    const bool alphaTest = renderable.alphaMode == Scene::RenderableComponent::AlphaMode::Mask;
    if (alphaTest && renderable.doubleSided) {
        return RenderableDepthClass::DoubleSidedAlphaTestedDepthWriting;
    }
    if (alphaTest) {
        return RenderableDepthClass::AlphaTestedDepthWriting;
    }
    if (renderable.doubleSided) {
        return RenderableDepthClass::DoubleSidedOpaqueDepthWriting;
    }
    return RenderableDepthClass::OpaqueDepthWriting;
}

const char* ToString(RenderableDepthClass depthClass) {
    switch (depthClass) {
    case RenderableDepthClass::Invalid:
        return "invalid";
    case RenderableDepthClass::OpaqueDepthWriting:
        return "opaque_depth_writing";
    case RenderableDepthClass::AlphaTestedDepthWriting:
        return "alpha_tested_depth_writing";
    case RenderableDepthClass::DoubleSidedOpaqueDepthWriting:
        return "double_sided_opaque_depth_writing";
    case RenderableDepthClass::DoubleSidedAlphaTestedDepthWriting:
        return "double_sided_alpha_tested_depth_writing";
    case RenderableDepthClass::TransparentDepthTested:
        return "transparent_depth_tested";
    case RenderableDepthClass::WaterDepthTestedNoWrite:
        return "water_depth_tested_no_write";
    case RenderableDepthClass::OverlayDepthTestedNoWrite:
        return "overlay_depth_tested_no_write";
    default:
        return "unknown";
    }
}

} // namespace Cortex::Graphics
