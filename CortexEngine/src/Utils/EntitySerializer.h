#pragma once

// EntitySerializer.h
// Entity and component serialization to/from JSON.
// Supports hierarchies, resource references, and version migration.

#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Forward declare ECS types
namespace Cortex {
    class ECS_Registry;
    using Entity = uint32_t;
}

namespace Cortex::Serialization {

// Current serialization version
constexpr uint32_t SERIALIZATION_VERSION = 1;

// Entity reference (for hierarchy and entity references in components)
struct EntityRef {
    uint32_t localId = UINT32_MAX;      // ID within save file
    std::string name;                    // Optional name for lookup
    Entity resolvedEntity = UINT32_MAX;  // Resolved runtime entity

    bool IsValid() const { return localId != UINT32_MAX; }
};

// Resource reference (for meshes, textures, etc.)
struct ResourceRef {
    std::string path;
    std::string type;       // "mesh", "texture", "material", "audio", etc.
    uint32_t flags = 0;

    bool IsValid() const { return !path.empty(); }
};

// Serialization context
struct SerializationContext {
    std::string basePath;               // Base path for relative resources
    uint32_t version = SERIALIZATION_VERSION;
    bool includeDefaults = false;       // Include values even if default
    bool prettyPrint = true;

    // Entity ID mapping (runtime ID -> local save ID)
    std::unordered_map<Entity, uint32_t> entityToLocalId;
    uint32_t nextLocalId = 0;

    uint32_t GetOrCreateLocalId(Entity entity) {
        auto it = entityToLocalId.find(entity);
        if (it != entityToLocalId.end()) {
            return it->second;
        }
        uint32_t localId = nextLocalId++;
        entityToLocalId[entity] = localId;
        return localId;
    }
};

// Deserialization context
struct DeserializationContext {
    std::string basePath;
    uint32_t version = SERIALIZATION_VERSION;

    // Local ID mapping (local save ID -> runtime entity)
    std::unordered_map<uint32_t, Entity> localIdToEntity;
    std::vector<std::pair<Entity, uint32_t>> pendingEntityRefs;  // Entity, target local ID

    void RegisterEntity(uint32_t localId, Entity entity) {
        localIdToEntity[localId] = entity;
    }

    Entity ResolveLocalId(uint32_t localId) const {
        auto it = localIdToEntity.find(localId);
        return (it != localIdToEntity.end()) ? it->second : UINT32_MAX;
    }
};

// Component serialization interface
class IComponentSerializer {
public:
    virtual ~IComponentSerializer() = default;

    // Component type name (for JSON key)
    virtual const char* GetTypeName() const = 0;

    // Check if entity has this component
    virtual bool HasComponent(ECS_Registry* registry, Entity entity) const = 0;

    // Serialize component to JSON
    virtual nlohmann::json Serialize(ECS_Registry* registry, Entity entity,
                                      SerializationContext& ctx) const = 0;

    // Deserialize component from JSON
    virtual void Deserialize(ECS_Registry* registry, Entity entity,
                              const nlohmann::json& data,
                              DeserializationContext& ctx) const = 0;
};

// Template for automatic component serialization
template<typename T>
class ComponentSerializer : public IComponentSerializer {
public:
    explicit ComponentSerializer(const char* typeName) : m_typeName(typeName) {}

    const char* GetTypeName() const override { return m_typeName; }

    bool HasComponent(ECS_Registry* registry, Entity entity) const override;
    nlohmann::json Serialize(ECS_Registry* registry, Entity entity,
                             SerializationContext& ctx) const override;
    void Deserialize(ECS_Registry* registry, Entity entity,
                     const nlohmann::json& data,
                     DeserializationContext& ctx) const override;

private:
    const char* m_typeName;
};

// Entity serializer
class EntitySerializer {
public:
    EntitySerializer();
    ~EntitySerializer() = default;

    // Register component serializers
    template<typename T>
    void RegisterComponent(const char* typeName) {
        m_componentSerializers.push_back(std::make_unique<ComponentSerializer<T>>(typeName));
    }

    void RegisterComponent(std::unique_ptr<IComponentSerializer> serializer);

    // Serialize single entity
    nlohmann::json SerializeEntity(ECS_Registry* registry, Entity entity,
                                    SerializationContext& ctx) const;

    // Deserialize single entity
    Entity DeserializeEntity(ECS_Registry* registry, const nlohmann::json& data,
                              DeserializationContext& ctx) const;

    // Serialize entity hierarchy (entity + all children)
    nlohmann::json SerializeHierarchy(ECS_Registry* registry, Entity root,
                                       SerializationContext& ctx) const;

    // Deserialize entity hierarchy
    Entity DeserializeHierarchy(ECS_Registry* registry, const nlohmann::json& data,
                                 DeserializationContext& ctx) const;

    // Serialize entire scene
    nlohmann::json SerializeScene(ECS_Registry* registry, SerializationContext& ctx) const;

    // Deserialize entire scene
    void DeserializeScene(ECS_Registry* registry, const nlohmann::json& data,
                           DeserializationContext& ctx) const;

    // File operations
    bool SaveToFile(const std::string& path, ECS_Registry* registry,
                    SerializationContext& ctx) const;
    bool LoadFromFile(const std::string& path, ECS_Registry* registry,
                      DeserializationContext& ctx) const;

    // Resolve entity references after loading
    void ResolveEntityReferences(ECS_Registry* registry, DeserializationContext& ctx) const;

private:
    std::vector<std::unique_ptr<IComponentSerializer>> m_componentSerializers;

    // Get children entities (requires HierarchyComponent or similar)
    std::vector<Entity> GetChildren(ECS_Registry* registry, Entity parent) const;
};

// JSON conversion helpers for GLM types
void to_json(nlohmann::json& j, const glm::vec2& v);
void from_json(const nlohmann::json& j, glm::vec2& v);

void to_json(nlohmann::json& j, const glm::vec3& v);
void from_json(const nlohmann::json& j, glm::vec3& v);

void to_json(nlohmann::json& j, const glm::vec4& v);
void from_json(const nlohmann::json& j, glm::vec4& v);

void to_json(nlohmann::json& j, const glm::quat& q);
void from_json(const nlohmann::json& j, glm::quat& q);

void to_json(nlohmann::json& j, const glm::mat4& m);
void from_json(const nlohmann::json& j, glm::mat4& m);

// JSON conversion helpers for engine types
void to_json(nlohmann::json& j, const EntityRef& ref);
void from_json(const nlohmann::json& j, EntityRef& ref);

void to_json(nlohmann::json& j, const ResourceRef& ref);
void from_json(const nlohmann::json& j, ResourceRef& ref);

// Utility functions
namespace SerializationUtils {

// Generate unique name for unnamed entity
std::string GenerateEntityName(Entity entity);

// Validate JSON structure
bool ValidateSceneJson(const nlohmann::json& data, std::string& errorMsg);

// Calculate checksum for save integrity
uint32_t CalculateChecksum(const nlohmann::json& data);

// Compress JSON data
std::vector<uint8_t> CompressJson(const nlohmann::json& data);
nlohmann::json DecompressJson(const std::vector<uint8_t>& compressed);

} // namespace SerializationUtils

} // namespace Cortex::Serialization
