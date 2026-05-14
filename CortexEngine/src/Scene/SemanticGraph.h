#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>

namespace Cortex::Scene {

enum class SemanticAdmissionStatus : uint8_t {
    Draft = 0,
    Proposed,
    Validated,
    Accepted,
    Rejected
};

enum class SemanticDiffOpType : uint8_t {
    AddObject = 0,
    UpdateObject,
    RemoveObject
};

struct SemanticProvenance {
    std::string prompt;
    uint64_t seed = 0;
    std::string generator;
    std::string sourceAsset;
    std::string validationReport;
    std::string commitId;

    [[nodiscard]] bool HasReproducibleSource() const;
};

struct SemanticBudget {
    uint64_t estimatedTextureBytes = 0;
    uint32_t texturePages = 0;
    uint32_t psoSignatures = 0;
    uint32_t blasBuilds = 0;
    uint32_t tlasInstances = 0;
    uint32_t descriptors = 0;
    uint32_t validationCameraCount = 0;

    [[nodiscard]] bool FitsWithin(const SemanticBudget& limit) const;
};

struct SemanticInvalidation {
    bool taaHistory = false;
    bool rtReflectionHistory = false;
    bool rtGIHistory = false;
    bool temporalMasks = false;
    std::string dirtyRegion;

    [[nodiscard]] bool Any() const;
};

struct SemanticObject {
    std::string id;
    std::string editableGroup;
    std::string semanticType;
    std::string support;
    std::string region;
    std::string materialIntent;
    SemanticProvenance provenance;
    SemanticBudget budget;
    SemanticInvalidation invalidation;
    SemanticAdmissionStatus admission = SemanticAdmissionStatus::Draft;
    entt::entity linkedEntity = entt::null;
    std::vector<std::string> tags;
};

struct SemanticRuntimeObjectPlan {
    std::string semanticId;
    std::string editableGroup;
    std::string semanticType;
    std::string region;
    std::string materialIntent;
    SemanticBudget budget;
    SemanticInvalidation invalidation;
    entt::entity existingEntity = entt::null;
};

struct SemanticGraphDiffOp {
    SemanticDiffOpType type = SemanticDiffOpType::AddObject;
    std::optional<SemanticObject> before;
    std::optional<SemanticObject> after;
};

struct SemanticGraphDiff {
    std::string id;
    std::vector<SemanticGraphDiffOp> ops;

    [[nodiscard]] bool Empty() const { return ops.empty(); }
    [[nodiscard]] SemanticGraphDiff Inverted(std::string inverseId) const;
};

class SemanticSceneGraph {
public:
    [[nodiscard]] bool AddObject(const SemanticObject& object, std::string* error = nullptr);
    [[nodiscard]] bool UpsertObject(const SemanticObject& object, std::string* error = nullptr);
    [[nodiscard]] bool RemoveObject(const std::string& id, std::string* error = nullptr);
    [[nodiscard]] bool ApplyDiff(const SemanticGraphDiff& diff, std::string* error = nullptr);

    [[nodiscard]] const SemanticObject* FindById(const std::string& id) const;
    [[nodiscard]] std::vector<const SemanticObject*> FindByGroup(const std::string& group) const;
    [[nodiscard]] std::vector<const SemanticObject*> FindByRegion(const std::string& region) const;
    [[nodiscard]] std::vector<SemanticRuntimeObjectPlan> CompileRuntimePlan() const;
    [[nodiscard]] std::vector<std::string> ValidateV0Objects() const;

    [[nodiscard]] size_t ObjectCount() const { return m_objects.size(); }
    [[nodiscard]] const std::vector<std::string>& ObjectOrder() const { return m_order; }

private:
    std::unordered_map<std::string, SemanticObject> m_objects;
    std::vector<std::string> m_order;
};

[[nodiscard]] const char* ToString(SemanticAdmissionStatus status);
[[nodiscard]] const char* ToString(SemanticDiffOpType type);

// Runtime self-test used by validation tooling. It exercises graph creation,
// V0 fields, group/region lookup, diff application, inversion/undo, and runtime
// plan compilation without initializing DX12.
[[nodiscard]] std::string RunSemanticGraphSelfTestJson();

} // namespace Cortex::Scene
