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
    float g_Time;
    float g_DeltaTime;
    float2 _padding;
};

cbuffer MaterialConstants : register(b2)
{
    float4 g_Albedo;
    float g_Metallic;
    float g_Roughness;
    float g_AO;
    uint4 g_MapFlags; // x: albedo, y: normal, z: metallic, w: roughness
};

// Texture and sampler
Texture2D g_AlbedoTexture : register(t0);
Texture2D g_NormalTexture : register(t1);
Texture2D g_MetallicTexture : register(t2);
Texture2D g_RoughnessTexture : register(t3);
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

float3 CalculateLighting(float3 normal, float3 worldPos, float3 albedo, float metallic, float roughness, float ao)
{
    float3 lightDir = normalize(float3(0.5f, 1.0f, 0.3f));
    float3 lightColor = float3(1.0f, 1.0f, 1.0f);

    float3 viewDir = normalize(g_CameraPosition.xyz - worldPos);
    float3 halfDir = normalize(viewDir + lightDir);

    float NdotL = saturate(dot(normal, lightDir));
    float NdotV = saturate(dot(normal, viewDir));
    float NdotH = saturate(dot(normal, halfDir));
    float VdotH = saturate(dot(viewDir, halfDir));

    metallic = saturate(metallic);
    roughness = saturate(roughness);
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);
    float3 F = FresnelSchlick(VdotH, F0);

    float D = DistributionGGX(NdotH, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);

    float3 numerator = D * G * F;
    float  denom = max(4.0f * NdotV * NdotL, 1e-4f);
    float3 specular = numerator / denom;

    float3 kd = (1.0f - F) * (1.0f - metallic);
    float3 diffuse = kd * albedo / PI;

    float3 color = (diffuse + specular) * lightColor * NdotL;
    float3 ambient = 0.03f * albedo * ao;

    return color + ambient;
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
        normal = normalize(mul(nSample, TBN));
    }

    metallic = saturate(metallic);
    roughness = max(saturate(roughness), 0.04f);
    ao = saturate(ao);

    float3 color = CalculateLighting(normal, input.worldPos, albedo, metallic, roughness, ao);
    color = ApplyACESFilm(color);
    color = pow(color, 1.0f / 2.2f);

    return float4(saturate(color), albedoSample.a * g_Albedo.a);
}
