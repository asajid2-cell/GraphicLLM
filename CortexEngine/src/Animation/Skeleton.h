#pragma once

// Skeleton.h
// Skeletal hierarchy for character animation.
// Supports bone-based skeletal animation with inverse bind poses.
// Reference: "Game Engine Architecture" - Gregory, Chapter 11

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <cstdint>
#include <memory>

namespace Cortex::Animation {

// Maximum bones supported per skeleton
constexpr uint32_t MAX_BONES = 256;

// Maximum bones that can influence a single vertex
constexpr uint32_t MAX_BONES_PER_VERTEX = 4;

// Bone transform in local space
struct BoneTransform {
    glm::vec3 position = glm::vec3(0.0f);
    glm::quat rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::vec3 scale = glm::vec3(1.0f);

    // Default constructor
    BoneTransform() = default;

    // Construct from components
    BoneTransform(const glm::vec3& pos, const glm::quat& rot, const glm::vec3& scl)
        : position(pos), rotation(rot), scale(scl) {}

    // Convert to 4x4 matrix
    [[nodiscard]] glm::mat4 ToMatrix() const;

    // Create from matrix (decompose)
    static BoneTransform FromMatrix(const glm::mat4& matrix);

    // Interpolate between two transforms
    static BoneTransform Lerp(const BoneTransform& a, const BoneTransform& b, float t);

    // Blend multiple transforms with weights
    static BoneTransform Blend(const BoneTransform* transforms, const float* weights, uint32_t count);

    // Additive blend (base + additive * weight)
    static BoneTransform AddBlend(const BoneTransform& base, const BoneTransform& additive, float weight);

    // Identity transform
    static BoneTransform Identity() {
        return BoneTransform(glm::vec3(0.0f), glm::quat(1.0f, 0.0f, 0.0f, 0.0f), glm::vec3(1.0f));
    }
};

// Bone definition in skeleton
struct Bone {
    std::string name;
    int32_t parentIndex = -1;           // -1 = root bone

    // Rest pose (bind pose) in local space
    BoneTransform restPose;

    // Inverse bind pose matrix (transforms from model space to bone space)
    glm::mat4 inverseBindPose = glm::mat4(1.0f);

    // Bind pose matrix (bone space to model space)
    glm::mat4 bindPose = glm::mat4(1.0f);

    // Optional length (for visualization/IK)
    float length = 0.0f;

    // Optional flags
    uint32_t flags = 0;

    // Commonly used flag constants
    static constexpr uint32_t FLAG_NONE = 0;
    static constexpr uint32_t FLAG_NO_INHERIT_SCALE = 1 << 0;   // Don't inherit parent scale
    static constexpr uint32_t FLAG_IK_EFFECTOR = 1 << 1;        // Used as IK end effector
    static constexpr uint32_t FLAG_IK_POLE = 1 << 2;            // Used as IK pole target
    static constexpr uint32_t FLAG_PHYSICS_DRIVEN = 1 << 3;     // Driven by physics (ragdoll)
};

// Skeleton definition (shared across instances)
class Skeleton {
public:
    Skeleton() = default;
    ~Skeleton() = default;

    // Add a bone to the skeleton
    int32_t AddBone(const std::string& name, int32_t parentIndex = -1);

    // Get bone by name
    int32_t GetBoneIndex(const std::string& name) const;

    // Get bone by index
    Bone* GetBone(int32_t index);
    const Bone* GetBone(int32_t index) const;

    // Get bone count
    [[nodiscard]] uint32_t GetBoneCount() const { return static_cast<uint32_t>(m_bones.size()); }

    // Get all bones
    [[nodiscard]] const std::vector<Bone>& GetBones() const { return m_bones; }
    [[nodiscard]] std::vector<Bone>& GetBones() { return m_bones; }

    // Compute inverse bind poses from current rest poses
    void ComputeInverseBindPoses();

    // Get children of a bone
    std::vector<int32_t> GetChildBones(int32_t boneIndex) const;

    // Get bone chain from root to bone
    std::vector<int32_t> GetBoneChain(int32_t boneIndex) const;

    // Validate skeleton integrity
    bool Validate() const;

    // Skeleton name
    std::string name;

private:
    std::vector<Bone> m_bones;
    std::unordered_map<std::string, int32_t> m_boneNameToIndex;
};

// Skeleton instance (runtime state)
class SkeletonInstance {
public:
    SkeletonInstance() = default;
    explicit SkeletonInstance(std::shared_ptr<Skeleton> skeleton);

    // Set skeleton
    void SetSkeleton(std::shared_ptr<Skeleton> skeleton);

    // Get skeleton
    [[nodiscard]] const Skeleton* GetSkeleton() const { return m_skeleton.get(); }

    // Get local pose
    [[nodiscard]] const std::vector<BoneTransform>& GetLocalPose() const { return m_localPose; }
    [[nodiscard]] std::vector<BoneTransform>& GetLocalPose() { return m_localPose; }

    // Set local bone transform
    void SetLocalBoneTransform(int32_t boneIndex, const BoneTransform& transform);

    // Get local bone transform
    [[nodiscard]] const BoneTransform& GetLocalBoneTransform(int32_t boneIndex) const;

    // Get world/model space bone transform (after Update)
    [[nodiscard]] const glm::mat4& GetWorldBoneMatrix(int32_t boneIndex) const;

    // Get skinning matrix (inverse bind * world)
    [[nodiscard]] const glm::mat4& GetSkinningMatrix(int32_t boneIndex) const;

    // Get all skinning matrices (for GPU upload)
    [[nodiscard]] const std::vector<glm::mat4>& GetSkinningMatrices() const { return m_skinningMatrices; }

    // Update world matrices from local transforms (call after animation)
    void UpdateWorldMatrices();

    // Reset to bind pose
    void ResetToBindPose();

    // Blend another pose onto this one
    void BlendPose(const SkeletonInstance& other, float weight);

    // Apply additive pose
    void ApplyAdditivePose(const SkeletonInstance& additive, float weight);

    // Get bone world position
    [[nodiscard]] glm::vec3 GetBoneWorldPosition(int32_t boneIndex) const;

    // Get bone world rotation
    [[nodiscard]] glm::quat GetBoneWorldRotation(int32_t boneIndex) const;

    // Set root motion
    void SetRootMotion(const glm::vec3& deltaPosition, const glm::quat& deltaRotation);

    // Get and consume root motion
    glm::vec3 ConsumeRootMotionPosition();
    glm::quat ConsumeRootMotionRotation();

    // Check if valid
    [[nodiscard]] bool IsValid() const { return m_skeleton != nullptr && !m_localPose.empty(); }

private:
    std::shared_ptr<Skeleton> m_skeleton;

    // Current local pose (bone local transforms)
    std::vector<BoneTransform> m_localPose;

    // Computed world matrices
    std::vector<glm::mat4> m_worldMatrices;

    // Final skinning matrices (inverse bind * world)
    std::vector<glm::mat4> m_skinningMatrices;

    // Root motion accumulator
    glm::vec3 m_rootMotionPosition = glm::vec3(0.0f);
    glm::quat m_rootMotionRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
};

// Skin weight data for a vertex
struct SkinWeight {
    uint32_t boneIndices[MAX_BONES_PER_VERTEX] = {0, 0, 0, 0};
    float weights[MAX_BONES_PER_VERTEX] = {0.0f, 0.0f, 0.0f, 0.0f};

    // Normalize weights to sum to 1
    void Normalize();

    // Add a bone influence (maintains sorted by weight)
    void AddInfluence(uint32_t boneIndex, float weight);
};

// GPU-ready bone data for upload
struct alignas(16) BoneMatrixGPU {
    glm::mat4 skinningMatrix;   // Inverse bind * world
};

// GPU constant buffer for skinned mesh rendering
struct alignas(16) SkinningCB {
    uint32_t boneCount;
    uint32_t padding[3];
};

} // namespace Cortex::Animation
