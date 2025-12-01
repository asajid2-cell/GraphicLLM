// Camera-only motion vector pass.
// Outputs per-pixel screen-space velocity in UV units (prevUV - currUV).

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
    // Directional + local light view-projection matrices (0-2 = cascades, 3-5 = local)
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

Texture2D g_Depth : register(t0);
SamplerState g_Sampler : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;

    float2 pos;
    if (vertexId == 0)      pos = float2(-1.0f, -1.0f);
    else if (vertexId == 1) pos = float2(-1.0f,  3.0f);
    else                    pos = float2( 3.0f, -1.0f);

    output.position = float4(pos, 0.0f, 1.0f);
    output.uv = float2(0.5f * pos.x + 0.5f, -0.5f * pos.y + 0.5f);
    return output;
}

float3 ReconstructWorldPosition(float2 uv, float depth)
{
    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;
    float4 clip = float4(x, y, depth, 1.0f);
    // Depth was written with the jittered view-projection matrix in the
    // main shading pass, so reconstruct world-space using the matching
    // jittered inverse view-projection. We later project into the
    // non-jittered current/previous view-projection matrices so the
    // resulting velocity encodes camera motion only (jitter is handled
    // separately in the TAA resolve).
    float4 world = mul(g_InvViewProjMatrix, clip);
    return world.xyz / max(world.w, 1e-4f);
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float depth = g_Depth.SampleLevel(g_Sampler, uv, 0).r;

    if (depth >= 1.0f - 1e-4f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float3 worldPos = ReconstructWorldPosition(uv, depth);

    // Motion vectors are computed in non-jittered clip space so they capture
    // camera/object motion only; jitter is handled separately during TAA
    // reprojection.
    float4 currClip = mul(g_ViewProjectionNoJitter, float4(worldPos, 1.0f));
    float4 prevClip = mul(g_PrevViewProjMatrix, float4(worldPos, 1.0f));

    if (currClip.w == 0.0f || prevClip.w == 0.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    float2 currNdc = currClip.xy / currClip.w;
    float2 prevNdc = prevClip.xy / prevClip.w;

    float2 currUV;
    currUV.x = currNdc.x * 0.5f + 0.5f;
    currUV.y = 0.5f - currNdc.y * 0.5f;

    float2 prevUV;
    prevUV.x = prevNdc.x * 0.5f + 0.5f;
    prevUV.y = 0.5f - prevNdc.y * 0.5f;

    float2 velocity = prevUV - currUV;
    return float4(velocity, 0.0f, 0.0f);
}
