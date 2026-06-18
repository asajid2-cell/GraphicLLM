// Fullscreen post-process: exposure, ACES tonemapping, gamma, simple bloom stub

#include "SurfaceClassification.hlsli"

// Frame constants must match ShaderTypes.h / Basic.hlsl exactly
cbuffer FrameConstants : register(b1)
{
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4x4 g_InvProjectionMatrix;
    float4   g_CameraPosition;
    // x = time, y = deltaTime, z = exposure, w = bloom intensity
    float4   g_TimeAndExposure;
    // rgb: ambient color * intensity, w unused
    float4   g_AmbientColor;
    uint4    g_LightCount;
    // Forward lights (light 0 is the sun). Must match ShaderTypes.h.
    struct Light
    {
        float4 position_type;        // xyz = position (for point/spot), w = type
        float4 direction_cosInner;   // xyz = direction, w = inner cone cos (spot)
        float4 color_range;          // rgb = color * intensity, w = range (point/spot)
        float4 params;               // x = outer cone cos, y = shadow index, z,w reserved
    };
    static const uint LIGHT_MAX = 16;
    Light    g_Lights[LIGHT_MAX];
    // Directional + local light view-projection matrices (0-2 = cascades, 3-5 = local)
    float4x4 g_LightViewProjection[6];
    // x,y,z = cascade split depths in view space, w = far plane
    float4   g_CascadeSplits;
    // x = depth bias, y = PCF radius in texels, z = shadows enabled (>0.5), w = PCSS enabled (>0.5)
    float4   g_ShadowParams;
    // x = debug view mode (matches ShaderTypes.h / Basic.hlsl; see comments there),
    //     w = RT history valid (>0.5), y/z reserved
    float4   g_DebugMode;
    // x = 1 / screenWidth, y = 1 / screenHeight,
    // z = FXAA enabled (>0.5),
    // w = RT sun shadows enabled (>0.5)
    float4   g_PostParams;
    // x = diffuse IBL intensity, y = specular IBL intensity,
    // z = IBL enabled (>0.5), w = environment index (0 = studio, 2 = night)
    float4   g_EnvParams;
    // x = warm tint (-1..1), y = cool tint (-1..1), z = god-ray intensity, w = vignette
    float4   g_ColorGrade;
    // x = fog density, y = base height, z = height falloff, w = fog enabled (>0.5)
    float4   g_FogParams;
    // x = anisotropy, y = scattering strength, z = near fade, w = max fog luma
    float4   g_FogExtraParams;
    // x = SSAO enabled (>0.5), y = radius, z = bias, w = intensity
    float4   g_AOParams;
    // x = bloom threshold, y = soft-knee factor, z = max bloom contribution,
    // w = post-process flags: low bits = SSR/RT/debug flags, bits 8-15 = lens dirt 0..255,
    // bit24 = V2 reflection resolver candidate beauty review toggle
    float4   g_BloomParams;
    // x,y = jitter delta in UV (prevJitter - currJitter),
    // z = TAA blend factor, w = TAA history valid (>0.5)
    float4   g_TAAParams;
    float4x4 g_ViewProjectionNoJitter;
    float4x4 g_InvViewProjectionNoJitter;
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
    float4   g_WaterParams0;
    float4   g_WaterParams1;
    // x = SSR max ray distance, y = SSR view-space thickness,
    // z = SSR composition strength, w = reserved
    float4   g_SSRParams;
    // x = contrast, y = saturation, z = motion blur, w = depth of field
    float4   g_PostGradeParams;
    // x = RT reflection roughness threshold, y = history max blend,
    // z = firefly clamp max luma, w = signal scale
    float4   g_RTReflectionParams;
    uint4    g_ScreenAndCluster;
    uint4    g_ClusterParams;
    uint4    g_ClusterSRVIndices;
    float4   g_ProjectionParams;
    // x = tone-mapper mode (0=ACES, 1=Reinhard, 2=soft filmic, 3=punchy)
    float4   g_CinematicParams;
    // x = authored DOF focus distance, y = authored aperture, z/w reserved
    float4   g_CinematicDofParams;
    // x = material/specular motion damping, y = reflection debug stability,
    // z = shadow softness scale, w = highlight/exposure protection
    float4   g_CinematicStabilityParams;
    // x = black/toe lift, y = highlight rolloff strength,
    // z = color separation strength, w = bloom halation strength
    float4   g_CinematicLookParams;
    // x = profile exposure trim, y = HDR shoulder start,
    // z = HDR shoulder strength, w = post-tonemap white compression
    float4   g_CinematicExposureParams;
    // x = scene-local probe diffuse scale, y = scene-local probe specular scale,
    // z = scene-local probe radiance enabled (>0.5), w = reserved
    float4   g_LocalProbeParams;
};

Texture2D g_SceneColor : register(t0);
Texture2D g_BloomSource : register(t1);
Texture2D g_SSAO : register(t2);
Texture2D g_HistoryColor : register(t3);
Texture2D g_Depth : register(t4);
Texture2D g_NormalRoughness : register(t5);
Texture2D g_SSRColor : register(t6);
Texture2D g_Velocity : register(t7);
// Optional RT reflection color buffer written by the DXR reflections pipeline.
// Used for hybrid SSR/RT reflections when ray tracing is enabled and the
// reflection pipeline is available, plus a simple history buffer for temporal
// accumulation/denoising.
Texture2D g_RTReflection : register(t8);
Texture2D g_RTReflectionHistory : register(t9);
Texture2D g_EmissiveMetallic : register(t10);
Texture2D g_MaterialExt1 : register(t11);
Texture2D g_MaterialExt2 : register(t12);
Texture2D g_LocalReflectionRadiance : register(t13);
// Shadow map array is accessed via a separate descriptor table (space1) so
// that t0-t5 in space0 can be used for post-process textures without aliasing.
Texture2DArray g_ShadowMap : register(t0, space1);
Texture2D g_EnvDiffuse : register(t1, space1);
Texture2D g_EnvSpecular : register(t2, space1);
SamplerState g_Sampler : register(s0);

// -----------------------------------------------------------------------------
// Minimal 3x5 pixel font for GPU-drawn labels in the settings overlay.
// Each glyph row encodes three bits (left..right) in the low bits of a uint.
// -----------------------------------------------------------------------------
static const uint GLYPH_A[5] = { 2, 5, 7, 5, 5 };
static const uint GLYPH_B[5] = { 6, 5, 6, 5, 6 };
static const uint GLYPH_C[5] = { 3, 4, 4, 4, 3 };
static const uint GLYPH_D[5] = { 6, 5, 5, 5, 6 };
static const uint GLYPH_E[5] = { 7, 4, 7, 4, 7 };
static const uint GLYPH_F[5] = { 7, 4, 7, 4, 4 };
static const uint GLYPH_G[5] = { 3, 4, 5, 5, 3 };
static const uint GLYPH_H[5] = { 5, 5, 7, 5, 5 };
static const uint GLYPH_I[5] = { 7, 2, 2, 2, 7 };
static const uint GLYPH_L[5] = { 4, 4, 4, 4, 7 };
static const uint GLYPH_M[5] = { 5, 7, 5, 5, 5 };
static const uint GLYPH_O[5] = { 7, 5, 5, 5, 7 };
static const uint GLYPH_P[5] = { 7, 5, 7, 4, 4 };
static const uint GLYPH_R[5] = { 7, 5, 7, 5, 5 };
static const uint GLYPH_S[5] = { 3, 4, 3, 1, 6 };
static const uint GLYPH_T[5] = { 7, 2, 2, 2, 2 };
static const uint GLYPH_X[5] = { 5, 5, 2, 5, 5 };

// Digits 0-9 (used for row indices).
static const uint GLYPH_0[5] = { 7, 5, 5, 5, 7 };
static const uint GLYPH_1[5] = { 2, 6, 2, 2, 7 };
static const uint GLYPH_2[5] = { 7, 1, 7, 4, 7 };
static const uint GLYPH_3[5] = { 7, 1, 7, 1, 7 };
static const uint GLYPH_4[5] = { 5, 5, 7, 1, 1 };
static const uint GLYPH_5[5] = { 7, 4, 7, 1, 7 };
static const uint GLYPH_6[5] = { 7, 4, 7, 5, 7 };
static const uint GLYPH_7[5] = { 7, 1, 2, 2, 2 };
static const uint GLYPH_8[5] = { 7, 5, 7, 5, 7 };
static const uint GLYPH_9[5] = { 7, 5, 7, 1, 7 };

// Simple enumeration for the glyph ids we use.
static const int GL_A = 0;
static const int GL_B = 1;
static const int GL_C = 2;
static const int GL_D = 3;
static const int GL_E_ = 4;
static const int GL_F_ = 5;
static const int GL_G_ = 6;
static const int GL_H_ = 7;
static const int GL_I_ = 8;
static const int GL_L_ = 9;
static const int GL_M_ = 10;
static const int GL_O_ = 11;
static const int GL_P_ = 12;
static const int GL_R_ = 13;
static const int GL_S_ = 14;
static const int GL_T_ = 15;
static const int GL_X_ = 16;
static const int GL_0_ = 17;
static const int GL_1_ = 18;
static const int GL_2_ = 19;
static const int GL_3_ = 20;
static const int GL_4_ = 21;
static const int GL_5_ = 22;
static const int GL_6_ = 23;
static const int GL_7_ = 24;
static const int GL_8_ = 25;
static const int GL_9_ = 26;

int DigitToGlyph(int d)
{
    d = clamp(d, 0, 9);
    switch (d)
    {
        case 0: return GL_0_;
        case 1: return GL_1_;
        case 2: return GL_2_;
        case 3: return GL_3_;
        case 4: return GL_4_;
        case 5: return GL_5_;
        case 6: return GL_6_;
        case 7: return GL_7_;
        case 8: return GL_8_;
        case 9: return GL_9_;
        default: return GL_0_;
    }
}

uint GetGlyphRowBits(int glyphId, int row)
{
    row = clamp(row, 0, 4);
    switch (glyphId)
    {
        case GL_A:  return GLYPH_A[row];
        case GL_B:  return GLYPH_B[row];
        case GL_C:  return GLYPH_C[row];
        case GL_D:  return GLYPH_D[row];
        case GL_E_: return GLYPH_E[row];
        case GL_F_: return GLYPH_F[row];
        case GL_G_: return GLYPH_G[row];
        case GL_H_: return GLYPH_H[row];
        case GL_I_: return GLYPH_I[row];
        case GL_L_: return GLYPH_L[row];
        case GL_M_: return GLYPH_M[row];
        case GL_O_: return GLYPH_O[row];
        case GL_P_: return GLYPH_P[row];
        case GL_R_: return GLYPH_R[row];
        case GL_S_: return GLYPH_S[row];
        case GL_T_: return GLYPH_T[row];
        case GL_X_: return GLYPH_X[row];
        case GL_0_: return GLYPH_0[row];
        case GL_1_: return GLYPH_1[row];
        case GL_2_: return GLYPH_2[row];
        case GL_3_: return GLYPH_3[row];
        case GL_4_: return GLYPH_4[row];
        case GL_5_: return GLYPH_5[row];
        case GL_6_: return GLYPH_6[row];
        case GL_7_: return GLYPH_7[row];
        case GL_8_: return GLYPH_8[row];
        case GL_9_: return GLYPH_9[row];
        default:    return 0;
    }
}

// Sample a single glyph in a [origin, origin+size] box in screen UV space.
float SampleGlyph(int glyphId, float2 uv, float2 origin, float2 size)
{
    float2 local = (uv - origin) / size;
    if (local.x < 0.0f || local.x >= 1.0f || local.y < 0.0f || local.y >= 1.0f)
    {
        return 0.0f;
    }

    const float gw = 3.0f;
    const float gh = 5.0f;

    float2 cell = float2(local.x * gw, (1.0f - local.y) * gh);
    int ix = (int)cell.x;
    int iy = (int)cell.y;

    if (ix < 0 || ix > 2 || iy < 0 || iy > 4)
    {
        return 0.0f;
    }

    uint rowBits = GetGlyphRowBits(glyphId, iy);
    uint mask = 1u << (2 - ix); // bit 2 = leftmost
    return (rowBits & mask) != 0 ? 1.0f : 0.0f;
}

// Render a compact row label into the overlay panel. To keep things easily
// readable with a very small 3x5 bitmap font, we draw a single strong
// letter per row (E,B,S,F,...) rather than trying to form full words. The
// HUD legend still shows the full text for each row.
float RenderRowLabel(int rowIndex, float2 uv, float2 origin, float2 size)
{
    int glyphId = -1;
    switch (rowIndex)
    {
        case 0: // Exposure
            glyphId = GL_E_;
            break;
        case 1: // Bloom
            glyphId = GL_B;
            break;
        case 2: // Shadows
            glyphId = GL_S_;
            break;
        case 3: // PCSS
            glyphId = GL_P_;
            break;
        case 4: // Bias
            glyphId = GL_B;
            break;
        case 5: // PCF radius
            glyphId = GL_P_;
            break;
        case 6: // Lambda
            glyphId = GL_L_;
            break;
        case 7: // FXAA
            glyphId = GL_F_;
            break;
        case 8: // TAA
            glyphId = GL_T_;
            break;
        case 9: // SSR
            glyphId = GL_S_;
            break;
        case 10: // SSAO
            glyphId = GL_A;
            break;
        case 11: // IBL
            glyphId = GL_I_;
            break;
        case 12: // Fog
            glyphId = GL_F_;
            break;
        case 13: // Speed
            glyphId = GL_S_;
            break;
        case 14: // RTX / RT
            glyphId = GL_R_;
            break;
        default:
            glyphId = DigitToGlyph(rowIndex % 10);
            break;
    }

    if (glyphId < 0)
    {
        return 0.0f;
    }

    // Center the single glyph inside the label rect.
    float2 glyphSize = size * float2(0.6f, 0.8f);
    float2 glyphOrigin = origin + 0.5f * (size - glyphSize);
    return SampleGlyph(glyphId, uv, glyphOrigin, glyphSize);
}

// -----------------------------------------------------------------------------
// SDF / CSG debug raymarcher
// -----------------------------------------------------------------------------

float sdSphere(float3 p, float r)
{
    return length(p) - r;
}

float sdBox(float3 p, float3 b)
{
    float3 q = abs(p) - b;
    return length(max(q, 0.0)) + min(max(q.x, max(q.y, q.z)), 0.0);
}

float sdTorus(float3 p, float2 t)
{
    float2 q = float2(length(p.xz) - t.x, p.y);
    return length(q) - t.y;
}

float opUnion(float d1, float d2)
{
    return min(d1, d2);
}

float opIntersection(float d1, float d2)
{
    return max(d1, d2);
}

float opSubtraction(float d1, float d2)
{
    return max(d1, -d2);
}

float mapSDF(float3 p)
{
    // Three simple primitives combined with CSG for debugging:
    //  - Sphere at origin
    //  - Box shifted to the right
    //  - Torus above the sphere, subtracted out
    float dSphere = sdSphere(p, 0.8f);
    float dBox    = sdBox(p - float3(1.2f, 0.0f, 0.0f), float3(0.6f, 0.6f, 0.6f));
    float dTorus  = sdTorus(p - float3(0.0f, 1.0f, 0.0f), float2(0.8f, 0.25f));

    float dUnion = opUnion(dSphere, dBox);
    float dCSG   = opSubtraction(dUnion, dTorus);
    return dCSG;
}

float3 normalSDF(float3 p)
{
    const float eps = 0.001f;
    float3 e = float3(1.0f, -1.0f, 0.0f) * eps;
    float nx = mapSDF(p + e.xyy) - mapSDF(p - e.xyy);
    float ny = mapSDF(p + e.yyx) - mapSDF(p - e.yyx);
    float nz = mapSDF(p + e.yxy) - mapSDF(p - e.yxy);
    return normalize(float3(nx, ny, nz));
}

float3 RenderSDFScene(float2 uv)
{
    // Reconstruct a simple camera ray from the inverse view-projection
    // matrix and the current pixel UV. We interpret the depth range as
    // [0,1] and pick two clip-space points along the ray.
    float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    float4 clipNear = float4(ndc, 0.0f, 1.0f);
    float4 clipFar  = float4(ndc, 1.0f, 1.0f);

    float4 worldNearH = mul(g_InvViewProjMatrix, clipNear);
    float4 worldFarH  = mul(g_InvViewProjMatrix, clipFar);

    float3 worldNear = worldNearH.xyz / worldNearH.w;
    float3 worldFar  = worldFarH.xyz / worldFarH.w;

    float3 ro = worldNear;
    float3 rd = normalize(worldFar - worldNear);

    // Raymarch
    const int   MAX_STEPS = 96;
    const float MAX_DIST  = 30.0f;
    const float SURF_EPS  = 0.0015f;

    float t = 0.0f;
    float dist = 0.0f;
    bool hit = false;

    [loop]
    for (int i = 0; i < MAX_STEPS; ++i)
    {
        float3 p = ro + rd * t;
        dist = mapSDF(p);
        if (dist < SURF_EPS)
        {
            hit = true;
            break;
        }
        t += dist;
        if (t > MAX_DIST)
        {
            break;
        }
    }

    if (!hit)
    {
        // Simple background: fade to dark based on view direction.
        float sky = saturate(0.5f + 0.5f * rd.y);
        return lerp(float3(0.02f, 0.02f, 0.04f), float3(0.2f, 0.25f, 0.35f), sky);
    }

    float3 pHit = ro + rd * t;
    float3 nHit = normalSDF(pHit);

    // Simple lighting: one white key light + ambient.
    float3 lightDir = normalize(-g_Lights[0].direction_cosInner.xyz);
    float3 lightColor = (g_LightCount.x > 0) ? g_Lights[0].color_range.rgb : float3(4.0f, 4.0f, 4.0f);

    float NdotL = saturate(dot(nHit, lightDir));
    float NdotV = saturate(dot(nHit, -rd));

    float3 albedo = float3(0.7f, 0.75f, 0.9f);

    // Basic Lambert + specular
    float3 diffuse = albedo * NdotL;

    float3 halfDir = normalize(lightDir - rd);
    float NdotH = saturate(dot(nHit, halfDir));
    float spec = pow(NdotH, 64.0f) * NdotL;

    float3 color = diffuse * lightColor * 0.5f + spec * lightColor;

    // Cheap rim light for readability
    float rim = pow(1.0f - NdotV, 2.0f);
    color += rim * float3(0.3f, 0.4f, 0.6f);

    return color;
}

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

float3 ApplyToneMapper(float3 color, uint mode)
{
    if (mode == 1u)
    {
        return saturate(color / (1.0f.xxx + color));
    }
    if (mode == 2u)
    {
        float3 soft = ApplyACESFilm(color * 0.88f);
        float3 shoulder = color / (1.0f.xxx + color);
        return saturate(lerp(soft, shoulder, 0.28f));
    }
    if (mode == 3u)
    {
        float3 aces = ApplyACESFilm(color * 1.08f);
        return saturate((aces - 0.5f.xxx) * 1.10f + 0.5f.xxx);
    }
    return ApplyACESFilm(color);
}

float3 ApplyPhotographicSplitTone(float3 color, float2 warmCool)
{
    float luma = dot(color, float3(0.299f, 0.587f, 0.114f));
    float shadowMask = 1.0f - smoothstep(0.18f, 0.62f, luma);
    float highlightMask = smoothstep(0.42f, 0.92f, luma);

    float warm = saturate(max(warmCool.x, 0.0f));
    float cool = saturate(max(warmCool.y, 0.0f));
    float antiWarm = saturate(max(-warmCool.x, 0.0f));
    float antiCool = saturate(max(-warmCool.y, 0.0f));

    float3 warmTint = lerp(1.0f.xxx, float3(1.065f, 1.018f, 0.935f), warm * (0.25f + 0.75f * highlightMask));
    float3 coolTint = lerp(1.0f.xxx, float3(0.940f, 0.985f, 1.085f), cool * (0.30f + 0.70f * shadowMask));
    float3 antiWarmTint = lerp(1.0f.xxx, float3(0.965f, 0.990f, 1.045f), antiWarm * (0.25f + 0.75f * highlightMask));
    float3 antiCoolTint = lerp(1.0f.xxx, float3(1.035f, 1.005f, 0.965f), antiCool * (0.30f + 0.70f * shadowMask));

    return color * warmTint * coolTint * antiWarmTint * antiCoolTint;
}

float3 ApplyCinematicToeLift(float3 color, float strength)
{
    strength = saturate(strength);
    float luma = dot(color, float3(0.299f, 0.587f, 0.114f));
    float shadow = 1.0f - smoothstep(0.10f, 0.55f, luma);
    float3 liftTint = float3(0.030f, 0.034f, 0.040f);
    return color + liftTint * shadow * strength;
}

float3 ApplyProfileColorSeparation(float3 color, float2 warmCool, float strength)
{
    strength = saturate(strength);
    float luma = dot(color, float3(0.299f, 0.587f, 0.114f));
    float shadowMask = 1.0f - smoothstep(0.20f, 0.64f, luma);
    float highlightMask = smoothstep(0.38f, 0.92f, luma);

    float warm = saturate(max(warmCool.x, 0.0f));
    float cool = saturate(max(warmCool.y, 0.0f));
    float3 shadowTint = lerp(float3(0.955f, 0.980f, 1.055f), float3(1.045f, 0.972f, 0.948f), warm);
    float3 highlightTint = lerp(float3(1.060f, 0.998f, 0.925f), float3(0.930f, 0.985f, 1.065f), cool);
    float3 tint = lerp(1.0f.xxx, shadowTint, shadowMask * strength);
    tint *= lerp(1.0f.xxx, highlightTint, highlightMask * strength * 0.80f);
    return color * tint;
}

float3 ApplyHighlightSaturationRollOff(float3 color, float strength)
{
    strength = saturate(strength);
    float luma = dot(color, float3(0.299f, 0.587f, 0.114f));
    float highlight = smoothstep(0.72f, 1.0f, luma);
    float3 gray = luma.xxx;
    color = lerp(color, gray, highlight * lerp(0.10f, 0.28f, strength));

    float shoulderStart = lerp(0.86f, 0.72f, strength);
    if (luma > shoulderStart)
    {
        float targetLuma = shoulderStart + (luma - shoulderStart) * lerp(0.84f, 0.52f, strength);
        color *= targetLuma / max(luma, 1e-4f);
    }
    return color;
}

float3 ApplyPostWhiteCompression(float3 color, float strength)
{
    strength = saturate(strength);
    if (strength <= 0.001f)
    {
        return color;
    }

    float luma = dot(color, float3(0.299f, 0.587f, 0.114f));
    float knee = lerp(0.92f, 0.76f, strength);
    if (luma <= knee)
    {
        return color;
    }

    float compressed = knee + (luma - knee) * lerp(0.86f, 0.42f, strength);
    return color * (compressed / max(luma, 1e-4f));
}

float3 ApplySceneLocalCinematicMidtoneCurve(float3 color, float strength)
{
    strength = saturate(strength);
    if (strength <= 0.001f)
    {
        return color;
    }

    const float3 lumaWeights = float3(0.299f, 0.587f, 0.114f);
    float luma = dot(color, lumaWeights);
    float midMask = smoothstep(0.14f, 0.42f, luma) * (1.0f - smoothstep(0.72f, 0.98f, luma));
    float toeMask = 1.0f - smoothstep(0.08f, 0.34f, luma);

    // A small photographic S-curve: lift the deepest floor slightly, then
    // shape midtones around the display midpoint. The luma rescale keeps hue
    // stable and avoids channel clipping.
    float lifted = luma + toeMask * strength * 0.018f;
    float contrast = lerp(1.0f, 1.13f, strength * midMask);
    float curved = saturate((lifted - 0.50f) * contrast + 0.50f);

    float scale = curved / max(luma, 1e-4f);
    return max(color * scale, 0.0f.xxx);
}

float3 ApplySceneLocalCinematicChromaPolish(float3 color,
                                            float2 warmCool,
                                            float strength,
                                            float highlightProtection)
{
    strength = saturate(strength);
    if (strength <= 0.001f)
    {
        return color;
    }

    const float3 lumaWeights = float3(0.299f, 0.587f, 0.114f);
    float luma = dot(color, lumaWeights);
    float midMask = smoothstep(0.16f, 0.50f, luma) * (1.0f - smoothstep(0.78f, 1.0f, luma));
    float shadowMask = 1.0f - smoothstep(0.12f, 0.42f, luma);
    float highlightMask = smoothstep(0.60f, 0.96f, luma);

    float warm = saturate(max(warmCool.x, 0.0f));
    float cool = saturate(max(warmCool.y, 0.0f));
    float3 shadowTone = lerp(float3(0.955f, 0.980f, 1.045f),
                             float3(1.035f, 0.975f, 0.940f),
                             warm);
    float3 highlightTone = lerp(float3(1.035f, 1.006f, 0.955f),
                                float3(0.940f, 0.990f, 1.055f),
                                cool);

    float3 toned = color;
    toned *= lerp(1.0f.xxx, shadowTone, shadowMask * strength * 0.24f);
    toned *= lerp(1.0f.xxx, highlightTone, highlightMask * strength * 0.18f);

    float3 gray = luma.xxx;
    float saturationLift = midMask * strength * 0.12f * (1.0f - highlightProtection * highlightMask);
    toned = lerp(gray, toned, 1.0f + saturationLift);

    // Preserve local luma so the polish reads as color craft, not exposure
    // drift. This is important for the existing white-ratio stability gates.
    float tonedLuma = max(dot(toned, lumaWeights), 1e-4f);
    toned *= luma / tonedLuma;
    return saturate(toned);
}

float3 ApplySceneLocalCinematicLookPolish(float3 color,
                                          float2 warmCool,
                                          float4 lookParams,
                                          float whiteCompression,
                                          float highlightProtection)
{
    float polishStrength = saturate(
        lookParams.x * 0.55f +
        lookParams.y * 0.45f +
        lookParams.z * 0.70f +
        lookParams.w * 0.35f +
        whiteCompression * 0.30f);
    if (polishStrength <= 0.001f)
    {
        return color;
    }

    float3 shaped = ApplySceneLocalCinematicMidtoneCurve(color, polishStrength);
    shaped = ApplySceneLocalCinematicChromaPolish(
        shaped,
        warmCool,
        polishStrength,
        saturate(highlightProtection));

    const float3 lumaWeights = float3(0.299f, 0.587f, 0.114f);
    float sourceLuma = max(dot(color, lumaWeights), 1e-4f);
    float shapedLuma = max(dot(shaped, lumaWeights), 1e-4f);
    float lumaCeiling = sourceLuma * lerp(1.02f, 1.10f, polishStrength);
    if (shapedLuma > lumaCeiling)
    {
        shaped *= lumaCeiling / shapedLuma;
    }

    return saturate(shaped);
}

// Downsample + bright-pass for bloom (runs at reduced resolution, sampling g_SceneColor)
float4 BloomDownsamplePS(VSOutput input) : SV_TARGET
{
    float3 hdr = g_SceneColor.Sample(g_Sampler, input.uv).rgb;

    float threshold = g_BloomParams.x;
    float softKnee  = g_BloomParams.y;

    // Soft-threshold bloom based on Unity-style formulation.
    float knee = threshold * softKnee + 1e-4f;
    float3 delta = max(hdr - threshold.xxx, 0.0f);
    float3 soft = delta * delta / (delta + knee);

    return float4(soft, 1.0f);
}

// Horizontal blur of the bloom texture (source bound at t0)
float4 BloomBlurHPS(VSOutput input) : SV_TARGET
{
    float2 texel = float2(g_PostParams.x * 4.0f, 0.0f); // quarter-res approximation
    float3 sum = 0.0f;
    float weights[6] = {0.183480f, 0.165472f, 0.121375f, 0.072411f, 0.035136f, 0.013866f};
    sum += g_SceneColor.Sample(g_Sampler, input.uv).rgb * weights[0];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel).rgb * weights[1];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel).rgb * weights[1];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel * 2.0f).rgb * weights[2];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel * 2.0f).rgb * weights[2];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel * 3.0f).rgb * weights[3];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel * 3.0f).rgb * weights[3];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel * 4.0f).rgb * weights[4];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel * 4.0f).rgb * weights[4];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel * 5.0f).rgb * weights[5];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel * 5.0f).rgb * weights[5];
    return float4(sum, 1.0f);
}

// Vertical blur of the bloom texture (source bound at t0)
float4 BloomBlurVPS(VSOutput input) : SV_TARGET
{
    float2 texel = float2(0.0f, g_PostParams.y * 4.0f); // quarter-res approximation
    float3 sum = 0.0f;
    float weights[6] = {0.183480f, 0.165472f, 0.121375f, 0.072411f, 0.035136f, 0.013866f};
    sum += g_SceneColor.Sample(g_Sampler, input.uv).rgb * weights[0];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel).rgb * weights[1];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel).rgb * weights[1];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel * 2.0f).rgb * weights[2];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel * 2.0f).rgb * weights[2];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel * 3.0f).rgb * weights[3];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel * 3.0f).rgb * weights[3];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel * 4.0f).rgb * weights[4];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel * 4.0f).rgb * weights[4];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel * 5.0f).rgb * weights[5];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel * 5.0f).rgb * weights[5];
    return float4(sum, 1.0f);
}

// Simple upsample/composite pass: reads from g_BloomSource and writes color
// directly. The pipeline uses additive blending when accumulating levels.
float4 BloomUpsamplePS(VSOutput input) : SV_TARGET
{
    float3 src = g_SceneColor.Sample(g_Sampler, input.uv).rgb;
    return float4(src, 1.0f);
}

// Reconstruct world-space position from depth and UV using the inverse of the
// current jittered view-projection matrix. This mirrors the mapping used in
// VSMain (NDC -> UV with Y flipped).
float3 ReconstructWorldPosition(float2 uv, float depth)
{
    // Convert UV back to clip space (matching VSMain's mapping).
    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;
    float4 clip = float4(x, y, depth, 1.0f);

    float4 world = mul(g_InvViewProjMatrix, clip);
    return world.xyz / max(world.w, 1e-4f);
}

float3 SanitizeHDRColor(float3 color)
{
    color.r = isfinite(color.r) ? color.r : 0.0f;
    color.g = isfinite(color.g) ? color.g : 0.0f;
    color.b = isfinite(color.b) ? color.b : 0.0f;
    return max(color, 0.0f);
}

float ReflectionLuma(float3 color)
{
    const float3 lumaWeights = float3(0.2126f, 0.7152f, 0.0722f);
    return dot(color, lumaWeights);
}

float3 SoftLimitReflectionLuma(float3 color, float maxLumaOverride)
{
    color = SanitizeHDRColor(color);

    const float maxLuma = clamp(maxLumaOverride, 4.0f, 32.0f);
    const float kneeLuma = max(1.0f, maxLuma * 0.25f);

    float luma = ReflectionLuma(color);
    if (!isfinite(luma) || luma <= 1e-5f || luma <= kneeLuma)
    {
        return color;
    }

    float over = luma - kneeLuma;
    float range = max(maxLuma - kneeLuma, 1e-3f);
    float limitedLuma = kneeLuma + range * (over / (over + range));
    return color * saturate(limitedLuma / max(luma, 1e-5f));
}

float Hash12(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float HazePhaseHG(float cosTheta, float anisotropy)
{
    float g = clamp(anisotropy, -0.75f, 0.75f);
    float gg = g * g;
    float denom = max(1.0f + gg - 2.0f * g * cosTheta, 1e-3f);
    return (1.0f - gg) / (4.0f * PI * denom * sqrt(denom));
}

float3 LimitHazeLuma(float3 color, float maxLuma)
{
    float luma = dot(color, float3(0.2126f, 0.7152f, 0.0722f));
    if (luma <= maxLuma || luma <= 1e-5f)
    {
        return color;
    }
    return color * (maxLuma / luma);
}

float3 ApplyLocalizedSingleScatterHaze(float3 hdrColor, float2 uv)
{
    float depth = g_Depth.Sample(g_Sampler, uv).r;
    if (depth >= 1.0f - 1e-4f)
    {
        return hdrColor;
    }

    float3 camPos = g_CameraPosition.xyz;
    float3 worldPos = ReconstructWorldPosition(uv, depth);
    float3 viewRay = worldPos - camPos;
    float rayLength = length(viewRay);
    if (rayLength <= 1e-3f)
    {
        return hdrColor;
    }

    float3 rayDir = viewRay / rayLength;
    float density = max(g_FogParams.x, 0.0f);
    float scatteringStrength = max(g_FogExtraParams.y, 0.0f);
    if (density <= 1e-5f || scatteringStrength <= 1e-5f)
    {
        return hdrColor;
    }

    float anisotropy = clamp(g_FogExtraParams.x, -0.65f, 0.65f);
    float nearFade = max(g_FogExtraParams.z, 0.05f);
    float maxFogLuma = clamp(g_FogExtraParams.w, 0.25f, 6.0f);
    float fogStart = 0.20f;
    float marchLength = max(rayLength - fogStart, 0.0f);
    if (marchLength <= 1e-3f)
    {
        return hdrColor;
    }

    const int kHazeSteps = 10;
    float stepLength = marchLength / (float)kHazeSteps;
    float jitter = Hash12(uv * float2(173.3f, 419.7f) + g_TimeAndExposure.xx);
    float3 scatter = 0.0f.xxx;
    float transmittance = 1.0f;
    float baseHeight = g_FogParams.y;
    float falloff = max(g_FogParams.z, 0.0f);
    float3 ambientTint = max(g_AmbientColor.rgb, 0.0f.xxx);

    [unroll]
    for (int i = 0; i < kHazeSteps; ++i)
    {
        float t = ((float)i + jitter) / (float)kHazeSteps;
        float dist = fogStart + t * marchLength;
        float3 p = camPos + rayDir * dist;

        float heightFactor = exp(-falloff * max(p.y - baseHeight, 0.0f));
        float localDensity = density * heightFactor * saturate((dist - fogStart) / nearFade);

        float3 inscatter = ambientTint * 0.045f;
        if (g_LightCount.x > 0)
        {
            Light sun = g_Lights[0];
            if ((uint)sun.position_type.w == 0)
            {
                float3 toLight = -normalize(sun.direction_cosInner.xyz);
                float sunPhase = HazePhaseHG(dot(toLight, -rayDir), anisotropy);
                float3 sunTint = max(sun.color_range.rgb, 0.0f.xxx);
                inscatter += sunTint * sunPhase * 1.35f;
            }
        }

        uint lightCount = min(g_LightCount.x, LIGHT_MAX);
        [unroll]
        for (uint li = 1u; li < LIGHT_MAX; ++li)
        {
            if (li >= lightCount)
            {
                break;
            }

            Light light = g_Lights[li];
            uint lightType = (uint)light.position_type.w;
            if (lightType != 1u && lightType != 2u && lightType != 3u)
            {
                continue;
            }

            float3 lightPos = light.position_type.xyz;
            float3 toLightVec = lightPos - p;
            float distToLight = max(length(toLightVec), 1e-3f);
            float range = max(light.color_range.w, 0.1f);
            float rangeFalloff = saturate(1.0f - distToLight / range);
            rangeFalloff *= rangeFalloff;

            float3 toLight = toLightVec / distToLight;
            float spotAtten = 1.0f;
            if (lightType == 2u)
            {
                float cd = dot(-toLight, normalize(light.direction_cosInner.xyz));
                float inner = light.direction_cosInner.w;
                float outer = light.params.x;
                spotAtten = saturate((cd - outer) / max(inner - outer, 1e-3f));
                spotAtten *= spotAtten;
            }

            float fixturePhase = HazePhaseHG(dot(toLight, -rayDir), anisotropy * 0.70f);
            float distAtten = 1.0f / max(distToLight * distToLight, 0.25f);
            float3 fixtureRadiance = max(light.color_range.rgb, 0.0f.xxx);
            inscatter += fixtureRadiance * fixturePhase * rangeFalloff * spotAtten * distAtten * 3.2f;
        }

        float segmentOpticalDepth = localDensity * stepLength;
        float segmentScatter = saturate(segmentOpticalDepth * scatteringStrength);
        scatter += transmittance * inscatter * segmentScatter;
        transmittance *= exp(-segmentOpticalDepth * 0.72f);
    }

    float depthLayer = saturate(marchLength / 7.5f);
    float extinction = 1.0f - exp(-density * marchLength * 0.38f);
    float3 coolWindowTint = float3(0.72f, 0.82f, 1.0f);
    float3 mediumTint = lerp(ambientTint, coolWindowTint, 0.35f);
    float3 softened = lerp(hdrColor, mediumTint * maxFogLuma * 0.16f, saturate(extinction * scatteringStrength * depthLayer));
    scatter = LimitHazeLuma(scatter, maxFogLuma);

    float sourceLuma = dot(hdrColor, float3(0.2126f, 0.7152f, 0.0722f));
    if (sourceLuma > maxFogLuma)
    {
        float knee = maxFogLuma * 0.58f;
        float compressed = knee + (sourceLuma - knee) * 0.45f;
        hdrColor *= compressed / max(sourceLuma, 1e-4f);
        softened = lerp(hdrColor, softened, 0.65f);
    }

    return min(softened + scatter, maxFogLuma.xxx);
}

float2 DirectionToLatLong(float3 dir)
{
    dir = normalize(dir);
    if (!all(isfinite(dir))) {
        dir = float3(0.0f, 0.0f, 1.0f);
    }

    float phi = atan2(-dir.z, dir.x);
    float theta = asin(clamp(dir.y, -1.0f, 1.0f));
    return float2(0.5f + phi / (2.0f * PI), 0.5f - theta / PI);
}

float3 RotateEnvironmentDirection(float3 dir)
{
    float s, c;
    sincos(g_CinematicParams.y, s, c);
    return normalize(float3(c * dir.x + s * dir.z, dir.y, -s * dir.x + c * dir.z));
}

float EnvReflectionFootprintMip(float2 uv, float width, float height, float maxMip)
{
    float2 dx = ddx(uv);
    float2 dy = ddy(uv);
    dx.x = frac(dx.x + 0.5f) - 0.5f;
    dy.x = frac(dy.x + 0.5f) - 0.5f;

    float2 texelDx = dx * float2(width, height);
    float2 texelDy = dy * float2(width, height);
    float footprint = max(length(texelDx), length(texelDy));
    return clamp(log2(max(footprint, 1.0f)), 0.0f, maxMip);
}

float StableIblMipRoughness(float roughness,
                            float metallic,
                            bool isMirror,
                            bool isGlass,
                            bool isWaterLike,
                            bool isBrushedMetal)
{
    if (isMirror) {
        return roughness;
    }
    if (isGlass || isWaterLike) {
        return max(roughness, 0.06f);
    }
    if (isBrushedMetal) {
        return max(roughness, 0.28f);
    }
    if (metallic > 0.85f) {
        return max(roughness, 0.24f);
    }
    return roughness;
}

float3 NormalizedPostKeyLightColor()
{
    float3 lightColor = (g_LightCount.x > 0) ? g_Lights[0].color_range.rgb : float3(1.0f, 1.0f, 1.0f);
    float lightLuma = max(dot(lightColor, float3(0.2126f, 0.7152f, 0.0722f)), 0.01f);
    return lightColor / lightLuma;
}

float3 PostKeyLightDirection()
{
    if (g_LightCount.x == 0) {
        return normalize(float3(0.35f, 0.85f, 0.25f));
    }
    return normalize(-g_Lights[0].direction_cosInner.xyz);
}

float3 ComputePostSceneLocalProbeSpecular(float3 reflectionDir,
                                          uint surfaceClass,
                                          uint sceneMaterialClass,
                                          float roughness)
{
    reflectionDir = normalize(reflectionDir);
    float3 ambientBase = max(g_AmbientColor.rgb, 0.018f.xxx);
    float3 keyColor = NormalizedPostKeyLightColor();
    float3 keyDir = PostKeyLightDirection();
    float up = saturate(reflectionDir.y * 0.5f + 0.5f);
    float front = saturate(
        dot(normalize(reflectionDir.xz + 1e-4f.xx), normalize(keyDir.xz + 1e-4f.xx)) * 0.5f + 0.5f);

    float3 roomLow = ambientBase * 0.70f + float3(0.040f, 0.036f, 0.032f);
    float3 roomHigh = ambientBase * 1.28f + keyColor * 0.035f;
    float3 local = lerp(roomLow, roomHigh, up);
    local += keyColor * pow(front, 12.0f) * lerp(0.010f, 0.055f, saturate(1.0f - roughness));

    if (sceneMaterialClass == SCENE_MATERIAL_EMISSIVE_NEON ||
        sceneMaterialClass == SCENE_MATERIAL_SCREEN_PANEL) {
        local += float3(0.030f, 0.090f, 0.150f) * saturate(1.0f - roughness * 0.55f);
    } else if (surfaceClass == SURFACE_CLASS_WATER ||
               surfaceClass == SURFACE_CLASS_GLASS ||
               surfaceClass == SURFACE_CLASS_MIRROR) {
        local *= 1.18f;
    }

    return max(local, 0.0f.xxx);
}

float3 SamplePostSceneLocalReflectionSource(float3 reflectionDir,
                                            uint surfaceClass,
                                            uint sceneMaterialClass,
                                            float roughness,
                                            float metallic,
                                            float fireflyClampLuma)
{
    reflectionDir = normalize(reflectionDir);
    float3 source = ComputePostSceneLocalProbeSpecular(
        reflectionDir, surfaceClass, sceneMaterialClass, roughness);

    if (g_EnvParams.z > 0.5f && g_EnvParams.y > 0.001f)
    {
        uint specWidth = 1u;
        uint specHeight = 1u;
        uint specMipCount = 1u;
        g_EnvSpecular.GetDimensions(0, specWidth, specHeight, specMipCount);
        float specMaxMip = max((specMipCount > 0u) ? (float)(specMipCount - 1u) : 0.0f, 0.0f);
        float2 specUV = DirectionToLatLong(RotateEnvironmentDirection(reflectionDir));
        float reflectionSafeMipFloor = saturate(g_AmbientColor.w) * specMaxMip;
        float footprintMip = EnvReflectionFootprintMip(specUV, (float)specWidth, (float)specHeight, specMaxMip);
        float roughnessMip = StableIblMipRoughness(
            roughness,
            metallic,
            SurfaceIsMirrorClass(surfaceClass) || sceneMaterialClass == SCENE_MATERIAL_MIRROR,
            surfaceClass == SURFACE_CLASS_GLASS || sceneMaterialClass == SCENE_MATERIAL_GLASS_PANE,
            SurfaceIsWater(surfaceClass) || sceneMaterialClass == SCENE_MATERIAL_WATER,
            surfaceClass == SURFACE_CLASS_BRUSHED_METAL || sceneMaterialClass == SCENE_MATERIAL_BRUSHED_METAL);
        float mipLevel = max(max(roughnessMip * specMaxMip, reflectionSafeMipFloor), footprintMip);
        source = g_EnvSpecular.SampleLevel(g_Sampler, specUV, mipLevel).rgb * max(g_EnvParams.y, 0.0f);
    }

    return SoftLimitReflectionLuma(source, fireflyClampLuma);
}

float3 ComputePostSceneLocalReflectionStructure(float3 reflectionDir,
                                                float3 worldPos,
                                                float3 normal,
                                                uint surfaceClass,
                                                uint sceneMaterialClass,
                                                float roughness,
                                                float metallic)
{
    reflectionDir = normalize(reflectionDir);
    normal = normalize(normal);

    float3 keyColor = NormalizedPostKeyLightColor();
    float3 keyDir = PostKeyLightDirection();
    float3 ambientBase = max(g_AmbientColor.rgb, 0.012f.xxx);

    const bool glassLike =
        surfaceClass == SURFACE_CLASS_GLASS ||
        sceneMaterialClass == SCENE_MATERIAL_GLASS_PANE ||
        surfaceClass == SURFACE_CLASS_MIRROR ||
        sceneMaterialClass == SCENE_MATERIAL_MIRROR;
    const bool waterLike =
        SurfaceIsWater(surfaceClass) ||
        sceneMaterialClass == SCENE_MATERIAL_WATER ||
        sceneMaterialClass == SCENE_MATERIAL_WET_SURFACE;
    const bool metalLike =
        surfaceClass == SURFACE_CLASS_BRUSHED_METAL ||
        sceneMaterialClass == SCENE_MATERIAL_BRUSHED_METAL ||
        sceneMaterialClass == SCENE_MATERIAL_POLISHED_METAL ||
        metallic > 0.62f;

    float gloss = saturate(1.0f - roughness);
    float horizon = 1.0f - abs(reflectionDir.y);
    float ceiling = saturate(reflectionDir.y * 0.5f + 0.5f);
    float floorBounce = saturate(-reflectionDir.y * 0.5f + 0.5f);
    float frontKey = saturate(
        dot(normalize(reflectionDir.xz + 1e-4f.xx), normalize(keyDir.xz + 1e-4f.xx)) * 0.5f + 0.5f);

    float broadWindow = pow(frontKey, lerp(8.0f, 22.0f, gloss));
    float horizonLine = pow(saturate(horizon), lerp(3.0f, 10.0f, gloss));
    float floorLine = pow(floorBounce, 5.0f) * horizonLine;

    // Stable, low-frequency world-space variation: enough structure for
    // glossy surfaces to read as reflecting a local room, but not screen-space
    // detail that jitters with mouse-look.
    float roomStripeA = 0.5f + 0.5f * sin(dot(worldPos.xz, float2(0.115f, 0.073f)) + g_CinematicParams.y * 0.03f);
    float roomStripeB = 0.5f + 0.5f * sin(worldPos.y * 0.170f + worldPos.x * 0.045f);
    float architecturalBreakup = lerp(roomStripeA, roomStripeA * roomStripeB, 0.35f);
    architecturalBreakup = smoothstep(0.18f, 0.92f, architecturalBreakup);

    float3 upperTint = ambientBase * 0.55f + keyColor * 0.050f;
    float3 lowerTint = ambientBase * 0.42f + float3(0.040f, 0.034f, 0.028f);
    if (glassLike || waterLike) {
        upperTint += float3(0.020f, 0.060f, 0.100f);
    }
    if (metalLike) {
        upperTint = lerp(upperTint, float3(0.090f, 0.100f, 0.115f), 0.35f);
        lowerTint = lerp(lowerTint, float3(0.050f, 0.045f, 0.040f), 0.35f);
    }

    float3 structured = lerp(lowerTint, upperTint, ceiling);
    structured += keyColor * broadWindow * lerp(0.035f, 0.180f, gloss);
    structured += lerp(float3(0.050f, 0.070f, 0.090f), keyColor, 0.35f) *
                  horizonLine *
                  lerp(0.014f, 0.080f, gloss) *
                  lerp(0.65f, 1.25f, architecturalBreakup);
    structured += float3(0.070f, 0.052f, 0.034f) *
                  floorLine *
                  lerp(0.015f, 0.070f, gloss);

    float normalFacing = saturate(abs(dot(normal, reflectionDir)) * 0.35f + 0.65f);
    float materialBoost = 1.0f;
    if (glassLike) {
        materialBoost = 1.16f;
    } else if (waterLike) {
        materialBoost = 1.24f;
    } else if (metalLike) {
        materialBoost = 1.10f;
    }

    return max(structured * normalFacing * materialBoost, 0.0f.xxx);
}

float3 ResolveV2SceneLocalReflectionRadiance(float3 reflectionDir,
                                             float3 worldPos,
                                             float3 normal,
                                             uint surfaceClass,
                                             uint sceneMaterialClass,
                                             float roughness,
                                             float metallic,
                                             float fireflyClampLuma)
{
    float3 ownedSource = SamplePostSceneLocalReflectionSource(
        reflectionDir,
        surfaceClass,
        sceneMaterialClass,
        roughness,
        metallic,
        fireflyClampLuma);

    float3 localStructure = ComputePostSceneLocalReflectionStructure(
        reflectionDir,
        worldPos,
        normal,
        surfaceClass,
        sceneMaterialClass,
        roughness,
        metallic);

    float gloss = saturate(1.0f - roughness);
    float localProbeActive = (g_LocalProbeParams.z > 0.5f) ? 1.0f : 0.0f;
    float enclosedBias = saturate(localProbeActive + (1.0f - g_EnvParams.z) * 0.5f);
    float structureWeight = saturate(enclosedBias * lerp(0.26f, 0.58f, gloss));
    if (sceneMaterialClass == SCENE_MATERIAL_WATER ||
        sceneMaterialClass == SCENE_MATERIAL_WET_SURFACE ||
        surfaceClass == SURFACE_CLASS_WATER) {
        structureWeight = max(structureWeight, 0.42f);
    }
    if (sceneMaterialClass == SCENE_MATERIAL_GLASS_PANE ||
        surfaceClass == SURFACE_CLASS_GLASS ||
        surfaceClass == SURFACE_CLASS_MIRROR) {
        structureWeight = max(structureWeight, 0.36f);
    }

    float3 resolved = lerp(ownedSource, max(ownedSource, localStructure), structureWeight);
    return SoftLimitReflectionLuma(resolved, fireflyClampLuma);
}

float3 SceneMaterialCinematicReflectionTint(uint sceneMaterialClass,
                                            uint surfaceClass,
                                            float roughness,
                                            float metallic)
{
    float3 tint = float3(1.0f, 1.0f, 1.0f);
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_CERAMIC_TILE:   tint = float3(0.965f, 1.010f, 1.035f); break;
        case SCENE_MATERIAL_POLISHED_WOOD:  tint = float3(1.070f, 1.020f, 0.940f); break;
        case SCENE_MATERIAL_BRUSHED_METAL:  tint = float3(0.975f, 0.995f, 1.030f); break;
        case SCENE_MATERIAL_POLISHED_METAL: tint = float3(0.990f, 0.998f, 1.018f); break;
        case SCENE_MATERIAL_GLASS_PANE:     tint = float3(0.925f, 1.020f, 1.085f); break;
        case SCENE_MATERIAL_WET_SURFACE:    tint = float3(0.945f, 1.018f, 1.055f); break;
        case SCENE_MATERIAL_WATER:          tint = float3(0.885f, 1.030f, 1.110f); break;
        case SCENE_MATERIAL_MIRROR:         tint = float3(1.000f, 1.000f, 1.000f); break;
        case SCENE_MATERIAL_SCREEN_PANEL:
        case SCENE_MATERIAL_EMISSIVE_NEON:  tint = float3(0.970f, 1.015f, 1.055f); break;
        default:
            if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL) {
                tint = float3(0.980f, 0.995f, 1.025f);
            } else if (surfaceClass == SURFACE_CLASS_WOOD) {
                tint = float3(1.055f, 1.018f, 0.950f);
            } else if (surfaceClass == SURFACE_CLASS_GLASS ||
                       surfaceClass == SURFACE_CLASS_WATER) {
                tint = float3(0.920f, 1.020f, 1.080f);
            }
            break;
    }

    const float gloss = saturate(1.0f - roughness);
    const float materialWeight = saturate(gloss * 0.65f + metallic * 0.35f);
    return lerp(1.0f.xxx, tint, materialWeight);
}

float3 ApplySceneMaterialCinematicReflectionGrade(float3 reflectionColor,
                                                  float3 baseColor,
                                                  uint sceneMaterialClass,
                                                  uint surfaceClass,
                                                  float roughness,
                                                  float metallic,
                                                  float materialReflectance,
                                                  float fireflyClampLuma)
{
    reflectionColor = SoftLimitReflectionLuma(reflectionColor, fireflyClampLuma);

    const float baseLuma = max(ReflectionLuma(baseColor), 1e-4f);
    const float reflectionLuma = ReflectionLuma(reflectionColor);
    if (reflectionLuma <= 1e-5f) {
        return reflectionColor;
    }

    float3 tint = SceneMaterialCinematicReflectionTint(
        sceneMaterialClass, surfaceClass, roughness, metallic);
    reflectionColor *= tint;

    // Keep post reflections scene-local and plausible: they can be brighter
    // than the lit surface on glossy materials, but should not become a black
    // replacement layer or a hot external-HDRI patch.
    const bool strongOwner =
        SurfaceIsMirrorClass(surfaceClass) ||
        SurfaceIsWater(surfaceClass) ||
        sceneMaterialClass == SCENE_MATERIAL_MIRROR ||
        sceneMaterialClass == SCENE_MATERIAL_WATER;
    const float gloss = saturate(1.0f - roughness);
    const float maxRelativeLuma = strongOwner
        ? lerp(2.4f, 5.0f, gloss)
        : lerp(1.12f, 2.15f, saturate(gloss * materialReflectance + metallic * 0.35f));
    const float minRelativeLuma = strongOwner
        ? 0.10f
        : lerp(0.42f, 0.18f, saturate(gloss + metallic));

    float gradedLuma = ReflectionLuma(reflectionColor);
    const float maxAllowed = max(baseLuma * maxRelativeLuma, 0.08f);
    if (gradedLuma > maxAllowed) {
        reflectionColor *= maxAllowed / max(gradedLuma, 1e-4f);
        gradedLuma = maxAllowed;
    }

    const float minAllowed = baseLuma * minRelativeLuma;
    if (gradedLuma < minAllowed && !strongOwner) {
        float lift = saturate((minAllowed - gradedLuma) / max(minAllowed, 1e-4f));
        reflectionColor = lerp(reflectionColor, baseColor, lift * 0.28f);
    }

    return SoftLimitReflectionLuma(reflectionColor, fireflyClampLuma);
}

float3 CompositeSceneMaterialCinematicReflection(float3 baseColor,
                                                 float3 reflectionColor,
                                                 float reflectionBlend,
                                                 uint sceneMaterialClass,
                                                 uint surfaceClass,
                                                 float roughness,
                                                 float metallic,
                                                 float materialReflectance,
                                                 float fireflyClampLuma)
{
    reflectionBlend = saturate(reflectionBlend);
    if (reflectionBlend <= 1e-5f) {
        return baseColor;
    }

    float3 gradedReflection = ApplySceneMaterialCinematicReflectionGrade(
        reflectionColor,
        baseColor,
        sceneMaterialClass,
        surfaceClass,
        roughness,
        metallic,
        materialReflectance,
        fireflyClampLuma);

    const bool strongOwner =
        SurfaceIsMirrorClass(surfaceClass) ||
        SurfaceIsWater(surfaceClass) ||
        sceneMaterialClass == SCENE_MATERIAL_MIRROR ||
        sceneMaterialClass == SCENE_MATERIAL_WATER;
    const float replacementWeight = strongOwner ? 0.92f : lerp(0.40f, 0.72f, saturate(1.0f - roughness));
    float3 replaceComposite = lerp(baseColor, gradedReflection, reflectionBlend * replacementWeight);

    // A small additive sheen preserves the base lighting and makes broad
    // polished materials read glossy without replacing them with the raw
    // screen/RT sample.
    float3 positiveReflection = max(gradedReflection - baseColor * 0.20f, 0.0f.xxx);
    float additiveStrength = reflectionBlend * (strongOwner ? 0.08f : 0.18f) * saturate(1.0f - roughness);
    float3 additiveComposite = replaceComposite + positiveReflection * additiveStrength;

    const float baseLuma = max(ReflectionLuma(baseColor), 1e-4f);
    float compositeLuma = ReflectionLuma(additiveComposite);
    float compositeCeiling = baseLuma * (strongOwner ? 5.2f : 2.35f);
    compositeCeiling = min(compositeCeiling, fireflyClampLuma);
    if (compositeLuma > compositeCeiling) {
        additiveComposite *= compositeCeiling / max(compositeLuma, 1e-4f);
    }

    return max(additiveComposite, 0.0f.xxx);
}

float SceneMaterialCinematicContactAoStrength(uint sceneMaterialClass,
                                              uint surfaceClass,
                                              float roughness,
                                              float metallic)
{
    if (surfaceClass == SURFACE_CLASS_EMISSIVE ||
        surfaceClass == SURFACE_CLASS_GLASS ||
        surfaceClass == SURFACE_CLASS_MIRROR ||
        surfaceClass == SURFACE_CLASS_WATER ||
        sceneMaterialClass == SCENE_MATERIAL_EMISSIVE_NEON ||
        sceneMaterialClass == SCENE_MATERIAL_SCREEN_PANEL ||
        sceneMaterialClass == SCENE_MATERIAL_GLASS_PANE ||
        sceneMaterialClass == SCENE_MATERIAL_MIRROR ||
        sceneMaterialClass == SCENE_MATERIAL_WATER) {
        return 0.0f;
    }

    float strength = 0.62f;
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_PAINTED_WALL:   strength = 0.82f; break;
        case SCENE_MATERIAL_CERAMIC_TILE:   strength = 0.70f; break;
        case SCENE_MATERIAL_POLISHED_WOOD:  strength = 0.64f; break;
        case SCENE_MATERIAL_BRUSHED_METAL:  strength = 0.36f; break;
        case SCENE_MATERIAL_POLISHED_METAL: strength = 0.22f; break;
        case SCENE_MATERIAL_FABRIC:         strength = 0.95f; break;
        case SCENE_MATERIAL_PLASTIC:        strength = 0.66f; break;
        case SCENE_MATERIAL_WET_SURFACE:    strength = 0.32f; break;
        case SCENE_MATERIAL_CONCRETE:       strength = 0.90f; break;
        case SCENE_MATERIAL_RUBBER:         strength = 0.96f; break;
        default:
            if (surfaceClass == SURFACE_CLASS_MASONRY) {
                strength = 0.88f;
            } else if (surfaceClass == SURFACE_CLASS_WOOD) {
                strength = 0.68f;
            } else if (surfaceClass == SURFACE_CLASS_BRUSHED_METAL) {
                strength = 0.36f;
            } else if (surfaceClass == SURFACE_CLASS_PLASTIC) {
                strength = 0.62f;
            }
            break;
    }

    float roughReceiver = lerp(0.55f, 1.10f, saturate(roughness));
    float conductorSuppression = lerp(1.0f, 0.42f, saturate(metallic));
    return saturate(strength * roughReceiver * conductorSuppression);
}

float3 SceneMaterialCinematicContactAoTint(uint sceneMaterialClass,
                                           uint surfaceClass)
{
    switch (sceneMaterialClass) {
        case SCENE_MATERIAL_CERAMIC_TILE:  return float3(0.955f, 0.975f, 1.015f);
        case SCENE_MATERIAL_POLISHED_WOOD: return float3(1.035f, 0.975f, 0.910f);
        case SCENE_MATERIAL_FABRIC:        return float3(0.975f, 0.960f, 0.940f);
        case SCENE_MATERIAL_CONCRETE:      return float3(0.955f, 0.960f, 0.955f);
        case SCENE_MATERIAL_RUBBER:        return float3(0.930f, 0.935f, 0.950f);
        case SCENE_MATERIAL_WET_SURFACE:   return float3(0.925f, 0.970f, 1.020f);
        default:
            if (surfaceClass == SURFACE_CLASS_WOOD) {
                return float3(1.030f, 0.975f, 0.915f);
            }
            if (surfaceClass == SURFACE_CLASS_MASONRY) {
                return float3(0.955f, 0.960f, 0.955f);
            }
            return float3(0.965f, 0.965f, 0.965f);
    }
}

float3 ApplySceneMaterialCinematicContactAo(float3 color,
                                            float ao,
                                            uint sceneMaterialClass,
                                            uint surfaceClass,
                                            float roughness,
                                            float metallic,
                                            float3 normal,
                                            float depthCenter)
{
    color = SanitizeHDRColor(color);
    ao = saturate(ao);

    const float validSceneDepth = 1.0f - smoothstep(0.985f, 1.0f, depthCenter);
    if (validSceneDepth <= 1e-4f) {
        return color;
    }

    const float baseAoAmount = saturate(g_AOParams.w * 0.48f);
    float3 grounded = color * lerp(1.0f, ao, baseAoAmount * validSceneDepth);

    const float cinematicActive =
        saturate(max(g_CinematicStabilityParams.z - 1.0f, 0.0f) * 2.5f +
                 g_CinematicStabilityParams.w * 3.0f);
    if (cinematicActive <= 1e-4f) {
        return grounded;
    }

    const float materialStrength = SceneMaterialCinematicContactAoStrength(
        sceneMaterialClass, surfaceClass, roughness, metallic);
    if (materialStrength <= 1e-4f) {
        return grounded;
    }

    const float occlusion = pow(saturate(1.0f - ao), 1.18f);
    const float floorLikeReceiver = lerp(0.88f, 1.08f, saturate(abs(normal.y)));
    const float highlightProtection = saturate(g_CinematicStabilityParams.w);
    const float luma = ReflectionLuma(grounded);
    const float protectedHighlight = lerp(
        1.0f,
        1.0f - smoothstep(0.72f, 1.28f, luma),
        highlightProtection);
    const float contact = saturate(
        occlusion *
        materialStrength *
        floorLikeReceiver *
        validSceneDepth *
        protectedHighlight *
        cinematicActive);

    float3 contactTint = SceneMaterialCinematicContactAoTint(sceneMaterialClass, surfaceClass);
    float3 tinted = grounded * lerp(1.0f.xxx, contactTint, contact * 0.18f);
    float contactDarken = 1.0f - contact * 0.42f;
    return max(tinted * contactDarken, 0.0f.xxx);
}

// ----------------------------------------------------------------------------
// HDR TAA resolve pass
// ----------------------------------------------------------------------------
float4 TAAResolvePS(VSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float3 curr = g_SceneColor.Sample(g_Sampler, uv).rgb;

    uint  debugView      = (uint)g_DebugMode.x;
    bool  isRtDebugView  = (debugView >= 18u && debugView <= 24u);
    bool  historyValid   = (g_TAAParams.w > 0.5f);
    float taaBlendBase   = (historyValid && !isRtDebugView) ? g_TAAParams.z : 0.0f;

    // No valid history yet or TAA disabled: pass through.
    if (taaBlendBase <= 0.0f)
    {
        if (debugView == 25u)
        {
            return float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        return float4(curr, 1.0f);
    }

    float2 texel = g_PostParams.xy;
    float2 vel = g_Velocity.Sample(g_Sampler, uv).xy;

    // Velocity is stored in UV units. Convert to pixel units for thresholds
    // so behaviour stays consistent across resolutions and distances.
    float2 safeTexel = max(texel, float2(1e-6f, 1e-6f));
    float2 velPx = vel / safeTexel;
    float  speedPx = length(velPx);

    // Disable TAA only for extreme motion. Normal mouse-look can produce large
    // screen-space velocities on static surfaces, and those pixels still
    // reproject correctly through the velocity/depth/normal checks below.
    if (speedPx >= 48.0f)
    {
        if (debugView == 25u)
        {
            return float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        return float4(curr, 1.0f);
    }

    // Center depth/normal for surface-aware neighbourhood selection.
    float  centerDepth = g_Depth.SampleLevel(g_Sampler, uv, 0).r;
    float4 centerNR    = g_NormalRoughness.SampleLevel(g_Sampler, uv, 0);
    float3 centerNormal = normalize(centerNR.xyz * 2.0f - 1.0f);
    float  surfaceRoughness = centerNR.w;

    // Keep the depth window very tight so silhouettes do not mix surfaces,
    // with a small relaxation in the far distance to account for depth-buffer
    // precision. A separate edge factor derived from depth variance further
    // suppresses history exactly at discontinuities.
    float depthThreshold = max(0.0008f, centerDepth * 0.0025f);
    const float normalThreshold = 0.9f; // ~25 degrees

    float3 cMin = curr;
    float3 cMax = curr;
    bool   anyNeighborAccepted = false;
    float  maxDepthDelta = 0.0f;

    // For a simple reactive mask we also track local luminance statistics
    // for accepted neighbours so we can identify very bright specular
    // highlights that diverge strongly from their surroundings.
    float  neighbourLumSum   = 0.0f;
    float  neighbourLumCount = 0.0f;
    const float3 lumaWeights = float3(0.299f, 0.587f, 0.114f);

    // Neighborhood clamp: build a min/max envelope around the current pixel,
    // but only from samples that are likely to belong to the same surface
    // (similar depth and normal). This prevents history from borrowing colors
    // across silhouettes or between the dragon and the floor.
    [unroll]
    for (int ny = -1; ny <= 1; ++ny)
    {
        [unroll]
        for (int nx = -1; nx <= 1; ++nx)
        {
            float2 offset   = float2(nx, ny) * texel;
            float2 sampleUV = saturate(uv + offset);

            float  sampleDepth = g_Depth.SampleLevel(g_Sampler, sampleUV, 0).r;
            float4 sampleNR    = g_NormalRoughness.SampleLevel(g_Sampler, sampleUV, 0);
            float3 sampleNormal = normalize(sampleNR.xyz * 2.0f - 1.0f);

            float depthDelta = abs(sampleDepth - centerDepth);
            maxDepthDelta = max(maxDepthDelta, depthDelta);
            bool depthOk  = (depthDelta < depthThreshold);
            bool normalOk = (dot(sampleNormal, centerNormal) > normalThreshold);

            if (depthOk && normalOk)
            {
                float3 cN = g_SceneColor.Sample(g_Sampler, sampleUV).rgb;
                cMin = min(cMin, cN);
                cMax = max(cMax, cN);
                anyNeighborAccepted = true;

                float lumN = dot(cN, lumaWeights);
                neighbourLumSum   += lumN;
                neighbourLumCount += 1.0f;
            }
        }
    }

    // If no neighbour passed the surface test, treat this pixel as a
    // disocclusion: the envelope collapses to the current color and history
    // cannot pull us away from it.
    if (!anyNeighborAccepted)
    {
        cMin = curr;
        cMax = curr;
    }

    // Motion vectors are computed in non-jittered space, so add the jitter
    // delta to align history with the current jittered projection.
    float2 historyUV = saturate(uv + vel + g_TAAParams.xy);
    float3 history   = g_HistoryColor.Sample(g_Sampler, historyUV).rgb;
    float3 historyClamped = clamp(history, cMin, cMax);
    float3 currClamped    = clamp(curr,    cMin, cMax);

    // Conservative history blending using three regimes per pixel:
    //   - Static: low speed, small color delta -> strong history.
    //   - Transitional: moderate motion or delta -> modest history.
    //   - Dynamic / disoccluded: large motion or delta -> history disabled.
    float3 diff = abs(currClamped - historyClamped);
    float  maxDiff = max(max(diff.r, diff.g), diff.b);

    // Roughness gating: glossy surfaces still need bounded temporal history
    // for sharp IBL/highlight stability. Reprojection, edge rejection, and
    // reactive masking below already remove history when a specular feature
    // diverges, so do not starve smooth metals/glass by default.
    float roughHistoryScale = lerp(0.45f, 1.0f, saturate(surfaceRoughness / 0.6f));

    float finalBlend = 0.0f;

    // Static: essentially locked pixels.
    if (speedPx < 0.75f && maxDiff < 0.03f)
    {
        finalBlend = taaBlendBase;
    }
    // Transitional: small motion or moderate color changes.
    else if (speedPx < 6.0f && maxDiff < 0.20f)
    {
        finalBlend = taaBlendBase * 0.45f;
    }
    // High-frequency but still somewhat stable (e.g., glossy highlights):
    else if (speedPx < 10.0f && maxDiff < 0.35f)
    {
        finalBlend = taaBlendBase * 0.35f;
    }
    // Camera-look motion on static scene geometry: keep a small amount of
    // history so IBL/specular detail does not collapse to single-frame shimmer.
    else if (speedPx < 32.0f && maxDiff < 0.45f)
    {
        finalBlend = taaBlendBase * 0.22f;
    }
    // Otherwise treat as dynamic / disoccluded and rely on the current
    // frame only; history stays in the clamp range but does not influence
    // the final color this frame.

    // At hard geometric edges (large depth variance in the 3x3 stencil) we
    // aggressively suppress history so silhouettes remain crisp instead of
    // blending foreground and background together.
    float edgeDepthMin = 0.0015f;
    float edgeDepthMax = 0.01f;
    float edgeFactor = saturate((maxDepthDelta - edgeDepthMin) / (edgeDepthMax - edgeDepthMin));

    // Hard disocclusion cutoff: for very large depth steps treat the pixel
    // as newly exposed and rely entirely on the current frame.
    if (maxDepthDelta > edgeDepthMax)
    {
        edgeFactor = 1.0f;
    }

    // Reactive mask: when a pixel is both much brighter than its local
    // neighbourhood and significantly different from history, and also
    // relatively glossy, we treat it as a highly dynamic specular feature
    // and clamp history very aggressively. This is particularly important
    // for the bright highlights on the dragon and floor.
    float currLum = dot(currClamped, lumaWeights);
    float avgNeighbourLum = (neighbourLumCount > 0.0f)
        ? (neighbourLumSum / neighbourLumCount)
        : currLum;

    float lumDeltaLocal  = max(currLum - avgNeighbourLum, 0.0f);
    float reactiveLocal  = saturate(lumDeltaLocal * 4.0f);

    float histLum        = dot(historyClamped, lumaWeights);
    float lumDeltaHist   = abs(currLum - histLum);
    float reactiveHist   = saturate(lumDeltaHist * 2.0f);

    float specFactor     = saturate(1.0f - surfaceRoughness);
    float reactiveMask   = saturate(max(reactiveLocal, reactiveHist) * specFactor);

    // Additional reprojection validation: if the reprojected sample lands on
    // a different surface in the current frame (depth/normal mismatch),
    // reject history entirely. This is a strong anti-ghosting measure.
    float historyDepth = g_Depth.SampleLevel(g_Sampler, historyUV, 0).r;
    float4 historyNR = g_NormalRoughness.SampleLevel(g_Sampler, historyUV, 0);
    float3 historyNormal = normalize(historyNR.xyz * 2.0f - 1.0f);

    float historyDepthDelta = abs(historyDepth - centerDepth);
    float historyNormalDot = dot(historyNormal, centerNormal);

    // Tighten the reprojection match near the camera where small errors
    // create very visible scaled ghosting.
    float nearFactor = saturate((0.12f - centerDepth) / 0.12f);
    float reprojDepthThreshold = lerp(depthThreshold * 2.5f, depthThreshold * 1.2f, nearFactor);

    bool reprojectionMismatch =
        (historyDepth >= 1.0f - 1e-4f) ||
        (historyDepthDelta > reprojDepthThreshold) ||
        (historyNormalDot < (normalThreshold - 0.05f));

    if (reprojectionMismatch)
    {
        finalBlend = 0.0f;
    }

    // In the TAA descriptor table, t12 carries the shared temporal rejection
    // mask instead of material ext2. x is accepted-history weight, y/z are
    // disocclusion/high-motion rejection debug channels, w is in-bounds.
    float4 sharedTemporalMask = g_MaterialExt2.SampleLevel(g_Sampler, uv, 0);
    finalBlend *= sharedTemporalMask.x * sharedTemporalMask.w;
    finalBlend *= roughHistoryScale * (1.0f - edgeFactor) * (1.0f - reactiveMask);

    // Clamp maximum history contribution per frame. Use a tighter cap on
    // glossy surfaces where specular reflections move non-linearly and
    // camera-only motion vectors cannot reproject perfectly.
    float roughnessClamp = lerp(0.08f, 0.25f, saturate(surfaceRoughness / 0.6f));
    finalBlend = min(finalBlend, roughnessClamp);

    finalBlend = saturate(finalBlend);

    float3 result = lerp(currClamped, historyClamped, finalBlend);

    if (debugView == 25u)
    {
        // TAA debug: visualize how much history is blended into the final
        // color. Bright pixels have strong temporal accumulation; dark
        // pixels lean towards the current frame.
        return float4(finalBlend.xxx, 1.0f);
    }
    if (debugView == 42u)
    {
        return float4(sharedTemporalMask.y, sharedTemporalMask.z, 1.0f - sharedTemporalMask.x, 1.0f);
    }

    return float4(result, 1.0f);
}

float4 SampleRtReflectionEdgeAware(float2 sampleUV,
                                   uint2 rtDim,
                                   uint2 depthDim,
                                   float centerDepth,
                                   float3 centerN)
{
    float2 rtDimF = float2(max(rtDim.x, 1u), max(rtDim.y, 1u));
    float2 coord = sampleUV * rtDimF - 0.5f;
    int2 base = (int2)floor(coord);

    int2 depthMax = int2(depthDim) - 1;

    float bestW = -1.0f;
    float4 best = float4(0.0f, 0.0f, 0.0f, 0.0f);

    // Pick the half-res sample that best matches the full-res surface.
    [unroll]
    for (int oy = 0; oy <= 1; ++oy)
    {
        [unroll]
        for (int ox = 0; ox <= 1; ++ox)
        {
            int2 p = base + int2(ox, oy);
            p.x = clamp(p.x, 0, (int)rtDim.x - 1);
            p.y = clamp(p.y, 0, (int)rtDim.y - 1);

            float4 s = g_RTReflection.Load(int3(p, 0));
            float  v = saturate(s.a);

            float2 uvP = (float2(p) + 0.5f) / rtDimF;
            int2 pix = clamp((int2)(uvP * float2(depthDim)), int2(0, 0), depthMax);

            float d = g_Depth.Load(int3(pix, 0));
            float3 n = normalize(g_NormalRoughness.Load(int3(pix, 0)).xyz * 2.0f - 1.0f);

            float depthScale = lerp(420.0f, 90.0f, saturate(centerDepth));
            float wDepth = saturate(1.0f - abs(d - centerDepth) * depthScale);
            float wNormal = saturate((dot(n, centerN) - 0.85f) / 0.15f);
            float w = max(v, 0.02f) * wDepth * wNormal;

            if (w > bestW)
            {
                bestW = w;
                best = s;
            }
        }
    }

    return best;
}

float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float LensDirtMask(float2 uv)
{
    float2 cell = floor(uv * float2(42.0f, 27.0f));
    float coarse = Hash21(cell);
    float speckle = smoothstep(0.82f, 1.0f, coarse);

    float2 fineCell = floor(uv * float2(160.0f, 100.0f));
    float fine = smoothstep(0.90f, 1.0f, Hash21(fineCell));

    float radial = saturate(1.0f - length(uv - 0.5f) * 1.35f);
    return saturate((speckle * 0.75f + fine * 0.25f) * radial);
}

float3 SampleHighlightStreakAxis(float2 uv, float2 axis, float radiusPixels, float strength)
{
    float2 safeAxis = axis;
    float axisLen = length(safeAxis);
    if (axisLen < 1e-4f)
    {
        safeAxis = float2(1.0f, 0.0f);
    }
    else
    {
        safeAxis /= axisLen;
    }

    float2 pixelStep = safeAxis * g_PostParams.xy;
    float3 accum = 0.0f;
    float weightSum = 0.0f;

    [unroll]
    for (int i = 1; i <= 9; ++i)
    {
        float t = (float)i * (1.0f / 9.0f);
        float radius = lerp(4.0f, radiusPixels, t);
        float weight = exp2(-t * 4.2f) * (1.0f - t * 0.28f);
        float2 delta = pixelStep * radius;

        float3 s0 = g_BloomSource.SampleLevel(g_Sampler, uv + delta, 0).rgb;
        float3 s1 = g_BloomSource.SampleLevel(g_Sampler, uv - delta, 0).rgb;
        float l0 = dot(s0, float3(0.2126f, 0.7152f, 0.0722f));
        float l1 = dot(s1, float3(0.2126f, 0.7152f, 0.0722f));
        float gate0 = smoothstep(0.015f, 0.35f, l0);
        float gate1 = smoothstep(0.015f, 0.35f, l1);

        accum += s0 * weight * gate0;
        accum += s1 * weight * gate1;
        weightSum += weight * (gate0 + gate1);
    }

    return (weightSum > 1e-4f) ? (accum / weightSum) * strength : 0.0f.xxx;
}

float3 SampleHighlightStreaks(float2 uv)
{
    float aspect = g_PostParams.y / max(g_PostParams.x, 1e-6f);
    float3 horizontal = SampleHighlightStreakAxis(uv, float2(1.0f, 0.0f), 240.0f, 1.05f);
    float3 vertical = SampleHighlightStreakAxis(uv, float2(0.0f, 1.0f), 48.0f, 0.035f);

    float3 sunAxis = 0.0f.xxx;
    if (g_LightCount.x > 0)
    {
        Light sun = g_Lights[0];
        uint sunType = (uint)sun.position_type.w;
        if (sunType == 0)
        {
            float3 sunDirWS = -normalize(sun.direction_cosInner.xyz);
            float3 sunWorld = g_CameraPosition.xyz + sunDirWS * 1000.0f;
            float4 sunClip = mul(g_ViewProjectionMatrix, float4(sunWorld, 1.0f));
            if (sunClip.w > 0.0f)
            {
                float2 sunNdc = sunClip.xy / sunClip.w;
                float2 sunUV = float2(sunNdc.x * 0.5f + 0.5f, 0.5f - sunNdc.y * 0.5f);
                if (sunUV.x > -0.25f && sunUV.x < 1.25f &&
                    sunUV.y > -0.25f && sunUV.y < 1.25f)
                {
                    float2 radial = uv - sunUV;
                    radial.x *= aspect;
                    sunAxis = SampleHighlightStreakAxis(uv, radial, 72.0f, 0.08f);
                }
            }
        }
    }

    return horizontal + vertical + sunAxis;
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float2 uv = input.uv;

    // Post-process feature flags are packed into g_BloomParams.w on the CPU.
    // Keep this as a small integer bitmask so the shader can gate optional
    // sampling even in SM5.1:
    //   bit0: SSR enabled
    //   bit1: RT reflections enabled
    //   bit2: RT reflection history valid
    //   bit3: disable RT reflection temporal (debug)
    //   bit4: visibility-buffer path active (HUD / debug)
    //   bits 5-7: RT reflection composition strength quantized to 0..7
    //   bits 8-15: lens dirt amount quantized to 0..255
    //   bits 16-23: RT reflection denoise alpha quantized to 0..255
    //   bit24: V2 reflection resolver candidate drives beauty (debug/review)
    uint postFxFlags = (uint)(g_BloomParams.w + 0.5f);
    bool ssrEnabled = ((postFxFlags & 1u) != 0u);
    bool rtReflEnabled = ((postFxFlags & 2u) != 0u);
    bool rtReflHistoryValid = ((postFxFlags & 4u) != 0u);
    bool rtReflTemporalOff = ((postFxFlags & 8u) != 0u);
    bool vbActive = ((postFxFlags & 16u) != 0u);
    bool v2ReflectionCandidateBeauty = ((postFxFlags & (1u << 24u)) != 0u);
    float rtReflectionCompositionStrength = (float)((postFxFlags >> 5u) & 7u) * (1.0f / 7.0f);
    float lensDirtAmount = (float)((postFxFlags >> 8u) & 255u) * (1.0f / 255.0f);
    float rtReflectionDenoiseAlpha = max((float)((postFxFlags >> 16u) & 255u) * (1.0f / 255.0f), 0.02f);
    float rtReflectionRoughnessThreshold = clamp(g_RTReflectionParams.x, 0.05f, 1.0f);
    float rtReflectionHistoryMaxBlend = clamp(g_RTReflectionParams.y, 0.0f, 0.5f);
    float rtReflectionFireflyClampLuma = clamp(g_RTReflectionParams.z, 4.0f, 32.0f);
    float rtReflectionSignalScale = clamp(g_RTReflectionParams.w, 0.0f, 2.0f);
    uint depthW, depthH;
    g_Depth.GetDimensions(depthW, depthH);
    uint2 depthDim = uint2(max(depthW, 1u), max(depthH, 1u));
    int2 depthMax = int2(depthDim) - 1;

    // SDF / CSG debug view renders a raymarched implicit scene instead of the
    // normal post-process chain so that SDF primitives can be inspected in
    // isolation using the current camera and light state.
    uint debugViewNow = (uint)g_DebugMode.x;
    if (debugViewNow == 24u && !rtReflEnabled)
    {
        float3 sdfColor = RenderSDFScene(uv);
        return float4(sdfColor, 1.0f);
    }

    // Base scene color + alpha as written by the main PBR pass. Alpha is a
    // post-process control channel from the opaque/G-buffer path; blended
    // overlays preserve the destination alpha because they do not publish
    // matching normal/material buffers.
    float4 sceneSample = g_SceneColor.Sample(g_Sampler, uv);
    float3 hdrColor = sceneSample.rgb;
    float  opacity = sceneSample.a;

    // G-buffer normal and roughness for the current pixel, shared between
    // refraction and reflection logic. The normal is encoded as 0..1 and
    // unpacked back to -1..1 here.
    float4 nrSample = g_NormalRoughness.Sample(g_Sampler, uv);
    float3 gbufNormal = normalize(nrSample.xyz * 2.0f - 1.0f);
    float  roughness = nrSample.w;
    float  metallic = saturate(g_EmissiveMetallic.Sample(g_Sampler, uv).a);
    float4 materialExt1 = g_MaterialExt1.Sample(g_Sampler, uv);
    float4 materialExt2 = g_MaterialExt2.Sample(g_Sampler, uv);
    float  transmission = saturate(materialExt1.a);
    uint   surfaceClass = DecodeSurfaceClass(materialExt2.r);
    uint   sceneMaterialClass = DecodeSceneMaterialClass(materialExt2.a);

    if (g_DebugMode.x == 1.0f)
    {
        return float4(gbufNormal * 0.5f + 0.5f, 1.0f);
    }
    if (g_DebugMode.x == 2.0f)
    {
        return float4(roughness.xxx, 1.0f);
    }
    if (g_DebugMode.x == 3.0f)
    {
        return float4(metallic.xxx, 1.0f);
    }
    if (g_DebugMode.x == 4.0f)
    {
        return float4(saturate(g_SceneColor.Sample(g_Sampler, uv).rgb), 1.0f);
    }
    if (g_DebugMode.x == 41.0f)
    {
        return float4(SurfaceClassDebugColor(surfaceClass), 1.0f);
    }
    if (g_DebugMode.x == 47.0f)
    {
        return float4(SceneMaterialPolicyDebugColor(sceneMaterialClass, surfaceClass, roughness, metallic), 1.0f);
    }

    // Screen-space refraction for thin transparent materials (glass, water).
    // We approximate refraction as a small UV offset in the scene color
    // buffer driven by the surface normal and opacity. This is not physically
    // exact but gives a convincing "bent background" look for glass bricks
    // and water without requiring a separate depth/scene-color prepass.
    if (SurfaceIsTransmissive(surfaceClass, transmission, opacity))
    {
        float3 N = gbufNormal;
        float2 dir = N.xy;
        float  lenDir = max(length(dir), 1e-3f);
        dir /= lenDir;

        // Stronger distortion for thinner / more transparent surfaces and
        // for glossier materials; very rough transparent objects behave more
        // like frosted glass so the refraction offset is smaller.
        float gloss = saturate(1.0f - roughness);
        float transparency = max(saturate(1.0f - opacity), transmission);

        // Base strength in "pixels" at the current resolution. Values are
        // deliberately modest so the effect reads without becoming noisy.
        const float kBaseRefractPixels = 6.0f;
        float refractPixels = kBaseRefractPixels * gloss * transparency;

        float2 texel = g_PostParams.xy; // 1 / width, 1 / height
        float2 offset = dir * refractPixels * texel;
        float2 refractUV = saturate(uv + offset);

        float3 refracted = g_SceneColor.Sample(g_Sampler, refractUV).rgb;

        // Blend refraction with the original shaded color so local lighting
        // (specular highlights on glass, etc.) is preserved while the
        // background appears distorted through the surface.
        float refractionWeight = lerp(0.35f, 0.75f, transparency);
        hdrColor = lerp(hdrColor, refracted, refractionWeight);
    }

    // Screen-space + RT reflection composite. The SSR buffer stores reflection
    // color in rgb and a confidence/coverage term in alpha; the optional RT
    // reflection buffer provides a fallback for regions where SSR is unreliable
    // (off-screen or failed rays). Both are applied in HDR space so they
    // participate in bloom and tonemapping.
    float4 ssrSample   = ssrEnabled ? g_SSRColor.Sample(g_Sampler, uv) : float4(0.0f, 0.0f, 0.0f, 0.0f);
    float  ssrWeightRaw = saturate(ssrSample.a);

    // Limit extremely bright SSR highlights to avoid harsh color pops when
    // the ray marches across very hot pixels in the environment or scene.
    float3 ssrColor = SoftLimitReflectionLuma(ssrSample.rgb, rtReflectionFireflyClampLuma);

    // Base reflection strength from roughness (shared by SSR and RT). This
    // roughly tracks the specular lobe so glossy surfaces get stronger
    // reflections while rough surfaces stay diffuse/IBL dominated.
    float gloss = saturate(1.0f - roughness);
    gloss *= gloss;
    float depthForFresnel = g_Depth.Sample(g_Sampler, uv).r;
    float3 worldForFresnel = ReconstructWorldPosition(uv, depthForFresnel);
    float3 viewForFresnel = normalize(g_CameraPosition.xyz - worldForFresnel);
    float nDotVForFresnel = saturate(dot(gbufNormal, viewForFresnel));
    float dielectricFresnel = 0.04f + 0.96f * pow(1.0f - nDotVForFresnel, 5.0f);
    float roughFresnelDamp = lerp(1.0f, 0.25f, roughness);
    float materialReflectance = lerp(saturate(dielectricFresnel * roughFresnelDamp), 1.0f, metallic);
    float reflectionStabilityScale =
        SceneMaterialReflectionStabilityScale(sceneMaterialClass, surfaceClass, roughness, metallic);
    float cinematicMotionDamping = saturate(g_CinematicStabilityParams.x);
    float glossyMotionDamp = lerp(1.0f,
                                  0.82f,
                                  cinematicMotionDamping * saturate(gloss * materialReflectance));
    reflectionStabilityScale *= glossyMotionDamp;

    // Scale SSR coverage into a soft weight; keep some headroom so the
    // underlying BRDF/IBL specular term can still contribute. Only allow
    // SSR to contribute on genuinely glossy surfaces with reasonably high
    // ray confidence to avoid large, low-frequency reflection overlays on
    // mid-roughness walls and floors.
    const float kMaxSSRWeight = 0.6f;
    float  wSSR = 0.0f;
    const bool isMirrorClass = SurfaceIsMirrorClass(surfaceClass);
    const bool isWaterClass = SurfaceIsWater(surfaceClass);
    const bool reflectionEligibleClass =
        isMirrorClass ||
        isWaterClass ||
        surfaceClass == SURFACE_CLASS_GLASS ||
        surfaceClass == SURFACE_CLASS_BRUSHED_METAL;
    const bool isPolishedConductor =
        SurfaceIsPolishedConductor(surfaceClass, metallic, roughness) ||
        (reflectionEligibleClass && metallic > 0.75f);
    // SSR is extremely fragile on near-perfect mirrors (roughness ~ 0) and
    // tends to self-intersect on convex glossy objects (chrome spheres),
    // producing the classic "inner copy" ghost. Prefer IBL/RT in that regime.
    if (!isMirrorClass && roughness > 0.08f && roughness < 0.28f && ssrWeightRaw > 0.4f)
    {
        float ssrConf = ssrWeightRaw * ssrWeightRaw;
        wSSR = ssrConf * kMaxSSRWeight * gloss * materialReflectance * reflectionStabilityScale;
    }

    // Optional RT reflection buffer: when RT is enabled (postParams.w > 0.5)
    // we blend between SSR (near-field, high-confidence) and RT (fallback for
    // low-SSR-confidence regions). Debug view 23 forces RT reflections off so
    // that SSR-only behaviour can be inspected without recompiling.
    float3 rtRefl = 0.0f;
    bool   rtEnabled = (rtReflEnabled && (uint)g_DebugMode.x != 23u);

    if (rtEnabled)
    {
        int2 centerPix = clamp((int2)(uv * float2(depthDim)), int2(0, 0), depthMax);
        float  centerDepth = g_Depth.Load(int3(centerPix, 0));
        float3 centerN = normalize(g_NormalRoughness.Load(int3(centerPix, 0)).xyz * 2.0f - 1.0f);

        uint rtWidth, rtHeight;
        g_RTReflection.GetDimensions(rtWidth, rtHeight);
        uint2 rtDim = uint2(max(rtWidth, 1u), max(rtHeight, 1u));
        float2 texel = 1.0f / float2(rtDim);

        float4 rtCenter4 = SampleRtReflectionEdgeAware(uv, rtDim, depthDim, centerDepth, centerN);
        rtRefl = rtCenter4.rgb;
        float  rtValid = saturate(rtCenter4.a);
        // Limit extreme RT reflection luma so very hot env texels do not
        // overpower the underlying BRDF/IBL term.
        rtRefl = SoftLimitReflectionLuma(rtRefl, rtReflectionFireflyClampLuma);

        // Small 5-tap cross filter in screen space over the RT reflection
        // buffer to reduce aliasing/fizzing from single-sample DXR. This is
        // followed by a simple temporal accumulation against a history
        // buffer when RT history has been populated on the CPU side.
        float  baseW = max(rtValid, 0.05f);
        float3 accum = rtRefl * baseW;
        float  total = baseW;

        float2 offsets[4] = {
            float2( texel.x,  0.0f),
            float2(-texel.x,  0.0f),
            float2( 0.0f,     texel.y),
            float2( 0.0f,    -texel.y)
        };

        [unroll]
        for (int i = 0; i < 4; ++i)
        {
            float2 sampleUV = saturate(uv + offsets[i]);
            float4 sampleRT4 = SampleRtReflectionEdgeAware(sampleUV, rtDim, depthDim, centerDepth, centerN);
            float3 sampleRT = SoftLimitReflectionLuma(sampleRT4.rgb, rtReflectionFireflyClampLuma);
            float  sampleValid = saturate(sampleRT4.a);

            // Bilateral weights: keep RT reflections from bleeding across depth/normal edges.
            int2 samplePix = clamp((int2)(sampleUV * float2(depthDim)), int2(0, 0), depthMax);
            float sampleDepth = g_Depth.Load(int3(samplePix, 0));
            float3 sampleN = normalize(g_NormalRoughness.Load(int3(samplePix, 0)).xyz * 2.0f - 1.0f);

            float depthScale = lerp(420.0f, 90.0f, saturate(centerDepth));
            float wDepth = saturate(1.0f - abs(sampleDepth - centerDepth) * depthScale);
            float wNormal = saturate((dot(sampleN, centerN) - 0.85f) / 0.15f);
            float w = max(sampleValid, 0.02f) * wDepth * wNormal;

            accum += sampleRT * w;
            total += w;
        }

        rtRefl = SoftLimitReflectionLuma(accum / max(total, 1e-4f), rtReflectionFireflyClampLuma);

        // If the RT reflection buffer has no meaningful signal, treat it as
        // unavailable so it does not pull reflections toward black (this can
        // look like "boxy" dark overlays when SSR confidence is low).
        float rtSignal = max(max(abs(rtRefl.r), abs(rtRefl.g)), abs(rtRefl.b));
        if (rtSignal < 1e-5f)
        {
            rtEnabled = false;
            rtRefl = 0.0f;
        }

        // Temporal accumulation using a simple history buffer updated once
        // per frame from the CPU. Only blend against history once the
        // reflection history has been seeded (avoid sampling undefined VRAM).
        if (rtReflHistoryValid && !rtReflTemporalOff)
        {
            // Reproject history using the same camera motion vectors used by TAA.
            float2 vel = g_Velocity.Sample(g_Sampler, uv).xy;
            float2 historyUV = saturate(uv + vel + g_TAAParams.xy);

            float4 rtHist4 = g_RTReflectionHistory.Sample(g_Sampler, historyUV);
            float3 rtHist = SoftLimitReflectionLuma(rtHist4.rgb, rtReflectionFireflyClampLuma);
            float  histValid = saturate(rtHist4.a);

            float3 diff = abs(rtRefl - rtHist);
            float maxDiffHist = max(max(diff.r, diff.g), diff.b);
            float currHistLuma = ReflectionLuma(rtRefl);
            float prevHistLuma = ReflectionLuma(rtHist);
            float lumaDelta = abs(currHistLuma - prevHistLuma);
            float lumaNorm = lumaDelta / max(max(currHistLuma, prevHistLuma), 1e-3f);

            // Reject history on mismatched surfaces (disocclusion).
            float historyDepth = g_Depth.SampleLevel(g_Sampler, historyUV, 0).r;
            float3 historyN = normalize(g_NormalRoughness.SampleLevel(g_Sampler, historyUV, 0).xyz * 2.0f - 1.0f);
            float depthOk = saturate(1.0f - abs(historyDepth - centerDepth) * 120.0f);
            float normalOk = saturate((dot(historyN, centerN) - 0.80f) / 0.20f);
            float reprojOk = depthOk * normalOk;

            // Motion-aware temporal weight: keep this conservative to avoid edge trails.
            float2 velPx = vel / max(g_PostParams.xy, float2(1e-6f, 1e-6f));
            float speedPx = length(velPx);
            float baseHist = lerp(0.25f, 0.05f, saturate(speedPx / 2.0f));
            baseHist *= saturate((1.0f - rtReflectionDenoiseAlpha) / 0.72f);
            float historyWeight = baseHist * histValid * reprojOk;
            historyWeight *= lerp(1.0f, 0.0f, saturate(maxDiffHist * 4.0f));
            historyWeight *= 1.0f - smoothstep(0.12f, 0.55f, lumaNorm);
            historyWeight *= 1.0f - smoothstep(0.15f, 1.0f, lumaDelta);
            historyWeight = min(historyWeight, rtReflectionHistoryMaxBlend);

            rtRefl = SoftLimitReflectionLuma(lerp(rtRefl, rtHist, historyWeight), rtReflectionFireflyClampLuma);
        }

        rtRefl = SoftLimitReflectionLuma(rtRefl * rtReflectionSignalScale, rtReflectionFireflyClampLuma);
    }

    // Prefer SSR whenever it is confident; let RT take over only when SSR
    // confidence is low so the total reflection energy stays stable and the
    // two lobes do not "fight" each other.
    float  effectiveSSRConfidence = (wSSR > 1e-5f) ? ssrWeightRaw : 0.0f;
    float  rawRTWeight = 1.0f - effectiveSSRConfidence;
    rawRTWeight *= rawRTWeight;
    float roughnessFadeStart = max(0.02f, rtReflectionRoughnessThreshold * 0.36f);
    float smoothSurface = isMirrorClass ? 1.0f : (1.0f - smoothstep(roughnessFadeStart, rtReflectionRoughnessThreshold, roughness));
    float rtGloss = gloss * smoothSurface;
    if (isWaterClass || isPolishedConductor) {
        rtGloss = max(rtGloss, saturate(1.0f - roughness) * 0.75f);
    }
    float  wRT = rtEnabled ? rawRTWeight *
                              rtGloss *
                              materialReflectance *
                              rtReflectionCompositionStrength *
                              reflectionStabilityScale : 0.0f;
    float localProbeSpecularPotential =
        (g_LocalProbeParams.z > 0.5f) ? max(g_LocalProbeParams.y, 0.0f) : 0.0f;
    float iblReflectionPotential = saturate(materialReflectance * gloss * g_EnvParams.y);
    float sceneLocalReflectionPotential =
        saturate(materialReflectance * gloss * localProbeSpecularPotential);
    float authorizedPrelitReflectionPotential =
        saturate(max(iblReflectionPotential, sceneLocalReflectionPotential));

    if (g_DebugMode.x == 46.0f)
    {
        // Reflection-owner debug:
        //   black  = no meaningful reflection owner for this pixel
        //   blue   = screen-space reflection owns the post reflection
        //   magenta= ray-traced reflection owns the post reflection
        //   yellow = image-based lighting / prelit scene color remains owner
        //   green  = scene-local neutral/local fallback owner
        //   gray   = sky/background/no scene depth
        float sceneDepth = g_Depth.Sample(g_Sampler, uv).r;
        if (sceneDepth >= 1.0f - 1e-4f)
        {
            return float4(0.18f, 0.18f, 0.18f, 1.0f);
        }

        float reflectionDebugStability = saturate(g_CinematicStabilityParams.y);
        float ownerStrength = saturate(max(wSSR, wRT) * 2.0f);
        ownerStrength *= lerp(1.0f, 0.82f, reflectionDebugStability);
        if (wSSR > 1e-5f && wSSR >= wRT)
        {
            return float4(0.05f, 0.32f + ownerStrength * 0.50f, 1.0f, 1.0f);
        }
        if (wRT > 1e-5f)
        {
            return float4(0.95f, 0.10f + ownerStrength * 0.30f, 0.95f, 1.0f);
        }

        float iblOwnerStrength = iblReflectionPotential;
        iblOwnerStrength *= lerp(1.0f, 0.82f, reflectionDebugStability);
        if (g_EnvParams.z > 0.5f && iblOwnerStrength > 0.015f)
        {
            return float4(1.0f, 0.78f + iblOwnerStrength * 0.20f, 0.05f, 1.0f);
        }
        float localOwnerStrength = sceneLocalReflectionPotential;
        localOwnerStrength *= lerp(1.0f, 0.82f, reflectionDebugStability);
        if (localOwnerStrength > 0.015f)
        {
            return float4(0.05f, 0.70f + localOwnerStrength * 0.20f, 0.22f, 1.0f);
        }
        if (reflectionEligibleClass || materialReflectance * gloss > 0.015f)
        {
            return float4(0.05f, 0.70f, 0.22f, 1.0f);
        }
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }
    if (g_DebugMode.x == 56.0f)
    {
        // Reflection-source resolver weights:
        //   R = SSR post-composite weight
        //   G = RT post-composite weight
        //   B = authorized scene-local or IBL/prelit reflection potential
        return float4(saturate(wSSR * 4.0f),
                      saturate(wRT * 4.0f),
                      authorizedPrelitReflectionPotential,
                      1.0f);
    }
    if (g_DebugMode.x == 57.0f)
    {
        // Reflection stability policy:
        //   R = material reflectance
        //   G = gloss
        //   B = scene/material stability scale
        return float4(saturate(materialReflectance),
                      saturate(gloss),
                      saturate(reflectionStabilityScale),
                      1.0f);
    }
    if (g_DebugMode.x == 60.0f)
    {
        // Reflection source authority:
        //   R = authorized external IBL/prelit source potential
        //   G = scene-local probe source potential
        //   B = screen/ray source potential
        return float4(saturate(iblReflectionPotential),
                      saturate(sceneLocalReflectionPotential),
                      saturate(max(wSSR, wRT) * 4.0f),
                      1.0f);
    }
    if (g_DebugMode.x == 61.0f)
    {
        // Local reflection radiance buffer proof view:
        //   RGB = resolved local reflection radiance
        //   A   = producer confidence/admission weight visualized as brightness
        float4 localRadiance = g_LocalReflectionRadiance.SampleLevel(g_Sampler, uv, 0);
        return float4(saturate(localRadiance.rgb + localRadiance.a.xxx * 0.25f), 1.0f);
    }

    float3 reflectionBaseColor = hdrColor;
    float3 currentReflectionCompositeColor = hdrColor;
    float3 candidateReflectionCompositeColor = hdrColor;

    float  weightSum = wSSR + wRT;
    if (weightSum > 1e-4f)
    {
        float invSum = 1.0f / weightSum;
        float3 reflHybrid = (ssrColor * wSSR + rtRefl * wRT) * invSum;

        float maxReflBlend = SurfaceReflectionCeiling(
            surfaceClass,
            roughness,
            metallic,
            transmission,
            dielectricFresnel);
        maxReflBlend *= reflectionStabilityScale;

        // Final lerp factor: surface roughness and total reflection weight
        // gate how strongly we move towards the hybrid reflection color.
        float roughBlendGate = pow(saturate(1.0f - roughness), 2.0f);
        float reflBlend = maxReflBlend * saturate(weightSum) * roughBlendGate;
        currentReflectionCompositeColor = CompositeSceneMaterialCinematicReflection(
            reflectionBaseColor,
            reflHybrid,
            reflBlend,
            sceneMaterialClass,
            surfaceClass,
            roughness,
            metallic,
            materialReflectance,
            rtReflectionFireflyClampLuma);
    }
    hdrColor = currentReflectionCompositeColor;

    // V2 candidate resolver: stricter SSR admission and smoother RT handoff.
    // This is debug/packet opt-in only; default beauty remains the current
    // resolver until cross-family stability evidence proves the candidate.
    float stableSSRConfidence = smoothstep(0.58f, 0.82f, ssrWeightRaw);
    stableSSRConfidence *= smoothstep(0.10f, 0.16f, roughness);
    stableSSRConfidence *= 1.0f - smoothstep(0.24f, 0.34f, roughness);
    float candidateWSSR = 0.0f;
    if (!isMirrorClass && !isWaterClass && stableSSRConfidence > 0.0f)
    {
        candidateWSSR = stableSSRConfidence *
                        stableSSRConfidence *
                        kMaxSSRWeight *
                        gloss *
                        materialReflectance *
                        reflectionStabilityScale;
    }
    float candidateRTConfidence = 1.0f - stableSSRConfidence;
    candidateRTConfidence *= candidateRTConfidence;
    if (isWaterClass || isPolishedConductor || isMirrorClass)
    {
        candidateRTConfidence = max(candidateRTConfidence, 0.72f);
    }
    float candidateWRT = rtEnabled ? candidateRTConfidence *
                                      rtGloss *
                                      materialReflectance *
                                      rtReflectionCompositionStrength *
                                      reflectionStabilityScale : 0.0f;
    float candidateWeightSum = candidateWSSR + candidateWRT;
    if (candidateWeightSum > 1e-4f)
    {
        float invCandidateSum = 1.0f / candidateWeightSum;
        float3 candidateHybrid = (ssrColor * candidateWSSR + rtRefl * candidateWRT) * invCandidateSum;
        float candidateMaxBlend = SurfaceReflectionCeiling(
            surfaceClass,
            roughness,
            metallic,
            transmission,
            dielectricFresnel);
        candidateMaxBlend *= reflectionStabilityScale;
        float candidateRoughBlendGate = pow(saturate(1.0f - roughness), 2.0f);
        float candidateBlend = candidateMaxBlend * saturate(candidateWeightSum) * candidateRoughBlendGate;
        candidateReflectionCompositeColor = CompositeSceneMaterialCinematicReflection(
            reflectionBaseColor,
            candidateHybrid,
            candidateBlend,
            sceneMaterialClass,
            surfaceClass,
            roughness,
            metallic,
            materialReflectance,
            rtReflectionFireflyClampLuma);
    }
    // Gate the V2 source sheen with the same owned-source potential reported by
    // debug view 56. The resolver below still chooses between authorized IBL and
    // scene-local radiance, then adds stable room-local structure only inside
    // the opt-in candidate path.
    float candidateLocalProbeWeight =
        saturate(authorizedPrelitReflectionPotential * 10.0f) *
        saturate(reflectionStabilityScale) *
        saturate(SurfaceReflectionCeiling(
            surfaceClass,
            roughness,
            metallic,
            transmission,
            dielectricFresnel) * 0.72f);
    candidateLocalProbeWeight *= lerp(1.0f, 0.52f, saturate(candidateWeightSum));
    if (candidateLocalProbeWeight > 1e-4f)
    {
        float3 fallbackLocalRadiance = ResolveV2SceneLocalReflectionRadiance(
            reflect(-viewForFresnel, gbufNormal),
            worldForFresnel,
            gbufNormal,
            surfaceClass,
            sceneMaterialClass,
            roughness,
            metallic,
            rtReflectionFireflyClampLuma);
        float4 producedLocalRadiance = g_LocalReflectionRadiance.SampleLevel(g_Sampler, uv, 0);
        float producedLocalConfidence = saturate(producedLocalRadiance.a);
        float3 producedLocalColor = SoftLimitReflectionLuma(
            max(producedLocalRadiance.rgb, 0.0f.xxx),
            rtReflectionFireflyClampLuma);
        float3 localProbeSheenColor = lerp(
            fallbackLocalRadiance,
            producedLocalColor,
            producedLocalConfidence);

        candidateReflectionCompositeColor = CompositeSceneMaterialCinematicReflection(
            candidateReflectionCompositeColor,
            localProbeSheenColor,
            candidateLocalProbeWeight,
            sceneMaterialClass,
            surfaceClass,
            roughness,
            metallic,
            materialReflectance,
            rtReflectionFireflyClampLuma);
    }
    if (g_DebugMode.x == 58.0f || v2ReflectionCandidateBeauty)
    {
        hdrColor = candidateReflectionCompositeColor;
    }
    if (g_DebugMode.x == 59.0f)
    {
        return float4(saturate(abs(candidateReflectionCompositeColor - currentReflectionCompositeColor) * 4.0f), 1.0f);
    }

    // Bloom: sample blurred bloom texture if available
    float bloomIntensity = max(g_TimeAndExposure.w, 0.0f);
    float3 bloom = 0.0f;
    float3 highlightStreaks = 0.0f;
    if (bloomIntensity > 0.001f) {
        bloom = g_BloomSource.Sample(g_Sampler, uv).rgb * bloomIntensity;
        float streakIntensity = max(bloomIntensity, sqrt(saturate(bloomIntensity)) * 0.65f);
        highlightStreaks = SampleHighlightStreaks(uv) * streakIntensity;

        // Clamp bloom contribution to avoid overly blown-out highlights.
        float maxBloom = max(g_BloomParams.z, 0.0f);
        if (maxBloom > 0.0f)
        {
            bloom = min(bloom, maxBloom.xxx);
            highlightStreaks = min(highlightStreaks, (maxBloom * 0.55f).xxx);
        }

        if (lensDirtAmount > 0.001f)
        {
            float dirt = LensDirtMask(uv);
            bloom += bloom * dirt * lensDirtAmount * 0.75f;
            highlightStreaks += highlightStreaks * dirt * lensDirtAmount * 0.45f;
        }
    }

    float bloomLuma = dot(bloom + highlightStreaks * 0.45f, float3(0.2126f, 0.7152f, 0.0722f));
    float lookHalation = saturate(g_CinematicLookParams.w);
    float halation = saturate(bloomLuma * lerp(0.06f, 0.16f, lookHalation)) * saturate(bloomIntensity);
    float warmIntent = saturate(max(g_ColorGrade.x, 0.0f));
    float coolIntent = saturate(max(g_ColorGrade.y, 0.0f));
    float3 halationTint = lerp(float3(1.0f, 0.50f, 0.24f),
                               float3(0.42f, 0.70f, 1.0f),
                               coolIntent * (1.0f - warmIntent));
    float3 halationColor = halationTint * halation * lerp(0.08f, 0.36f, lookHalation);

    // Start from base HDR lighting (without bloom); motion blur (when enabled)
    // operates on this term only so bloom and grading stay stable.
    float3 hdrBlurred = hdrColor;

    // Simple motion blur based on velocity buffer (camera-only) in HDR space.
    // This stays opt-in through cinematic post tuning so temporal validation
    // can keep a sharp default while showcase captures can add controlled smear.
    float motionBlurAmount = saturate(g_PostGradeParams.z);
    if (motionBlurAmount > 0.001f)
    {
        float2 vel = g_Velocity.Sample(g_Sampler, uv).xy;
        float  speed = length(vel);
        // Keep blur radius modest to avoid sampling across large portions of
        // the screen; high-speed motion will still get some streaking, but
        // we bias towards stability over extremely strong blur.
        float  blurStrength = saturate(speed * 4.0f) * motionBlurAmount;

        if (blurStrength > 0.001f)
        {
            float2 dir = vel / max(speed, 1e-4f);
            const int blurSamples = 5;
            float3 accum = hdrBlurred;
            float  total = 1.0f;
            float3 lumaWeights = float3(0.299f, 0.587f, 0.114f);
            float  centerLum = dot(hdrBlurred, lumaWeights);

            [unroll]
            for (int i = 1; i < blurSamples; ++i)
            {
                float t = (float)i / (float)(blurSamples - 1);
                float2 offset = dir * blurStrength * (t - 0.5f);
                float2 sampleUV = saturate(uv + offset);
                float3 sampleHdr = g_SceneColor.Sample(g_Sampler, sampleUV).rgb;
                // Down-weight samples whose luminance differs strongly from
                // the center; this reduces hue shifts when crossing very
                // bright or very dark edges.
                float sampleLum = dot(sampleHdr, lumaWeights);
                float lumDiff = abs(sampleLum - centerLum);
                float  w = saturate(1.0f - lumDiff * 0.25f);
                accum += sampleHdr * w;
                total += w;
            }

            hdrBlurred = accum / max(total, 1e-4f);
        }
    }

    // Lightweight depth of field. Authored focus/aperture controls override the
    // center auto-focus fallback used by older showcase profiles.
    float depthOfFieldAmount = saturate(g_PostGradeParams.w);
    if (depthOfFieldAmount > 0.001f)
    {
        float centerFocusDepth = g_Depth.SampleLevel(g_Sampler, float2(0.5f, 0.5f), 0).r;
        float pixelDepth = g_Depth.SampleLevel(g_Sampler, uv, 0).r;
        if (pixelDepth < 1.0f - 1e-4f)
        {
            float focusDistance = clamp(g_CinematicDofParams.x, 0.1f, 100.0f);
            if (focusDistance <= 0.11f)
            {
                float3 focusWorld = ReconstructWorldPosition(float2(0.5f, 0.5f), min(centerFocusDepth, 0.995f));
                focusDistance = length(focusWorld - g_CameraPosition.xyz);
                if (centerFocusDepth >= 1.0f - 1e-4f)
                {
                    focusDistance = 18.0f;
                }
            }

            float3 pixelWorld = ReconstructWorldPosition(uv, pixelDepth);
            float pixelDistance = length(pixelWorld - g_CameraPosition.xyz);
            float aperture = saturate(g_CinematicDofParams.y / 8.0f);
            float focusBand = max(focusDistance * lerp(0.45f, 0.14f, aperture), 0.35f);
            float coc = saturate(abs(pixelDistance - focusDistance) / focusBand) * depthOfFieldAmount;

            if (coc > 0.001f)
            {
                float2 texel = float2(g_PostParams.x, g_PostParams.y);
                float radius = lerp(1.0f, 4.0f, coc);
                float3 accum = hdrBlurred * 0.28f;
                accum += g_SceneColor.SampleLevel(g_Sampler, uv + texel * float2( radius,  0.0f), 0).rgb * 0.12f;
                accum += g_SceneColor.SampleLevel(g_Sampler, uv + texel * float2(-radius,  0.0f), 0).rgb * 0.12f;
                accum += g_SceneColor.SampleLevel(g_Sampler, uv + texel * float2( 0.0f,  radius), 0).rgb * 0.12f;
                accum += g_SceneColor.SampleLevel(g_Sampler, uv + texel * float2( 0.0f, -radius), 0).rgb * 0.12f;
                accum += g_SceneColor.SampleLevel(g_Sampler, uv + texel * float2( radius,  radius), 0).rgb * 0.08f;
                accum += g_SceneColor.SampleLevel(g_Sampler, uv + texel * float2(-radius,  radius), 0).rgb * 0.08f;
                accum += g_SceneColor.SampleLevel(g_Sampler, uv + texel * float2( radius, -radius), 0).rgb * 0.08f;
                accum += g_SceneColor.SampleLevel(g_Sampler, uv + texel * float2(-radius, -radius), 0).rgb * 0.08f;
                hdrBlurred = lerp(hdrBlurred, accum, coc);
            }
        }
    }

    // Localized single-scatter interior haze. Applied before bloom/tonemap so
    // bright windows and practical lamps create subtle air volume instead of
    // hard white rectangles.
    if (g_FogParams.w > 0.5f)
    {
        hdrBlurred = ApplyLocalizedSingleScatterHaze(hdrBlurred, uv);
    }

    // Underwater grading: when the camera is below the global water level,
    // bias colors toward a cool, desaturated palette. This is intentionally
    // lightweight and layered on top of the existing fog/tonemapping.
    {
        float waterLevelY = g_WaterParams0.w;
        bool isUnderwater = (g_CameraPosition.y < waterLevelY - 0.2f);
        if (isUnderwater)
        {
            // Approximate how deep we are below the surface and use that to
            // drive intensity; clamp to avoid over-darkening.
            float depthBelow = saturate((waterLevelY - g_CameraPosition.y) * 0.1f);

            // Shift towards blue-green, reduce contrast slightly.
            float3 underwaterTint = float3(0.0f, 0.4f, 0.6f);
            float3 tinted = lerp(hdrBlurred, underwaterTint, 0.25f * depthBelow);

            // Mild desaturation for a hazy underwater look.
            float3 lumaWeights = float3(0.299f, 0.587f, 0.114f);
            float  luma = dot(tinted, lumaWeights);
            float3 desat = lerp(tinted, luma.xxx, 0.2f * depthBelow);

            hdrBlurred = desat;
        }
    }

    // Sun god-rays (crepuscular rays) in HDR space. These are only applied
    // when fog is active so that beams have a plausible medium to scatter in.
    if (g_FogParams.w > 0.5f && g_LightCount.x > 0)
    {
        // Treat the first directional light as the sun.
        Light sun = g_Lights[0];
        uint sunType = (uint)sun.position_type.w;
        if (sunType == 0) // LIGHT_TYPE_DIRECTIONAL
        {
            float3 lightDirWS = normalize(sun.direction_cosInner.xyz);
            // Direction from camera towards the sun (opposite of light direction).
            float3 sunDirWS = -lightDirWS;

            float3 camPos = g_CameraPosition.xyz;
            float3 sunWorld = camPos + sunDirWS * 1000.0f;

            float4 sunClip = mul(g_ViewProjectionMatrix, float4(sunWorld, 1.0f));
            if (sunClip.w > 0.0f)
            {
                float2 sunNdc = sunClip.xy / sunClip.w;
                float2 sunUV;
                sunUV.x = sunNdc.x * 0.5f + 0.5f;
                sunUV.y = 0.5f - sunNdc.y * 0.5f;

                // Only bother if the projected sun is at least roughly on-screen.
                if (sunUV.x > -0.2f && sunUV.x < 1.2f &&
                    sunUV.y > -0.2f && sunUV.y < 1.2f)
                {
                    const int NUM_SAMPLES = 16;
                    float2 toSun = sunUV - uv;
                    float distToSun = length(toSun);

                    const float godRayScale = max(g_ColorGrade.z, 0.0f);

                    // Skip pixels very far from the sun projection to keep cost down.
                    if (distToSun > 0.02f && godRayScale > 1e-4f)
                    {
                        float2 step = toSun / (float)NUM_SAMPLES;
                        float2 sampleUV = uv;

                        float3 godAccum = 0.0f;
                        float illumination = 1.0f;

                        float density = saturate(max(g_FogParams.x, 0.0f) * 24.0f);
                        // Base intensity is controlled by fog density and the
                        // UI/scene intensity scalar. Keep the value bounded so
                        // shafts add energy gradually instead of flashing.
                        float baseIntensity = density * godRayScale;
                        float decay = lerp(0.88f, 0.96f, density);

                        [unroll]
                        for (int i = 0; i < NUM_SAMPLES; ++i)
                        {
                            sampleUV += step;
                            if (sampleUV.x < 0.0f || sampleUV.x > 1.0f ||
                                sampleUV.y < 0.0f || sampleUV.y > 1.0f)
                            {
                                break;
                            }

                            float d = g_Depth.SampleLevel(g_Sampler, sampleUV, 0).r;
                            // Treat fully-clear depth as sky. Geometry
                            // attenuates the ray instead of zeroing it so
                            // subpixel depth changes do not pop the whole
                            // shaft on/off while the camera moves.
                            float unoccluded = smoothstep(0.996f, 0.9995f, d);
                            illumination *= lerp(0.70f, decay, unoccluded);

                            float3 sampleHdr = g_SceneColor.SampleLevel(g_Sampler, sampleUV, 0).rgb;
                            float lum = min(dot(sampleHdr, float3(0.299f, 0.587f, 0.114f)), 8.0f);
                            godAccum += lum.xxx * illumination;
                        }

                        float falloff = saturate(1.0f - distToSun * 1.5f);
                        falloff *= falloff;

                        float3 sunRadiance = max(sun.color_range.rgb, 0.0f.xxx);
                        float sunMax = max(max(sunRadiance.r, sunRadiance.g), sunRadiance.b);
                        float3 sunTint = (sunMax > 1e-3f) ? (sunRadiance / sunMax) : max(g_AmbientColor.rgb, 0.0f.xxx);
                        float3 godRays = sunTint * godAccum * baseIntensity * falloff * 0.35f / (float)NUM_SAMPLES;

                        // Clamp to avoid excessive streak brightness.
                        godRays = min(godRays, 2.5f.xxx);

                        hdrBlurred += godRays;
                    }
                }
            }
        }
    }

    // Compose bloom after any motion blur and fog so blurred highlights remain
    // physically plausible and color-stable.
    float3 hdrCombined = hdrBlurred + bloom + highlightStreaks + halationColor;

    // Clamp HDR before tonemapping to avoid extreme spikes that can show up
    // as sudden RGB flashes when moving the camera across very bright areas.
    const float kMaxHdrBeforeTonemap = 32.0f;
    hdrCombined = min(hdrCombined, kMaxHdrBeforeTonemap.xxx);
    float highlightProtection = saturate(g_CinematicStabilityParams.w);
    float profileExposureTrim = clamp(g_CinematicExposureParams.x, 0.42f, 1.10f);
    float profileShoulderStart = clamp(g_CinematicExposureParams.y, 1.0f, 24.0f);
    float profileShoulderStrength = saturate(g_CinematicExposureParams.z);
    float profileWhiteCompression = saturate(g_CinematicExposureParams.w);
    float combinedShoulderStrength = saturate(highlightProtection + profileShoulderStrength);
    if (combinedShoulderStrength > 0.001f)
    {
        float hdrLuma = dot(hdrCombined, float3(0.2126f, 0.7152f, 0.0722f));
        float shoulderStart = min(lerp(24.0f, 16.0f, highlightProtection), profileShoulderStart);
        float shoulderScale = 1.0f;
        if (hdrLuma > shoulderStart)
        {
            float protectedLuma = shoulderStart + (hdrLuma - shoulderStart) * lerp(1.0f, 0.34f, combinedShoulderStrength);
            shoulderScale = protectedLuma / max(hdrLuma, 1e-4f);
        }
        hdrCombined *= shoulderScale;
    }

    float exposure = max(g_TimeAndExposure.z * profileExposureTrim, 0.01f);
    float3 color = hdrCombined * exposure;

    uint toneMapperMode = (uint)round(max(g_CinematicParams.x, 0.0f));
    color = ApplyToneMapper(color, toneMapperMode);
    color = ApplyCinematicToeLift(color, g_CinematicLookParams.x);
    color = ApplyPhotographicSplitTone(color, g_ColorGrade.xy);
    color = ApplyProfileColorSeparation(color, g_ColorGrade.xy, g_CinematicLookParams.z);
    color = ApplyHighlightSaturationRollOff(color, g_CinematicLookParams.y);
    color = ApplyPostWhiteCompression(color, profileWhiteCompression);
    color = ApplySceneLocalCinematicLookPolish(
        color,
        g_ColorGrade.xy,
        g_CinematicLookParams,
        profileWhiteCompression,
        highlightProtection);
    color = pow(color, 1.0f / 2.2f);

    // GPU-driven settings overlay. When g_DebugMode.y > 0.5 the engine is
    // indicating that the settings panel (M/F2) is active. We dim the scene
    // and render a simple panel on the right that shows rows/bars whose
    // lengths reflect the current debug settings. g_DebugMode.z encodes the
    // currently selected row (0..1 normalized).
    if (g_DebugMode.y > 0.5f)
    {
        // Dim the background colors, but keep the scene reasonably visible
        // so changes to lighting remain apparent while the menu is open.
        color *= 0.5f;

        const float panelX = 0.72f;
        const float headerY = 0.15f;
        const float bodyBottom = 0.95f;

        if (uv.x > panelX)
        {
            float3 panelColor = float3(0.05f, 0.05f, 0.05f);

            // Header band.
            if (uv.y < headerY)
            {
                panelColor = float3(0.0f, 0.35f, 0.55f);

                // Render-path indicator glyph in the header:
                // 'B' = visibility-buffer path, 'F' = forward fallback.
                int glyphId = vbActive ? GL_B : GL_F_;
                float2 iconOrigin = float2(panelX + (1.0f - panelX) * 0.05f, headerY * 0.20f);
                float2 iconSize = float2((1.0f - panelX) * 0.10f, headerY * 0.60f);
                float iconAlpha = SampleGlyph(glyphId, uv, iconOrigin, iconSize);
                if (iconAlpha > 0.01f)
                {
                    panelColor = lerp(panelColor, float3(1.0f, 1.0f, 1.0f), iconAlpha);
                }
            }

            // Subtle horizontal stripes.
            float stripe = frac(uv.y * 20.0f);
            if (stripe < 0.02f)
            {
                panelColor += 0.05f;
            }

            // Body rows representing settings.
            if (uv.y >= headerY && uv.y <= bodyBottom)
            {
                const int rowCount = 15;
                float rowHeight = (bodyBottom - headerY) / rowCount;
                int row = clamp(int((uv.y - headerY) / rowHeight), 0, rowCount - 1);
                int selectedRow = (int)round(saturate(g_DebugMode.z) * (rowCount - 1));

                // Row label on the left side. This uses a tiny bitmap font
                // rendered entirely in the shader, but we allocate a bit more
                // space so the glyphs form legible 3-4 letter abbreviations
                // instead of a dense QR-like block.
                float labelWidthNorm = 0.30f; // fraction of panel width
                float2 rowOrigin = float2(panelX, headerY + row * rowHeight);
                float2 labelOrigin = rowOrigin + float2(0.04f * (1.0f - panelX), rowHeight * 0.15f);
                float2 labelSize = float2((1.0f - panelX) * labelWidthNorm, rowHeight * 0.7f);
                float labelAlpha = RenderRowLabel(row, uv, labelOrigin, labelSize);

                if (labelAlpha > 0.01f)
                {
                    float3 textColor = (row == selectedRow)
                        ? float3(1.0f, 1.0f, 0.2f)
                        : float3(0.9f, 0.9f, 0.9f);
                    panelColor = lerp(panelColor, textColor, labelAlpha);
                }

                // Map settings to a normalized 0..1 bar length. The row
                // indices are aligned with Engine::m_settingsSection.
                float value = 0.0f;
                switch (row)
                {
                    case 0: // Exposure
                        value = saturate((g_TimeAndExposure.z - 0.5f) / 4.5f);
                        break;
                    case 1: // Bloom intensity
                        value = saturate(g_TimeAndExposure.w / max(g_BloomParams.z, 1.0f));
                        break;
                    case 2: // Shadows enabled
                        value = (g_ShadowParams.z > 0.5f) ? 1.0f : 0.0f;
                        break;
                    case 3: // PCSS
                        value = (g_ShadowParams.w > 0.5f) ? 1.0f : 0.0f;
                        break;
                    case 4: // Shadow bias
                        value = saturate(g_ShadowParams.x / 0.01f);
                        break;
                    case 5: // Shadow PCF radius
                        value = saturate(g_ShadowParams.y / 5.0f);
                        break;
                    case 6: // Cascade lambda (approximate from first split depth)
                        // Lambda lives on the CPU, but higher lambda generally
                        // pushes the first split farther from the camera, so
                        // we normalize the first split against the far plane.
                        value = saturate(g_CascadeSplits.x / max(g_CascadeSplits.w, 1e-4f));
                        break;
                    case 7: // FXAA
                        value = (g_PostParams.z > 0.5f) ? 1.0f : 0.0f;
                        break;
                    case 8: // TAA
                        value = (g_TAAParams.w > 0.5f) ? 1.0f : 0.0f;
                        break;
                    case 9: // SSR enabled flag
                        value = ssrEnabled ? 1.0f : 0.0f;
                        break;
                    case 10: // SSAO
                        value = (g_AOParams.x > 0.5f) ? 1.0f : 0.0f;
                        break;
                    case 11: // IBL
                        value = (g_EnvParams.z > 0.5f) ? 1.0f : 0.0f;
                        break;
                    case 12: // Fog
                        value = (g_FogParams.w > 0.5f) ? 1.0f : 0.0f;
                        break;
                    case 13: // Camera base speed (visualized as 0..1 in a loose range)
                        // The camera speed itself is only known on the CPU.
                        // We approximate this row as mid-level so navigation
                        // still lines up; the actual numeric value is
                        // displayed in the HUD legend.
                        value = 0.5f;
                        break;
                    case 14: // RT sun shadows (pipeline-ready)
                        value = (g_PostParams.w > 0.5f) ? 1.0f : 0.0f;
                        break;
                    default:
                        value = 0.5f;
                        break;
                }

                // Horizontal bar: start just to the right of the label area.
                float barStartX = panelX + (1.0f - panelX) * (labelWidthNorm + 0.04f);
                float xNorm = saturate((uv.x - barStartX) / (1.0f - barStartX));
                if (xNorm <= value)
                {
                    panelColor += 0.20f;
                }

                // Highlight selected row.
                if (row == selectedRow)
                {
                    panelColor += 0.10f;
                }
            }

            color = lerp(color, panelColor, 0.9f);
        }
    }

    float saturation = clamp(g_PostGradeParams.y, 0.0f, 2.0f);
    float luma = dot(color, float3(0.299f, 0.587f, 0.114f));
    color = lerp(luma.xxx, color, saturation);

    float contrastGrade = clamp(g_PostGradeParams.x, 0.5f, 1.5f);
    color = (color - 0.5f.xxx) * contrastGrade + 0.5f.xxx;

    float vignetteStrength = saturate(g_ColorGrade.w);
    if (vignetteStrength > 0.001f)
    {
        float2 centered = uv * 2.0f - 1.0f;
        centered.x *= g_PostParams.y / max(g_PostParams.x, 1e-6f);
        float radius2 = dot(centered, centered);
        float vignette = smoothstep(1.70f, 0.30f, radius2);
        color *= lerp(1.0f, lerp(0.55f, 1.0f, vignette), vignetteStrength);
    }

    // Screen-space ambient occlusion modulation (applied after tonemapping/grading).
    float ao = 1.0f;
    if (g_AOParams.x > 0.5f)
    {
        // Bilateral 3x3 blur over the SSAO buffer using full-resolution depth
        // as a guide. This keeps occlusion pinned to the correct surfaces and
        // reduces the large "halo discs" that appear when AO bleeds across
        // object boundaries or onto the environment.
        float2 texel = g_PostParams.xy;
        float  depthCenter = g_Depth.Sample(g_Sampler, uv).r;

        float aoAccum = 0.0f;
        float wAccum  = 0.0f;

        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            [unroll]
            for (int x = -1; x <= 1; ++x)
            {
                float2 offset = float2(x, y) * texel;
                float2 sampleUV = uv + offset;

                float sampleAO    = g_SSAO.Sample(g_Sampler, sampleUV).r;
                float sampleDepth = g_Depth.Sample(g_Sampler, sampleUV).r;

                float depthDelta = abs(sampleDepth - depthCenter);
                // Prefer AO from surfaces at a similar depth; fade out
                // contributions from significantly different depths so
                // background walls do not inherit foreground occlusion.
                float wDepth = saturate(1.0f - depthDelta * 40.0f);

                float w = wDepth;
                aoAccum += sampleAO * w;
                wAccum  += w;
            }
        }

        ao = (wAccum > 0.0f) ? saturate(aoAccum / wAccum) : 1.0f;

        color = ApplySceneMaterialCinematicContactAo(
            color,
            ao,
            sceneMaterialClass,
            surfaceClass,
            roughness,
            metallic,
            gbufNormal,
            depthCenter);
    }

    // Profile-owned clarity for scene-local views: use stable geometry
    // discontinuities rather than temporal/noisy color history so silhouettes
    // and material breaks read crisply without reintroducing mouse-look shimmer.
    if (g_CinematicLookParams.z > 0.001f)
    {
        float2 texel = g_PostParams.xy;
        float depthCenter = g_Depth.SampleLevel(g_Sampler, uv, 0).r;
        float depthR = g_Depth.SampleLevel(g_Sampler, uv + float2(texel.x, 0.0f), 0).r;
        float depthL = g_Depth.SampleLevel(g_Sampler, uv - float2(texel.x, 0.0f), 0).r;
        float depthU = g_Depth.SampleLevel(g_Sampler, uv - float2(0.0f, texel.y), 0).r;
        float depthD = g_Depth.SampleLevel(g_Sampler, uv + float2(0.0f, texel.y), 0).r;

        float3 normalR = normalize(g_NormalRoughness.SampleLevel(g_Sampler, uv + float2(texel.x, 0.0f), 0).xyz * 2.0f - 1.0f);
        float3 normalL = normalize(g_NormalRoughness.SampleLevel(g_Sampler, uv - float2(texel.x, 0.0f), 0).xyz * 2.0f - 1.0f);
        float3 normalU = normalize(g_NormalRoughness.SampleLevel(g_Sampler, uv - float2(0.0f, texel.y), 0).xyz * 2.0f - 1.0f);
        float3 normalD = normalize(g_NormalRoughness.SampleLevel(g_Sampler, uv + float2(0.0f, texel.y), 0).xyz * 2.0f - 1.0f);

        float depthEdge = max(max(abs(depthCenter - depthR), abs(depthCenter - depthL)),
                              max(abs(depthCenter - depthU), abs(depthCenter - depthD)));
        float normalEdge = max(max(1.0f - dot(gbufNormal, normalR),
                                   1.0f - dot(gbufNormal, normalL)),
                               max(1.0f - dot(gbufNormal, normalU),
                                   1.0f - dot(gbufNormal, normalD)));
        float edgeMask = saturate(depthEdge * 220.0f + normalEdge * 0.90f);
        float strength = saturate(g_CinematicLookParams.z * 0.22f + g_CinematicLookParams.y * 0.08f);
        float luma = dot(color, float3(0.299f, 0.587f, 0.114f));
        float midMask = smoothstep(0.08f, 0.28f, luma) * (1.0f - smoothstep(0.86f, 1.0f, luma));
        float clarity = edgeMask * strength * midMask;
        color *= 1.0f - clarity * 0.16f;
        color += (color - luma.xxx) * clarity * 0.08f;
        color = saturate(color);
    }

    // Optional FXAA-like smoothing (lightweight approximation). When TAA is
    // active we allow slightly more blur here so that any residual temporal
    // noise is traded for a stable, softer edge rather than visible
    // ghosting on large, high-contrast objects.
    if (g_PostParams.z > 0.5f)
    {
        float2 texel = g_PostParams.xy;
        float3 cM = color;
        float3 cR = g_SceneColor.Sample(g_Sampler, uv + float2(texel.x, 0.0f)).rgb;
        float3 cL = g_SceneColor.Sample(g_Sampler, uv - float2(texel.x, 0.0f)).rgb;
        float3 cU = g_SceneColor.Sample(g_Sampler, uv - float2(0.0f, texel.y)).rgb;
        float3 cD = g_SceneColor.Sample(g_Sampler, uv + float2(0.0f, texel.y)).rgb;

        float3 lumaWeights = float3(0.299f, 0.587f, 0.114f);
        float lumM = dot(cM, lumaWeights);
        float lumR = dot(cR, lumaWeights);
        float lumL = dot(cL, lumaWeights);
        float lumU = dot(cU, lumaWeights);
        float lumD = dot(cD, lumaWeights);

        float contrast = max(max(abs(lumM - lumR), abs(lumM - lumL)),
                             max(abs(lumM - lumU), abs(lumM - lumD)));

        // Only smooth when local contrast is noticeable
        float threshold = 0.03f;
        if (g_TAAParams.w > 0.5f)
        {
            threshold = 0.02f;
        }

        if (contrast > threshold)
        {
            float3 avg = (cM + cR + cL + cU + cD) * (1.0f / 5.0f);
            float blurAmount = (g_TAAParams.w > 0.5f) ? 0.8f : 0.6f;
            color = lerp(cM, avg, blurAmount);
        }
    }

    // SSAO / SSR / RT reflection debug views in post-process so tuning radius/bias/intensity is easier.
    if (g_DebugMode.x == 13.0f)
    {
        // AO only
        return float4(ao.xxx, 1.0f);
    }
    else if (g_DebugMode.x == 14.0f)
    {
        // AO overlay: visualize occlusion on top of final color
        float3 overlay = color * lerp(1.0f, ao, 0.75f);
        return float4(saturate(overlay), 1.0f);
    }
    else if (g_DebugMode.x == 15.0f)
    {
        // SSR-only view (pre-tonemap reflections buffer).
        float3 ssr = g_SSRColor.Sample(g_Sampler, uv).rgb;
        return float4(ssr, 1.0f);
    }
    else if (g_DebugMode.x == 16.0f)
    {
        // SSR overlay: visualize reflections on top of final color.
        float3 ssr = g_SSRColor.Sample(g_Sampler, uv).rgb;
        float3 overlay = color * 0.5f + ssr * 0.5f;
        return float4(saturate(overlay), 1.0f);
    }
    else if (g_DebugMode.x == 32.0f)
    {
        // HZB mip debug view.
        // The engine binds the HZB full-mip SRV into the g_SSRColor slot (t6)
        // for this debug mode. g_DebugMode.z selects the mip in [0..1].
        uint width = 0, height = 0, mipCount = 0;
        g_SSRColor.GetDimensions(0, width, height, mipCount);
        if (width == 0 || height == 0 || mipCount == 0)
        {
            // Magenta = SRV missing/unbound.
            return float4(1.0f, 0.0f, 1.0f, 1.0f);
        }

        uint mip = 0;
        if (mipCount > 1)
        {
            mip = (uint)round(saturate(g_DebugMode.z) * (mipCount - 1));
        }

        // HZB stores view-space Z (meters/units). Map to a readable grayscale.
        float viewZ = g_SSRColor.SampleLevel(g_Sampler, uv, mip).r;
        float farZ = max(g_CascadeSplits.w, 1e-3f);
        // Treat "inf"/invalid depth as far.
        if (viewZ > 1e8f || viewZ <= 0.0f)
        {
            viewZ = farZ;
        }

        float t = saturate(viewZ / farZ);
        float v = pow(1.0f - t, 0.7f);
        return float4(v.xxx, 1.0f);
    }
    else if (g_DebugMode.x == 20.0f)
    {
        // RT reflection-only debug view (pre-tonemap). Shows the raw DXR
        // reflection buffer color so the pipeline can be validated in
        // isolation from SSR and the main PBR shading.
        uint rtW = 0, rtH = 0;
        g_RTReflection.GetDimensions(rtW, rtH);
        if (rtW == 0 || rtH == 0)
        {
            // Red indicates the RT reflection SRV is unbound/null.
            return float4(1.0f, 0.0f, 0.0f, 1.0f);
        }
        float4 rtSample = g_RTReflection.SampleLevel(g_Sampler, uv, 0);
        float maxC = max(max(abs(rtSample.r), abs(rtSample.g)), abs(rtSample.b));
        if (maxC < 1e-5f)
        {
            int2 pix = clamp((int2)(uv * float2(depthDim)), int2(0, 0), depthMax);
            float depth = g_Depth.Load(int3(pix, 0));
            if (depth < 1.0f - 1e-4f)
            {
                // Green = geometry present but RT reflection buffer is zero.
                return float4(0.0f, 1.0f, 0.0f, 1.0f);
            }
        }
        // Boost visibility for debug (the RT buffer is HDR and often very dark
        // when displayed without tonemapping).
        return float4(saturate(rtSample.rgb * 4.0f), 1.0f);
    }
    else if (g_DebugMode.x == 30.0f)
    {
        // RT reflection history-only debug view (pre-tonemap).
        uint rtW = 0, rtH = 0;
        g_RTReflectionHistory.GetDimensions(rtW, rtH);
        if (rtW == 0 || rtH == 0)
        {
            return float4(1.0f, 0.0f, 0.0f, 1.0f);
        }
        float4 rtSample = g_RTReflectionHistory.SampleLevel(g_Sampler, uv, 0);
        return float4(saturate(rtSample.rgb * 4.0f), 1.0f);
    }
    else if (g_DebugMode.x == 31.0f)
    {
        // RT reflection delta: visualize absolute difference between current
        // RT reflection and the history buffer. Useful for spotting stale-tile
        // artifacts or reprojection/jitter mismatch.
        float3 curr = g_RTReflection.SampleLevel(g_Sampler, uv, 0).rgb;
        float3 hist = g_RTReflectionHistory.SampleLevel(g_Sampler, uv, 0).rgb;
        float3 d = abs(curr - hist);
        return float4(saturate(d * 8.0f), 1.0f);
    }
    else if (g_DebugMode.x == 24.0f)
    {
        // RT reflection ray-direction debug view. The DXR reflection pass
        // encodes the per-pixel reflection ray direction as RGB in the
        // reflection buffer when this mode is active (see RaytracedReflections.hlsl).
        float3 rayVis = g_RTReflection.SampleLevel(g_Sampler, uv, 0).rgb;
        return float4(rayVis, 1.0f);
    }

    // Shadow map cascade visualization in the top-right corner, only when
    // debug screen mode is active (g_DebugMode.x == 6). We show all three
    // cascades side-by-side in a 40% width x 40% height box, with an
    // inverted depth mapping to make geometry stand out (near = bright).
    if (g_DebugMode.x == 6 && uv.x > 0.6f && uv.y < 0.4f)
    {
        float2 local = float2((uv.x - 0.6f) / 0.4f, uv.y / 0.4f);
        float tiles = 3.0f;
        float scaledX = local.x * tiles;
        uint cascadeIndex = (uint)scaledX;
        cascadeIndex = min(cascadeIndex, 2u);
        float2 tileUV = float2(frac(scaledX), local.y);

        float depth = g_ShadowMap.Sample(g_Sampler, float3(tileUV, cascadeIndex)).r;

        // Depth is in [0,1] with 1 = far plane (clear). Invert and scale
        // to emphasize surfaces near the light; empty space stays dark.
        float invDepth = saturate((1.0f - depth) * 6.0f);
        float3 depthVis = invDepth.xxx;
        color = depthVis;
    }

    return float4(saturate(color), 1.0f);
}
