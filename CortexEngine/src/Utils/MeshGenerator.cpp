#include "MeshGenerator.h"
#include <cmath>

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

    // Indices (2 triangles per face)
    for (uint32_t face = 0; face < 6; face++) {
        uint32_t baseIndex = face * 4;
        // Triangle 1
        mesh->indices.push_back(baseIndex + 0);
        mesh->indices.push_back(baseIndex + 1);
        mesh->indices.push_back(baseIndex + 2);
        // Triangle 2
        mesh->indices.push_back(baseIndex + 0);
        mesh->indices.push_back(baseIndex + 2);
        mesh->indices.push_back(baseIndex + 3);
    }

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

    // Indices
    mesh->indices = { 0, 1, 2, 0, 2, 3 };

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

    // Indices
    mesh->indices = { 0, 1, 2, 0, 2, 3 };

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

    // Generate indices
    for (uint32_t y = 0; y < segments; y++) {
        for (uint32_t x = 0; x < segments; x++) {
            uint32_t i0 = y * (segments + 1) + x;
            uint32_t i1 = i0 + segments + 1;
            uint32_t i2 = i0 + 1;
            uint32_t i3 = i1 + 1;

            mesh->indices.push_back(i0);
            mesh->indices.push_back(i1);
            mesh->indices.push_back(i2);

            mesh->indices.push_back(i2);
            mesh->indices.push_back(i1);
            mesh->indices.push_back(i3);
        }
    }

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

    // Generate indices for cylinder sides
    for (uint32_t i = 0; i < segments; i++) {
        uint32_t i0 = i * 2;
        uint32_t i1 = i0 + 1;
        uint32_t i2 = (i0 + 2) % ((segments + 1) * 2);
        uint32_t i3 = (i0 + 3) % ((segments + 1) * 2);

        // Triangle 1
        mesh->indices.push_back(i0);
        mesh->indices.push_back(i1);
        mesh->indices.push_back(i2);

        // Triangle 2
        mesh->indices.push_back(i2);
        mesh->indices.push_back(i1);
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
        // Top cap
        mesh->indices.push_back(topCenterIdx);
        mesh->indices.push_back(topCapStart + i * 2);
        mesh->indices.push_back(topCapStart + ((i + 1) % (segments + 1)) * 2);

        // Bottom cap (reversed winding)
        mesh->indices.push_back(bottomCenterIdx);
        mesh->indices.push_back(bottomCapStart + ((i + 1) % (segments + 1)) * 2);
        mesh->indices.push_back(bottomCapStart + i * 2);
    }

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
    mesh->indices.push_back(baseIdx + 0);
    mesh->indices.push_back(baseIdx + 1);
    mesh->indices.push_back(baseIdx + 2);

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
    mesh->indices.push_back(baseIdx + 0);
    mesh->indices.push_back(baseIdx + 1);
    mesh->indices.push_back(baseIdx + 2);

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
    mesh->indices.push_back(baseIdx + 0);
    mesh->indices.push_back(baseIdx + 1);
    mesh->indices.push_back(baseIdx + 2);

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
    mesh->indices.push_back(baseIdx + 0);
    mesh->indices.push_back(baseIdx + 1);
    mesh->indices.push_back(baseIdx + 2);

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

        // Calculate face normal
        glm::vec3 edge1 = v1 - v0;
        glm::vec3 edge2 = apex - v0;
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

        mesh->indices.push_back(baseIdx + 0);
        mesh->indices.push_back(baseIdx + 1);
        mesh->indices.push_back(baseIdx + 2);
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

    // Base cap indices (reversed winding for downward-facing normal)
    uint32_t baseCapStart = baseCenterIdx + 1;
    for (uint32_t i = 0; i < segments; i++) {
        mesh->indices.push_back(baseCenterIdx);
        mesh->indices.push_back(baseCapStart + i + 1);
        mesh->indices.push_back(baseCapStart + i);
    }

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

    // Generate indices
    for (uint32_t i = 0; i < majorSegments; i++) {
        for (uint32_t j = 0; j < minorSegments; j++) {
            uint32_t i0 = i * (minorSegments + 1) + j;
            uint32_t i1 = i0 + minorSegments + 1;
            uint32_t i2 = i0 + 1;
            uint32_t i3 = i1 + 1;

            // Triangle 1
            mesh->indices.push_back(i0);
            mesh->indices.push_back(i1);
            mesh->indices.push_back(i2);

            // Triangle 2
            mesh->indices.push_back(i2);
            mesh->indices.push_back(i1);
            mesh->indices.push_back(i3);
        }
    }

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

    // Indices for triangle fan
    for (uint32_t i = 1; i <= segments; ++i) {
        mesh->indices.push_back(0);
        mesh->indices.push_back(i);
        mesh->indices.push_back((i % segments) + 1);
    }

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

} // namespace Cortex::Utils
