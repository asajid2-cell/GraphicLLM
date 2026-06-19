// DXR ray-traced sun shadow visibility pass.
// This shader library is designed to be used with a dedicated DXR pipeline
// that binds:
//   - TLAS and depth in SRV space2 (t0, t1)
//   - Compact RT material buffer in SRV space2 (t3), keyed by TLAS InstanceID()
//   - Shadow mask UAV in space2 (u0)
//   - FrameConstants in space0 (b0), matching ShaderTypes.h / Basic.hlsl.

#include "RaytracingMaterials.hlsli"

RaytracingAccelerationStructure g_TopLevel : register(t0, space2);
Texture2D<float>               g_Depth     : register(t1, space2);
RWTexture2D<float>             g_ShadowMask: register(u0, space2);

cbuffer FrameConstants : register(b0, space0)
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
    Light   g_Lights[LIGHT_MAX];
    float4x4 g_LightViewProjection[6];
    float4   g_CascadeSplits;
    float4   g_ShadowParams;
    float4   g_DebugMode;
    float4   g_PostParams;
    float4   g_EnvParams;
    float4   g_ColorGrade;
    float4   g_FogParams;
    float4   g_FogExtraParams;
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

struct ShadowPayload
{
    float visibility;
    float hitT;
};

uint Hash(uint v)
{
    v ^= v >> 16u;
    v *= 0x7feb352du;
    v ^= v >> 15u;
    v *= 0x846ca68bu;
    v ^= v >> 16u;
    return v;
}

float2 BlueNoiseUnit(uint2 pixel, uint sampleIndex)
{
    uint frame = (uint)(g_TimeAndExposure.x * 60.0f + 0.5f);
    uint seed = Hash(pixel.x * 1973u ^ pixel.y * 9277u ^ sampleIndex * 26699u ^ frame * 31847u);
    uint seed2 = Hash(seed ^ 0x68bc21ebu);
    return float2(seed & 0xffffu, seed2 & 0xffffu) * (1.0f / 65535.0f);
}

void BuildOrthonormalBasis(float3 n, out float3 t, out float3 b)
{
    float3 up = (abs(n.y) < 0.999f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    t = normalize(cross(up, n));
    b = cross(n, t);
}

float3 SampleSunDiskDirection(float3 sunDir, uint2 pixel, uint sampleIndex)
{
    float2 xi = BlueNoiseUnit(pixel, sampleIndex);
    float r = sqrt(xi.x);
    float phi = xi.y * 6.28318530718f;
    float s;
    float c;
    sincos(phi, s, c);

    float3 tangent;
    float3 bitangent;
    BuildOrthonormalBasis(sunDir, tangent, bitangent);

    const float sunAngularRadius = 0.0131f; // ~0.75 degrees, between real sun and cinematic area sun.
    float2 disk = float2(c, s) * (r * sunAngularRadius);
    return normalize(sunDir + tangent * disk.x + bitangent * disk.y);
}

float TraceVisibility(float3 origin, float3 direction, float tMax, out float hitT)
{
    ShadowPayload payload;
    payload.visibility = 1.0f;
    payload.hitT = tMax;

    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = direction;
    ray.TMin = 0.0f;
    ray.TMax = tMax;

    TraceRay(
        g_TopLevel,
        RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH,
        /*InstanceInclusionMask*/ 0xFF,
        /*RayContributionToHitGroupIndex*/ 0,
        /*MultiplierForGeometryContributionToHitGroupIndex*/ 0,
        /*MissShaderIndex*/ 0,
        ray,
        payload);

    hitT = payload.hitT;
    return saturate(payload.visibility);
}

void StoreShadowBlock(uint2 launchIndex, uint2 launchDims, float value)
{
    [unroll]
    for (uint y = 0u; y < 2u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 2u; ++x)
        {
            uint2 p = launchIndex + uint2(x, y);
            if (p.x < launchDims.x && p.y < launchDims.y)
            {
                g_ShadowMask[p] = value;
            }
        }
    }
}

[shader("miss")]
void Miss_Shadow(inout ShadowPayload payload)
{
    payload.visibility = 1.0f;
    payload.hitT = 10000.0f;
}

[shader("closesthit")]
void ClosestHit_Shadow(inout ShadowPayload payload, in BuiltInTriangleIntersectionAttributes /*attribs*/)
{
    RTMaterial material = g_RTMaterials[InstanceID()];
    payload.visibility = RTMaterialSunVisibility(material);
    payload.hitT = RayTCurrent();
}

[shader("raygeneration")]
void RayGen_Shadow()
{
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDims  = DispatchRaysDimensions().xy;

    // The raster shadow map remains the primary high-frequency shadow source.
    // RT sun shadows are a contact/visibility refinement, so trace at quarter
    // rate and broadcast across a 2x2 block. TAA and PCF hide the small spatial
    // quantization while the number of expensive TraceRay calls drops sharply.
    if (((launchIndex.x | launchIndex.y) & 1u) != 0u)
    {
        return;
    }

    // Load depth; if depth is invalid, treat as unshadowed.
    float depth = g_Depth.Load(int3(launchIndex, 0));
    if (depth <= 0.0f || depth >= 1.0f)
    {
        StoreShadowBlock(launchIndex, launchDims, 1.0f);
        return;
    }

    // Reconstruct clip-space position (DirectX 0..1 depth).
    float2 ndc;
    ndc.x = ( (float(launchIndex.x) + 0.5f) / float(launchDims.x) ) * 2.0f - 1.0f;
    ndc.y = 1.0f - ( (float(launchIndex.y) + 0.5f) / float(launchDims.y) ) * 2.0f;
    float4 clipPos = float4(ndc.x, ndc.y, depth, 1.0f);

    // Unproject to world space using the same inverse view-projection matrix
    // that was used to write the depth buffer (jittered). This keeps screen-
    // space depth and TLAS queries in lock-step and avoids systematic
    // distortions/banding from mismatched matrices.
    float4 worldH = mul(g_InvViewProjMatrix, clipPos);
    if (worldH.w == 0.0f)
    {
        StoreShadowBlock(launchIndex, launchDims, 1.0f);
        return;
    }
    float3 worldPos = worldH.xyz / worldH.w;

    // Use light 0 as the sun (directional). direction_cosInner.xyz stores
    // the direction from the shaded point *toward* the light in the main
    // PBR path, and we cast the shadow ray along the same direction.
    float3 sunDir = float3(0.0f, -1.0f, 0.0f);
    if (g_LightCount.x > 0)
    {
        sunDir = normalize(g_Lights[0].direction_cosInner.xyz);
    }

    const float bias = 0.012f;
    float3 origin = worldPos + sunDir * bias;

    float hitT0;
    float hitT1;
    float visibility = 0.0f;
    visibility += TraceVisibility(origin, SampleSunDiskDirection(sunDir, launchIndex, 0u), 10000.0f, hitT0);
    visibility += TraceVisibility(origin, SampleSunDiskDirection(sunDir, launchIndex, 1u), 10000.0f, hitT1);
    visibility *= 0.5f;

    // A short center ray adds tight grounding near object bases without making
    // broad sun penumbrae uniformly darker.
    float contactHitT;
    float contactVisibility = TraceVisibility(origin, sunDir, 1.35f, contactHitT);
    float contact = (contactVisibility < 0.98f)
        ? (1.0f - smoothstep(0.08f, 1.35f, contactHitT))
        : 0.0f;
    visibility = min(visibility, lerp(visibility, contactVisibility, contact * 0.65f));

    StoreShadowBlock(launchIndex, launchDims, saturate(max(visibility, 0.08f)));
}
