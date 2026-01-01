// BehaviorTree.cpp
// Implementation of behavior tree nodes and execution.

#include "BehaviorTree.h"
#include <algorithm>
#include <random>
#include <chrono>

namespace Cortex::AI {

// ============================================================================
// Blackboard Implementation
// ============================================================================

void Blackboard::SetBool(const std::string& key, bool value) {
    m_data[key] = value;
}

void Blackboard::SetInt(const std::string& key, int32_t value) {
    m_data[key] = value;
}

void Blackboard::SetFloat(const std::string& key, float value) {
    m_data[key] = value;
}

void Blackboard::SetString(const std::string& key, const std::string& value) {
    m_data[key] = value;
}

void Blackboard::SetVector3(const std::string& key, const glm::vec3& value) {
    m_data[key] = value;
}

void Blackboard::SetQuat(const std::string& key, const glm::quat& value) {
    m_data[key] = value;
}

void Blackboard::SetEntityId(const std::string& key, uint32_t entityId) {
    m_data[key] = entityId;
}

void Blackboard::SetAny(const std::string& key, const std::any& value) {
    m_data[key] = value;
}

bool Blackboard::GetBool(const std::string& key, bool defaultValue) const {
    auto it = m_data.find(key);
    if (it != m_data.end()) {
        if (auto* val = std::get_if<bool>(&it->second)) {
            return *val;
        }
    }
    return defaultValue;
}

int32_t Blackboard::GetInt(const std::string& key, int32_t defaultValue) const {
    auto it = m_data.find(key);
    if (it != m_data.end()) {
        if (auto* val = std::get_if<int32_t>(&it->second)) {
            return *val;
        }
    }
    return defaultValue;
}

float Blackboard::GetFloat(const std::string& key, float defaultValue) const {
    auto it = m_data.find(key);
    if (it != m_data.end()) {
        if (auto* val = std::get_if<float>(&it->second)) {
            return *val;
        }
    }
    return defaultValue;
}

std::string Blackboard::GetString(const std::string& key, const std::string& defaultValue) const {
    auto it = m_data.find(key);
    if (it != m_data.end()) {
        if (auto* val = std::get_if<std::string>(&it->second)) {
            return *val;
        }
    }
    return defaultValue;
}

glm::vec3 Blackboard::GetVector3(const std::string& key, const glm::vec3& defaultValue) const {
    auto it = m_data.find(key);
    if (it != m_data.end()) {
        if (auto* val = std::get_if<glm::vec3>(&it->second)) {
            return *val;
        }
    }
    return defaultValue;
}

glm::quat Blackboard::GetQuat(const std::string& key, const glm::quat& defaultValue) const {
    auto it = m_data.find(key);
    if (it != m_data.end()) {
        if (auto* val = std::get_if<glm::quat>(&it->second)) {
            return *val;
        }
    }
    return defaultValue;
}

uint32_t Blackboard::GetEntityId(const std::string& key, uint32_t defaultValue) const {
    auto it = m_data.find(key);
    if (it != m_data.end()) {
        if (auto* val = std::get_if<uint32_t>(&it->second)) {
            return *val;
        }
    }
    return defaultValue;
}

bool Blackboard::Has(const std::string& key) const {
    return m_data.find(key) != m_data.end();
}

void Blackboard::Remove(const std::string& key) {
    m_data.erase(key);
}

void Blackboard::Clear() {
    m_data.clear();
}

void Blackboard::CopyFrom(const Blackboard& other) {
    for (const auto& [key, value] : other.m_data) {
        m_data[key] = value;
    }
}

// ============================================================================
// Composite Nodes
// ============================================================================

BTStatus BTSequence::Tick(BTContext& context) {
    // Execute children in order until one fails or returns Running
    while (m_currentChild < m_children.size()) {
        BTStatus status = m_children[m_currentChild]->Tick(context);

        if (status == BTStatus::Running) {
            return BTStatus::Running;
        }

        if (status == BTStatus::Failure) {
            m_currentChild = 0;
            return BTStatus::Failure;
        }

        // Success - move to next child
        m_currentChild++;
    }

    // All children succeeded
    m_currentChild = 0;
    return BTStatus::Success;
}

void BTSequence::Reset() {
    m_currentChild = 0;
    BTComposite::Reset();
}

BTStatus BTSelector::Tick(BTContext& context) {
    // Execute children until one succeeds or returns Running
    while (m_currentChild < m_children.size()) {
        BTStatus status = m_children[m_currentChild]->Tick(context);

        if (status == BTStatus::Running) {
            return BTStatus::Running;
        }

        if (status == BTStatus::Success) {
            m_currentChild = 0;
            return BTStatus::Success;
        }

        // Failure - try next child
        m_currentChild++;
    }

    // All children failed
    m_currentChild = 0;
    return BTStatus::Failure;
}

void BTSelector::Reset() {
    m_currentChild = 0;
    BTComposite::Reset();
}

BTStatus BTParallel::Tick(BTContext& context) {
    // Initialize status tracking on first tick
    if (m_childStatuses.size() != m_children.size()) {
        m_childStatuses.resize(m_children.size(), BTStatus::Running);
    }

    uint32_t successCount = 0;
    uint32_t failureCount = 0;
    bool anyRunning = false;

    // Execute all children
    for (size_t i = 0; i < m_children.size(); i++) {
        // Skip already completed children
        if (m_childStatuses[i] != BTStatus::Running) {
            if (m_childStatuses[i] == BTStatus::Success) successCount++;
            else failureCount++;
            continue;
        }

        BTStatus status = m_children[i]->Tick(context);
        m_childStatuses[i] = status;

        switch (status) {
            case BTStatus::Success:
                successCount++;
                break;
            case BTStatus::Failure:
                failureCount++;
                break;
            case BTStatus::Running:
                anyRunning = true;
                break;
        }
    }

    // Check success policy
    if (m_successPolicy == Policy::RequireOne && successCount > 0) {
        Reset();
        return BTStatus::Success;
    }
    if (m_successPolicy == Policy::RequireAll && successCount == m_children.size()) {
        Reset();
        return BTStatus::Success;
    }

    // Check failure policy
    if (m_failurePolicy == Policy::RequireOne && failureCount > 0) {
        Reset();
        return BTStatus::Failure;
    }
    if (m_failurePolicy == Policy::RequireAll && failureCount == m_children.size()) {
        Reset();
        return BTStatus::Failure;
    }

    // Still running
    if (anyRunning) {
        return BTStatus::Running;
    }

    // All finished - default based on policy
    Reset();
    return (successCount > 0) ? BTStatus::Success : BTStatus::Failure;
}

void BTParallel::Reset() {
    m_childStatuses.clear();
    BTComposite::Reset();
}

BTStatus BTRandomSelector::Tick(BTContext& context) {
    // Initialize shuffled order on first tick
    if (!m_initialized) {
        m_shuffledOrder.resize(m_children.size());
        for (size_t i = 0; i < m_children.size(); i++) {
            m_shuffledOrder[i] = i;
        }

        // Shuffle using Fisher-Yates
        static std::mt19937 rng(static_cast<uint32_t>(
            std::chrono::steady_clock::now().time_since_epoch().count()));

        for (size_t i = m_children.size() - 1; i > 0; i--) {
            std::uniform_int_distribution<size_t> dist(0, i);
            std::swap(m_shuffledOrder[i], m_shuffledOrder[dist(rng)]);
        }

        m_initialized = true;
        m_currentIndex = 0;
    }

    // Execute children in shuffled order until one succeeds
    while (m_currentIndex < m_shuffledOrder.size()) {
        size_t childIndex = m_shuffledOrder[m_currentIndex];
        BTStatus status = m_children[childIndex]->Tick(context);

        if (status == BTStatus::Running) {
            return BTStatus::Running;
        }

        if (status == BTStatus::Success) {
            Reset();
            return BTStatus::Success;
        }

        m_currentIndex++;
    }

    // All children failed
    Reset();
    return BTStatus::Failure;
}

void BTRandomSelector::Reset() {
    m_initialized = false;
    m_currentIndex = 0;
    BTComposite::Reset();
}

// ============================================================================
// Decorator Nodes
// ============================================================================

BTStatus BTInverter::Tick(BTContext& context) {
    if (!m_child) return BTStatus::Failure;

    BTStatus status = m_child->Tick(context);

    switch (status) {
        case BTStatus::Success:
            return BTStatus::Failure;
        case BTStatus::Failure:
            return BTStatus::Success;
        case BTStatus::Running:
        default:
            return BTStatus::Running;
    }
}

BTStatus BTSucceeder::Tick(BTContext& context) {
    if (!m_child) return BTStatus::Success;

    BTStatus status = m_child->Tick(context);

    if (status == BTStatus::Running) {
        return BTStatus::Running;
    }

    return BTStatus::Success;
}

BTStatus BTFailer::Tick(BTContext& context) {
    if (!m_child) return BTStatus::Failure;

    BTStatus status = m_child->Tick(context);

    if (status == BTStatus::Running) {
        return BTStatus::Running;
    }

    return BTStatus::Failure;
}

BTStatus BTRepeater::Tick(BTContext& context) {
    if (!m_child) return BTStatus::Failure;

    // Infinite repeat
    if (m_repeatCount < 0) {
        BTStatus status = m_child->Tick(context);

        if (status == BTStatus::Running) {
            return BTStatus::Running;
        }

        // Reset child and continue
        m_child->Reset();
        return BTStatus::Running;
    }

    // Limited repeat
    while (m_currentCount < m_repeatCount) {
        BTStatus status = m_child->Tick(context);

        if (status == BTStatus::Running) {
            return BTStatus::Running;
        }

        if (status == BTStatus::Failure) {
            Reset();
            return BTStatus::Failure;
        }

        // Success - increment and reset child
        m_currentCount++;
        m_child->Reset();
    }

    Reset();
    return BTStatus::Success;
}

void BTRepeater::Reset() {
    m_currentCount = 0;
    BTDecorator::Reset();
}

BTStatus BTRepeatUntilFail::Tick(BTContext& context) {
    if (!m_child) return BTStatus::Success;

    BTStatus status = m_child->Tick(context);

    if (status == BTStatus::Running) {
        return BTStatus::Running;
    }

    if (status == BTStatus::Failure) {
        return BTStatus::Success;  // RepeatUntilFail returns success when child fails
    }

    // Success - reset child and continue
    m_child->Reset();
    return BTStatus::Running;
}

BTStatus BTCooldown::Tick(BTContext& context) {
    if (!m_child) return BTStatus::Failure;

    // Update cooldown timer
    if (!m_isRunning) {
        m_timeSinceLastRun += context.deltaTime;

        if (m_timeSinceLastRun < m_duration) {
            return BTStatus::Failure;  // Still on cooldown
        }
    }

    // Execute child
    m_isRunning = true;
    BTStatus status = m_child->Tick(context);

    if (status == BTStatus::Running) {
        return BTStatus::Running;
    }

    // Child completed - start cooldown
    m_isRunning = false;
    m_timeSinceLastRun = 0.0f;
    m_child->Reset();

    return status;
}

void BTCooldown::Reset() {
    m_timeSinceLastRun = FLT_MAX;  // Allow immediate execution after reset
    m_isRunning = false;
    BTDecorator::Reset();
}

BTStatus BTTimeLimit::Tick(BTContext& context) {
    if (!m_child) return BTStatus::Failure;

    if (!m_isRunning) {
        m_isRunning = true;
        m_elapsedTime = 0.0f;
    }

    m_elapsedTime += context.deltaTime;

    if (m_elapsedTime >= m_timeLimit) {
        Reset();
        return BTStatus::Failure;  // Time limit exceeded
    }

    BTStatus status = m_child->Tick(context);

    if (status != BTStatus::Running) {
        Reset();
    }

    return status;
}

void BTTimeLimit::Reset() {
    m_elapsedTime = 0.0f;
    m_isRunning = false;
    BTDecorator::Reset();
}

BTStatus BTCondition::Tick(BTContext& context) {
    if (!m_condition) return BTStatus::Failure;

    if (!m_condition(context)) {
        return BTStatus::Failure;
    }

    if (!m_child) {
        return BTStatus::Success;
    }

    return m_child->Tick(context);
}

// ============================================================================
// Leaf Nodes
// ============================================================================

BTStatus BTAction::Tick(BTContext& context) {
    if (!m_action) return BTStatus::Failure;
    return m_action(context);
}

BTStatus BTWait::Tick(BTContext& context) {
    m_elapsedTime += context.deltaTime;

    if (m_elapsedTime >= m_duration) {
        m_elapsedTime = 0.0f;
        return BTStatus::Success;
    }

    return BTStatus::Running;
}

void BTWait::Reset() {
    m_elapsedTime = 0.0f;
    BTNode::Reset();
}

BTStatus BTSetBlackboard::Tick(BTContext& context) {
    if (!context.blackboard) return BTStatus::Failure;

    // Set value based on variant type
    std::visit([&](auto&& arg) {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, bool>) {
            context.blackboard->SetBool(m_key, arg);
        } else if constexpr (std::is_same_v<T, int32_t>) {
            context.blackboard->SetInt(m_key, arg);
        } else if constexpr (std::is_same_v<T, float>) {
            context.blackboard->SetFloat(m_key, arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
            context.blackboard->SetString(m_key, arg);
        } else if constexpr (std::is_same_v<T, glm::vec3>) {
            context.blackboard->SetVector3(m_key, arg);
        } else if constexpr (std::is_same_v<T, glm::quat>) {
            context.blackboard->SetQuat(m_key, arg);
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            context.blackboard->SetEntityId(m_key, arg);
        } else if constexpr (std::is_same_v<T, std::any>) {
            context.blackboard->SetAny(m_key, arg);
        }
    }, m_value);

    return BTStatus::Success;
}

BTStatus BTCheckBlackboard::Tick(BTContext& context) {
    if (!context.blackboard) return BTStatus::Failure;

    // Handle existence checks
    if (m_comparison == Comparison::Exists) {
        return context.blackboard->Has(m_key) ? BTStatus::Success : BTStatus::Failure;
    }
    if (m_comparison == Comparison::NotExists) {
        return !context.blackboard->Has(m_key) ? BTStatus::Success : BTStatus::Failure;
    }

    if (!context.blackboard->Has(m_key)) {
        return BTStatus::Failure;
    }

    // Compare based on value type
    bool result = std::visit([&](auto&& compareArg) -> bool {
        using T = std::decay_t<decltype(compareArg)>;

        if constexpr (std::is_same_v<T, bool>) {
            bool val = context.blackboard->GetBool(m_key);
            switch (m_comparison) {
                case Comparison::Equals: return val == compareArg;
                case Comparison::NotEquals: return val != compareArg;
                default: return false;
            }
        } else if constexpr (std::is_same_v<T, int32_t>) {
            int32_t val = context.blackboard->GetInt(m_key);
            switch (m_comparison) {
                case Comparison::Equals: return val == compareArg;
                case Comparison::NotEquals: return val != compareArg;
                case Comparison::Greater: return val > compareArg;
                case Comparison::Less: return val < compareArg;
                case Comparison::GreaterOrEqual: return val >= compareArg;
                case Comparison::LessOrEqual: return val <= compareArg;
                default: return false;
            }
        } else if constexpr (std::is_same_v<T, float>) {
            float val = context.blackboard->GetFloat(m_key);
            switch (m_comparison) {
                case Comparison::Equals: return std::abs(val - compareArg) < 0.0001f;
                case Comparison::NotEquals: return std::abs(val - compareArg) >= 0.0001f;
                case Comparison::Greater: return val > compareArg;
                case Comparison::Less: return val < compareArg;
                case Comparison::GreaterOrEqual: return val >= compareArg;
                case Comparison::LessOrEqual: return val <= compareArg;
                default: return false;
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            std::string val = context.blackboard->GetString(m_key);
            switch (m_comparison) {
                case Comparison::Equals: return val == compareArg;
                case Comparison::NotEquals: return val != compareArg;
                default: return false;
            }
        } else if constexpr (std::is_same_v<T, uint32_t>) {
            uint32_t val = context.blackboard->GetEntityId(m_key);
            switch (m_comparison) {
                case Comparison::Equals: return val == compareArg;
                case Comparison::NotEquals: return val != compareArg;
                default: return false;
            }
        } else {
            return false;
        }
    }, m_compareValue);

    return result ? BTStatus::Success : BTStatus::Failure;
}

// ============================================================================
// BehaviorTree
// ============================================================================

BehaviorTree::BehaviorTree(std::shared_ptr<BTNode> root)
    : m_root(root) {}

void BehaviorTree::SetRoot(std::shared_ptr<BTNode> root) {
    m_root = root;
}

BTStatus BehaviorTree::Tick(float deltaTime, void* owner, uint32_t entityId) {
    if (!m_root) {
        m_lastStatus = BTStatus::Failure;
        return m_lastStatus;
    }

    BTContext context;
    context.deltaTime = deltaTime;
    context.blackboard = &m_blackboard;
    context.owner = owner;
    context.ownerEntityId = entityId;

    m_lastStatus = m_root->Tick(context);
    m_currentNodeName = m_root->GetName();

    return m_lastStatus;
}

void BehaviorTree::Reset() {
    if (m_root) {
        m_root->Reset();
    }
    m_lastStatus = BTStatus::Success;
}

// ============================================================================
// BTBuilder
// ============================================================================

void BTBuilder::AddNode(std::shared_ptr<BTNode> node) {
    BuilderNode builderNode;
    builderNode.node = node;
    builderNode.parentIndex = m_currentParent;

    int nodeIndex = static_cast<int>(m_nodes.size());
    m_nodes.push_back(builderNode);

    // Add to parent if exists
    if (m_currentParent >= 0) {
        m_nodes[m_currentParent].node->AddChild(node);
    }
}

BTBuilder& BTBuilder::Sequence(const std::string& name) {
    auto node = std::make_shared<BTSequence>(name);
    AddNode(node);
    m_currentParent = static_cast<int>(m_nodes.size()) - 1;
    return *this;
}

BTBuilder& BTBuilder::Selector(const std::string& name) {
    auto node = std::make_shared<BTSelector>(name);
    AddNode(node);
    m_currentParent = static_cast<int>(m_nodes.size()) - 1;
    return *this;
}

BTBuilder& BTBuilder::Parallel(BTParallel::Policy success, BTParallel::Policy failure,
                                const std::string& name) {
    auto node = std::make_shared<BTParallel>(success, failure, name);
    AddNode(node);
    m_currentParent = static_cast<int>(m_nodes.size()) - 1;
    return *this;
}

BTBuilder& BTBuilder::RandomSelector(const std::string& name) {
    auto node = std::make_shared<BTRandomSelector>(name);
    AddNode(node);
    m_currentParent = static_cast<int>(m_nodes.size()) - 1;
    return *this;
}

BTBuilder& BTBuilder::Inverter(const std::string& name) {
    auto node = std::make_shared<BTInverter>(name);
    AddNode(node);
    m_currentParent = static_cast<int>(m_nodes.size()) - 1;
    return *this;
}

BTBuilder& BTBuilder::Succeeder(const std::string& name) {
    auto node = std::make_shared<BTSucceeder>(name);
    AddNode(node);
    m_currentParent = static_cast<int>(m_nodes.size()) - 1;
    return *this;
}

BTBuilder& BTBuilder::Failer(const std::string& name) {
    auto node = std::make_shared<BTFailer>(name);
    AddNode(node);
    m_currentParent = static_cast<int>(m_nodes.size()) - 1;
    return *this;
}

BTBuilder& BTBuilder::Repeater(int32_t count, const std::string& name) {
    auto node = std::make_shared<BTRepeater>(count, name);
    AddNode(node);
    m_currentParent = static_cast<int>(m_nodes.size()) - 1;
    return *this;
}

BTBuilder& BTBuilder::RepeatUntilFail(const std::string& name) {
    auto node = std::make_shared<BTRepeatUntilFail>(name);
    AddNode(node);
    m_currentParent = static_cast<int>(m_nodes.size()) - 1;
    return *this;
}

BTBuilder& BTBuilder::Cooldown(float duration, const std::string& name) {
    auto node = std::make_shared<BTCooldown>(duration, name);
    AddNode(node);
    m_currentParent = static_cast<int>(m_nodes.size()) - 1;
    return *this;
}

BTBuilder& BTBuilder::TimeLimit(float limit, const std::string& name) {
    auto node = std::make_shared<BTTimeLimit>(limit, name);
    AddNode(node);
    m_currentParent = static_cast<int>(m_nodes.size()) - 1;
    return *this;
}

BTBuilder& BTBuilder::Condition(BTCondition::ConditionFunc func, const std::string& name) {
    auto node = std::make_shared<BTCondition>(func, name);
    AddNode(node);
    m_currentParent = static_cast<int>(m_nodes.size()) - 1;
    return *this;
}

BTBuilder& BTBuilder::Action(BTAction::ActionFunc func, const std::string& name) {
    auto node = std::make_shared<BTAction>(func, name);
    AddNode(node);
    // Actions are leaves - don't update parent
    return *this;
}

BTBuilder& BTBuilder::Wait(float duration, const std::string& name) {
    auto node = std::make_shared<BTWait>(duration, name);
    AddNode(node);
    return *this;
}

BTBuilder& BTBuilder::SetBlackboard(const std::string& key, BlackboardValue value) {
    auto node = std::make_shared<BTSetBlackboard>(key, value);
    AddNode(node);
    return *this;
}

BTBuilder& BTBuilder::CheckBlackboard(const std::string& key, BTCheckBlackboard::Comparison comp,
                                       BlackboardValue value) {
    auto node = std::make_shared<BTCheckBlackboard>(key, comp, value);
    AddNode(node);
    return *this;
}

BTBuilder& BTBuilder::End() {
    if (m_currentParent >= 0) {
        m_currentParent = m_nodes[m_currentParent].parentIndex;
    }
    return *this;
}

BTBuilder& BTBuilder::Back() {
    return End();
}

std::shared_ptr<BehaviorTree> BTBuilder::Build() {
    if (m_nodes.empty()) {
        return std::make_shared<BehaviorTree>();
    }

    return std::make_shared<BehaviorTree>(m_nodes[0].node);
}

} // namespace Cortex::AI
