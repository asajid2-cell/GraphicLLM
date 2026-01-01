// AnimationClip.cpp
// Implementation of animation clips and playback.

#include "AnimationClip.h"
#include <algorithm>
#include <cmath>

namespace Cortex::Animation {

// BoneAnimationChannel implementation
template<typename T>
void BoneAnimationChannel::FindKeyframes(const std::vector<T>& keys, float time,
                                          int32_t& out0, int32_t& out1, float& outT) {
    if (keys.empty()) {
        out0 = out1 = -1;
        outT = 0.0f;
        return;
    }

    if (keys.size() == 1) {
        out0 = out1 = 0;
        outT = 0.0f;
        return;
    }

    // Binary search for the correct interval
    int32_t low = 0;
    int32_t high = static_cast<int32_t>(keys.size()) - 1;

    while (low < high - 1) {
        int32_t mid = (low + high) / 2;
        if (keys[mid].time <= time) {
            low = mid;
        } else {
            high = mid;
        }
    }

    out0 = low;
    out1 = high;

    float t0 = keys[low].time;
    float t1 = keys[high].time;

    if (t1 <= t0) {
        outT = 0.0f;
    } else {
        outT = (time - t0) / (t1 - t0);
    }
}

glm::vec3 BoneAnimationChannel::CubicSpline(const glm::vec3& p0, const glm::vec3& m0,
                                             const glm::vec3& p1, const glm::vec3& m1, float t) {
    float t2 = t * t;
    float t3 = t2 * t;

    return (2.0f * t3 - 3.0f * t2 + 1.0f) * p0 +
           (t3 - 2.0f * t2 + t) * m0 +
           (-2.0f * t3 + 3.0f * t2) * p1 +
           (t3 - t2) * m1;
}

glm::quat BoneAnimationChannel::CubicSplineQuat(const glm::quat& p0, const glm::quat& m0,
                                                 const glm::quat& p1, const glm::quat& m1, float t) {
    // For quaternions, we use squad (spherical cubic) interpolation
    // Simplified version using slerp for tangents
    glm::quat result = glm::slerp(p0, p1, t);
    return glm::normalize(result);
}

glm::vec3 BoneAnimationChannel::SamplePosition(float time) const {
    if (positionKeys.empty()) {
        return glm::vec3(0.0f);
    }

    int32_t idx0, idx1;
    float t;
    FindKeyframes(positionKeys, time, idx0, idx1, t);

    if (idx0 < 0) {
        return glm::vec3(0.0f);
    }

    const auto& k0 = positionKeys[idx0];
    const auto& k1 = positionKeys[idx1];

    switch (positionInterpolation) {
        case InterpolationMode::Step:
            return k0.value;

        case InterpolationMode::Linear:
            return glm::mix(k0.value, k1.value, t);

        case InterpolationMode::CubicSpline:
            return CubicSpline(k0.value, k0.outTangent, k1.value, k1.inTangent, t);

        default:
            return glm::mix(k0.value, k1.value, t);
    }
}

glm::quat BoneAnimationChannel::SampleRotation(float time) const {
    if (rotationKeys.empty()) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    int32_t idx0, idx1;
    float t;
    FindKeyframes(rotationKeys, time, idx0, idx1, t);

    if (idx0 < 0) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    const auto& k0 = rotationKeys[idx0];
    const auto& k1 = rotationKeys[idx1];

    switch (rotationInterpolation) {
        case InterpolationMode::Step:
            return k0.value;

        case InterpolationMode::Linear:
            return glm::slerp(k0.value, k1.value, t);

        case InterpolationMode::CubicSpline:
            return CubicSplineQuat(k0.value, k0.outTangent, k1.value, k1.inTangent, t);

        default:
            return glm::slerp(k0.value, k1.value, t);
    }
}

glm::vec3 BoneAnimationChannel::SampleScale(float time) const {
    if (scaleKeys.empty()) {
        return glm::vec3(1.0f);
    }

    int32_t idx0, idx1;
    float t;
    FindKeyframes(scaleKeys, time, idx0, idx1, t);

    if (idx0 < 0) {
        return glm::vec3(1.0f);
    }

    const auto& k0 = scaleKeys[idx0];
    const auto& k1 = scaleKeys[idx1];

    switch (scaleInterpolation) {
        case InterpolationMode::Step:
            return k0.value;

        case InterpolationMode::Linear:
            return glm::mix(k0.value, k1.value, t);

        case InterpolationMode::CubicSpline:
            return CubicSpline(k0.value, k0.outTangent, k1.value, k1.inTangent, t);

        default:
            return glm::mix(k0.value, k1.value, t);
    }
}

BoneTransform BoneAnimationChannel::Sample(float time) const {
    return BoneTransform(
        SamplePosition(time),
        SampleRotation(time),
        SampleScale(time)
    );
}

// AnimationClip implementation
BoneAnimationChannel* AnimationClip::GetChannel(int32_t boneIndex) {
    for (auto& channel : channels) {
        if (channel.boneIndex == boneIndex) {
            return &channel;
        }
    }
    return nullptr;
}

const BoneAnimationChannel* AnimationClip::GetChannel(int32_t boneIndex) const {
    for (const auto& channel : channels) {
        if (channel.boneIndex == boneIndex) {
            return &channel;
        }
    }
    return nullptr;
}

BoneAnimationChannel* AnimationClip::GetChannelByName(const std::string& boneName) {
    for (auto& channel : channels) {
        if (channel.boneName == boneName) {
            return &channel;
        }
    }
    return nullptr;
}

const BoneAnimationChannel* AnimationClip::GetChannelByName(const std::string& boneName) const {
    for (const auto& channel : channels) {
        if (channel.boneName == boneName) {
            return &channel;
        }
    }
    return nullptr;
}

BoneAnimationChannel& AnimationClip::AddChannel(int32_t boneIndex, const std::string& boneName) {
    channels.emplace_back();
    auto& channel = channels.back();
    channel.boneIndex = boneIndex;
    channel.boneName = boneName;
    return channel;
}

void AnimationClip::Sample(float time, SkeletonInstance& pose) const {
    for (const auto& channel : channels) {
        if (channel.boneIndex >= 0) {
            BoneTransform transform = channel.Sample(time);
            pose.SetLocalBoneTransform(channel.boneIndex, transform);
        }
    }
}

void AnimationClip::SampleWithWeight(float time, SkeletonInstance& pose, float weight) const {
    if (weight <= 0.0f) {
        return;
    }

    if (weight >= 1.0f) {
        Sample(time, pose);
        return;
    }

    for (const auto& channel : channels) {
        if (channel.boneIndex >= 0) {
            BoneTransform animTransform = channel.Sample(time);
            const BoneTransform& currentTransform = pose.GetLocalBoneTransform(channel.boneIndex);

            BoneTransform blended = BoneTransform::Lerp(currentTransform, animTransform, weight);
            pose.SetLocalBoneTransform(channel.boneIndex, blended);
        }
    }
}

void AnimationClip::ComputeDuration() {
    duration = 0.0f;

    for (const auto& channel : channels) {
        if (!channel.positionKeys.empty()) {
            duration = std::max(duration, channel.positionKeys.back().time);
        }
        if (!channel.rotationKeys.empty()) {
            duration = std::max(duration, channel.rotationKeys.back().time);
        }
        if (!channel.scaleKeys.empty()) {
            duration = std::max(duration, channel.scaleKeys.back().time);
        }
    }
}

void AnimationClip::BindToSkeleton(const Skeleton* skeleton) {
    if (!skeleton) {
        return;
    }

    for (auto& channel : channels) {
        if (channel.boneIndex < 0 && !channel.boneName.empty()) {
            channel.boneIndex = skeleton->GetBoneIndex(channel.boneName);
        }
    }
}

glm::vec3 AnimationClip::GetRootMotionDelta(float startTime, float endTime) const {
    if (!hasRootMotion || rootBoneIndex < 0) {
        return glm::vec3(0.0f);
    }

    const BoneAnimationChannel* channel = GetChannel(rootBoneIndex);
    if (!channel) {
        return glm::vec3(0.0f);
    }

    glm::vec3 startPos = channel->SamplePosition(startTime);
    glm::vec3 endPos = channel->SamplePosition(endTime);

    return endPos - startPos;
}

glm::quat AnimationClip::GetRootRotationDelta(float startTime, float endTime) const {
    if (!hasRootMotion || rootBoneIndex < 0) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    const BoneAnimationChannel* channel = GetChannel(rootBoneIndex);
    if (!channel) {
        return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }

    glm::quat startRot = channel->SampleRotation(startTime);
    glm::quat endRot = channel->SampleRotation(endTime);

    return endRot * glm::inverse(startRot);
}

// AnimationPlayback implementation
AnimationPlayback::AnimationPlayback(std::shared_ptr<AnimationClip> clip) {
    SetClip(clip);
}

void AnimationPlayback::SetClip(std::shared_ptr<AnimationClip> clip) {
    m_clip = clip;
    m_time = 0.0f;
    m_prevTime = 0.0f;
    m_playing = false;
    m_finished = false;
    m_lastEventIndex = -1;
}

void AnimationPlayback::Play() {
    m_playing = true;
    m_finished = false;
}

void AnimationPlayback::Pause() {
    m_playing = false;
}

void AnimationPlayback::Stop() {
    m_playing = false;
    m_time = 0.0f;
    m_prevTime = 0.0f;
    m_finished = false;
    m_lastEventIndex = -1;
}

void AnimationPlayback::Seek(float time) {
    m_time = time;
    m_prevTime = time;
    m_lastEventIndex = -1;
}

void AnimationPlayback::Update(float deltaTime) {
    if (!m_clip || !m_playing || m_finished) {
        return;
    }

    m_prevTime = m_time;
    m_time += deltaTime * m_speed;

    float duration = m_clip->duration;
    if (duration <= 0.0f) {
        return;
    }

    // Handle wrap modes
    switch (m_clip->wrapMode) {
        case WrapMode::Once:
            if (m_time >= duration) {
                m_time = duration;
                m_finished = true;
                m_playing = false;
            }
            break;

        case WrapMode::Loop:
            while (m_time >= duration) {
                m_time -= duration;
                m_lastEventIndex = -1;
            }
            while (m_time < 0.0f) {
                m_time += duration;
            }
            break;

        case WrapMode::PingPong:
            if (m_pingPongForward) {
                if (m_time >= duration) {
                    m_time = duration - (m_time - duration);
                    m_pingPongForward = false;
                }
            } else {
                if (m_time <= 0.0f) {
                    m_time = -m_time;
                    m_pingPongForward = true;
                }
            }
            break;

        case WrapMode::ClampForever:
            if (m_time >= duration) {
                m_time = duration;
            }
            break;
    }

    // Calculate root motion
    if (m_clip->hasRootMotion) {
        m_rootMotionPosition = m_clip->GetRootMotionDelta(m_prevTime, m_time);
        m_rootMotionRotation = m_clip->GetRootRotationDelta(m_prevTime, m_time);
    }

    // Process events
    ProcessEvents(m_prevTime, m_time);
}

void AnimationPlayback::Sample(SkeletonInstance& pose) const {
    if (m_clip) {
        m_clip->Sample(m_time, pose);
    }
}

void AnimationPlayback::SampleWithWeight(SkeletonInstance& pose, float weight) const {
    if (m_clip) {
        m_clip->SampleWithWeight(m_time, pose, weight * m_weight);
    }
}

float AnimationPlayback::GetNormalizedTime() const {
    if (!m_clip || m_clip->duration <= 0.0f) {
        return 0.0f;
    }
    return m_time / m_clip->duration;
}

float AnimationPlayback::GetDuration() const {
    return m_clip ? m_clip->duration : 0.0f;
}

void AnimationPlayback::ProcessEvents(float startTime, float endTime) {
    if (!m_clip || !m_eventCallback) {
        return;
    }

    for (size_t i = 0; i < m_clip->events.size(); i++) {
        const auto& event = m_clip->events[i];

        // Check if event is in time range
        if (event.time >= startTime && event.time < endTime) {
            if (static_cast<int32_t>(i) > m_lastEventIndex) {
                m_eventCallback(event);
                m_lastEventIndex = static_cast<int32_t>(i);
            }
        }
    }
}

glm::vec3 AnimationPlayback::ConsumeRootMotionPosition() {
    glm::vec3 motion = m_rootMotionPosition;
    m_rootMotionPosition = glm::vec3(0.0f);
    return motion;
}

glm::quat AnimationPlayback::ConsumeRootMotionRotation() {
    glm::quat motion = m_rootMotionRotation;
    m_rootMotionRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    return motion;
}

// AnimationBlender implementation
void AnimationBlender::SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
    m_skeleton = skeleton;
}

uint32_t AnimationBlender::AddLayer() {
    m_layers.emplace_back();
    m_crossfades.emplace_back();
    return static_cast<uint32_t>(m_layers.size() - 1);
}

void AnimationBlender::RemoveLayer(uint32_t layerIndex) {
    if (layerIndex < m_layers.size()) {
        m_layers.erase(m_layers.begin() + layerIndex);
        m_crossfades.erase(m_crossfades.begin() + layerIndex);
    }
}

AnimationBlender::AnimationLayer* AnimationBlender::GetLayer(uint32_t layerIndex) {
    if (layerIndex < m_layers.size()) {
        return &m_layers[layerIndex];
    }
    return nullptr;
}

void AnimationBlender::PlayAnimation(uint32_t layerIndex, std::shared_ptr<AnimationClip> clip, float blendTime) {
    if (layerIndex >= m_layers.size()) {
        return;
    }

    if (blendTime > 0.0f && m_layers[layerIndex].playback.IsPlaying()) {
        Crossfade(layerIndex, clip, blendTime);
    } else {
        m_layers[layerIndex].playback.SetClip(clip);
        m_layers[layerIndex].playback.Play();
    }
}

void AnimationBlender::Update(float deltaTime) {
    for (size_t i = 0; i < m_layers.size(); i++) {
        m_layers[i].playback.Update(deltaTime);

        // Update crossfade
        if (m_crossfades[i].active) {
            m_crossfades[i].elapsed += deltaTime;
            m_crossfades[i].outgoing.Update(deltaTime);

            if (m_crossfades[i].elapsed >= m_crossfades[i].duration) {
                m_crossfades[i].active = false;
            }
        }
    }
}

void AnimationBlender::Evaluate(SkeletonInstance& pose) {
    if (!m_skeleton) {
        return;
    }

    // Start from bind pose
    pose.ResetToBindPose();

    for (size_t i = 0; i < m_layers.size(); i++) {
        auto& layer = m_layers[i];
        float weight = layer.weight;

        // Handle crossfade
        if (m_crossfades[i].active) {
            float fadeT = m_crossfades[i].elapsed / m_crossfades[i].duration;
            float outWeight = (1.0f - fadeT) * weight;
            float inWeight = fadeT * weight;

            m_crossfades[i].outgoing.SampleWithWeight(pose, outWeight);
            layer.playback.SampleWithWeight(pose, inWeight);
        } else {
            if (layer.mode == LayerMode::Override) {
                layer.playback.SampleWithWeight(pose, weight);
            } else {
                // Additive mode
                // TODO: Implement proper additive blending
                layer.playback.SampleWithWeight(pose, weight);
            }
        }
    }

    // Update world matrices
    pose.UpdateWorldMatrices();
}

void AnimationBlender::Crossfade(uint32_t layerIndex, std::shared_ptr<AnimationClip> newClip, float duration) {
    if (layerIndex >= m_layers.size()) {
        return;
    }

    // Copy current playback to outgoing
    m_crossfades[layerIndex].outgoing = m_layers[layerIndex].playback;
    m_crossfades[layerIndex].duration = duration;
    m_crossfades[layerIndex].elapsed = 0.0f;
    m_crossfades[layerIndex].active = true;

    // Start new clip
    m_layers[layerIndex].playback.SetClip(newClip);
    m_layers[layerIndex].playback.Play();
}

} // namespace Cortex::Animation
