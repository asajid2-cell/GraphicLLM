#pragma once

// Pathfinder.h
// A* pathfinding on navigation meshes.
// Supports path smoothing, string pulling, and hierarchical search.
// Reference: "AI Game Programming Wisdom" - Rabin

#include "NavMesh.h"
#include <glm/glm.hpp>
#include <vector>
#include <queue>
#include <functional>
#include <memory>

namespace Cortex::AI {

// Path query status
enum class PathStatus {
    NotStarted,
    InProgress,
    Complete,
    Failed,
    Partial         // Partial path found (goal unreachable)
};

// Path query result
struct PathResult {
    PathStatus status = PathStatus::NotStarted;
    std::vector<glm::vec3> path;            // Smoothed path points
    std::vector<uint32_t> polygonPath;      // Polygon corridor
    float totalCost = 0.0f;
    uint32_t nodesExplored = 0;

    bool IsValid() const {
        return status == PathStatus::Complete || status == PathStatus::Partial;
    }
};

// Path query parameters
struct PathQueryParams {
    glm::vec3 start;
    glm::vec3 end;
    float agentRadius = 0.5f;
    float agentHeight = 2.0f;
    NavAreaFlags allowedFlags = NavAreaFlags::All;
    float maxPathLength = 1000.0f;
    uint32_t maxIterations = 10000;
    bool smoothPath = true;
    bool useStringPulling = true;
};

// Steering agent parameters
struct SteeringParams {
    float maxSpeed = 5.0f;
    float acceleration = 10.0f;
    float deceleration = 15.0f;
    float turnSpeed = 360.0f;           // Degrees per second
    float arrivalRadius = 1.0f;         // Slow down within this radius
    float stoppingRadius = 0.5f;        // Stop within this radius
    float avoidanceRadius = 1.0f;       // Obstacle avoidance radius
    float separationRadius = 2.0f;      // Separation from other agents
    float pathFollowRadius = 0.5f;      // How close to stay to path
};

// Steering output
struct SteeringOutput {
    glm::vec3 velocity = glm::vec3(0.0f);
    float rotation = 0.0f;              // Angular velocity (radians/sec)
    bool arrived = false;
};

// A* node for open/closed lists
struct AStarNode {
    uint32_t polyRef;
    float gCost;
    float hCost;
    float fCost() const { return gCost + hCost; }
    uint32_t parentRef;
    glm::vec3 entryPoint;

    bool operator>(const AStarNode& other) const {
        return fCost() > other.fCost();
    }
};

// Navigation agent for path following
class NavAgent {
public:
    NavAgent() = default;
    explicit NavAgent(const NavMesh* navMesh);

    // Set nav mesh
    void SetNavMesh(const NavMesh* navMesh) { m_navMesh = navMesh; }

    // Set position (snaps to navmesh)
    void SetPosition(const glm::vec3& position);

    // Get current position
    glm::vec3 GetPosition() const { return m_position; }

    // Set destination
    bool SetDestination(const glm::vec3& destination);

    // Clear current path
    void ClearPath();

    // Update agent (call each frame)
    void Update(float deltaTime);

    // Is agent following a path?
    bool IsFollowingPath() const { return !m_path.empty() && m_currentPathIndex < m_path.size(); }

    // Has agent reached destination?
    bool HasReachedDestination() const { return m_hasReachedDestination; }

    // Get current velocity
    glm::vec3 GetVelocity() const { return m_velocity; }

    // Get current facing direction (Y rotation)
    float GetFacingAngle() const { return m_facingAngle; }

    // Parameters
    SteeringParams& GetSteeringParams() { return m_steeringParams; }
    const SteeringParams& GetSteeringParams() const { return m_steeringParams; }

    // Path access
    const std::vector<glm::vec3>& GetPath() const { return m_path; }
    size_t GetCurrentPathIndex() const { return m_currentPathIndex; }

    // Polygon the agent is on
    uint32_t GetCurrentPoly() const { return m_currentPoly; }

    // Override velocity (for external control)
    void SetVelocity(const glm::vec3& velocity);

    // Teleport (immediate position change)
    void Teleport(const glm::vec3& position);

private:
    void FollowPath(float deltaTime);
    void UpdatePosition(float deltaTime);
    void UpdateFacing(float deltaTime);
    glm::vec3 GetSteeringToTarget(const glm::vec3& target) const;

    const NavMesh* m_navMesh = nullptr;

    glm::vec3 m_position = glm::vec3(0.0f);
    glm::vec3 m_velocity = glm::vec3(0.0f);
    float m_facingAngle = 0.0f;

    std::vector<glm::vec3> m_path;
    size_t m_currentPathIndex = 0;
    uint32_t m_currentPoly = UINT32_MAX;

    glm::vec3 m_destination = glm::vec3(0.0f);
    bool m_hasReachedDestination = true;

    SteeringParams m_steeringParams;
};

// Pathfinder service
class Pathfinder {
public:
    Pathfinder();
    ~Pathfinder() = default;

    // Set navigation mesh
    void SetNavMesh(const NavMesh* navMesh);

    // Synchronous pathfinding
    PathResult FindPath(const PathQueryParams& params);

    // Asynchronous pathfinding
    uint32_t StartPathQuery(const PathQueryParams& params);
    PathStatus GetPathQueryStatus(uint32_t queryId) const;
    PathResult GetPathQueryResult(uint32_t queryId);
    void CancelPathQuery(uint32_t queryId);

    // Process pending async queries (call each frame)
    void Update(uint32_t maxIterationsPerFrame = 1000);

    // Simple movement queries
    glm::vec3 MoveTowards(const glm::vec3& current, const glm::vec3& target,
                          float maxDistance, float agentRadius = 0.5f);

    // Raycast on navmesh
    bool Raycast(const glm::vec3& start, const glm::vec3& end, glm::vec3& hitPoint);

    // Find nearest walkable point
    NavMeshPoint FindNearestWalkable(const glm::vec3& position, float searchRadius = 5.0f);

    // Heuristic function type
    using HeuristicFunc = std::function<float(const glm::vec3&, const glm::vec3&)>;

    // Set custom heuristic
    void SetHeuristic(HeuristicFunc func) { m_heuristic = func; }

    // Reset to default heuristic
    void ResetHeuristic();

    // Statistics
    struct Statistics {
        uint32_t totalQueries = 0;
        uint32_t successfulQueries = 0;
        uint32_t failedQueries = 0;
        uint32_t averageNodesExplored = 0;
        float averagePathLength = 0.0f;
    };

    const Statistics& GetStatistics() const { return m_statistics; }
    void ResetStatistics();

private:
    // Core A* implementation
    PathResult FindPathInternal(const PathQueryParams& params);

    // Reconstruct path from closed list
    void ReconstructPath(const std::unordered_map<uint32_t, AStarNode>& closedList,
                         uint32_t goalPoly, PathResult& result);

    // Smooth path using string pulling (funnel algorithm)
    void SmoothPath(PathResult& result, const PathQueryParams& params);

    // Simple path smoothing (removes redundant waypoints)
    void SimplifyPath(PathResult& result, float maxDeviation);

    // Default heuristic (Euclidean distance)
    static float DefaultHeuristic(const glm::vec3& a, const glm::vec3& b);

    const NavMesh* m_navMesh = nullptr;
    HeuristicFunc m_heuristic;
    Statistics m_statistics;

    // Async query state
    struct AsyncQuery {
        PathQueryParams params;
        PathResult result;
        PathStatus status;

        // A* state for incremental processing
        std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openList;
        std::unordered_map<uint32_t, AStarNode> closedList;
        uint32_t goalPoly;
        uint32_t iterations;
    };

    std::unordered_map<uint32_t, AsyncQuery> m_asyncQueries;
    uint32_t m_nextQueryId = 1;
};

// Steering behaviors
namespace Steering {

// Seek: Move towards target
SteeringOutput Seek(const glm::vec3& position, const glm::vec3& target,
                    const glm::vec3& currentVelocity, const SteeringParams& params);

// Flee: Move away from target
SteeringOutput Flee(const glm::vec3& position, const glm::vec3& threat,
                    const glm::vec3& currentVelocity, const SteeringParams& params);

// Arrive: Move towards target and slow down
SteeringOutput Arrive(const glm::vec3& position, const glm::vec3& target,
                      const glm::vec3& currentVelocity, const SteeringParams& params);

// Pursue: Intercept a moving target
SteeringOutput Pursue(const glm::vec3& position, const glm::vec3& targetPos,
                      const glm::vec3& targetVelocity, const glm::vec3& currentVelocity,
                      const SteeringParams& params);

// Evade: Flee from a moving threat
SteeringOutput Evade(const glm::vec3& position, const glm::vec3& threatPos,
                     const glm::vec3& threatVelocity, const glm::vec3& currentVelocity,
                     const SteeringParams& params);

// Wander: Random wandering behavior
SteeringOutput Wander(const glm::vec3& position, const glm::vec3& forward,
                      const glm::vec3& currentVelocity, float wanderRadius,
                      float wanderDistance, float& wanderAngle,
                      const SteeringParams& params);

// Obstacle avoidance
SteeringOutput AvoidObstacles(const glm::vec3& position, const glm::vec3& forward,
                              const glm::vec3& currentVelocity,
                              const std::vector<glm::vec3>& obstacles,
                              const SteeringParams& params);

// Separation from other agents
SteeringOutput Separation(const glm::vec3& position,
                          const std::vector<glm::vec3>& neighbors,
                          const SteeringParams& params);

// Cohesion with group
SteeringOutput Cohesion(const glm::vec3& position,
                        const std::vector<glm::vec3>& neighbors,
                        const glm::vec3& currentVelocity,
                        const SteeringParams& params);

// Alignment with group velocity
SteeringOutput Alignment(const glm::vec3& position,
                         const std::vector<glm::vec3>& neighborVelocities,
                         const glm::vec3& currentVelocity,
                         const SteeringParams& params);

// Path following
SteeringOutput FollowPath(const glm::vec3& position, const std::vector<glm::vec3>& path,
                          size_t& currentIndex, const glm::vec3& currentVelocity,
                          const SteeringParams& params);

// Blend multiple steering outputs
SteeringOutput BlendSteering(const std::vector<SteeringOutput>& outputs,
                              const std::vector<float>& weights);

} // namespace Steering

} // namespace Cortex::AI
