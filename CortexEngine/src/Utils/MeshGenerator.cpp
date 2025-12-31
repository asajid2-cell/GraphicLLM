#include "MeshGenerator.h"
#include "Scene/BiomeMap.h"
#include "Scene/BiomeTypes.h"
#include <cmath>
#include <unordered_map>

namespace Cortex::Utils {

std::shared_ptr<Scene::MeshData> MeshGenerator::CreateCube() {
    auto mesh = std::make_shared<Scene::MeshData>();

    // Cube vertices (unique per face for proper normals and UVs)
    // 24 vertices (6 faces * 4 vertices)

    // Front face (+Z)
    mesh->positions.push_back(glm::vec3(-0.5f, -0.5f,  0.5f));
    mesh->positions.push_back(glm::vec3( 0.5f, -0.5f,  0.5f));
    mesh->positions.push_back(glm::vec3( 0.5f,  0.5f,  0.5f));
    mesh->positions.push_back(glm::vec3(-0.5f,  0.5f,  0.5f));

    // Back face (-Z)
    mesh->positions.push_back(glm::vec3( 0.5f, -0.5f, -0.5f));
    mesh->positions.push_back(glm::vec3(-0.5f, -0.5f, -0.5f));
    mesh->positions.push_back(glm::vec3(-0.5f,  0.5f, -0.5f));
    mesh->positions.push_back(glm::vec3( 0.5f,  0.5f, -0.5f));

    // Top face (+Y)
    mesh->positions.push_back(glm::vec3(-0.5f,  0.5f,  0.5f));
    mesh->positions.push_back(glm::vec3( 0.5f,  0.5f,  0.5f));
    mesh->positions.push_back(glm::vec3( 0.5f,  0.5f, -0.5f));
    mesh->positions.push_back(glm::vec3(-0.5f,  0.5f, -0.5f));

    // Bottom face (-Y)
    mesh->positions.push_back(glm::vec3(-0.5f, -0.5f, -0.5f));
    mesh->positions.push_back(glm::vec3( 0.5f, -0.5f, -0.5f));
    mesh->positions.push_back(glm::vec3( 0.5f, -0.5f,  0.5f));
    mesh->positions.push_back(glm::vec3(-0.5f, -0.5f,  0.5f));

    // Right face (+X)
    mesh->positions.push_back(glm::vec3( 0.5f, -0.5f,  0.5f));
    mesh->positions.push_back(glm::vec3( 0.5f, -0.5f, -0.5f));
    mesh->positions.push_back(glm::vec3( 0.5f,  0.5f, -0.5f));
    mesh->positions.push_back(glm::vec3( 0.5f,  0.5f,  0.5f));

    // Left face (-X)
    mesh->positions.push_back(glm::vec3(-0.5f, -0.5f, -0.5f));
    mesh->positions.push_back(glm::vec3(-0.5f, -0.5f,  0.5f));
    mesh->positions.push_back(glm::vec3(-0.5f,  0.5f,  0.5f));
    mesh->positions.push_back(glm::vec3(-0.5f,  0.5f, -0.5f));

    // Normals (one per face, repeated for each vertex)
    for (int i = 0; i < 4; i++) mesh->normals.push_back(glm::vec3( 0.0f,  0.0f,  1.0f)); // Front
    for (int i = 0; i < 4; i++) mesh->normals.push_back(glm::vec3( 0.0f,  0.0f, -1.0f)); // Back
    for (int i = 0; i < 4; i++) mesh->normals.push_back(glm::vec3( 0.0f,  1.0f,  0.0f)); // Top
    for (int i = 0; i < 4; i++) mesh->normals.push_back(glm::vec3( 0.0f, -1.0f,  0.0f)); // Bottom
    for (int i = 0; i < 4; i++) mesh->normals.push_back(glm::vec3( 1.0f,  0.0f,  0.0f)); // Right
    for (int i = 0; i < 4; i++) mesh->normals.push_back(glm::vec3(-1.0f,  0.0f,  0.0f)); // Left

    // Texture coordinates (same for each face)
    for (int face = 0; face < 6; face++) {
        mesh->texCoords.push_back(glm::vec2(0.0f, 1.0f)); // Bottom-left
        mesh->texCoords.push_back(glm::vec2(1.0f, 1.0f)); // Bottom-right
        mesh->texCoords.push_back(glm::vec2(1.0f, 0.0f)); // Top-right
        mesh->texCoords.push_back(glm::vec2(0.0f, 0.0f)); // Top-left
    }

    // Indices (2 triangles per face, CW winding produces outward-facing normals)
    // For each face, we compute cross((v[1]-v[0]), (v[2]-v[0])) to get the normal.

    // Front face (+Z): vertices 0,1,2,3 -> cross gives (0,0,+1)
    mesh->indices.push_back(0); mesh->indices.push_back(1); mesh->indices.push_back(2);
    mesh->indices.push_back(0); mesh->indices.push_back(2); mesh->indices.push_back(3);

    // Back face (-Z): vertices 4,5,6,7 -> cross gives (0,0,-1)
    mesh->indices.push_back(4); mesh->indices.push_back(5); mesh->indices.push_back(6);
    mesh->indices.push_back(4); mesh->indices.push_back(6); mesh->indices.push_back(7);

    // Top face (+Y): vertices 8,9,10,11 -> cross gives (0,+1,0)
    mesh->indices.push_back(8); mesh->indices.push_back(9); mesh->indices.push_back(10);
    mesh->indices.push_back(8); mesh->indices.push_back(10); mesh->indices.push_back(11);

    // Bottom face (-Y): vertices 12,13,14,15 -> cross gives (0,-1,0)
    mesh->indices.push_back(12); mesh->indices.push_back(13); mesh->indices.push_back(14);
    mesh->indices.push_back(12); mesh->indices.push_back(14); mesh->indices.push_back(15);

    // Right face (+X): vertices 16,17,18,19 -> cross gives (+1,0,0)
    mesh->indices.push_back(16); mesh->indices.push_back(17); mesh->indices.push_back(18);
    mesh->indices.push_back(16); mesh->indices.push_back(18); mesh->indices.push_back(19);

    // Left face (-X): vertices 20,21,22,23 -> cross gives (-1,0,0)
    mesh->indices.push_back(20); mesh->indices.push_back(21); mesh->indices.push_back(22);
    mesh->indices.push_back(20); mesh->indices.push_back(22); mesh->indices.push_back(23);

    mesh->UpdateBounds();
    return mesh;
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreatePlane(float width, float height) {
    auto mesh = std::make_shared<Scene::MeshData>();

    float halfW = width * 0.5f;
    float halfH = height * 0.5f;

    // Positions
    mesh->positions.push_back(glm::vec3(-halfW, 0.0f,  halfH));
    mesh->positions.push_back(glm::vec3( halfW, 0.0f,  halfH));
    mesh->positions.push_back(glm::vec3( halfW, 0.0f, -halfH));
    mesh->positions.push_back(glm::vec3(-halfW, 0.0f, -halfH));

    // Normals (all pointing up)
    for (int i = 0; i < 4; i++) {
        mesh->normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
    }

    // Texture coordinates
    mesh->texCoords.push_back(glm::vec2(0.0f, 1.0f));
    mesh->texCoords.push_back(glm::vec2(1.0f, 1.0f));
    mesh->texCoords.push_back(glm::vec2(1.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(0.0f, 0.0f));

    // Indices: cross((v1-v0), (v2-v0)) = (0, +1, 0) for +Y normal
    mesh->indices = { 0, 1, 2, 0, 2, 3 };

    mesh->UpdateBounds();
    return mesh;
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreateQuad(float width, float height) {
    auto mesh = std::make_shared<Scene::MeshData>();

    float halfW = width * 0.5f;
    float halfH = height * 0.5f;

    // Positions in XY plane, facing +Z
    mesh->positions.push_back(glm::vec3(-halfW, -halfH, 0.0f));
    mesh->positions.push_back(glm::vec3( halfW, -halfH, 0.0f));
    mesh->positions.push_back(glm::vec3( halfW,  halfH, 0.0f));
    mesh->positions.push_back(glm::vec3(-halfW,  halfH, 0.0f));

    // Normals (all pointing +Z)
    for (int i = 0; i < 4; ++i) {
        mesh->normals.push_back(glm::vec3(0.0f, 0.0f, 1.0f));
    }

    // Texture coordinates
    mesh->texCoords.push_back(glm::vec2(0.0f, 1.0f));
    mesh->texCoords.push_back(glm::vec2(1.0f, 1.0f));
    mesh->texCoords.push_back(glm::vec2(1.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(0.0f, 0.0f));

    // Indices: cross((v1-v0), (v2-v0)) = (0, 0, +1) for +Z normal
    mesh->indices = { 0, 1, 2, 0, 2, 3 };

    mesh->UpdateBounds();
    return mesh;
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreateSphere(float radius, uint32_t segments) {
    auto mesh = std::make_shared<Scene::MeshData>();

    // Generate sphere using spherical coordinates
    for (uint32_t y = 0; y <= segments; y++) {
        for (uint32_t x = 0; x <= segments; x++) {
            float xSegment = static_cast<float>(x) / static_cast<float>(segments);
            float ySegment = static_cast<float>(y) / static_cast<float>(segments);

            float xPos = std::cos(xSegment * 2.0f * glm::pi<float>()) * std::sin(ySegment * glm::pi<float>());
            float yPos = std::cos(ySegment * glm::pi<float>());
            float zPos = std::sin(xSegment * 2.0f * glm::pi<float>()) * std::sin(ySegment * glm::pi<float>());

            mesh->positions.push_back(glm::vec3(xPos * radius, yPos * radius, zPos * radius));
            mesh->normals.push_back(glm::vec3(xPos, yPos, zPos));
            mesh->texCoords.push_back(glm::vec2(xSegment, ySegment));
        }
    }

    // Generate indices for outward-facing triangles
    // Vertex layout per quad: i0 (current), i1 (below), i2 (right), i3 (below-right)
    // cross(i1-i0, i3-i0) and cross(i3-i0, i2-i0) both point outward
    for (uint32_t y = 0; y < segments; y++) {
        for (uint32_t x = 0; x < segments; x++) {
            uint32_t i0 = y * (segments + 1) + x;
            uint32_t i1 = i0 + segments + 1;
            uint32_t i2 = i0 + 1;
            uint32_t i3 = i1 + 1;

            // Triangle 1: i0 -> i1 -> i3 (outward normal)
            mesh->indices.push_back(i0);
            mesh->indices.push_back(i1);
            mesh->indices.push_back(i3);

            // Triangle 2: i0 -> i3 -> i2 (outward normal)
            mesh->indices.push_back(i0);
            mesh->indices.push_back(i3);
            mesh->indices.push_back(i2);
        }
    }

    mesh->UpdateBounds();
    return mesh;
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreateCylinder(float radius, float height, uint32_t segments) {
    auto mesh = std::make_shared<Scene::MeshData>();

    const float halfHeight = height * 0.5f;

    // Generate vertices for top and bottom circles
    for (uint32_t i = 0; i <= segments; i++) {
        float angle = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * glm::pi<float>();
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;

        // Top circle
        mesh->positions.push_back(glm::vec3(x, halfHeight, z));
        mesh->normals.push_back(glm::normalize(glm::vec3(x, 0.0f, z)));
        mesh->texCoords.push_back(glm::vec2(static_cast<float>(i) / static_cast<float>(segments), 1.0f));

        // Bottom circle
        mesh->positions.push_back(glm::vec3(x, -halfHeight, z));
        mesh->normals.push_back(glm::normalize(glm::vec3(x, 0.0f, z)));
        mesh->texCoords.push_back(glm::vec2(static_cast<float>(i) / static_cast<float>(segments), 0.0f));
    }

    // Generate indices for cylinder sides (CW winding for front faces)
    for (uint32_t i = 0; i < segments; i++) {
        uint32_t i0 = i * 2;
        uint32_t i1 = i0 + 1;
        uint32_t i2 = (i0 + 2) % ((segments + 1) * 2);
        uint32_t i3 = (i0 + 3) % ((segments + 1) * 2);

        // Triangle 1 (CW: i0 -> i2 -> i1)
        mesh->indices.push_back(i0);
        mesh->indices.push_back(i2);
        mesh->indices.push_back(i1);

        // Triangle 2 (CW: i1 -> i2 -> i3)
        mesh->indices.push_back(i1);
        mesh->indices.push_back(i2);
        mesh->indices.push_back(i3);
    }

    // Add top cap center vertex
    uint32_t topCenterIdx = static_cast<uint32_t>(mesh->positions.size());
    mesh->positions.push_back(glm::vec3(0.0f, halfHeight, 0.0f));
    mesh->normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(0.5f, 0.5f));

    // Add bottom cap center vertex
    uint32_t bottomCenterIdx = static_cast<uint32_t>(mesh->positions.size());
    mesh->positions.push_back(glm::vec3(0.0f, -halfHeight, 0.0f));
    mesh->normals.push_back(glm::vec3(0.0f, -1.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(0.5f, 0.5f));

    // Generate cap vertices with correct normals
    for (uint32_t i = 0; i <= segments; i++) {
        float angle = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * glm::pi<float>();
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;

        // Top cap
        mesh->positions.push_back(glm::vec3(x, halfHeight, z));
        mesh->normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
        mesh->texCoords.push_back(glm::vec2(0.5f + x / (2.0f * radius), 0.5f + z / (2.0f * radius)));

        // Bottom cap
        mesh->positions.push_back(glm::vec3(x, -halfHeight, z));
        mesh->normals.push_back(glm::vec3(0.0f, -1.0f, 0.0f));
        mesh->texCoords.push_back(glm::vec2(0.5f + x / (2.0f * radius), 0.5f - z / (2.0f * radius)));
    }

    // Generate indices for caps
    uint32_t topCapStart = topCenterIdx + 2;
    uint32_t bottomCapStart = topCapStart + 1;

    for (uint32_t i = 0; i < segments; i++) {
        // Top cap (CW winding when viewed from above)
        mesh->indices.push_back(topCenterIdx);
        mesh->indices.push_back(topCapStart + ((i + 1) % (segments + 1)) * 2);
        mesh->indices.push_back(topCapStart + i * 2);

        // Bottom cap (CW winding when viewed from below)
        mesh->indices.push_back(bottomCenterIdx);
        mesh->indices.push_back(bottomCapStart + i * 2);
        mesh->indices.push_back(bottomCapStart + ((i + 1) % (segments + 1)) * 2);
    }

    mesh->UpdateBounds();
    return mesh;
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreatePyramid(float baseSize, float height) {
    auto mesh = std::make_shared<Scene::MeshData>();

    const float halfBase = baseSize * 0.5f;

    // Base vertices (bottom square)
    mesh->positions.push_back(glm::vec3(-halfBase, 0.0f,  halfBase)); // 0
    mesh->positions.push_back(glm::vec3( halfBase, 0.0f,  halfBase)); // 1
    mesh->positions.push_back(glm::vec3( halfBase, 0.0f, -halfBase)); // 2
    mesh->positions.push_back(glm::vec3(-halfBase, 0.0f, -halfBase)); // 3

    // Apex (top point)
    mesh->positions.push_back(glm::vec3(0.0f, height, 0.0f)); // 4

    // Calculate normals for each face
    auto calcNormal = [](const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3) {
        return glm::normalize(glm::cross(p2 - p1, p3 - p1));
    };

    // Base normals (pointing down)
    for (int i = 0; i < 4; i++) {
        mesh->normals.push_back(glm::vec3(0.0f, -1.0f, 0.0f));
    }

    // Apex normal (will be overridden per face)
    mesh->normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));

    // Base texture coordinates
    mesh->texCoords.push_back(glm::vec2(0.0f, 1.0f));
    mesh->texCoords.push_back(glm::vec2(1.0f, 1.0f));
    mesh->texCoords.push_back(glm::vec2(1.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(0.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(0.5f, 0.5f)); // Apex

    // Base indices (2 triangles)
    mesh->indices.push_back(0);
    mesh->indices.push_back(2);
    mesh->indices.push_back(1);
    mesh->indices.push_back(0);
    mesh->indices.push_back(3);
    mesh->indices.push_back(2);

    // Side faces - we need to add duplicate vertices for proper normals
    // Front face (facing +Z)
    glm::vec3 frontNormal = calcNormal(mesh->positions[0], mesh->positions[1], mesh->positions[4]);
    uint32_t baseIdx = static_cast<uint32_t>(mesh->positions.size());

    mesh->positions.push_back(mesh->positions[0]);
    mesh->positions.push_back(mesh->positions[1]);
    mesh->positions.push_back(mesh->positions[4]);
    mesh->normals.push_back(frontNormal);
    mesh->normals.push_back(frontNormal);
    mesh->normals.push_back(frontNormal);
    mesh->texCoords.push_back(glm::vec2(0.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(1.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(0.5f, 1.0f));
    // Front face side triangle (CW winding)
    mesh->indices.push_back(baseIdx + 0);
    mesh->indices.push_back(baseIdx + 2);
    mesh->indices.push_back(baseIdx + 1);

    // Right face (facing +X)
    glm::vec3 rightNormal = calcNormal(mesh->positions[1], mesh->positions[2], mesh->positions[4]);
    baseIdx = static_cast<uint32_t>(mesh->positions.size());

    mesh->positions.push_back(mesh->positions[1]);
    mesh->positions.push_back(mesh->positions[2]);
    mesh->positions.push_back(mesh->positions[4]);
    mesh->normals.push_back(rightNormal);
    mesh->normals.push_back(rightNormal);
    mesh->normals.push_back(rightNormal);
    mesh->texCoords.push_back(glm::vec2(0.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(1.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(0.5f, 1.0f));
    // Right face side triangle (CW winding)
    mesh->indices.push_back(baseIdx + 0);
    mesh->indices.push_back(baseIdx + 2);
    mesh->indices.push_back(baseIdx + 1);

    // Back face (facing -Z)
    glm::vec3 backNormal = calcNormal(mesh->positions[2], mesh->positions[3], mesh->positions[4]);
    baseIdx = static_cast<uint32_t>(mesh->positions.size());

    mesh->positions.push_back(mesh->positions[2]);
    mesh->positions.push_back(mesh->positions[3]);
    mesh->positions.push_back(mesh->positions[4]);
    mesh->normals.push_back(backNormal);
    mesh->normals.push_back(backNormal);
    mesh->normals.push_back(backNormal);
    mesh->texCoords.push_back(glm::vec2(0.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(1.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(0.5f, 1.0f));
    // Back face side triangle (CW winding)
    mesh->indices.push_back(baseIdx + 0);
    mesh->indices.push_back(baseIdx + 2);
    mesh->indices.push_back(baseIdx + 1);

    // Left face (facing -X)
    glm::vec3 leftNormal = calcNormal(mesh->positions[3], mesh->positions[0], mesh->positions[4]);
    baseIdx = static_cast<uint32_t>(mesh->positions.size());

    mesh->positions.push_back(mesh->positions[3]);
    mesh->positions.push_back(mesh->positions[0]);
    mesh->positions.push_back(mesh->positions[4]);
    mesh->normals.push_back(leftNormal);
    mesh->normals.push_back(leftNormal);
    mesh->normals.push_back(leftNormal);
    mesh->texCoords.push_back(glm::vec2(0.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(1.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(0.5f, 1.0f));
    // Left face side triangle (CW winding)
    mesh->indices.push_back(baseIdx + 0);
    mesh->indices.push_back(baseIdx + 2);
    mesh->indices.push_back(baseIdx + 1);

    mesh->UpdateBounds();
    return mesh;
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreateCone(float radius, float height, uint32_t segments) {
    auto mesh = std::make_shared<Scene::MeshData>();

    // Apex (top point)
    glm::vec3 apex(0.0f, height, 0.0f);

    // Generate base circle vertices
    for (uint32_t i = 0; i <= segments; i++) {
        float angle = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * glm::pi<float>();
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;

        // Base vertex
        mesh->positions.push_back(glm::vec3(x, 0.0f, z));
        mesh->texCoords.push_back(glm::vec2(static_cast<float>(i) / static_cast<float>(segments), 0.0f));
    }

    // Add apex for side faces
    uint32_t apexIdx = static_cast<uint32_t>(mesh->positions.size());
    mesh->positions.push_back(apex);
    mesh->texCoords.push_back(glm::vec2(0.5f, 1.0f));

    // Calculate normals for side faces and generate indices
    for (uint32_t i = 0; i < segments; i++) {
        uint32_t i0 = i;
        uint32_t i1 = i + 1;

        // Calculate face normal
        glm::vec3 v0 = mesh->positions[i0];
        glm::vec3 v1 = mesh->positions[i1];
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = apex - v0;
        glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

        // Add normals for this triangle's vertices
        mesh->normals.push_back(normal); // For v0
        mesh->normals.push_back(normal); // For v1 (will be added separately)
    }

    // Add normals for base
    for (uint32_t i = 0; i <= segments; i++) {
        mesh->normals.push_back(glm::vec3(0.0f, -1.0f, 0.0f));
    }

    // Add normal for apex
    mesh->normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));

    // Rebuild with proper per-vertex normals for side faces
    mesh->positions.clear();
    mesh->normals.clear();
    mesh->texCoords.clear();

    // Side faces - duplicate vertices for proper normals
    for (uint32_t i = 0; i < segments; i++) {
        float angle0 = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * glm::pi<float>();
        float angle1 = (static_cast<float>(i + 1) / static_cast<float>(segments)) * 2.0f * glm::pi<float>();

        glm::vec3 v0(std::cos(angle0) * radius, 0.0f, std::sin(angle0) * radius);
        glm::vec3 v1(std::cos(angle1) * radius, 0.0f, std::sin(angle1) * radius);

        // Calculate face normal (cross order matches triangle winding for outward normal)
        glm::vec3 edge1 = apex - v0;
        glm::vec3 edge2 = v1 - v0;
        glm::vec3 normal = glm::normalize(glm::cross(edge1, edge2));

        uint32_t baseIdx = static_cast<uint32_t>(mesh->positions.size());

        // Add triangle vertices with normals
        mesh->positions.push_back(v0);
        mesh->normals.push_back(normal);
        mesh->texCoords.push_back(glm::vec2(static_cast<float>(i) / static_cast<float>(segments), 0.0f));

        mesh->positions.push_back(v1);
        mesh->normals.push_back(normal);
        mesh->texCoords.push_back(glm::vec2(static_cast<float>(i + 1) / static_cast<float>(segments), 0.0f));

        mesh->positions.push_back(apex);
        mesh->normals.push_back(normal);
        mesh->texCoords.push_back(glm::vec2(0.5f, 1.0f));

        // Cone side triangle indices (CW winding: v0 -> apex -> v1)
        mesh->indices.push_back(baseIdx + 0);
        mesh->indices.push_back(baseIdx + 2);
        mesh->indices.push_back(baseIdx + 1);
    }

    // Base cap - center vertex
    uint32_t baseCenterIdx = static_cast<uint32_t>(mesh->positions.size());
    mesh->positions.push_back(glm::vec3(0.0f, 0.0f, 0.0f));
    mesh->normals.push_back(glm::vec3(0.0f, -1.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(0.5f, 0.5f));

    // Base cap vertices
    for (uint32_t i = 0; i <= segments; i++) {
        float angle = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * glm::pi<float>();
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;

        mesh->positions.push_back(glm::vec3(x, 0.0f, z));
        mesh->normals.push_back(glm::vec3(0.0f, -1.0f, 0.0f));
        mesh->texCoords.push_back(glm::vec2(0.5f + x / (2.0f * radius), 0.5f - z / (2.0f * radius)));
    }

    // Base cap indices (CW winding when viewed from below)
    uint32_t baseCapStart = baseCenterIdx + 1;
    for (uint32_t i = 0; i < segments; i++) {
        mesh->indices.push_back(baseCenterIdx);
        mesh->indices.push_back(baseCapStart + i);
        mesh->indices.push_back(baseCapStart + i + 1);
    }

    mesh->UpdateBounds();
    return mesh;
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreateTorus(float majorRadius, float minorRadius, uint32_t majorSegments, uint32_t minorSegments) {
    auto mesh = std::make_shared<Scene::MeshData>();

    // Generate torus vertices
    for (uint32_t i = 0; i <= majorSegments; i++) {
        float theta = (static_cast<float>(i) / static_cast<float>(majorSegments)) * 2.0f * glm::pi<float>();
        float cosTheta = std::cos(theta);
        float sinTheta = std::sin(theta);

        for (uint32_t j = 0; j <= minorSegments; j++) {
            float phi = (static_cast<float>(j) / static_cast<float>(minorSegments)) * 2.0f * glm::pi<float>();
            float cosPhi = std::cos(phi);
            float sinPhi = std::sin(phi);

            // Torus parametric equations
            float x = (majorRadius + minorRadius * cosPhi) * cosTheta;
            float y = minorRadius * sinPhi;
            float z = (majorRadius + minorRadius * cosPhi) * sinTheta;

            // Normal calculation
            glm::vec3 center(majorRadius * cosTheta, 0.0f, majorRadius * sinTheta);
            glm::vec3 pos(x, y, z);
            glm::vec3 normal = glm::normalize(pos - center);

            mesh->positions.push_back(pos);
            mesh->normals.push_back(normal);
            mesh->texCoords.push_back(glm::vec2(
                static_cast<float>(i) / static_cast<float>(majorSegments),
                static_cast<float>(j) / static_cast<float>(minorSegments)
            ));
        }
    }

    // Generate indices for outward-facing triangles
    // Vertex layout per quad: i0 (current), i1 (next major ring), i2 (next minor), i3 (both next)
    // Same pattern as sphere: cross(i1-i0, i3-i0) and cross(i3-i0, i2-i0) point outward
    for (uint32_t i = 0; i < majorSegments; i++) {
        for (uint32_t j = 0; j < minorSegments; j++) {
            uint32_t i0 = i * (minorSegments + 1) + j;
            uint32_t i1 = i0 + minorSegments + 1;
            uint32_t i2 = i0 + 1;
            uint32_t i3 = i1 + 1;

            // Triangle 1: i0 -> i1 -> i3 (outward normal)
            mesh->indices.push_back(i0);
            mesh->indices.push_back(i1);
            mesh->indices.push_back(i3);

            // Triangle 2: i0 -> i3 -> i2 (outward normal)
            mesh->indices.push_back(i0);
            mesh->indices.push_back(i3);
            mesh->indices.push_back(i2);
        }
    }

    mesh->UpdateBounds();
    return mesh;
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreateDisk(float radius, uint32_t segments) {
    auto mesh = std::make_shared<Scene::MeshData>();

    // Center vertex
    mesh->positions.push_back(glm::vec3(0.0f, 0.0f, 0.0f));
    mesh->normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
    mesh->texCoords.push_back(glm::vec2(0.5f, 0.5f));

    // Rim vertices
    for (uint32_t i = 0; i <= segments; ++i) {
        float angle = (static_cast<float>(i) / static_cast<float>(segments)) * 2.0f * glm::pi<float>();
        float x = std::cos(angle) * radius;
        float z = std::sin(angle) * radius;

        mesh->positions.push_back(glm::vec3(x, 0.0f, z));
        mesh->normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
        mesh->texCoords.push_back(glm::vec2(
            0.5f + x / (2.0f * radius),
            0.5f - z / (2.0f * radius)));
    }

    // Indices for triangle fan (CW winding when viewed from above)
    for (uint32_t i = 1; i <= segments; ++i) {
        mesh->indices.push_back(0);
        mesh->indices.push_back((i % segments) + 1);
        mesh->indices.push_back(i);
    }

    mesh->UpdateBounds();
    return mesh;
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreateCapsule(float radius, float height, uint32_t segments) {
    // For now, approximate a capsule as a cylinder with caps. This keeps the
    // API available for higher-level systems while reusing the well-tested
    // cylinder generator.
    return CreateCylinder(radius, height + 2.0f * radius, segments);
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreateLine(float length, float thickness) {
    // Represent a line segment as a thin box aligned along the X axis so it
    // can be rendered with the standard triangle pipeline and scaled/rotated
    // via TransformComponent.
    float halfL = length * 0.5f;
    float halfT = thickness * 0.5f;

    auto mesh = CreateCube();
    for (auto& p : mesh->positions) {
        p.x *= halfL * 2.0f;
        p.y *= halfT * 2.0f;
        p.z *= halfT * 2.0f;
    }
    return mesh;
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreateTerrainClipmapGrid(uint32_t gridDim, bool ring, bool skirts) {
    auto mesh = std::make_shared<Scene::MeshData>();

    const float cellSize = 1.0f;
    const float halfGrid = static_cast<float>(gridDim - 1) * cellSize * 0.5f;

    // Determine inner-hole bounds when ring == true.
    uint32_t holeStart = 0, holeEnd = 0;
    if (ring && gridDim >= 5) {
        uint32_t holeDim = (gridDim + 1) / 2;
        holeStart = (gridDim - holeDim) / 2;
        holeEnd = holeStart + holeDim;
    }

    auto inHole = [&](uint32_t x, uint32_t z) {
        return ring && (x >= holeStart && x < holeEnd && z >= holeStart && z < holeEnd);
    };

    // Vertex index map for deduplication.
    std::unordered_map<uint64_t, uint32_t> vertMap;
    auto getOrAddVertex = [&](uint32_t x, uint32_t z, bool isSkirt) -> uint32_t {
        uint64_t key = (static_cast<uint64_t>(x) << 32) | (static_cast<uint64_t>(z) << 1) | (isSkirt ? 1 : 0);
        auto it = vertMap.find(key);
        if (it != vertMap.end()) return it->second;

        float px = static_cast<float>(x) * cellSize - halfGrid;
        float pz = static_cast<float>(z) * cellSize - halfGrid;

        mesh->positions.push_back(glm::vec3(px, 0.0f, pz));
        mesh->normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
        // texCoord.y == 1 marks skirt vertices (used by VS to push down).
        mesh->texCoords.push_back(glm::vec2(static_cast<float>(x) / static_cast<float>(gridDim - 1), isSkirt ? 1.0f : 0.0f));

        uint32_t idx = static_cast<uint32_t>(mesh->positions.size()) - 1;
        vertMap[key] = idx;
        return idx;
    };

    // Main grid quads - using same CW winding as other mesh generators.
    for (uint32_t z = 0; z < gridDim - 1; ++z) {
        for (uint32_t x = 0; x < gridDim - 1; ++x) {
            // Skip quads entirely inside hole.
            if (inHole(x, z) && inHole(x + 1, z) && inHole(x, z + 1) && inHole(x + 1, z + 1)) continue;

            uint32_t i0 = getOrAddVertex(x, z, false);
            uint32_t i1 = getOrAddVertex(x + 1, z, false);
            uint32_t i2 = getOrAddVertex(x, z + 1, false);
            uint32_t i3 = getOrAddVertex(x + 1, z + 1, false);

            // Two triangles per quad (CW winding for +Y normal).
            mesh->indices.push_back(i0);
            mesh->indices.push_back(i2);
            mesh->indices.push_back(i1);

            mesh->indices.push_back(i1);
            mesh->indices.push_back(i2);
            mesh->indices.push_back(i3);
        }
    }

    // Skirt geometry at grid boundaries.
    if (skirts) {
        auto addSkirtQuad = [&](uint32_t x0, uint32_t z0, uint32_t x1, uint32_t z1) {
            uint32_t a = getOrAddVertex(x0, z0, false);
            uint32_t b = getOrAddVertex(x1, z1, false);
            uint32_t c = getOrAddVertex(x0, z0, true);
            uint32_t d = getOrAddVertex(x1, z1, true);

            // Two triangles (outward-facing from grid edge).
            mesh->indices.push_back(a);
            mesh->indices.push_back(c);
            mesh->indices.push_back(b);

            mesh->indices.push_back(b);
            mesh->indices.push_back(c);
            mesh->indices.push_back(d);
        };

        // Outer boundary skirts.
        for (uint32_t i = 0; i < gridDim - 1; ++i) {
            addSkirtQuad(i, 0, i + 1, 0);
            addSkirtQuad(i + 1, gridDim - 1, i, gridDim - 1);
            addSkirtQuad(0, i + 1, 0, i);
            addSkirtQuad(gridDim - 1, i, gridDim - 1, i + 1);
        }

        // Inner hole skirts (if ring).
        if (ring && holeEnd > holeStart) {
            for (uint32_t i = holeStart; i < holeEnd - 1; ++i) {
                addSkirtQuad(i + 1, holeStart, i, holeStart);
                addSkirtQuad(i, holeEnd - 1, i + 1, holeEnd - 1);
                addSkirtQuad(holeStart, i, holeStart, i + 1);
                addSkirtQuad(holeEnd - 1, i + 1, holeEnd - 1, i);
            }
        }
    }

    mesh->UpdateBounds();
    return mesh;
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreateTerrainHeightmapChunk(
    uint32_t gridDim,
    float chunkSize,
    int32_t chunkX,
    int32_t chunkZ,
    const Scene::TerrainNoiseParams& params,
    float skirtDepth
) {
    auto mesh = std::make_shared<Scene::MeshData>();

    const float cellSize = chunkSize / static_cast<float>(gridDim - 1);
    const float worldOffsetX = static_cast<float>(chunkX) * chunkSize;
    const float worldOffsetZ = static_cast<float>(chunkZ) * chunkSize;

    // Generate grid vertices with heights sampled from noise.
    for (uint32_t z = 0; z < gridDim; ++z) {
        for (uint32_t x = 0; x < gridDim; ++x) {
            float localX = static_cast<float>(x) * cellSize;
            float localZ = static_cast<float>(z) * cellSize;
            float worldX = worldOffsetX + localX;
            float worldZ = worldOffsetZ + localZ;

            float height = Scene::SampleTerrainHeight(worldX, worldZ, params);

            mesh->positions.push_back(glm::vec3(localX, height, localZ));
            mesh->texCoords.push_back(glm::vec2(static_cast<float>(x) / static_cast<float>(gridDim - 1),
                                                 static_cast<float>(z) / static_cast<float>(gridDim - 1)));
            mesh->normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));
        }
    }

    // Compute normals from finite differences.
    auto idx = [&](uint32_t x, uint32_t z) { return z * gridDim + x; };
    for (uint32_t z = 0; z < gridDim; ++z) {
        for (uint32_t x = 0; x < gridDim; ++x) {
            float hL = (x > 0) ? mesh->positions[idx(x - 1, z)].y : mesh->positions[idx(x, z)].y;
            float hR = (x < gridDim - 1) ? mesh->positions[idx(x + 1, z)].y : mesh->positions[idx(x, z)].y;
            float hD = (z > 0) ? mesh->positions[idx(x, z - 1)].y : mesh->positions[idx(x, z)].y;
            float hU = (z < gridDim - 1) ? mesh->positions[idx(x, z + 1)].y : mesh->positions[idx(x, z)].y;

            float dx = (hR - hL) / (2.0f * cellSize);
            float dz = (hU - hD) / (2.0f * cellSize);

            glm::vec3 n(-dx, 1.0f, -dz);
            float len2 = glm::dot(n, n);
            if (len2 > 1e-8f) n /= std::sqrt(len2);
            mesh->normals[idx(x, z)] = n;
        }
    }

    // Generate indices (CW winding for +Y normal, matching other generators).
    for (uint32_t z = 0; z < gridDim - 1; ++z) {
        for (uint32_t x = 0; x < gridDim - 1; ++x) {
            uint32_t i0 = idx(x, z);
            uint32_t i1 = idx(x + 1, z);
            uint32_t i2 = idx(x, z + 1);
            uint32_t i3 = idx(x + 1, z + 1);

            mesh->indices.push_back(i0);
            mesh->indices.push_back(i2);
            mesh->indices.push_back(i1);

            mesh->indices.push_back(i1);
            mesh->indices.push_back(i2);
            mesh->indices.push_back(i3);
        }
    }

    // Add skirt vertices at grid boundaries.
    uint32_t skirtBaseIdx = static_cast<uint32_t>(mesh->positions.size());
    auto addSkirtVertex = [&](uint32_t x, uint32_t z) {
        const glm::vec3& p = mesh->positions[idx(x, z)];
        mesh->positions.push_back(glm::vec3(p.x, p.y - skirtDepth, p.z));
        mesh->normals.push_back(mesh->normals[idx(x, z)]);
        mesh->texCoords.push_back(mesh->texCoords[idx(x, z)]);
    };

    for (uint32_t x = 0; x < gridDim; ++x) addSkirtVertex(x, 0);
    for (uint32_t x = 0; x < gridDim; ++x) addSkirtVertex(x, gridDim - 1);
    for (uint32_t z = 0; z < gridDim; ++z) addSkirtVertex(0, z);
    for (uint32_t z = 0; z < gridDim; ++z) addSkirtVertex(gridDim - 1, z);

    auto skirtIdx = [&](uint32_t edge, uint32_t i) -> uint32_t {
        return skirtBaseIdx + edge * gridDim + i;
    };

    // Bottom edge skirt (z=0) - normal should face -Z (outward)
    // Reversed winding: (a,b,c) instead of (a,c,b) to flip normal direction
    for (uint32_t x = 0; x < gridDim - 1; ++x) {
        uint32_t a = idx(x, 0), b = idx(x + 1, 0);
        uint32_t c = skirtIdx(0, x), d = skirtIdx(0, x + 1);
        mesh->indices.push_back(a); mesh->indices.push_back(b); mesh->indices.push_back(c);
        mesh->indices.push_back(b); mesh->indices.push_back(d); mesh->indices.push_back(c);
    }
    // Top edge skirt (z=gridDim-1) - normal should face +Z (outward)
    for (uint32_t x = 0; x < gridDim - 1; ++x) {
        uint32_t a = idx(x + 1, gridDim - 1), b = idx(x, gridDim - 1);
        uint32_t c = skirtIdx(1, x + 1), d = skirtIdx(1, x);
        mesh->indices.push_back(a); mesh->indices.push_back(b); mesh->indices.push_back(c);
        mesh->indices.push_back(b); mesh->indices.push_back(d); mesh->indices.push_back(c);
    }
    // Left edge skirt (x=0) - normal should face -X (outward)
    for (uint32_t z = 0; z < gridDim - 1; ++z) {
        uint32_t a = idx(0, z + 1), b = idx(0, z);
        uint32_t c = skirtIdx(2, z + 1), d = skirtIdx(2, z);
        mesh->indices.push_back(a); mesh->indices.push_back(b); mesh->indices.push_back(c);
        mesh->indices.push_back(b); mesh->indices.push_back(d); mesh->indices.push_back(c);
    }
    // Right edge skirt (x=gridDim-1) - normal should face +X (outward)
    for (uint32_t z = 0; z < gridDim - 1; ++z) {
        uint32_t a = idx(gridDim - 1, z), b = idx(gridDim - 1, z + 1);
        uint32_t c = skirtIdx(3, z), d = skirtIdx(3, z + 1);
        mesh->indices.push_back(a); mesh->indices.push_back(b); mesh->indices.push_back(c);
        mesh->indices.push_back(b); mesh->indices.push_back(d); mesh->indices.push_back(c);
    }

    mesh->UpdateBounds();
    return mesh;
}

std::shared_ptr<Scene::MeshData> MeshGenerator::CreateTerrainHeightmapChunkWithBiomes(
    uint32_t gridDim,
    float chunkSize,
    int32_t chunkX,
    int32_t chunkZ,
    const Scene::TerrainNoiseParams& params,
    const Scene::BiomeMap* biomeMap,
    float skirtDepth
) {
    auto mesh = std::make_shared<Scene::MeshData>();

    // If no biome map provided, fall back to standard generation
    if (!biomeMap || !biomeMap->IsInitialized()) {
        return CreateTerrainHeightmapChunk(gridDim, chunkSize, chunkX, chunkZ, params, skirtDepth);
    }

    const float cellSize = chunkSize / static_cast<float>(gridDim - 1);
    const float worldOffsetX = static_cast<float>(chunkX) * chunkSize;
    const float worldOffsetZ = static_cast<float>(chunkZ) * chunkSize;

    // Generate grid vertices with heights sampled from noise, modified by biome.
    for (uint32_t z = 0; z < gridDim; ++z) {
        for (uint32_t x = 0; x < gridDim; ++x) {
            float localX = static_cast<float>(x) * cellSize;
            float localZ = static_cast<float>(z) * cellSize;
            float worldX = worldOffsetX + localX;
            float worldZ = worldOffsetZ + localZ;

            // Sample base terrain height
            float baseHeight = Scene::SampleTerrainHeight(worldX, worldZ, params);

            // Get blended height modifiers from biome
            float heightScale = biomeMap->GetBlendedHeightScale(worldX, worldZ);
            float heightOffset = biomeMap->GetBlendedHeightOffset(worldX, worldZ);

            // Apply biome height modifiers
            float finalHeight = baseHeight * heightScale + heightOffset;

            mesh->positions.push_back(glm::vec3(localX, finalHeight, localZ));
            mesh->texCoords.push_back(glm::vec2(
                static_cast<float>(x) / static_cast<float>(gridDim - 1),
                static_cast<float>(z) / static_cast<float>(gridDim - 1)
            ));
            mesh->normals.push_back(glm::vec3(0.0f, 1.0f, 0.0f));

            // Placeholder for vertex color - will be computed after normals
            mesh->colors.push_back(glm::vec4(1.0f));
        }
    }

    // Compute normals from finite differences and update vertex colors with height/slope data.
    auto idx = [&](uint32_t x, uint32_t z) { return z * gridDim + x; };
    for (uint32_t z = 0; z < gridDim; ++z) {
        for (uint32_t x = 0; x < gridDim; ++x) {
            float hL = (x > 0) ? mesh->positions[idx(x - 1, z)].y : mesh->positions[idx(x, z)].y;
            float hR = (x < gridDim - 1) ? mesh->positions[idx(x + 1, z)].y : mesh->positions[idx(x, z)].y;
            float hD = (z > 0) ? mesh->positions[idx(x, z - 1)].y : mesh->positions[idx(x, z)].y;
            float hU = (z < gridDim - 1) ? mesh->positions[idx(x, z + 1)].y : mesh->positions[idx(x, z)].y;

            float dx = (hR - hL) / (2.0f * cellSize);
            float dz = (hU - hD) / (2.0f * cellSize);

            glm::vec3 n(-dx, 1.0f, -dz);
            float len2 = glm::dot(n, n);
            if (len2 > 1e-8f) n /= std::sqrt(len2);
            mesh->normals[idx(x, z)] = n;

            // Compute slope from normal (0 = flat, 1 = vertical)
            float slope = 1.0f - n.y; // n.y = 1 for flat, 0 for vertical

            // Get world position for biome sampling
            float localX = static_cast<float>(x) * cellSize;
            float localZ = static_cast<float>(z) * cellSize;
            float worldX = worldOffsetX + localX;
            float worldZ = worldOffsetZ + localZ;
            float height = mesh->positions[idx(x, z)].y;

            // Get height and slope-aware biome color
            glm::vec3 biomeColor = biomeMap->GetHeightLayeredColor(worldX, worldZ, height, slope);
            mesh->colors[idx(x, z)] = glm::vec4(biomeColor, 1.0f);
        }
    }

    // Generate indices (CW winding for +Y normal, matching other generators).
    for (uint32_t z = 0; z < gridDim - 1; ++z) {
        for (uint32_t x = 0; x < gridDim - 1; ++x) {
            uint32_t i0 = idx(x, z);
            uint32_t i1 = idx(x + 1, z);
            uint32_t i2 = idx(x, z + 1);
            uint32_t i3 = idx(x + 1, z + 1);

            mesh->indices.push_back(i0);
            mesh->indices.push_back(i2);
            mesh->indices.push_back(i1);

            mesh->indices.push_back(i1);
            mesh->indices.push_back(i2);
            mesh->indices.push_back(i3);
        }
    }

    // Add skirt vertices at grid boundaries.
    uint32_t skirtBaseIdx = static_cast<uint32_t>(mesh->positions.size());
    auto addSkirtVertex = [&](uint32_t x, uint32_t z) {
        const glm::vec3& p = mesh->positions[idx(x, z)];
        mesh->positions.push_back(glm::vec3(p.x, p.y - skirtDepth, p.z));
        mesh->normals.push_back(mesh->normals[idx(x, z)]);
        mesh->texCoords.push_back(mesh->texCoords[idx(x, z)]);
        // Copy biome color to skirt vertex
        mesh->colors.push_back(mesh->colors[idx(x, z)]);
    };

    for (uint32_t x = 0; x < gridDim; ++x) addSkirtVertex(x, 0);
    for (uint32_t x = 0; x < gridDim; ++x) addSkirtVertex(x, gridDim - 1);
    for (uint32_t z = 0; z < gridDim; ++z) addSkirtVertex(0, z);
    for (uint32_t z = 0; z < gridDim; ++z) addSkirtVertex(gridDim - 1, z);

    auto skirtIdx = [&](uint32_t edge, uint32_t i) -> uint32_t {
        return skirtBaseIdx + edge * gridDim + i;
    };

    // Bottom edge skirt (z=0) - normal should face -Z (outward)
    for (uint32_t x = 0; x < gridDim - 1; ++x) {
        uint32_t a = idx(x, 0), b = idx(x + 1, 0);
        uint32_t c = skirtIdx(0, x), d = skirtIdx(0, x + 1);
        mesh->indices.push_back(a); mesh->indices.push_back(b); mesh->indices.push_back(c);
        mesh->indices.push_back(b); mesh->indices.push_back(d); mesh->indices.push_back(c);
    }
    // Top edge skirt (z=gridDim-1) - normal should face +Z (outward)
    for (uint32_t x = 0; x < gridDim - 1; ++x) {
        uint32_t a = idx(x + 1, gridDim - 1), b = idx(x, gridDim - 1);
        uint32_t c = skirtIdx(1, x + 1), d = skirtIdx(1, x);
        mesh->indices.push_back(a); mesh->indices.push_back(b); mesh->indices.push_back(c);
        mesh->indices.push_back(b); mesh->indices.push_back(d); mesh->indices.push_back(c);
    }
    // Left edge skirt (x=0) - normal should face -X (outward)
    for (uint32_t z = 0; z < gridDim - 1; ++z) {
        uint32_t a = idx(0, z + 1), b = idx(0, z);
        uint32_t c = skirtIdx(2, z + 1), d = skirtIdx(2, z);
        mesh->indices.push_back(a); mesh->indices.push_back(b); mesh->indices.push_back(c);
        mesh->indices.push_back(b); mesh->indices.push_back(d); mesh->indices.push_back(c);
    }
    // Right edge skirt (x=gridDim-1) - normal should face +X (outward)
    for (uint32_t z = 0; z < gridDim - 1; ++z) {
        uint32_t a = idx(gridDim - 1, z), b = idx(gridDim - 1, z + 1);
        uint32_t c = skirtIdx(3, z), d = skirtIdx(3, z + 1);
        mesh->indices.push_back(a); mesh->indices.push_back(b); mesh->indices.push_back(c);
        mesh->indices.push_back(b); mesh->indices.push_back(d); mesh->indices.push_back(c);
    }

    mesh->UpdateBounds();
    return mesh;
}

} // namespace Cortex::Utils
