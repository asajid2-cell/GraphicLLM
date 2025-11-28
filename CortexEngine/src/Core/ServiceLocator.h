#pragma once

#include <memory>

// Forward declarations
namespace Cortex {
    namespace Graphics {
        class DX12Device;
        class Renderer;
    }
    namespace Scene {
        class ECS_Registry;
    }
}

namespace Cortex {

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

private:
    inline static Graphics::DX12Device* s_device = nullptr;
    inline static Graphics::Renderer* s_renderer = nullptr;
    inline static Scene::ECS_Registry* s_registry = nullptr;
};

} // namespace Cortex
