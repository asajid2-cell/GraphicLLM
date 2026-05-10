#pragma once

#include "Graphics/FrameContract.h"
#include "Scene/Components.h"

#include <entt/entt.hpp>

namespace Cortex::Scene {
class ECS_Registry;
}

namespace Cortex::Graphics {

[[nodiscard]] bool IsTransparentRenderable(const Scene::RenderableComponent& renderable);

[[nodiscard]] RenderableDepthClass ClassifyRenderableDepth(
    const Scene::ECS_Registry* registry,
    entt::entity entity,
    const Scene::RenderableComponent& renderable);

[[nodiscard]] const char* ToString(RenderableDepthClass depthClass);

} // namespace Cortex::Graphics
