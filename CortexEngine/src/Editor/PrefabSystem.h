// PrefabSystem.h
// Prefab system for creating, instantiating, and managing reusable entity templates.

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json_fwd.hpp>

// Forward declare ECS types
namespace entt { class registry; }
using Entity = uint32_t;

namespace Cortex::Editor {

// Forward declarations
class ECS_Registry;

// ============================================================================
// Component Data Storage
// ============================================================================

// Generic component data for serialization
struct ComponentData {
    std::string typeName;
    nlohmann::json data;
};

// ============================================================================
// Entity Template
// ============================================================================

struct EntityTemplate {
    std::string name;
    std::string tag;
    bool active = true;

    // Transform data
    glm::vec3 position = glm::vec3(0);
    glm::quat rotation = glm::quat(1, 0, 0, 0);
    glm::vec3 scale = glm::vec3(1);

    // Components
    std::vector<ComponentData> components;

    // Child indices (into Prefab's entities array)
    std::vector<size_t> children;

    // Parent index (-1 for root)
    int parentIndex = -1;

    // Local ID within prefab (for references)
    int localId = -1;
};

// ============================================================================
// Prefab Definition
// ============================================================================

class Prefab {
public:
    Prefab() = default;
    Prefab(const std::string& name);
    ~Prefab() = default;

    // Prefab info
    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    const std::string& GetPath() const { return m_path; }
    void SetPath(const std::string& path) { m_path = path; }

    // Entities
    void AddEntity(const EntityTemplate& entity);
    void RemoveEntity(size_t index);
    EntityTemplate& GetEntity(size_t index) { return m_entities[index]; }
    const EntityTemplate& GetEntity(size_t index) const { return m_entities[index]; }
    size_t GetEntityCount() const { return m_entities.size(); }

    // Get root entity (first entity is always root)
    EntityTemplate& GetRoot() { return m_entities[0]; }
    const EntityTemplate& GetRoot() const { return m_entities[0]; }

    // Hierarchy
    std::vector<size_t> GetRootEntities() const;
    std::vector<size_t> GetChildren(size_t parentIndex) const;

    // Serialization
    nlohmann::json ToJson() const;
    static Prefab FromJson(const nlohmann::json& json);

    bool SaveToFile(const std::string& path);
    static std::unique_ptr<Prefab> LoadFromFile(const std::string& path);

    // Modification tracking
    bool IsDirty() const { return m_dirty; }
    void SetDirty(bool dirty = true) { m_dirty = dirty; }

    // Thumbnail
    const std::vector<uint8_t>& GetThumbnail() const { return m_thumbnail; }
    void SetThumbnail(const std::vector<uint8_t>& data) { m_thumbnail = data; }

    // Tags for organization
    void AddTag(const std::string& tag) { m_tags.push_back(tag); }
    void RemoveTag(const std::string& tag);
    const std::vector<std::string>& GetTags() const { return m_tags; }
    bool HasTag(const std::string& tag) const;

private:
    std::string m_name;
    std::string m_path;
    std::vector<EntityTemplate> m_entities;
    std::vector<std::string> m_tags;
    std::vector<uint8_t> m_thumbnail;
    bool m_dirty = false;
};

// ============================================================================
// Prefab Instance (tracks overrides)
// ============================================================================

struct PrefabOverride {
    int entityLocalId;          // Which entity in prefab
    std::string componentType;  // Which component
    std::string propertyPath;   // Property path (e.g., "position.x")
    nlohmann::json value;       // Override value
};

struct PrefabInstanceComponent {
    std::string prefabPath;                     // Path to source prefab
    std::vector<PrefabOverride> overrides;      // Local modifications
    bool unpackPending = false;                 // Queued for unpacking

    // Get override value for property (returns null if no override)
    nlohmann::json* GetOverride(int entityLocalId, const std::string& componentType,
                                  const std::string& propertyPath);

    // Set override
    void SetOverride(int entityLocalId, const std::string& componentType,
                      const std::string& propertyPath, const nlohmann::json& value);

    // Remove override (revert to prefab value)
    void RemoveOverride(int entityLocalId, const std::string& componentType,
                         const std::string& propertyPath);

    // Check if has any overrides
    bool HasOverrides() const { return !overrides.empty(); }

    // Clear all overrides
    void ClearOverrides() { overrides.clear(); }
};

// ============================================================================
// Prefab System
// ============================================================================

class PrefabSystem {
public:
    PrefabSystem();
    ~PrefabSystem();

    // Initialize with registry
    void Initialize(entt::registry* registry);
    void Shutdown();

    // Prefab library
    void SetPrefabRoot(const std::string& path) { m_prefabRoot = path; }
    const std::string& GetPrefabRoot() const { return m_prefabRoot; }

    // Load/Unload prefabs
    Prefab* LoadPrefab(const std::string& path);
    void UnloadPrefab(const std::string& path);
    Prefab* GetPrefab(const std::string& path);
    bool IsPrefabLoaded(const std::string& path) const;

    // Create prefab from selection
    std::unique_ptr<Prefab> CreateFromEntity(Entity rootEntity, bool includeChildren = true);
    std::unique_ptr<Prefab> CreateFromSelection(const std::vector<Entity>& entities);

    // Save prefab
    bool SavePrefab(Prefab* prefab, const std::string& path);

    // Instantiation
    Entity Instantiate(const std::string& prefabPath,
                        const glm::vec3& position = glm::vec3(0),
                        const glm::quat& rotation = glm::quat(1, 0, 0, 0),
                        Entity parent = 0);

    Entity Instantiate(Prefab* prefab,
                        const glm::vec3& position = glm::vec3(0),
                        const glm::quat& rotation = glm::quat(1, 0, 0, 0),
                        Entity parent = 0);

    // Prefab instance management
    bool IsPrefabInstance(Entity entity) const;
    std::string GetPrefabPath(Entity entity) const;
    Prefab* GetSourcePrefab(Entity entity);

    // Update instance from prefab (apply prefab changes)
    void UpdateInstance(Entity entity);
    void UpdateAllInstances(const std::string& prefabPath);

    // Apply instance changes back to prefab
    void ApplyOverridesToPrefab(Entity entity);

    // Unpack prefab (convert to regular entities, losing prefab link)
    void UnpackPrefab(Entity entity, bool completely = false);

    // Revert instance to prefab state
    void RevertInstance(Entity entity);
    void RevertProperty(Entity entity, int localId, const std::string& componentType,
                         const std::string& propertyPath);

    // Query all instances of a prefab
    std::vector<Entity> GetAllInstances(const std::string& prefabPath) const;

    // Prefab browser
    std::vector<std::string> GetAllPrefabPaths() const;
    void RefreshPrefabLibrary();

    // Component serialization registry
    using SerializeFunc = std::function<nlohmann::json(entt::registry&, Entity)>;
    using DeserializeFunc = std::function<void(entt::registry&, Entity, const nlohmann::json&)>;

    void RegisterComponentSerializer(const std::string& typeName,
                                       SerializeFunc serialize,
                                       DeserializeFunc deserialize);

    // Callbacks
    using InstanceCallback = std::function<void(Entity)>;
    void SetOnPrefabInstantiated(InstanceCallback callback) { m_onInstantiated = callback; }
    void SetOnPrefabUpdated(InstanceCallback callback) { m_onUpdated = callback; }

private:
    // Internal instantiation
    Entity InstantiateEntity(const EntityTemplate& templ, Entity parent,
                              const glm::vec3& positionOffset,
                              const glm::quat& rotationOffset);

    // Serialize entity to template
    EntityTemplate SerializeEntity(Entity entity);

    // Apply component data to entity
    void ApplyComponentData(Entity entity, const ComponentData& data);

    // Get component data from entity
    ComponentData GetComponentData(Entity entity, const std::string& typeName);

    entt::registry* m_registry = nullptr;
    std::string m_prefabRoot;

    // Loaded prefabs
    std::unordered_map<std::string, std::unique_ptr<Prefab>> m_loadedPrefabs;

    // Component serializers
    struct ComponentSerializer {
        SerializeFunc serialize;
        DeserializeFunc deserialize;
    };
    std::unordered_map<std::string, ComponentSerializer> m_componentSerializers;

    // Callbacks
    InstanceCallback m_onInstantiated;
    InstanceCallback m_onUpdated;

    // Prefab library cache
    std::vector<std::string> m_prefabLibrary;
    bool m_libraryDirty = true;
};

// ============================================================================
// Prefab Utilities
// ============================================================================

namespace PrefabUtils {

// Generate unique prefab path
std::string GenerateUniquePath(const std::string& basePath, const std::string& baseName);

// Validate prefab (check for issues)
struct ValidationResult {
    bool valid = true;
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};
ValidationResult ValidatePrefab(const Prefab& prefab);

// Compare two prefabs for differences
struct PrefabDiff {
    std::vector<size_t> addedEntities;
    std::vector<size_t> removedEntities;
    std::vector<std::pair<size_t, std::string>> modifiedComponents;
};
PrefabDiff ComparePrefabs(const Prefab& a, const Prefab& b);

// Merge prefab changes
bool MergePrefabs(Prefab& target, const Prefab& source, const PrefabDiff& diff);

} // namespace PrefabUtils

// ============================================================================
// Nested Prefab Support
// ============================================================================

class NestedPrefabResolver {
public:
    NestedPrefabResolver(PrefabSystem* system);

    // Resolve nested prefabs during instantiation
    void ResolveNested(Prefab* prefab);

    // Detect circular references
    bool HasCircularReference(const std::string& prefabPath) const;

    // Get dependency list
    std::vector<std::string> GetDependencies(const std::string& prefabPath) const;

private:
    PrefabSystem* m_system;
    std::unordered_map<std::string, std::vector<std::string>> m_dependencies;

    void BuildDependencyGraph(const std::string& prefabPath,
                               std::unordered_set<std::string>& visited);
};

} // namespace Cortex::Editor
