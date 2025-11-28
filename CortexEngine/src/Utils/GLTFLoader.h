#pragma once

#include <memory>
#include <string>
#include "Utils/Result.h"
#include "Scene/Components.h"

namespace Cortex::Utils {

// Very small glTF 2.0 mesh loader tailored for the Khronos sample models.
// Supports:
//   - .gltf (JSON) with external .bin buffers
//   - single mesh / single primitive
//   - POSITION, NORMAL, TEXCOORD_0 attributes (float)
//   - uint16/uint32 indices
Result<std::shared_ptr<Scene::MeshData>> LoadGLTFMesh(const std::string& path);

// Sample model library -------------------------------------------------------
//
// The engine ships with the Khronos glTF-Sample-Models repository checked out
// under graphics/glTF-Sample-Models. These helpers provide a lightweight
// registry so higher-level systems can spawn models by name instead of hard-
// coding file paths.
//
// InitializeSampleModelLibrary:
//   - Locates graphics/glTF-Sample-Models/2.0/model-index.json based on the
//     current working directory (typically CortexEngine/build or build/bin).
//   - Registers every entry that exposes a "glTF" variant in its variants map.
//   - Is safe to call multiple times; subsequent calls are no-ops.
Result<void> InitializeSampleModelLibrary();

// LoadSampleModelMesh:
//   - Looks up a model by logical name (e.g., "DamagedHelmet",
//     "DragonAttenuation") and loads its glTF mesh via LoadGLTFMesh.
//   - Name matching is case-insensitive.
Result<std::shared_ptr<Scene::MeshData>> LoadSampleModelMesh(const std::string& assetName);

} // namespace Cortex::Utils
