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

// Simplified atmospheric scattering for real-time rendering
float3 ComputeAtmosphericScattering(float3 viewDir, float3 sunDir) {
    // View angle from horizon
    float viewAngle = viewDir.y;

    // Sun angle from horizon
    float sunAngle = sunDir.y;

    // Optical depth approximation (thicker atmosphere at horizon)
    float opticalDepthView = exp(-max(viewAngle, -0.1f) * 3.0f);
    float opticalDepthSun = exp(-max(sunAngle, -0.1f) * 2.0f);

    // Rayleigh scattering (blue sky)
    float3 rayleigh = RAYLEIGH_BETA * opticalDepthView * 40.0f;

    // Mie scattering (sun glow, haze)
    float cosTheta = dot(viewDir, sunDir);
    float miePhase = MiePhase(cosTheta, MIE_G);
    float3 mie = MIE_BETA * miePhase * opticalDepthView * 200.0f;

    // Sun disk
    float sunDisk = smoothstep(0.9997f, 0.9999f, cosTheta);

    // Soft sun glow
    float sunGlow = pow(saturate(cosTheta), 8.0f) * 0.5f;
    float sunHalo = pow(saturate(cosTheta), 64.0f) * 2.0f;

    // Sky color based on sun position
    float3 skyZenith = float3(0.15f, 0.3f, 0.6f);      // Deep blue at zenith
    float3 skyHorizon = float3(0.5f, 0.6f, 0.7f);     // Lighter at horizon

    // Sunset/sunrise colors
    float sunsetFactor = saturate(1.0f - abs(sunAngle) * 4.0f);
    float3 sunsetZenith = float3(0.2f, 0.15f, 0.3f);   // Purple-ish zenith at sunset
    float3 sunsetHorizon = float3(1.0f, 0.4f, 0.1f);   // Orange horizon at sunset

    skyZenith = lerp(skyZenith, sunsetZenith, sunsetFactor * 0.7f);
    skyHorizon = lerp(skyHorizon, sunsetHorizon, sunsetFactor);

    // Blend zenith to horizon based on view direction
    float horizonBlend = 1.0f - saturate(viewAngle + 0.1f);
    horizonBlend = pow(horizonBlend, 0.8f);
    float3 skyBase = lerp(skyZenith, skyHorizon, horizonBlend);

    // Apply scattering
    float3 scattered = rayleigh + mie;
    float3 skyColor = skyBase + scattered * saturate(sunAngle + 0.3f);

    // Sun contribution
    float3 sunColor = g_SunRadiance.rgb;
    float sunIntensity = dot(sunColor, float3(0.2126f, 0.7152f, 0.0722f));
    sunColor = normalize(sunColor + 0.001f) * min(sunIntensity, 10.0f);

    // Add sun disk and glow
    skyColor += sunColor * sunDisk * 10.0f;
    skyColor += sunColor * (sunGlow + sunHalo) * saturate(sunAngle + 0.2f);

    // Ground color (below horizon)
    float groundBlend = saturate(-viewAngle * 3.0f);
    float3 groundColor = float3(0.3f, 0.25f, 0.2f) * saturate(sunAngle + 0.3f) * 0.5f;
    skyColor = lerp(skyColor, groundColor, groundBlend);

    // Night sky (when sun is below horizon)
    float nightFactor = saturate(-sunAngle * 2.0f);
    float3 nightSky = float3(0.01f, 0.01f, 0.02f);
    skyColor = lerp(skyColor, nightSky, nightFactor * (1.0f - groundBlend));

    // Exposure/intensity based on sun
    float exposure = lerp(0.3f, 1.2f, saturate(sunAngle + 0.2f));
    skyColor *= exposure;

    return max(skyColor, 0.0f);
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
