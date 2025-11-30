#pragma once

#include <memory>

namespace Cortex {

// Forward declarations
namespace Graphics {
    class DX12Device;
    class Renderer;
}
namespace Scene {
    class ECS_Registry;
}
class Engine;

// Global service locator for accessing core subsystems
// Used for communication between async loops
class ServiceLocator {
public:
    // Graphics services
    static void SetDevice(Graphics::DX12Device* device) { s_device = device; }
    static Graphics::DX12Device* GetDevice() { return s_device; }

    static void SetRenderer(Graphics::Renderer* renderer) { s_renderer = renderer; }
    static Graphics::Renderer* GetRenderer() { return s_renderer; }

    // Scene services
    static void SetRegistry(Scene::ECS_Registry* registry) { s_registry = registry; }
    static Scene::ECS_Registry* GetRegistry() { return s_registry; }

    // Engine service (for scene management / high-level controls)
    static void SetEngine(Engine* engine) { s_engine = engine; }
    static Engine* GetEngine() { return s_engine; }

private:
    inline static Graphics::DX12Device* s_device = nullptr;
    inline static Graphics::Renderer* s_renderer = nullptr;
    inline static Scene::ECS_Registry* s_registry = nullptr;
    inline static Engine* s_engine = nullptr;
};

} // namespace Cortex
