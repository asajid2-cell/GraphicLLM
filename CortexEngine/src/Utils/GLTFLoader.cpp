#include "GLTFLoader.h"
#include "Utils/FileUtils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <algorithm>

namespace fs = std::filesystem;

namespace Cortex::Utils {

namespace {

// -----------------------------------------------------------------------------
// Raw accessor helpers for single-mesh / single-primitive glTF files

struct AccessorInfo {
    int bufferView = -1;
    size_t byteOffset = 0;
    size_t count = 0;
    int componentType = 0;
    std::string type;
};

struct BufferViewInfo {
    int buffer = 0;
    size_t byteOffset = 0;
    size_t byteLength = 0;
    size_t byteStride = 0; // 0 = tightly packed
};

struct BufferInfo {
    std::vector<uint8_t> data;
};

Result<void> LoadBuffers(const nlohmann::json& j,
                         const fs::path& baseDir,
                         std::vector<BufferInfo>& outBuffers) {
    if (!j.contains("buffers") || !j["buffers"].is_array()) {
        return Result<void>::Err("gltf has no buffers array");
    }

    for (const auto& jb : j["buffers"]) {
        if (!jb.contains("uri")) {
            return Result<void>::Err("buffer missing uri");
        }
        fs::path uri = jb["uri"].get<std::string>();
        fs::path fullPath = baseDir / uri;

        auto binResult = ReadBinaryFile(fullPath);
        if (binResult.IsErr()) {
            return Result<void>::Err("Failed to read buffer '" + fullPath.string() +
                                     "': " + binResult.Error());
        }

        BufferInfo info;
        info.data = std::move(binResult.Value());
        outBuffers.push_back(std::move(info));
    }

    return Result<void>::Ok();
}

Result<void> LoadBufferViews(const nlohmann::json& j,
                             std::vector<BufferViewInfo>& outViews) {
    if (!j.contains("bufferViews") || !j["bufferViews"].is_array()) {
        return Result<void>::Err("gltf has no bufferViews array");
    }

    for (const auto& jv : j["bufferViews"]) {
        BufferViewInfo v;
        v.buffer = jv.value("buffer", 0);
        v.byteOffset = static_cast<size_t>(jv.value("byteOffset", 0));
        v.byteLength = static_cast<size_t>(jv.value("byteLength", 0));
        v.byteStride = static_cast<size_t>(jv.value("byteStride", 0));
        outViews.push_back(v);
    }

    return Result<void>::Ok();
}

Result<void> LoadAccessors(const nlohmann::json& j,
                           std::vector<AccessorInfo>& outAccessors) {
    if (!j.contains("accessors") || !j["accessors"].is_array()) {
        return Result<void>::Err("gltf has no accessors array");
    }

    for (const auto& ja : j["accessors"]) {
        AccessorInfo a;
        a.bufferView = ja.value("bufferView", -1);
        a.byteOffset = static_cast<size_t>(ja.value("byteOffset", 0));
        a.count = static_cast<size_t>(ja.value("count", 0));
        a.componentType = ja.value("componentType", 0);
        a.type = ja.value("type", std::string{});
        outAccessors.push_back(a);
    }

    return Result<void>::Ok();
}

size_t ComponentSize(int componentType) {
    switch (componentType) {
        case 5120: // BYTE
        case 5121: // UNSIGNED_BYTE
            return 1;
        case 5122: // SHORT
        case 5123: // UNSIGNED_SHORT
            return 2;
        case 5125: // UNSIGNED_INT
        case 5126: // FLOAT
            return 4;
        default:
            return 0;
    }
}

size_t NumComponents(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    return 0;
}

template<typename T>
void ReadAccessorFloats(const AccessorInfo& acc,
                        const BufferViewInfo& view,
                        const BufferInfo& buf,
                        size_t numComponents,
                        std::vector<T>& out) {
    size_t compSize = ComponentSize(acc.componentType);
    size_t elemSize = compSize * numComponents;
    size_t stride = view.byteStride != 0 ? view.byteStride : elemSize;

    out.resize(acc.count);

    const uint8_t* base = buf.data.data() + view.byteOffset + acc.byteOffset;

    for (size_t i = 0; i < acc.count; ++i) {
        const uint8_t* src = base + stride * i;
        const float* f = reinterpret_cast<const float*>(src);
        if constexpr (std::is_same_v<T, glm::vec2>) {
            out[i] = glm::vec2(f[0], f[1]);
        } else if constexpr (std::is_same_v<T, glm::vec3>) {
            out[i] = glm::vec3(f[0], f[1], f[2]);
        }
    }
}

Result<void> ReadIndices(const AccessorInfo& acc,
                         const BufferViewInfo& view,
                         const BufferInfo& buf,
                         std::vector<uint32_t>& out) {
    size_t compSize = ComponentSize(acc.componentType);
    if (compSize == 0) {
        return Result<void>::Err("Unsupported index componentType");
    }

    size_t stride = view.byteStride != 0 ? view.byteStride : compSize;
    out.resize(acc.count);

    const uint8_t* base = buf.data.data() + view.byteOffset + acc.byteOffset;

    for (size_t i = 0; i < acc.count; ++i) {
        const uint8_t* src = base + stride * i;
        uint32_t idx = 0;
        switch (acc.componentType) {
            case 5121: // UNSIGNED_BYTE
                idx = *reinterpret_cast<const uint8_t*>(src);
                break;
            case 5123: // UNSIGNED_SHORT
                idx = *reinterpret_cast<const uint16_t*>(src);
                break;
            case 5125: // UNSIGNED_INT
                idx = *reinterpret_cast<const uint32_t*>(src);
                break;
            default:
                return Result<void>::Err("Unsupported index componentType");
        }
        out[i] = idx;
    }

    return Result<void>::Ok();
}

} // namespace

Result<std::shared_ptr<Scene::MeshData>> LoadGLTFMesh(const std::string& pathStr) {
    fs::path path = fs::u8path(pathStr);

    if (!FileExists(path)) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err("GLTF file not found: " + path.string());
    }

    auto textResult = ReadTextFile(path);
    if (textResult.IsErr()) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err(textResult.Error());
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(textResult.Value());
    } catch (const std::exception& e) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err(std::string("Failed to parse glTF JSON: ") + e.what());
    }

    fs::path baseDir = path.parent_path();

    std::vector<BufferInfo> buffers;
    auto bufRes = LoadBuffers(j, baseDir, buffers);
    if (bufRes.IsErr()) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err(bufRes.Error());
    }

    std::vector<BufferViewInfo> views;
    auto viewRes = LoadBufferViews(j, views);
    if (viewRes.IsErr()) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err(viewRes.Error());
    }

    std::vector<AccessorInfo> accessors;
    auto accRes = LoadAccessors(j, accessors);
    if (accRes.IsErr()) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err(accRes.Error());
    }

    if (!j.contains("meshes") || !j["meshes"].is_array() || j["meshes"].empty()) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err("gltf has no meshes");
    }

    // Many Khronos sample models contain multiple meshes (e.g., backdrop + hero
    // object). Instead of blindly taking meshes[0], choose the mesh whose first
    // primitive's POSITION accessor has the largest vertex count. This tends to
    // select the "main" mesh (e.g., the dragon rather than the cloth backdrop).
    const auto& meshes = j["meshes"];
    int bestMeshIndex = -1;
    size_t bestVertexCount = 0;

    for (int mi = 0; mi < static_cast<int>(meshes.size()); ++mi) {
        const auto& mesh = meshes[mi];
        if (!mesh.contains("primitives") || !mesh["primitives"].is_array() || mesh["primitives"].empty()) {
            continue;
        }
        const auto& prim0 = mesh["primitives"][0];
        if (!prim0.contains("attributes")) {
            continue;
        }
        const auto& attrs0 = prim0["attributes"];
        if (!attrs0.contains("POSITION")) {
            continue;
        }
        int posAccIndex = attrs0["POSITION"].get<int>();
        if (posAccIndex < 0 || posAccIndex >= static_cast<int>(accessors.size())) {
            continue;
        }
        const AccessorInfo& posAcc0 = accessors[posAccIndex];
        if (posAcc0.count > bestVertexCount) {
            bestVertexCount = posAcc0.count;
            bestMeshIndex = mi;
        }
    }

    if (bestMeshIndex < 0) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err("Failed to choose mesh: no valid POSITION accessor found");
    }

    const auto& mesh0 = meshes[bestMeshIndex];
    if (!mesh0.contains("primitives") || !mesh0["primitives"].is_array() || mesh0["primitives"].empty()) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err("gltf mesh has no primitives");
    }

    const auto& prim = mesh0["primitives"][0];

    // Attributes
    if (!prim.contains("attributes")) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err("primitive has no attributes");
    }

    const auto& attrs = prim["attributes"];

    auto getAccessorIndex = [&](const char* semantic) -> int {
        if (!attrs.contains(semantic)) return -1;
        return attrs[semantic].get<int>();
    };

    int posIndex = getAccessorIndex("POSITION");
    if (posIndex < 0 || posIndex >= static_cast<int>(accessors.size())) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err("primitive missing POSITION accessor");
    }

    int normIndex = getAccessorIndex("NORMAL");
    int uvIndex   = getAccessorIndex("TEXCOORD_0");

    const AccessorInfo& posAcc = accessors[posIndex];
    if (posAcc.bufferView < 0 || posAcc.bufferView >= static_cast<int>(views.size())) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err("POSITION accessor has invalid bufferView");
    }

    std::vector<glm::vec3> positions;
    std::vector<glm::vec3> normals;
    std::vector<glm::vec2> uvs;

    // Positions
    {
        if (posAcc.componentType != 5126 || posAcc.type != "VEC3") {
            return Result<std::shared_ptr<Scene::MeshData>>::Err("POSITION accessor must be float VEC3");
        }
        const BufferViewInfo& v = views[posAcc.bufferView];
        const BufferInfo& b = buffers[v.buffer];
        ReadAccessorFloats<glm::vec3>(posAcc, v, b, 3, positions);
    }

    // Normals (optional)
    if (normIndex >= 0 && normIndex < static_cast<int>(accessors.size())) {
        const AccessorInfo& nAcc = accessors[normIndex];
        if (nAcc.componentType == 5126 && nAcc.type == "VEC3" && nAcc.bufferView >= 0) {
            const BufferViewInfo& v = views[nAcc.bufferView];
            const BufferInfo& b = buffers[v.buffer];
            ReadAccessorFloats<glm::vec3>(nAcc, v, b, 3, normals);
        }
    }

    // UVs (optional)
    if (uvIndex >= 0 && uvIndex < static_cast<int>(accessors.size())) {
        const AccessorInfo& uvAcc = accessors[uvIndex];
        if (uvAcc.componentType == 5126 && uvAcc.type == "VEC2" && uvAcc.bufferView >= 0) {
            const BufferViewInfo& v = views[uvAcc.bufferView];
            const BufferInfo& b = buffers[v.buffer];
            ReadAccessorFloats<glm::vec2>(uvAcc, v, b, 2, uvs);
        }
    }

    // Indices (optional - but our renderer expects indexed)
    std::vector<uint32_t> indices;
    if (prim.contains("indices")) {
        int idxAccIndex = prim["indices"].get<int>();
        if (idxAccIndex < 0 || idxAccIndex >= static_cast<int>(accessors.size())) {
            return Result<std::shared_ptr<Scene::MeshData>>::Err("indices accessor index out of range");
        }
        const AccessorInfo& idxAcc = accessors[idxAccIndex];
        if (idxAcc.bufferView < 0 || idxAcc.bufferView >= static_cast<int>(views.size())) {
            return Result<std::shared_ptr<Scene::MeshData>>::Err("indices accessor has invalid bufferView");
        }
        const BufferViewInfo& v = views[idxAcc.bufferView];
        const BufferInfo& b = buffers[v.buffer];
        auto idxRes = ReadIndices(idxAcc, v, b, indices);
        if (idxRes.IsErr()) {
            return Result<std::shared_ptr<Scene::MeshData>>::Err(idxRes.Error());
        }
    } else {
        // Fallback: generate a linear index buffer
        indices.resize(positions.size());
        for (size_t i = 0; i < positions.size(); ++i) {
            indices[i] = static_cast<uint32_t>(i);
        }
    }

    auto mesh = std::make_shared<Scene::MeshData>();
    mesh->positions = std::move(positions);
    mesh->normals   = std::move(normals);
    mesh->texCoords = std::move(uvs);
    mesh->indices   = std::move(indices);
    mesh->UpdateBounds();

    spdlog::info("Loaded glTF mesh '{}' (verts={}, indices={})",
                 path.string(),
                 mesh->positions.size(),
                 mesh->indices.size());

    return Result<std::shared_ptr<Scene::MeshData>>::Ok(mesh);
}

// -----------------------------------------------------------------------------
// Sample model registry (glTF-Sample-Models/2.0)

std::unordered_map<std::string, fs::path> g_sampleModelPaths;
bool g_sampleModelsInitialized = false;
bool g_sampleModelsInitAttempted = false;

std::string ToLower(const std::string& s) {
    std::string out = s;
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

Result<void> InitializeSampleModelLibrary() {
    if (g_sampleModelsInitialized) {
        return Result<void>::Ok();
    }
    if (g_sampleModelsInitAttempted && g_sampleModelPaths.empty()) {
        return Result<void>::Err("Sample model library previously failed to initialize");
    }

    g_sampleModelsInitAttempted = true;

    namespace fs = std::filesystem;
    fs::path cwd;
    try {
        cwd = fs::current_path();
    } catch (...) {
        return Result<void>::Err("Failed to query current working directory for sample model library");
    }

    // Resolve graphics root: .../CortexEngine/build/bin -> .../graphics
    fs::path graphicsRoot = cwd;
    for (int i = 0; i < 3; ++i) {
        graphicsRoot = graphicsRoot.parent_path();
    }

    fs::path modelsRoot = graphicsRoot / "glTF-Sample-Models/2.0";
    if (!fs::exists(modelsRoot)) {
        spdlog::info("SampleModelLibrary: glTF-Sample-Models repo not found at '{}'", modelsRoot.string());
        return Result<void>::Err("glTF-Sample-Models repo not found");
    }

    fs::path indexPath = modelsRoot / "model-index.json";
    if (!fs::exists(indexPath)) {
        spdlog::warn("SampleModelLibrary: model-index.json not found at '{}'", indexPath.string());
        return Result<void>::Err("model-index.json not found");
    }

    auto indexText = ReadTextFile(indexPath);
    if (indexText.IsErr()) {
        return Result<void>::Err("Failed to read model-index.json: " + indexText.Error());
    }

    nlohmann::json indexJson;
    try {
        indexJson = nlohmann::json::parse(indexText.Value());
    } catch (const std::exception& e) {
        return Result<void>::Err(std::string("Failed to parse model-index.json: ") + e.what());
    }

    if (!indexJson.is_array()) {
        return Result<void>::Err("model-index.json root is not an array");
    }

    g_sampleModelPaths.clear();

    size_t registered = 0;
    size_t skipped = 0;
    for (const auto& entry : indexJson) {
        if (!entry.contains("name") || !entry.contains("variants")) {
            ++skipped;
            continue;
        }

        std::string name = entry["name"].get<std::string>();
        const auto& variants = entry["variants"];
        if (!variants.contains("glTF")) {
            // We only support .gltf + external buffers for now.
            ++skipped;
            continue;
        }

        std::string relGltf = variants["glTF"].get<std::string>();
        fs::path gltfPath = modelsRoot / name / "glTF" / relGltf;
        if (!fs::exists(gltfPath)) {
            spdlog::warn("SampleModelLibrary: glTF file missing for '{}': {}", name, gltfPath.string());
            ++skipped;
            continue;
        }

        std::string key = ToLower(name);
        g_sampleModelPaths[key] = gltfPath;
        ++registered;
    }

    if (registered == 0) {
        return Result<void>::Err("No compatible sample models found under glTF-Sample-Models/2.0");
    }

    g_sampleModelsInitialized = true;
    spdlog::info("SampleModelLibrary: registered {} sample models ({} skipped)", registered, skipped);
    return Result<void>::Ok();
}

Result<std::shared_ptr<Scene::MeshData>> LoadSampleModelMesh(const std::string& assetName) {
    if (assetName.empty()) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err("Sample model asset name is empty");
    }

    auto initResult = InitializeSampleModelLibrary();
    if (initResult.IsErr()) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err("Sample model library not initialized: " + initResult.Error());
    }

    std::string key = ToLower(assetName);
    auto it = g_sampleModelPaths.find(key);
    if (it == g_sampleModelPaths.end()) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err("Sample model not registered: " + assetName);
    }

    return LoadGLTFMesh(it->second.string());
}

std::vector<std::string> GetSampleModelNames() {
    std::vector<std::string> names;

    auto initResult = InitializeSampleModelLibrary();
    if (initResult.IsErr()) {
        return names;
    }

    names.reserve(g_sampleModelPaths.size());
    for (const auto& kv : g_sampleModelPaths) {
        names.push_back(kv.first);
    }

    std::sort(names.begin(), names.end());
    return names;
}

} // namespace Cortex::Utils
