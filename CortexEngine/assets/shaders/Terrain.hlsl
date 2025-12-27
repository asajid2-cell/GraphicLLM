// Procedural terrain clipmap shaders (forward path).
// The mesh is a flat XZ grid; the vertex shader displaces it using a
// deterministic height function that matches the CPU collision sampler.

// This file intentionally duplicates the relevant constant buffer layouts
// from Basic.hlsl so it can share the engine root signature.

// Constant buffers - must match ShaderTypes.h / Basic.hlsl.
cbuffer ObjectConstants : register(b0)
{
    float4x4 g_ModelMatrix;
    float4x4 g_NormalMatrix;
};

cbuffer FrameConstants : register(b1)
{
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4x4 g_InvProjectionMatrix;
    float4 g_CameraPosition;
    float4 g_TimeAndExposure;
    float4 g_AmbientColor;
    uint4 g_LightCount;
    struct Light
    {
        float4 position_type;
        float4 direction_cosInner;
        float4 color_range;
        float4 params;
    };
    static const uint LIGHT_MAX = 16;
    Light g_Lights[LIGHT_MAX];
    float4x4 g_LightViewProjection[6];
    float4 g_CascadeSplits;
    float4 g_ShadowParams;
    float4 g_DebugMode;
    float4 g_PostParams;
    float4 g_EnvParams;
    float4 g_ColorGrade;
    float4 g_FogParams;
    float4 g_AOParams;
    float4 g_BloomParams;
    float4 g_TAAParams;
    float4x4 g_ViewProjectionNoJitter;
    float4x4 g_InvViewProjectionNoJitter;
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
    float4 g_WaterParams0;
    float4 g_WaterParams1;
    uint4  g_ScreenAndCluster;
    uint4  g_ClusterParams;
    uint4  g_ClusterSRVIndices;
    float4 g_ProjectionParams;
};

// Terrain parameters (b2). Other pipelines bind MaterialConstants here.
cbuffer TerrainConstants : register(b2)
{
    uint4  g_TerrainSeedAndOctaves; // x=seed, y=octaves
    float4 g_TerrainParams0;        // x=amplitude, y=frequency, z=lacunarity, w=gain
    float4 g_TerrainParams1;        // x=warp, y=skirtDepth, z=originHiX, w=originHiZ
    float4 g_TerrainParams2;        // x=originLoX, y=originLoZ
};

// Shadow constants (b3) are bound by the engine root signature, but unused here.
cbuffer ShadowConstants : register(b3)
{
    uint4 g_ShadowCascadeIndex;
};

Texture2DArray g_ShadowMap : register(t0, space1);
SamplerState g_Sampler : register(s0);

static const float SHADOW_MAP_SIZE = 2048.0f;

#include "TerrainNoise.hlsli"

float TerrainHeight(float worldX, float worldZ)
{
    uint seed = g_TerrainSeedAndOctaves.x;
    int octaves = (int)g_TerrainSeedAndOctaves.y;
    float amplitude = g_TerrainParams0.x;
    float frequency = g_TerrainParams0.y;
    float lacunarity = g_TerrainParams0.z;
    float gain = g_TerrainParams0.w;
    float warp = g_TerrainParams1.x;
    return TerrainHeightParams(worldX, worldZ, seed, octaves, amplitude, frequency, lacunarity, gain, warp);
}

float SamplePCF(float2 shadowUV, float currentDepth, float bias, float pcfRadius, uint cascadeIndex)
{
    float2 texelSize = 1.0f / float2(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);

    float shadow = 0.0f;
    int samples = 0;
    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize * pcfRadius;
            float depthSample = g_ShadowMap.Sample(g_Sampler, float3(shadowUV + offset, cascadeIndex)).r;
            shadow += (currentDepth - bias > depthSample) ? 0.0f : 1.0f;
            samples++;
        }
    }
    return shadow / max(samples, 1);
}

float ComputeShadowCascade(float3 worldPos, float3 normal, uint cascadeIndex)
{
    cascadeIndex = min(cascadeIndex, 2u);

    float4 lightClip = mul(g_LightViewProjection[cascadeIndex], float4(worldPos, 1.0f));
    if (lightClip.w <= 1e-4f || !all(isfinite(lightClip)))
    {
        return 1.0f;
    }
    float3 lightNDC = lightClip.xyz / lightClip.w;

    if (lightNDC.x < -1.0f || lightNDC.x > 1.0f ||
        lightNDC.y < -1.0f || lightNDC.y > 1.0f ||
        lightNDC.z < 0.0f || lightNDC.z > 1.0f)
    {
        return 1.0f;
    }

    float2 shadowUV;
    shadowUV.x = 0.5f * lightNDC.x + 0.5f;
    shadowUV.y = -0.5f * lightNDC.y + 0.5f;

    float currentDepth = lightNDC.z;
    float bias = g_ShadowParams.x;
    float pcfRadius = g_ShadowParams.y;

    float cascadeScale = 1.0f + (float)cascadeIndex * 0.5f;
    bias *= cascadeScale;

    float3 lightDirWS = normalize(g_Lights[0].direction_cosInner.xyz);
    float ndotl = saturate(dot(normal, lightDirWS));
    bias *= lerp(1.5f, 0.5f, ndotl);

    return SamplePCF(shadowUV, currentDepth, bias, pcfRadius, cascadeIndex);
}

float ComputeShadow(float3 worldPos, float3 normal)
{
    if (g_ShadowParams.z < 0.5f)
    {
        return 1.0f;
    }

    float3 viewPos = mul(g_ViewMatrix, float4(worldPos, 1.0f)).xyz;
    float depth = viewPos.z;

    float split0 = g_CascadeSplits.x;
    float split1 = g_CascadeSplits.y;

    uint primary = 0;
    uint secondary = 0;
    float blend = 0.0f;

    float range0 = max(split0 * 0.2f, 4.0f);
    float range1 = max(split1 * 0.2f, 8.0f);

    if (depth <= split0)
    {
        primary = 0;
        secondary = 1;
        float d = split0 - depth;
        blend = saturate(1.0f - d / range0);
    }
    else if (depth <= split1)
    {
        float d0 = depth - split0;
        float d1 = split1 - depth;
        primary = 1;
        secondary = (d1 < d0) ? 2 : 0;
        float range = (secondary == 2) ? range1 : range0;
        float d = (secondary == 2) ? d1 : d0;
        blend = saturate(1.0f - d / range);
    }
    else
    {
        primary = 2;
        secondary = 1;
        float d = depth - split1;
        blend = saturate(1.0f - d / range1);
    }

    float s0 = ComputeShadowCascade(worldPos, normal, primary);
    float s1 = ComputeShadowCascade(worldPos, normal, secondary);
    return lerp(s0, s1, blend);
}

struct VSInput
{
    float3 position : POSITION;
    float3 normal   : NORMAL;
    float4 tangent  : TANGENT;
    float2 texCoord : TEXCOORD;
};

struct VSOutput
{
    float4 position      : SV_POSITION;
    float3 worldPos      : TEXCOORD0;
    float3 worldNormal   : TEXCOORD1;
    float  roughness     : TEXCOORD2;
};

VSOutput TerrainVS(VSInput input)
{
    VSOutput o;

    float3 worldPos = mul(g_ModelMatrix, float4(input.position, 1.0f)).xyz;

    float2 originHi = float2(g_TerrainParams1.z, g_TerrainParams1.w);
    float2 originLo = float2(g_TerrainParams2.x, g_TerrainParams2.y);
    float2 worldXZ = worldPos.xz + originHi + originLo;

    float h = TerrainHeight(worldXZ.x, worldXZ.y);
    if (input.texCoord.y > 0.5f)
    {
        h -= g_TerrainParams1.y; // skirt depth
    }
    worldPos.y = h;

    // Normal via finite differences in world units.
    const float eps = 0.5f;
    float hL = TerrainHeight(worldXZ.x - eps, worldXZ.y);
    float hR = TerrainHeight(worldXZ.x + eps, worldXZ.y);
    float hD = TerrainHeight(worldXZ.x, worldXZ.y - eps);
    float hU = TerrainHeight(worldXZ.x, worldXZ.y + eps);
    float3 n = normalize(float3(-(hR - hL), 2.0f * eps, -(hU - hD)));

    o.position = mul(g_ViewProjectionMatrix, float4(worldPos, 1.0f));
    o.worldPos = worldPos;
    o.worldNormal = n;

    // Procedural roughness baseline; final roughness in PS can vary with slope/height.
    o.roughness = 0.85f;
    return o;
}

struct PSOutput
{
    float4 color : SV_Target0;
    float4 normalRoughness : SV_Target1;
};

PSOutput TerrainPS(VSOutput input)
{
    PSOutput o;

    float3 n = normalize(input.worldNormal);
    float height = input.worldPos.y;
    float slope = saturate(1.0f - n.y);

    // Height bands for snow/rock/grass.
    float hNorm = saturate(height / max(g_TerrainParams0.x, 1.0f));
    float grassW = saturate(1.0f - smoothstep(0.25f, 0.55f, hNorm) - slope * 1.2f);
    float rockW  = saturate(smoothstep(0.15f, 0.45f, hNorm) + slope);
    float snowW  = saturate(smoothstep(0.65f, 0.9f, hNorm) * (1.0f - slope * 0.75f));
    float wSum = max(grassW + rockW + snowW, 1e-3f);
    grassW /= wSum; rockW /= wSum; snowW /= wSum;

    float3 grass = float3(0.18f, 0.32f, 0.14f);
    float3 rock  = float3(0.35f, 0.35f, 0.37f);
    float3 snow  = float3(0.85f, 0.88f, 0.92f);
    float3 albedo = grass * grassW + rock * rockW + snow * snowW;

    float roughness = lerp(0.75f, 0.98f, rockW) * (1.0f - 0.2f * snowW);

    // Directional light (sun) is light 0.
    float3 lightDir = normalize(g_Lights[0].direction_cosInner.xyz);
    float3 lightColor = g_Lights[0].color_range.rgb;
    float ndotl = saturate(dot(n, lightDir));

    float shadow = ComputeShadow(input.worldPos, n);

    float3 diffuse = albedo * ndotl * shadow;
    float3 ambient = albedo * g_AmbientColor.rgb;

    // Simple specular lobe (cheap Blinn-Phong, roughness-mapped).
    float3 viewDir = normalize(g_CameraPosition.xyz - input.worldPos);
    float3 halfV = normalize(lightDir + viewDir);
    float ndoth = saturate(dot(n, halfV));
    float specPower = lerp(16.0f, 128.0f, saturate(1.0f - roughness));
    float spec = pow(ndoth, specPower) * 0.04f * shadow;

    float3 color = diffuse * lightColor + ambient + spec * lightColor;
    o.color = float4(color, 1.0f);
    float3 nEnc = n * 0.5f + 0.5f;
    o.normalRoughness = float4(nEnc, roughness);
    return o;
}

// Depth-only vertex shader for the cascaded shadow map pass.
struct ShadowVSOut
{
    float4 position : SV_POSITION;
};

ShadowVSOut TerrainShadowVS(VSInput input)
{
    ShadowVSOut o;

    float3 worldPos = mul(g_ModelMatrix, float4(input.position, 1.0f)).xyz;
    float2 originHi = float2(g_TerrainParams1.z, g_TerrainParams1.w);
    float2 originLo = float2(g_TerrainParams2.x, g_TerrainParams2.y);
    float2 worldXZ = worldPos.xz + originHi + originLo;

    float h = TerrainHeight(worldXZ.x, worldXZ.y);
    if (input.texCoord.y > 0.5f)
    {
        h -= g_TerrainParams1.y;
    }
    worldPos.y = h;

    uint cascadeIndex = min(g_ShadowCascadeIndex.x, 5u);
    o.position = mul(g_LightViewProjection[cascadeIndex], float4(worldPos, 1.0f));
    return o;
}
