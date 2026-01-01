#pragma once

// AnimationClip.h
// Animation clip data structures and playback.
// Supports keyframe interpolation, looping, and events.
// Reference: "Game Engine Architecture" - Gregory, Chapter 11

#include "Skeleton.h"
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <string>
#include <functional>
#include <memory>

namespace Cortex::Animation {

// Forward declarations
class Skeleton;
class SkeletonInstance;

// Keyframe interpolation modes
enum class InterpolationMode : uint8_t {
    Step = 0,           // No interpolation
    Linear = 1,         // Linear interpolation
    CubicSpline = 2     // Cubic spline interpolation
};

// Animation wrap mode
enum class WrapMode : uint8_t {
    Once = 0,           // Play once and stop
    Loop = 1,           // Loop continuously
    PingPong = 2,       // Play forward, then backward
    ClampForever = 3    // Play once and hold last frame
};

// Keyframe for position
struct PositionKeyframe {
    float time = 0.0f;
    glm::vec3 value = glm::vec3(0.0f);
    glm::vec3 inTangent = glm::vec3(0.0f);   // For cubic spline
    glm::vec3 outTangent = glm::vec3(0.0f);  // For cubic spline
};

// Keyframe for rotation
struct RotationKeyframe {
    float time = 0.0f;
    glm::quat value = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    glm::quat inTangent = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
    glm::quat outTangent = glm::quat(0.0f, 0.0f, 0.0f, 0.0f);
};

// Keyframe for scale
struct ScaleKeyframe {
    float time = 0.0f;
    glm::vec3 value = glm::vec3(1.0f);
    glm::vec3 inTangent = glm::vec3(0.0f);
    glm::vec3 outTangent = glm::vec3(0.0f);
};

// Animation channel for a single bone
struct BoneAnimationChannel {
    int32_t boneIndex = -1;
    std::string boneName;

    std::vector<PositionKeyframe> positionKeys;
    std::vector<RotationKeyframe> rotationKeys;
    std::vector<ScaleKeyframe> scaleKeys;

    InterpolationMode positionInterpolation = InterpolationMode::Linear;
    InterpolationMode rotationInterpolation = InterpolationMode::Linear;
    InterpolationMode scaleInterpolation = InterpolationMode::Linear;

    // Sample position at time
    glm::vec3 SamplePosition(float time) const;

    // Sample rotation at time
    glm::quat SampleRotation(float time) const;

    // Sample scale at time
    glm::vec3 SampleScale(float time) const;

    // Sample full transform at time
    BoneTransform Sample(float time) const;

private:
    // Find keyframe indices for time
    template<typename T>
    static void FindKeyframes(const std::vector<T>& keys, float time, int32_t& out0, int32_t& out1, float& outT);

    // Cubic spline interpolation
    static glm::vec3 CubicSpline(const glm::vec3& p0, const glm::vec3& m0,
                                  const glm::vec3& p1, const glm::vec3& m1, float t);
    static glm::quat CubicSplineQuat(const glm::quat& p0, const glm::quat& m0,
                                      const glm::quat& p1, const glm::quat& m1, float t);
};

// Animation event triggered at specific times
struct AnimationEvent {
    float time = 0.0f;
    std::string name;
    std::string parameter;
};

using AnimationEventCallback = std::function<void(const AnimationEvent&)>;

// Animation clip (shared data)
class AnimationClip {
public:
    AnimationClip() = default;
    ~AnimationClip() = default;

    // Properties
    std::string name;
    float duration = 0.0f;
    float ticksPerSecond = 30.0f;
    WrapMode wrapMode = WrapMode::Loop;
    bool isAdditive = false;

    // Channels
    std::vector<BoneAnimationChannel> channels;

    // Events
    std::vector<AnimationEvent> events;

    // Get channel for bone
    BoneAnimationChannel* GetChannel(int32_t boneIndex);
    const BoneAnimationChannel* GetChannel(int32_t boneIndex) const;

    // Get channel by bone name
    BoneAnimationChannel* GetChannelByName(const std::string& boneName);
    const BoneAnimationChannel* GetChannelByName(const std::string& boneName) const;

    // Add channel
    BoneAnimationChannel& AddChannel(int32_t boneIndex, const std::string& boneName);

    // Sample all channels at time into a pose
    void Sample(float time, SkeletonInstance& pose) const;

    // Sample with weight (for blending)
    void SampleWithWeight(float time, SkeletonInstance& pose, float weight) const;

    // Compute duration from keyframes
    void ComputeDuration();

    // Bind to skeleton (resolve bone names to indices)
    void BindToSkeleton(const Skeleton* skeleton);

    // Root motion
    bool hasRootMotion = false;
    int32_t rootBoneIndex = 0;
    glm::vec3 GetRootMotionDelta(float startTime, float endTime) const;
    glm::quat GetRootRotationDelta(float startTime, float endTime) const;
};

// Animation playback state
class AnimationPlayback {
public:
    AnimationPlayback() = default;
    explicit AnimationPlayback(std::shared_ptr<AnimationClip> clip);

    // Set clip
    void SetClip(std::shared_ptr<AnimationClip> clip);
    [[nodiscard]] const AnimationClip* GetClip() const { return m_clip.get(); }

    // Playback control
    void Play();
    void Pause();
    void Stop();
    void Seek(float time);

    // Update (returns root motion if applicable)
    void Update(float deltaTime);

    // Sample current pose
    void Sample(SkeletonInstance& pose) const;

    // Sample with weight
    void SampleWithWeight(SkeletonInstance& pose, float weight) const;

    // State
    [[nodiscard]] bool IsPlaying() const { return m_playing; }
    [[nodiscard]] bool IsFinished() const { return m_finished; }
    [[nodiscard]] float GetTime() const { return m_time; }
    [[nodiscard]] float GetNormalizedTime() const;
    [[nodiscard]] float GetDuration() const;

    // Speed control
    void SetSpeed(float speed) { m_speed = speed; }
    [[nodiscard]] float GetSpeed() const { return m_speed; }

    // Weight (for blending)
    void SetWeight(float weight) { m_weight = weight; }
    [[nodiscard]] float GetWeight() const { return m_weight; }

    // Events
    void SetEventCallback(AnimationEventCallback callback) { m_eventCallback = callback; }

    // Root motion
    [[nodiscard]] glm::vec3 GetRootMotionPosition() const { return m_rootMotionPosition; }
    [[nodiscard]] glm::quat GetRootMotionRotation() const { return m_rootMotionRotation; }
    glm::vec3 ConsumeRootMotionPosition();
    glm::quat ConsumeRootMotionRotation();

private:
    void ProcessEvents(float startTime, float endTime);

    std::shared_ptr<AnimationClip> m_clip;

    float m_time = 0.0f;
    float m_prevTime = 0.0f;
    float m_speed = 1.0f;
    float m_weight = 1.0f;

    bool m_playing = false;
    bool m_finished = false;
    bool m_pingPongForward = true;

    // Root motion
    glm::vec3 m_rootMotionPosition = glm::vec3(0.0f);
    glm::quat m_rootMotionRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    // Event handling
    AnimationEventCallback m_eventCallback;
    int32_t m_lastEventIndex = -1;
};

// Animation blender for combining multiple animations
class AnimationBlender {
public:
    // Layer modes
    enum class LayerMode {
        Override,       // Replace previous layers
        Additive        // Add to previous layers
    };

    struct AnimationLayer {
        AnimationPlayback playback;
        float weight = 1.0f;
        LayerMode mode = LayerMode::Override;
        std::vector<float> boneMask;  // Per-bone weight mask (empty = all bones)
    };

    AnimationBlender() = default;

    // Set skeleton
    void SetSkeleton(std::shared_ptr<Skeleton> skeleton);

    // Layer management
    uint32_t AddLayer();
    void RemoveLayer(uint32_t layerIndex);
    AnimationLayer* GetLayer(uint32_t layerIndex);
    [[nodiscard]] uint32_t GetLayerCount() const { return static_cast<uint32_t>(m_layers.size()); }

    // Play animation on layer
    void PlayAnimation(uint32_t layerIndex, std::shared_ptr<AnimationClip> clip,
                       float blendTime = 0.2f);

    // Update all layers
    void Update(float deltaTime);

    // Evaluate final pose
    void Evaluate(SkeletonInstance& pose);

    // Crossfade between animations
    void Crossfade(uint32_t layerIndex, std::shared_ptr<AnimationClip> newClip, float duration);

private:
    std::shared_ptr<Skeleton> m_skeleton;
    std::vector<AnimationLayer> m_layers;

    // Crossfade state
    struct CrossfadeState {
        AnimationPlayback outgoing;
        float duration = 0.0f;
        float elapsed = 0.0f;
        bool active = false;
    };
    std::vector<CrossfadeState> m_crossfades;
};

} // namespace Cortex::Animation
