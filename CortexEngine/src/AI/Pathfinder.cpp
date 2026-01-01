// Pathfinder.cpp
// A* pathfinding implementation.

#include "Pathfinder.h"
#include <algorithm>
#include <cmath>
#include <random>

namespace Cortex::AI {

// NavAgent implementation
NavAgent::NavAgent(const NavMesh* navMesh)
    : m_navMesh(navMesh)
{
}

void NavAgent::SetPosition(const glm::vec3& position) {
    m_position = position;

    if (m_navMesh) {
        NavMeshPoint point = m_navMesh->FindNearestPoint(position, 5.0f);
        if (point.valid) {
            m_position = point.position;
            m_currentPoly = point.polyRef;
        }
    }
}

bool NavAgent::SetDestination(const glm::vec3& destination) {
    if (!m_navMesh) {
        return false;
    }

    m_destination = destination;
    m_hasReachedDestination = false;

    // Create pathfinder and find path
    Pathfinder pathfinder;
    pathfinder.SetNavMesh(m_navMesh);

    PathQueryParams params;
    params.start = m_position;
    params.end = destination;
    params.agentRadius = m_steeringParams.avoidanceRadius;
    params.smoothPath = true;

    PathResult result = pathfinder.FindPath(params);

    if (result.IsValid()) {
        m_path = result.path;
        m_currentPathIndex = 0;
        return true;
    }

    return false;
}

void NavAgent::ClearPath() {
    m_path.clear();
    m_currentPathIndex = 0;
    m_hasReachedDestination = true;
}

void NavAgent::Update(float deltaTime) {
    if (IsFollowingPath()) {
        FollowPath(deltaTime);
    }

    UpdatePosition(deltaTime);
    UpdateFacing(deltaTime);
}

void NavAgent::FollowPath(float deltaTime) {
    if (m_currentPathIndex >= m_path.size()) {
        m_hasReachedDestination = true;
        m_velocity = glm::vec3(0.0f);
        return;
    }

    glm::vec3 target = m_path[m_currentPathIndex];
    glm::vec3 toTarget = target - m_position;
    toTarget.y = 0.0f;  // XZ plane only
    float distToTarget = glm::length(toTarget);

    // Move to next waypoint if close enough
    if (distToTarget < m_steeringParams.pathFollowRadius) {
        m_currentPathIndex++;
        if (m_currentPathIndex >= m_path.size()) {
            m_hasReachedDestination = true;
            m_velocity = glm::vec3(0.0f);
            return;
        }
        target = m_path[m_currentPathIndex];
        toTarget = target - m_position;
        toTarget.y = 0.0f;
        distToTarget = glm::length(toTarget);
    }

    // Steering
    SteeringOutput steering;
    bool isLastWaypoint = (m_currentPathIndex == m_path.size() - 1);

    if (isLastWaypoint) {
        // Arrive at final destination
        steering = Steering::Arrive(m_position, target, m_velocity, m_steeringParams);
    } else {
        // Seek towards next waypoint
        steering = Steering::Seek(m_position, target, m_velocity, m_steeringParams);
    }

    m_velocity = steering.velocity;

    if (steering.arrived) {
        m_hasReachedDestination = true;
    }
}

void NavAgent::UpdatePosition(float deltaTime) {
    if (glm::length(m_velocity) < 0.001f) {
        return;
    }

    glm::vec3 newPos = m_position + m_velocity * deltaTime;

    // Clamp to navmesh
    if (m_navMesh) {
        NavMeshPoint point = m_navMesh->FindNearestPoint(newPos, 2.0f);
        if (point.valid) {
            newPos = point.position;
            m_currentPoly = point.polyRef;
        }
    }

    m_position = newPos;
}

void NavAgent::UpdateFacing(float deltaTime) {
    if (glm::length(m_velocity) < 0.1f) {
        return;
    }

    // Calculate target angle from velocity
    float targetAngle = std::atan2(-m_velocity.x, -m_velocity.z);

    // Smooth rotation
    float angleDiff = targetAngle - m_facingAngle;

    // Normalize to -PI to PI
    while (angleDiff > glm::pi<float>()) angleDiff -= glm::two_pi<float>();
    while (angleDiff < -glm::pi<float>()) angleDiff += glm::two_pi<float>();

    float maxTurn = glm::radians(m_steeringParams.turnSpeed) * deltaTime;
    float turn = glm::clamp(angleDiff, -maxTurn, maxTurn);

    m_facingAngle += turn;
}

glm::vec3 NavAgent::GetSteeringToTarget(const glm::vec3& target) const {
    glm::vec3 desired = target - m_position;
    desired.y = 0.0f;

    float dist = glm::length(desired);
    if (dist < 0.001f) {
        return glm::vec3(0.0f);
    }

    desired = glm::normalize(desired) * m_steeringParams.maxSpeed;
    return desired - m_velocity;
}

void NavAgent::SetVelocity(const glm::vec3& velocity) {
    m_velocity = velocity;
}

void NavAgent::Teleport(const glm::vec3& position) {
    m_position = position;
    m_velocity = glm::vec3(0.0f);

    if (m_navMesh) {
        m_currentPoly = m_navMesh->GetPolyAt(position);
    }

    ClearPath();
}

// Pathfinder implementation
Pathfinder::Pathfinder() {
    ResetHeuristic();
}

void Pathfinder::SetNavMesh(const NavMesh* navMesh) {
    m_navMesh = navMesh;
}

float Pathfinder::DefaultHeuristic(const glm::vec3& a, const glm::vec3& b) {
    return glm::length(b - a);
}

void Pathfinder::ResetHeuristic() {
    m_heuristic = DefaultHeuristic;
}

PathResult Pathfinder::FindPath(const PathQueryParams& params) {
    return FindPathInternal(params);
}

PathResult Pathfinder::FindPathInternal(const PathQueryParams& params) {
    PathResult result;
    result.status = PathStatus::Failed;

    if (!m_navMesh) {
        return result;
    }

    // Find start and end polygons
    NavMeshPoint startPoint = m_navMesh->FindNearestPoint(params.start, params.agentRadius * 4.0f);
    NavMeshPoint endPoint = m_navMesh->FindNearestPoint(params.end, params.agentRadius * 4.0f);

    if (!startPoint.valid || !endPoint.valid) {
        return result;
    }

    uint32_t startPoly = startPoint.polyRef;
    uint32_t goalPoly = endPoint.polyRef;

    // Same polygon - direct path
    if (startPoly == goalPoly) {
        result.status = PathStatus::Complete;
        result.path.push_back(startPoint.position);
        result.path.push_back(endPoint.position);
        result.polygonPath.push_back(startPoly);
        result.totalCost = glm::length(endPoint.position - startPoint.position);
        return result;
    }

    // A* search
    std::priority_queue<AStarNode, std::vector<AStarNode>, std::greater<AStarNode>> openList;
    std::unordered_map<uint32_t, AStarNode> closedList;

    AStarNode startNode;
    startNode.polyRef = startPoly;
    startNode.gCost = 0.0f;
    startNode.hCost = m_heuristic(startPoint.position, endPoint.position);
    startNode.parentRef = UINT32_MAX;
    startNode.entryPoint = startPoint.position;

    openList.push(startNode);

    uint32_t iterations = 0;
    bool found = false;

    while (!openList.empty() && iterations < params.maxIterations) {
        AStarNode current = openList.top();
        openList.pop();
        iterations++;

        // Already processed?
        if (closedList.find(current.polyRef) != closedList.end()) {
            continue;
        }

        closedList[current.polyRef] = current;

        // Reached goal?
        if (current.polyRef == goalPoly) {
            found = true;
            break;
        }

        // Explore neighbors
        std::vector<uint32_t> neighbors = m_navMesh->GetPolyNeighbors(current.polyRef);

        for (uint32_t neighborRef : neighbors) {
            if (closedList.find(neighborRef) != closedList.end()) {
                continue;
            }

            glm::vec3 neighborCenter = m_navMesh->GetPolyCenter(neighborRef);
            float edgeCost = glm::length(neighborCenter - current.entryPoint);
            float polyCost = m_navMesh->GetPolyCost(neighborRef);
            float gCost = current.gCost + edgeCost * polyCost;

            // Check path length limit
            if (gCost > params.maxPathLength) {
                continue;
            }

            AStarNode neighbor;
            neighbor.polyRef = neighborRef;
            neighbor.gCost = gCost;
            neighbor.hCost = m_heuristic(neighborCenter, endPoint.position);
            neighbor.parentRef = current.polyRef;
            neighbor.entryPoint = neighborCenter;

            openList.push(neighbor);
        }
    }

    result.nodesExplored = iterations;

    if (found) {
        ReconstructPath(closedList, goalPoly, result);
        result.status = PathStatus::Complete;

        // Add actual start and end points
        result.path.insert(result.path.begin(), startPoint.position);
        result.path.push_back(endPoint.position);

        if (params.smoothPath) {
            SmoothPath(result, params);
        }
    } else if (!closedList.empty()) {
        // Find closest reached polygon to goal
        float bestDist = FLT_MAX;
        uint32_t bestPoly = UINT32_MAX;

        for (const auto& [polyRef, node] : closedList) {
            float dist = glm::length(node.entryPoint - endPoint.position);
            if (dist < bestDist) {
                bestDist = dist;
                bestPoly = polyRef;
            }
        }

        if (bestPoly != UINT32_MAX) {
            ReconstructPath(closedList, bestPoly, result);
            result.status = PathStatus::Partial;
            result.path.insert(result.path.begin(), startPoint.position);
        }
    }

    // Update statistics
    m_statistics.totalQueries++;
    if (result.status == PathStatus::Complete) {
        m_statistics.successfulQueries++;
    } else {
        m_statistics.failedQueries++;
    }

    return result;
}

void Pathfinder::ReconstructPath(const std::unordered_map<uint32_t, AStarNode>& closedList,
                                  uint32_t goalPoly, PathResult& result) {
    std::vector<uint32_t> polyPath;
    std::vector<glm::vec3> points;

    uint32_t current = goalPoly;
    while (current != UINT32_MAX) {
        auto it = closedList.find(current);
        if (it == closedList.end()) break;

        polyPath.push_back(current);
        points.push_back(it->second.entryPoint);
        current = it->second.parentRef;
        result.totalCost = it->second.gCost;
    }

    // Reverse to get start-to-goal order
    std::reverse(polyPath.begin(), polyPath.end());
    std::reverse(points.begin(), points.end());

    result.polygonPath = polyPath;
    result.path = points;
}

void Pathfinder::SmoothPath(PathResult& result, const PathQueryParams& params) {
    if (result.path.size() < 3) {
        return;
    }

    // Simple smoothing: remove redundant waypoints
    std::vector<glm::vec3> smoothed;
    smoothed.push_back(result.path.front());

    for (size_t i = 1; i < result.path.size() - 1; i++) {
        glm::vec3 prev = smoothed.back();
        glm::vec3 next = result.path[i + 1];

        // Check if we can skip this waypoint
        glm::vec3 hitPoint;
        if (!Raycast(prev, next, hitPoint)) {
            // Can skip, continue to next
            continue;
        }

        // Cannot skip, add waypoint
        smoothed.push_back(result.path[i]);
    }

    smoothed.push_back(result.path.back());
    result.path = smoothed;
}

uint32_t Pathfinder::StartPathQuery(const PathQueryParams& params) {
    uint32_t queryId = m_nextQueryId++;

    AsyncQuery query;
    query.params = params;
    query.status = PathStatus::InProgress;
    query.iterations = 0;

    // Initialize A* state
    if (m_navMesh) {
        NavMeshPoint startPoint = m_navMesh->FindNearestPoint(params.start, params.agentRadius * 4.0f);
        NavMeshPoint endPoint = m_navMesh->FindNearestPoint(params.end, params.agentRadius * 4.0f);

        if (startPoint.valid && endPoint.valid) {
            query.goalPoly = endPoint.polyRef;

            AStarNode startNode;
            startNode.polyRef = startPoint.polyRef;
            startNode.gCost = 0.0f;
            startNode.hCost = m_heuristic(startPoint.position, endPoint.position);
            startNode.parentRef = UINT32_MAX;
            startNode.entryPoint = startPoint.position;

            query.openList.push(startNode);
        } else {
            query.status = PathStatus::Failed;
        }
    } else {
        query.status = PathStatus::Failed;
    }

    m_asyncQueries[queryId] = std::move(query);
    return queryId;
}

PathStatus Pathfinder::GetPathQueryStatus(uint32_t queryId) const {
    auto it = m_asyncQueries.find(queryId);
    if (it != m_asyncQueries.end()) {
        return it->second.status;
    }
    return PathStatus::Failed;
}

PathResult Pathfinder::GetPathQueryResult(uint32_t queryId) {
    auto it = m_asyncQueries.find(queryId);
    if (it != m_asyncQueries.end()) {
        PathResult result = it->second.result;
        if (it->second.status == PathStatus::Complete || it->second.status == PathStatus::Failed) {
            m_asyncQueries.erase(it);
        }
        return result;
    }
    return PathResult();
}

void Pathfinder::CancelPathQuery(uint32_t queryId) {
    m_asyncQueries.erase(queryId);
}

void Pathfinder::Update(uint32_t maxIterationsPerFrame) {
    uint32_t totalIterations = 0;

    for (auto& [queryId, query] : m_asyncQueries) {
        if (query.status != PathStatus::InProgress) {
            continue;
        }

        uint32_t iterationsForQuery = maxIterationsPerFrame - totalIterations;
        if (iterationsForQuery == 0) {
            break;
        }

        // Continue A* search
        // (Simplified - full implementation would continue from saved state)
        PathResult result = FindPath(query.params);
        query.result = result;
        query.status = result.status;
        query.iterations = result.nodesExplored;

        totalIterations += result.nodesExplored;
    }
}

glm::vec3 Pathfinder::MoveTowards(const glm::vec3& current, const glm::vec3& target,
                                   float maxDistance, float agentRadius) {
    if (!m_navMesh) {
        return current;
    }

    glm::vec3 direction = target - current;
    float distance = glm::length(direction);

    if (distance <= maxDistance) {
        // Can reach target directly
        glm::vec3 hitPoint;
        if (!Raycast(current, target, hitPoint)) {
            return target;
        }
        return hitPoint;
    }

    // Move towards target
    direction = glm::normalize(direction) * maxDistance;
    glm::vec3 newPos = current + direction;

    // Clamp to navmesh
    NavMeshPoint point = m_navMesh->FindNearestPoint(newPos, agentRadius * 2.0f);
    if (point.valid) {
        return point.position;
    }

    return current;
}

bool Pathfinder::Raycast(const glm::vec3& start, const glm::vec3& end, glm::vec3& hitPoint) {
    if (!m_navMesh) {
        hitPoint = start;
        return false;
    }

    NavMeshRaycastResult result = m_navMesh->Raycast(start, end);
    hitPoint = result.hit ? result.hitPoint : end;
    return result.hit;
}

NavMeshPoint Pathfinder::FindNearestWalkable(const glm::vec3& position, float searchRadius) {
    if (!m_navMesh) {
        return NavMeshPoint();
    }
    return m_navMesh->FindNearestPoint(position, searchRadius);
}

void Pathfinder::ResetStatistics() {
    m_statistics = Statistics();
}

// Steering behaviors implementation
namespace Steering {

SteeringOutput Seek(const glm::vec3& position, const glm::vec3& target,
                    const glm::vec3& currentVelocity, const SteeringParams& params) {
    SteeringOutput output;

    glm::vec3 desired = target - position;
    desired.y = 0.0f;

    float dist = glm::length(desired);
    if (dist < 0.001f) {
        output.arrived = true;
        return output;
    }

    desired = glm::normalize(desired) * params.maxSpeed;
    glm::vec3 steering = desired - currentVelocity;

    // Limit acceleration
    float steeringMag = glm::length(steering);
    if (steeringMag > params.acceleration) {
        steering = glm::normalize(steering) * params.acceleration;
    }

    output.velocity = currentVelocity + steering;

    // Limit speed
    float speed = glm::length(output.velocity);
    if (speed > params.maxSpeed) {
        output.velocity = glm::normalize(output.velocity) * params.maxSpeed;
    }

    return output;
}

SteeringOutput Flee(const glm::vec3& position, const glm::vec3& threat,
                    const glm::vec3& currentVelocity, const SteeringParams& params) {
    SteeringOutput output;

    glm::vec3 desired = position - threat;
    desired.y = 0.0f;

    float dist = glm::length(desired);
    if (dist < 0.001f) {
        return output;
    }

    desired = glm::normalize(desired) * params.maxSpeed;
    glm::vec3 steering = desired - currentVelocity;

    float steeringMag = glm::length(steering);
    if (steeringMag > params.acceleration) {
        steering = glm::normalize(steering) * params.acceleration;
    }

    output.velocity = currentVelocity + steering;

    float speed = glm::length(output.velocity);
    if (speed > params.maxSpeed) {
        output.velocity = glm::normalize(output.velocity) * params.maxSpeed;
    }

    return output;
}

SteeringOutput Arrive(const glm::vec3& position, const glm::vec3& target,
                      const glm::vec3& currentVelocity, const SteeringParams& params) {
    SteeringOutput output;

    glm::vec3 toTarget = target - position;
    toTarget.y = 0.0f;

    float dist = glm::length(toTarget);

    if (dist < params.stoppingRadius) {
        output.velocity = glm::vec3(0.0f);
        output.arrived = true;
        return output;
    }

    float targetSpeed = params.maxSpeed;

    // Slow down in arrival radius
    if (dist < params.arrivalRadius) {
        targetSpeed = params.maxSpeed * (dist / params.arrivalRadius);
    }

    glm::vec3 desired = glm::normalize(toTarget) * targetSpeed;
    glm::vec3 steering = desired - currentVelocity;

    float steeringMag = glm::length(steering);
    if (steeringMag > params.deceleration) {
        steering = glm::normalize(steering) * params.deceleration;
    }

    output.velocity = currentVelocity + steering;

    return output;
}

SteeringOutput Pursue(const glm::vec3& position, const glm::vec3& targetPos,
                      const glm::vec3& targetVelocity, const glm::vec3& currentVelocity,
                      const SteeringParams& params) {
    glm::vec3 toTarget = targetPos - position;
    float dist = glm::length(toTarget);

    float speed = glm::length(currentVelocity);
    float predictionTime = (speed > 0.0f) ? dist / speed : 0.0f;
    predictionTime = std::min(predictionTime, 2.0f);

    glm::vec3 predictedTarget = targetPos + targetVelocity * predictionTime;
    return Seek(position, predictedTarget, currentVelocity, params);
}

SteeringOutput Evade(const glm::vec3& position, const glm::vec3& threatPos,
                     const glm::vec3& threatVelocity, const glm::vec3& currentVelocity,
                     const SteeringParams& params) {
    glm::vec3 toThreat = threatPos - position;
    float dist = glm::length(toThreat);

    float speed = glm::length(currentVelocity);
    float predictionTime = (speed > 0.0f) ? dist / speed : 0.0f;
    predictionTime = std::min(predictionTime, 2.0f);

    glm::vec3 predictedThreat = threatPos + threatVelocity * predictionTime;
    return Flee(position, predictedThreat, currentVelocity, params);
}

SteeringOutput Wander(const glm::vec3& position, const glm::vec3& forward,
                      const glm::vec3& currentVelocity, float wanderRadius,
                      float wanderDistance, float& wanderAngle,
                      const SteeringParams& params) {
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    // Update wander angle
    wanderAngle += dist(rng) * 0.5f;

    // Calculate wander target
    glm::vec3 circleCenter = position + glm::normalize(forward) * wanderDistance;
    glm::vec3 displacement = glm::vec3(
        std::cos(wanderAngle) * wanderRadius,
        0.0f,
        std::sin(wanderAngle) * wanderRadius
    );

    glm::vec3 wanderTarget = circleCenter + displacement;
    return Seek(position, wanderTarget, currentVelocity, params);
}

SteeringOutput AvoidObstacles(const glm::vec3& position, const glm::vec3& forward,
                              const glm::vec3& currentVelocity,
                              const std::vector<glm::vec3>& obstacles,
                              const SteeringParams& params) {
    SteeringOutput output;
    output.velocity = currentVelocity;

    float avoidanceForce = 0.0f;
    glm::vec3 avoidanceDir(0.0f);

    for (const glm::vec3& obstacle : obstacles) {
        glm::vec3 toObstacle = obstacle - position;
        float dist = glm::length(toObstacle);

        if (dist < params.avoidanceRadius * 2.0f && dist > 0.001f) {
            float strength = 1.0f - (dist / (params.avoidanceRadius * 2.0f));
            avoidanceDir -= glm::normalize(toObstacle) * strength;
            avoidanceForce += strength;
        }
    }

    if (avoidanceForce > 0.0f) {
        avoidanceDir = glm::normalize(avoidanceDir);
        output.velocity += avoidanceDir * params.maxSpeed * avoidanceForce;

        float speed = glm::length(output.velocity);
        if (speed > params.maxSpeed) {
            output.velocity = glm::normalize(output.velocity) * params.maxSpeed;
        }
    }

    return output;
}

SteeringOutput Separation(const glm::vec3& position,
                          const std::vector<glm::vec3>& neighbors,
                          const SteeringParams& params) {
    SteeringOutput output;
    glm::vec3 steering(0.0f);
    int count = 0;

    for (const glm::vec3& neighbor : neighbors) {
        glm::vec3 toSelf = position - neighbor;
        float dist = glm::length(toSelf);

        if (dist > 0.0f && dist < params.separationRadius) {
            glm::vec3 repulse = glm::normalize(toSelf) / dist;
            steering += repulse;
            count++;
        }
    }

    if (count > 0) {
        steering /= static_cast<float>(count);
        if (glm::length(steering) > 0.0f) {
            steering = glm::normalize(steering) * params.maxSpeed;
        }
    }

    output.velocity = steering;
    return output;
}

SteeringOutput Cohesion(const glm::vec3& position,
                        const std::vector<glm::vec3>& neighbors,
                        const glm::vec3& currentVelocity,
                        const SteeringParams& params) {
    if (neighbors.empty()) {
        return SteeringOutput();
    }

    glm::vec3 centerOfMass(0.0f);
    for (const glm::vec3& neighbor : neighbors) {
        centerOfMass += neighbor;
    }
    centerOfMass /= static_cast<float>(neighbors.size());

    return Seek(position, centerOfMass, currentVelocity, params);
}

SteeringOutput Alignment(const glm::vec3& position,
                         const std::vector<glm::vec3>& neighborVelocities,
                         const glm::vec3& currentVelocity,
                         const SteeringParams& params) {
    if (neighborVelocities.empty()) {
        return SteeringOutput();
    }

    glm::vec3 avgVelocity(0.0f);
    for (const glm::vec3& vel : neighborVelocities) {
        avgVelocity += vel;
    }
    avgVelocity /= static_cast<float>(neighborVelocities.size());

    SteeringOutput output;
    output.velocity = avgVelocity;

    float speed = glm::length(output.velocity);
    if (speed > params.maxSpeed) {
        output.velocity = glm::normalize(output.velocity) * params.maxSpeed;
    }

    return output;
}

SteeringOutput FollowPath(const glm::vec3& position, const std::vector<glm::vec3>& path,
                          size_t& currentIndex, const glm::vec3& currentVelocity,
                          const SteeringParams& params) {
    if (path.empty() || currentIndex >= path.size()) {
        SteeringOutput output;
        output.arrived = true;
        return output;
    }

    glm::vec3 target = path[currentIndex];
    glm::vec3 toTarget = target - position;
    toTarget.y = 0.0f;

    if (glm::length(toTarget) < params.pathFollowRadius) {
        currentIndex++;
        if (currentIndex >= path.size()) {
            SteeringOutput output;
            output.arrived = true;
            return output;
        }
        target = path[currentIndex];
    }

    bool isLastPoint = (currentIndex == path.size() - 1);
    if (isLastPoint) {
        return Arrive(position, target, currentVelocity, params);
    } else {
        return Seek(position, target, currentVelocity, params);
    }
}

SteeringOutput BlendSteering(const std::vector<SteeringOutput>& outputs,
                              const std::vector<float>& weights) {
    SteeringOutput result;

    float totalWeight = 0.0f;
    for (size_t i = 0; i < outputs.size() && i < weights.size(); i++) {
        result.velocity += outputs[i].velocity * weights[i];
        totalWeight += weights[i];
    }

    if (totalWeight > 0.0f) {
        result.velocity /= totalWeight;
    }

    return result;
}

} // namespace Steering

} // namespace Cortex::AI
