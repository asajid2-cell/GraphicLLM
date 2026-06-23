#include "GLTFLoader.h"
#include "Utils/FileUtils.h"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <spdlog/spdlog.h>
#include <unordered_map>
#include <algorithm>
#include <functional>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

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

glm::vec4 ReadVec4(const nlohmann::json& jv, const glm::vec4& fallback) {
    if (!jv.is_array() || jv.size() < 4) {
        return fallback;
    }
    return glm::vec4(jv[0].get<float>(), jv[1].get<float>(), jv[2].get<float>(), jv[3].get<float>());
}

glm::vec3 ReadVec3(const nlohmann::json& jv, const glm::vec3& fallback) {
    if (!jv.is_array() || jv.size() < 3) {
        return fallback;
    }
    return glm::vec3(jv[0].get<float>(), jv[1].get<float>(), jv[2].get<float>());
}

glm::mat4 ReadNodeTransform(const nlohmann::json& node) {
    if (node.contains("matrix") && node["matrix"].is_array() && node["matrix"].size() >= 16) {
        glm::mat4 m(1.0f);
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                m[col][row] = node["matrix"][col * 4 + row].get<float>();
            }
        }
        return m;
    }

    glm::vec3 translation(0.0f);
    if (node.contains("translation")) {
        translation = ReadVec3(node["translation"], translation);
    }

    glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
    if (node.contains("rotation") && node["rotation"].is_array() && node["rotation"].size() >= 4) {
        rotation = glm::quat(node["rotation"][3].get<float>(),
                             node["rotation"][0].get<float>(),
                             node["rotation"][1].get<float>(),
                             node["rotation"][2].get<float>());
    }

    glm::vec3 scale(1.0f);
    if (node.contains("scale")) {
        scale = ReadVec3(node["scale"], scale);
    }

    return glm::translate(glm::mat4(1.0f), translation) * glm::mat4_cast(rotation) *
           glm::scale(glm::mat4(1.0f), scale);
}

std::string TexturePathForIndex(const nlohmann::json& j, const fs::path& baseDir, int textureIndex) {
    if (textureIndex < 0 || !j.contains("textures") || !j["textures"].is_array() ||
        textureIndex >= static_cast<int>(j["textures"].size())) {
        return {};
    }
    const auto& texture = j["textures"][textureIndex];
    const int imageIndex = texture.value("source", -1);
    if (imageIndex < 0 || !j.contains("images") || !j["images"].is_array() ||
        imageIndex >= static_cast<int>(j["images"].size())) {
        return {};
    }
    const auto& image = j["images"][imageIndex];
    if (!image.contains("uri") || !image["uri"].is_string()) {
        return {};
    }

    const std::string uri = image["uri"].get<std::string>();
    if (uri.empty() || uri.rfind("data:", 0) == 0) {
        return {};
    }
    return (baseDir / fs::u8path(uri)).lexically_normal().string();
}

std::string TexturePathForSlot(const nlohmann::json& j,
                               const fs::path& baseDir,
                               const nlohmann::json& material,
                               const char* slotName) {
    if (!material.contains(slotName) || !material[slotName].is_object()) {
        return {};
    }
    return TexturePathForIndex(j, baseDir, material[slotName].value("index", -1));
}

Scene::MeshData::EmbeddedPbrMaterial ReadEmbeddedMaterial(const nlohmann::json& j,
                                                          const fs::path& baseDir,
                                                          const nlohmann::json& prim) {
    Scene::MeshData::EmbeddedPbrMaterial out;
    const int materialIndex = prim.value("material", -1);
    if (materialIndex < 0 || !j.contains("materials") || !j["materials"].is_array() ||
        materialIndex >= static_cast<int>(j["materials"].size())) {
        return out;
    }

    const auto& material = j["materials"][materialIndex];
    out.doubleSided = material.value("doubleSided", false);

    if (material.contains("pbrMetallicRoughness") && material["pbrMetallicRoughness"].is_object()) {
        const auto& pbr = material["pbrMetallicRoughness"];
        if (pbr.contains("baseColorFactor")) {
            out.baseColorFactor = ReadVec4(pbr["baseColorFactor"], out.baseColorFactor);
        }
        out.metallicFactor = pbr.value("metallicFactor", out.metallicFactor);
        out.roughnessFactor = pbr.value("roughnessFactor", out.roughnessFactor);
        out.albedoPath = TexturePathForSlot(j, baseDir, pbr, "baseColorTexture");
        out.metallicRoughnessPath = TexturePathForSlot(j, baseDir, pbr, "metallicRoughnessTexture");
    }

    out.normalPath = TexturePathForSlot(j, baseDir, material, "normalTexture");
    if (material.contains("normalTexture") && material["normalTexture"].is_object()) {
        out.normalScale = material["normalTexture"].value("scale", out.normalScale);
    }

    out.occlusionPath = TexturePathForSlot(j, baseDir, material, "occlusionTexture");
    if (material.contains("occlusionTexture") && material["occlusionTexture"].is_object()) {
        out.occlusionStrength = material["occlusionTexture"].value("strength", out.occlusionStrength);
    }

    out.emissivePath = TexturePathForSlot(j, baseDir, material, "emissiveTexture");
    if (material.contains("emissiveFactor")) {
        out.emissiveFactor = ReadVec3(material["emissiveFactor"], out.emissiveFactor);
    }
    return out;
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

    auto mesh = std::make_shared<Scene::MeshData>();
    const auto& meshes = j["meshes"];
    constexpr size_t kMaxMergedIndices = 500000;
    size_t primitiveCount = 0;
    size_t skippedBudgetPrimitives = 0;
    Scene::MeshData::EmbeddedPbrMaterial firstMaterial;
    bool haveMaterial = false;
    bool haveTexturedMaterial = false;

    auto appendPrimitive = [&](const nlohmann::json& prim, const glm::mat4& transform) -> Result<void> {
        if (prim.value("mode", 4) != 4) {
            return Result<void>::Ok();
        }
        if (!prim.contains("attributes")) {
            return Result<void>::Ok();
        }

        const auto& attrs = prim["attributes"];
        auto getAccessorIndex = [&](const char* semantic) -> int {
            if (!attrs.contains(semantic)) return -1;
            return attrs[semantic].get<int>();
        };

        int posIndex = getAccessorIndex("POSITION");
        if (posIndex < 0 || posIndex >= static_cast<int>(accessors.size())) {
            return Result<void>::Ok();
        }

        const AccessorInfo& posAcc = accessors[posIndex];
        if (posAcc.componentType != 5126 || posAcc.type != "VEC3" ||
            posAcc.bufferView < 0 || posAcc.bufferView >= static_cast<int>(views.size())) {
            return Result<void>::Err("POSITION accessor must be float VEC3 with valid bufferView");
        }

        const BufferViewInfo& posView = views[posAcc.bufferView];
        if (posView.buffer < 0 || posView.buffer >= static_cast<int>(buffers.size())) {
            return Result<void>::Err("POSITION bufferView references invalid buffer");
        }
        std::vector<glm::vec3> positions;
        ReadAccessorFloats<glm::vec3>(posAcc, posView, buffers[posView.buffer], 3, positions);

        std::vector<glm::vec3> normals;
        int normIndex = getAccessorIndex("NORMAL");
        if (normIndex >= 0 && normIndex < static_cast<int>(accessors.size())) {
            const AccessorInfo& nAcc = accessors[normIndex];
            if (nAcc.componentType == 5126 && nAcc.type == "VEC3" &&
                nAcc.bufferView >= 0 && nAcc.bufferView < static_cast<int>(views.size())) {
                const BufferViewInfo& nView = views[nAcc.bufferView];
                if (nView.buffer >= 0 && nView.buffer < static_cast<int>(buffers.size())) {
                    ReadAccessorFloats<glm::vec3>(nAcc, nView, buffers[nView.buffer], 3, normals);
                }
            }
        }

        std::vector<glm::vec2> uvs;
        int uvIndex = getAccessorIndex("TEXCOORD_0");
        if (uvIndex >= 0 && uvIndex < static_cast<int>(accessors.size())) {
            const AccessorInfo& uvAcc = accessors[uvIndex];
            if (uvAcc.componentType == 5126 && uvAcc.type == "VEC2" &&
                uvAcc.bufferView >= 0 && uvAcc.bufferView < static_cast<int>(views.size())) {
                const BufferViewInfo& uvView = views[uvAcc.bufferView];
                if (uvView.buffer >= 0 && uvView.buffer < static_cast<int>(buffers.size())) {
                    ReadAccessorFloats<glm::vec2>(uvAcc, uvView, buffers[uvView.buffer], 2, uvs);
                }
            }
        }

        std::vector<uint32_t> indices;
        if (prim.contains("indices")) {
            int idxAccIndex = prim["indices"].get<int>();
            if (idxAccIndex < 0 || idxAccIndex >= static_cast<int>(accessors.size())) {
                return Result<void>::Err("indices accessor index out of range");
            }
            const AccessorInfo& idxAcc = accessors[idxAccIndex];
            if (idxAcc.bufferView < 0 || idxAcc.bufferView >= static_cast<int>(views.size())) {
                return Result<void>::Err("indices accessor has invalid bufferView");
            }
            const BufferViewInfo& idxView = views[idxAcc.bufferView];
            if (idxView.buffer < 0 || idxView.buffer >= static_cast<int>(buffers.size())) {
                return Result<void>::Err("indices bufferView references invalid buffer");
            }
            auto idxRes = ReadIndices(idxAcc, idxView, buffers[idxView.buffer], indices);
            if (idxRes.IsErr()) {
                return Result<void>::Err(idxRes.Error());
            }
        } else {
            indices.resize(positions.size());
            for (size_t i = 0; i < positions.size(); ++i) {
                indices[i] = static_cast<uint32_t>(i);
            }
        }

        if (!mesh->indices.empty() && mesh->indices.size() + indices.size() > kMaxMergedIndices) {
            ++skippedBudgetPrimitives;
            spdlog::warn("LoadGLTFMesh: skipping primitive that would exceed merged index budget for '{}' ({} + {} > {})",
                         path.string(), mesh->indices.size(), indices.size(), kMaxMergedIndices);
            return Result<void>::Ok();
        }

        if (!normals.empty() && normals.size() != positions.size()) {
            normals.clear();
        }
        if (!uvs.empty() && uvs.size() != positions.size()) {
            uvs.clear();
        }

        const uint32_t baseVertex = static_cast<uint32_t>(mesh->positions.size());
        const size_t previousVertexCount = mesh->positions.size();
        const bool appendNormals = !normals.empty() || !mesh->normals.empty();
        const bool appendUvs = !uvs.empty() || !mesh->texCoords.empty();
        if (appendNormals && mesh->normals.empty() && previousVertexCount > 0) {
            mesh->normals.resize(previousVertexCount, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        if (appendUvs && mesh->texCoords.empty() && previousVertexCount > 0) {
            mesh->texCoords.resize(previousVertexCount, glm::vec2(0.0f));
        }

        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(transform)));
        for (size_t i = 0; i < positions.size(); ++i) {
            mesh->positions.push_back(glm::vec3(transform * glm::vec4(positions[i], 1.0f)));
            if (appendNormals) {
                const glm::vec3 n = !normals.empty() ? normals[i] : glm::vec3(0.0f, 1.0f, 0.0f);
                mesh->normals.push_back(glm::normalize(normalMatrix * n));
            }
            if (appendUvs) {
                mesh->texCoords.push_back(!uvs.empty() ? uvs[i] : glm::vec2(0.0f));
            }
        }
        for (uint32_t idx : indices) {
            mesh->indices.push_back(baseVertex + idx);
        }

        auto mat = ReadEmbeddedMaterial(j, baseDir, prim);
        if (!haveMaterial || (!haveTexturedMaterial && mat.HasTexture())) {
            firstMaterial = std::move(mat);
            haveMaterial = true;
            haveTexturedMaterial = firstMaterial.HasTexture();
        }
        ++primitiveCount;
        return Result<void>::Ok();
    };

    auto appendMesh = [&](int meshIndex, const glm::mat4& transform) -> Result<void> {
        if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes.size())) {
            return Result<void>::Ok();
        }
        const auto& jm = meshes[meshIndex];
        if (!jm.contains("primitives") || !jm["primitives"].is_array()) {
            return Result<void>::Ok();
        }
        for (const auto& prim : jm["primitives"]) {
            auto primRes = appendPrimitive(prim, transform);
            if (primRes.IsErr()) {
                return primRes;
            }
        }
        return Result<void>::Ok();
    };

    bool traversedScene = false;
    if (j.contains("nodes") && j["nodes"].is_array() && j.contains("scenes") && j["scenes"].is_array() &&
        !j["scenes"].empty()) {
        const int sceneIndex = j.value("scene", 0);
        if (sceneIndex >= 0 && sceneIndex < static_cast<int>(j["scenes"].size())) {
            const auto& scene = j["scenes"][sceneIndex];
            if (scene.contains("nodes") && scene["nodes"].is_array()) {
                std::function<Result<void>(int, const glm::mat4&)> visitNode =
                    [&](int nodeIndex, const glm::mat4& parent) -> Result<void> {
                    if (nodeIndex < 0 || nodeIndex >= static_cast<int>(j["nodes"].size())) {
                        return Result<void>::Ok();
                    }
                    const auto& node = j["nodes"][nodeIndex];
                    const glm::mat4 local = ReadNodeTransform(node);
                    const glm::mat4 world = parent * local;
                    if (node.contains("mesh")) {
                        auto meshRes = appendMesh(node["mesh"].get<int>(), world);
                        if (meshRes.IsErr()) {
                            return meshRes;
                        }
                    }
                    if (node.contains("children") && node["children"].is_array()) {
                        for (const auto& child : node["children"]) {
                            auto childRes = visitNode(child.get<int>(), world);
                            if (childRes.IsErr()) {
                                return childRes;
                            }
                        }
                    }
                    return Result<void>::Ok();
                };

                for (const auto& nodeIndex : scene["nodes"]) {
                    auto nodeRes = visitNode(nodeIndex.get<int>(), glm::mat4(1.0f));
                    if (nodeRes.IsErr()) {
                        return Result<std::shared_ptr<Scene::MeshData>>::Err(nodeRes.Error());
                    }
                    traversedScene = true;
                }
            }
        }
    }

    if (!traversedScene) {
        for (int mi = 0; mi < static_cast<int>(meshes.size()); ++mi) {
            auto meshRes = appendMesh(mi, glm::mat4(1.0f));
            if (meshRes.IsErr()) {
                return Result<std::shared_ptr<Scene::MeshData>>::Err(meshRes.Error());
            }
        }
    }

    if (mesh->positions.empty() || mesh->indices.empty()) {
        return Result<std::shared_ptr<Scene::MeshData>>::Err("gltf has no loadable triangle primitives");
    }
    if (haveMaterial) {
        mesh->embeddedMaterial = std::move(firstMaterial);
    }
    mesh->UpdateBounds();

    spdlog::info("Loaded glTF mesh '{}' (verts={}, indices={}, primitives={}, skippedBudget={}, pbrTextures={})",
                 path.string(),
                 mesh->positions.size(),
                 mesh->indices.size(),
                 primitiveCount,
                 skippedBudgetPrimitives,
                 mesh->embeddedMaterial.HasTexture() ? "yes" : "no");

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

    fs::path modelsRoot;
    for (fs::path root = cwd; !root.empty(); root = root.parent_path()) {
        fs::path candidate = root / "glTF-Sample-Models" / "2.0";
        if (fs::exists(candidate)) {
            modelsRoot = candidate;
            break;
        }
        if (root == root.parent_path()) {
            break;
        }
    }

    if (!fs::exists(modelsRoot)) {
        spdlog::info("SampleModelLibrary: glTF-Sample-Models repo not found while walking upward from '{}'", cwd.string());
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
