#pragma once

#include "SceneCommands.h"
#include "Scene/ECS_Registry.h"
#include "SceneLookup.h"
#include <queue>
#include <mutex>
#include <memory>
#include <optional>
#include <unordered_map>
#include <functional>

namespace Cortex {
    namespace Graphics {
        class Renderer;
    }
}

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

    // Set a callback that is invoked when the logical
    // focus target changes (e.g., last spawned or edited group).
    void SetFocusCallback(std::function<void(const std::string&)>&& cb) {
        m_focusCallback = std::move(cb);
    }

    // Keep the queue's notion of the "currently focused" entity in sync with
    // the engine's editor-style selection. When an LLM command names the
    // focus target explicitly (e.g., after the user picks an object and the
    // engine advertises its tag), we prefer to operate on this exact entity
    // instead of resolving purely by name or falling back to heuristics.
    void SetCurrentFocus(const std::string& name, entt::entity id) {
        m_currentFocusName = name;
        m_currentFocusEntity = id;
    }

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

    // Last high-level scene recipe generated from a scene_plan (if any).
    std::string GetLastSceneRecipe() const { return m_lastSceneRecipe; }

    // Optional callback invoked when the LLM explicitly selects or focuses
    // an entity by name. The callback returns the resolved tag (if any) so
    // status messages can reflect the concrete scene name.
    void SetSelectionCallback(std::function<std::optional<std::string>(const std::string&)>&& cb) {
        m_selectionCallback = std::move(cb);
    }

    void SetFocusCameraCallback(std::function<void(const std::string&)>&& cb) {
        m_focusCameraCallback = std::move(cb);
    }

private:
    std::queue<std::shared_ptr<SceneCommand>> m_commands;
    mutable std::mutex m_mutex;
    std::queue<CommandStatus> m_status;
    mutable std::mutex m_statusMutex;
    SceneLookup m_lookup;
    uint32_t m_spawnIndex = 0;
    std::string m_lastSceneRecipe;
    std::function<void(const std::string&)> m_focusCallback;
    std::function<std::optional<std::string>(const std::string&)> m_selectionCallback;
    std::function<void(const std::string&)> m_focusCameraCallback;

    // Editor-driven focus state (kept in sync by the Engine). This lets us
    // "lock" LLM edits to the same concrete entity that the user currently
    // has selected/framed, even when names are ambiguous.
    std::string m_currentFocusName;
    entt::entity m_currentFocusEntity{ entt::null };

    // Execute a single command
    void ExecuteCommand(SceneCommand* command, Scene::ECS_Registry* registry, Graphics::Renderer* renderer);

    // Command execution helpers
    void ExecuteAddEntity(AddEntityCommand* cmd, Scene::ECS_Registry* registry, Graphics::Renderer* renderer);
    void ExecuteRemoveEntity(RemoveEntityCommand* cmd, Scene::ECS_Registry* registry);
    void ExecuteModifyTransform(ModifyTransformCommand* cmd, Scene::ECS_Registry* registry);
    void ExecuteModifyMaterial(ModifyMaterialCommand* cmd, Scene::ECS_Registry* registry);
    void ExecuteModifyCamera(ModifyCameraCommand* cmd, Scene::ECS_Registry* registry);
    void ExecuteAddLight(AddLightCommand* cmd, Scene::ECS_Registry* registry, Graphics::Renderer* renderer);
    void ExecuteModifyLight(ModifyLightCommand* cmd, Scene::ECS_Registry* registry);
    void ExecuteModifyRenderer(ModifyRendererCommand* cmd, Graphics::Renderer* renderer, Scene::ECS_Registry* registry);
    void ExecuteAddPattern(AddPatternCommand* cmd, Scene::ECS_Registry* registry, Graphics::Renderer* renderer);
    void ExecuteAddCompound(AddCompoundCommand* cmd, Scene::ECS_Registry* registry, Graphics::Renderer* renderer);
    void ExecuteModifyGroup(ModifyGroupCommand* cmd, Scene::ECS_Registry* registry);
    void ExecuteScenePlan(ScenePlanCommand* cmd, Scene::ECS_Registry* registry, Graphics::Renderer* renderer);

    // Helper that prefers the externally provided focus entity when the
    // target name matches the current focus name; otherwise falls back to
    // the standard SceneLookup resolution logic.
    entt::entity ResolveTargetWithFocus(const std::string& targetName,
                                        Scene::ECS_Registry* registry,
                                        std::string& outHint);

    // Region builders used by ScenePlanCommand
    void BuildFieldRegion(const ScenePlanCommand::Region& region,
                          Scene::ECS_Registry* registry,
                          Graphics::Renderer* renderer);
    void BuildRoadRegion(const ScenePlanCommand::Region& region,
                         Scene::ECS_Registry* registry,
                         Graphics::Renderer* renderer);
    void BuildGenericRegion(const ScenePlanCommand::Region& region,
                            Scene::ECS_Registry* registry,
                            Graphics::Renderer* renderer);

    // Shared mesh cache so repeated shapes reuse GPU buffers
    struct MeshKey {
        AddEntityCommand::EntityType type;
        uint32_t segmentsPrimary;
        uint32_t segmentsSecondary;

        bool operator==(const MeshKey& other) const noexcept {
            return type == other.type &&
                   segmentsPrimary == other.segmentsPrimary &&
                   segmentsSecondary == other.segmentsSecondary;
        }
    };

    struct MeshKeyHasher {
        size_t operator()(const MeshKey& key) const noexcept {
            size_t h1 = std::hash<int>{}(static_cast<int>(key.type));
            size_t h2 = std::hash<uint32_t>{}(key.segmentsPrimary);
            size_t h3 = std::hash<uint32_t>{}(key.segmentsSecondary);
            return ((h1 * 251u) ^ (h2 * 131u)) ^ h3;
        }
    };

    std::unordered_map<MeshKey, std::shared_ptr<Scene::MeshData>, MeshKeyHasher> m_meshCache;
    // Separate cache for glTF sample models keyed by asset name (e.g., "DamagedHelmet").
    std::unordered_map<std::string, std::shared_ptr<Scene::MeshData>> m_modelMeshCache;

    void PushStatus(bool success, const std::string& message);
};

} // namespace Cortex::LLM
