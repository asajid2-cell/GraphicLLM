// Simple CPU-driven particle shader rendered as camera-facing billboards.
// Shares the same FrameConstants layout as Basic.hlsl / PostProcess.hlsl so
// it can reuse the main root signature and frame constant buffer.

cbuffer ObjectConstants : register(b0)
{
    float4x4 g_ModelMatrix;
    float4x4 g_NormalMatrix;
    float g_DepthBiasNdc;
    float3 _pad0;
};

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

struct VSInput
{
    // Base quad in local space (centered at origin, unit size)
    float3 position : POSITION;
    float2 uv       : TEXCOORD0;
    // Per-instance data
    float3 instancePos : TEXCOORD1;
    float  size        : TEXCOORD2;
    float4 color       : COLOR0;
};

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

PSInput VSMain(VSInput input)
{
    PSInput output;

    // Derive camera right/up vectors from the view matrix so particles face
    // the camera regardless of its orientation.
    float3 camRight = normalize(float3(g_ViewMatrix[0].x, g_ViewMatrix[1].x, g_ViewMatrix[2].x));
    float3 camUp    = normalize(float3(g_ViewMatrix[0].y, g_ViewMatrix[1].y, g_ViewMatrix[2].y));

    float halfSize = 0.5f * input.size;
    float3 offset = (input.position.x * camRight + input.position.y * camUp) * halfSize * 2.0f;

    float3 worldPos = input.instancePos + offset;
    float4 clipPos = mul(g_ViewProjectionMatrix, float4(worldPos, 1.0f));
    clipPos.z += g_DepthBiasNdc * clipPos.w;

    output.position = clipPos;
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float4 PSMain(PSInput input) : SV_TARGET
{
    // Simple radial falloff based on distance from quad center to soften the edges.
    float2 centered = input.uv * 2.0f - 1.0f;
    float  r2 = dot(centered, centered);
    float  alphaFalloff = saturate(1.0f - r2);

    float4 color = input.color;
    color.a *= alphaFalloff;

    // Optional debug mode to visualize particles only.
    if ((uint)g_DebugMode.x == 31u)
    {
        return color;
    }

    return color;
}

