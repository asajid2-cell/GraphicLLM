#pragma once

#include "SceneCommands.h"
#include "Scene/ECS_Registry.h"
#include "Graphics/Renderer.h"
#include "SceneLookup.h"
#include <queue>
#include <mutex>
#include <memory>
#include <optional>
#include <unordered_map>

namespace Cortex::LLM {

/**
 * Thread-safe command queue for scene manipulation
 *
 * The Architect loop pushes commands here asynchronously
 * The main render loop executes them during the update phase
 */
class CommandQueue {
public:
    CommandQueue() = default;

    // Push a command to the queue (thread-safe)
    void Push(std::shared_ptr<SceneCommand> command);

    // Push multiple commands
    void PushBatch(const std::vector<std::shared_ptr<SceneCommand>>& commands);

    // Execute all pending commands (call from main thread)
    void ExecuteAll(Scene::ECS_Registry* registry, Graphics::Renderer* renderer);

    // Check if queue has pending commands
    bool HasPending() const;

    // Get pending command count
    size_t GetPendingCount() const;

    // Clear all pending commands
    void Clear();

    // Drain status messages generated during execution
    std::vector<CommandStatus> ConsumeStatus();

    // Get the last spawned entity name (if still valid)
    std::optional<std::string> GetLastSpawnedName(Scene::ECS_Registry* registry) const;

    // Rebuild lookup cache from registry (call after scene boot)
    void RefreshLookup(Scene::ECS_Registry* registry);

    // Build a compact scene summary for prompt conditioning
    std::string BuildSceneSummary(Scene::ECS_Registry* registry, size_t maxChars = 1200) const;

private:
    std::queue<std::shared_ptr<SceneCommand>> m_commands;
    mutable std::mutex m_mutex;
    std::queue<CommandStatus> m_status;
    mutable std::mutex m_statusMutex;
    SceneLookup m_lookup;
    uint32_t m_spawnIndex = 0;

    // Execute a single command
    void ExecuteCommand(SceneCommand* command, Scene::ECS_Registry* registry, Graphics::Renderer* renderer);

    // Command execution helpers
    void ExecuteAddEntity(AddEntityCommand* cmd, Scene::ECS_Registry* registry, Graphics::Renderer* renderer);
    void ExecuteRemoveEntity(RemoveEntityCommand* cmd, Scene::ECS_Registry* registry);
    void ExecuteModifyTransform(ModifyTransformCommand* cmd, Scene::ECS_Registry* registry);
    void ExecuteModifyMaterial(ModifyMaterialCommand* cmd, Scene::ECS_Registry* registry);
    void ExecuteModifyCamera(ModifyCameraCommand* cmd, Scene::ECS_Registry* registry);

    // Shared mesh cache so repeated shapes reuse GPU buffers
    std::unordered_map<AddEntityCommand::EntityType, std::shared_ptr<Scene::MeshData>> m_meshCache;

    void PushStatus(bool success, const std::string& message);
};

} // namespace Cortex::LLM
