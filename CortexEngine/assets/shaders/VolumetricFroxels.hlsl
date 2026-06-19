// View-aligned froxel volumetric lighting.

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
    float4   g_SSRParams;
    float4   g_PostGradeParams;
    float4   g_RTReflectionParams;
    uint4    g_ScreenAndCluster;
    uint4    g_ClusterParams;
    uint4    g_ClusterSRVIndices;
    float4   g_ProjectionParams;
    float4   g_CinematicParams;
    float4   g_CinematicDofParams;
    float4   g_CinematicStabilityParams;
    float4   g_CinematicLookParams;
    float4   g_CinematicExposureParams;
    float4   g_LocalProbeParams;
};

Texture2D<float> g_Depth : register(t0);
Texture3D<float4> g_PreviousVolume : register(t1);
Texture3D<float4> g_InjectedVolumeSRV : register(t2);
Texture3D<float4> g_IntegratedVolumeSRV : register(t3);
Texture2DArray<float> g_ShadowMap : register(t0, space1);

RWTexture3D<float4> g_InjectedVolume : register(u0);
RWTexture3D<float4> g_IntegratedVolume : register(u1);
RWTexture3D<float4> g_HistoryOut : register(u2);
RWTexture2D<float4> g_SceneColor : register(u3);

SamplerState g_Sampler : register(s0);

static const float PI = 3.14159265359f;

uint3 FroxelDims()
{
    uint w, h, d;
    g_InjectedVolume.GetDimensions(w, h, d);
    return uint3(w, h, d);
}

float Hash13(float3 p)
{
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 clip = float4(ndc, depth, 1.0f);
    float4 world = mul(g_InvViewProjMatrix, clip);
    return world.xyz / max(abs(world.w), 1e-6f);
}

float ViewZToLinear01(float viewZ)
{
    float nearZ = max(g_ProjectionParams.z, 0.01f);
    float farZ = max(g_ProjectionParams.w, nearZ + 1.0f);
    return saturate((viewZ - nearZ) / max(farZ - nearZ, 1e-3f));
}

float FroxelSliceToViewZ(float slice01)
{
    float nearZ = max(g_ProjectionParams.z, 0.03f);
    float farZ = min(max(g_ProjectionParams.w, nearZ + 4.0f), 85.0f);
    float k = 7.5f;
    return nearZ * (exp(slice01 * k) - 1.0f) / (exp(k) - 1.0f) + nearZ;
}

float ViewZToFroxelW(float viewZ)
{
    float nearZ = max(g_ProjectionParams.z, 0.03f);
    float farZ = min(max(g_ProjectionParams.w, nearZ + 4.0f), 85.0f);
    float k = 7.5f;
    float n = saturate((viewZ - nearZ) / max(farZ - nearZ, 1e-3f));
    return saturate(log(1.0f + n * (exp(k) - 1.0f)) / k);
}

float3 FroxelWorldPosition(uint3 id, uint3 dims, out float viewZ)
{
    float jitter = Hash13(float3(id.xy, (float)id.z + g_TimeAndExposure.x * 11.0f));
    float2 uv = (float2(id.xy) + 0.5f) / float2(dims.xy);
    float z01 = ((float)id.z + jitter) / (float)dims.z;
    viewZ = FroxelSliceToViewZ(z01);

    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    float4 view = mul(g_InvProjectionMatrix, float4(ndc, 1.0f, 1.0f));
    float3 rayVS = normalize(view.xyz / max(abs(view.w), 1e-6f));
    float3 pVS = rayVS * (viewZ / max(rayVS.z, 1e-4f));
    float4 pWS = mul(g_InvViewProjMatrix, mul(g_ProjectionMatrix, float4(pVS, 1.0f)));
    return pWS.xyz / max(abs(pWS.w), 1e-6f);
}

float PhaseHG(float cosTheta, float g)
{
    g = clamp(g, -0.80f, 0.82f);
    float gg = g * g;
    float denom = max(1.0f + gg - 2.0f * g * cosTheta, 1e-3f);
    return (1.0f - gg) / (4.0f * PI * denom * sqrt(denom));
}

float SamplePCF(float2 uv, float currentDepth, uint slice, float biasScale)
{
    if (any(uv < 0.0f.xx) || any(uv > 1.0f.xx)) return 1.0f;
    float radius = max(g_ShadowParams.y * 0.65f, 0.75f);
    float bias = max(g_ShadowParams.x * biasScale, 0.00025f);
    float lit = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            float2 o = float2(x, y) * radius / 2048.0f;
            float d = g_ShadowMap.SampleLevel(g_Sampler, float3(uv + o, slice), 0).r;
            lit += (currentDepth - bias <= d) ? 1.0f : 0.0f;
        }
    }
    return lit / 9.0f;
}

float SunVisibility(float3 worldPos, float viewZ)
{
    if (g_ShadowParams.z < 0.5f) return 1.0f;
    uint cascade = 0u;
    if (viewZ > g_CascadeSplits.x) cascade = 1u;
    if (viewZ > g_CascadeSplits.y) cascade = 2u;
    cascade = min(cascade, 2u);
    float4 clip = mul(g_LightViewProjection[cascade], float4(worldPos, 1.0f));
    float3 ndc = clip.xyz / max(abs(clip.w), 1e-6f);
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    return SamplePCF(uv, ndc.z, cascade, 1.8f + cascade * 0.65f);
}

float LocalShadowVisibility(float3 worldPos, Light light)
{
    if (g_ShadowParams.z < 0.5f || light.params.y < 0.5f) return 1.0f;
    uint slice = min((uint)(light.params.y + 0.5f), 5u);
    float4 clip = mul(g_LightViewProjection[slice], float4(worldPos, 1.0f));
    float3 ndc = clip.xyz / max(abs(clip.w), 1e-6f);
    float2 uv = ndc.xy * float2(0.5f, -0.5f) + 0.5f;
    return SamplePCF(uv, ndc.z, slice, 0.9f);
}

[numthreads(4, 4, 4)]
void InjectCS(uint3 id : SV_DispatchThreadID)
{
    uint3 dims = FroxelDims();
    if (any(id >= dims)) return;

    float viewZ = 0.0f;
    float3 worldPos = FroxelWorldPosition(id, dims, viewZ);
    float3 camPos = g_CameraPosition.xyz;
    float3 viewDir = normalize(worldPos - camPos);

    float authoredDensity = max(g_FogParams.x, 0.0f);
    float density = (g_FogParams.w > 0.5f) ? max(authoredDensity, 0.014f) : max(authoredDensity, 0.018f);
    float heightFactor = exp(-max(g_FogParams.z, 0.0f) * max(worldPos.y - g_FogParams.y, 0.0f));
    float nearFade = saturate((viewZ - 0.22f) / max(g_FogExtraParams.z, 0.12f));
    float densityScale = lerp(0.78f, 1.18f, saturate(g_ColorGrade.z));
    float mediumDensity = density * densityScale * heightFactor * nearFade;

    float anisotropy = clamp(max(g_FogExtraParams.x, 0.64f), -0.72f, 0.82f);
    float scatterStrength = max(g_FogExtraParams.y, 1.75f);
    float3 inscatter = max(g_AmbientColor.rgb, 0.0f.xxx) * 0.0015f;

    if (g_LightCount.x > 0u && (uint)g_Lights[0].position_type.w == 0u)
    {
        Light sun = g_Lights[0];
        float3 toSun = -normalize(sun.direction_cosInner.xyz);
        float phase = PhaseHG(dot(toSun, -viewDir), anisotropy);
        float visibility = SunVisibility(worldPos, viewZ);
        float shaftBoost = 6.5f + saturate(g_ColorGrade.z) * 4.5f;
        inscatter += max(sun.color_range.rgb, 0.0f.xxx) * phase * visibility * shaftBoost;
    }

    uint lightCount = min(g_LightCount.x, LIGHT_MAX);
    [loop]
    for (uint i = 1u; i < lightCount; ++i)
    {
        Light light = g_Lights[i];
        uint type = (uint)light.position_type.w;
        if (type != 1u && type != 2u && type != 3u) continue;

        float3 toLightVec = light.position_type.xyz - worldPos;
        float dist = max(length(toLightVec), 1e-3f);
        float range = max(light.color_range.w, 0.25f);
        float rangeAtten = saturate(1.0f - dist / range);
        rangeAtten *= rangeAtten;
        float3 toLight = toLightVec / dist;

        float spot = 1.0f;
        if (type == 2u)
        {
            float cd = dot(-toLight, normalize(light.direction_cosInner.xyz));
            spot = saturate((cd - light.params.x) / max(light.direction_cosInner.w - light.params.x, 1e-3f));
            spot *= spot;
        }
        float areaScale = (type == 3u) ? 0.55f : 1.0f;
        float visibility = LocalShadowVisibility(worldPos, light);
        float phase = PhaseHG(dot(toLight, -viewDir), anisotropy * 0.65f);
        inscatter += max(light.color_range.rgb, 0.0f.xxx) * phase * rangeAtten * spot * visibility *
                     areaScale / max(dist * dist, 0.35f) * 6.25f;
    }

    float jitter = Hash13(float3(id) + g_TimeAndExposure.xxx * 17.0f) - 0.5f;
    float blueNoise = 1.0f + jitter * 0.075f;
    float extinction = mediumDensity * (0.16f + scatterStrength * 0.055f);
    float3 scattering = inscatter * mediumDensity * scatterStrength * 1.35f * blueNoise;
    g_InjectedVolume[id] = float4(max(scattering, 0.0f.xxx), max(extinction, 0.0f));
}

[numthreads(8, 8, 1)]
void IntegrateCS(uint3 id : SV_DispatchThreadID)
{
    uint3 dims = FroxelDims();
    if (id.x >= dims.x || id.y >= dims.y) return;

    float3 accum = 0.0f.xxx;
    float transmittance = 1.0f;
    float prevZ = 0.0f;
    float historyAlpha = (g_TAAParams.w > 0.5f) ? 0.82f : 0.0f;

    [loop]
    for (uint z = 0u; z < dims.z; ++z)
    {
        uint3 p = uint3(id.xy, z);
        float viewZ = FroxelSliceToViewZ(((float)z + 0.5f) / (float)dims.z);
        float dz = max(viewZ - prevZ, 0.02f);
        prevZ = viewZ;

        float4 local = g_InjectedVolumeSRV.Load(int4(p, 0));
        float opticalDepth = saturate(local.a * dz);
        float segmentTrans = exp(-opticalDepth);
        float3 segmentScatter = local.rgb * (1.0f - segmentTrans) / max(local.a, 1e-4f);
        accum += transmittance * segmentScatter;
        transmittance *= segmentTrans;

        float3 uvw = (float3(p) + 0.5f) / float3(dims);
        float4 history = g_PreviousVolume.SampleLevel(g_Sampler, uvw, 0);
        float3 minV = min(accum, history.rgb) - 0.080f.xxx;
        float3 maxV = max(accum, history.rgb) + 0.080f.xxx;
        float3 clampedHistory = clamp(history.rgb, minV, maxV);
        float3 stable = lerp(accum, clampedHistory, historyAlpha);
        stable = min(stable, g_FogExtraParams.w.xxx);
        float4 outValue = float4(stable, saturate(1.0f - transmittance));
        g_IntegratedVolume[p] = outValue;
        g_HistoryOut[p] = outValue;
    }
}

[numthreads(8, 8, 1)]
void CompositeCS(uint3 id : SV_DispatchThreadID)
{
    uint w, h;
    g_SceneColor.GetDimensions(w, h);
    if (id.x >= w || id.y >= h) return;

    float2 uv = (float2(id.xy) + 0.5f) / float2(w, h);
    float depth = g_Depth.Load(int3(id.xy, 0));
    if (depth >= 1.0f - 1e-5f) return;

    float3 worldPos = ReconstructWorldPosition(uv, depth);
    float3 viewPos = mul(g_ViewMatrix, float4(worldPos, 1.0f)).xyz;
    float froxelZ = ViewZToFroxelW(max(viewPos.z, 0.0f));
    float4 volume = g_IntegratedVolumeSRV.SampleLevel(g_Sampler, float3(uv, froxelZ), 0);

    float4 scene = g_SceneColor[id.xy];
    float3 fogTint = max(g_AmbientColor.rgb, 0.0f.xxx) * 0.015f;
    float extinction = saturate(volume.a * 0.035f);
    float3 color = scene.rgb * (1.0f - extinction) + fogTint * extinction + volume.rgb * 32.0f;
    float maxLuma = max(g_FogExtraParams.w, 0.5f);
    float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    if (luma > maxLuma)
    {
        color *= maxLuma / max(luma, 1e-4f);
    }
    g_SceneColor[id.xy] = float4(max(color, 0.0f.xxx), scene.a);
}
