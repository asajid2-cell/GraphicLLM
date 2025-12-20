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
    // x = warm tint (-1..1), y = cool tint (-1..1), z,w reserved
    float4   g_ColorGrade;
    // x = fog density, y = base height, z = height falloff, w = fog enabled (>0.5)
    float4   g_FogParams;
    // x = SSAO enabled (>0.5), y = radius, z = bias, w = intensity
    float4   g_AOParams;
    // x = bloom threshold, y = soft-knee factor, z = max bloom contribution,
    // w = SSR enabled (>0.5) for the post-process debug overlay
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

    // Disable TAA for very fast motion (in pixels) to avoid long streaks.
    if (speedPx >= 24.0f)
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

    // Roughness gating: shiny surfaces get dramatically less history in all
    // regimes so fast-changing specular reflections do not leave "inner
    // ghosts" (especially noticeable up close on chrome spheres).
    float roughFactor = saturate(surfaceRoughness * 0.75f + 0.25f);
    roughFactor *= roughFactor;

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

    finalBlend *= roughFactor * (1.0f - edgeFactor) * (1.0f - reactiveMask);

    // Clamp maximum history contribution per frame. Use a tighter cap on
    // glossy surfaces where specular reflections move non-linearly and
    // camera-only motion vectors cannot reproject perfectly.
    float roughnessClamp = lerp(0.02f, 0.25f, saturate(surfaceRoughness / 0.6f));
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
    uint postFxFlags = (uint)(g_BloomParams.w + 0.5f); 
    bool ssrEnabled = ((postFxFlags & 1u) != 0u); 
    bool rtReflEnabled = ((postFxFlags & 2u) != 0u); 
    bool rtReflHistoryValid = ((postFxFlags & 4u) != 0u); 
    bool rtReflTemporalOff = ((postFxFlags & 8u) != 0u); 
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

    // Base scene color + alpha as written by the main PBR pass. For opaque
    // materials alpha is 1; glass / water / other transparent materials use
    // alpha < 1 and are treated as thin transmissive surfaces in this pass.
    float4 sceneSample = g_SceneColor.Sample(g_Sampler, uv);
    float3 hdrColor = sceneSample.rgb;
    float  opacity = sceneSample.a;

    // G-buffer normal and roughness for the current pixel, shared between
    // refraction and reflection logic. The normal is encoded as 0..1 and
    // unpacked back to -1..1 here.
    float4 nrSample = g_NormalRoughness.Sample(g_Sampler, uv);
    float3 gbufNormal = normalize(nrSample.xyz * 2.0f - 1.0f);
    float  roughness = nrSample.w;

    // Screen-space refraction for thin transparent materials (glass, water).
    // We approximate refraction as a small UV offset in the scene color
    // buffer driven by the surface normal and opacity. This is not physically
    // exact but gives a convincing "bent background" look for glass bricks
    // and water without requiring a separate depth/scene-color prepass.
    if (opacity < 0.99f)
    {
        float3 N = gbufNormal;
        float2 dir = N.xy;
        float  lenDir = max(length(dir), 1e-3f);
        dir /= lenDir;

        // Stronger distortion for thinner / more transparent surfaces and
        // for glossier materials; very rough transparent objects behave more
        // like frosted glass so the refraction offset is smaller.
        float gloss = saturate(1.0f - roughness);
        float transparency = saturate(1.0f - opacity);

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

    // Clamp extremely bright SSR highlights to avoid harsh color pops when
    // the ray marches across very hot pixels in the environment or scene.
    float3 ssrColor = ssrSample.rgb;
    float  ssrMax   = max(max(ssrColor.r, ssrColor.g), ssrColor.b);
    const float kMaxSSRIntensity = 32.0f;
    if (ssrMax > kMaxSSRIntensity)
    {
        ssrColor *= (kMaxSSRIntensity / ssrMax);
    }

    // Base reflection strength from roughness (shared by SSR and RT). This
    // roughly tracks the specular lobe so glossy surfaces get stronger
    // reflections while rough surfaces stay diffuse/IBL dominated.
    float gloss = saturate(1.0f - roughness);
    gloss *= gloss;

    // Scale SSR coverage into a soft weight; keep some headroom so the
    // underlying BRDF/IBL specular term can still contribute. Only allow
    // SSR to contribute on genuinely glossy surfaces with reasonably high
    // ray confidence to avoid large, low-frequency reflection overlays on
    // mid-roughness walls and floors.
    const float kMaxSSRWeight = 0.6f;
    float  wSSR = 0.0f;
    // SSR is extremely fragile on near-perfect mirrors (roughness ~ 0) and
    // tends to self-intersect on convex glossy objects (chrome spheres),
    // producing the classic "inner copy" ghost. Prefer IBL/RT in that regime.
    if (roughness > 0.08f && roughness < 0.35f && ssrWeightRaw > 0.4f)
    {
        float ssrConf = ssrWeightRaw * ssrWeightRaw;
        wSSR = ssrConf * kMaxSSRWeight * gloss;
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
        // Clamp extreme RT reflection intensity so very hot env texels do not
        // overpower the underlying BRDF/IBL term.
        const float kMaxRTIntensity = 32.0f;
        float rtMax = max(max(rtRefl.r, rtRefl.g), rtRefl.b);
        if (rtMax > kMaxRTIntensity)
        {
            rtRefl *= (kMaxRTIntensity / rtMax);
        }

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
            float3 sampleRT = sampleRT4.rgb;
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

        rtRefl = accum / max(total, 1e-4f); 
 
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
            float3 rtHist = rtHist4.rgb;
            float  histValid = saturate(rtHist4.a);

            float3 diff = abs(rtRefl - rtHist);
            float maxDiffHist = max(max(diff.r, diff.g), diff.b);

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
            float historyWeight = baseHist * histValid * reprojOk;
            historyWeight *= lerp(1.0f, 0.0f, saturate(maxDiffHist * 4.0f));

            rtRefl = lerp(rtRefl, rtHist, historyWeight);
        }
    }

    // Prefer SSR whenever it is confident; let RT take over only when SSR
    // confidence is low so the total reflection energy stays stable and the
    // two lobes do not "fight" each other.
    float  rawRTWeight = 1.0f - ssrWeightRaw;
    rawRTWeight *= rawRTWeight;
    float  wRT = rtEnabled ? rawRTWeight * gloss : 0.0f;

    float  weightSum = wSSR + wRT;
    if (weightSum > 1e-4f)
    {
        float invSum = 1.0f / weightSum;
        float3 reflHybrid = (ssrColor * wSSR + rtRefl * wRT) * invSum;

        // Detect water pixels approximately by comparing reconstructed
        // world-space height to the global water level. This lets us boost
        // reflection strength on water surfaces without introducing a full
        // material ID system.
        float depth = g_Depth.Sample(g_Sampler, uv).r;
        float3 worldPos = ReconstructWorldPosition(uv, depth);
        float waterLevelY = g_WaterParams0.w;
        bool isWaterPixel = abs(worldPos.y - waterLevelY) < 0.3f;

        const float maxReflBlendNonWater = 0.3f;
        const float maxReflBlendWater    = 0.75f;
        float maxReflBlend = isWaterPixel ? maxReflBlendWater : maxReflBlendNonWater;

        // Final lerp factor: surface gloss and total reflection weight gate
        // how strongly we move towards the hybrid reflection color.
        float reflBlend = maxReflBlend * saturate(weightSum);
        hdrColor = lerp(hdrColor, reflHybrid, reflBlend);
    }

    // Bloom: sample blurred bloom texture if available
    float bloomIntensity = max(g_TimeAndExposure.w, 0.0f);
    float3 bloom = 0.0f;
    if (bloomIntensity > 0.001f) {
        bloom = g_BloomSource.Sample(g_Sampler, uv).rgb * bloomIntensity;

        // Clamp bloom contribution to avoid overly blown-out highlights.
        float maxBloom = max(g_BloomParams.z, 0.0f);
        if (maxBloom > 0.0f)
        {
            bloom = min(bloom, maxBloom.xxx);
        }
    }

    // Start from base HDR lighting (without bloom); motion blur (when enabled)
    // operates on this term only so bloom and grading stay stable.
    float3 hdrBlurred = hdrColor;

    // Simple motion blur based on velocity buffer (camera-only) in HDR space.
    // Disabled by default because it can be easily confused with TAA ghosting
    // (especially on glossy reflections) and makes temporal debugging harder.
    if (false)
    {
        float2 vel = g_Velocity.Sample(g_Sampler, uv).xy;
        float  speed = length(vel);
        // Keep blur radius modest to avoid sampling across large portions of
        // the screen; high-speed motion will still get some streaking, but
        // we bias towards stability over extremely strong blur.
        float  blurStrength = saturate(speed * 4.0f);

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

    // Exponential height fog in HDR space, using the depth buffer and
    // reconstructed world position. Applied before bloom so fogged highlights
    // still contribute naturally to bloom.
    if (g_FogParams.w > 0.5f)
    {
        float depth = g_Depth.Sample(g_Sampler, uv).r;
        if (depth < 1.0f - 1e-4f)
        {
            float3 worldPos = ReconstructWorldPosition(uv, depth);
            float3 camPos = g_CameraPosition.xyz;

            float distance = length(worldPos - camPos);
            float density = max(g_FogParams.x, 0.0f);

            // Base exponential distance fog
            float fog = 1.0f - exp(-density * distance);

            // Optional height falloff so fog thickens near a reference plane.
            float baseHeight = g_FogParams.y;
            float falloff = max(g_FogParams.z, 0.0f);
            float h = worldPos.y - baseHeight;
            // Fog weaker above the base height, stronger below.
            float heightFactor = exp(-falloff * max(h, 0.0f));

            fog *= heightFactor;
            fog = saturate(fog);

            float3 fogColor = g_AmbientColor.rgb;
            hdrBlurred = lerp(hdrBlurred, fogColor, fog);
        }
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

                    // Skip pixels very far from the sun projection to keep cost down.
                    if (distToSun > 0.02f)
                    {
                        float2 step = toSun / (float)NUM_SAMPLES;
                        float2 sampleUV = uv;

                        float3 godAccum = 0.0f;
                        float illumination = 1.0f;

                        float density = max(g_FogParams.x, 0.0f);
                        // Base intensity tied to fog density so thicker fog yields
                        // stronger, more visible beams.
                        float baseIntensity = saturate(density * 20.0f);
                        float decay = 0.92f;

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
                            // Treat fully-clear depth as sky; anything else occludes.
                            float unoccluded = (d >= 1.0f - 1e-3f) ? 1.0f : 0.0f;
                            illumination *= lerp(0.0f, 1.0f, unoccluded * decay);

                            float3 sampleHdr = g_SceneColor.SampleLevel(g_Sampler, sampleUV, 0).rgb;
                            float lum = dot(sampleHdr, float3(0.299f, 0.587f, 0.114f));
                            godAccum += lum.xxx * illumination;
                        }

                        float falloff = saturate(1.0f - distToSun * 1.5f);
                        float3 godColor = g_AmbientColor.rgb;
                        float3 godRays = godColor * godAccum * baseIntensity * falloff / (float)NUM_SAMPLES;

                        // Clamp to avoid excessive streak brightness.
                        godRays = min(godRays, 4.0f.xxx);

                        hdrBlurred += godRays;
                    }
                }
            }
        }
    }

    // Compose bloom after any motion blur and fog so blurred highlights remain
    // physically plausible and color-stable.
    float3 hdrCombined = hdrBlurred + bloom;

    // Clamp HDR before tonemapping to avoid extreme spikes that can show up
    // as sudden RGB flashes when moving the camera across very bright areas.
    const float kMaxHdrBeforeTonemap = 32.0f;
    hdrCombined = min(hdrCombined, kMaxHdrBeforeTonemap.xxx);

    float exposure = max(g_TimeAndExposure.z, 0.01f);
    float3 color = hdrCombined * exposure;

    color = ApplyACESFilm(color);
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
                // space so the glyphs form legible 3–4 letter abbreviations
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

    // Simple warm/cool grading driven by g_ColorGrade.xy.
    // Positive warm shifts towards orange, positive cool shifts towards blue.
    float warm = saturate(0.5f + g_ColorGrade.x * 0.5f); // map [-1,1] -> [0,1]
    float cool = saturate(0.5f + g_ColorGrade.y * 0.5f);
    float3 warmTint = lerp(float3(1.0f, 1.0f, 1.0f), float3(1.05f, 1.0f, 0.95f), warm);
    float3 coolTint = lerp(float3(1.0f, 1.0f, 1.0f), float3(0.96f, 1.0f, 1.05f), cool);
    color *= warmTint * coolTint;

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

        // Keep AO influence relatively subtle so it grounds objects without
        // creating strong dark discs under them.
        float aoIntensity = saturate(g_AOParams.w * 0.6f);
        color *= lerp(1.0f, ao, aoIntensity);
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
