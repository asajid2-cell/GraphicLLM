// Fullscreen post-process: exposure, ACES tonemapping, gamma, simple bloom stub

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
    // x = warm tint (-1..1), y = cool tint (-1..1),
    // z = god-ray intensity scale, w reserved
    float4   g_ColorGrade;
    // x = fog density, y = base height, z = height falloff, w = fog enabled (>0.5)
    float4   g_FogParams;
    // x = SSAO enabled (>0.5), y = radius, z = bias, w = intensity
    float4   g_AOParams;
    // x = bloom threshold, y = soft-knee factor, z = max bloom contribution,
    // w = SSR enabled (>0.5) for the post-process debug overlay
    float4   g_BloomParams;
    // x = jitterX, y = jitterY, z = TAA blend factor, w = TAA enabled (>0.5)
    float4   g_TAAParams;
    float4x4 g_ViewProjectionNoJitter;
    float4x4 g_InvViewProjectionNoJitter;
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
    float4   g_WaterParams0;
    float4   g_WaterParams1;
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
// Shadow map array is accessed via a separate descriptor table (space1) so
// that t0-t5 in space0 can be used for post-process textures without aliasing.
Texture2DArray g_ShadowMap : register(t0, space1);
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
    float weights[5] = {0.204164f, 0.304005f, 0.093913f, 0.010381f, 0.000837f};
    sum += g_SceneColor.Sample(g_Sampler, input.uv).rgb * weights[0];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel).rgb * weights[1];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel).rgb * weights[1];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel * 2.0f).rgb * weights[2];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel * 2.0f).rgb * weights[2];
    return float4(sum, 1.0f);
}

// Vertical blur of the bloom texture (source bound at t0)
float4 BloomBlurVPS(VSOutput input) : SV_TARGET
{
    float2 texel = float2(0.0f, g_PostParams.y * 4.0f); // quarter-res approximation
    float3 sum = 0.0f;
    float weights[5] = {0.204164f, 0.304005f, 0.093913f, 0.010381f, 0.000837f};
    sum += g_SceneColor.Sample(g_Sampler, input.uv).rgb * weights[0];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel).rgb * weights[1];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel).rgb * weights[1];
    sum += g_SceneColor.Sample(g_Sampler, input.uv + texel * 2.0f).rgb * weights[2];
    sum += g_SceneColor.Sample(g_Sampler, input.uv - texel * 2.0f).rgb * weights[2];
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

// ----------------------------------------------------------------------------
// HDR TAA resolve pass
// ----------------------------------------------------------------------------
float4 TAAResolvePS(VSOutput input) : SV_TARGET
{
    float2 uv = input.uv;
    float4 sceneSample = g_SceneColor.Sample(g_Sampler, uv);
    float3 curr = sceneSample.rgb;
    float  opacity = sceneSample.a;

    // Per-material TAA weight encoded into the alpha channel for opaque
    // surfaces. Opaque materials store a weight in the [0.99, 1.0] range
    // so the post-process can recover it without affecting the opaque vs
    // transparent classification used elsewhere. Transparent pixels
    // (opacity < 0.99) are treated as having no TAA history to avoid
    // smearing glass / UI / volume-like elements.
    float materialTAAWeight = 1.0f;
    if (opacity >= 0.99f)
    {
        materialTAAWeight = saturate((opacity - 0.99f) * 100.0f);
    }
    else
    {
        materialTAAWeight = 0.0f;
    }

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

    float2 vel = g_Velocity.Sample(g_Sampler, uv).xy;
    float  speed = length(vel);

    // Disable TAA for very fast motion to avoid long streaks.
    if (speed >= 0.5f)
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
    float2 texel = g_PostParams.xy;
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

    // Motion vectors are stored in non-jittered UV space; g_TAAParams.xy
    // encodes the jitter delta between the previous and current frames in
    // UV units. To sample the correct history location, apply both the
    // camera motion (vel) and the jitter difference.
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

    // Large color deltas generally indicate either disocclusion or highly
    // view-dependent shading (e.g., reflections). In those cases, relying
    // on history tends to produce visible ghosts, so treat them as
    // effectively "no history" for this frame.
    if (maxDiff > 0.6f)
    {
        if (debugView == 25u)
        {
            return float4(0.0f, 0.0f, 0.0f, 1.0f);
        }
        return float4(curr, 1.0f);
    }

    // Roughness gating: shiny surfaces get less history in all regimes so
    // fast-moving specular highlights do not leave long trails.
    float roughFactor = saturate(surfaceRoughness * 0.6f + 0.2f);

    float finalBlend = 0.0f;

    // Static: essentially locked pixels. Use a modest history weight so
    // they benefit from temporal smoothing without leaving long-lived
    // ghosts when lighting changes.
    if (speed < 0.02f && maxDiff < 0.02f)
    {
        finalBlend = taaBlendBase * 0.6f;
    }
    // Transitional: small motion or moderate color changes. Keep history
    // contribution very low so new frames dominate.
    else if (speed < 0.20f && maxDiff < 0.18f)
    {
        finalBlend = taaBlendBase * 0.25f;
    }
    // High-frequency but still somewhat stable (e.g., glossy highlights).
    // Only allow a tiny amount of history so specular streaks do not smear.
    else if (speed < 0.30f && maxDiff < 0.30f)
    {
        finalBlend = taaBlendBase * 0.15f;
    }
    // Otherwise treat as dynamic / disoccluded and rely on the current
    // frame only; history stays in the clamp range but does not influence
    // the final color this frame.

    // At hard geometric edges (large depth variance in the 3x3 stencil) we
    // aggressively suppress history so silhouettes remain crisp instead of
    // blending foreground and background together.
    // Treat even small depth discontinuities as candidates for history
    // suppression; the Cornell box has large, high-contrast edges where
    // cross-surface blending is very noticeable.
    float edgeDepthMin = 0.0007f;
    float edgeDepthMax = 0.0040f;
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

    finalBlend *= roughFactor * (1.0f - edgeFactor) * (1.0f - reactiveMask);

    // If this pixel sits on a clear geometric edge (noticeable depth step),
    // clamp history even more aggressively so Cornell-box style wall/floor
    // edges do not accumulate ghosts. Anything with a depth variance above
    // edgeDepthMin is considered an edge for this purpose.
    if (maxDepthDelta > edgeDepthMin)
    {
        finalBlend *= 0.15f;
    }

    // Mirror-like / very smooth surfaces (low roughness) are highly
    // view-dependent; relying on history here tends to create visible
    // ghosting and seam artifacts (e.g., the Cornell mirror panel). For
    // those pixels, disable history entirely so the image tracks the
    // current frame only; they are handled by SSR/RT instead.
    if (surfaceRoughness < 0.07f)
    {
        finalBlend = 0.0f;
    }

    // Clamp maximum history contribution per frame so that large objects do
    // not accumulate very long tails of stale information. This trades a bit
    // more spatial noise for significantly shorter-lived ghosting.
    finalBlend = min(finalBlend, 0.18f);

    // Treat very bright, isolated hotspots as fully reactive: if the current
    // pixel is much brighter than its local neighbourhood, relying on
    // history tends to leave visible "afterimages". In that case, fall back
    // to the current frame only for this pixel.
    if (lumDeltaLocal > 0.5f)
    {
        finalBlend = 0.0f;
    }

    // Apply per-material TAA weight recovered from the scene color alpha so
    // mirrors, glass, and emissive panels can request reduced or disabled
    // history accumulation without requiring an additional G-buffer.
    finalBlend *= materialTAAWeight;

    finalBlend = saturate(finalBlend);

    float3 result = lerp(currClamped, historyClamped, finalBlend);

    if (debugView == 25u)
    {
        // TAA debug: visualize how much history is blended into the final
        // color. Bright pixels have strong temporal accumulation; dark
        // pixels lean towards the current frame.
        return float4(finalBlend.xxx, 1.0f);
    }

    return float4(result, 1.0f);
}

float4 PSMain(VSOutput input) : SV_TARGET
{
    float3 hdr = g_SceneColor.Sample(g_Sampler, input.uv).rgb;
    // Guard against NaNs/Infs propagating into the tonemap. If we hit bad data,
    // paint hot magenta so we can spot it visually without triggering undefined
    // behavior on some drivers.
    if (any(isnan(hdr)) || any(isinf(hdr)))
    {
        return float4(1.0f, 0.0f, 1.0f, 1.0f);
    }

    float exposure = max(g_TimeAndExposure.z, 0.01f);
    float3 color = hdr * exposure;
    color = ApplyACESFilm(color);
    color = saturate(color);
    color = pow(color, 1.0f / 2.2f);
    return float4(color, 1.0f);
}
