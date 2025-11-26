// Fullscreen post-process: exposure, ACES tonemapping, gamma, simple bloom stub

cbuffer FrameConstants : register(b1)
{
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4 g_CameraPosition;
    // x = time, y = deltaTime, z = exposure, w = bloom intensity
    float4 g_TimeAndExposure;
};

Texture2D g_SceneColor : register(t0);
SamplerState g_Sampler : register(s0);

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

VSOutput VSMain(uint vertexId : SV_VertexID)
{
    VSOutput output;

    // Fullscreen triangle in NDC
    float2 pos;
    if (vertexId == 0)      pos = float2(-1.0f, -1.0f);
    else if (vertexId == 1) pos = float2(-1.0f,  3.0f);
    else                    pos = float2( 3.0f, -1.0f);

    output.position = float4(pos, 0.0f, 1.0f);

    // Map NDC to UV (0..1), flipping Y
    output.uv = float2(0.5f * pos.x + 0.5f, -0.5f * pos.y + 0.5f);

    return output;
}

static const float PI = 3.14159265f;

float3 ApplyACESFilm(float3 x)
{
    // ACES fitted curve (Narkowicz 2015)
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 hdrColor = g_SceneColor.Sample(g_Sampler, input.uv).rgb;

    // Simple bloom approximation: boost very bright areas scaled by user intensity
    float3 bright = max(hdrColor - 1.0f, 0.0f);
    float bloomIntensity = max(g_TimeAndExposure.w, 0.0f);
    hdrColor += bright * bloomIntensity;

    float exposure = max(g_TimeAndExposure.z, 0.01f);
    float3 color = hdrColor * exposure;

    color = ApplyACESFilm(color);
    color = pow(color, 1.0f / 2.2f);

    return float4(saturate(color), 1.0f);
}
