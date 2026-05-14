#pragma once

#include <string>
#include <vector>

namespace Cortex::Scene {

enum class CapturedSceneKind {
    GaussianSplat,
    NeRF
};

struct CapturedSceneImportRequest {
    std::string captureId;
    CapturedSceneKind kind = CapturedSceneKind::GaussianSplat;
    bool editableWorldRequested = false;
    std::vector<std::string> proxyGeometryIds;
    std::vector<std::string> semanticAnchorIds;
};

struct CapturedSceneReferenceLayer {
    std::string layerId;
    CapturedSceneKind kind = CapturedSceneKind::GaussianSplat;
    std::vector<std::string> proxyGeometryIds;
    std::vector<std::string> semanticAnchorIds;
    bool authoritativeGeometry = false;
    bool editableWorld = false;
};

struct CapturedSceneImportResult {
    bool accepted = false;
    CapturedSceneReferenceLayer layer;
    std::vector<std::string> errors;
};

[[nodiscard]] CapturedSceneImportResult ImportCapturedSceneReferenceLayer(const CapturedSceneImportRequest& request);
[[nodiscard]] const char* ToString(CapturedSceneKind kind);
[[nodiscard]] std::string RunCapturedSceneImportSelfTestJson();

} // namespace Cortex::Scene
