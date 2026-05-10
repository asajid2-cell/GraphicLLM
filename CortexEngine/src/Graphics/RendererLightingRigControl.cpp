#include "RendererLightingRigControl.h"

namespace Cortex::Graphics {

void ApplyLightingRigControl(Renderer& renderer,
                             Renderer::LightingRig rig,
                             Scene::ECS_Registry* registry) {
    if (rig == Renderer::LightingRig::Custom || registry == nullptr) {
        return;
    }

    renderer.ApplyLightingRig(rig, registry);
}

} // namespace Cortex::Graphics
