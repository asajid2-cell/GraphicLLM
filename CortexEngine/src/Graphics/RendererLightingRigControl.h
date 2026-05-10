#pragma once

#include "Renderer.h"

namespace Cortex::Scene {
class ECS_Registry;
}

namespace Cortex::Graphics {

void ApplyLightingRigControl(Renderer& renderer,
                             Renderer::LightingRig rig,
                             Scene::ECS_Registry* registry);

} // namespace Cortex::Graphics
