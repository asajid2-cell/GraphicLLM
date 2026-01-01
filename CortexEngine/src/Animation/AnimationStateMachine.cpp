// AnimationStateMachine.cpp
// Implementation of animation state machine.

#include "AnimationStateMachine.h"
#include <algorithm>
#include <cmath>

namespace Cortex::Animation {

// TransitionCondition implementation
bool TransitionCondition::Evaluate(const ParameterValue& value) const {
    // Handle different types
    if (std::holds_alternative<bool>(value)) {
        bool v = std::get<bool>(value);
        bool c = std::get<bool>(compareValue);
        switch (op) {
            case ConditionOp::Equals: return v == c;
            case ConditionOp::NotEquals: return v != c;
            default: return false;
        }
    }
    else if (std::holds_alternative<int32_t>(value)) {
        int32_t v = std::get<int32_t>(value);
        int32_t c = std::get<int32_t>(compareValue);
        switch (op) {
            case ConditionOp::Equals: return v == c;
            case ConditionOp::NotEquals: return v != c;
            case ConditionOp::Greater: return v > c;
            case ConditionOp::Less: return v < c;
            case ConditionOp::GreaterOrEqual: return v >= c;
            case ConditionOp::LessOrEqual: return v <= c;
        }
    }
    else if (std::holds_alternative<float>(value)) {
        float v = std::get<float>(value);
        float c = std::get<float>(compareValue);
        switch (op) {
            case ConditionOp::Equals: return std::abs(v - c) < 0.0001f;
            case ConditionOp::NotEquals: return std::abs(v - c) >= 0.0001f;
            case ConditionOp::Greater: return v > c;
            case ConditionOp::Less: return v < c;
            case ConditionOp::GreaterOrEqual: return v >= c;
            case ConditionOp::LessOrEqual: return v <= c;
        }
    }

    return false;
}

// StateTransition implementation
bool StateTransition::CanTransition(const AnimationStateMachine& sm, float normalizedTime) const {
    // Check exit time
    if (hasExitTime && exitTime >= 0.0f) {
        if (normalizedTime < exitTime) {
            return false;
        }
    }

    // Check all conditions
    for (const auto& condition : conditions) {
        const AnimationParameter* param = sm.GetParameter(condition.parameterName);
        if (!param) {
            return false;
        }

        if (!condition.Evaluate(param->value)) {
            return false;
        }
    }

    return true;
}

// BlendTree implementation
void BlendTree::Evaluate(const AnimationStateMachine& sm, float time, SkeletonInstance& pose) const {
    switch (nodeType) {
        case BlendNodeType::Clip:
            if (clip) {
                clip->Sample(time, pose);
            }
            break;

        case BlendNodeType::Blend1D:
            Evaluate1D(sm, time, pose);
            break;

        case BlendNodeType::Blend2D:
            Evaluate2D(sm, time, pose);
            break;

        case BlendNodeType::Additive:
            EvaluateAdditive(sm, time, pose);
            break;

        case BlendNodeType::Override:
            // Override: just use last child with weight > 0
            for (const auto& child : children) {
                if (child.clip && child.weight > 0.0f) {
                    child.clip->Sample(time, pose);
                }
            }
            break;
    }
}

void BlendTree::Evaluate1D(const AnimationStateMachine& sm, float time, SkeletonInstance& pose) const {
    if (children.empty()) {
        return;
    }

    float blendValue = sm.GetFloat(blendParameter);

    // Find the two clips to blend between
    int32_t lowIdx = 0;
    int32_t highIdx = 0;
    float blendT = 0.0f;

    // Sort children by threshold (should be pre-sorted, but just in case)
    std::vector<size_t> sortedIndices(children.size());
    for (size_t i = 0; i < sortedIndices.size(); i++) {
        sortedIndices[i] = i;
    }
    std::sort(sortedIndices.begin(), sortedIndices.end(),
        [this](size_t a, size_t b) {
            return children[a].threshold < children[b].threshold;
        });

    // Find interval
    for (size_t i = 0; i < sortedIndices.size() - 1; i++) {
        size_t idx0 = sortedIndices[i];
        size_t idx1 = sortedIndices[i + 1];

        if (blendValue >= children[idx0].threshold && blendValue <= children[idx1].threshold) {
            lowIdx = static_cast<int32_t>(idx0);
            highIdx = static_cast<int32_t>(idx1);

            float range = children[idx1].threshold - children[idx0].threshold;
            if (range > 0.0001f) {
                blendT = (blendValue - children[idx0].threshold) / range;
            }
            break;
        }
    }

    // Clamp to ends
    if (blendValue <= children[sortedIndices.front()].threshold) {
        lowIdx = highIdx = static_cast<int32_t>(sortedIndices.front());
        blendT = 0.0f;
    }
    if (blendValue >= children[sortedIndices.back()].threshold) {
        lowIdx = highIdx = static_cast<int32_t>(sortedIndices.back());
        blendT = 0.0f;
    }

    // Blend
    if (children[lowIdx].clip && children[highIdx].clip) {
        children[lowIdx].clip->SampleWithWeight(time, pose, 1.0f - blendT);
        children[highIdx].clip->SampleWithWeight(time, pose, blendT);
    } else if (children[lowIdx].clip) {
        children[lowIdx].clip->Sample(time, pose);
    } else if (children[highIdx].clip) {
        children[highIdx].clip->Sample(time, pose);
    }
}

void BlendTree::Evaluate2D(const AnimationStateMachine& sm, float time, SkeletonInstance& pose) const {
    if (children.empty()) {
        return;
    }

    glm::vec2 blendPos(sm.GetFloat(blendParameter), sm.GetFloat(blendParameterY));

    // Calculate weights using gradient band interpolation
    std::vector<float> weights(children.size(), 0.0f);
    float totalWeight = 0.0f;

    for (size_t i = 0; i < children.size(); i++) {
        float distance = glm::length(blendPos - children[i].position);
        float weight = 1.0f / (distance + 0.001f);  // Inverse distance weighting
        weights[i] = weight;
        totalWeight += weight;
    }

    // Normalize weights
    if (totalWeight > 0.0001f) {
        for (float& w : weights) {
            w /= totalWeight;
        }
    }

    // Blend all clips
    bool firstClip = true;
    for (size_t i = 0; i < children.size(); i++) {
        if (children[i].clip && weights[i] > 0.001f) {
            if (firstClip) {
                children[i].clip->SampleWithWeight(time, pose, weights[i]);
                firstClip = false;
            } else {
                children[i].clip->SampleWithWeight(time, pose, weights[i]);
            }
        }
    }
}

void BlendTree::EvaluateAdditive(const AnimationStateMachine& sm, float time, SkeletonInstance& pose) const {
    if (children.empty()) {
        return;
    }

    // First child is base
    if (children[0].clip) {
        children[0].clip->Sample(time, pose);
    }

    // Additional children are additive
    for (size_t i = 1; i < children.size(); i++) {
        if (children[i].clip && children[i].weight > 0.0f) {
            // Create temporary pose for additive
            SkeletonInstance additivePose(std::const_pointer_cast<Skeleton>(
                std::shared_ptr<const Skeleton>(sm.GetSkeleton(), [](const Skeleton*) {})
            ));
            children[i].clip->Sample(time, additivePose);

            pose.ApplyAdditivePose(additivePose, children[i].weight);
        }
    }
}

float BlendTree::GetDuration(const AnimationStateMachine& sm) const {
    switch (nodeType) {
        case BlendNodeType::Clip:
            return clip ? clip->duration : 0.0f;

        case BlendNodeType::Blend1D:
        case BlendNodeType::Blend2D: {
            // Weighted average of durations
            float totalDuration = 0.0f;
            float totalWeight = 0.0f;
            for (const auto& child : children) {
                if (child.clip) {
                    totalDuration += child.clip->duration * child.weight;
                    totalWeight += child.weight;
                }
            }
            return totalWeight > 0.0f ? totalDuration / totalWeight : 0.0f;
        }

        case BlendNodeType::Additive:
        case BlendNodeType::Override:
            return children.empty() || !children[0].clip ? 0.0f : children[0].clip->duration;
    }

    return 0.0f;
}

// AnimationState implementation
void AnimationState::SetClip(std::shared_ptr<AnimationClip> clip) {
    blendTree.nodeType = BlendNodeType::Clip;
    blendTree.clip = clip;
}

float AnimationState::GetDuration(const AnimationStateMachine& sm) const {
    return blendTree.GetDuration(sm);
}

float AnimationState::GetSpeed(const AnimationStateMachine& sm) const {
    if (speedParameter.empty()) {
        return speed;
    }
    return speed * sm.GetFloat(speedParameter);
}

// AnimationLayer implementation
int32_t AnimationLayer::FindState(const std::string& name) const {
    for (size_t i = 0; i < states.size(); i++) {
        if (states[i].name == name) {
            return static_cast<int32_t>(i);
        }
    }
    return -1;
}

int32_t AnimationLayer::AddState(const AnimationState& state) {
    states.push_back(state);
    return static_cast<int32_t>(states.size() - 1);
}

// AnimationStateMachine implementation
AnimationStateMachine::AnimationStateMachine() {
    // Add default base layer
    AddLayer("Base");
}

void AnimationStateMachine::SetSkeleton(std::shared_ptr<Skeleton> skeleton) {
    m_skeleton = skeleton;
}

void AnimationStateMachine::AddParameter(const std::string& name, bool value) {
    m_parameters[name] = AnimationParameter(name, value);
}

void AnimationStateMachine::AddParameter(const std::string& name, int32_t value) {
    m_parameters[name] = AnimationParameter(name, value);
}

void AnimationStateMachine::AddParameter(const std::string& name, float value) {
    m_parameters[name] = AnimationParameter(name, value);
}

void AnimationStateMachine::AddTrigger(const std::string& name) {
    AnimationParameter param;
    param.name = name;
    param.type = ParameterType::Trigger;
    param.value = false;
    m_parameters[name] = param;
}

void AnimationStateMachine::SetBool(const std::string& name, bool value) {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end() && it->second.type == ParameterType::Bool) {
        it->second.value = value;
    }
}

void AnimationStateMachine::SetInt(const std::string& name, int32_t value) {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end() && it->second.type == ParameterType::Int) {
        it->second.value = value;
    }
}

void AnimationStateMachine::SetFloat(const std::string& name, float value) {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end() && it->second.type == ParameterType::Float) {
        it->second.value = value;
    }
}

void AnimationStateMachine::SetTrigger(const std::string& name) {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end() && it->second.type == ParameterType::Trigger) {
        it->second.value = true;
    }
}

void AnimationStateMachine::ResetTrigger(const std::string& name) {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end() && it->second.type == ParameterType::Trigger) {
        it->second.value = false;
    }
}

bool AnimationStateMachine::GetBool(const std::string& name) const {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end()) {
        return std::get<bool>(it->second.value);
    }
    return false;
}

int32_t AnimationStateMachine::GetInt(const std::string& name) const {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end()) {
        return std::get<int32_t>(it->second.value);
    }
    return 0;
}

float AnimationStateMachine::GetFloat(const std::string& name) const {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end()) {
        if (std::holds_alternative<float>(it->second.value)) {
            return std::get<float>(it->second.value);
        }
    }
    return 0.0f;
}

bool AnimationStateMachine::IsTriggerSet(const std::string& name) const {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end() && it->second.type == ParameterType::Trigger) {
        return std::get<bool>(it->second.value);
    }
    return false;
}

const AnimationParameter* AnimationStateMachine::GetParameter(const std::string& name) const {
    auto it = m_parameters.find(name);
    if (it != m_parameters.end()) {
        return &it->second;
    }
    return nullptr;
}

uint32_t AnimationStateMachine::AddLayer(const std::string& name) {
    AnimationLayer layer;
    layer.name = name;
    m_layers.push_back(layer);
    return static_cast<uint32_t>(m_layers.size() - 1);
}

AnimationLayer* AnimationStateMachine::GetLayer(uint32_t index) {
    if (index < m_layers.size()) {
        return &m_layers[index];
    }
    return nullptr;
}

AnimationLayer* AnimationStateMachine::GetLayer(const std::string& name) {
    for (auto& layer : m_layers) {
        if (layer.name == name) {
            return &layer;
        }
    }
    return nullptr;
}

void AnimationStateMachine::ForceState(uint32_t layerIndex, const std::string& stateName) {
    if (layerIndex >= m_layers.size()) {
        return;
    }

    int32_t stateIndex = m_layers[layerIndex].FindState(stateName);
    if (stateIndex >= 0) {
        ForceState(layerIndex, stateIndex);
    }
}

void AnimationStateMachine::ForceState(uint32_t layerIndex, int32_t stateIndex) {
    if (layerIndex >= m_layers.size()) {
        return;
    }

    auto& layer = m_layers[layerIndex];
    if (stateIndex >= 0 && stateIndex < static_cast<int32_t>(layer.states.size())) {
        std::string oldState = layer.states[layer.currentStateIndex].name;

        layer.currentStateIndex = stateIndex;
        layer.stateTime = 0.0f;
        layer.isTransitioning = false;

        if (m_onStateChange) {
            m_onStateChange(layerIndex, oldState, layer.states[stateIndex].name);
        }
    }
}

void AnimationStateMachine::Update(float deltaTime) {
    m_rootMotionPosition = glm::vec3(0.0f);
    m_rootMotionRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);

    for (auto& layer : m_layers) {
        UpdateLayer(layer, deltaTime);
    }

    // Reset triggers after update
    for (auto& [name, param] : m_parameters) {
        if (param.type == ParameterType::Trigger) {
            param.value = false;
        }
    }
}

void AnimationStateMachine::UpdateLayer(AnimationLayer& layer, float deltaTime) {
    if (layer.states.empty()) {
        return;
    }

    // Check for transitions
    if (!layer.isTransitioning) {
        CheckTransitions(layer);
    }

    // Update transition
    if (layer.isTransitioning) {
        layer.transitionTime += deltaTime;
        if (layer.transitionTime >= layer.transitionDuration) {
            layer.isTransitioning = false;
            layer.previousStateIndex = -1;
        }
    }

    // Update current state time
    AnimationState& currentState = layer.states[layer.currentStateIndex];
    float speed = currentState.GetSpeed(*this);
    layer.stateTime += deltaTime * speed;

    // Wrap state time based on clip wrap mode
    float duration = currentState.GetDuration(*this);
    if (duration > 0.0f) {
        // Simple loop
        while (layer.stateTime >= duration) {
            layer.stateTime -= duration;
        }
    }
}

void AnimationStateMachine::CheckTransitions(AnimationLayer& layer) {
    if (layer.states.empty()) {
        return;
    }

    AnimationState& currentState = layer.states[layer.currentStateIndex];
    float normalizedTime = 0.0f;
    float duration = currentState.GetDuration(*this);
    if (duration > 0.0f) {
        normalizedTime = layer.stateTime / duration;
    }

    // Sort transitions by priority
    std::vector<const StateTransition*> sortedTransitions;
    for (const auto& trans : currentState.transitions) {
        sortedTransitions.push_back(&trans);
    }
    std::sort(sortedTransitions.begin(), sortedTransitions.end(),
        [](const StateTransition* a, const StateTransition* b) {
            return a->priority > b->priority;
        });

    // Check each transition
    for (const StateTransition* trans : sortedTransitions) {
        if (trans->CanTransition(*this, normalizedTime)) {
            int32_t targetIdx = trans->targetStateIndex;
            if (targetIdx < 0) {
                targetIdx = layer.FindState(trans->targetStateName);
            }

            if (targetIdx >= 0 && (trans->canTransitionToSelf || targetIdx != layer.currentStateIndex)) {
                StartTransition(layer, targetIdx, trans->duration);
                break;
            }
        }
    }
}

void AnimationStateMachine::StartTransition(AnimationLayer& layer, int32_t targetStateIndex, float duration) {
    std::string oldState = layer.states[layer.currentStateIndex].name;

    layer.previousStateIndex = layer.currentStateIndex;
    layer.currentStateIndex = targetStateIndex;
    layer.isTransitioning = true;
    layer.transitionTime = 0.0f;
    layer.transitionDuration = duration;
    layer.stateTime = 0.0f;

    if (m_onStateChange) {
        uint32_t layerIdx = 0;
        for (uint32_t i = 0; i < m_layers.size(); i++) {
            if (&m_layers[i] == &layer) {
                layerIdx = i;
                break;
            }
        }
        m_onStateChange(layerIdx, oldState, layer.states[targetStateIndex].name);
    }
}

void AnimationStateMachine::Evaluate(SkeletonInstance& pose) {
    if (!m_skeleton || m_layers.empty()) {
        return;
    }

    // Reset to bind pose
    pose.ResetToBindPose();

    // Evaluate each layer
    for (auto& layer : m_layers) {
        if (layer.weight > 0.0f) {
            EvaluateLayer(layer, pose);
        }
    }

    // Update world matrices
    pose.UpdateWorldMatrices();
}

void AnimationStateMachine::EvaluateLayer(AnimationLayer& layer, SkeletonInstance& pose) {
    if (layer.states.empty()) {
        return;
    }

    AnimationState& currentState = layer.states[layer.currentStateIndex];

    if (layer.isTransitioning && layer.previousStateIndex >= 0) {
        // Blend between previous and current states
        AnimationState& previousState = layer.states[layer.previousStateIndex];

        float t = layer.transitionTime / layer.transitionDuration;

        // Evaluate previous state
        previousState.blendTree.Evaluate(*this, layer.stateTime, pose);

        // Blend in current state
        currentState.blendTree.Evaluate(*this, layer.stateTime, pose);

        // TODO: Proper blending implementation
    } else {
        // Just evaluate current state
        currentState.blendTree.Evaluate(*this, layer.stateTime, pose);
    }
}

const AnimationState* AnimationStateMachine::GetCurrentState(uint32_t layerIndex) const {
    if (layerIndex >= m_layers.size()) {
        return nullptr;
    }
    const auto& layer = m_layers[layerIndex];
    if (layer.currentStateIndex >= 0 && layer.currentStateIndex < static_cast<int32_t>(layer.states.size())) {
        return &layer.states[layer.currentStateIndex];
    }
    return nullptr;
}

float AnimationStateMachine::GetCurrentStateTime(uint32_t layerIndex) const {
    if (layerIndex >= m_layers.size()) {
        return 0.0f;
    }
    return m_layers[layerIndex].stateTime;
}

float AnimationStateMachine::GetCurrentStateNormalizedTime(uint32_t layerIndex) const {
    if (layerIndex >= m_layers.size()) {
        return 0.0f;
    }
    const auto& layer = m_layers[layerIndex];
    const AnimationState* state = GetCurrentState(layerIndex);
    if (state) {
        float duration = state->GetDuration(*this);
        if (duration > 0.0f) {
            return layer.stateTime / duration;
        }
    }
    return 0.0f;
}

bool AnimationStateMachine::IsTransitioning(uint32_t layerIndex) const {
    if (layerIndex >= m_layers.size()) {
        return false;
    }
    return m_layers[layerIndex].isTransitioning;
}

glm::vec3 AnimationStateMachine::ConsumeRootMotionPosition() {
    glm::vec3 motion = m_rootMotionPosition;
    m_rootMotionPosition = glm::vec3(0.0f);
    return motion;
}

glm::quat AnimationStateMachine::ConsumeRootMotionRotation() {
    glm::quat motion = m_rootMotionRotation;
    m_rootMotionRotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    return motion;
}

// Helper function
AnimationStateMachine CreateLocomotionStateMachine(
    std::shared_ptr<Skeleton> skeleton,
    std::shared_ptr<AnimationClip> idle,
    std::shared_ptr<AnimationClip> walk,
    std::shared_ptr<AnimationClip> run) {

    AnimationStateMachine sm;
    sm.SetSkeleton(skeleton);

    // Add speed parameter
    sm.AddParameter("Speed", 0.0f);

    // Get base layer
    AnimationLayer* layer = sm.GetLayer(0);

    // Add states
    AnimationState idleState("Idle");
    idleState.SetClip(idle);

    AnimationState moveState("Move");
    moveState.blendTree.nodeType = BlendNodeType::Blend1D;
    moveState.blendTree.blendParameter = "Speed";
    moveState.blendTree.children.push_back({"Walk", walk, 0.0f});
    moveState.blendTree.children.push_back({"Run", run, 1.0f});

    int32_t idleIdx = layer->AddState(idleState);
    int32_t moveIdx = layer->AddState(moveState);

    // Add transitions
    StateTransition idleToMove;
    idleToMove.targetStateIndex = moveIdx;
    idleToMove.duration = 0.2f;
    TransitionCondition speedCond;
    speedCond.parameterName = "Speed";
    speedCond.op = ConditionOp::Greater;
    speedCond.compareValue = 0.1f;
    idleToMove.conditions.push_back(speedCond);
    layer->states[idleIdx].transitions.push_back(idleToMove);

    StateTransition moveToIdle;
    moveToIdle.targetStateIndex = idleIdx;
    moveToIdle.duration = 0.2f;
    TransitionCondition slowCond;
    slowCond.parameterName = "Speed";
    slowCond.op = ConditionOp::LessOrEqual;
    slowCond.compareValue = 0.1f;
    moveToIdle.conditions.push_back(slowCond);
    layer->states[moveIdx].transitions.push_back(moveToIdle);

    return sm;
}

} // namespace Cortex::Animation
