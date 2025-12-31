#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include <entt/entt.hpp>
#include "Graphics/MeshBuffers.h"  // For DeferMeshBuffersDeletion

namespace Cortex::Graphics {
    class DX12Texture;
    struct MaterialGPUState;
}

namespace Cortex::Scene {

// Transform Component - Local transform + simple hierarchy
struct TransformComponent {
    // Local transform relative to parent (or world if parent == null)
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);  // Identity quaternion
    glm::vec3 scale    = glm::vec3(1.0f);

    // Optional parent in the transform hierarchy. When entt::null, this
    // transform is treated as a root.
    entt::entity parent = entt::null;

    // Cached world transform and normal matrix, updated by ECS_Registry.
    glm::mat4 worldMatrix       = glm::mat4(1.0f);
    glm::mat4 normalMatrix      = glm::mat4(1.0f);
    glm::mat4 inverseWorldMatrix = glm::mat4(1.0f);

    // Local transformation matrix (no parent applied)
    [[nodiscard]] glm::mat4 GetLocalMatrix() const;

    // World transformation matrix (after hierarchy update)
    [[nodiscard]] glm::mat4 GetMatrix() const;

    // World-space normal matrix (for lighting)
    [[nodiscard]] glm::mat4 GetNormalMatrix() const;
};

// Tag Component - Semantic labels for AI context (Phase 4)
struct TagComponent {
    std::string tag;

    TagComponent() = default;
    explicit TagComponent(std::string t) : tag(std::move(t)) {}
};

// Mesh types used for high-level classification. StaticTriangle is the common
// case; Skinned and Procedural are reserved for future animation and
// on-the-fly generation passes.
enum class MeshKind : uint32_t {
    StaticTriangle = 0,
    Skinned        = 1,
    Procedural     = 2
};

// Mesh data
struct MeshData {
    MeshKind kind = MeshKind::StaticTriangle;
    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> texCoords;
    std::vector<uint32_t> indices;
    // Vertex colors - used for biome splatmap data on terrain
    // RGBA channels encode: R=biome0 index, G=biome1 index, B=blend weight, A=flags
    std::vector<glm::vec4> colors;
    // Simple bounding volume used for culling and RT acceleration structure
    // budgeting. Bounds are computed in object space and updated by mesh
    // generators / loaders once vertex positions are populated.
    // NOTE: boundsMin/Max are used to detect thin plate geometry (e.g., planes)
    // for automatic depth separation to reduce coplanar z-fighting.
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    glm::vec3 boundsCenter{0.0f};
    float     boundsRadius = 0.0f;
    bool      hasBounds = false;

    // GPU buffer handles (renderer-owned)
    std::shared_ptr<Cortex::Graphics::MeshBuffers> gpuBuffers;

    MeshData() = default;

    // Destructor uses deferred deletion to prevent D3D12 validation errors.
    // When MeshData is destroyed (e.g., when an entity is deleted), the GPU
    // buffers are queued for deletion after N frames to ensure they are no
    // longer referenced by any in-flight command lists.
    ~MeshData() {
        if (gpuBuffers) {
            Cortex::Graphics::DeferMeshBuffersDeletion(gpuBuffers);
        }
    }

    // Move constructor and assignment
    MeshData(MeshData&& other) noexcept = default;
    MeshData& operator=(MeshData&& other) noexcept {
        if (this != &other) {
            // Defer deletion of current buffers before taking new ones
            if (gpuBuffers) {
                Cortex::Graphics::DeferMeshBuffersDeletion(gpuBuffers);
            }
            kind = other.kind;
            positions = std::move(other.positions);
            normals = std::move(other.normals);
            texCoords = std::move(other.texCoords);
            indices = std::move(other.indices);
            colors = std::move(other.colors);
            boundsMin = other.boundsMin;
            boundsMax = other.boundsMax;
            boundsCenter = other.boundsCenter;
            boundsRadius = other.boundsRadius;
            hasBounds = other.hasBounds;
            gpuBuffers = std::move(other.gpuBuffers);
        }
        return *this;
    }

    // Copy is deleted (GPU buffers should not be duplicated)
    MeshData(const MeshData&) = delete;
    MeshData& operator=(const MeshData&) = delete;

    // Reset GPU resources using deferred deletion.
    // This queues the buffers for deletion after N frames to ensure the GPU
    // is no longer referencing them, preventing D3D12 validation errors.
    void ResetGPUResources() {
        Cortex::Graphics::DeferMeshBuffersDeletion(gpuBuffers);
    }

    void UpdateBounds() {
        if (positions.empty()) {
            boundsMin = glm::vec3(0.0f);
            boundsMax = glm::vec3(0.0f);
            boundsCenter = glm::vec3(0.0f);
            boundsRadius = 0.0f;
            hasBounds = false;
            return;
        }

        glm::vec3 minP = positions[0];
        glm::vec3 maxP = positions[0];
        for (const auto& p : positions) {
            minP = glm::min(minP, p);
            maxP = glm::max(maxP, p);
        }
        boundsMin = minP;
        boundsMax = maxP;
        boundsCenter = 0.5f * (minP + maxP);
        boundsRadius = glm::length(maxP - boundsCenter);
        hasBounds = true;
    }
};

// Renderable Component - What to draw
struct RenderableComponent {
    enum class RenderLayer : uint8_t {
        Opaque = 0,
        Overlay = 1, // Decals / markings rendered after opaque depth
    };

    std::shared_ptr<MeshData> mesh;
    struct MaterialTextures {
        std::shared_ptr<Cortex::Graphics::DX12Texture> albedo;
        std::shared_ptr<Cortex::Graphics::DX12Texture> normal;
        std::shared_ptr<Cortex::Graphics::DX12Texture> metallic;
        std::shared_ptr<Cortex::Graphics::DX12Texture> roughness;
        std::shared_ptr<Cortex::Graphics::DX12Texture> occlusion;
        std::shared_ptr<Cortex::Graphics::DX12Texture> emissive;
        // glTF extensions (KHR_materials_*). These are optional and default to null.
        std::shared_ptr<Cortex::Graphics::DX12Texture> transmission;
        std::shared_ptr<Cortex::Graphics::DX12Texture> clearcoat;
        std::shared_ptr<Cortex::Graphics::DX12Texture> clearcoatRoughness;
        std::shared_ptr<Cortex::Graphics::DX12Texture> specular;
        std::shared_ptr<Cortex::Graphics::DX12Texture> specularColor;
        std::string albedoPath;
        std::string normalPath;
        std::string metallicPath;
        std::string roughnessPath;
        std::string occlusionPath;
        std::string emissivePath;
        std::string transmissionPath;
        std::string clearcoatPath;
        std::string clearcoatRoughnessPath;
        std::string specularPath;
        std::string specularColorPath;
        std::shared_ptr<Cortex::Graphics::MaterialGPUState> gpuState;
    } textures;

     // Material properties
     glm::vec4 albedoColor = glm::vec4(1.0f);
     float metallic = 0.0f;
     float roughness = 0.5f;
     float ao = 1.0f;
     glm::vec3 emissiveColor = glm::vec3(0.0f);
     float emissiveStrength = 1.0f;
     float occlusionStrength = 1.0f;
     float normalScale = 1.0f;

     // glTF extensions (KHR_materials_transmission / ior / specular / clearcoat).
     // These are ignored unless a given material chooses to use them.
     float transmissionFactor = 0.0f;          // 0 = opaque, 1 = fully transmissive (thin)
     float ior = 1.5f;                         // index of refraction (>= 1)
     float specularFactor = 1.0f;              // dielectric specular intensity multiplier
     glm::vec3 specularColorFactor = glm::vec3(1.0f); // dielectric specular tint
     float clearcoatFactor = 0.0f;             // additional glossy layer weight
     float clearcoatRoughnessFactor = 0.0f;    // clearcoat roughness (0..1)

     enum class AlphaMode : uint32_t {
         Opaque = 0,
         Mask = 1,
         Blend = 2,
     };
     AlphaMode alphaMode = AlphaMode::Opaque;
     float alphaCutoff = 0.5f; // Used when alphaMode == Mask.
     bool doubleSided = false;
     // Optional logical material preset (e.g. "chrome", "gold") used by LLM commands.
     std::string presetName;

    // Visibility
    bool visible = true;
    RenderLayer renderLayer = RenderLayer::Opaque;
};

// Rotation Component - For spinning cube demo
struct RotationComponent {
    glm::vec3 axis = glm::vec3(0.0f, 1.0f, 0.0f);
    float speed = 1.0f;  // Radians per second
};

// Light types for forward lighting
enum class LightType : uint32_t {
    Directional = 0,
    Point       = 1,
    Spot        = 2,
    AreaRect    = 3
};

// Light Component - Forward lighting sources
struct LightComponent {
    LightType type = LightType::Point;
    glm::vec3 color = glm::vec3(1.0f);
    float intensity = 5.0f;
    float range = 10.0f;          // For point/spot
    float innerConeDegrees = 20.0f; // For spot (ignored for others)
    float outerConeDegrees = 30.0f; // For spot
    bool castsShadows = false;    // Reserved for future shadowed lights
    // Rectangular area lights (softboxes) use the light's transform
    // orientation plus this size in local X/Y as their emitting surface.
    glm::vec2 areaSize = glm::vec2(1.0f, 1.0f);
    bool twoSided = false;
};

// Camera Component
struct CameraComponent {
    float fov = 60.0f;  // Field of view in degrees
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    bool isActive = true;

    // Get projection matrix
    [[nodiscard]] glm::mat4 GetProjectionMatrix(float aspectRatio) const;

    // Get view matrix (requires TransformComponent)
    [[nodiscard]] glm::mat4 GetViewMatrix(const TransformComponent& transform) const;
};

// Reflection probe volume used for local image-based lighting selection in the
// deferred/VB path. The probe defines an axis-aligned box in world space
// (center from TransformComponent, half-extents from this component scaled by
// the world matrix). Blend distance defines a soft transition outside the box.
struct ReflectionProbeComponent {
    glm::vec3 extents = glm::vec3(5.0f);  // half-size in local space
    float blendDistance = 1.0f;           // world-space fade region outside extents
    uint32_t environmentIndex = 0;        // index into Renderer::m_environmentMaps
    uint32_t enabled = 1;                 // 0 = disabled
};

// Marker component for planar water surfaces. Any renderable entity tagged
// with this component is treated as part of the water system (wave
// displacement, water shading, and buoyancy queries).
struct WaterSurfaceComponent {
    // Higher priority surfaces can be preferred when sampling height in
    // scenes with multiple overlapping water bodies in the future.
    float priority = 0.0f;
};

// Simple buoyancy data for objects that should float on water. Vertical
// integration and interaction are handled by a dedicated update step.
struct BuoyancyComponent {
    // Approximate radius used as a contact area scale for buoyant force.
    float radius = 0.5f;
    // Effective density of the object relative to water; values < 1 tend to
    // float higher, > 1 sit lower.
    float density = 1.0f;
    // Linear damping applied to vertical motion to stabilize bobbing.
    float damping = 0.8f;
    // Internal vertical velocity used by the buoyancy integrator.
    float verticalVelocity = 0.0f;
};

// Simple CPU-side particle representation for emitters. Particles are
// simulated in local or world space and rendered via a GPU-instanced
// quad in the renderer.
struct Particle {
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f};
    float     age = 0.0f;
    float     lifetime = 1.0f;
    float     size = 0.1f;
    glm::vec4 color{1.0f};
};

enum class ParticleEmitterType : uint32_t {
    Smoke = 0,
    Fire  = 1
};

struct ParticleEmitterComponent {
    ParticleEmitterType type = ParticleEmitterType::Smoke;
    float rate = 20.0f;           // particles per second
    float lifetime = 3.0f;        // seconds
    glm::vec3 initialVelocity{0.0f, 1.0f, 0.0f};
    glm::vec3 velocityRandom{0.3f, 0.3f, 0.3f};
    float sizeStart = 0.1f;
    float sizeEnd   = 0.5f;
    glm::vec4 colorStart{1.0f};
    glm::vec4 colorEnd{1.0f, 1.0f, 1.0f, 0.0f};
    float gravity = -0.5f;
    bool  localSpace = false;

    // Internal state
    float emissionAccumulator = 0.0f;
    std::vector<Particle> particles;
};

// Terrain clipmap level component for GPU-displaced terrain rings.
struct TerrainClipmapLevelComponent {
    uint32_t ringIndex = 0;       // 0 = innermost (highest detail)
    float baseScale = 1.0f;       // Base scale for this ring
    bool isRing = false;          // true = ring topology, false = full grid
};

// CPU-generated terrain chunk component for VB-integrated terrain.
struct TerrainChunkComponent {
    int32_t chunkX = 0;           // Grid coordinate X
    int32_t chunkZ = 0;           // Grid coordinate Z
    float chunkSize = 64.0f;      // World-space size of chunk
    uint32_t lodLevel = 0;        // LOD level (0 = highest detail)
};

// Interactable object component for pick-up / activate / examine interactions.
enum class InteractionType : uint32_t {
    Pickup = 0,
    Activate = 1,
    Examine = 2
};

struct InteractableComponent {
    InteractionType type = InteractionType::Pickup;
    glm::vec3 highlightColor = glm::vec3(1.0f, 0.8f, 0.2f);
    float interactionRadius = 2.0f;
    bool isHighlighted = false;
};

// Marks an object as currently held by the player.
struct HeldObjectComponent {
    glm::vec3 holdOffset = glm::vec3(0.0f, -0.2f, 0.5f);
    glm::quat holdRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

// Simple rigid body physics for dropped/thrown objects.
struct PhysicsBodyComponent {
    glm::vec3 velocity = glm::vec3(0.0f);
    glm::vec3 angularVelocity = glm::vec3(0.0f);
    float mass = 1.0f;
    float restitution = 0.3f;
    float friction = 0.5f;
    bool useGravity = true;
    bool isKinematic = false;
};

} // namespace Cortex::Scene
