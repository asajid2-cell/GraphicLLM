// Basic PBR-style vertex and pixel shaders
// Implements forward rendering with texture support

#include "PBR_Lighting.hlsli"
#include "BiomeMaterials.hlsli"

// Constant buffers - must match ShaderTypes.h
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
    float4 g_CameraPosition;
    // x = time, y = deltaTime, z = exposure, w = bloom intensity
    float4 g_TimeAndExposure;
    // rgb: ambient color * intensity, w unused
    float4 g_AmbientColor;
    uint4 g_LightCount;
    // Forward lights (light 0 is the sun). Must match the size used in
    // ShaderTypes.h (kMaxForwardLights).
    struct Light
    {
        float4 position_type;        // xyz = position (for point/spot), w = type
        float4 direction_cosInner;   // xyz = direction, w = inner cone cos (spot)
        float4 color_range;          // rgb = color * intensity, w = range (point/spot)
        float4 params;               // x = outer cone cos, y = shadow index, z,w reserved
    };
    static const uint LIGHT_MAX = 16;
    Light g_Lights[LIGHT_MAX];
    // Directional + local light view-projection matrices:
    // indices 0-2: cascades for the sun
    // indices 3-5: shadowed local lights (spot)
    float4x4 g_LightViewProjection[6];
    // x,y,z = cascade split depths in view space, w = far plane
    float4 g_CascadeSplits;
    // x = depth bias, y = PCF radius in texels, z = shadows enabled (>0.5), w = PCSS enabled (>0.5)
    float4 g_ShadowParams;
    // x = debug view mode (0 = shaded, 1 = normals, 2 = roughness, 3 = metallic,
    //                      4 = albedo, 5 = cascade index, 6 = debug screen,
    //                      7 = fractal height, 8 = IBL diffuse only,
    //                      9 = IBL specular only, 10 = env direction/UV,
    //                      11 = Fresnel (Fibl), 12 = specular mip,
    //                      13 = SSAO only, 14 = SSAO overlay), others reserved
    float4 g_DebugMode;
    // x = 1 / screenWidth, y = 1 / screenHeight, z = FXAA enabled (>0.5),
    // w = RT sun shadows enabled (>0.5)
    float4 g_PostParams;
    // x = diffuse IBL intensity, y = specular IBL intensity,
    // z = IBL enabled (>0.5), w = environment index (0 = studio, 1 = sunset, 2 = night)
    float4 g_EnvParams;
    // x = warm tint (-1..1), y = cool tint (-1..1),
    // z = god-ray intensity scale, w reserved
    float4 g_ColorGrade;
    // x = fog density, y = base height, z = height falloff, w = fog enabled (>0.5)
    float4 g_FogParams;
    // x = SSAO enabled (>0.5), y = radius, z = bias, w = intensity
    float4 g_AOParams;
    // x = bloom threshold, y = soft-knee factor, z = max bloom contribution,
    // w = SSR enabled (>0.5) for the post-process debug overlay
    float4 g_BloomParams;
    // x = jitterX, y = jitterY, z = TAA blend factor, w = TAA enabled (>0.5)
    float4 g_TAAParams;
    float4x4 g_ViewProjectionNoJitter;
    float4x4 g_InvViewProjectionNoJitter;
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
    // x = base wave amplitude, y = base wave length,
    // z = wave speed,          w = global water level (Y)
    float4 g_WaterParams0;
    // x = primary wave dir X,  y = primary wave dir Z,
    // z = secondary amplitude, w = steepness (0..1)
    float4 g_WaterParams1;

    // Clustered lighting parameters for forward+ transparency (populated by the VB path).
    // SRV indices refer to the global shader-visible CBV/SRV/UAV heap.
    uint4  g_ScreenAndCluster;   // x=width, y=height, z=clusterCountX, w=clusterCountY
    uint4  g_ClusterParams;      // x=clusterCountZ, y=maxLightsPerCluster, z=localLightCount, w unused
    uint4  g_ClusterSRVIndices;  // x=localLights, y=clusterRanges, z=clusterIndices, w unused
    float4 g_ProjectionParams;   // x=proj11, y=proj22, z=nearZ, w=farZ
};

cbuffer ShadowConstants : register(b3)
{
    uint4 g_ShadowCascadeIndex;
};

cbuffer MaterialConstants : register(b2)
{
    float4 g_Albedo;
    float g_Metallic;
    float g_Roughness;
    float g_AO;
    float g_MaterialPad0;  // Padding for 16-byte alignment
    // Bindless texture indices for SM6.6 ResourceDescriptorHeap access
    // Use 0xFFFFFFFF for invalid/unused textures
    uint4 g_TextureIndices;  // x: albedo, y: normal, z: metallic, w: roughness
    uint4 g_MapFlags;        // x: albedo, y: normal, z: metallic, w: roughness (legacy)
    uint4 g_TextureIndices2; // x: occlusion, y: emissive, z/w unused
    uint4 g_MapFlags2;       // x: occlusion, y: emissive, z/w unused
    float4 g_EmissiveFactorStrength; // rgb emissive factor, w emissive strength
    float4 g_ExtraParams;            // x occlusion strength, y normal scale, z/w reserved
    float4 g_FractalParams0; // x=amplitude, y=frequency, z=octaves, w=useFractalNormal
    float4 g_FractalParams1; // x=coordMode (0=UV,1=worldXZ), y=scaleX, z=scaleZ, w=reserved
    float4 g_FractalParams2; // x=lacunarity, y=gain, z=warpStrength, w=noiseType (0=fbm,1=ridged,2=turb)
    // x = clear-coat weight, y = clear-coat roughness, z/w reserved.
    float4 g_CoatParams;
    // x = transmission factor, y = IOR, z/w reserved
    float4 g_TransmissionParams;
    // rgb = specular color factor (linear), w = specular factor
    float4 g_SpecularParams;
    // x=transmission, y=clearcoat, z=clearcoatRoughness, w=specular
    uint4  g_TextureIndices3;
    // x=specularColor, y/z/w unused
    uint4  g_TextureIndices4;
};

// Invalid bindless index sentinel
static const uint INVALID_BINDLESS_INDEX = 0xFFFFFFFF;

// Legacy texture and sampler bindings (must be declared before helper functions)
Texture2D g_AlbedoTexture : register(t0);
Texture2D g_NormalTexture : register(t1);
Texture2D g_MetallicTexture : register(t2);
Texture2D g_RoughnessTexture : register(t3);
// Shadow map + IBL + optional RT shadow mask textures are bound in a separate
// descriptor table (space1) to avoid overlapping with per-pass textures in
// space0.
Texture2DArray g_ShadowMap : register(t0, space1);
Texture2D g_EnvDiffuse : register(t1, space1);
Texture2D g_EnvSpecular : register(t2, space1);
Texture2D<float> g_RtShadowMask : register(t3, space1);
Texture2D<float> g_RtShadowMaskHistory : register(t4, space1);
Texture2D g_RtGI : register(t5, space1);
Texture2D g_RtGIHistory : register(t6, space1);
SamplerState g_Sampler : register(s0);

// ============================================================================
// BINDLESS TEXTURE SUPPORT (SM6.6)
// ============================================================================
// When ENABLE_BINDLESS is defined (requires SM6.6 / DXC compiler), we use
// ResourceDescriptorHeap[] to sample textures directly by index. This enables
// GPU-driven rendering where material texture choices are made entirely on
// the GPU without CPU descriptor table binding.
//
// When ENABLE_BINDLESS is not defined (SM5.1 / D3DCompile), we always use
// the legacy descriptor table textures for full backwards compatibility.
// ============================================================================

// Helper functions to sample textures with bindless fallback
float4 SampleBindlessTexture(uint textureIndex, Texture2D fallbackTex, float2 uv, SamplerState samp)
{
#ifdef ENABLE_BINDLESS
    if (textureIndex != INVALID_BINDLESS_INDEX)
    {
        // SM6.6 bindless access via ResourceDescriptorHeap
        Texture2D tex = ResourceDescriptorHeap[textureIndex];
        return tex.Sample(samp, uv);
    }
    else
    {
        return fallbackTex.Sample(samp, uv);
    }
#else
    // SM5.1 fallback: always use descriptor table textures
    return fallbackTex.Sample(samp, uv);
#endif
}

// Sample albedo texture (with bindless support)
float4 SampleAlbedo(float2 uv)
{
    return SampleBindlessTexture(g_TextureIndices.x, g_AlbedoTexture, uv, g_Sampler);
}

// Sample normal texture (with bindless support)
float4 SampleNormal(float2 uv)
{
    return SampleBindlessTexture(g_TextureIndices.y, g_NormalTexture, uv, g_Sampler);
}

// Sample metallic texture (with bindless support)
float4 SampleMetallic(float2 uv)
{
    return SampleBindlessTexture(g_TextureIndices.z, g_MetallicTexture, uv, g_Sampler);
}

// Sample roughness texture (with bindless support)
float4 SampleRoughness(float2 uv)
{
    return SampleBindlessTexture(g_TextureIndices.w, g_RoughnessTexture, uv, g_Sampler);
}

// Vertex shader input
struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;  // Vertex color (biome blend data for terrain)
};

// Vertex shader output / Pixel shader input
struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
    float4 color : COLOR;  // Vertex color passed through
};

// Vertex Shader
PSInput VSMain(VSInput input)
{
    PSInput output;

    // Transform to world space
    float4 worldPos = mul(g_ModelMatrix, float4(input.position, 1.0f));
    output.worldPos = worldPos.xyz;

    // Transform to clip space
    output.position = mul(g_ViewProjectionMatrix, worldPos);
    output.position.z += g_DepthBiasNdc * output.position.w;

    // Transform normal to world space
    output.normal = normalize(mul(g_NormalMatrix, float4(input.normal, 0.0f)).xyz);
    float3 tangentWS = normalize(mul(g_NormalMatrix, float4(input.tangent.xyz, 0.0f)).xyz);
    output.tangent = float4(tangentWS, input.tangent.w);

    // Pass through texture coordinates and vertex color
    output.texCoord = input.texCoord;
    output.color = input.color;

    return output;
}

// Depth-only vertex shader for directional shadow map
struct VSShadowOutput
{
    float4 position : SV_POSITION;
    float2 texCoord : TEXCOORD0;
};

VSShadowOutput VSShadow(VSInput input)
{
    VSShadowOutput output;

    float4 worldPos = mul(g_ModelMatrix, float4(input.position, 1.0f));
    uint sliceIndex = g_ShadowCascadeIndex.x;
    // Support the three cascades (0-2) plus additional slices for local
    // shadowed lights (3-5). Clamp to the last valid matrix.
    sliceIndex = min(sliceIndex, 5u);
    output.position = mul(g_LightViewProjection[sliceIndex], worldPos);
    output.texCoord = input.texCoord;

    return output;
}

float4 SampleBindlessTextureOrDefault(uint textureIndex, float2 uv, float4 defaultValue)
{
#ifdef ENABLE_BINDLESS
    if (textureIndex != INVALID_BINDLESS_INDEX)
    {
        Texture2D tex = ResourceDescriptorHeap[textureIndex];
        return tex.Sample(g_Sampler, uv);
    }
#endif
    return defaultValue;
}

// Alpha-tested pixel shader for shadow maps (glTF alphaMode=MASK). We rely on
// g_MaterialPad0 as the alpha cutoff when drawing masked materials.
void PSShadowAlphaTest(VSShadowOutput input)
{
    float alphaCutoff = g_MaterialPad0;
    float alpha = SampleAlbedo(input.texCoord).a * g_Albedo.a;
    clip(alpha - alphaCutoff);
}

static const float SHADOW_MAP_SIZE = 2048.0f;
static const uint LIGHT_TYPE_DIRECTIONAL = 0;
static const uint LIGHT_TYPE_POINT       = 1;
static const uint LIGHT_TYPE_SPOT        = 2;
static const uint LIGHT_TYPE_AREA_RECT   = 3;

// --- Fractal noise helpers (2D hash + value noise + fbm) ---

float2 Hash2(float2 p) {
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac(float2(p3.x + p3.y, p3.y + p3.z));
}

float Noise2D(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);

    float2 u = f * f * (3.0 - 2.0 * f);

    float2 a = Hash2(i + float2(0, 0));
    float2 b = Hash2(i + float2(1, 0));
    float2 c = Hash2(i + float2(0, 1));
    float2 d = Hash2(i + float2(1, 1));

    float v1 = lerp(a.x, b.x, u.x);
    float v2 = lerp(c.x, d.x, u.x);
    return lerp(v1, v2, u.y) * 2.0 - 1.0; // -1..1
}

// Simple 2D cellular (Voronoi-style) noise: high near random cell centers
float CellNoise2D(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);

    float minDist = 1.0f;

    [unroll]
    for (int y = -1; y <= 1; ++y) {
        [unroll]
        for (int x = -1; x <= 1; ++x) {
            float2 offset = float2(x, y);
            float2 h = Hash2(i + offset);
            float2 cellPos = offset + h; // random point in neighboring cell
            float2 d = cellPos - f;
            float dist = dot(d, d);
            minDist = min(minDist, dist);
        }
    }

    // Map distance to 0..1, where 1 is center of a cell, 0 near edges
    float v = 1.0f - saturate(sqrt(minDist));
    return v * 2.0f - 1.0f; // -1..1
}

float FractalHeightBase(float2 p, float amplitude, float frequency, int octaves,
                        float lacunarity, float gain, float warpStrength, int noiseType) {
    // Optional domain warp for richer structure
    if (warpStrength > 0.0f) {
        float2 warp;
        warp.x = Noise2D(p + float2(37.2, 17.4));
        warp.y = Noise2D(p + float2(-11.1, 8.3));
        p += warp * warpStrength;
    }

    float h = 0.0f;
    float amp = amplitude;
    float freq = frequency;

    [unroll]
    for (int i = 0; i < 8; ++i) {
        if (i >= octaves) break;
        float n = Noise2D(p * freq);

        // Convert base noise to different fractal flavors while keeping it roughly in [-1,1]
        float v = n;
        if (noiseType == 1) {
            // Ridged fbm: sharp crests
            v = 2.0f * (1.0f - abs(n)) - 1.0f;
        } else if (noiseType == 2) {
            // Turbulence: absolute noise
            v = 2.0f * abs(n) - 1.0f;
        } else if (noiseType == 3) {
            // Cellular / Voronoi-style fragments
            v = CellNoise2D(p * freq);
        }

        h += v * amp;
        freq *= lacunarity;
        amp *= gain;
    }
    return h;
}

// Backward-compatible helper with default lacunarity/gain/warp/noiseType
float FractalHeight(float2 p, float amplitude, float frequency, int octaves) {
    return FractalHeightBase(p, amplitude, frequency, octaves, 2.0f, 0.5f, 0.0f, 0);
}

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

// Map a direction vector to lat-long environment UVs for an equirectangular
// panorama (2:1 aspect). Assumes a left-handed coordinate system with +Z
// forward, +X to the right, +Y up.
float2 DirectionToLatLong(float3 dir)
{
    dir = normalize(dir);

    // Guard against NaNs and degenerate directions
    if (!all(isfinite(dir))) {
        dir = float3(0.0f, 0.0f, 1.0f);
    }

    // Longitude: angle around the Y axis. Using -Z so that looking down +Z
    // corresponds to the center of the panorama rather than the seam.
    float phi = atan2(-dir.z, dir.x);              // [-PI, PI]

    // Latitude: angle above/below horizon. asin(y) is numerically stable for
    // values near 0 and keeps the poles at v = 0/1.
    float theta = asin(clamp(dir.y, -1.0f, 1.0f)); // [-PI/2, PI/2]

    float2 uv;
    uv.x = 0.5f + phi / (2.0f * PI);   // wrap around [0,1]
    uv.y = 0.5f - theta / PI;          // +Y up -> v decreasing
    return uv;
}

float SamplePCF(float2 shadowUV, float currentDepth, float bias, float pcfRadius, uint cascadeIndex)
{
    float2 texelSize = 1.0f / float2(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);

    float shadow = 0.0f;
    int samples = 0;

    [unroll]
    for (int x = -1; x <= 1; ++x)
    {
        [unroll]
        for (int y = -1; y <= 1; ++y)
        {
            float2 offset = float2(x, y) * texelSize * pcfRadius;
            float depthSample = g_ShadowMap.Sample(g_Sampler, float3(shadowUV + offset, cascadeIndex)).r;
            shadow += (currentDepth - bias > depthSample) ? 0.0f : 1.0f;
            samples++;
        }
    }

    return shadow / max(samples, 1);
}

  // Single-cascade shadow evaluation helper used by the main shadow function.
  float ComputeShadowCascade(float3 worldPos, float3 normal, uint cascadeIndex)
  {
      cascadeIndex = min(cascadeIndex, 2u);
  
      float4 lightClip = mul(g_LightViewProjection[cascadeIndex], float4(worldPos, 1.0f));
      if (lightClip.w <= 1e-4f || !all(isfinite(lightClip)))
      {
          // Treat degenerate projections as unshadowed to avoid streaks when
          // world positions fall outside the valid light frustum.
          return 1.0f;
      }
      float3 lightNDC = lightClip.xyz / lightClip.w;
  
      // Outside light frustum for this cascade
      if (lightNDC.x < -1.0f || lightNDC.x > 1.0f ||
          lightNDC.y < -1.0f || lightNDC.y > 1.0f ||
          lightNDC.z < 0.0f || lightNDC.z > 1.0f)
      {
          return 1.0f;
      }
  
      float2 shadowUV;
      shadowUV.x = 0.5f * lightNDC.x + 0.5f;
      shadowUV.y = -0.5f * lightNDC.y + 0.5f;
  
      float currentDepth = lightNDC.z;
  
      float bias = g_ShadowParams.x;
      float pcfRadius = g_ShadowParams.y;

      // Increase bias slightly for farther cascades where depth precision is lower.
      // Use a conservative scale so mid/far cascades stay stable without
      // excessively washing out contact shadows.
      float cascadeScale = 1.0f + (float)cascadeIndex * 0.5f;
      bias *= cascadeScale;
  
      // Simple slope-scaled bias to reduce acne
      float3 lightDirWS = normalize(g_Lights[0].direction_cosInner.xyz);
      float ndotl = saturate(dot(normal, lightDirWS));
      bias *= lerp(1.5f, 0.5f, ndotl);
  
      // Optional PCSS-style contact-hardening
      if (g_ShadowParams.w > 0.5f)
      {
          float2 texelSize = 1.0f / float2(SHADOW_MAP_SIZE, SHADOW_MAP_SIZE);
          float searchRadius = pcfRadius * 2.5f;
  
          float avgBlocker = 0.0f;
          int blockerCount = 0;
  
          [unroll]
          for (int x = -1; x <= 1; ++x)
          {
              [unroll]
              for (int y = -1; y <= 1; ++y)
              {
                  float2 offset = float2(x, y) * texelSize * searchRadius;
                  float depthSample = g_ShadowMap.Sample(g_Sampler, float3(shadowUV + offset, cascadeIndex)).r;
                  if (depthSample + bias < currentDepth)
                  {
                      avgBlocker += depthSample;
                      blockerCount++;
                  }
              }
          }
  
          if (blockerCount > 0)
          {
              avgBlocker /= blockerCount;
              float penumbra = saturate((currentDepth - avgBlocker) / max(avgBlocker, 1e-4f));
              pcfRadius *= (1.0f + penumbra * 4.0f);
          }
      }
  
      return SamplePCF(shadowUV, currentDepth, bias, pcfRadius, cascadeIndex);
  }
  
  // Cascaded shadow evaluation with cheap cross-fade between cascades near the split planes
  // to reduce visible popping/flicker on large objects.
  float ComputeShadow(float3 worldPos, float3 normal)
  {
      // Shadow disabled
      if (g_ShadowParams.z < 0.5f)
      {
          return 1.0f;
      }
  
      // View-space depth (camera looks down +Z)
      float3 viewPos = mul(g_ViewMatrix, float4(worldPos, 1.0f)).xyz;
      float depth = viewPos.z;
  
      float split0 = g_CascadeSplits.x;
      float split1 = g_CascadeSplits.y;
  
      // Choose primary cascade and optional secondary cascade to blend with
      uint primary = 0;
      uint secondary = 0;
      float blend = 0.0f;
  
      // Blend range in world units around each split; widened slightly so large objects
      // spanning multiple cascades see a smoother transition.
      float range0 = max(split0 * 0.2f, 4.0f);
      float range1 = max(split1 * 0.2f, 8.0f);
  
      if (depth <= split0)
      {
          primary = 0;
          secondary = 1;
          float d = split0 - depth;
          blend = saturate(1.0f - d / range0);
      }
      else if (depth <= split1)
      {
          // Between split0 and split1: blend against the closer boundary
          float d0 = depth - split0;
          float d1 = split1 - depth;
          primary = 1;
          if (d0 < d1)
          {
              secondary = 0;
              blend = saturate(1.0f - d0 / range0);
          }
          else
          {
              secondary = 2;
              blend = saturate(1.0f - d1 / range1);
          }
      }
      else
      {
          primary = 2;
          secondary = 1;
          float d = depth - split1;
          blend = saturate(1.0f - d / range1);
      }
  
      primary = min(primary, 2u);
      secondary = min(secondary, 2u);
  
      float shadowPrimary = ComputeShadowCascade(worldPos, normal, primary);
  
      // Outside blend zones or degenerate case
      if (blend <= 0.001f || primary == secondary)
      {
          return shadowPrimary;
      }
  
      float shadowSecondary = ComputeShadowCascade(worldPos, normal, secondary);
      return lerp(shadowPrimary, shadowSecondary, blend);
  }

  // Local light shadow evaluation for a shadow-mapped spotlight. The CPU maps
  // each selected local light to a dedicated slice in the shared shadow-map
  // array and writes its view-projection matrix into g_LightViewProjection.
  float ComputeLocalLightShadow(float3 worldPos, float3 normal, float3 lightDir, float shadowIndex)
  {
      // Shadows disabled globally
      if (g_ShadowParams.z < 0.5f)
      {
          return 1.0f;
      }

      // Light has no associated shadow slice
      if (shadowIndex < 0.0f)
      {
          return 1.0f;
      }

      uint slice = (uint)shadowIndex;
      // Clamp to the last valid matrix (supports cascades + several local lights).
      slice = min(slice, 5u);

      float4 lightClip = mul(g_LightViewProjection[slice], float4(worldPos, 1.0f));
      float3 lightNDC = lightClip.xyz / lightClip.w;

      // Outside the local light frustum
      if (lightNDC.x < -1.0f || lightNDC.x > 1.0f ||
          lightNDC.y < -1.0f || lightNDC.y > 1.0f ||
          lightNDC.z < 0.0f  || lightNDC.z > 1.0f)
      {
          return 1.0f;
      }

      float2 shadowUV;
      shadowUV.x = 0.5f * lightNDC.x + 0.5f;
      shadowUV.y = -0.5f * lightNDC.y + 0.5f;

      float currentDepth = lightNDC.z;

      // Use a slightly reduced bias/PCF radius for local lights so shadows
      // stay tight and avoid excessive peter-panning.
      float bias = g_ShadowParams.x * 0.5f;
      float pcfRadius = g_ShadowParams.y * 0.75f;

      float ndotl = saturate(dot(normalize(normal), normalize(lightDir)));
      bias *= lerp(1.5f, 0.5f, ndotl);

      return SamplePCF(shadowUV, currentDepth, bias, pcfRadius, slice);
  }

uint ComputeClusterZ(float viewZ)
{
    uint clusterCountZ = g_ClusterParams.x;
    if (clusterCountZ == 0u) {
        return 0u;
    }

    float nearZ = max(g_ProjectionParams.z, 1e-4f);
    float farZ = max(g_ProjectionParams.w, nearZ + 1e-3f);

    viewZ = max(viewZ, nearZ);
    float denom = log(max(farZ / nearZ, 1.0001f));
    float t = (denom > 0.0f) ? (log(viewZ / nearZ) / denom) : 0.0f;
    t = saturate(t);

    uint z = (uint)floor(t * (float)clusterCountZ);
    return min(z, clusterCountZ - 1u);
}

uint ComputeClusterIndex(int2 pixelCoord, float viewZ)
{
    uint width = max(g_ScreenAndCluster.x, 1u);
    uint height = max(g_ScreenAndCluster.y, 1u);
    uint clusterCountX = max(g_ScreenAndCluster.z, 1u);
    uint clusterCountY = max(g_ScreenAndCluster.w, 1u);

    uint tileW = (width + clusterCountX - 1u) / clusterCountX;
    uint tileH = (height + clusterCountY - 1u) / clusterCountY;
    tileW = max(tileW, 1u);
    tileH = max(tileH, 1u);

    uint px = (uint)clamp(pixelCoord.x, 0, (int)width - 1);
    uint py = (uint)clamp(pixelCoord.y, 0, (int)height - 1);

    uint cx = min(px / tileW, clusterCountX - 1u);
    uint cy = min(py / tileH, clusterCountY - 1u);
    uint cz = ComputeClusterZ(viewZ);

    uint clusterCountXY = clusterCountX * clusterCountY;
    return cx + cy * clusterCountX + cz * clusterCountXY;
}

float3 CalculateLighting(float3 normal, float3 worldPos, float3 albedo, float metallic, float roughness, float ao, float2 uv, int2 pixelCoord, float4 tangentWS, bool useClusteredLocalLights)
{
    float3 viewDir = normalize(g_CameraPosition.xyz - worldPos);

    // Material classification encoded in fractalParams1.w by the renderer:
    // 0 = default,
    // 1 = glass,
    // 2 = mirror,
    // 3 = plastic,
    // 4 = brick/masonry,
    // 5 = emissive / neon surface.
    float materialType    = g_FractalParams1.w;
    bool isGlass          = (materialType > 0.5f && materialType < 1.5f);
    bool isMirror         = (materialType > 1.5f && materialType < 2.5f);
    bool isPlastic        = (materialType > 2.5f && materialType < 3.5f);
    bool isBrick          = (materialType > 3.5f && materialType < 4.5f);
    bool isEmissive       = (materialType > 4.5f && materialType < 5.5f);
    bool anisoMetalFlag   = (materialType > 5.5f && materialType < 6.5f);
    bool anisoWoodFlag    = (materialType > 6.5f && materialType < 7.5f);
    float clearCoatWeight   = saturate(g_CoatParams.x);
    float clearCoatRoughness = saturate(g_CoatParams.y);
    float sheenWeight       = saturate(g_CoatParams.z);
    float sssWrap           = saturate(g_CoatParams.w);

    // KHR_materials_transmission: treat transmissive materials as "glass"
    // for BRDF energy split even if they are not using a preset material type.
    float transmission = saturate(g_TransmissionParams.x);
    if (g_TextureIndices3.x != INVALID_BINDLESS_INDEX && transmission > 0.0f)
    {
        float t = SampleBindlessTextureOrDefault(g_TextureIndices3.x, uv, float4(1, 1, 1, 1)).r;
        transmission *= t;
    }
    isGlass = isGlass || (transmission > 0.01f);

    // glTF clearcoat textures (KHR_materials_clearcoat)
    if (g_TextureIndices3.y != INVALID_BINDLESS_INDEX)
    {
        float cc = SampleBindlessTextureOrDefault(g_TextureIndices3.y, uv, float4(1, 1, 1, 1)).r;
        clearCoatWeight = saturate(clearCoatWeight * cc);
    }
    if (g_TextureIndices3.z != INVALID_BINDLESS_INDEX)
    {
        float4 ccr4 = SampleBindlessTextureOrDefault(g_TextureIndices3.z, uv, float4(0, 0, 0, 0));
        float ccr = (ccr4.g > 1e-5f) ? ccr4.g : ccr4.r;
        clearCoatRoughness = saturate(ccr);
    }

    float anisotropy = 0.0f;
    if (anisoMetalFlag)
    {
        anisotropy = 0.7f;
    }
    else if (anisoWoodFlag)
    {
        anisotropy = 0.5f;
    }

    // Reconstruct tangent and bitangent in world space for anisotropic
    // highlights; tangentWS.w encodes the bitangent sign.
    float3 T = normalize(tangentWS.xyz);
    float  bitangentSign = (tangentWS.w >= 0.0f) ? 1.0f : -1.0f;
    float3 B = normalize(cross(normal, T)) * bitangentSign;

    // Clamp metallic to leave a small diffuse contribution even for "full"
    // metals so that scenes remain readable under very dark environments.
    metallic = min(saturate(metallic), 0.95f);

    // Glass, plastic, brick, and emissive surfaces are always treated as dielectrics in the
    // BRDF (metallic = 0) so they rely on Fresnel and the specular lobe
    // rather than a colored metal F0. Their reflectivity is controlled via
    // roughness and the Fresnel term.
    if (isGlass || isPlastic || isBrick || isEmissive) {
        metallic = 0.0f;
    }
    roughness = saturate(roughness);

    // Simple specular anti-aliasing: when the normal field varies rapidly
    // across neighbouring pixels (for example, along the dragon scales),
    // increase effective roughness slightly so the specular lobe is wide
    // enough for TAA to stabilize without visible "sparkling".
    float3 n = normalize(normal);
    float3 dnDx = ddx(n);
    float3 dnDy = ddy(n);
    float normalVariance = dot(dnDx, dnDx) + dot(dnDy, dnDy);
    normalVariance = saturate(normalVariance);
    // Bias the variance term so flat regions remain unaffected while
    // highly curved areas get a modest roughness boost. The factor is kept
    // conservative so materials do not lose their intended gloss.
    float specAAStrength = 0.8f;
    float roughnessFromVariance = normalVariance * specAAStrength;

    // Raise roughness floor to soften extremely sharp highlights and reduce
    // specular aliasing when the camera moves quickly, then fold in the
    // variance-based adjustment. Highly polished metals and glass are given
    // a lower floor so mirror-like surfaces remain sharp, plastics sit in
    // the mid-range, and brick/masonry stays fairly rough.
    float minBaseRoughness;
    if (isGlass || metallic > 0.8f)
    {
        minBaseRoughness = 0.02f;
    }
    else if (isPlastic)
    {
        minBaseRoughness = 0.25f;
    }
    else if (isBrick)
    {
        minBaseRoughness = 0.45f;
    }
    else
    {
        minBaseRoughness = 0.2f;
    }
    float baseRoughness = max(roughness, minBaseRoughness);
    roughness = saturate(baseRoughness + roughnessFromVariance);
    ao = saturate(ao);

    // Dielectric F0 from IOR (KHR_materials_ior), modulated by KHR_materials_specular.
    float ior = max(g_TransmissionParams.y, 1.0f);
    float f0Ior = pow((ior - 1.0f) / max(ior + 1.0f, 1e-4f), 2.0f);
    float specFactor = saturate(g_SpecularParams.w);
    float3 specColor = saturate(g_SpecularParams.rgb);

    if (g_TextureIndices3.w != INVALID_BINDLESS_INDEX)
    {
        float4 s = SampleBindlessTextureOrDefault(g_TextureIndices3.w, uv, float4(1, 1, 1, 1));
        specFactor *= s.a;
    }
    if (g_TextureIndices4.x != INVALID_BINDLESS_INDEX)
    {
        float3 sc = SampleBindlessTextureOrDefault(g_TextureIndices4.x, uv, float4(1, 1, 1, 1)).rgb;
        specColor *= sc;
    }

    float3 dielectricF0 = f0Ior.xxx * specFactor * specColor;
    float3 F0 = lerp(dielectricF0, albedo, metallic);
    float3 totalLighting = 0.0f;

    bool clusterActive = false;
    uint lightCount = g_LightCount.x;

#ifdef ENABLE_BINDLESS
    clusterActive =
        useClusteredLocalLights &&
        (g_ClusterParams.z > 0u) &&
        (g_ClusterParams.x > 0u) &&
        (g_ClusterSRVIndices.x != INVALID_BINDLESS_INDEX) &&
        (g_ClusterSRVIndices.y != INVALID_BINDLESS_INDEX) &&
        (g_ClusterSRVIndices.z != INVALID_BINDLESS_INDEX);

    // When clustered light lists are available, keep the classic forward loop
    // for the sun only to avoid double-counting local lights.
    if (clusterActive) {
        lightCount = min(lightCount, 1u);
    }
#endif

    [loop]
    for (uint i = 0; i < lightCount; ++i)
    {
        Light light = g_Lights[i];
        uint type = (uint)light.position_type.w;

        float3 lightDir;
        float attenuation = 1.0f;
        float3 radiance = light.color_range.rgb;

        const bool isPointLike = (type == LIGHT_TYPE_POINT ||
                                  type == LIGHT_TYPE_SPOT  ||
                                  type == LIGHT_TYPE_AREA_RECT);

        if (isPointLike)
        {
            float3 toLight = light.position_type.xyz - worldPos;
            float dist = length(toLight);
            if (dist <= 1e-4f)
            {
                continue;
            }
            lightDir = toLight / dist;

            float range = max(light.color_range.w, 0.001f);
            float falloff = saturate(1.0f - dist / range);
            attenuation = falloff * falloff;

            if (type == LIGHT_TYPE_SPOT)
            {
                float3 spotDir = normalize(light.direction_cosInner.xyz);
                float cosTheta = dot(-lightDir, spotDir);
                float cosInner = light.direction_cosInner.w;
                float cosOuter = light.params.x;
                float spotFactor = saturate((cosTheta - cosOuter) / max(cosInner - cosOuter, 1e-4f));
                attenuation *= spotFactor * spotFactor;
            }
            else if (type == LIGHT_TYPE_AREA_RECT)
            {
                // Rectangular area lights approximate a softbox: clamp the
                // minimum attenuation so they do not fall off as aggressively
                // as point lights and stay visually present across a larger
                // portion of the scene.
                attenuation = max(attenuation, 0.35f);
            }
        }
        else
        {
            // Directional light
            lightDir = normalize(light.direction_cosInner.xyz);
        }

        float3 halfDir = normalize(viewDir + lightDir);

        float NdotL = saturate(dot(normal, lightDir));
        float NdotV = saturate(dot(normal, viewDir));
        float NdotH = saturate(dot(normal, halfDir));
        float VdotH = saturate(dot(viewDir, halfDir));

        if (NdotL <= 0.0f || NdotV <= 0.0f)
        {
            continue;
        }

        float3 F = FresnelSchlick(VdotH, F0);
        float  D;
        float  roughForLight = roughness;
        // Area lights produce broader highlights; approximate this by using
        // a slightly higher effective roughness for their specular lobe so
        // they read as large soft sources in the studio / Cornell scenes.
        if (type == LIGHT_TYPE_AREA_RECT)
        {
            roughForLight = saturate(roughness * 1.5f + 0.05f);
        }
        if (anisotropy > 0.01f)
        {
            // Anisotropic GGX distribution using different roughness along
            // tangent and bitangent directions. Geometry term remains the
            // isotropic Smith approximation.
            float alpha  = roughForLight * roughForLight;
            float alphaX = alpha * (1.0f + anisotropy);
            float alphaY = alpha / max(1.0f + anisotropy, 1e-3f);

            float3 H = halfDir;
            float3 Ht = float3(dot(H, T), dot(H, B), dot(H, normal));

            float denomH = Ht.x * Ht.x / (alphaX * alphaX) +
                           Ht.y * Ht.y / (alphaY * alphaY) +
                           Ht.z * Ht.z / (alpha  * alpha);
            denomH = max(denomH, 1e-4f);
            float denom2 = denomH * denomH;
            D = 1.0f / (PI * alphaX * alphaY * denom2);
        }
        else
        {
            D = DistributionGGX(NdotH, roughForLight);
        }
        float G = GeometrySmith(NdotV, NdotL, roughForLight);

        float3 numerator = D * G * F;
        float  denom = max(4.0f * NdotV * NdotL, 1e-4f);
        float3 specular = numerator / denom;

        // Optional clear-coat layer: a thin glossy dielectric top layer over
        // the base BRDF used for painted plastics and polished metals. This
        // reuses the GGX lobes with a lower roughness and fixed F0 so that
        // highlights stay tight and energetic without exploding HDR.
        float coatWeight = clearCoatWeight;
        float coatRough  = clearCoatRoughness;
        if (coatWeight > 0.01f)
        {
            float3 F_coat = FresnelSchlick(VdotH, float3(0.04f, 0.04f, 0.04f));
            float  D_coat = DistributionGGX(NdotH, coatRough);
            float  G_coat = GeometrySmith(NdotV, NdotL, coatRough);
            float3 specCoat = (D_coat * G_coat * F_coat) / denom;

            // Blend base and coat lobes; keep the coat slightly energy-
            // limited so we do not double-count all incoming light.
            float coatBlend = coatWeight * 0.8f;
            specular = lerp(specular, specCoat, coatBlend);
        }
        // Clamp per-light specular to avoid extreme spikes that can manifest
        // as full-object color pops when multiple colored lights are present.
        specular = min(specular, 4.0f.xxx);

        // Gently emphasize the specular lobe for plastic so that highlights
        // feel a bit more like a coated surface without introducing a full
        // second clear-coat BRDF layer.
        if (isPlastic)
        {
            specular *= 1.25f;
        }

        // Optional cloth/sheen term: adds a soft grazing-angle highlight
        // suitable for fabric/velvet presets. Uses coatParams.z as a simple
        // weight so presets can enable or tune the effect without changing
        // the material layout.
        float3 sheenColor = albedo;
        if (sheenWeight > 0.01f)
        {
            float oneMinusNL = 1.0f - NdotL;
            float oneMinusNV = 1.0f - NdotV;
            float sheen = pow(saturate(oneMinusNL), 4.0f) * pow(saturate(oneMinusNV), 4.0f);
            float3 sheenTerm = sheenWeight * sheen * sheenColor * radiance * attenuation;
            // Sheen is purely additive on top of the base BRDF.
            specular += sheenTerm / max(NdotL, 1e-4f);
        }

        float3 kd = (1.0f - F) * (1.0f - metallic);
        // Glass is dominated by specular; keep only a very small diffuse
        // term so that tinted glass can still contribute slightly to GI
        // without washing out reflections.
        if (isGlass) {
            kd *= 0.1f;
        }
        float3 diffuse = kd * albedo / PI;

        // Very simple SSS / wrap-diffuse for skin-like materials. Instead of
        // changing the geometry term we scale the Lambert diffuse so that the
        // effective NdotL behaves like a wrapped cosine, keeping specular
        // unchanged. coatParams.w carries the wrap factor (0 = classic
        // Lambert, ~0.3 = noticeably wrapped).
        if (sssWrap > 0.01f)
        {
            float lambert = max(NdotL, 1e-4f);
            float wrapped = saturate((NdotL + sssWrap) / (1.0f + sssWrap));
            float scale = wrapped / lambert;
            diffuse *= scale;
        }

        float3 lightColor = radiance;

        float3 contribution = (diffuse + specular) * lightColor * NdotL * attenuation;

        // Softly compress per-light luminance to keep extremely bright spots
        // from dominating the frame and fighting temporal filtering. Using a
        // smooth rolloff avoids hard thresholds that can cause subtle popping
        // when NdotL or attenuation crosses the clamp. The knee is set high
        // enough that typical highlights remain largely linear.
        float  lum = dot(contribution, float3(0.299f, 0.587f, 0.114f));
        const float kLightLuminanceKnee = 64.0f;
        if (lum > 1e-3f)
        {
            float compress = kLightLuminanceKnee / (lum + kLightLuminanceKnee);
            contribution *= compress;
        }

        // Apply shadows from the cascaded directional sun and a single
        // shadow-mapped local spotlight (if present).
        if (type == LIGHT_TYPE_DIRECTIONAL)
        {
            // Sun shadows: only the primary directional light (index 0) uses
            // cascaded shadow maps. When the RT path is enabled (toggled via V
            // and gated on DXR support), we allow a ray-traced shadow mask to
            // override the cascaded result in screen space.
            if (i == 0)
            {
                // Sun shadows from cascaded shadow maps.
                float shadow = ComputeShadow(worldPos, normal);

                // When RT sun shadows are enabled (postParams.w > 0.5), replace
                // the cascaded result with a spatially-filtered, temporally-
                // smoothed RT mask sampled in screen space. History blending
                // is only applied once the CPU has populated the history buffer
                // at least once.
                if (g_PostParams.w > 0.5f)
                {
                    // Small cross-shaped spatial filter (center + 4 neighbors)
                    // to soften per-frame RT noise while preserving contact
                    // detail better than a full 3x3 blur.
                    static const int2 offsets[5] = {
                        int2( 0,  0),
                        int2( 1,  0),
                        int2(-1,  0),
                        int2( 0,  1),
                        int2( 0, -1)
                    };

                    float sum = 0.0f;
                    float count = 0.0f;

                    // Derive approximate render size from 1 / resolution stored
                    // in postParams.xy so we can clamp sampling at image edges.
                    float width  = 1.0f / max(g_PostParams.x, 1e-6f);
                    float height = 1.0f / max(g_PostParams.y, 1e-6f);

                    [unroll]
                    for (int oi = 0; oi < 5; ++oi)
                    {
                        int2 p = pixelCoord + offsets[oi];
                        if (p.x < 0 || p.y < 0 ||
                            p.x >= (int)width || p.y >= (int)height)
                        {
                            continue;
                        }
                        sum += g_RtShadowMask.Load(int3(p, 0));
                        count += 1.0f;
                    }

                    float rtShadow = (count > 0.0f)
                        ? (sum / count)
                        : g_RtShadowMask.Load(int3(pixelCoord, 0));

                    // debugMode.w is used as a simple "RT history valid" flag,
                    // written by the renderer once the first mask->history copy
                    // has been performed.
                    bool hasHistory = (g_DebugMode.w > 0.5f);
                    if (hasHistory)
                    {
                        float history = g_RtShadowMaskHistory.Load(int3(pixelCoord, 0));
                        float diff = abs(rtShadow - history);

                        // Conservative temporal accumulation: reduced max weight
                        // from 0.6 to 0.25 and increased diff sensitivity from
                        // 4.0 to 10.0 to aggressively reject history at shadow
                        // boundaries and when objects move. This eliminates the
                        // ghosting/afterimage artifacts visible in corners and
                        // on moving objects while retaining enough temporal
                        // smoothing to reduce single-pixel ray tracing noise.
                        float historyWeight = lerp(0.25f, 0.0f, saturate(diff * 10.0f));
                        rtShadow = lerp(rtShadow, history, historyWeight);
                    }

                    shadow = rtShadow;
                }

                contribution *= shadow;
            }
        }
        else
        {
            float shadowIndex = light.params.y;
            if (shadowIndex >= 0.0f)
            {
                float shadow = ComputeLocalLightShadow(worldPos, normal, lightDir, shadowIndex);
                contribution *= shadow;
            }
        }

        totalLighting += contribution;
    }

#ifdef ENABLE_BINDLESS
    // Forward+ for transparent materials: accumulate clustered local lights.
    if (clusterActive)
    {
        StructuredBuffer<Light> localLights = ResourceDescriptorHeap[g_ClusterSRVIndices.x];
        StructuredBuffer<uint2> clusterRanges = ResourceDescriptorHeap[g_ClusterSRVIndices.y];
        StructuredBuffer<uint> clusterLightIndices = ResourceDescriptorHeap[g_ClusterSRVIndices.z];

        float viewZ = mul(g_ViewMatrix, float4(worldPos, 1.0f)).z;
        uint clusterIndex = ComputeClusterIndex(pixelCoord, viewZ);
        uint2 range = clusterRanges[clusterIndex];

        uint base = range.x;
        uint count = min(range.y, g_ClusterParams.y);

        [loop]
        for (uint li = 0; li < count; ++li)
        {
            uint lightIndex = clusterLightIndices[base + li];
            if (lightIndex >= g_ClusterParams.z) {
                continue;
            }

            Light light = localLights[lightIndex];
            uint type = (uint)light.position_type.w;
            if (type == LIGHT_TYPE_DIRECTIONAL) {
                continue;
            }

            float3 lightDir;
            float attenuation = 1.0f;
            float3 radiance = light.color_range.rgb;

            const bool isPointLike = (type == LIGHT_TYPE_POINT ||
                                      type == LIGHT_TYPE_SPOT  ||
                                      type == LIGHT_TYPE_AREA_RECT);

            if (isPointLike)
            {
                float3 toLight = light.position_type.xyz - worldPos;
                float dist = length(toLight);
                if (dist <= 1e-4f)
                {
                    continue;
                }
                lightDir = toLight / dist;

                float rangeMeters = max(light.color_range.w, 0.001f);
                float falloff = saturate(1.0f - dist / rangeMeters);
                attenuation = falloff * falloff;

                if (type == LIGHT_TYPE_SPOT)
                {
                    float3 spotDir = normalize(light.direction_cosInner.xyz);
                    float cosTheta = dot(-lightDir, spotDir);
                    float cosInner = light.direction_cosInner.w;
                    float cosOuter = light.params.x;
                    float spotFactor = saturate((cosTheta - cosOuter) / max(cosInner - cosOuter, 1e-4f));
                    attenuation *= spotFactor * spotFactor;
                }
                else if (type == LIGHT_TYPE_AREA_RECT)
                {
                    attenuation = max(attenuation, 0.35f);
                }
            }
            else
            {
                lightDir = normalize(light.direction_cosInner.xyz);
            }

            float3 halfDir = normalize(viewDir + lightDir);

            float NdotL = saturate(dot(normal, lightDir));
            float NdotV = saturate(dot(normal, viewDir));
            float NdotH = saturate(dot(normal, halfDir));
            float VdotH = saturate(dot(viewDir, halfDir));

            if (NdotL <= 0.0f || NdotV <= 0.0f)
            {
                continue;
            }

            float3 F = FresnelSchlick(VdotH, F0);
            float  D;
            float  roughForLight = roughness;
            if (type == LIGHT_TYPE_AREA_RECT)
            {
                roughForLight = saturate(roughness * 1.5f + 0.05f);
            }
            if (anisotropy > 0.01f)
            {
                float alpha  = roughForLight * roughForLight;
                float alphaX = alpha * (1.0f + anisotropy);
                float alphaY = alpha / max(1.0f + anisotropy, 1e-3f);

                float3 H = halfDir;
                float3 Ht = float3(dot(H, T), dot(H, B), dot(H, normal));

                float denomH = Ht.x * Ht.x / (alphaX * alphaX) +
                               Ht.y * Ht.y / (alphaY * alphaY) +
                               Ht.z * Ht.z / (alpha  * alpha);
                denomH = max(denomH, 1e-4f);
                float denom2 = denomH * denomH;
                D = 1.0f / (PI * alphaX * alphaY * denom2);
            }
            else
            {
                D = DistributionGGX(NdotH, roughForLight);
            }
            float G = GeometrySmith(NdotV, NdotL, roughForLight);

            float3 numerator = D * G * F;
            float  denom = max(4.0f * NdotV * NdotL, 1e-4f);
            float3 specular = numerator / denom;

            float coatWeight = clearCoatWeight;
            if (coatWeight > 0.001f)
            {
                float coatRough = saturate(clearCoatRoughness);
                float3 F_coat = FresnelSchlick(VdotH, float3(0.04f, 0.04f, 0.04f));
                float  D_coat = DistributionGGX(NdotH, coatRough);
                float  G_coat = GeometrySmith(NdotV, NdotL, coatRough);

                float3 coatSpec = (D_coat * G_coat * F_coat) / max(4.0f * NdotV * NdotL, 1e-4f);
                coatSpec = min(coatSpec, 4.0f.xxx);

                specular = lerp(specular, specular * (1.0f - coatWeight) + coatSpec * coatWeight, coatWeight);
            }

            specular = min(specular, 4.0f.xxx);
            if (isPlastic)
            {
                specular *= 1.25f;
            }

            float3 sheenColor = albedo;
            if (sheenWeight > 0.01f)
            {
                float oneMinusNL = 1.0f - NdotL;
                float oneMinusNV = 1.0f - NdotV;
                float sheen = pow(saturate(oneMinusNL), 4.0f) * pow(saturate(oneMinusNV), 4.0f);
                float3 sheenTerm = sheenWeight * sheen * sheenColor * radiance * attenuation;
                specular += sheenTerm / max(NdotL, 1e-4f);
            }

            float3 kd = (1.0f - F) * (1.0f - metallic);
            if (isGlass) {
                kd *= 0.1f;
            }
            float3 diffuse = kd * albedo / PI;

            if (sssWrap > 0.01f)
            {
                float lambert = max(NdotL, 1e-4f);
                float wrapped = saturate((NdotL + sssWrap) / (1.0f + sssWrap));
                float scale = wrapped / lambert;
                diffuse *= scale;
            }

            float3 contribution = (diffuse + specular) * radiance * NdotL * attenuation;

            float  lum = dot(contribution, float3(0.299f, 0.587f, 0.114f));
            const float kLightLuminanceKnee = 64.0f;
            if (lum > 1e-3f)
            {
                float compress = kLightLuminanceKnee / (lum + kLightLuminanceKnee);
                contribution *= compress;
            }

            float shadowIndex = light.params.y;
            if (shadowIndex >= 0.0f)
            {
                float shadow = ComputeLocalLightShadow(worldPos, normal, lightDir, shadowIndex);
                contribution *= shadow;
            }

            totalLighting += contribution;
        }
    }
#endif

    // Image-based lighting (IBL) using environment maps when available.
    // Expose the debug view mode for the entire lighting function so that
    // both IBL-specialized views and RT GI gating can branch on it.
    uint debugView = (uint)g_DebugMode.x;
    float3 ambient = 0.0f;
    float3 diffuseIBL = 0.0f;
    float3 specularIBL = 0.0f;
    if (g_EnvParams.z > 0.5f)
    {
        float3 N = normalize(normal);
        float3 V = normalize(g_CameraPosition.xyz - worldPos);
        float NdotV = saturate(dot(N, V));

        // Diffuse IBL from low-frequency environment in equirectangular form.
        float2 envUV = DirectionToLatLong(N);
        float3 irradiance = g_EnvDiffuse.SampleLevel(g_Sampler, envUV, 0.0f).rgb;

        float3 kd = (1.0f - metallic) * (1.0f - FresnelSchlickRoughness(NdotV, F0, roughness));
        diffuseIBL = irradiance * kd * albedo;

        // Specular IBL from prefiltered environment mip chain (mip-mapped
        // equirectangular texture).
        uint specWidth, specHeight, specMips;
        g_EnvSpecular.GetDimensions(0, specWidth, specHeight, specMips);
        float maxMip = specMips > 0 ? float(specMips - 1) : 0.0f;

        float3 R = reflect(-V, N);
        float2 specUV = DirectionToLatLong(R);
        float3 prefiltered = g_EnvSpecular.SampleLevel(g_Sampler, specUV, roughness * maxMip).rgb;

        float3 Fibl = FresnelSchlickRoughness(NdotV, F0, roughness);
        specularIBL = prefiltered * Fibl;

        float diffuseIntensity = g_EnvParams.x;
        float specularIntensity = g_EnvParams.y;

        ambient = (diffuseIBL * diffuseIntensity + specularIBL * specularIntensity) * ao;

        // IBL-only debug modes
        if (debugView == 8)
        {
            // Diffuse IBL-only debug: clamp to avoid extreme flashes.
            float3 dbg = diffuseIBL * diffuseIntensity * ao;
            dbg = min(dbg, 32.0f.xxx);
            return dbg;
        }
        else if (debugView == 9)
        {
            // Specular IBL-only debug: clamp to avoid extreme flashes.
            float3 dbg = specularIBL * specularIntensity * ao;
            dbg = min(dbg, 32.0f.xxx);
            return dbg;
        }
        else if (debugView == 11)
        {
            // Visualize Fresnel term at view angle (metals stay bright at grazing).
            return saturate(Fibl);
        }
        else if (debugView == 12)
        {
            // Visualize roughness -> mip mapping (0 = sharpest, 1 = blurriest).
            float mipVis = (specMips > 1) ? (roughness * maxMip) / maxMip : roughness;
            return mipVis.xxx;
        }
    }
    else
    {
        // Fallback: flat ambient term
        ambient = g_AmbientColor.rgb * albedo * ao;
    }

    // Optional RT diffuse GI: when the DXR path is active (toggled via V and
    // gated on device support), sample the RT GI buffer as a low-frequency
    // ambient/IBL occlusion term rather than an additive light source. The
    // GI pass encodes visibility in alpha (0 = fully occluded, 1 = fully
    // visible), with rgb containing a debug-only color. The RT GI buffers
    // are allocated at half-resolution relative to the main render target,
    // so we convert from full-resolution pixel coordinates to GI texel
    // coordinates when sampling.
    float giVisibility = 1.0f;
    // To reduce single-sample noise, apply a small 5-tap cross filter in
    // screen space over the RT GI alpha channel. We always allow the current
    // GI buffer to influence shading when RTX is enabled; g_DebugMode.w is
    // used only to gate history blending once the CPU has populated the
    // GI history buffer at least once.
    if (g_PostParams.w > 0.5f && debugView != 22u)
    {
        static const int2 offsets[5] = {
            int2( 0,  0),
            int2( 1,  0),
            int2(-1,  0),
            int2( 0,  1),
            int2( 0, -1)
        };

        float sum = 0.0f;
        float count = 0.0f;

        // Derive approximate render size from 1 / resolution stored in
        // postParams.xy so we can clamp sampling at image edges. The GI
        // buffers are half-resolution, so use half of the main render size
        // for bounds.
        float fullWidth  = 1.0f / max(g_PostParams.x, 1e-6f);
        float fullHeight = 1.0f / max(g_PostParams.y, 1e-6f);
        float giWidth    = fullWidth * 0.5f;
        float giHeight   = fullHeight * 0.5f;

        // Convert the full-resolution pixel coordinate to the GI buffer's
        // texel space and build a small cross-shaped kernel around it.
        int2 giBase = pixelCoord / 2;

        [unroll]
        for (int i = 0; i < 5; ++i)
        {
            int2 p = giBase + offsets[i];
            if (p.x < 0 || p.y < 0 ||
                p.x >= (int)giWidth || p.y >= (int)giHeight)
            {
                continue;
            }
            float a = g_RtGI.Load(int3(p, 0)).a;
            sum += a;
            count += 1.0f;
        }

        float giCurr = (count > 0.0f) ? saturate(sum / count) : 1.0f;

        // Temporal accumulation: blend with a simple history buffer when
        // available to reduce frame-to-frame noise from single-sample RT GI.
        if (g_DebugMode.w > 0.5f)
        {
            float giHistory = g_RtGIHistory.Load(int3(giBase, 0)).a;
            float diff = abs(giCurr - giHistory);
            // When GI is stable, lean more on history; when it changes a lot,
            // trust the new sample to avoid visible ghosts/afterimages.
            float historyWeight = lerp(0.7f, 0.1f, saturate(diff * 4.0f));
            giVisibility = lerp(giCurr, giHistory, historyWeight);
        }
        else
        {
            giVisibility = giCurr;
        }

        // Soften RT GI influence to reduce visible flicker: treat the raw
        // visibility as a suggestion and blend it back towards 1.0 so that
        // ambient is only gently modulated by the RT term. Also clamp to a
        // minimum so GI never fully crushes ambient, which helps avoid
        // "breathing" artifacts when rays flip between occluded / clear.
        const float kGiStrength = 0.10f;
        giVisibility = lerp(1.0f, giVisibility, kGiStrength);
        giVisibility = max(giVisibility, 0.8f);
    }

    // Apply GI as an occlusion term on top of the existing ambient/IBL
    // contribution so that indirect lighting is reduced in crevices and
    // contact zones instead of simply increasing total energy.
    ambient *= giVisibility;

    // Clamp combined direct + ambient radiance to a sane range before HDR,
    // to reduce extreme spikes that can cause visible "flashes" when the
    // camera moves rapidly across very bright highlights.
    float3 color = totalLighting + ambient;
    // Global safety net for extreme spikes; most shaping is handled by the
    // per-light soft clamp above, so this is rarely hit.
    const float kMaxRadiance = 96.0f;
    color = min(color, kMaxRadiance.xxx);
    return color;
}

// Combined pixel output: main HDR color + packed normal/roughness for SSR/post
struct PSOutput
{
    float4 color : SV_Target0;
    float4 normalRoughness : SV_Target1;
};

PSOutput MakePSOutput(float4 color, float3 normal, float roughness)
{
    PSOutput o;
    float3 nEnc = normalize(normal) * 0.5f + 0.5f;
    o.color = color;
    o.normalRoughness = float4(nEnc, roughness);
    return o;
}

// === Debug line rendering (overlay) ===

struct DebugVSInput
{
    float3 position : POSITION;
    float4 color    : COLOR0;
};

struct DebugPSInput
{
    float4 position : SV_POSITION;
    float4 color    : COLOR0;
};

DebugPSInput DebugLineVS(DebugVSInput input)
{
    DebugPSInput output;
    float4 worldPos = float4(input.position, 1.0f);
    // Use the non-jittered view-projection for debug lines/gizmos so they
    // remain rock-solid on screen and do not inherit TAA jitter. The main
    // scene still uses the jittered matrix in the standard VS path.
    output.position = mul(g_ViewProjectionNoJitter, worldPos);
    output.color = input.color;
    return output;
}

float4 DebugLinePS(DebugPSInput input) : SV_TARGET
{
    return input.color;
}

// Pixel Shader
PSOutput PSMainInternal(PSInput input, bool useClusteredLocalLights)
{
    // Sample albedo texture using bindless sampling (falls back to legacy if no bindless index)
    bool hasAlbedoMap = (g_TextureIndices.x != INVALID_BINDLESS_INDEX) || g_MapFlags.x;
    float4 albedoSample = hasAlbedoMap ? SampleAlbedo(input.texCoord) : float4(1.0f, 1.0f, 1.0f, 1.0f);

    // Biome terrain handling: vertex color encodes biome indices and blend weights
    float3 albedo;
    float biomeRoughness = 0.0f;
    float biomeMetallic = 0.0f;
    bool isBiomeTerrain = IsBiomeTerrain(input.color) && g_BiomeCount > 0;

    if (isBiomeTerrain) {
        // Sample biome material using world position and normal
        BiomeSurfaceData biomeSurface = SampleBiomeMaterial(input.worldPos, input.normal, input.color);
        // Use biome-computed color (includes height layers, slope blending)
        albedo = saturate(albedoSample.rgb * g_Albedo.rgb * biomeSurface.albedo.rgb);
        biomeRoughness = biomeSurface.roughness;
        biomeMetallic = biomeSurface.metallic;
    } else {
        // Non-terrain: apply vertex color as a simple tint multiplier
        albedo = saturate(albedoSample.rgb * g_Albedo.rgb * input.color.rgb);
    }

    float  baseOpacity = saturate(albedoSample.a * g_Albedo.a);

    // Alpha-masked (cutout) materials use g_MaterialPad0 as the cutoff.
    // Keep alpha-blended materials in the transparent pass (no cutoff).
    if (g_MaterialPad0 > 0.0f)
    {
        clip(baseOpacity - g_MaterialPad0);
    }

    // KHR_materials_transmission: approximate transmission by driving opacity.
    // This keeps transmissive glTF materials in the transparent pass and
    // prevents them from rendering as solid opaque surfaces.
    float transmission = saturate(g_TransmissionParams.x);
#ifdef ENABLE_BINDLESS
    if (g_TextureIndices3.x != INVALID_BINDLESS_INDEX && transmission > 0.0f)
    {
        Texture2D transTex = ResourceDescriptorHeap[g_TextureIndices3.x];
        transmission *= transTex.Sample(g_Sampler, input.texCoord).r;
    }
#endif
    baseOpacity *= (1.0f - transmission);

    bool hasMetallicMap = (g_TextureIndices.z != INVALID_BINDLESS_INDEX) || g_MapFlags.z;
    bool hasRoughnessMap = (g_TextureIndices.w != INVALID_BINDLESS_INDEX) || g_MapFlags.w;

    float metallic = g_Metallic;
    float roughness = g_Roughness;

#ifdef ENABLE_BINDLESS
    // glTF metallic-roughness convention:
    // - When metallic + roughness refer to the same texture, treat it as packed:
    //   roughness = G, metallic = B.
    const bool packedMR =
        hasMetallicMap && hasRoughnessMap &&
        (g_TextureIndices.z != INVALID_BINDLESS_INDEX) &&
        (g_TextureIndices.z == g_TextureIndices.w);
    if (packedMR)
    {
        float4 mr = SampleBindlessTextureOrDefault(g_TextureIndices.z, input.texCoord, float4(0, 0, 0, 0));
        roughness = mr.g;
        metallic  = mr.b;
    }
    else
#endif
    {
        if (hasMetallicMap)  metallic  = SampleMetallic(input.texCoord).r;
        if (hasRoughnessMap) roughness = SampleRoughness(input.texCoord).r;
    }

    // Apply biome material overrides for terrain (when no texture maps override)
    if (isBiomeTerrain) {
        // Only apply biome values if no explicit texture maps are provided
        if (!hasMetallicMap) metallic = biomeMetallic;
        if (!hasRoughnessMap) roughness = biomeRoughness;
    }

    float ao = g_AO;
    float occlusionStrength = saturate(g_ExtraParams.x);

    // Normal mapping using bindless sampling
    float3 normal = normalize(input.normal);
    bool hasNormalMap = (g_TextureIndices.y != INVALID_BINDLESS_INDEX) || g_MapFlags.y;
    if (hasNormalMap) {
        float3 tangent = normalize(input.tangent.xyz);
        float bitangentSign = (input.tangent.w >= 0.0f) ? 1.0f : -1.0f;
        float3 bitangent = normalize(cross(normal, tangent)) * bitangentSign;
        float3x3 TBN = float3x3(tangent, bitangent, normal);
        float3 nSample = SampleNormal(input.texCoord).xyz * 2.0f - 1.0f;
        // glTF normalScale: scale X/Y and renormalize.
        float normalScale = max(g_ExtraParams.y, 0.0f);
        nSample.xy *= normalScale;
        normal = normalize(mul(TBN, nSample));
    }

#ifdef ENABLE_BINDLESS
    // Occlusion texture (glTF): stored in R, applied only to indirect.
    if (g_TextureIndices2.x != INVALID_BINDLESS_INDEX && occlusionStrength > 0.0f)
    {
        Texture2D occTex = ResourceDescriptorHeap[g_TextureIndices2.x];
        float occ = occTex.Sample(g_Sampler, input.texCoord).r;
        ao *= lerp(1.0f, occ, occlusionStrength);
    }
#endif

    // Fractal normal perturbation (normal-only "bump" using procedural heightfield)
    float amplitude  = g_FractalParams0.x;
    float frequency  = g_FractalParams0.y;
    int   octaves    = (int)g_FractalParams0.z;
    bool  useFractal = (g_FractalParams0.w > 0.5f);
    float lacunarity   = max(g_FractalParams2.x, 1.0f);
    float gain         = saturate(g_FractalParams2.y);
    float warpStrength = max(g_FractalParams2.z, 0.0f);
    int   noiseType    = (int)g_FractalParams2.w;

    if (useFractal && amplitude > 0.0001f && octaves > 0) {
        float mode = g_FractalParams1.x; // 0 = UV, 1 = world XZ
        float2 scale = float2(g_FractalParams1.y, g_FractalParams1.z);
        float2 p;

        if (mode < 0.5f) {
            p = input.texCoord * scale;
        } else {
            p = float2(input.worldPos.x, input.worldPos.z) * scale;
        }

        float h = FractalHeightBase(p, amplitude, frequency, octaves, lacunarity, gain, warpStrength, noiseType);

        float eps = 0.01f;
        float hx = FractalHeightBase(p + float2(eps, 0), amplitude, frequency, octaves, lacunarity, gain, warpStrength, noiseType);
        float hz = FractalHeightBase(p + float2(0, eps), amplitude, frequency, octaves, lacunarity, gain, warpStrength, noiseType);

        float dhdx = (hx - h) / eps;
        float dhdz = (hz - h) / eps;

        // Build a simple tangent frame from the current normal
        float3 N = normalize(normal);
        float3 up = float3(0, 1, 0);
        float3 T = normalize(cross(up, N));
        // If N is nearly parallel to up, pick an alternate up vector
        if (all(abs(T) < 1e-3)) {
            up = float3(0, 0, 1);
            T = normalize(cross(up, N));
        }
        float3 B = normalize(cross(N, T));

        float3 perturbed = normalize(N - dhdx * T - dhdz * B);
        normal = perturbed;
    }

    // Debug views
    uint debugView = (uint)g_DebugMode.x;
    if (debugView == 1)
    {
        float3 nVis = normalize(normal) * 0.5f + 0.5f;
        return MakePSOutput(float4(nVis, 1.0f), normal, roughness);
    }
    else if (debugView == 2)
    {
        float3 rVis = roughness.xxx;
        return MakePSOutput(float4(rVis, 1.0f), normal, roughness);
    }
    else if (debugView == 3)
    {
        float3 mVis = metallic.xxx;
        return MakePSOutput(float4(mVis, 1.0f), normal, roughness);
    }
    else if (debugView == 4)
    {
        return MakePSOutput(float4(albedo, baseOpacity), normal, roughness);
    }
    else if (debugView == 5)
    {
        float3 viewPos = mul(g_ViewMatrix, float4(input.worldPos, 1.0f)).xyz;
        float depth = viewPos.z;
        uint cascadeIndex = 0;
        if (depth > g_CascadeSplits.x) cascadeIndex = 1;
        if (depth > g_CascadeSplits.y) cascadeIndex = 2;
        cascadeIndex = min(cascadeIndex, 2u);

        float3 colors[3] = {
            float3(1, 0, 0),
            float3(0, 1, 0),
            float3(0, 0, 1)
        };
        return MakePSOutput(float4(colors[cascadeIndex], 1.0f), normal, roughness);
    }
    else if (debugView == 7)
    {
        // Visualize fractal height as greyscale
        float amplitude  = g_FractalParams0.x;
        float frequency  = g_FractalParams0.y;
        int   octaves    = (int)g_FractalParams0.z;
        float lacunarity   = max(g_FractalParams2.x, 1.0f);
        float gain         = saturate(g_FractalParams2.y);
        float warpStrength = max(g_FractalParams2.z, 0.0f);
        int   noiseType    = (int)g_FractalParams2.w;

        float mode = g_FractalParams1.x; // 0 = UV, 1 = world XZ
        float2 scale = float2(g_FractalParams1.y, g_FractalParams1.z);

        float2 p;
        if (mode < 0.5f) {
            p = input.texCoord * scale;
        } else {
            p = float2(input.worldPos.x, input.worldPos.z) * scale;
        }

        float amp = max(amplitude, 0.001f);
        int   oct = max(octaves, 1);

        float h = FractalHeightBase(p, amp, frequency, oct, lacunarity, gain, warpStrength, noiseType);
        float v = 0.5f + h / (2.0f * amp); // map [-amp,amp] -> [0,1]
        v = saturate(v);
        return MakePSOutput(float4(v, v, v, 1.0f), normal, roughness);
    }
    else if (debugView == 10)
    {
        // DirectionToLatLong debug: visualize env UVs as color
        float3 N = normalize(normal);
        float2 uv = DirectionToLatLong(N);
        return MakePSOutput(float4(uv.x, uv.y, 0.0f, 1.0f), normal, roughness);
    }
    else if (debugView == 17)
    {
        // Forward-light debug: visualize how many lights significantly affect
        // this point and the strongest light's color. This helps you see which
        // LightComponent objects are actually contributing illumination and
        // how their ranges overlap.
        uint lightCount = g_LightCount.x;
        float3 viewDirDbg = normalize(g_CameraPosition.xyz - input.worldPos);

        uint activeLights = 0;
        float maxLuma = 0.0f;
        float3 maxColor = 0.0f;

        [loop]
        for (uint i = 0; i < lightCount; ++i)
        {
            Light light = g_Lights[i];
            uint type = (uint)light.position_type.w;

            float3 lightDir;
            float attenuation = 1.0f;

            if (type == LIGHT_TYPE_POINT || type == LIGHT_TYPE_SPOT || type == LIGHT_TYPE_AREA_RECT)
            {
                float3 toLight = light.position_type.xyz - input.worldPos;
                float dist = length(toLight);
                if (dist <= 1e-4f)
                {
                    continue;
                }
                lightDir = toLight / dist;

                float range = max(light.color_range.w, 0.001f);
                float falloff = saturate(1.0f - dist / range);
                attenuation = falloff * falloff;

                if (type == LIGHT_TYPE_SPOT)
                {
                    float3 spotDir = normalize(light.direction_cosInner.xyz);
                    float cosTheta = dot(-lightDir, spotDir);
                    float cosInner = light.direction_cosInner.w;
                    float cosOuter = light.params.x;
                    float spotFactor = saturate((cosTheta - cosOuter) / max(cosInner - cosOuter, 1e-4f));
                    attenuation *= spotFactor * spotFactor;
                }
            }
            else
            {
                // Directional light: treat as always in range.
                lightDir = normalize(light.direction_cosInner.xyz);
            }

            float NdotL = saturate(dot(normalize(normal), lightDir));
            if (NdotL <= 0.0f || attenuation <= 1e-4f)
            {
                continue;
            }

            float3 contrib = light.color_range.rgb * (NdotL * attenuation);
            float luma = dot(contrib, float3(0.299f, 0.587f, 0.114f));
            if (luma > 1e-3f)
            {
                activeLights++;
                if (luma > maxLuma)
                {
                    maxLuma = luma;
                    maxColor = light.color_range.rgb;
                }
            }
        }

        float lightsNorm = (LIGHT_MAX > 1) ? (min(activeLights, LIGHT_MAX) / (float)(LIGHT_MAX)) : 0.0f;
        float brightness = saturate(maxLuma / 10.0f);

        // Encode: R = strongest light color weighted by brightness,
        // G = normalized active light count,
        // B = brightness.
        float3 dbgColor = float3(
            saturate(maxColor.r * brightness),
            lightsNorm,
            brightness);

        return MakePSOutput(float4(dbgColor, 1.0f), normal, roughness);
    }
    else if (debugView == 18)
    {
        // Visualize the raw RT shadow mask as grayscale. When RT is disabled
        // or no RT pipeline is available, show a neutral gray instead of
        // sampling potentially uninitialized resources.
        if (g_PostParams.w <= 0.5f)
        {
            return MakePSOutput(float4(0.5f, 0.5f, 0.5f, 1.0f), normal, roughness);
        }
        int2 pixelCoord = int2(input.position.xy);
        float v = g_RtShadowMask.Load(int3(pixelCoord, 0));
        return MakePSOutput(float4(v, v, v, 1.0f), normal, roughness);
    }
    else if (debugView == 19)
    {
        // Visualize the RT shadow history buffer as grayscale. Guard on both
        // RT being enabled and history being marked valid by the renderer
        // (debugMode.w > 0.5).
        if (g_PostParams.w <= 0.5f || g_DebugMode.w <= 0.5f)
        {
            return MakePSOutput(float4(0.5f, 0.5f, 0.5f, 1.0f), normal, roughness);
        }
        int2 pixelCoord = int2(input.position.xy);
        float v = g_RtShadowMaskHistory.Load(int3(pixelCoord, 0));
        return MakePSOutput(float4(v, v, v, 1.0f), normal, roughness);
    }
    else if (debugView == 21)
    {
        // RT diffuse GI-only debug view. Shows the temporally-filtered GI
        // visibility term (alpha) as grayscale so it matches what the main
        // shading path actually uses instead of the raw, noisier buffer.
        if (g_PostParams.w <= 0.5f)
        {
            return MakePSOutput(float4(0.0f, 0.0f, 0.0f, 1.0f), normal, roughness);
        }
        int2 pixelCoord = int2(input.position.xy);
        // RT GI buffers are half-resolution; convert from full-res pixel
        // coordinates to GI texel coordinates before sampling.
        int2 giCoord = pixelCoord / 2;
        float aCurr = g_RtGI.Load(int3(giCoord, 0)).a;
        float a = aCurr;
        // Only blend against history once the CPU has populated the GI
        // history buffer and flagged it as valid via g_DebugMode.w.
        if (g_DebugMode.w > 0.5f)
        {
            float aHist = g_RtGIHistory.Load(int3(giCoord, 0)).a;
            float diff = abs(aCurr - aHist);
            float historyWeight = lerp(0.7f, 0.1f, saturate(diff * 4.0f));
            a = lerp(aCurr, aHist, historyWeight);
        }
        return MakePSOutput(float4(a.xxx, 1.0f), normal, roughness);
    }
    else if (debugView == 26)
    {
        // Material layers debug: visualize clear-coat, sheen, and SSS wrap
        // packed into RGB so you can quickly see where each effect is active.
        float clearCoatWeight   = saturate(g_CoatParams.x);
        float sheenWeight       = saturate(g_CoatParams.z);
        float sssWrap           = saturate(g_CoatParams.w);
        float3 layerVis = float3(clearCoatWeight, sheenWeight, sssWrap);
        return MakePSOutput(float4(layerVis, 1.0f), normal, roughness);
    }
    else if (debugView == 27)
    {
        // Anisotropy debug: encode tangent direction in RG and anisotropy
        // strength in B so brushed metals / wood grain can be inspected.
        float materialType    = g_FractalParams1.w;
        bool  anisoMetalFlag  = (materialType > 5.5f && materialType < 6.5f);
        bool  anisoWoodFlag   = (materialType > 6.5f && materialType < 7.5f);
        float anisotropy      = anisoMetalFlag ? 0.7f : (anisoWoodFlag ? 0.5f : 0.0f);

        // Reconstruct tangent in world space from the interpolated normal and
        // tangent attributes; this mirrors the work done in CalculateLighting
        // closely enough for debug visualization.
        float3 N = normalize(normal);
        float3 tangentWS = normalize(mul(g_NormalMatrix, float4(input.tangent.xyz, 0.0f)).xyz);
        float3 T = tangentWS;
        float3 tVis = normalize(T) * 0.5f + 0.5f;
        float3 debugColor = float3(tVis.x, tVis.y, anisotropy);
        return MakePSOutput(float4(debugColor, 1.0f), normal, roughness);
    }

    int2 pixelCoord = int2(input.position.xy);
    float3 color = CalculateLighting(normal, input.worldPos, albedo, metallic, roughness, ao, input.texCoord, pixelCoord, input.tangent, useClusteredLocalLights);

    // glTF emissive: emissiveFactor * emissiveStrength * emissiveTexture.
    float3 emissive = max(g_EmissiveFactorStrength.rgb, 0.0f) * max(g_EmissiveFactorStrength.w, 0.0f);
#ifdef ENABLE_BINDLESS
    if (g_TextureIndices2.y != INVALID_BINDLESS_INDEX)
    {
        Texture2D emTex = ResourceDescriptorHeap[g_TextureIndices2.y];
        emissive *= emTex.Sample(g_Sampler, input.texCoord).rgb;
    }
#endif
    color += emissive;

    // Emissive / neon materials: add a simple self-illumination term derived
    // from the albedo color so that glowing panels and signs contribute
    // visible light and feed into bloom/tonemapping. The preset encodes
    // emission strength in the alpha channel of the albedo color.
    float materialType   = g_FractalParams1.w;
    bool isGlass         = (materialType > 0.5f && materialType < 1.5f);
    bool isMirror        = (materialType > 1.5f && materialType < 2.5f);
    bool isPlastic       = (materialType > 2.5f && materialType < 3.5f);
    bool isBrick         = (materialType > 3.5f && materialType < 4.5f);
    bool isEmissive      = (materialType > 4.5f && materialType < 5.5f);
    bool anisoMetalFlag2 = (materialType > 5.5f && materialType < 6.5f);
    bool anisoWoodFlag2  = (materialType > 6.5f && materialType < 7.5f);
    if (isEmissive)
    {
        // Treat albedo alpha as a normalized emission control and map it
        // into a practical intensity range so emissive presets can request
        // brighter or dimmer surfaces without exploding HDR.
        float emissiveControl = baseOpacity;
        float emissiveIntensity = lerp(2.0f, 12.0f, emissiveControl);
        float3 emissiveColor = albedo;
        color += emissiveColor * emissiveIntensity;
    }

    // Per-material TAA weight: opaque materials encode a simple temporal
    // accumulation preference into the alpha channel so the TAA resolve can
    // down-weight history on problematic surfaces (mirrors, glass, strong
    // emissive panels) without adding a dedicated G-buffer. Transparent
    // materials keep their physical opacity (< 0.99) so refraction and
    // blending in the post-process pass continue to behave as before.
    float taaWeight = 1.0f;
    if (isGlass || isMirror)
    {
        // Mirrors / pure glass rely on SSR/RT and per-frame shading; even a
        // small amount of history can leave visible multi-exposures.
        taaWeight = 0.0f;
    }
    else if (isEmissive)
    {
        taaWeight = 0.25f;
    }
    else if (isPlastic)
    {
        taaWeight = 0.7f;
    }
    else if (anisoMetalFlag2 || anisoWoodFlag2)
    {
        taaWeight = 0.5f;
    }
    else if (isBrick)
    {
        taaWeight = 1.0f;
    }

    float finalOpacity = baseOpacity;
    if (baseOpacity >= 0.99f)
    {
        // Opaque materials: remap TAA weight into a tight 0.99-1.0 window so
        // the post-process can recover it while still treating the surface
        // as fully opaque (opacity < 0.99 is the transparent cutoff).
        finalOpacity = 0.99f + 0.01f * saturate(taaWeight);
    }

    // Output linear HDR color; exposure/tonemapping is applied in a post-process pass.
    return MakePSOutput(float4(color, finalOpacity), normal, roughness);
}

PSOutput PSMain(PSInput input)
{
    return PSMainInternal(input, false);
}

// Transparent-only variant: render HDR color without writing normal/roughness.
// This keeps the post stack's normal/roughness buffer stable for opaque/VB.
float4 PSMainTransparent(PSInput input) : SV_Target0
{
    return PSMainInternal(input, true).color;
}

// === Skybox full-screen pass using the same FrameConstants / IBL maps ===
struct SkyboxVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
};

SkyboxVSOutput SkyboxVS(uint vertexId : SV_VertexID)
{
    SkyboxVSOutput output;

    float2 pos;
    if (vertexId == 0)      pos = float2(-1.0f, -1.0f);
    else if (vertexId == 1) pos = float2(-1.0f,  3.0f);
    else                    pos = float2( 3.0f, -1.0f);

    output.position = float4(pos, 0.0f, 1.0f);

    // Map NDC to UV (0..1), matching the convention used in post-process.
    output.uv = float2(0.5f * pos.x + 0.5f, -0.5f * pos.y + 0.5f);

    return output;
}

float4 SkyboxPS(SkyboxVSOutput input) : SV_TARGET
{
    // Reconstruct a world-space direction from screen UV. We go via inverse
    // projection (view space) and then undo the view rotation so that the
    // environment sampling is independent of camera FOV and position.
    float2 uv = input.uv;
    float x = uv.x * 2.0f - 1.0f;
    float y = 1.0f - 2.0f * uv.y;
    float4 viewH = mul(g_InvProjectionMatrix, float4(x, y, 1.0f, 1.0f));
    float3 viewDir = normalize(viewH.xyz);

    float3x3 viewRot = (float3x3)g_ViewMatrix;
    float3x3 invViewRot = transpose(viewRot);
    float3 dir = normalize(mul(invViewRot, viewDir));

    // Debug: show raw environment textures in screen space to verify they
    // loaded correctly, without any spherical mapping.
    if ((uint)g_DebugMode.x == 8)
    {
        float3 tex = g_EnvDiffuse.SampleLevel(g_Sampler, input.uv, 0.0f).rgb;
        return float4(tex, 1.0f);
    }
    if ((uint)g_DebugMode.x == 9)
    {
        float3 tex = g_EnvSpecular.SampleLevel(g_Sampler, input.uv, 0.0f).rgb;
        return float4(tex, 1.0f);
    }

    // Debug mode 10: visualize sky direction as a simple gradient so we can
    // verify that the view->world reconstruction and mapping agree.
    if ((uint)g_DebugMode.x == 10)
    {
        float3 dbg = 0.5f * (dir + 1.0f);
        return float4(dbg, 1.0f);
    }

    // If IBL is disabled, fall back to flat ambient
    if (g_EnvParams.z <= 0.5f)
    {
        return float4(g_AmbientColor.rgb, 1.0f);
    }

    // Proper 3D environment sampling: convert the world-space direction to
    // lat-long UVs for the equirectangular panorama so the background rotates
    // correctly with the camera.
    float2 skyUV = DirectionToLatLong(dir);
    float3 color = g_EnvSpecular.SampleLevel(g_Sampler, skyUV, 0.0f).rgb * g_EnvParams.y;
    return float4(color, 1.0f);
}
