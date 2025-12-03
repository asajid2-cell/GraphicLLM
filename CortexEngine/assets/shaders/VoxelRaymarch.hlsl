// Experimental fullscreen voxel raymarch shader.
// ------------------------------------------------------------
// This prototype implements a simple grid-based voxel renderer
// driven entirely from the existing FrameConstants layout so it
// can share camera and lighting state with the main renderer.
// Initial implementation used analytic occupancy. This version reads from
// a dense voxel grid uploaded by the CPU so the renderer can visualize
// actual scene geometry before moving on to a sparse SVO layout.
// ------------------------------------------------------------

cbuffer FrameConstants : register(b1)
{
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4x4 g_InvProjectionMatrix;
    float4   g_CameraPosition;
    float4   g_TimeAndExposure;
    float4   g_AmbientColor;
    uint4    g_LightCount;
    struct Light
    {
        float4 position_type;
        float4 direction_cosInner;
        float4 color_range;
        float4 params;
    };
    static const uint LIGHT_MAX = 16;
    Light    g_Lights[LIGHT_MAX];
    float4x4 g_LightViewProjection[6];
    float4   g_CascadeSplits;
    float4   g_ShadowParams;
    float4   g_DebugMode;
    float4   g_PostParams;
    float4   g_EnvParams;
    float4   g_ColorGrade;
    float4   g_FogParams;
    float4   g_AOParams;
    float4   g_BloomParams;
    float4   g_TAAParams;
    float4x4 g_ViewProjectionNoJitter;
    float4x4 g_InvViewProjectionNoJitter;
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
    float4   g_WaterParams0;
    float4   g_WaterParams1;
};

// Dense voxel grid: 1D array of uints representing a dim^3 volume. For the
// initial prototype we assume dim=128 on all axes and pack a simple material
// identifier into the low 8 bits (0 = empty).
StructuredBuffer<uint> g_VoxelGrid : register(t0);

struct VSOut {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

// Fullscreen triangle driven by SV_VertexID (same pattern as PostProcess.hlsl)
VSOut VSMain(uint vid : SV_VertexID)
{
    VSOut o;
    float2 pos;
    if (vid == 0) {
        pos = float2(-1.0f, -1.0f);
    } else if (vid == 1) {
        pos = float2(-1.0f, 3.0f);
    } else {
        pos = float2(3.0f, -1.0f);
    }

    o.position = float4(pos, 0.0f, 1.0f);
    // Map NDC to UV with Y flipped (top-left origin)
    o.uv = float2(0.5f * (pos.x + 1.0f), 0.5f * (1.0f - pos.y));
    return o;
}

// Ray construction helpers ---------------------------------------------------

float3 BuildRayDirection(float2 uv)
{
    // Reconstruct a camera ray from the inverse view-projection matrix and
    // the current pixel UV. This mirrors the SDF debug path in
    // PostProcess.hlsl so that both systems share identical ray setup.
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    float4 clipNear = float4(ndc, 0.0f, 1.0f);
    float4 clipFar  = float4(ndc, 1.0f, 1.0f);

    float4 worldNearH = mul(g_InvViewProjMatrix, clipNear);
    float4 worldFarH  = mul(g_InvViewProjMatrix, clipFar);

    float3 worldNear = worldNearH.xyz / worldNearH.w;
    float3 worldFar  = worldFarH.xyz / worldFarH.w;

    float3 dir = worldFar - worldNear;
    float len = max(length(dir), 1e-4f);
    return dir / len;
}

bool IntersectAABB(float3 rayOrigin,
                   float3 rayDir,
                   float3 bmin,
                   float3 bmax,
                   out float tEnter,
                   out float tExit)
{
    float3 invDir = 1.0f / max(abs(rayDir), 1e-5f) * sign(rayDir);

    float3 t0 = (bmin - rayOrigin) * invDir;
    float3 t1 = (bmax - rayOrigin) * invDir;

    float3 tmin = min(t0, t1);
    float3 tmax = max(t0, t1);

    tEnter = max(max(tmin.x, tmin.y), tmin.z);
    tExit  = min(min(tmax.x, tmax.y), tmax.z);
    return tExit >= max(tEnter, 0.0f);
}

// Dense voxel occupancy query ------------------------------------------------
// Simple dense-grid lookup: returns true when the given cell lies inside the
// volume and the stored material identifier is non-zero.
bool SampleVoxel(int3 cell,
                 int3 gridSize,
                 float3 gridMin,
                 float3 cellSize,
                 out uint materialId)
{
    materialId = 0;

    if (any(cell < 0) || any(cell >= gridSize)) {
        return false;
    }

    uint dim = gridSize.x;
    uint index = cell.x + cell.y * dim + cell.z * dim * dim;
    uint data = g_VoxelGrid[index];
    if (data == 0u) {
        return false;
    }

    materialId = data & 0xFFu;
    return true;
}

float3 MaterialColor(uint materialId)
{
    switch (materialId) {
    case 1:  return float3(0.25f, 0.20f, 0.18f); // floor
    case 2:  return float3(0.15f, 0.6f, 0.2f);   // left wall (green)
    case 3:  return float3(0.75f, 0.15f, 0.15f); // right wall (red)
    case 4:  return float3(0.5f, 0.5f, 0.5f);    // back wall
    case 5:  return float3(0.35f, 0.35f, 0.38f); // ceiling
    case 6:  return float3(0.4f, 0.22f, 0.15f);  // column
    case 7:  return float3(0.85f, 0.85f, 0.9f);  // chrome sphere
    case 8:  return float3(0.8f, 0.5f, 0.2f);    // rough sphere
    default: return float3(0.2f, 0.2f, 0.22f);
    }
}

float3 ComputeSunDirection()
{
    // Light 0 is the directional sun in the main renderer.
    if (g_LightCount.x > 0) {
        float3 dir = g_Lights[0].direction_cosInner.xyz;
        float len = max(length(dir), 1e-4f);
        return -dir / len; // lights encode negative forward
    }
    // Fallback: simple top-down light.
    return normalize(float3(0.3f, -1.0f, 0.25f));
}

// Main pixel shader ----------------------------------------------------------

float4 PSMain(VSOut input) : SV_TARGET
{
    // Build a perspective camera ray that matches the main renderer's SDF
    // debug view so that voxel and raster paths stay visually aligned.
    float3 origin = g_CameraPosition.xyz;
    float3 dir    = BuildRayDirection(input.uv);

    // World-space bounds for the voxel volume. These must match the values
    // used in Renderer::BuildVoxelGridFromScene so that CPU voxelization and
    // GPU traversal agree on which region of space is discretized.
    float3 gridMin = float3(-10.0f, -2.0f, -10.0f);
    float3 gridMax = float3( 10.0f,  8.0f,  10.0f);

    float tEnter, tExit;
    if (!IntersectAABB(origin, dir, gridMin, gridMax, tEnter, tExit)) {
        // Bright gradient background when the ray misses the voxel volume.
        float t = saturate(dir.y * 0.5f + 0.5f);
        float3 sky = lerp(float3(0.15f, 0.20f, 0.30f), float3(0.8f, 0.9f, 1.0f), t);
        return float4(sky, 1.0f);
    }

    // Clamp entry to the near side of the volume.
    float t = max(tEnter, 0.0f);
    float3 pos = origin + dir * t;

    // Discrete grid configuration. Resolution is modest to keep the prototype
    // lightweight; higher resolutions can be used when we add a proper SVO.
    // Must match Renderer::m_voxelGridDim. Using 384^3 gives high detail
    // while keeping the raymarch affordable on the 8 GB target GPU.
    int3   gridSize = int3(384, 384, 384);
    float3 cellSize = (gridMax - gridMin) / float3(gridSize);

    int3 voxel = int3(clamp(floor((pos - gridMin) / cellSize), 0.0f, (float3)(gridSize - 1)));

    // Precompute both signed and absolute inverses of the ray direction.
    // The signed version is used for distance along the ray; the absolute
    // value is used for the per-cell step length so tDelta is always
    // positive regardless of ray direction.
    float3 invDirAbs = 1.0f / max(abs(dir), 1.0e-5f);
    float3 invDir    = invDirAbs * sign(dir);

    int3   step   = int3(dir.x >= 0.0f ? 1 : -1,
                         dir.y >= 0.0f ? 1 : -1,
                         dir.z >= 0.0f ? 1 : -1);

    float3 voxelMin = gridMin + float3(voxel) * cellSize;
    float3 voxelMax = voxelMin + cellSize;

    float3 tMax;
    tMax.x = ((step.x > 0 ? voxelMax.x : voxelMin.x) - pos.x) * invDir.x;
    tMax.y = ((step.y > 0 ? voxelMax.y : voxelMin.y) - pos.y) * invDir.y;
    tMax.z = ((step.z > 0 ? voxelMax.z : voxelMin.z) - pos.z) * invDir.z;

    float3 tDelta = cellSize * invDirAbs;

    const int   kMaxSteps = 512;
    uint        hitMaterial = 0;
    float3      hitNormal   = float3(0.0f, 0.0f, 0.0f);
    bool        hit         = false;

    [loop]
    for (int i = 0; i < kMaxSteps; ++i) {
        if (voxel.x < 0 || voxel.x >= gridSize.x ||
            voxel.y < 0 || voxel.y >= gridSize.y ||
            voxel.z < 0 || voxel.z >= gridSize.z ||
            t > tExit) {
            break;
        }

        uint matId = 0;
        if (SampleVoxel(voxel, gridSize, gridMin, cellSize, matId)) {
            hit = true;
            hitMaterial = matId;
            break;
        }

        // Advance to next voxel cell along the dominant axis.
        if (tMax.x < tMax.y && tMax.x < tMax.z) {
            voxel.x += step.x;
            t = tMax.x;
            tMax.x += tDelta.x;
            hitNormal = float3(-step.x, 0.0f, 0.0f);
        } else if (tMax.y < tMax.z) {
            voxel.y += step.y;
            t = tMax.y;
            tMax.y += tDelta.y;
            hitNormal = float3(0.0f, -step.y, 0.0f);
        } else {
            voxel.z += step.z;
            t = tMax.z;
            tMax.z += tDelta.z;
            hitNormal = float3(0.0f, 0.0f, -step.z);
        }
    }

    if (!hit) {
        float tBg = saturate(dir.y * 0.5f + 0.5f);
        float3 sky = lerp(float3(0.15f, 0.20f, 0.30f), float3(0.8f, 0.9f, 1.0f), tBg);
        return float4(sky, 1.0f);
    }

    float3 sunDir = ComputeSunDirection();
    float3 N = normalize(hitNormal);
    float3 baseColor = MaterialColor(hitMaterial);

    float NdotL = max(dot(N, -sunDir), 0.0f);
    float3 diffuse = baseColor * NdotL;

    // Simple ambient term pulled from FrameConstants.
    float3 ambient = baseColor * g_AmbientColor.rgb;

    float3 color = diffuse + ambient;
    color = pow(saturate(color), 1.0f / 2.2f); // simple gamma
    return float4(color, 1.0f);
}
