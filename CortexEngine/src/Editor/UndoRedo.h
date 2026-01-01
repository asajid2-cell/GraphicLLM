// UndoRedo.h
// Command pattern-based undo/redo system for editor operations.

#pragma once

#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <stack>
#include <deque>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

// Forward declare ECS types
namespace entt { class registry; }
using Entity = uint32_t;

namespace Cortex::Editor {

// ============================================================================
// Command Interface
// ============================================================================

class ICommand {
public:
    virtual ~ICommand() = default;

    // Execute the command
    virtual void Execute() = 0;

    // Undo the command
    virtual void Undo() = 0;

    // Redo the command (default: just Execute again)
    virtual void Redo() { Execute(); }

    // Get description for UI
    virtual std::string GetDescription() const = 0;

    // Can this command be merged with previous? (e.g., consecutive transforms)
    virtual bool CanMerge(const ICommand* other) const { (void)other; return false; }

    // Merge with previous command
    virtual void Merge(const ICommand* other) { (void)other; }

    // Get memory size for memory management
    virtual size_t GetMemorySize() const { return sizeof(*this); }

    // Get affected entities (for selection preservation)
    virtual std::vector<Entity> GetAffectedEntities() const { return {}; }
};

// ============================================================================
// Undo Manager
// ============================================================================

class UndoManager {
public:
    UndoManager();
    ~UndoManager();

    // Execute a command and add to undo stack
    void Execute(std::unique_ptr<ICommand> command);

    // Execute without adding to undo stack (dangerous, use sparingly)
    void ExecuteWithoutUndo(std::unique_ptr<ICommand> command);

    // Undo/Redo
    bool CanUndo() const { return !m_undoStack.empty(); }
    bool CanRedo() const { return !m_redoStack.empty(); }

    void Undo();
    void Redo();

    // Undo/Redo multiple
    void UndoMultiple(size_t count);
    void RedoMultiple(size_t count);

    // Get descriptions
    std::string GetUndoDescription() const;
    std::string GetRedoDescription() const;
    std::vector<std::string> GetUndoHistory(size_t maxCount = 10) const;
    std::vector<std::string> GetRedoHistory(size_t maxCount = 10) const;

    // Clear history
    void Clear();
    void ClearRedo();

    // Group commands (all commands in group undo/redo together)
    void BeginGroup(const std::string& description);
    void EndGroup();
    bool IsInGroup() const { return m_groupDepth > 0; }

    // Mark current state as saved
    void MarkSaved();
    bool IsModified() const { return m_savedPosition != m_undoStack.size(); }

    // Configuration
    void SetMaxUndoLevels(size_t levels) { m_maxUndoLevels = levels; }
    size_t GetMaxUndoLevels() const { return m_maxUndoLevels; }

    void SetMaxMemoryUsage(size_t bytes) { m_maxMemoryUsage = bytes; }
    size_t GetMaxMemoryUsage() const { return m_maxMemoryUsage; }

    // Statistics
    size_t GetUndoCount() const { return m_undoStack.size(); }
    size_t GetRedoCount() const { return m_redoStack.size(); }
    size_t GetMemoryUsage() const { return m_currentMemoryUsage; }

    // Callbacks
    void SetOnStateChanged(std::function<void()> callback) { m_onStateChanged = callback; }

private:
    void TrimHistory();
    void UpdateMemoryUsage();

    std::deque<std::unique_ptr<ICommand>> m_undoStack;
    std::deque<std::unique_ptr<ICommand>> m_redoStack;

    // Command grouping
    int m_groupDepth = 0;
    std::unique_ptr<class CommandGroup> m_currentGroup;

    // Configuration
    size_t m_maxUndoLevels = 100;
    size_t m_maxMemoryUsage = 100 * 1024 * 1024;  // 100 MB
    size_t m_currentMemoryUsage = 0;

    // Save tracking
    size_t m_savedPosition = 0;

    // Callbacks
    std::function<void()> m_onStateChanged;
};

// ============================================================================
// Command Group (multiple commands as one undo step)
// ============================================================================

class CommandGroup : public ICommand {
public:
    CommandGroup(const std::string& description);
    ~CommandGroup() override = default;

    void AddCommand(std::unique_ptr<ICommand> command);

    void Execute() override;
    void Undo() override;
    void Redo() override;
    std::string GetDescription() const override { return m_description; }
    size_t GetMemorySize() const override;
    std::vector<Entity> GetAffectedEntities() const override;

    bool IsEmpty() const { return m_commands.empty(); }

private:
    std::string m_description;
    std::vector<std::unique_ptr<ICommand>> m_commands;
};

// ============================================================================
// Common Editor Commands
// ============================================================================

// Transform modification command
class TransformCommand : public ICommand {
public:
    struct TransformData {
        glm::vec3 position;
        glm::quat rotation;
        glm::vec3 scale;
    };

    TransformCommand(entt::registry* registry, Entity entity,
                      const TransformData& oldTransform,
                      const TransformData& newTransform);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override;
    bool CanMerge(const ICommand* other) const override;
    void Merge(const ICommand* other) override;
    std::vector<Entity> GetAffectedEntities() const override { return {m_entity}; }

private:
    entt::registry* m_registry;
    Entity m_entity;
    TransformData m_oldTransform;
    TransformData m_newTransform;
};

// Multi-entity transform command
class MultiTransformCommand : public ICommand {
public:
    MultiTransformCommand(entt::registry* registry,
                           const std::vector<Entity>& entities,
                           const std::vector<TransformCommand::TransformData>& oldTransforms,
                           const std::vector<TransformCommand::TransformData>& newTransforms);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override;
    std::vector<Entity> GetAffectedEntities() const override { return m_entities; }

private:
    entt::registry* m_registry;
    std::vector<Entity> m_entities;
    std::vector<TransformCommand::TransformData> m_oldTransforms;
    std::vector<TransformCommand::TransformData> m_newTransforms;
};

// Create entity command
class CreateEntityCommand : public ICommand {
public:
    CreateEntityCommand(entt::registry* registry, const std::string& name,
                         Entity parent = 0);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override { return "Create Entity: " + m_name; }
    std::vector<Entity> GetAffectedEntities() const override { return {m_createdEntity}; }

    Entity GetCreatedEntity() const { return m_createdEntity; }

private:
    entt::registry* m_registry;
    std::string m_name;
    Entity m_parent;
    Entity m_createdEntity = 0;
    bool m_executed = false;
};

// Delete entity command
class DeleteEntityCommand : public ICommand {
public:
    DeleteEntityCommand(entt::registry* registry, Entity entity);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override { return "Delete Entity: " + m_name; }
    std::vector<Entity> GetAffectedEntities() const override { return {m_entity}; }

private:
    struct EntitySnapshot {
        std::string name;
        std::string tag;
        glm::vec3 position;
        glm::quat rotation;
        glm::vec3 scale;
        Entity parent;
        std::vector<std::pair<std::string, std::vector<uint8_t>>> components;
    };

    void SnapshotEntity(Entity entity, EntitySnapshot& snapshot);
    Entity RestoreEntity(const EntitySnapshot& snapshot);

    entt::registry* m_registry;
    Entity m_entity;
    std::string m_name;
    std::vector<EntitySnapshot> m_snapshots;  // Entity + children
    std::vector<Entity> m_deletedEntities;
};

// Duplicate entity command
class DuplicateEntityCommand : public ICommand {
public:
    DuplicateEntityCommand(entt::registry* registry, Entity entity);
    DuplicateEntityCommand(entt::registry* registry, const std::vector<Entity>& entities);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override;
    std::vector<Entity> GetAffectedEntities() const override;

    std::vector<Entity> GetDuplicatedEntities() const { return m_duplicatedEntities; }

private:
    Entity DuplicateEntityRecursive(Entity source, Entity parent);

    entt::registry* m_registry;
    std::vector<Entity> m_sourceEntities;
    std::vector<Entity> m_duplicatedEntities;
};

// Reparent entity command
class ReparentEntityCommand : public ICommand {
public:
    ReparentEntityCommand(entt::registry* registry, Entity entity, Entity newParent);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override { return "Reparent Entity"; }
    std::vector<Entity> GetAffectedEntities() const override { return {m_entity}; }

private:
    entt::registry* m_registry;
    Entity m_entity;
    Entity m_oldParent;
    Entity m_newParent;
    int m_oldSiblingIndex;
};

// Rename entity command
class RenameEntityCommand : public ICommand {
public:
    RenameEntityCommand(entt::registry* registry, Entity entity,
                         const std::string& oldName, const std::string& newName);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override { return "Rename to: " + m_newName; }
    bool CanMerge(const ICommand* other) const override;
    void Merge(const ICommand* other) override;
    std::vector<Entity> GetAffectedEntities() const override { return {m_entity}; }

private:
    entt::registry* m_registry;
    Entity m_entity;
    std::string m_oldName;
    std::string m_newName;
};

// Component modification command
class ComponentModifyCommand : public ICommand {
public:
    ComponentModifyCommand(entt::registry* registry, Entity entity,
                            const std::string& componentType,
                            const std::string& propertyName,
                            const std::vector<uint8_t>& oldValue,
                            const std::vector<uint8_t>& newValue);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override;
    bool CanMerge(const ICommand* other) const override;
    void Merge(const ICommand* other) override;
    std::vector<Entity> GetAffectedEntities() const override { return {m_entity}; }

private:
    entt::registry* m_registry;
    Entity m_entity;
    std::string m_componentType;
    std::string m_propertyName;
    std::vector<uint8_t> m_oldValue;
    std::vector<uint8_t> m_newValue;
};

// Add component command
class AddComponentCommand : public ICommand {
public:
    AddComponentCommand(entt::registry* registry, Entity entity,
                         const std::string& componentType);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override { return "Add " + m_componentType; }
    std::vector<Entity> GetAffectedEntities() const override { return {m_entity}; }

private:
    entt::registry* m_registry;
    Entity m_entity;
    std::string m_componentType;
};

// Remove component command
class RemoveComponentCommand : public ICommand {
public:
    RemoveComponentCommand(entt::registry* registry, Entity entity,
                            const std::string& componentType);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override { return "Remove " + m_componentType; }
    std::vector<Entity> GetAffectedEntities() const override { return {m_entity}; }

private:
    entt::registry* m_registry;
    Entity m_entity;
    std::string m_componentType;
    std::vector<uint8_t> m_componentData;  // For restoration
};

// Selection change command (optional, for selection undo)
class SelectionChangeCommand : public ICommand {
public:
    using SelectionCallback = std::function<void(const std::vector<Entity>&)>;

    SelectionChangeCommand(const std::vector<Entity>& oldSelection,
                            const std::vector<Entity>& newSelection,
                            SelectionCallback setSelection);

    void Execute() override;
    void Undo() override;
    std::string GetDescription() const override { return "Change Selection"; }

private:
    std::vector<Entity> m_oldSelection;
    std::vector<Entity> m_newSelection;
    SelectionCallback m_setSelection;
};

// ============================================================================
// Macro Recording
// ============================================================================

class MacroRecorder {
public:
    MacroRecorder(UndoManager* undoManager);
    ~MacroRecorder();

    // Recording control
    void StartRecording(const std::string& name);
    void StopRecording();
    bool IsRecording() const { return m_recording; }

    // Save/Load macros
    bool SaveMacro(const std::string& name, const std::string& path);
    bool LoadMacro(const std::string& path);

    // Play macro
    void PlayMacro(const std::string& name);
    void PlayMacroRepeat(const std::string& name, int repeatCount);

    // Get available macros
    std::vector<std::string> GetMacroNames() const;

private:
    UndoManager* m_undoManager;
    bool m_recording = false;
    std::string m_currentMacroName;
    std::vector<std::unique_ptr<ICommand>> m_recordedCommands;
    std::unordered_map<std::string, std::vector<std::unique_ptr<ICommand>>> m_macros;
};

// ============================================================================
// Scoped Undo Group Helper
// ============================================================================

class ScopedUndoGroup {
public:
    ScopedUndoGroup(UndoManager* manager, const std::string& description)
        : m_manager(manager) {
        m_manager->BeginGroup(description);
    }

    ~ScopedUndoGroup() {
        m_manager->EndGroup();
    }

    // Non-copyable
    ScopedUndoGroup(const ScopedUndoGroup&) = delete;
    ScopedUndoGroup& operator=(const ScopedUndoGroup&) = delete;

private:
    UndoManager* m_manager;
};

// Usage: SCOPED_UNDO_GROUP(manager, "Transform Multiple")
#define SCOPED_UNDO_GROUP(manager, desc) \
    Cortex::Editor::ScopedUndoGroup _undoGroup##__LINE__(manager, desc)

} // namespace Cortex::Editor
