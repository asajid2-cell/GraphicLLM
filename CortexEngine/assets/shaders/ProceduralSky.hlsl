// ProceduralSky.hlsl
// Atmospheric scattering-based procedural sky for outdoor terrain rendering
// Renders when IBL is disabled to provide sun-driven outdoor lighting

cbuffer FrameConstants : register(b1) {
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_InvViewMatrix;
    float4x4 g_InvProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4x4 g_InvViewProjectionMatrix;
    float4x4 g_PrevViewProjectionMatrix;

    float4 g_CameraPosition;
    float4 g_SunDirection;      // xyz = direction TO the sun (normalized)
    float4 g_SunRadiance;       // rgb = sun color * intensity
    float4 g_Time;              // x = total time, y = delta time

    float4 g_AmbientColor;
    float4 g_FogParams;         // x = start, y = end, z = density, w = enabled
    float4   g_FogExtraParams;
    float4 g_FogColor;

    float4 g_ScreenParams;      // x = width, y = height, z = 1/width, w = 1/height
    float4 g_ShadowParams;
    float4 g_DebugMode;
};

struct VSOutput {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

// Fullscreen triangle vertex shader
VSOutput VSMain(uint vertexId : SV_VertexID) {
    VSOutput output;

    // Generate fullscreen triangle covering the entire screen
    float2 pos;
    if (vertexId == 0)      pos = float2(-1.0f, -1.0f);
    else if (vertexId == 1) pos = float2(-1.0f,  3.0f);
    else                    pos = float2( 3.0f, -1.0f);

    output.position = float4(pos, 0.0f, 1.0f);
    output.uv = float2(0.5f * pos.x + 0.5f, -0.5f * pos.y + 0.5f);

    return output;
}

// Attempt at physically-based atmospheric scattering
static const float3 RAYLEIGH_BETA = float3(5.5e-6f, 13.0e-6f, 22.4e-6f); // Scattering coefficients (makes sky blue)
static const float3 MIE_BETA = float3(21e-6f, 21e-6f, 21e-6f);           // Mie scattering (haze)
static const float MIE_G = 0.76f;                                         // Mie anisotropy (sun glow)
static const float EARTH_RADIUS = 6371000.0f;
static const float ATMOSPHERE_HEIGHT = 100000.0f;
static const float H_RAYLEIGH = 8000.0f;
static const float H_MIE = 1200.0f;

float RayleighPhase(float cosTheta) {
    return (3.0f / (16.0f * 3.14159265f)) * (1.0f + cosTheta * cosTheta);
}

float MiePhase(float cosTheta, float g) {
    float g2 = g * g;
    float denom = 1.0f + g2 - 2.0f * g * cosTheta;
    return (1.0f - g2) / (4.0f * 3.14159265f * pow(max(denom, 0.001f), 1.5f));
}

float Hash21(float2 p)
{
    p = frac(p * float2(127.1f, 311.7f));
    p += dot(p, p + 41.23f);
    return frac(p.x * p.y);
}

float ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1.0f, 0.0f));
    float c = Hash21(i + float2(0.0f, 1.0f));
    float d = Hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float FBM(float2 p)
{
    float value = 0.0f;
    float amp = 0.5f;
    [unroll]
    for (int i = 0; i < 4; ++i)
    {
        value += ValueNoise(p) * amp;
        p = p * 2.05f + 13.17f;
        amp *= 0.5f;
    }
    return value;
}

float CloudMask(float3 viewDir, float horizon, float up)
{
    float2 cloudUv = float2(viewDir.x * 2.3f + viewDir.z * 0.55f + g_Time.x * 0.0015f,
                            up * 1.85f + viewDir.z * 0.18f);
    float streaks = FBM(cloudUv * float2(1.25f, 0.55f));
    float wisps = FBM(cloudUv * float2(3.40f, 1.05f) + 23.0f);
    float layer = smoothstep(0.38f, 0.66f, streaks * 0.78f + wisps * 0.30f);
    return layer * saturate(up * 1.35f + 0.28f) * saturate(1.0f - horizon * 0.18f);
}

// Simplified atmospheric scattering for real-time rendering. This is tuned for
// local outdoor scenes rather than generic HDRI replacement: soft sun, low wet
// horizon haze, and darker below-horizon tones for water/shore captures.
float3 ComputeAtmosphericScattering(float3 viewDir, float3 sunDir) {
    float viewY = viewDir.y;
    float sunY = sunDir.y;
    float sunDot = saturate(dot(viewDir, sunDir));
    float horizon = pow(saturate(1.0f - abs(viewY) * 1.15f), 1.45f);
    float up = saturate(viewY);
    float down = saturate(-viewY);

    float3 sunColor = g_SunRadiance.rgb;
    float sunLum = max(dot(sunColor, float3(0.2126f, 0.7152f, 0.0722f)), 0.01f);
    sunColor = sunColor / sunLum;

    float lowSun = saturate(1.0f - sunY * 1.45f);
    float3 zenith = lerp(float3(0.18f, 0.31f, 0.47f),
                         float3(0.09f, 0.16f, 0.28f),
                         lowSun * 0.45f);
    float3 upperHaze = lerp(float3(0.47f, 0.57f, 0.63f),
                            float3(0.72f, 0.47f, 0.28f),
                            lowSun * 0.55f);
    float3 wetHorizon = lerp(float3(0.19f, 0.30f, 0.29f),
                             float3(0.42f, 0.27f, 0.16f),
                             lowSun * 0.50f);
    float3 belowHorizon = float3(0.035f, 0.075f, 0.068f);

    float3 skyColor = lerp(upperHaze, zenith, pow(up, 0.55f));
    skyColor = lerp(skyColor, wetHorizon, horizon * 0.80f);
    skyColor = lerp(skyColor, belowHorizon, pow(down, 0.65f));

    float opticalDepth = exp(-max(viewY, -0.12f) * 2.3f);
    float rayleighLift = saturate(up + 0.18f) * 0.035f;
    skyColor += RAYLEIGH_BETA * opticalDepth * 2600.0f * rayleighLift;

    float miePhase = MiePhase(sunDot, MIE_G);
    float sunDisk = smoothstep(0.99945f, 0.99982f, sunDot);
    float sunCore = pow(sunDot, 420.0f) * 1.35f;
    float sunHalo = pow(sunDot, 18.0f) * 0.55f;
    float broadGlow = pow(sunDot, 4.0f) * 0.18f;
    float atmosphereVisibility = saturate(sunY + 0.28f);
    skyColor += sunColor * (sunDisk * 4.0f + sunCore + sunHalo + broadGlow) * atmosphereVisibility;
    skyColor += sunColor * miePhase * MIE_BETA * opticalDepth * 7000.0f * atmosphereVisibility;

    float waterMist = horizon * saturate(0.75f - sunY * 0.25f);
    skyColor = lerp(skyColor, float3(0.55f, 0.61f, 0.55f), waterMist * 0.18f);

    float clouds = CloudMask(viewDir, horizon, up);
    float3 cloudColor = lerp(float3(0.50f, 0.56f, 0.52f),
                             float3(0.84f, 0.80f, 0.70f),
                             lowSun * 0.38f);
    skyColor = lerp(skyColor, cloudColor, clouds * 0.42f);

    float exposure = lerp(0.60f, 1.12f, saturate(sunY + 0.18f));
    return max(skyColor * exposure, 0.0f);
}

float4 PSMain(VSOutput input) : SV_TARGET {
    // Reconstruct world-space view direction from screen UV
    float2 uv = input.uv;
    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;

    // Unproject to view space
    float4 viewH = mul(g_InvProjectionMatrix, float4(x, y, 1.0f, 1.0f));
    float3 viewDir = normalize(viewH.xyz);

    // Transform to world space
    float3x3 viewRot = (float3x3)g_ViewMatrix;
    float3x3 invViewRot = transpose(viewRot);
    float3 worldDir = normalize(mul(invViewRot, viewDir));

    // Sun direction (pointing toward sun)
    float3 sunDir = normalize(g_SunDirection.xyz);

    // Compute atmospheric scattering
    float3 skyColor = ComputeAtmosphericScattering(worldDir, sunDir);

    return float4(skyColor, 1.0f);
}
