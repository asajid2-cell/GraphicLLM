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

} // namespace Cortex::Utils
