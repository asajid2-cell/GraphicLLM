#pragma once

// AnimationStateMachine.h
// Finite state machine for animation state management.
// Supports states, transitions with conditions, and blend trees.
// Reference: "Game AI Pro" - Animation State Machine Architecture

#include "AnimationClip.h"
#include "Skeleton.h"
#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include <memory>
#include <variant>

namespace Cortex::Animation {

// Forward declarations
class AnimationStateMachine;
class AnimationState;

// Parameter types for conditions
enum class ParameterType : uint8_t {
    Bool,
    Int,
    Float,
    Trigger
};

// Parameter value variant
using ParameterValue = std::variant<bool, int32_t, float>;

// Animation parameter
struct AnimationParameter {
    std::string name;
    ParameterType type = ParameterType::Float;
    ParameterValue value;

    AnimationParameter() : value(0.0f) {}
    AnimationParameter(const std::string& n, bool v) : name(n), type(ParameterType::Bool), value(v) {}
    AnimationParameter(const std::string& n, int32_t v) : name(n), type(ParameterType::Int), value(v) {}
    AnimationParameter(const std::string& n, float v) : name(n), type(ParameterType::Float), value(v) {}
};

// Condition comparison operators
enum class ConditionOp : uint8_t {
    Equals,
    NotEquals,
    Greater,
    Less,
    GreaterOrEqual,
    LessOrEqual
};

// Transition condition
struct TransitionCondition {
    std::string parameterName;
    ConditionOp op = ConditionOp::Equals;
    ParameterValue compareValue;

    // Evaluate condition
    bool Evaluate(const ParameterValue& value) const;
};

// Transition between states
struct StateTransition {
    std::string targetStateName;
    int32_t targetStateIndex = -1;

    std::vector<TransitionCondition> conditions;

    // Transition settings
    float duration = 0.2f;              // Blend duration in seconds
    float exitTime = -1.0f;             // -1 = any time, 0-1 = normalized exit time
    bool hasExitTime = false;
    bool canTransitionToSelf = false;
    int32_t priority = 0;               // Higher = checked first

    // Evaluate all conditions
    bool CanTransition(const AnimationStateMachine& sm, float normalizedTime) const;
};

// Blend tree node types
enum class BlendNodeType : uint8_t {
    Clip,           // Single animation clip
    Blend1D,        // 1D blend (e.g., walk to run by speed)
    Blend2D,        // 2D blend (e.g., locomotion by direction)
    Additive,       // Additive blend
    Override        // Override/layer blend
};

// Blend tree child entry
struct BlendTreeChild {
    std::string name;
    std::shared_ptr<AnimationClip> clip;
    float threshold = 0.0f;             // For 1D blend
    glm::vec2 position = glm::vec2(0.0f);  // For 2D blend
    float weight = 1.0f;
};

// Blend tree for complex animation blending
class BlendTree {
public:
    std::string name;
    BlendNodeType nodeType = BlendNodeType::Clip;
    std::string blendParameter;         // Parameter name for blending
    std::string blendParameterY;        // Second parameter for 2D blend

    std::vector<BlendTreeChild> children;

    // Single clip (for Clip type)
    std::shared_ptr<AnimationClip> clip;

    // Evaluate blend tree at current parameter value
    void Evaluate(const AnimationStateMachine& sm, float time, SkeletonInstance& pose) const;

    // Get duration (weighted average for blends)
    float GetDuration(const AnimationStateMachine& sm) const;

private:
    void Evaluate1D(const AnimationStateMachine& sm, float time, SkeletonInstance& pose) const;
    void Evaluate2D(const AnimationStateMachine& sm, float time, SkeletonInstance& pose) const;
    void EvaluateAdditive(const AnimationStateMachine& sm, float time, SkeletonInstance& pose) const;
};

// Animation state
class AnimationState {
public:
    std::string name;
    BlendTree blendTree;

    // Speed multiplier
    float speed = 1.0f;
    std::string speedParameter;         // Optional parameter-driven speed

    // Transitions out of this state
    std::vector<StateTransition> transitions;

    // Motion
    bool applyRootMotion = true;

    // Default constructor
    AnimationState() = default;
    explicit AnimationState(const std::string& n) : name(n) {}

    // Set simple clip
    void SetClip(std::shared_ptr<AnimationClip> clip);

    // Get duration
    float GetDuration(const AnimationStateMachine& sm) const;

    // Get actual speed (with parameter)
    float GetSpeed(const AnimationStateMachine& sm) const;
};

// Animation layer
class AnimationLayer {
public:
    std::string name;
    float weight = 1.0f;
    bool additive = false;

    // Bone mask (empty = all bones)
    std::vector<int32_t> boneMask;

    // States
    std::vector<AnimationState> states;
    int32_t defaultStateIndex = 0;

    // Current state
    int32_t currentStateIndex = 0;
    float stateTime = 0.0f;

    // Transition state
    bool isTransitioning = false;
    int32_t previousStateIndex = -1;
    float transitionTime = 0.0f;
    float transitionDuration = 0.0f;

    // Find state by name
    int32_t FindState(const std::string& name) const;

    // Add state
    int32_t AddState(const AnimationState& state);
};

// Animation state machine
class AnimationStateMachine {
public:
    AnimationStateMachine();
    ~AnimationStateMachine() = default;

    // Set skeleton
    void SetSkeleton(std::shared_ptr<Skeleton> skeleton);
    [[nodiscard]] const Skeleton* GetSkeleton() const { return m_skeleton.get(); }

    // Parameters
    void AddParameter(const std::string& name, bool value);
    void AddParameter(const std::string& name, int32_t value);
    void AddParameter(const std::string& name, float value);
    void AddTrigger(const std::string& name);

    void SetBool(const std::string& name, bool value);
    void SetInt(const std::string& name, int32_t value);
    void SetFloat(const std::string& name, float value);
    void SetTrigger(const std::string& name);
    void ResetTrigger(const std::string& name);

    [[nodiscard]] bool GetBool(const std::string& name) const;
    [[nodiscard]] int32_t GetInt(const std::string& name) const;
    [[nodiscard]] float GetFloat(const std::string& name) const;
    [[nodiscard]] bool IsTriggerSet(const std::string& name) const;

    [[nodiscard]] const AnimationParameter* GetParameter(const std::string& name) const;

    // Layers
    uint32_t AddLayer(const std::string& name);
    AnimationLayer* GetLayer(uint32_t index);
    AnimationLayer* GetLayer(const std::string& name);
    [[nodiscard]] uint32_t GetLayerCount() const { return static_cast<uint32_t>(m_layers.size()); }

    // State control
    void ForceState(uint32_t layerIndex, const std::string& stateName);
    void ForceState(uint32_t layerIndex, int32_t stateIndex);

    // Update
    void Update(float deltaTime);

    // Evaluate pose
    void Evaluate(SkeletonInstance& pose);

    // Get current state info
    [[nodiscard]] const AnimationState* GetCurrentState(uint32_t layerIndex) const;
    [[nodiscard]] float GetCurrentStateTime(uint32_t layerIndex) const;
    [[nodiscard]] float GetCurrentStateNormalizedTime(uint32_t layerIndex) const;
    [[nodiscard]] bool IsTransitioning(uint32_t layerIndex) const;

    // Root motion
    [[nodiscard]] glm::vec3 GetRootMotionPosition() const { return m_rootMotionPosition; }
    [[nodiscard]] glm::quat GetRootMotionRotation() const { return m_rootMotionRotation; }
    glm::vec3 ConsumeRootMotionPosition();
    glm::quat ConsumeRootMotionRotation();

    // Events
    using StateChangeCallback = std::function<void(uint32_t layer, const std::string& oldState, const std::string& newState)>;
    void SetStateChangeCallback(StateChangeCallback callback) { m_onStateChange = callback; }

private:
    void UpdateLayer(AnimationLayer& layer, float deltaTime);
    void CheckTransitions(AnimationLayer& layer);
    void StartTransition(AnimationLayer& layer, int32_t targetStateIndex, float duration);
    void EvaluateLayer(AnimationLayer& layer, SkeletonInstance& pose);

    std::shared_ptr<Skeleton> m_skeleton;

    // Parameters
    std::unordered_map<std::string, AnimationParameter> m_parameters;

    // Layers
    std::vector<AnimationLayer> m_layers;

    // Root motion
    glm::vec3 m_rootMotionPosition = glm::vec3(0.0f);
    glm::quat m_rootMotionRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    // Callbacks
    StateChangeCallback m_onStateChange;
};

// Helper to create common locomotion state machine
AnimationStateMachine CreateLocomotionStateMachine(
    std::shared_ptr<Skeleton> skeleton,
    std::shared_ptr<AnimationClip> idle,
    std::shared_ptr<AnimationClip> walk,
    std::shared_ptr<AnimationClip> run
);

} // namespace Cortex::Animation
