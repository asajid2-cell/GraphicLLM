// Skeleton.cpp
// Implementation of skeleton hierarchy and bone transforms.

#include "Skeleton.h"
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

namespace Cortex::Animation {

// BoneTransform implementation
glm::mat4 BoneTransform::ToMatrix() const {
    glm::mat4 mat = glm::mat4(1.0f);
    mat = glm::translate(mat, position);
    mat = mat * glm::mat4_cast(rotation);
    mat = glm::scale(mat, scale);
    return mat;
}

BoneTransform BoneTransform::FromMatrix(const glm::mat4& matrix) {
    BoneTransform transform;

    glm::vec3 skew;
    glm::vec4 perspective;
    glm::decompose(matrix, transform.scale, transform.rotation, transform.position, skew, perspective);

    return transform;
}

BoneTransform BoneTransform::Lerp(const BoneTransform& a, const BoneTransform& b, float t) {
    BoneTransform result;
    result.position = glm::mix(a.position, b.position, t);
    result.rotation = glm::slerp(a.rotation, b.rotation, t);
    result.scale = glm::mix(a.scale, b.scale, t);
    return result;
}

BoneTransform BoneTransform::Blend(const BoneTransform* transforms, const float* weights, uint32_t count) {
    if (count == 0) {
        return Identity();
    }

    if (count == 1) {
        return transforms[0];
    }

    // Weighted average for position and scale
    glm::vec3 position(0.0f);
    glm::vec3 scale(0.0f);
    float totalWeight = 0.0f;

    for (uint32_t i = 0; i < count; i++) {
        position += transforms[i].position * weights[i];
        scale += transforms[i].scale * weights[i];
        totalWeight += weights[i];
    }

    if (totalWeight > 0.0001f) {
        position /= totalWeight;
        scale /= totalWeight;
    } else {
        position = transforms[0].position;
        scale = transforms[0].scale;
    }

    // Weighted quaternion blend (normalized lerp for small differences)
    glm::quat rotation = transforms[0].rotation * weights[0];
    for (uint32_t i = 1; i < count; i++) {
        glm::quat q = transforms[i].rotation;

        // Ensure shortest path
        if (glm::dot(rotation, q) < 0.0f) {
            q = -q;
        }

        rotation = rotation + q * weights[i];
    }
    rotation = glm::normalize(rotation);

    return BoneTransform(position, rotation, scale);
}

BoneTransform BoneTransform::AddBlend(const BoneTransform& base, const BoneTransform& additive, float weight) {
    BoneTransform result;
    result.position = base.position + additive.position * weight;
    result.rotation = base.rotation * glm::slerp(glm::quat(1, 0, 0, 0), additive.rotation, weight);
    result.scale = base.scale * glm::mix(glm::vec3(1.0f), additive.scale, weight);
    return result;
}

// Skeleton implementation
int32_t Skeleton::AddBone(const std::string& name, int32_t parentIndex) {
    // Check for duplicate
    if (m_boneNameToIndex.find(name) != m_boneNameToIndex.end()) {
        return m_boneNameToIndex[name];
    }

    // Validate parent
    if (parentIndex >= 0 && parentIndex >= static_cast<int32_t>(m_bones.size())) {
        return -1;
    }

    int32_t index = static_cast<int32_t>(m_bones.size());

    Bone bone;
    bone.name = name;
    bone.parentIndex = parentIndex;

    m_bones.push_back(bone);
    m_boneNameToIndex[name] = index;

    return index;
}

int32_t Skeleton::GetBoneIndex(const std::string& name) const {
    auto it = m_boneNameToIndex.find(name);
    if (it != m_boneNameToIndex.end()) {
        return it->second;
    }
    return -1;
}

Bone* Skeleton::GetBone(int32_t index) {
    if (index >= 0 && index < static_cast<int32_t>(m_bones.size())) {
        return &m_bones[index];
    }
    return nullptr;
}

const Bone* Skeleton::GetBone(int32_t index) const {
    if (index >= 0 && index < static_cast<int32_t>(m_bones.size())) {
        return &m_bones[index];
    }
    return nullptr;
}

void Skeleton::ComputeInverseBindPoses() {
    // First compute bind pose matrices in world space
    std::vector<glm::mat4> bindPoses(m_bones.size(), glm::mat4(1.0f));

    for (size_t i = 0; i < m_bones.size(); i++) {
        Bone& bone = m_bones[i];
        glm::mat4 localMatrix = bone.restPose.ToMatrix();

        if (bone.parentIndex >= 0) {
            bindPoses[i] = bindPoses[bone.parentIndex] * localMatrix;
        } else {
            bindPoses[i] = localMatrix;
        }

        bone.bindPose = bindPoses[i];
        bone.inverseBindPose = glm::inverse(bindPoses[i]);
    }
}

std::vector<int32_t> Skeleton::GetChildBones(int32_t boneIndex) const {
    std::vector<int32_t> children;
    for (int32_t i = 0; i < static_cast<int32_t>(m_bones.size()); i++) {
        if (m_bones[i].parentIndex == boneIndex) {
            children.push_back(i);
        }
    }
    return children;
}

std::vector<int32_t> Skeleton::GetBoneChain(int32_t boneIndex) const {
    std::vector<int32_t> chain;

    int32_t current = boneIndex;
    while (current >= 0) {
        chain.push_back(current);
        current = m_bones[current].parentIndex;
    }

    // Reverse so root is first
    std::reverse(chain.begin(), chain.end());
    return chain;
}

bool Skeleton::Validate() const {
    for (size_t i = 0; i < m_bones.size(); i++) {
        const Bone& bone = m_bones[i];

        // Check parent index validity
        if (bone.parentIndex >= static_cast<int32_t>(i)) {
            return false;  // Parent must come before child
        }

        // Check name is not empty
        if (bone.name.empty()) {
            return false;
        }
    }
    return true;
}

// SkeletonInstance implementation
SkeletonInstance::SkeletonInstance(std::shared_ptr<Skeleton> skeleton) {
    SetSkeleton(skeleton);
}

void SkeletonInstance::SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
    m_skeleton = skeleton;

    if (skeleton) {
        uint32_t boneCount = skeleton->GetBoneCount();
        m_localPose.resize(boneCount);
        m_worldMatrices.resize(boneCount, glm::mat4(1.0f));
        m_skinningMatrices.resize(boneCount, glm::mat4(1.0f));

        ResetToBindPose();
    } else {
        m_localPose.clear();
        m_worldMatrices.clear();
        m_skinningMatrices.clear();
    }
}

void SkeletonInstance::SetLocalBoneTransform(int32_t boneIndex, const BoneTransform& transform) {
    if (boneIndex >= 0 && boneIndex < static_cast<int32_t>(m_localPose.size())) {
        m_localPose[boneIndex] = transform;
    }
}

const BoneTransform& SkeletonInstance::GetLocalBoneTransform(int32_t boneIndex) const {
    static const BoneTransform identity = BoneTransform::Identity();
    if (boneIndex >= 0 && boneIndex < static_cast<int32_t>(m_localPose.size())) {
        return m_localPose[boneIndex];
    }
    return identity;
}

const glm::mat4& SkeletonInstance::GetWorldBoneMatrix(int32_t boneIndex) const {
    static const glm::mat4 identity(1.0f);
    if (boneIndex >= 0 && boneIndex < static_cast<int32_t>(m_worldMatrices.size())) {
        return m_worldMatrices[boneIndex];
    }
    return identity;
}

const glm::mat4& SkeletonInstance::GetSkinningMatrix(int32_t boneIndex) const {
    static const glm::mat4 identity(1.0f);
    if (boneIndex >= 0 && boneIndex < static_cast<int32_t>(m_skinningMatrices.size())) {
        return m_skinningMatrices[boneIndex];
    }
    return identity;
}

void SkeletonInstance::UpdateWorldMatrices() {
    if (!m_skeleton) {
        return;
    }

    const auto& bones = m_skeleton->GetBones();

    for (size_t i = 0; i < bones.size(); i++) {
        const Bone& bone = bones[i];
        glm::mat4 localMatrix = m_localPose[i].ToMatrix();

        if (bone.parentIndex >= 0) {
            m_worldMatrices[i] = m_worldMatrices[bone.parentIndex] * localMatrix;
        } else {
            m_worldMatrices[i] = localMatrix;
        }

        // Compute skinning matrix
        m_skinningMatrices[i] = m_worldMatrices[i] * bone.inverseBindPose;
    }
}

void SkeletonInstance::ResetToBindPose() {
    if (!m_skeleton) {
        return;
    }

    const auto& bones = m_skeleton->GetBones();
    for (size_t i = 0; i < bones.size(); i++) {
        m_localPose[i] = bones[i].restPose;
    }

    UpdateWorldMatrices();
}

void SkeletonInstance::BlendPose(const SkeletonInstance& other, float weight) {
    if (!m_skeleton || !other.m_skeleton || m_localPose.size() != other.m_localPose.size()) {
        return;
    }

    for (size_t i = 0; i < m_localPose.size(); i++) {
        m_localPose[i] = BoneTransform::Lerp(m_localPose[i], other.m_localPose[i], weight);
    }
}

void SkeletonInstance::ApplyAdditivePose(const SkeletonInstance& additive, float weight) {
    if (!m_skeleton || !additive.m_skeleton || m_localPose.size() != additive.m_localPose.size()) {
        return;
    }

    for (size_t i = 0; i < m_localPose.size(); i++) {
        m_localPose[i] = BoneTransform::AddBlend(m_localPose[i], additive.m_localPose[i], weight);
    }
}

glm::vec3 SkeletonInstance::GetBoneWorldPosition(int32_t boneIndex) const {
    if (boneIndex >= 0 && boneIndex < static_cast<int32_t>(m_worldMatrices.size())) {
        return glm::vec3(m_worldMatrices[boneIndex][3]);
    }
    return glm::vec3(0.0f);
}

glm::quat SkeletonInstance::GetBoneWorldRotation(int32_t boneIndex) const {
    if (boneIndex >= 0 && boneIndex < static_cast<int32_t>(m_worldMatrices.size())) {
        return glm::quat_cast(m_worldMatrices[boneIndex]);
    }
    return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
}

void SkeletonInstance::SetRootMotion(const glm::vec3& deltaPosition, const glm::quat& deltaRotation) {
    m_rootMotionPosition += deltaPosition;
    m_rootMotionRotation = deltaRotation * m_rootMotionRotation;
}

glm::vec3 SkeletonInstance::ConsumeRootMotionPosition() {
    glm::vec3 motion = m_rootMotionPosition;
    m_rootMotionPosition = glm::vec3(0.0f);
    return motion;
}

glm::quat SkeletonInstance::ConsumeRootMotionRotation() {
    glm::quat motion = m_rootMotionRotation;
    m_rootMotionRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    return motion;
}

// SkinWeight implementation
void SkinWeight::Normalize() {
    float total = 0.0f;
    for (int i = 0; i < MAX_BONES_PER_VERTEX; i++) {
        total += weights[i];
    }

    if (total > 0.0001f) {
        for (int i = 0; i < MAX_BONES_PER_VERTEX; i++) {
            weights[i] /= total;
        }
    } else {
        // No weights, default to first bone
        weights[0] = 1.0f;
        for (int i = 1; i < MAX_BONES_PER_VERTEX; i++) {
            weights[i] = 0.0f;
        }
    }
}

void SkinWeight::AddInfluence(uint32_t boneIndex, float weight) {
    // Find slot to insert
    int insertIndex = -1;
    float minWeight = weight;

    for (int i = 0; i < MAX_BONES_PER_VERTEX; i++) {
        if (weights[i] < minWeight) {
            minWeight = weights[i];
            insertIndex = i;
        }
    }

    if (insertIndex >= 0) {
        boneIndices[insertIndex] = boneIndex;
        weights[insertIndex] = weight;
    }
}

} // namespace Cortex::Animation
