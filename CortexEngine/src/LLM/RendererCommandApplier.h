#pragma once

#include <string>

namespace Cortex {
namespace Graphics {
    class Renderer;
}
namespace Scene {
    class ECS_Registry;
}
}

namespace Cortex::LLM {

struct ModifyRendererCommand;

struct RendererCommandApplyResult {
    bool success = false;
    std::string message;
};

RendererCommandApplyResult ApplyModifyRendererCommand(ModifyRendererCommand& command,
                                                      Graphics::Renderer& renderer,
                                                      Scene::ECS_Registry* registry);

} // namespace Cortex::LLM
