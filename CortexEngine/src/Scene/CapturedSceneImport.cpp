#include "Scene/CapturedSceneImport.h"

#include <nlohmann/json.hpp>

namespace Cortex::Scene {
namespace {

bool Blank(const std::string& value) {
    return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

void AddError(std::vector<std::string>& errors, const std::string& message) {
    errors.push_back(message);
}

} // namespace

const char* ToString(CapturedSceneKind kind) {
    switch (kind) {
    case CapturedSceneKind::GaussianSplat: return "gaussian_splat";
    case CapturedSceneKind::NeRF: return "nerf";
    }
    return "unknown";
}

CapturedSceneImportResult ImportCapturedSceneReferenceLayer(const CapturedSceneImportRequest& request) {
    CapturedSceneImportResult result;
    if (Blank(request.captureId)) {
        AddError(result.errors, "capture id is required");
    }
    if (request.proxyGeometryIds.empty()) {
        AddError(result.errors, "captured scene import requires proxy geometry");
    }
    if (request.semanticAnchorIds.empty()) {
        AddError(result.errors, "captured scene import requires semantic anchors");
    }
    if (request.editableWorldRequested) {
        AddError(result.errors, "captured scenes cannot become editable worlds without authored proxy geometry");
    }

    result.accepted = result.errors.empty();
    if (result.accepted) {
        result.layer.layerId = "reference_capture:" + request.captureId;
        result.layer.kind = request.kind;
        result.layer.proxyGeometryIds = request.proxyGeometryIds;
        result.layer.semanticAnchorIds = request.semanticAnchorIds;
        result.layer.authoritativeGeometry = false;
        result.layer.editableWorld = false;
    }
    return result;
}

std::string RunCapturedSceneImportSelfTestJson() {
    CapturedSceneImportRequest valid;
    valid.captureId = "rain_pavilion_scan";
    valid.kind = CapturedSceneKind::GaussianSplat;
    valid.proxyGeometryIds = {"proxy.floor_plane", "proxy.table_box", "proxy.glass_wall"};
    valid.semanticAnchorIds = {"anchor.foreground_table", "anchor.background_glass"};

    CapturedSceneImportRequest missingProxy = valid;
    missingProxy.captureId = "missing_proxy_scan";
    missingProxy.proxyGeometryIds.clear();

    CapturedSceneImportRequest editableWorld = valid;
    editableWorld.captureId = "editable_world_scan";
    editableWorld.kind = CapturedSceneKind::NeRF;
    editableWorld.editableWorldRequested = true;

    const auto validResult = ImportCapturedSceneReferenceLayer(valid);
    const auto missingProxyResult = ImportCapturedSceneReferenceLayer(missingProxy);
    const auto editableWorldResult = ImportCapturedSceneReferenceLayer(editableWorld);

    const bool pass =
        validResult.accepted &&
        !validResult.layer.authoritativeGeometry &&
        !validResult.layer.editableWorld &&
        validResult.layer.proxyGeometryIds.size() == 3 &&
        validResult.layer.semanticAnchorIds.size() == 2 &&
        !missingProxyResult.accepted &&
        !editableWorldResult.accepted;

    nlohmann::json report;
    report["schema"] = "cortex.captured_scene_import.self_test.v1";
    report["pass"] = pass;
    report["valid"] = {
        {"accepted", validResult.accepted},
        {"layer_id", validResult.layer.layerId},
        {"kind", ToString(validResult.layer.kind)},
        {"proxy_count", validResult.layer.proxyGeometryIds.size()},
        {"anchor_count", validResult.layer.semanticAnchorIds.size()},
        {"authoritative_geometry", validResult.layer.authoritativeGeometry},
        {"editable_world", validResult.layer.editableWorld}
    };
    report["missing_proxy"] = {
        {"accepted", missingProxyResult.accepted},
        {"errors", missingProxyResult.errors}
    };
    report["editable_world"] = {
        {"accepted", editableWorldResult.accepted},
        {"errors", editableWorldResult.errors}
    };
    return report.dump(2);
}

} // namespace Cortex::Scene
