// RiverSystem.cpp
// Implementation of river/lake water system.

#include "RiverSystem.h"
#include <cmath>
#include <algorithm>
#include <fstream>
#include <nlohmann/json.hpp>

namespace Cortex::Scene {

// ============================================================================
// SPLINE INTERPOLATION HELPERS
// ============================================================================

glm::vec3 RiverSystem::CatmullRom(const glm::vec3& p0, const glm::vec3& p1,
                                   const glm::vec3& p2, const glm::vec3& p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;

    // Catmull-Rom basis functions
    float b0 = -0.5f * t3 + t2 - 0.5f * t;
    float b1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
    float b2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    float b3 = 0.5f * t3 - 0.5f * t2;

    return p0 * b0 + p1 * b1 + p2 * b2 + p3 * b3;
}

float RiverSystem::CatmullRomScalar(float p0, float p1, float p2, float p3, float t) {
    float t2 = t * t;
    float t3 = t2 * t;

    float b0 = -0.5f * t3 + t2 - 0.5f * t;
    float b1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
    float b2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    float b3 = 0.5f * t3 - 0.5f * t2;

    return p0 * b0 + p1 * b1 + p2 * b2 + p3 * b3;
}

// ============================================================================
// RIVER SPLINE METHODS
// ============================================================================

glm::vec3 RiverSpline::EvaluatePosition(float t) const {
    if (controlPoints.size() < 2) {
        return controlPoints.empty() ? glm::vec3(0) : controlPoints[0].position;
    }

    // Clamp t to valid range
    t = glm::clamp(t, 0.0f, 1.0f);

    // Find which segment we're in
    float segmentFloat = t * (controlPoints.size() - 1);
    int segment = static_cast<int>(segmentFloat);
    float localT = segmentFloat - segment;

    // Clamp segment to valid range
    segment = glm::clamp(segment, 0, static_cast<int>(controlPoints.size()) - 2);

    // Get the 4 control points for Catmull-Rom
    int i0 = glm::max(0, segment - 1);
    int i1 = segment;
    int i2 = segment + 1;
    int i3 = glm::min(static_cast<int>(controlPoints.size()) - 1, segment + 2);

    return RiverSystem::CatmullRom(
        controlPoints[i0].position,
        controlPoints[i1].position,
        controlPoints[i2].position,
        controlPoints[i3].position,
        localT
    );
}

float RiverSpline::EvaluateWidth(float t) const {
    if (controlPoints.size() < 2) {
        return controlPoints.empty() ? 5.0f : controlPoints[0].width;
    }

    t = glm::clamp(t, 0.0f, 1.0f);
    float segmentFloat = t * (controlPoints.size() - 1);
    int segment = static_cast<int>(segmentFloat);
    float localT = segmentFloat - segment;
    segment = glm::clamp(segment, 0, static_cast<int>(controlPoints.size()) - 2);

    int i0 = glm::max(0, segment - 1);
    int i1 = segment;
    int i2 = segment + 1;
    int i3 = glm::min(static_cast<int>(controlPoints.size()) - 1, segment + 2);

    return RiverSystem::CatmullRomScalar(
        controlPoints[i0].width,
        controlPoints[i1].width,
        controlPoints[i2].width,
        controlPoints[i3].width,
        localT
    );
}

float RiverSpline::EvaluateDepth(float t) const {
    if (controlPoints.size() < 2) {
        return controlPoints.empty() ? 1.0f : controlPoints[0].depth;
    }

    t = glm::clamp(t, 0.0f, 1.0f);
    float segmentFloat = t * (controlPoints.size() - 1);
    int segment = static_cast<int>(segmentFloat);
    float localT = segmentFloat - segment;
    segment = glm::clamp(segment, 0, static_cast<int>(controlPoints.size()) - 2);

    int i0 = glm::max(0, segment - 1);
    int i1 = segment;
    int i2 = segment + 1;
    int i3 = glm::min(static_cast<int>(controlPoints.size()) - 1, segment + 2);

    return RiverSystem::CatmullRomScalar(
        controlPoints[i0].depth,
        controlPoints[i1].depth,
        controlPoints[i2].depth,
        controlPoints[i3].depth,
        localT
    );
}

float RiverSpline::EvaluateFlowSpeed(float t) const {
    if (controlPoints.size() < 2) {
        return controlPoints.empty() ? 1.0f : controlPoints[0].flowSpeed;
    }

    t = glm::clamp(t, 0.0f, 1.0f);
    float segmentFloat = t * (controlPoints.size() - 1);
    int segment = static_cast<int>(segmentFloat);
    float localT = segmentFloat - segment;
    segment = glm::clamp(segment, 0, static_cast<int>(controlPoints.size()) - 2);

    int i0 = glm::max(0, segment - 1);
    int i1 = segment;
    int i2 = segment + 1;
    int i3 = glm::min(static_cast<int>(controlPoints.size()) - 1, segment + 2);

    return RiverSystem::CatmullRomScalar(
        controlPoints[i0].flowSpeed,
        controlPoints[i1].flowSpeed,
        controlPoints[i2].flowSpeed,
        controlPoints[i3].flowSpeed,
        localT
    );
}

glm::vec3 RiverSpline::EvaluateTangent(float t) const {
    const float epsilon = 0.001f;
    float t0 = glm::max(0.0f, t - epsilon);
    float t1 = glm::min(1.0f, t + epsilon);

    glm::vec3 p0 = EvaluatePosition(t0);
    glm::vec3 p1 = EvaluatePosition(t1);

    glm::vec3 tangent = p1 - p0;
    float len = glm::length(tangent);
    return len > 0.0001f ? tangent / len : glm::vec3(0, 0, 1);
}

float RiverSpline::GetTotalLength() const {
    if (controlPoints.size() < 2) return 0.0f;

    float totalLength = 0.0f;
    const int samples = (static_cast<int>(controlPoints.size()) - 1) * segmentsPerSpan;
    glm::vec3 prevPos = EvaluatePosition(0.0f);

    for (int i = 1; i <= samples; ++i) {
        float t = static_cast<float>(i) / static_cast<float>(samples);
        glm::vec3 pos = EvaluatePosition(t);
        totalLength += glm::length(pos - prevPos);
        prevPos = pos;
    }

    return totalLength;
}

float RiverSpline::TToArcLength(float t) const {
    if (controlPoints.size() < 2) return 0.0f;

    const int samples = static_cast<int>(t * (controlPoints.size() - 1) * segmentsPerSpan);
    float arcLength = 0.0f;
    glm::vec3 prevPos = EvaluatePosition(0.0f);

    for (int i = 1; i <= samples; ++i) {
        float localT = static_cast<float>(i) / static_cast<float>((controlPoints.size() - 1) * segmentsPerSpan);
        glm::vec3 pos = EvaluatePosition(localT);
        arcLength += glm::length(pos - prevPos);
        prevPos = pos;
    }

    return arcLength;
}

float RiverSpline::ArcLengthToT(float arcLength) const {
    float totalLength = GetTotalLength();
    if (totalLength <= 0.0f) return 0.0f;

    // Binary search for t that gives the desired arc length
    float targetArcLength = glm::clamp(arcLength, 0.0f, totalLength);
    float low = 0.0f, high = 1.0f;

    for (int iter = 0; iter < 20; ++iter) {
        float mid = (low + high) * 0.5f;
        float midArcLength = TToArcLength(mid);

        if (std::abs(midArcLength - targetArcLength) < 0.01f) {
            return mid;
        }

        if (midArcLength < targetArcLength) {
            low = mid;
        } else {
            high = mid;
        }
    }

    return (low + high) * 0.5f;
}

// ============================================================================
// LAKE VOLUME METHODS
// ============================================================================

bool LakeVolume::ContainsPoint(float x, float z) const {
    if (boundaryPoints.size() < 3) return false;

    // Ray casting algorithm for point-in-polygon
    bool inside = false;
    size_t j = boundaryPoints.size() - 1;

    for (size_t i = 0; i < boundaryPoints.size(); ++i) {
        if ((boundaryPoints[i].y > z) != (boundaryPoints[j].y > z) &&
            x < (boundaryPoints[j].x - boundaryPoints[i].x) * (z - boundaryPoints[i].y) /
                (boundaryPoints[j].y - boundaryPoints[i].y) + boundaryPoints[i].x) {
            inside = !inside;
        }
        j = i;
    }

    return inside;
}

float LakeVolume::GetDepthAt(float x, float z) const {
    if (!ContainsPoint(x, z)) return 0.0f;

    // Simple depth model: maximum at center, decreasing toward edges
    // Find distance to nearest edge
    float minDist = 1e10f;

    for (size_t i = 0; i < boundaryPoints.size(); ++i) {
        size_t j = (i + 1) % boundaryPoints.size();
        glm::vec2 a = boundaryPoints[i];
        glm::vec2 b = boundaryPoints[j];
        glm::vec2 p(x, z);

        // Distance from point to line segment
        glm::vec2 ab = b - a;
        float abLen2 = glm::dot(ab, ab);
        if (abLen2 < 0.0001f) {
            minDist = glm::min(minDist, glm::length(p - a));
        } else {
            float t = glm::clamp(glm::dot(p - a, ab) / abLen2, 0.0f, 1.0f);
            glm::vec2 closest = a + t * ab;
            minDist = glm::min(minDist, glm::length(p - closest));
        }
    }

    // Depth increases with distance from shore, capped at max depth
    float depthFactor = glm::smoothstep(0.0f, shoreBlendDistance * 2.0f, minDist);
    return depth * depthFactor;
}

void LakeVolume::ComputeBounds() {
    if (boundaryPoints.empty()) {
        boundsMin = boundsMax = glm::vec3(0);
        return;
    }

    boundsMin = glm::vec3(boundaryPoints[0].x, waterLevel - depth, boundaryPoints[0].y);
    boundsMax = glm::vec3(boundaryPoints[0].x, waterLevel, boundaryPoints[0].y);

    for (const auto& p : boundaryPoints) {
        boundsMin.x = glm::min(boundsMin.x, p.x);
        boundsMin.z = glm::min(boundsMin.z, p.y);
        boundsMax.x = glm::max(boundsMax.x, p.x);
        boundsMax.z = glm::max(boundsMax.z, p.y);
    }
}

// ============================================================================
// RIVER SYSTEM IMPLEMENTATION
// ============================================================================

RiverSystem::RiverSystem() {}

RiverSystem::~RiverSystem() {}

uint32_t RiverSystem::AddRiver(const RiverSpline& river) {
    m_rivers.push_back(river);
    return static_cast<uint32_t>(m_rivers.size() - 1);
}

void RiverSystem::RemoveRiver(uint32_t riverId) {
    if (riverId < m_rivers.size()) {
        m_rivers.erase(m_rivers.begin() + riverId);
    }
}

RiverSpline* RiverSystem::GetRiver(uint32_t riverId) {
    return riverId < m_rivers.size() ? &m_rivers[riverId] : nullptr;
}

const RiverSpline* RiverSystem::GetRiver(uint32_t riverId) const {
    return riverId < m_rivers.size() ? &m_rivers[riverId] : nullptr;
}

uint32_t RiverSystem::AddLake(const LakeVolume& lake) {
    m_lakes.push_back(lake);
    m_lakes.back().ComputeBounds();
    return static_cast<uint32_t>(m_lakes.size() - 1);
}

void RiverSystem::RemoveLake(uint32_t lakeId) {
    if (lakeId < m_lakes.size()) {
        m_lakes.erase(m_lakes.begin() + lakeId);
    }
}

LakeVolume* RiverSystem::GetLake(uint32_t lakeId) {
    return lakeId < m_lakes.size() ? &m_lakes[lakeId] : nullptr;
}

const LakeVolume* RiverSystem::GetLake(uint32_t lakeId) const {
    return lakeId < m_lakes.size() ? &m_lakes[lakeId] : nullptr;
}

uint32_t RiverSystem::AddWaterfall(const WaterfallSegment& waterfall) {
    m_waterfalls.push_back(waterfall);
    return static_cast<uint32_t>(m_waterfalls.size() - 1);
}

void RiverSystem::RemoveWaterfall(uint32_t waterfallId) {
    if (waterfallId < m_waterfalls.size()) {
        m_waterfalls.erase(m_waterfalls.begin() + waterfallId);
    }
}

// ============================================================================
// MESH GENERATION
// ============================================================================

void RiverSystem::GenerateRiverCrossSection(const RiverSpline& river, float t,
                                             std::vector<RiverVertex>& outVertices) {
    glm::vec3 position = river.EvaluatePosition(t);
    glm::vec3 tangent = river.EvaluateTangent(t);
    float width = river.EvaluateWidth(t);
    float depth = river.EvaluateDepth(t);
    float flowSpeed = river.EvaluateFlowSpeed(t);

    // Compute binormal (perpendicular to flow direction, in XZ plane)
    glm::vec3 up(0, 1, 0);
    glm::vec3 binormal = glm::normalize(glm::cross(up, tangent));
    if (glm::length(binormal) < 0.01f) {
        binormal = glm::vec3(1, 0, 0);
    }

    // Generate vertices across the width
    float halfWidth = width * 0.5f;
    for (int i = 0; i <= river.widthSegments; ++i) {
        float u = static_cast<float>(i) / static_cast<float>(river.widthSegments);
        float offsetX = (u - 0.5f) * 2.0f;  // -1 to 1
        float distFromCenter = std::abs(offsetX);

        RiverVertex vert{};
        vert.position = position + binormal * (halfWidth * offsetX);
        vert.normal = up;
        vert.texCoord = glm::vec2(u, t * river.GetTotalLength() * river.style.flowUVScale);
        vert.flowUV = glm::vec2(u, t);
        vert.flowSpeed = flowSpeed;
        vert.depth = depth * (1.0f - distFromCenter * 0.5f);  // Deeper in center
        vert.distanceFromBank = 1.0f - distFromCenter;
        vert.turbulence = 0.0f;  // Will be computed based on slope, narrows, etc.

        outVertices.push_back(vert);
    }
}

std::shared_ptr<MeshData> RiverSystem::GenerateRiverMesh(const RiverSpline& river) {
    std::vector<RiverVertex> vertices;
    std::vector<uint32_t> indices;

    if (river.controlPoints.size() < 2) {
        return nullptr;
    }

    // Generate cross-sections along the river
    int totalSegments = static_cast<int>(river.controlPoints.size() - 1) * river.segmentsPerSpan;
    int vertsPerCrossSection = river.widthSegments + 1;

    for (int seg = 0; seg <= totalSegments; ++seg) {
        float t = static_cast<float>(seg) / static_cast<float>(totalSegments);
        GenerateRiverCrossSection(river, t, vertices);
    }

    // Generate indices (triangle strip style, converted to triangles)
    for (int seg = 0; seg < totalSegments; ++seg) {
        int baseIdx = seg * vertsPerCrossSection;

        for (int i = 0; i < river.widthSegments; ++i) {
            uint32_t tl = baseIdx + i;
            uint32_t tr = baseIdx + i + 1;
            uint32_t bl = baseIdx + vertsPerCrossSection + i;
            uint32_t br = baseIdx + vertsPerCrossSection + i + 1;

            // Two triangles per quad
            indices.push_back(tl);
            indices.push_back(bl);
            indices.push_back(tr);

            indices.push_back(tr);
            indices.push_back(bl);
            indices.push_back(br);
        }
    }

    // Convert to MeshData
    auto mesh = std::make_shared<MeshData>();
    mesh->vertices.reserve(vertices.size());

    for (const auto& rv : vertices) {
        Graphics::Vertex v{};
        v.position = rv.position;
        v.normal = rv.normal;
        v.texCoord = rv.texCoord;
        v.tangent = glm::vec4(1, 0, 0, 1);  // Simple tangent
        v.color = glm::vec4(rv.flowSpeed, rv.depth, rv.distanceFromBank, rv.turbulence);
        mesh->vertices.push_back(v);
    }

    mesh->indices = std::move(indices);
    mesh->name = river.name + "_mesh";

    return mesh;
}

void RiverSystem::TriangulateLakeBoundary(const LakeVolume& lake,
                                           std::vector<RiverVertex>& outVertices,
                                           std::vector<uint32_t>& outIndices) {
    if (lake.boundaryPoints.size() < 3) return;

    // Simple fan triangulation from centroid
    // For complex polygons, ear-clipping would be better

    // Compute centroid
    glm::vec2 centroid(0);
    for (const auto& p : lake.boundaryPoints) {
        centroid += p;
    }
    centroid /= static_cast<float>(lake.boundaryPoints.size());

    // Add center vertex
    RiverVertex centerVert{};
    centerVert.position = glm::vec3(centroid.x, lake.waterLevel, centroid.y);
    centerVert.normal = glm::vec3(0, 1, 0);
    centerVert.texCoord = glm::vec2(0.5f, 0.5f);
    centerVert.flowUV = glm::vec2(0.5f, 0.5f);
    centerVert.flowSpeed = 0.0f;
    centerVert.depth = lake.depth;
    centerVert.distanceFromBank = 1.0f;
    centerVert.turbulence = 0.0f;

    uint32_t centerIdx = static_cast<uint32_t>(outVertices.size());
    outVertices.push_back(centerVert);

    // Add boundary vertices
    for (size_t i = 0; i < lake.boundaryPoints.size(); ++i) {
        const auto& p = lake.boundaryPoints[i];

        RiverVertex vert{};
        vert.position = glm::vec3(p.x, lake.waterLevel, p.y);
        vert.normal = glm::vec3(0, 1, 0);

        // UV based on position relative to centroid
        glm::vec2 toPoint = p - centroid;
        float angle = std::atan2(toPoint.y, toPoint.x);
        vert.texCoord = glm::vec2(0.5f + 0.5f * std::cos(angle), 0.5f + 0.5f * std::sin(angle));
        vert.flowUV = vert.texCoord;
        vert.flowSpeed = 0.0f;
        vert.depth = 0.0f;  // Shallow at edge
        vert.distanceFromBank = 0.0f;
        vert.turbulence = 0.0f;

        outVertices.push_back(vert);
    }

    // Create triangles
    for (size_t i = 0; i < lake.boundaryPoints.size(); ++i) {
        size_t next = (i + 1) % lake.boundaryPoints.size();
        uint32_t i0 = centerIdx;
        uint32_t i1 = centerIdx + 1 + static_cast<uint32_t>(i);
        uint32_t i2 = centerIdx + 1 + static_cast<uint32_t>(next);

        outIndices.push_back(i0);
        outIndices.push_back(i1);
        outIndices.push_back(i2);
    }
}

std::shared_ptr<MeshData> RiverSystem::GenerateLakeMesh(const LakeVolume& lake) {
    std::vector<RiverVertex> vertices;
    std::vector<uint32_t> indices;

    TriangulateLakeBoundary(lake, vertices, indices);

    if (vertices.empty()) {
        return nullptr;
    }

    auto mesh = std::make_shared<MeshData>();
    mesh->vertices.reserve(vertices.size());

    for (const auto& rv : vertices) {
        Graphics::Vertex v{};
        v.position = rv.position;
        v.normal = rv.normal;
        v.texCoord = rv.texCoord;
        v.tangent = glm::vec4(1, 0, 0, 1);
        v.color = glm::vec4(rv.flowSpeed, rv.depth, rv.distanceFromBank, rv.turbulence);
        mesh->vertices.push_back(v);
    }

    mesh->indices = std::move(indices);
    mesh->name = lake.name + "_mesh";

    return mesh;
}

void RiverSystem::RegenerateDirtyMeshes() {
    for (auto& river : m_rivers) {
        if (river.isDirty) {
            // Regenerate mesh
            river.isDirty = false;
        }
    }

    for (auto& lake : m_lakes) {
        if (lake.isDirty) {
            lake.ComputeBounds();
            lake.isDirty = false;
        }
    }
}

// ============================================================================
// TERRAIN INTERACTION
// ============================================================================

float RiverSystem::GetTerrainCarveOffset(float x, float z) const {
    float totalOffset = 0.0f;

    // Check rivers
    for (const auto& river : m_rivers) {
        if (!river.carvesTerrain) continue;

        // Sample river at multiple points to find closest
        float closestDist = 1e10f;
        float closestDepth = 0.0f;

        for (float t = 0.0f; t <= 1.0f; t += 0.01f) {
            glm::vec3 riverPos = river.EvaluatePosition(t);
            float width = river.EvaluateWidth(t);
            float depth = river.EvaluateDepth(t);

            float dx = x - riverPos.x;
            float dz = z - riverPos.z;
            float dist = std::sqrt(dx * dx + dz * dz);

            if (dist < closestDist && dist < width * 0.5f + river.carveBlendRadius) {
                closestDist = dist;
                float halfWidth = width * 0.5f;

                if (dist < halfWidth) {
                    // Inside river
                    closestDepth = -depth * river.carveDepth;
                } else {
                    // In blend zone
                    float blendFactor = 1.0f - (dist - halfWidth) / river.carveBlendRadius;
                    blendFactor = glm::smoothstep(0.0f, 1.0f, blendFactor);
                    closestDepth = -depth * river.carveDepth * blendFactor;
                }
            }
        }

        totalOffset = glm::min(totalOffset, closestDepth);
    }

    // Check lakes
    for (const auto& lake : m_lakes) {
        if (!lake.carvesTerrain) continue;

        float lakeDepth = lake.GetDepthAt(x, z);
        if (lakeDepth > 0.0f) {
            totalOffset = glm::min(totalOffset, -lakeDepth);
        }
    }

    return totalOffset;
}

bool RiverSystem::IsPointUnderwater(float x, float y, float z) const {
    float surfaceHeight = GetWaterSurfaceHeight(x, z);
    return y < surfaceHeight;
}

float RiverSystem::GetWaterSurfaceHeight(float x, float z) const {
    float maxHeight = -1e10f;

    // Check rivers
    for (const auto& river : m_rivers) {
        for (float t = 0.0f; t <= 1.0f; t += 0.01f) {
            glm::vec3 pos = river.EvaluatePosition(t);
            float width = river.EvaluateWidth(t);
            float halfWidth = width * 0.5f;

            float dx = x - pos.x;
            float dz = z - pos.z;
            float dist = std::sqrt(dx * dx + dz * dz);

            if (dist < halfWidth) {
                maxHeight = glm::max(maxHeight, pos.y);
            }
        }
    }

    // Check lakes
    for (const auto& lake : m_lakes) {
        if (lake.ContainsPoint(x, z)) {
            maxHeight = glm::max(maxHeight, lake.waterLevel);
        }
    }

    return maxHeight;
}

glm::vec3 RiverSystem::GetFlowDirectionAt(float x, float y, float z) const {
    glm::vec3 totalFlow(0);
    float totalWeight = 0.0f;

    for (const auto& river : m_rivers) {
        // Find closest point on river
        float closestDist = 1e10f;
        glm::vec3 closestTangent(0);

        for (float t = 0.0f; t <= 1.0f; t += 0.01f) {
            glm::vec3 pos = river.EvaluatePosition(t);
            float width = river.EvaluateWidth(t);

            float dx = x - pos.x;
            float dz = z - pos.z;
            float dist = std::sqrt(dx * dx + dz * dz);

            if (dist < closestDist && dist < width * 0.5f) {
                closestDist = dist;
                closestTangent = river.EvaluateTangent(t);
            }
        }

        if (closestDist < 1e9f) {
            float weight = 1.0f / (1.0f + closestDist);
            totalFlow += closestTangent * weight;
            totalWeight += weight;
        }
    }

    if (totalWeight > 0.0f) {
        return glm::normalize(totalFlow / totalWeight);
    }

    return glm::vec3(0);
}

float RiverSystem::GetFlowSpeedAt(float x, float y, float z) const {
    float maxSpeed = 0.0f;

    for (const auto& river : m_rivers) {
        for (float t = 0.0f; t <= 1.0f; t += 0.01f) {
            glm::vec3 pos = river.EvaluatePosition(t);
            float width = river.EvaluateWidth(t);

            float dx = x - pos.x;
            float dz = z - pos.z;
            float dist = std::sqrt(dx * dx + dz * dz);

            if (dist < width * 0.5f) {
                maxSpeed = glm::max(maxSpeed, river.EvaluateFlowSpeed(t));
            }
        }
    }

    return maxSpeed;
}

// ============================================================================
// UPDATE
// ============================================================================

void RiverSystem::Update(float deltaTime) {
    m_time += deltaTime;
}

// ============================================================================
// STATISTICS
// ============================================================================

float RiverSystem::GetTotalRiverLength() const {
    float total = 0.0f;
    for (const auto& river : m_rivers) {
        total += river.GetTotalLength();
    }
    return total;
}

float RiverSystem::GetTotalLakeArea() const {
    float total = 0.0f;
    for (const auto& lake : m_lakes) {
        // Simple polygon area calculation
        float area = 0.0f;
        size_t n = lake.boundaryPoints.size();
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            area += lake.boundaryPoints[i].x * lake.boundaryPoints[j].y;
            area -= lake.boundaryPoints[j].x * lake.boundaryPoints[i].y;
        }
        total += std::abs(area) * 0.5f;
    }
    return total;
}

// ============================================================================
// SERIALIZATION
// ============================================================================

bool RiverSystem::LoadFromJSON(const std::string& path) {
    try {
        std::ifstream file(path);
        if (!file.is_open()) return false;

        nlohmann::json json;
        file >> json;

        m_rivers.clear();
        m_lakes.clear();
        m_waterfalls.clear();

        // Load rivers
        if (json.contains("rivers")) {
            for (const auto& riverJson : json["rivers"]) {
                RiverSpline river;
                if (riverJson.contains("name")) river.name = riverJson["name"];

                if (riverJson.contains("controlPoints")) {
                    for (const auto& cpJson : riverJson["controlPoints"]) {
                        RiverSplinePoint cp;
                        if (cpJson.contains("position")) {
                            cp.position = glm::vec3(
                                cpJson["position"][0],
                                cpJson["position"][1],
                                cpJson["position"][2]
                            );
                        }
                        if (cpJson.contains("width")) cp.width = cpJson["width"];
                        if (cpJson.contains("depth")) cp.depth = cpJson["depth"];
                        if (cpJson.contains("flowSpeed")) cp.flowSpeed = cpJson["flowSpeed"];
                        river.controlPoints.push_back(cp);
                    }
                }

                if (riverJson.contains("style")) {
                    const auto& styleJson = riverJson["style"];
                    if (styleJson.contains("name")) river.style.name = styleJson["name"];
                    if (styleJson.contains("transparency")) river.style.transparency = styleJson["transparency"];
                }

                m_rivers.push_back(river);
            }
        }

        // Load lakes
        if (json.contains("lakes")) {
            for (const auto& lakeJson : json["lakes"]) {
                LakeVolume lake;
                if (lakeJson.contains("name")) lake.name = lakeJson["name"];
                if (lakeJson.contains("waterLevel")) lake.waterLevel = lakeJson["waterLevel"];
                if (lakeJson.contains("depth")) lake.depth = lakeJson["depth"];

                if (lakeJson.contains("boundary")) {
                    for (const auto& ptJson : lakeJson["boundary"]) {
                        lake.boundaryPoints.push_back(glm::vec2(ptJson[0], ptJson[1]));
                    }
                }

                lake.ComputeBounds();
                m_lakes.push_back(lake);
            }
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool RiverSystem::SaveToJSON(const std::string& path) const {
    try {
        nlohmann::json json;

        // Save rivers
        json["rivers"] = nlohmann::json::array();
        for (const auto& river : m_rivers) {
            nlohmann::json riverJson;
            riverJson["name"] = river.name;

            riverJson["controlPoints"] = nlohmann::json::array();
            for (const auto& cp : river.controlPoints) {
                nlohmann::json cpJson;
                cpJson["position"] = { cp.position.x, cp.position.y, cp.position.z };
                cpJson["width"] = cp.width;
                cpJson["depth"] = cp.depth;
                cpJson["flowSpeed"] = cp.flowSpeed;
                riverJson["controlPoints"].push_back(cpJson);
            }

            riverJson["style"]["name"] = river.style.name;
            riverJson["style"]["transparency"] = river.style.transparency;

            json["rivers"].push_back(riverJson);
        }

        // Save lakes
        json["lakes"] = nlohmann::json::array();
        for (const auto& lake : m_lakes) {
            nlohmann::json lakeJson;
            lakeJson["name"] = lake.name;
            lakeJson["waterLevel"] = lake.waterLevel;
            lakeJson["depth"] = lake.depth;

            lakeJson["boundary"] = nlohmann::json::array();
            for (const auto& p : lake.boundaryPoints) {
                lakeJson["boundary"].push_back({ p.x, p.y });
            }

            json["lakes"].push_back(lakeJson);
        }

        std::ofstream file(path);
        file << json.dump(2);
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace Cortex::Scene
