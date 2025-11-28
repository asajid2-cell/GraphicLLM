#pragma once

#include "SceneCommands.h"
#include "Scene/ECS_Registry.h"
#include "Scene/Components.h"
#include <deque>
#include <optional>
#include <string>
#include <unordered_map>

namespace Cortex::LLM {

// Status surfaced back to the UI/logs for each command
struct CommandStatus {
    bool success = true;
    std::string message;
};

// Helper for resolving entity targets with loose matching and recent history
class SceneLookup {
public:
    // Add or refresh an entity reference
    void TrackEntity(entt::entity entity,
                     const std::string& tag,
                     AddEntityCommand::EntityType type,
                     const glm::vec4& color);

    // Remove an entity from caches/history
    void ForgetEntity(entt::entity entity);

    // Rebuild caches from registry (used at startup)
    void Rebuild(Scene::ECS_Registry* registry);

    // Resolve a user/LLM-provided name with normalization, recent fallbacks, and color/type hints
    entt::entity ResolveTarget(const std::string& rawName,
                               Scene::ECS_Registry* registry,
                               std::string& outHint);

    // Get last spawned entity name (if still valid)
    std::optional<std::string> GetLastSpawnedName(Scene::ECS_Registry* registry) const;

    // Build a compact scene summary for prompt context
    std::string BuildSummary(Scene::ECS_Registry* registry, size_t maxChars = 1200) const;

private:
    struct Entry {
        entt::entity id{entt::null};
        std::string normalizedTag;
        std::string displayTag;
        AddEntityCommand::EntityType type{AddEntityCommand::EntityType::Cube};
        std::string colorLabel;
    };

    mutable std::deque<Entry> m_recent;
    mutable std::unordered_map<std::string, entt::entity> m_nameToEntity;
    mutable entt::entity m_lastSpawned{entt::null};

    static std::string Normalize(const std::string& name);
    static std::string ColorLabel(const glm::vec4& color);
    static std::string TypeToString(AddEntityCommand::EntityType type);
    static bool ContainsToken(const std::string& haystack, const std::string& token);

    void PruneInvalid(Scene::ECS_Registry* registry) const;
    entt::entity PickMostRecentValid(Scene::ECS_Registry* registry) const;
};

} // namespace Cortex::LLM
