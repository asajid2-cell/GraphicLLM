// Basic PBR-style vertex and pixel shaders
// Implements forward rendering with texture support

// Constant buffers - must match ShaderTypes.h
cbuffer ObjectConstants : register(b0)
{
    float4x4 g_ModelMatrix;
    float4x4 g_NormalMatrix;
};

cbuffer FrameConstants : register(b1)
{
    float4x4 g_ViewMatrix;
    float4x4 g_ProjectionMatrix;
    float4x4 g_ViewProjectionMatrix;
    float4x4 g_InvProjectionMatrix;
    float4 g_CameraPosition;
    // x = time, y = deltaTime, z = exposure, w = unused
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
    // x = warm tint (-1..1), y = cool tint (-1..1), z,w reserved
    float4 g_ColorGrade;
    // x = fog density, y = base height, z = height falloff, w = fog enabled (>0.5)
    float4 g_FogParams;
    // x = SSAO enabled (>0.5), y = radius, z = bias, w = intensity
    float4 g_AOParams;
    // x = jitterX, y = jitterY, z = TAA blend factor, w = TAA enabled (>0.5)
    float4 g_TAAParams;
    float4x4 g_PrevViewProjMatrix;
    float4x4 g_InvViewProjMatrix;
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
    uint4 g_MapFlags;      // x: albedo, y: normal, z: metallic, w: roughness
    float4 g_FractalParams0; // x=amplitude, y=frequency, z=octaves, w=useFractalNormal
    float4 g_FractalParams1; // x=coordMode (0=UV,1=worldXZ), y=scaleX, z=scaleZ, w=reserved
    float4 g_FractalParams2; // x=lacunarity, y=gain, z=warpStrength, w=noiseType (0=fbm,1=ridged,2=turb)
};

// Texture and sampler
Texture2D g_AlbedoTexture : register(t0);
Texture2D g_NormalTexture : register(t1);
Texture2D g_MetallicTexture : register(t2);
Texture2D g_RoughnessTexture : register(t3);
// Shadow map + IBL textures are bound in a separate descriptor table (space1)
// to avoid overlapping with per-pass textures in space0.
Texture2DArray g_ShadowMap : register(t0, space1);
Texture2D g_EnvDiffuse : register(t1, space1);
Texture2D g_EnvSpecular : register(t2, space1);
SamplerState g_Sampler : register(s0);

// Vertex shader input
struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
};

// Vertex shader output / Pixel shader input
struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
    float4 tangent : TANGENT;
    float2 texCoord : TEXCOORD;
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

    // Transform normal to world space
    output.normal = normalize(mul(g_NormalMatrix, float4(input.normal, 0.0f)).xyz);
    float3 tangentWS = normalize(mul(g_NormalMatrix, float4(input.tangent.xyz, 0.0f)).xyz);
    output.tangent = float4(tangentWS, input.tangent.w);

    // Pass through texture coordinates
    output.texCoord = input.texCoord;

    return output;
}

// Depth-only vertex shader for directional shadow map
struct VSShadowOutput
{
    float4 position : SV_POSITION;
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

    return output;
}

static const float PI = 3.14159265f;
static const float SHADOW_MAP_SIZE = 2048.0f;
static const uint LIGHT_TYPE_DIRECTIONAL = 0;
static const uint LIGHT_TYPE_POINT       = 1;
static const uint LIGHT_TYPE_SPOT        = 2;

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

float DistributionGGX(float NdotH, float roughness)
{
    float a     = max(roughness * roughness, 0.04f);
    float a2    = a * a;
    float denom = (NdotH * NdotH * (a2 - 1.0f) + 1.0f);
    return a2 / max(PI * denom * denom, 1e-4f);
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = max(roughness + 1.0f, 1.0f);
    float k = (r * r) / 8.0f;
    return NdotV / max(NdotV * (1.0f - k) + k, 1e-4f);
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float gv = GeometrySchlickGGX(NdotV, roughness);
    float gl = GeometrySchlickGGX(NdotL, roughness);
    return gv * gl;
}

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
}

// Slightly modified Fresnel for image-based lighting that
// softens the response at high roughness to avoid overly dark metals.
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    float3 oneMinusR = 1.0f - roughness;
    float3 F90 = saturate(F0 + (1.0f - F0) * 0.5f);
    return F0 + (F90 - F0) * pow(saturate(1.0f - cosTheta), 5.0f);
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
      // This reduces shimmering on large objects that live mostly in mid/far cascades.
      float cascadeScale = 1.0f + (float)cascadeIndex * 1.5f;
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

float3 CalculateLighting(float3 normal, float3 worldPos, float3 albedo, float metallic, float roughness, float ao)
{
    float3 viewDir = normalize(g_CameraPosition.xyz - worldPos);

    metallic = saturate(metallic);
    // Raise roughness floor to soften extremely sharp highlights and reduce
    // specular aliasing when the camera moves quickly.
    roughness = max(saturate(roughness), 0.2f);
    ao = saturate(ao);

    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 totalLighting = 0.0f;

    uint lightCount = g_LightCount.x;

    [loop]
    for (uint i = 0; i < lightCount; ++i)
    {
        Light light = g_Lights[i];
        uint type = (uint)light.position_type.w;

        float3 lightDir;
        float attenuation = 1.0f;
        float3 radiance = light.color_range.rgb;

        if (type == LIGHT_TYPE_POINT || type == LIGHT_TYPE_SPOT)
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
        float D = DistributionGGX(NdotH, roughness);
        float G = GeometrySmith(NdotV, NdotL, roughness);

        float3 numerator = D * G * F;
        float  denom = max(4.0f * NdotV * NdotL, 1e-4f);
        float3 specular = numerator / denom;
        // Clamp per-light specular to avoid extreme spikes that can manifest
        // as full-object color pops when multiple colored lights are present.
        specular = min(specular, 4.0f.xxx);

        float3 kd = (1.0f - F) * (1.0f - metallic);
        float3 diffuse = kd * albedo / PI;

        float3 lightColor = radiance;

        float3 contribution = (diffuse + specular) * lightColor * NdotL * attenuation;

        // Apply shadows from the cascaded directional sun and a single
        // shadow-mapped local spotlight (if present).
        if (type == LIGHT_TYPE_DIRECTIONAL)
        {
            // Sun shadows: only the primary directional light (index 0) uses
            // cascaded shadow maps. When the RT path is enabled (toggled via V
            // and gated on DXR support), we currently treat the sun as
            // unshadowed in the raster path; upcoming DXR passes will write a
            // ray-traced shadow mask instead.
            if (i == 0)
            {
                float shadow = ComputeShadow(worldPos, normal);
                if (g_PostParams.w > 0.5f)
                {
                    shadow = 1.0f;
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

    // Image-based lighting (IBL) using environment maps when available.
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
        uint debugView = (uint)g_DebugMode.x;
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

    // Clamp combined direct + ambient radiance to a sane range before HDR,
    // to reduce extreme spikes that can cause visible "flashes" when the
    // camera moves rapidly across very bright highlights.
    float3 color = totalLighting + ambient;
    const float kMaxRadiance = 64.0f;
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
    output.position = mul(g_ViewProjectionMatrix, worldPos);
    output.color = input.color;
    return output;
}

float4 DebugLinePS(DebugPSInput input) : SV_TARGET
{
    return input.color;
}

// Pixel Shader
PSOutput PSMain(PSInput input)
{
    // Sample albedo texture
    float4 albedoSample = g_MapFlags.x ? g_AlbedoTexture.Sample(g_Sampler, input.texCoord) : float4(1.0f, 1.0f, 1.0f, 1.0f);
    float3 albedo = saturate(albedoSample.rgb * g_Albedo.rgb);

    float metallic = g_MapFlags.z ? g_MetallicTexture.Sample(g_Sampler, input.texCoord).r : g_Metallic;
    float roughness = g_MapFlags.w ? g_RoughnessTexture.Sample(g_Sampler, input.texCoord).r : g_Roughness;
    float ao = g_AO;

    // Normal mapping
    float3 normal = normalize(input.normal);
    if (g_MapFlags.y) {
        float3 tangent = normalize(input.tangent.xyz);
        float bitangentSign = (input.tangent.w >= 0.0f) ? 1.0f : -1.0f;
        float3 bitangent = normalize(cross(normal, tangent)) * bitangentSign;
        float3x3 TBN = float3x3(tangent, bitangent, normal);
        float3 nSample = g_NormalTexture.Sample(g_Sampler, input.texCoord).xyz * 2.0f - 1.0f;
        normal = normalize(mul(TBN, nSample));
    }

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
        return MakePSOutput(float4(albedo, albedoSample.a * g_Albedo.a), normal, roughness);
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

            if (type == LIGHT_TYPE_POINT || type == LIGHT_TYPE_SPOT)
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

    float3 color = CalculateLighting(normal, input.worldPos, albedo, metallic, roughness, ao);

    // Output linear HDR color; exposure/tonemapping is applied in a post-process pass.
    return MakePSOutput(float4(color, albedoSample.a * g_Albedo.a), normal, roughness);
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
