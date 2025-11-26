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
    float4 g_CameraPosition;
    // x = time, y = deltaTime, z = exposure, w = unused
    float4 g_TimeAndExposure;
    // rgb: ambient color * intensity, w unused
    float4 g_AmbientColor;
    uint4 g_LightCount;
    // Forward lights (light 0 is the sun)
    struct Light
    {
        float4 position_type;        // xyz = position (for point/spot), w = type
        float4 direction_cosInner;   // xyz = direction, w = inner cone cos (spot)
        float4 color_range;          // rgb = color * intensity, w = range (point/spot)
        float4 params;               // x = outer cone cos, y = shadow index, z,w reserved
    };
    Light g_Lights[4];
    // Cascaded directional light view-projection matrices (we use first 3)
    float4x4 g_LightViewProjection[4];
    // x,y,z = cascade split depths in view space, w = far plane
    float4 g_CascadeSplits;
    // x = depth bias, y = PCF radius in texels, z = shadows enabled (>0.5), w unused
    float4 g_ShadowParams;
    // x = debug view mode (0 = shaded, 1 = normals, 2 = roughness, 3 = metallic, 4 = albedo, 5 = cascade index), others reserved
    float4 g_DebugMode;
    // x = 1 / screenWidth, y = 1 / screenHeight, z = FXAA enabled (>0.5), w reserved
    float4 g_PostParams;
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
Texture2DArray g_ShadowMap : register(t4);
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
    uint cascadeIndex = g_ShadowCascadeIndex.x;
    cascadeIndex = min(cascadeIndex, 2u);
    output.position = mul(g_LightViewProjection[cascadeIndex], worldPos);

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

float ComputeShadow(float3 worldPos, float3 normal)
{
    // Shadow disabled
    if (g_ShadowParams.z < 0.5f)
    {
        return 1.0f;
    }

    // Determine cascade based on view-space depth
    float3 viewPos = mul(g_ViewMatrix, float4(worldPos, 1.0f)).xyz;
    float depth = viewPos.z;

    uint cascadeIndex = 0;
    if (depth > g_CascadeSplits.x) cascadeIndex = 1;
    if (depth > g_CascadeSplits.y) cascadeIndex = 2;
    cascadeIndex = min(cascadeIndex, 2u);

    float4 lightClip = mul(g_LightViewProjection[cascadeIndex], float4(worldPos, 1.0f));
    float3 lightNDC = lightClip.xyz / lightClip.w;

    // Outside light frustum
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

float3 CalculateLighting(float3 normal, float3 worldPos, float3 albedo, float metallic, float roughness, float ao)
{
    float3 viewDir = normalize(g_CameraPosition.xyz - worldPos);

    metallic = saturate(metallic);
    roughness = max(saturate(roughness), 0.04f);
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

        float3 kd = (1.0f - F) * (1.0f - metallic);
        float3 diffuse = kd * albedo / PI;

        float3 lightColor = radiance;

        float3 contribution = (diffuse + specular) * lightColor * NdotL * attenuation;

        // Apply shadowing only for sun (light 0) for now
        if (i == 0 && type == LIGHT_TYPE_DIRECTIONAL)
        {
            float shadow = ComputeShadow(worldPos, normal);
            contribution *= shadow;
        }

        totalLighting += contribution;
    }

    float3 ambient = g_AmbientColor.rgb * albedo * ao;

    return totalLighting + ambient;
}

// Pixel Shader
float4 PSMain(PSInput input) : SV_TARGET
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
    uint debugMode = (uint)g_DebugMode.x;
    if (debugMode == 1)
    {
        float3 nVis = normalize(normal) * 0.5f + 0.5f;
        return float4(nVis, 1.0f);
    }
    else if (debugMode == 2)
    {
        float3 rVis = roughness.xxx;
        return float4(rVis, 1.0f);
    }
    else if (debugMode == 3)
    {
        float3 mVis = metallic.xxx;
        return float4(mVis, 1.0f);
    }
    else if (debugMode == 4)
    {
        return float4(albedo, albedoSample.a * g_Albedo.a);
    }
    else if (debugMode == 5)
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
        return float4(colors[cascadeIndex], 1.0f);
    }
    else if (debugMode == 7)
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
        return float4(v, v, v, 1.0f);
    }

    float3 color = CalculateLighting(normal, input.worldPos, albedo, metallic, roughness, ao);

    // Output linear HDR color; exposure/tonemapping is applied in a post-process pass
    return float4(color, albedoSample.a * g_Albedo.a);
}
