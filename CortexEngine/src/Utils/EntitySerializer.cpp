// EntitySerializer.cpp
// Implementation of entity and component serialization.

#include "EntitySerializer.h"
#include "../Scene/ECS_Registry.h"
#include "../Scene/Components.h"
#include <fstream>
#include <sstream>
#include <iomanip>

namespace Cortex::Serialization {

// ============================================================================
// GLM JSON Conversion
// ============================================================================

void to_json(nlohmann::json& j, const glm::vec2& v) {
    j = nlohmann::json::array({v.x, v.y});
}

void from_json(const nlohmann::json& j, glm::vec2& v) {
    if (j.is_array() && j.size() >= 2) {
        v.x = j[0].get<float>();
        v.y = j[1].get<float>();
    }
}

void to_json(nlohmann::json& j, const glm::vec3& v) {
    j = nlohmann::json::array({v.x, v.y, v.z});
}

void from_json(const nlohmann::json& j, glm::vec3& v) {
    if (j.is_array() && j.size() >= 3) {
        v.x = j[0].get<float>();
        v.y = j[1].get<float>();
        v.z = j[2].get<float>();
    }
}

void to_json(nlohmann::json& j, const glm::vec4& v) {
    j = nlohmann::json::array({v.x, v.y, v.z, v.w});
}

void from_json(const nlohmann::json& j, glm::vec4& v) {
    if (j.is_array() && j.size() >= 4) {
        v.x = j[0].get<float>();
        v.y = j[1].get<float>();
        v.z = j[2].get<float>();
        v.w = j[3].get<float>();
    }
}

void to_json(nlohmann::json& j, const glm::quat& q) {
    j = nlohmann::json::array({q.w, q.x, q.y, q.z});
}

void from_json(const nlohmann::json& j, glm::quat& q) {
    if (j.is_array() && j.size() >= 4) {
        q.w = j[0].get<float>();
        q.x = j[1].get<float>();
        q.y = j[2].get<float>();
        q.z = j[3].get<float>();
    }
}

void to_json(nlohmann::json& j, const glm::mat4& m) {
    j = nlohmann::json::array();
    for (int col = 0; col < 4; col++) {
        for (int row = 0; row < 4; row++) {
            j.push_back(m[col][row]);
        }
    }
}

void from_json(const nlohmann::json& j, glm::mat4& m) {
    if (j.is_array() && j.size() >= 16) {
        int idx = 0;
        for (int col = 0; col < 4; col++) {
            for (int row = 0; row < 4; row++) {
                m[col][row] = j[idx++].get<float>();
            }
        }
    }
}

void to_json(nlohmann::json& j, const EntityRef& ref) {
    j = nlohmann::json{
        {"localId", ref.localId},
        {"name", ref.name}
    };
}

void from_json(const nlohmann::json& j, EntityRef& ref) {
    if (j.contains("localId")) ref.localId = j["localId"].get<uint32_t>();
    if (j.contains("name")) ref.name = j["name"].get<std::string>();
}

void to_json(nlohmann::json& j, const ResourceRef& ref) {
    j = nlohmann::json{
        {"path", ref.path},
        {"type", ref.type},
        {"flags", ref.flags}
    };
}

void from_json(const nlohmann::json& j, ResourceRef& ref) {
    if (j.contains("path")) ref.path = j["path"].get<std::string>();
    if (j.contains("type")) ref.type = j["type"].get<std::string>();
    if (j.contains("flags")) ref.flags = j["flags"].get<uint32_t>();
}

// ============================================================================
// ComponentSerializer Template Specializations
// ============================================================================

template<>
bool ComponentSerializer<TransformComponent>::HasComponent(ECS_Registry* registry, Entity entity) const {
    return registry->HasComponent<TransformComponent>(entity);
}

template<>
nlohmann::json ComponentSerializer<TransformComponent>::Serialize(
    ECS_Registry* registry, Entity entity, SerializationContext& ctx) const {

    auto& t = registry->GetComponent<TransformComponent>(entity);
    return nlohmann::json{
        {"position", t.Position},
        {"rotation", t.Rotation},
        {"scale", t.Scale}
    };
}

template<>
void ComponentSerializer<TransformComponent>::Deserialize(
    ECS_Registry* registry, Entity entity,
    const nlohmann::json& data, DeserializationContext& ctx) const {

    auto& t = registry->GetOrAddComponent<TransformComponent>(entity);
    if (data.contains("position")) data["position"].get_to(t.Position);
    if (data.contains("rotation")) data["rotation"].get_to(t.Rotation);
    if (data.contains("scale")) data["scale"].get_to(t.Scale);
}

template<>
bool ComponentSerializer<NameComponent>::HasComponent(ECS_Registry* registry, Entity entity) const {
    return registry->HasComponent<NameComponent>(entity);
}

template<>
nlohmann::json ComponentSerializer<NameComponent>::Serialize(
    ECS_Registry* registry, Entity entity, SerializationContext& ctx) const {

    auto& n = registry->GetComponent<NameComponent>(entity);
    return nlohmann::json{
        {"name", n.Name},
        {"tag", n.Tag}
    };
}

template<>
void ComponentSerializer<NameComponent>::Deserialize(
    ECS_Registry* registry, Entity entity,
    const nlohmann::json& data, DeserializationContext& ctx) const {

    auto& n = registry->GetOrAddComponent<NameComponent>(entity);
    if (data.contains("name")) n.Name = data["name"].get<std::string>();
    if (data.contains("tag")) n.Tag = data["tag"].get<std::string>();
}

template<>
bool ComponentSerializer<MeshRendererComponent>::HasComponent(ECS_Registry* registry, Entity entity) const {
    return registry->HasComponent<MeshRendererComponent>(entity);
}

template<>
nlohmann::json ComponentSerializer<MeshRendererComponent>::Serialize(
    ECS_Registry* registry, Entity entity, SerializationContext& ctx) const {

    auto& m = registry->GetComponent<MeshRendererComponent>(entity);
    nlohmann::json j;
    j["meshIndex"] = m.MeshIndex;
    j["materialIndex"] = m.MaterialIndex;
    j["castShadows"] = m.CastShadows;
    j["receiveShadows"] = m.ReceiveShadows;
    j["visible"] = m.Visible;
    return j;
}

template<>
void ComponentSerializer<MeshRendererComponent>::Deserialize(
    ECS_Registry* registry, Entity entity,
    const nlohmann::json& data, DeserializationContext& ctx) const {

    auto& m = registry->GetOrAddComponent<MeshRendererComponent>(entity);
    if (data.contains("meshIndex")) m.MeshIndex = data["meshIndex"].get<uint32_t>();
    if (data.contains("materialIndex")) m.MaterialIndex = data["materialIndex"].get<uint32_t>();
    if (data.contains("castShadows")) m.CastShadows = data["castShadows"].get<bool>();
    if (data.contains("receiveShadows")) m.ReceiveShadows = data["receiveShadows"].get<bool>();
    if (data.contains("visible")) m.Visible = data["visible"].get<bool>();
}

template<>
bool ComponentSerializer<LightComponent>::HasComponent(ECS_Registry* registry, Entity entity) const {
    return registry->HasComponent<LightComponent>(entity);
}

template<>
nlohmann::json ComponentSerializer<LightComponent>::Serialize(
    ECS_Registry* registry, Entity entity, SerializationContext& ctx) const {

    auto& l = registry->GetComponent<LightComponent>(entity);
    return nlohmann::json{
        {"type", static_cast<int>(l.Type)},
        {"color", l.Color},
        {"intensity", l.Intensity},
        {"range", l.Range},
        {"innerAngle", l.InnerAngle},
        {"outerAngle", l.OuterAngle},
        {"castShadows", l.CastShadows}
    };
}

template<>
void ComponentSerializer<LightComponent>::Deserialize(
    ECS_Registry* registry, Entity entity,
    const nlohmann::json& data, DeserializationContext& ctx) const {

    auto& l = registry->GetOrAddComponent<LightComponent>(entity);
    if (data.contains("type")) l.Type = static_cast<LightType>(data["type"].get<int>());
    if (data.contains("color")) data["color"].get_to(l.Color);
    if (data.contains("intensity")) l.Intensity = data["intensity"].get<float>();
    if (data.contains("range")) l.Range = data["range"].get<float>();
    if (data.contains("innerAngle")) l.InnerAngle = data["innerAngle"].get<float>();
    if (data.contains("outerAngle")) l.OuterAngle = data["outerAngle"].get<float>();
    if (data.contains("castShadows")) l.CastShadows = data["castShadows"].get<bool>();
}

template<>
bool ComponentSerializer<CameraComponent>::HasComponent(ECS_Registry* registry, Entity entity) const {
    return registry->HasComponent<CameraComponent>(entity);
}

template<>
nlohmann::json ComponentSerializer<CameraComponent>::Serialize(
    ECS_Registry* registry, Entity entity, SerializationContext& ctx) const {

    auto& c = registry->GetComponent<CameraComponent>(entity);
    return nlohmann::json{
        {"fov", c.FOV},
        {"nearPlane", c.NearPlane},
        {"farPlane", c.FarPlane},
        {"isPrimary", c.IsPrimary},
        {"isOrthographic", c.IsOrthographic},
        {"orthoSize", c.OrthoSize}
    };
}

template<>
void ComponentSerializer<CameraComponent>::Deserialize(
    ECS_Registry* registry, Entity entity,
    const nlohmann::json& data, DeserializationContext& ctx) const {

    auto& c = registry->GetOrAddComponent<CameraComponent>(entity);
    if (data.contains("fov")) c.FOV = data["fov"].get<float>();
    if (data.contains("nearPlane")) c.NearPlane = data["nearPlane"].get<float>();
    if (data.contains("farPlane")) c.FarPlane = data["farPlane"].get<float>();
    if (data.contains("isPrimary")) c.IsPrimary = data["isPrimary"].get<bool>();
    if (data.contains("isOrthographic")) c.IsOrthographic = data["isOrthographic"].get<bool>();
    if (data.contains("orthoSize")) c.OrthoSize = data["orthoSize"].get<float>();
}

template<>
bool ComponentSerializer<RigidBodyComponent>::HasComponent(ECS_Registry* registry, Entity entity) const {
    return registry->HasComponent<RigidBodyComponent>(entity);
}

template<>
nlohmann::json ComponentSerializer<RigidBodyComponent>::Serialize(
    ECS_Registry* registry, Entity entity, SerializationContext& ctx) const {

    auto& r = registry->GetComponent<RigidBodyComponent>(entity);
    return nlohmann::json{
        {"type", static_cast<int>(r.Type)},
        {"mass", r.Mass},
        {"linearDamping", r.LinearDamping},
        {"angularDamping", r.AngularDamping},
        {"useGravity", r.UseGravity},
        {"isKinematic", r.IsKinematic},
        {"freezePositionX", r.FreezePositionX},
        {"freezePositionY", r.FreezePositionY},
        {"freezePositionZ", r.FreezePositionZ},
        {"freezeRotationX", r.FreezeRotationX},
        {"freezeRotationY", r.FreezeRotationY},
        {"freezeRotationZ", r.FreezeRotationZ}
    };
}

template<>
void ComponentSerializer<RigidBodyComponent>::Deserialize(
    ECS_Registry* registry, Entity entity,
    const nlohmann::json& data, DeserializationContext& ctx) const {

    auto& r = registry->GetOrAddComponent<RigidBodyComponent>(entity);
    if (data.contains("type")) r.Type = static_cast<RigidBodyType>(data["type"].get<int>());
    if (data.contains("mass")) r.Mass = data["mass"].get<float>();
    if (data.contains("linearDamping")) r.LinearDamping = data["linearDamping"].get<float>();
    if (data.contains("angularDamping")) r.AngularDamping = data["angularDamping"].get<float>();
    if (data.contains("useGravity")) r.UseGravity = data["useGravity"].get<bool>();
    if (data.contains("isKinematic")) r.IsKinematic = data["isKinematic"].get<bool>();
    if (data.contains("freezePositionX")) r.FreezePositionX = data["freezePositionX"].get<bool>();
    if (data.contains("freezePositionY")) r.FreezePositionY = data["freezePositionY"].get<bool>();
    if (data.contains("freezePositionZ")) r.FreezePositionZ = data["freezePositionZ"].get<bool>();
    if (data.contains("freezeRotationX")) r.FreezeRotationX = data["freezeRotationX"].get<bool>();
    if (data.contains("freezeRotationY")) r.FreezeRotationY = data["freezeRotationY"].get<bool>();
    if (data.contains("freezeRotationZ")) r.FreezeRotationZ = data["freezeRotationZ"].get<bool>();
}

// ============================================================================
// EntitySerializer
// ============================================================================

EntitySerializer::EntitySerializer() {
    // Register built-in component serializers
    RegisterComponent<TransformComponent>("Transform");
    RegisterComponent<NameComponent>("Name");
    RegisterComponent<MeshRendererComponent>("MeshRenderer");
    RegisterComponent<LightComponent>("Light");
    RegisterComponent<CameraComponent>("Camera");
    RegisterComponent<RigidBodyComponent>("RigidBody");
}

void EntitySerializer::RegisterComponent(std::unique_ptr<IComponentSerializer> serializer) {
    m_componentSerializers.push_back(std::move(serializer));
}

nlohmann::json EntitySerializer::SerializeEntity(ECS_Registry* registry, Entity entity,
                                                   SerializationContext& ctx) const {
    nlohmann::json entityJson;

    // Assign local ID
    uint32_t localId = ctx.GetOrCreateLocalId(entity);
    entityJson["id"] = localId;

    // Get name if available
    if (registry->HasComponent<NameComponent>(entity)) {
        entityJson["name"] = registry->GetComponent<NameComponent>(entity).Name;
    }

    // Serialize all components
    nlohmann::json components = nlohmann::json::object();
    for (const auto& serializer : m_componentSerializers) {
        if (serializer->HasComponent(registry, entity)) {
            components[serializer->GetTypeName()] = serializer->Serialize(registry, entity, ctx);
        }
    }
    entityJson["components"] = components;

    return entityJson;
}

Entity EntitySerializer::DeserializeEntity(ECS_Registry* registry, const nlohmann::json& data,
                                            DeserializationContext& ctx) const {
    Entity entity = registry->CreateEntity();

    // Get local ID and register mapping
    if (data.contains("id")) {
        uint32_t localId = data["id"].get<uint32_t>();
        ctx.RegisterEntity(localId, entity);
    }

    // Deserialize components
    if (data.contains("components")) {
        const auto& components = data["components"];
        for (const auto& serializer : m_componentSerializers) {
            if (components.contains(serializer->GetTypeName())) {
                serializer->Deserialize(registry, entity,
                                        components[serializer->GetTypeName()], ctx);
            }
        }
    }

    return entity;
}

nlohmann::json EntitySerializer::SerializeHierarchy(ECS_Registry* registry, Entity root,
                                                      SerializationContext& ctx) const {
    nlohmann::json hierarchyJson = SerializeEntity(registry, root, ctx);

    // Serialize children
    std::vector<Entity> children = GetChildren(registry, root);
    if (!children.empty()) {
        nlohmann::json childrenJson = nlohmann::json::array();
        for (Entity child : children) {
            childrenJson.push_back(SerializeHierarchy(registry, child, ctx));
        }
        hierarchyJson["children"] = childrenJson;
    }

    return hierarchyJson;
}

Entity EntitySerializer::DeserializeHierarchy(ECS_Registry* registry, const nlohmann::json& data,
                                                DeserializationContext& ctx) const {
    Entity entity = DeserializeEntity(registry, data, ctx);

    // Deserialize children
    if (data.contains("children")) {
        for (const auto& childData : data["children"]) {
            Entity child = DeserializeHierarchy(registry, childData, ctx);
            // TODO: Set parent-child relationship when HierarchyComponent is available
            (void)child;
        }
    }

    return entity;
}

nlohmann::json EntitySerializer::SerializeScene(ECS_Registry* registry,
                                                  SerializationContext& ctx) const {
    nlohmann::json sceneJson;

    sceneJson["version"] = ctx.version;
    sceneJson["name"] = "Scene";

    // Get all root entities (entities without parents)
    nlohmann::json entitiesJson = nlohmann::json::array();

    // For now, serialize all entities (hierarchy not implemented yet)
    auto view = registry->GetAllEntities();
    for (Entity entity : view) {
        entitiesJson.push_back(SerializeEntity(registry, entity, ctx));
    }

    sceneJson["entities"] = entitiesJson;

    // Calculate checksum
    sceneJson["checksum"] = SerializationUtils::CalculateChecksum(sceneJson);

    return sceneJson;
}

void EntitySerializer::DeserializeScene(ECS_Registry* registry, const nlohmann::json& data,
                                          DeserializationContext& ctx) const {
    // Validate version
    if (data.contains("version")) {
        ctx.version = data["version"].get<uint32_t>();
    }

    // Deserialize entities
    if (data.contains("entities")) {
        for (const auto& entityData : data["entities"]) {
            DeserializeEntity(registry, entityData, ctx);
        }
    }

    // Resolve entity references
    ResolveEntityReferences(registry, ctx);
}

bool EntitySerializer::SaveToFile(const std::string& path, ECS_Registry* registry,
                                    SerializationContext& ctx) const {
    try {
        nlohmann::json sceneJson = SerializeScene(registry, ctx);

        std::ofstream file(path);
        if (!file.is_open()) {
            return false;
        }

        if (ctx.prettyPrint) {
            file << std::setw(2) << sceneJson;
        } else {
            file << sceneJson;
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool EntitySerializer::LoadFromFile(const std::string& path, ECS_Registry* registry,
                                      DeserializationContext& ctx) const {
    try {
        std::ifstream file(path);
        if (!file.is_open()) {
            return false;
        }

        nlohmann::json sceneJson;
        file >> sceneJson;

        // Validate checksum
        if (sceneJson.contains("checksum")) {
            uint32_t storedChecksum = sceneJson["checksum"].get<uint32_t>();
            sceneJson.erase("checksum");
            uint32_t calculatedChecksum = SerializationUtils::CalculateChecksum(sceneJson);
            if (storedChecksum != calculatedChecksum) {
                // Checksum mismatch - file may be corrupted
                return false;
            }
        }

        DeserializeScene(registry, sceneJson, ctx);
        return true;
    } catch (...) {
        return false;
    }
}

void EntitySerializer::ResolveEntityReferences(ECS_Registry* registry,
                                                 DeserializationContext& ctx) const {
    for (const auto& [entity, targetLocalId] : ctx.pendingEntityRefs) {
        Entity resolved = ctx.ResolveLocalId(targetLocalId);
        // TODO: Update component with resolved entity reference
        (void)entity;
        (void)resolved;
    }
}

std::vector<Entity> EntitySerializer::GetChildren(ECS_Registry* registry, Entity parent) const {
    // TODO: Implement when HierarchyComponent is available
    (void)registry;
    (void)parent;
    return {};
}

// ============================================================================
// SerializationUtils
// ============================================================================

namespace SerializationUtils {

std::string GenerateEntityName(Entity entity) {
    std::stringstream ss;
    ss << "Entity_" << entity;
    return ss.str();
}

bool ValidateSceneJson(const nlohmann::json& data, std::string& errorMsg) {
    if (!data.contains("version")) {
        errorMsg = "Missing 'version' field";
        return false;
    }
    if (!data.contains("entities")) {
        errorMsg = "Missing 'entities' field";
        return false;
    }
    if (!data["entities"].is_array()) {
        errorMsg = "'entities' must be an array";
        return false;
    }
    return true;
}

uint32_t CalculateChecksum(const nlohmann::json& data) {
    // Simple FNV-1a hash of the JSON string
    std::string jsonStr = data.dump();
    uint32_t hash = 2166136261u;
    for (char c : jsonStr) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 16777619u;
    }
    return hash;
}

std::vector<uint8_t> CompressJson(const nlohmann::json& data) {
    // Simple RLE compression (placeholder - use zlib in production)
    std::string jsonStr = data.dump();
    std::vector<uint8_t> result(jsonStr.begin(), jsonStr.end());
    return result;
}

nlohmann::json DecompressJson(const std::vector<uint8_t>& compressed) {
    std::string jsonStr(compressed.begin(), compressed.end());
    return nlohmann::json::parse(jsonStr);
}

} // namespace SerializationUtils

} // namespace Cortex::Serialization
