#pragma once

// BehaviorTree.h
// Behavior tree for AI decision making.
// Supports composite nodes, decorators, and action/condition leaves.
// Reference: "Behavior Trees in Robotics and AI" - Colledanchise

#include <glm/glm.hpp>
#include <vector>
#include <unordered_map>
#include <memory>
#include <string>
#include <functional>
#include <variant>
#include <any>

namespace Cortex::AI {

// Forward declarations
class BehaviorTree;
class BTNode;
class Blackboard;

// Node execution status
enum class BTStatus {
    Success,
    Failure,
    Running
};

// Blackboard value types
using BlackboardValue = std::variant<
    bool,
    int32_t,
    float,
    std::string,
    glm::vec3,
    glm::quat,
    uint32_t,   // Entity ID
    std::any    // Generic data
>;

// Blackboard for shared AI state
class Blackboard {
public:
    Blackboard() = default;

    // Set values
    void SetBool(const std::string& key, bool value);
    void SetInt(const std::string& key, int32_t value);
    void SetFloat(const std::string& key, float value);
    void SetString(const std::string& key, const std::string& value);
    void SetVector3(const std::string& key, const glm::vec3& value);
    void SetQuat(const std::string& key, const glm::quat& value);
    void SetEntityId(const std::string& key, uint32_t entityId);
    void SetAny(const std::string& key, const std::any& value);

    // Get values
    bool GetBool(const std::string& key, bool defaultValue = false) const;
    int32_t GetInt(const std::string& key, int32_t defaultValue = 0) const;
    float GetFloat(const std::string& key, float defaultValue = 0.0f) const;
    std::string GetString(const std::string& key, const std::string& defaultValue = "") const;
    glm::vec3 GetVector3(const std::string& key, const glm::vec3& defaultValue = glm::vec3(0.0f)) const;
    glm::quat GetQuat(const std::string& key, const glm::quat& defaultValue = glm::quat(1, 0, 0, 0)) const;
    uint32_t GetEntityId(const std::string& key, uint32_t defaultValue = UINT32_MAX) const;

    template<typename T>
    T GetAny(const std::string& key, const T& defaultValue = T()) const {
        auto it = m_data.find(key);
        if (it != m_data.end()) {
            if (auto* anyPtr = std::get_if<std::any>(&it->second)) {
                try {
                    return std::any_cast<T>(*anyPtr);
                } catch (...) {
                    return defaultValue;
                }
            }
        }
        return defaultValue;
    }

    // Check if key exists
    bool Has(const std::string& key) const;

    // Remove key
    void Remove(const std::string& key);

    // Clear all
    void Clear();

    // Copy values from another blackboard
    void CopyFrom(const Blackboard& other);

private:
    std::unordered_map<std::string, BlackboardValue> m_data;
};

// Behavior tree context passed to nodes
struct BTContext {
    float deltaTime = 0.0f;
    Blackboard* blackboard = nullptr;
    void* owner = nullptr;              // Entity or agent pointer
    uint32_t ownerEntityId = UINT32_MAX;
};

// Base behavior tree node
class BTNode {
public:
    BTNode(const std::string& name = "") : m_name(name) {}
    virtual ~BTNode() = default;

    // Execute node
    virtual BTStatus Tick(BTContext& context) = 0;

    // Reset node state (called when parent resets)
    virtual void Reset() {}

    // Node name (for debugging)
    const std::string& GetName() const { return m_name; }
    void SetName(const std::string& name) { m_name = name; }

    // Add child (for composite nodes)
    virtual void AddChild(std::shared_ptr<BTNode> child) {}

protected:
    std::string m_name;
};

// Composite node base
class BTComposite : public BTNode {
public:
    BTComposite(const std::string& name = "") : BTNode(name) {}

    void AddChild(std::shared_ptr<BTNode> child) override {
        m_children.push_back(child);
    }

    void Reset() override {
        for (auto& child : m_children) {
            child->Reset();
        }
    }

protected:
    std::vector<std::shared_ptr<BTNode>> m_children;
};

// Sequence: Execute children in order until one fails
class BTSequence : public BTComposite {
public:
    BTSequence(const std::string& name = "Sequence") : BTComposite(name) {}

    BTStatus Tick(BTContext& context) override;
    void Reset() override;

private:
    size_t m_currentChild = 0;
};

// Selector: Execute children until one succeeds
class BTSelector : public BTComposite {
public:
    BTSelector(const std::string& name = "Selector") : BTComposite(name) {}

    BTStatus Tick(BTContext& context) override;
    void Reset() override;

private:
    size_t m_currentChild = 0;
};

// Parallel: Execute all children simultaneously
class BTParallel : public BTComposite {
public:
    enum class Policy {
        RequireOne,     // Succeed if one child succeeds
        RequireAll      // Succeed if all children succeed
    };

    BTParallel(Policy successPolicy = Policy::RequireOne,
               Policy failurePolicy = Policy::RequireOne,
               const std::string& name = "Parallel")
        : BTComposite(name), m_successPolicy(successPolicy), m_failurePolicy(failurePolicy) {}

    BTStatus Tick(BTContext& context) override;
    void Reset() override;

private:
    Policy m_successPolicy;
    Policy m_failurePolicy;
    std::vector<BTStatus> m_childStatuses;
};

// Random selector: Try children in random order
class BTRandomSelector : public BTComposite {
public:
    BTRandomSelector(const std::string& name = "RandomSelector") : BTComposite(name) {}

    BTStatus Tick(BTContext& context) override;
    void Reset() override;

private:
    std::vector<size_t> m_shuffledOrder;
    size_t m_currentIndex = 0;
    bool m_initialized = false;
};

// Decorator node base
class BTDecorator : public BTNode {
public:
    BTDecorator(const std::string& name = "") : BTNode(name) {}

    void SetChild(std::shared_ptr<BTNode> child) { m_child = child; }
    void AddChild(std::shared_ptr<BTNode> child) override { m_child = child; }

    void Reset() override {
        if (m_child) m_child->Reset();
    }

protected:
    std::shared_ptr<BTNode> m_child;
};

// Inverter: Invert child result
class BTInverter : public BTDecorator {
public:
    BTInverter(const std::string& name = "Inverter") : BTDecorator(name) {}

    BTStatus Tick(BTContext& context) override;
};

// Succeeder: Always return success
class BTSucceeder : public BTDecorator {
public:
    BTSucceeder(const std::string& name = "Succeeder") : BTDecorator(name) {}

    BTStatus Tick(BTContext& context) override;
};

// Failer: Always return failure
class BTFailer : public BTDecorator {
public:
    BTFailer(const std::string& name = "Failer") : BTDecorator(name) {}

    BTStatus Tick(BTContext& context) override;
};

// Repeater: Repeat child N times
class BTRepeater : public BTDecorator {
public:
    BTRepeater(int32_t count = -1, const std::string& name = "Repeater")
        : BTDecorator(name), m_repeatCount(count) {}

    BTStatus Tick(BTContext& context) override;
    void Reset() override;

private:
    int32_t m_repeatCount;      // -1 = infinite
    int32_t m_currentCount = 0;
};

// RepeatUntilFail: Repeat until child fails
class BTRepeatUntilFail : public BTDecorator {
public:
    BTRepeatUntilFail(const std::string& name = "RepeatUntilFail") : BTDecorator(name) {}

    BTStatus Tick(BTContext& context) override;
};

// Cooldown: Prevent re-execution for duration
class BTCooldown : public BTDecorator {
public:
    BTCooldown(float duration, const std::string& name = "Cooldown")
        : BTDecorator(name), m_duration(duration) {}

    BTStatus Tick(BTContext& context) override;
    void Reset() override;

private:
    float m_duration;
    float m_timeSinceLastRun = FLT_MAX;
    bool m_isRunning = false;
};

// TimeLimit: Fail if child takes too long
class BTTimeLimit : public BTDecorator {
public:
    BTTimeLimit(float limit, const std::string& name = "TimeLimit")
        : BTDecorator(name), m_timeLimit(limit) {}

    BTStatus Tick(BTContext& context) override;
    void Reset() override;

private:
    float m_timeLimit;
    float m_elapsedTime = 0.0f;
    bool m_isRunning = false;
};

// Condition decorator: Only run child if condition is true
class BTCondition : public BTDecorator {
public:
    using ConditionFunc = std::function<bool(BTContext&)>;

    BTCondition(ConditionFunc condition, const std::string& name = "Condition")
        : BTDecorator(name), m_condition(condition) {}

    BTStatus Tick(BTContext& context) override;

private:
    ConditionFunc m_condition;
};

// Action leaf node
class BTAction : public BTNode {
public:
    using ActionFunc = std::function<BTStatus(BTContext&)>;

    BTAction(ActionFunc action, const std::string& name = "Action")
        : BTNode(name), m_action(action) {}

    BTStatus Tick(BTContext& context) override;

private:
    ActionFunc m_action;
};

// Wait action: Wait for duration
class BTWait : public BTNode {
public:
    BTWait(float duration, const std::string& name = "Wait")
        : BTNode(name), m_duration(duration) {}

    BTStatus Tick(BTContext& context) override;
    void Reset() override;

private:
    float m_duration;
    float m_elapsedTime = 0.0f;
};

// Set blackboard value action
class BTSetBlackboard : public BTNode {
public:
    BTSetBlackboard(const std::string& key, BlackboardValue value,
                    const std::string& name = "SetBlackboard")
        : BTNode(name), m_key(key), m_value(value) {}

    BTStatus Tick(BTContext& context) override;

private:
    std::string m_key;
    BlackboardValue m_value;
};

// Check blackboard condition
class BTCheckBlackboard : public BTNode {
public:
    enum class Comparison {
        Equals,
        NotEquals,
        Greater,
        Less,
        GreaterOrEqual,
        LessOrEqual,
        Exists,
        NotExists
    };

    BTCheckBlackboard(const std::string& key, Comparison comp,
                      BlackboardValue compareValue = BlackboardValue(),
                      const std::string& name = "CheckBlackboard")
        : BTNode(name), m_key(key), m_comparison(comp), m_compareValue(compareValue) {}

    BTStatus Tick(BTContext& context) override;

private:
    std::string m_key;
    Comparison m_comparison;
    BlackboardValue m_compareValue;
};

// Behavior tree root
class BehaviorTree {
public:
    BehaviorTree() = default;
    explicit BehaviorTree(std::shared_ptr<BTNode> root);

    // Set root node
    void SetRoot(std::shared_ptr<BTNode> root);

    // Update tree
    BTStatus Tick(float deltaTime, void* owner = nullptr, uint32_t entityId = UINT32_MAX);

    // Reset tree
    void Reset();

    // Blackboard access
    Blackboard& GetBlackboard() { return m_blackboard; }
    const Blackboard& GetBlackboard() const { return m_blackboard; }

    // Get last status
    BTStatus GetLastStatus() const { return m_lastStatus; }

    // Debug info
    const std::string& GetCurrentNodeName() const { return m_currentNodeName; }

private:
    std::shared_ptr<BTNode> m_root;
    Blackboard m_blackboard;
    BTStatus m_lastStatus = BTStatus::Success;
    std::string m_currentNodeName;
};

// Builder for fluent tree construction
class BTBuilder {
public:
    BTBuilder() = default;

    // Composite nodes
    BTBuilder& Sequence(const std::string& name = "Sequence");
    BTBuilder& Selector(const std::string& name = "Selector");
    BTBuilder& Parallel(BTParallel::Policy success = BTParallel::Policy::RequireOne,
                        BTParallel::Policy failure = BTParallel::Policy::RequireOne,
                        const std::string& name = "Parallel");
    BTBuilder& RandomSelector(const std::string& name = "RandomSelector");

    // Decorators
    BTBuilder& Inverter(const std::string& name = "Inverter");
    BTBuilder& Succeeder(const std::string& name = "Succeeder");
    BTBuilder& Failer(const std::string& name = "Failer");
    BTBuilder& Repeater(int32_t count = -1, const std::string& name = "Repeater");
    BTBuilder& RepeatUntilFail(const std::string& name = "RepeatUntilFail");
    BTBuilder& Cooldown(float duration, const std::string& name = "Cooldown");
    BTBuilder& TimeLimit(float limit, const std::string& name = "TimeLimit");
    BTBuilder& Condition(BTCondition::ConditionFunc func, const std::string& name = "Condition");

    // Leaf nodes
    BTBuilder& Action(BTAction::ActionFunc func, const std::string& name = "Action");
    BTBuilder& Wait(float duration, const std::string& name = "Wait");
    BTBuilder& SetBlackboard(const std::string& key, BlackboardValue value);
    BTBuilder& CheckBlackboard(const std::string& key, BTCheckBlackboard::Comparison comp,
                                BlackboardValue value = BlackboardValue());

    // Tree structure
    BTBuilder& End();   // End current composite/decorator
    BTBuilder& Back();  // Go back to parent

    // Build final tree
    std::shared_ptr<BehaviorTree> Build();

private:
    struct BuilderNode {
        std::shared_ptr<BTNode> node;
        int parentIndex = -1;
    };

    std::vector<BuilderNode> m_nodes;
    int m_currentParent = -1;

    void AddNode(std::shared_ptr<BTNode> node);
};

} // namespace Cortex::AI
