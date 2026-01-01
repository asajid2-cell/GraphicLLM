// GrassBend.hlsli
// Grass Bending and Interaction System
// Handles character/vehicle interaction with vegetation for realistic deformation.
//
// Reference: "Rendering Grass in Real-Time" - GPU Pro 2
// Reference: "Ghost of Tsushima Grass Technology" - SIGGRAPH 2021

#ifndef GRASS_BEND_HLSLI
#define GRASS_BEND_HLSLI

// Maximum number of interaction points (characters, vehicles, projectiles)
#define MAX_GRASS_INTERACTORS 16

// Interactor types
#define INTERACTOR_NONE      0
#define INTERACTOR_CHARACTER 1
#define INTERACTOR_VEHICLE   2
#define INTERACTOR_PROJECTILE 3
#define INTERACTOR_EXPLOSION 4

// Grass interaction point data
struct GrassInteractor {
    float3 position;        // World position of interactor
    float radius;           // Interaction radius
    float3 velocity;        // Movement velocity for directional bend
    float strength;         // Bend strength multiplier
    float3 forward;         // Forward direction (for vehicle tire tracks)
    uint type;              // Interactor type
    float height;           // Height of effect (for explosion columns)
    float falloff;          // Falloff exponent (1 = linear, 2 = quadratic)
    float recovery;         // Recovery rate (how fast grass springs back)
    float padding;
};

// Constant buffer for grass bending
cbuffer GrassBendCB : register(b5) {
    GrassInteractor g_Interactors[MAX_GRASS_INTERACTORS];
    uint g_ActiveInteractors;
    float g_GlobalBendStrength;
    float g_WindBendScale;      // How much wind affects bent grass
    float g_RecoverySpeed;      // Global recovery speed multiplier

    float3 g_WindDirection;
    float g_WindStrength;

    float g_Time;
    float g_GrassHeight;        // Average grass height for scaling
    float2 g_Padding;
};

// Result of grass bend calculation
struct GrassBendResult {
    float3 offset;          // World-space offset to apply to vertex
    float bendAmount;       // 0-1 bend intensity for color/normal adjustment
    float3 bendDirection;   // Normalized bend direction
    float windInfluence;    // Reduced wind when bent by interactor
};

// Smooth falloff function
float SmoothFalloff(float distance, float radius, float falloffExp) {
    float t = saturate(distance / radius);
    return pow(1.0 - t, falloffExp);
}

// Compute bend from single interactor
float3 ComputeInteractorBend(
    float3 worldPos,
    float vertexHeight,     // 0 at base, 1 at tip
    GrassInteractor interactor,
    out float bendAmount
) {
    bendAmount = 0.0;

    if (interactor.type == INTERACTOR_NONE) {
        return float3(0, 0, 0);
    }

    // Distance from interactor (2D, ignore height for ground interaction)
    float2 toVertex = worldPos.xz - interactor.position.xz;
    float distance = length(toVertex);

    // Outside radius - no effect
    if (distance > interactor.radius) {
        return float3(0, 0, 0);
    }

    // Height check for explosion columns
    if (interactor.type == INTERACTOR_EXPLOSION) {
        float heightDist = abs(worldPos.y - interactor.position.y);
        if (heightDist > interactor.height) {
            return float3(0, 0, 0);
        }
    }

    // Calculate falloff
    float falloff = SmoothFalloff(distance, interactor.radius, interactor.falloff);

    // Bend direction - away from interactor center
    float2 bendDir2D = distance > 0.001 ? normalize(toVertex) : float2(1, 0);

    // Add velocity influence for directional bending
    float2 velocityInfluence = interactor.velocity.xz * 0.1;
    bendDir2D = normalize(bendDir2D + velocityInfluence);

    // Bend amount increases with vertex height (tip bends more than base)
    float heightFactor = pow(vertexHeight, 1.5);  // Non-linear for natural look
    bendAmount = falloff * interactor.strength * heightFactor;

    // Clamp bend amount
    bendAmount = saturate(bendAmount);

    // Calculate world offset
    float bendDistance = bendAmount * g_GrassHeight * 0.8;  // Max 80% of grass height

    // Slight downward push for trampled grass
    float verticalPush = -bendAmount * g_GrassHeight * 0.2;

    return float3(bendDir2D.x * bendDistance, verticalPush, bendDir2D.y * bendDistance);
}

// Compute vehicle tire track bend (elongated in movement direction)
float3 ComputeTireTrackBend(
    float3 worldPos,
    float vertexHeight,
    GrassInteractor vehicle,
    out float bendAmount
) {
    bendAmount = 0.0;

    // Transform to vehicle local space
    float3 toVertex = worldPos - vehicle.position;

    // Project onto vehicle forward/right axes
    float3 forward = normalize(vehicle.forward);
    float3 right = normalize(cross(float3(0, 1, 0), forward));

    float forwardDist = abs(dot(toVertex, forward));
    float rightDist = abs(dot(toVertex, right));

    // Elongated elliptical falloff (longer in forward direction)
    float forwardRadius = vehicle.radius * 2.0;  // Tire tracks extend forward
    float rightRadius = vehicle.radius * 0.5;    // Narrow width

    float normalizedDist = sqrt(
        (forwardDist * forwardDist) / (forwardRadius * forwardRadius) +
        (rightDist * rightDist) / (rightRadius * rightRadius)
    );

    if (normalizedDist > 1.0) {
        return float3(0, 0, 0);
    }

    float falloff = pow(1.0 - normalizedDist, vehicle.falloff);
    float heightFactor = pow(vertexHeight, 1.5);
    bendAmount = falloff * vehicle.strength * heightFactor;
    bendAmount = saturate(bendAmount);

    // Tire tracks push grass in movement direction
    float3 bendDir = forward * sign(dot(vehicle.velocity, forward));
    bendDir.y = -0.3;  // Push down into ground
    bendDir = normalize(bendDir);

    float bendDistance = bendAmount * g_GrassHeight;

    return bendDir * bendDistance;
}

// Compute explosion radial bend (circular expanding wave)
float3 ComputeExplosionBend(
    float3 worldPos,
    float vertexHeight,
    GrassInteractor explosion,
    out float bendAmount
) {
    bendAmount = 0.0;

    float3 toVertex = worldPos - explosion.position;
    float distance = length(toVertex.xz);

    // Expanding ring wave based on time/recovery value
    float waveRadius = explosion.recovery * explosion.radius;  // recovery used as wave progress
    float waveFront = abs(distance - waveRadius);
    float waveWidth = explosion.radius * 0.3;

    if (waveFront > waveWidth) {
        return float3(0, 0, 0);
    }

    float waveFalloff = 1.0 - (waveFront / waveWidth);
    float heightFactor = pow(vertexHeight, 1.2);

    // Fade strength as wave expands
    float distanceFade = 1.0 - saturate(waveRadius / explosion.radius);

    bendAmount = waveFalloff * explosion.strength * heightFactor * distanceFade;
    bendAmount = saturate(bendAmount);

    // Radial outward direction
    float2 radialDir = distance > 0.001 ? normalize(toVertex.xz) : float2(1, 0);
    float3 bendDir = float3(radialDir.x, 0.2, radialDir.y);  // Slight upward for blast effect
    bendDir = normalize(bendDir);

    float bendDistance = bendAmount * g_GrassHeight * 1.5;  // Explosions bend more

    return bendDir * bendDistance;
}

// Main grass bend calculation
GrassBendResult CalculateGrassBend(
    float3 worldPos,
    float3 localPos,        // Local position in grass mesh
    float grassHeight       // Height of this grass blade
) {
    GrassBendResult result;
    result.offset = float3(0, 0, 0);
    result.bendAmount = 0.0;
    result.bendDirection = float3(0, 0, 0);
    result.windInfluence = 1.0;

    // Calculate vertex height (0 at base, 1 at tip)
    float vertexHeight = saturate(localPos.y / grassHeight);

    // Accumulate bends from all active interactors
    float3 totalOffset = float3(0, 0, 0);
    float totalBend = 0.0;
    float3 weightedDirection = float3(0, 0, 0);

    for (uint i = 0; i < g_ActiveInteractors; i++) {
        GrassInteractor interactor = g_Interactors[i];
        float bendAmount = 0.0;
        float3 bendOffset = float3(0, 0, 0);

        switch (interactor.type) {
            case INTERACTOR_CHARACTER:
            case INTERACTOR_PROJECTILE:
                bendOffset = ComputeInteractorBend(worldPos, vertexHeight, interactor, bendAmount);
                break;

            case INTERACTOR_VEHICLE:
                bendOffset = ComputeTireTrackBend(worldPos, vertexHeight, interactor, bendAmount);
                break;

            case INTERACTOR_EXPLOSION:
                bendOffset = ComputeExplosionBend(worldPos, vertexHeight, interactor, bendAmount);
                break;
        }

        // Accumulate with max blend (strongest bend wins)
        if (bendAmount > totalBend) {
            totalBend = bendAmount;
            totalOffset = bendOffset;
            if (length(bendOffset) > 0.001) {
                weightedDirection = normalize(bendOffset);
            }
        }
    }

    // Apply global strength multiplier
    totalOffset *= g_GlobalBendStrength;
    totalBend *= g_GlobalBendStrength;

    result.offset = totalOffset;
    result.bendAmount = saturate(totalBend);
    result.bendDirection = weightedDirection;

    // Reduce wind influence when grass is bent (trampled grass doesn't sway as much)
    result.windInfluence = lerp(1.0, 0.2, result.bendAmount);

    return result;
}

// Apply wind to grass vertex (call after bend calculation)
float3 ApplyGrassWind(
    float3 worldPos,
    float3 localPos,
    float grassHeight,
    float windInfluence
) {
    float vertexHeight = saturate(localPos.y / grassHeight);

    // World-space noise for variation
    float noiseX = sin(worldPos.x * 0.5 + g_Time * 2.0) * 0.5 + 0.5;
    float noiseZ = cos(worldPos.z * 0.5 + g_Time * 1.7) * 0.5 + 0.5;
    float noise = noiseX * noiseZ;

    // Gust pattern
    float gust = sin(g_Time * 3.0 + worldPos.x * 0.1 + worldPos.z * 0.1) * 0.5 + 0.5;
    gust = pow(gust, 2.0);  // Make gusts more pronounced

    // Wind strength with variation
    float windMagnitude = g_WindStrength * (0.5 + noise * 0.5 + gust * 0.5);
    windMagnitude *= windInfluence;  // Reduce for bent grass
    windMagnitude *= pow(vertexHeight, 2.0);  // More at tip
    windMagnitude *= g_WindBendScale;

    // Apply wind direction with slight randomization
    float3 windOffset = g_WindDirection * windMagnitude;

    // Add oscillation perpendicular to wind
    float3 windRight = normalize(cross(g_WindDirection, float3(0, 1, 0)));
    float oscillation = sin(g_Time * 5.0 + worldPos.x * 2.0) * 0.1 * windMagnitude;
    windOffset += windRight * oscillation;

    return windOffset;
}

// Combine all grass vertex modifications
float3 GetGrassVertexOffset(
    float3 worldPos,
    float3 localPos,
    float grassHeight,
    out float bendAmount,
    out float windInfluence
) {
    // Calculate interactor-based bending
    GrassBendResult bendResult = CalculateGrassBend(worldPos, localPos, grassHeight);

    // Calculate wind offset
    float3 windOffset = ApplyGrassWind(worldPos, localPos, grassHeight, bendResult.windInfluence);

    // Combine offsets
    float3 totalOffset = bendResult.offset + windOffset;

    // Output values for shading
    bendAmount = bendResult.bendAmount;
    windInfluence = bendResult.windInfluence;

    return totalOffset;
}

// Color modification for bent grass (trampled grass is darker/yellower)
float3 GetBentGrassColor(float3 baseColor, float bendAmount) {
    // Trampled grass loses some green, becomes slightly yellow/brown
    float3 trampledColor = baseColor * float3(0.8, 0.7, 0.5);

    // Slight darkening from shadow of bent blades
    trampledColor *= lerp(1.0, 0.85, bendAmount);

    return lerp(baseColor, trampledColor, bendAmount * 0.5);
}

// Normal modification for bent grass
float3 GetBentGrassNormal(float3 baseNormal, float3 bendDirection, float bendAmount) {
    if (bendAmount < 0.01) {
        return baseNormal;
    }

    // Tilt normal in bend direction
    float3 bentNormal = normalize(lerp(baseNormal, bendDirection + float3(0, 0.5, 0), bendAmount * 0.5));

    return bentNormal;
}

#endif // GRASS_BEND_HLSLI
