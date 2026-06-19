// Ground-truth ambient occlusion compute shader.
// Writes encoded bent normal in RGB and diffuse AO in A.

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
};

Texture2D<float>  g_Depth : register(t0);
Texture2D<float4> g_NormalRoughness : register(t1);
RWTexture2D<float4> g_OutputAO : register(u0);
SamplerState g_Sampler : register(s0);

static const float PI = 3.14159265f;
static const int GTAO_SLICES = 4;
static const int GTAO_STEPS = 4;

float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

float3 ReconstructViewPos(float2 uv, float depth, float4x4 invProj)
{
    depth = min(saturate(depth), 1.0f - 1e-4f);
    float4 clip = float4(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f, depth, 1.0f);
    float4 view = mul(invProj, clip);
    return view.xyz / max(view.w, 1e-4f);
}

float3 DepthFallbackNormal(float2 uv, float2 texel, float3 centerView)
{
    float depthRight = g_Depth.SampleLevel(g_Sampler, uv + float2(texel.x, 0.0f), 0).r;
    float depthUp = g_Depth.SampleLevel(g_Sampler, uv - float2(0.0f, texel.y), 0).r;
    float3 posRight = ReconstructViewPos(uv + float2(texel.x, 0.0f), depthRight, g_InvProjectionMatrix);
    float3 posUp = ReconstructViewPos(uv - float2(0.0f, texel.y), depthUp, g_InvProjectionMatrix);
    float3 n = normalize(cross(posRight - centerView, posUp - centerView));
    return (all(isfinite(n)) && length(n) > 1e-4f) ? n : float3(0.0f, 0.0f, -1.0f);
}

float3 LoadViewNormal(float2 uv, float2 texel, float3 centerView)
{
    float3 normalWS = g_NormalRoughness.SampleLevel(g_Sampler, uv, 0).xyz * 2.0f - 1.0f;
    if (!all(isfinite(normalWS)) || length(normalWS) < 0.25f)
    {
        return DepthFallbackNormal(uv, texel, centerView);
    }

    float3 normalVS = normalize(mul((float3x3)g_ViewMatrix, normalize(normalWS)));
    if (!all(isfinite(normalVS)) || length(normalVS) < 1e-4f)
    {
        return DepthFallbackNormal(uv, texel, centerView);
    }
    return normalVS;
}

float3 ViewToWorldDir(float3 v)
{
    return normalize(mul(transpose((float3x3)g_ViewMatrix), v));
}

float4 PackGTAO(float ao, float3 bentNormalVS)
{
    float3 bentWS = ViewToWorldDir(bentNormalVS);
    if (!all(isfinite(bentWS)) || length(bentWS) < 1e-4f)
    {
        bentWS = float3(0.0f, 1.0f, 0.0f);
    }
    return float4(saturate(bentWS * 0.5f + 0.5f), saturate(ao));
}

float2 ProjectedRadius(float radius, float viewDepth)
{
    float2 projScale = float2(abs(g_ProjectionMatrix[0][0]), abs(g_ProjectionMatrix[1][1]));
    float2 uvRadius = radius * projScale / max(viewDepth, 0.25f) * 0.5f;
    float2 maxRadius = g_PostParams.xy * 96.0f;
    float2 minRadius = g_PostParams.xy * 2.0f;
    return clamp(uvRadius, minRadius, maxRadius);
}

void AccumulateHorizonSide(float2 uv,
                           float2 screenDir,
                           float3 tangentVS,
                           float sideSign,
                           float3 centerView,
                           float3 normalVS,
                           float radius,
                           float2 uvRadius,
                           float biasAngle,
                           inout float occlusion,
                           inout float3 bentAccum)
{
    float maxSinHorizon = sin(biasAngle);
    float maxWeight = 0.0f;

    [unroll]
    for (int stepIndex = 1; stepIndex <= GTAO_STEPS; ++stepIndex)
    {
        float step01 = ((float)stepIndex - 0.35f) / (float)GTAO_STEPS;
        float2 sampleUV = uv + screenDir * sideSign * uvRadius * step01;
        if (sampleUV.x <= 0.0f || sampleUV.x >= 1.0f || sampleUV.y <= 0.0f || sampleUV.y >= 1.0f)
        {
            continue;
        }

        float sampleDepth = g_Depth.SampleLevel(g_Sampler, sampleUV, 0).r;
        if (sampleDepth >= 1.0f - 1e-4f)
        {
            continue;
        }

        float3 sampleView = ReconstructViewPos(sampleUV, sampleDepth, g_InvProjectionMatrix);
        float3 delta = sampleView - centerView;
        float dist = length(delta);
        if (dist <= 1e-4f || dist > radius * 1.45f)
        {
            continue;
        }

        float3 sampleDir = delta / dist;
        float sinHorizon = dot(sampleDir, normalVS);
        float rangeWeight = saturate(1.0f - dist / max(radius * 1.45f, 1e-3f));
        rangeWeight *= rangeWeight;
        float weightedHorizon = lerp(sin(biasAngle), sinHorizon, rangeWeight);
        if (weightedHorizon > maxSinHorizon)
        {
            maxSinHorizon = weightedHorizon;
            maxWeight = rangeWeight;
        }
    }

    float sideOcc = saturate((maxSinHorizon - sin(biasAngle)) * 1.65f) * saturate(maxWeight * 1.35f);
    occlusion += sideOcc;
    bentAccum += (-sideSign * tangentVS) * sideOcc;
}

float4 ComputeGTAO(float2 uv, float2 pixel, float2 outputDim)
{
    if (g_AOParams.x <= 0.5f)
    {
        return float4(0.5f, 1.0f, 0.5f, 1.0f);
    }

    float depthCenter = g_Depth.SampleLevel(g_Sampler, uv, 0).r;
    if (depthCenter >= 1.0f - 1e-4f)
    {
        return float4(0.5f, 1.0f, 0.5f, 1.0f);
    }

    float2 texel = 1.0f / max(float2(outputDim), float2(1.0f, 1.0f));
    float3 centerView = ReconstructViewPos(uv, depthCenter, g_InvProjectionMatrix);
    if (!all(isfinite(centerView)))
    {
        return float4(0.5f, 1.0f, 0.5f, 1.0f);
    }

    float3 normalVS = LoadViewNormal(uv, texel, centerView);
    float baseRadius = max(g_AOParams.y, 0.01f);
    float bias = max(g_AOParams.z, 0.0f);
    float intensity = max(g_AOParams.w, 0.0f);
    float viewDepth = abs(centerView.z);
    float radius = baseRadius * lerp(0.45f, 1.0f, saturate(viewDepth / 8.0f));
    float2 uvRadius = ProjectedRadius(radius, viewDepth);
    float biasAngle = saturate(bias * 8.0f) * 0.35f;

    float3 up = (abs(normalVS.y) < 0.97f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent0 = normalize(cross(up, normalVS));
    float3 tangent1 = normalize(cross(normalVS, tangent0));
    float rotation = (InterleavedGradientNoise(pixel + g_TimeAndExposure.x) - 0.5f) * (PI / (float)GTAO_SLICES);

    float occlusion = 0.0f;
    float3 bentAccum = normalVS * (float)GTAO_SLICES * 2.0f;

    [unroll]
    for (int slice = 0; slice < GTAO_SLICES; ++slice)
    {
        float angle = rotation + ((float)slice + 0.5f) * (PI / (float)GTAO_SLICES);
        float s, c;
        sincos(angle, s, c);
        float2 screenDir = float2(c, s);
        float3 tangentVS = normalize(tangent0 * c + tangent1 * s);

        AccumulateHorizonSide(uv, screenDir, tangentVS, 1.0f, centerView, normalVS, radius, uvRadius, biasAngle, occlusion, bentAccum);
        AccumulateHorizonSide(uv, screenDir, tangentVS, -1.0f, centerView, normalVS, radius, uvRadius, biasAngle, occlusion, bentAccum);
    }

    float rawOcc = saturate(occlusion / ((float)GTAO_SLICES * 2.0f));
    rawOcc = smoothstep(0.02f, 0.78f, rawOcc);
    float ao = lerp(1.0f, 1.0f - rawOcc, saturate(intensity));
    ao = max(ao, 0.38f);

    float3 bentVS = normalize(bentAccum);
    if (!all(isfinite(bentVS)) || length(bentVS) < 1e-4f)
    {
        bentVS = normalVS;
    }
    bentVS = normalize(lerp(normalVS, bentVS, saturate(rawOcc * 1.8f)));
    return PackGTAO(ao, bentVS);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    uint2 outputDim;
    g_OutputAO.GetDimensions(outputDim.x, outputDim.y);
    if (dispatchThreadId.x >= outputDim.x || dispatchThreadId.y >= outputDim.y)
    {
        return;
    }

    float2 pixel = float2(dispatchThreadId.xy) + 0.5f;
    float2 uv = pixel / max(float2(outputDim), float2(1.0f, 1.0f));
    g_OutputAO[dispatchThreadId.xy] = ComputeGTAO(uv, pixel, float2(outputDim));
}
