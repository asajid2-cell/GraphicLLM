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
    float _padding2;
};

// Texture and sampler
Texture2D g_AlbedoTexture : register(t0);
SamplerState g_Sampler : register(s0);

// Vertex shader input
struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 texCoord : TEXCOORD;
};

// Vertex shader output / Pixel shader input
struct PSInput
{
    float4 position : SV_POSITION;
    float3 worldPos : WORLD_POSITION;
    float3 normal : NORMAL;
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

    // Pass through texture coordinates
    output.texCoord = input.texCoord;

    return output;
}

// Simple lighting calculation (Blinn-Phong for now, can upgrade to PBR later)
float3 CalculateLighting(float3 normal, float3 worldPos, float3 albedo)
{
    // Hardcoded directional light (we'll make this dynamic in Phase 2)
    float3 lightDir = normalize(float3(0.5f, 1.0f, 0.3f));
    float3 lightColor = float3(1.0f, 1.0f, 1.0f);

    // Ambient
    float3 ambient = 0.1f * albedo;

    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0f);
    float3 diffuse = diff * albedo * lightColor;

    // Specular (Blinn-Phong)
    float3 viewDir = normalize(g_CameraPosition.xyz - worldPos);
    float3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0f), 32.0f);
    float3 specular = spec * lightColor * (1.0f - g_Roughness);

    return ambient + diffuse + specular;
}

// Pixel Shader
float4 PSMain(PSInput input) : SV_TARGET
{
    // Sample albedo texture
    float4 albedoSample = g_AlbedoTexture.Sample(g_Sampler, input.texCoord);

    // Combine with material color
    float3 albedo = albedoSample.rgb * g_Albedo.rgb;

    // Calculate lighting
    float3 normal = normalize(input.normal);
    float3 color = CalculateLighting(normal, input.worldPos, albedo);

    // Apply ambient occlusion
    color *= g_AO;

    return float4(color, albedoSample.a * g_Albedo.a);
}
