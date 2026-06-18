#include "RendererLightingRigControl.h"

namespace Cortex::Graphics {

void ApplyLightingRigControl(Renderer& renderer,
                             Renderer::LightingRig rig,
                             Scene::ECS_Registry* registry) {
    if (rig == Renderer::LightingRig::Custom || registry == nullptr) {
        return;
    }

    renderer.ApplyLightingRig(rig, registry);
    renderer.SetFogEnabled(true);
    renderer.SetFogParams(0.012f, 0.15f, 0.44f);
    renderer.SetGodRayIntensity(rig == Renderer::LightingRig::StreetLanterns ? 0.42f : 0.30f);
    renderer.SetBloomShape(0.95f, 0.55f, 1.45f);
}

} // namespace Cortex::Graphics
