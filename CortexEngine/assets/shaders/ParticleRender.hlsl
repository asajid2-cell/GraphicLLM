// ParticleRender.hlsl
// Vertex and pixel shaders for GPU particle rendering.
// Supports billboards, stretched billboards, soft particles, and texture atlases.

#include "Common.hlsli"

// Render modes
#define RENDER_MODE_BILLBOARD           0
#define RENDER_MODE_STRETCHED           1
#define RENDER_MODE_HORIZONTAL          2
#define RENDER_MODE_VERTICAL            3
#define RENDER_MODE_MESH                4

// Blend modes (encoded in flags)
#define BLEND_MODE_ALPHA                0
#define BLEND_MODE_ADDITIVE             1
#define BLEND_MODE_PREMULTIPLIED        2
#define BLEND_MODE_MULTIPLY             3

// Particle structure (must match GPUParticles.h)
struct Particle {
    float4 position;        // xyz=pos, w=size
    float4 velocity;        // xyz=vel, w=rotation
    float4 color;           // rgba
    float4 params;          // x=age, y=lifetime, z=emitterIdx, w=seed
    float4 params2;         // x=rotSpeed, y=sizeStart, z=sizeEnd, w=flags
    float4 params3;         // x=colorLerp, y=gravityMod, z=dragCoeff, w=unused
    float4 sortKey;         // x=depth, yzw=unused
};

// Render constant buffer
cbuffer RenderCB : register(b0) {
    float4x4 g_ViewProj;
    float4x4 g_View;
    float4x4 g_InvView;
    float4 g_CameraPosition;
    float4 g_CameraRight;
    float4 g_CameraUp;
    float4 g_CameraForward;

    float4 g_AtlasParams;       // x=rows, y=cols, z=totalFrames, w=animSpeed
    float4 g_SoftParams;        // x=softDistance, y=nearFade, z=farFade, w=enabled

    uint g_RenderMode;
    uint g_MaxParticles;
    float g_StretchFactor;
    float g_Time;
};

// Particle buffer
StructuredBuffer<Particle> g_Particles : register(t0);

// Sorted indices
StructuredBuffer<uint> g_SortedIndices : register(t1);

// Particle texture atlas
Texture2D<float4> g_ParticleTexture : register(t2);

// Depth buffer for soft particles
Texture2D<float> g_DepthTexture : register(t3);

// Normal map (optional)
Texture2D<float4> g_NormalTexture : register(t4);

SamplerState g_LinearSampler : register(s0);
SamplerState g_PointSampler : register(s1);

// Vertex shader output
struct VSOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 texCoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float depth : TEXCOORD2;
    nointerpolation uint emitterIdx : TEXCOORD3;
    nointerpolation float normalizedAge : TEXCOORD4;
};

// Generate billboard vertex positions
float3 GetBillboardPosition(float3 particlePos, float2 corner, float size, float rotation, float3 velocity) {
    float3 right, up;

    if (g_RenderMode == RENDER_MODE_BILLBOARD) {
        // Camera-facing billboard
        right = g_CameraRight.xyz;
        up = g_CameraUp.xyz;

        // Apply rotation
        float s = sin(rotation);
        float c = cos(rotation);
        float3 rotRight = right * c + up * s;
        float3 rotUp = up * c - right * s;
        right = rotRight;
        up = rotUp;
    }
    else if (g_RenderMode == RENDER_MODE_STRETCHED) {
        // Velocity-stretched billboard
        float3 toCamera = normalize(g_CameraPosition.xyz - particlePos);
        float velLength = length(velocity);

        if (velLength > 0.001) {
            float3 velDir = velocity / velLength;

            // Up is velocity direction
            up = velDir;

            // Right is perpendicular to velocity and view
            right = normalize(cross(toCamera, up));

            // Stretch along velocity
            float stretch = 1.0 + velLength * g_StretchFactor;
            up *= stretch;
        } else {
            right = g_CameraRight.xyz;
            up = g_CameraUp.xyz;
        }
    }
    else if (g_RenderMode == RENDER_MODE_HORIZONTAL) {
        // Horizontal billboard (lies on XZ plane)
        right = float3(1, 0, 0);
        up = float3(0, 0, 1);

        // Apply rotation around Y axis
        float s = sin(rotation);
        float c = cos(rotation);
        right = float3(c, 0, s);
        up = float3(-s, 0, c);
    }
    else if (g_RenderMode == RENDER_MODE_VERTICAL) {
        // Vertical billboard (always upright, rotates to face camera)
        float3 toCamera = g_CameraPosition.xyz - particlePos;
        toCamera.y = 0;
        toCamera = normalize(toCamera);

        up = float3(0, 1, 0);
        right = cross(up, toCamera);
    }

    return particlePos + (right * corner.x + up * corner.y) * size;
}

// Get texture atlas UV for animated particles
float2 GetAtlasUV(float2 baseUV, float normalizedAge, float seed) {
    float rows = g_AtlasParams.x;
    float cols = g_AtlasParams.y;
    float totalFrames = g_AtlasParams.z;
    float animSpeed = g_AtlasParams.w;

    if (totalFrames <= 1.0) {
        return baseUV;
    }

    // Calculate current frame
    float frameIndex = normalizedAge * animSpeed * totalFrames;

    // Random start frame based on seed
    frameIndex += seed * totalFrames;

    frameIndex = fmod(frameIndex, totalFrames);

    uint frame = uint(frameIndex);
    uint frameX = frame % uint(cols);
    uint frameY = frame / uint(cols);

    float2 frameSize = float2(1.0 / cols, 1.0 / rows);
    float2 frameOffset = float2(frameX, frameY) * frameSize;

    return frameOffset + baseUV * frameSize;
}

// Vertex shader
VSOutput VSMain(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    VSOutput output;

    // Get sorted particle index
    uint particleIndex = g_SortedIndices[instanceID];

    // Skip invalid particles
    if (particleIndex == 0xFFFFFFFF || particleIndex >= g_MaxParticles) {
        output.position = float4(0, 0, 0, 0);
        output.color = float4(0, 0, 0, 0);
        output.texCoord = float2(0, 0);
        output.worldPos = float3(0, 0, 0);
        output.depth = 0;
        output.emitterIdx = 0;
        output.normalizedAge = 0;
        return output;
    }

    Particle p = g_Particles[particleIndex];

    // Check if particle is alive
    if (p.params.y < 0.0) {
        output.position = float4(0, 0, 0, 0);
        output.color = float4(0, 0, 0, 0);
        output.texCoord = float2(0, 0);
        output.worldPos = float3(0, 0, 0);
        output.depth = 0;
        output.emitterIdx = 0;
        output.normalizedAge = 0;
        return output;
    }

    // Billboard corners (2 triangles forming quad)
    // Vertex order: 0=BL, 1=TL, 2=TR, 3=BL, 4=TR, 5=BR
    static const float2 corners[6] = {
        float2(-0.5, -0.5),     // 0: Bottom-left
        float2(-0.5,  0.5),     // 1: Top-left
        float2( 0.5,  0.5),     // 2: Top-right
        float2(-0.5, -0.5),     // 3: Bottom-left
        float2( 0.5,  0.5),     // 4: Top-right
        float2( 0.5, -0.5)      // 5: Bottom-right
    };

    static const float2 texCoords[6] = {
        float2(0, 1),           // 0: Bottom-left
        float2(0, 0),           // 1: Top-left
        float2(1, 0),           // 2: Top-right
        float2(0, 1),           // 3: Bottom-left
        float2(1, 0),           // 4: Top-right
        float2(1, 1)            // 5: Bottom-right
    };

    float2 corner = corners[vertexID];
    float2 baseTexCoord = texCoords[vertexID];

    float3 particlePos = p.position.xyz;
    float size = p.position.w;
    float rotation = p.velocity.w;
    float3 velocity = p.velocity.xyz;

    // Generate billboard vertex
    float3 worldPos = GetBillboardPosition(particlePos, corner, size, rotation, velocity);

    // Transform to clip space
    output.position = mul(g_ViewProj, float4(worldPos, 1.0));
    output.worldPos = worldPos;

    // Get view-space depth for soft particles
    float4 viewPos = mul(g_View, float4(worldPos, 1.0));
    output.depth = -viewPos.z;

    // Color with alpha
    output.color = p.color;

    // Normalized age for effects
    float normalizedAge = p.params.x / max(p.params.y, 0.001);
    output.normalizedAge = normalizedAge;

    // Texture coordinates with atlas animation
    float seed = frac(p.params.w / 1000.0);
    output.texCoord = GetAtlasUV(baseTexCoord, normalizedAge, seed);

    output.emitterIdx = uint(p.params.z);

    return output;
}

// Pixel shader
float4 PSMain(VSOutput input) : SV_Target {
    // Skip invalid particles
    if (input.color.a <= 0.0) {
        discard;
    }

    // Sample particle texture
    float4 texColor = g_ParticleTexture.Sample(g_LinearSampler, input.texCoord);

    // Combine with vertex color
    float4 finalColor = texColor * input.color;

    // Alpha test (early out for transparent pixels)
    if (finalColor.a < 0.01) {
        discard;
    }

    // Soft particles
    if (g_SoftParams.w > 0.5) {
        // Get screen UV
        float2 screenUV = input.position.xy / float2(1920.0, 1080.0);  // TODO: Pass actual resolution

        // Sample scene depth
        float sceneDepth = g_DepthTexture.Sample(g_PointSampler, screenUV).r;

        // Convert to linear depth
        float nearPlane = 0.1;
        float farPlane = 10000.0;
        float linearSceneDepth = nearPlane * farPlane / (farPlane - sceneDepth * (farPlane - nearPlane));

        // Calculate soft particle factor
        float depthDiff = linearSceneDepth - input.depth;
        float softFactor = saturate(depthDiff / g_SoftParams.x);

        finalColor.a *= softFactor;
    }

    // Near/far fade
    float nearFade = saturate((input.depth - 0.5) / max(g_SoftParams.y, 0.01));
    float farFade = saturate((g_SoftParams.z - input.depth) / max(g_SoftParams.z * 0.1, 1.0));
    finalColor.a *= nearFade * farFade;

    return finalColor;
}

// Additive blend pixel shader
float4 PSMainAdditive(VSOutput input) : SV_Target {
    if (input.color.a <= 0.0) {
        discard;
    }

    float4 texColor = g_ParticleTexture.Sample(g_LinearSampler, input.texCoord);
    float4 finalColor = texColor * input.color;

    // For additive, multiply RGB by alpha and output alpha as 1
    finalColor.rgb *= finalColor.a;

    // Soft particles (same as alpha blend)
    if (g_SoftParams.w > 0.5) {
        float2 screenUV = input.position.xy / float2(1920.0, 1080.0);
        float sceneDepth = g_DepthTexture.Sample(g_PointSampler, screenUV).r;

        float nearPlane = 0.1;
        float farPlane = 10000.0;
        float linearSceneDepth = nearPlane * farPlane / (farPlane - sceneDepth * (farPlane - nearPlane));

        float depthDiff = linearSceneDepth - input.depth;
        float softFactor = saturate(depthDiff / g_SoftParams.x);

        finalColor.rgb *= softFactor;
    }

    float nearFade = saturate((input.depth - 0.5) / max(g_SoftParams.y, 0.01));
    float farFade = saturate((g_SoftParams.z - input.depth) / max(g_SoftParams.z * 0.1, 1.0));
    finalColor.rgb *= nearFade * farFade;

    return float4(finalColor.rgb, 1.0);
}

// Lit particle pixel shader (for mesh particles)
float4 PSMainLit(VSOutput input) : SV_Target {
    if (input.color.a <= 0.0) {
        discard;
    }

    float4 texColor = g_ParticleTexture.Sample(g_LinearSampler, input.texCoord);
    float4 finalColor = texColor * input.color;

    if (finalColor.a < 0.01) {
        discard;
    }

    // Sample normal map
    float3 normalMap = g_NormalTexture.Sample(g_LinearSampler, input.texCoord).rgb;
    normalMap = normalMap * 2.0 - 1.0;

    // Simple directional light
    float3 lightDir = normalize(float3(0.5, 0.8, 0.3));
    float3 normal = normalize(normalMap);
    float NdotL = max(dot(normal, lightDir), 0.0);

    float3 ambient = 0.3;
    float3 diffuse = NdotL * 0.7;

    finalColor.rgb *= (ambient + diffuse);

    // Soft particles
    if (g_SoftParams.w > 0.5) {
        float2 screenUV = input.position.xy / float2(1920.0, 1080.0);
        float sceneDepth = g_DepthTexture.Sample(g_PointSampler, screenUV).r;

        float nearPlane = 0.1;
        float farPlane = 10000.0;
        float linearSceneDepth = nearPlane * farPlane / (farPlane - sceneDepth * (farPlane - nearPlane));

        float depthDiff = linearSceneDepth - input.depth;
        float softFactor = saturate(depthDiff / g_SoftParams.x);

        finalColor.a *= softFactor;
    }

    return finalColor;
}

// Distortion particle pixel shader
struct PSDistortionOutput {
    float4 distortion : SV_Target0;     // RG = distortion offset, BA = unused
};

PSDistortionOutput PSMainDistortion(VSOutput input) {
    PSDistortionOutput output;

    if (input.color.a <= 0.0) {
        output.distortion = float4(0.5, 0.5, 0, 0);
        return output;
    }

    // Sample normal map for distortion direction
    float4 normalSample = g_NormalTexture.Sample(g_LinearSampler, input.texCoord);
    float2 distortionDir = normalSample.rg * 2.0 - 1.0;

    // Modulate by alpha and color
    float strength = input.color.a * normalSample.a;
    distortionDir *= strength * 0.1;  // Scale down

    // Encode as 0.5 = no distortion
    output.distortion = float4(distortionDir * 0.5 + 0.5, 0, 0);

    return output;
}

// Trail/ribbon vertex shader
struct TrailVertex {
    float3 position;
    float3 prevPosition;
    float width;
    float age;
    float4 color;
};

StructuredBuffer<TrailVertex> g_TrailVertices : register(t5);

struct TrailVSOutput {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 texCoord : TEXCOORD0;
    float3 worldPos : TEXCOORD1;
    float depth : TEXCOORD2;
};

TrailVSOutput VSTrail(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID) {
    TrailVSOutput output;

    // Each trail segment has 2 vertices (left and right edge)
    uint segmentIndex = vertexID / 2;
    uint side = vertexID % 2;  // 0 = left, 1 = right

    TrailVertex v = g_TrailVertices[instanceID * 64 + segmentIndex];  // Max 64 segments per trail

    // Calculate ribbon direction
    float3 direction = normalize(v.position - v.prevPosition);
    float3 toCamera = normalize(g_CameraPosition.xyz - v.position);
    float3 right = normalize(cross(direction, toCamera));

    // Offset by width
    float offset = (side == 0) ? -0.5 : 0.5;
    float3 worldPos = v.position + right * offset * v.width;

    output.position = mul(g_ViewProj, float4(worldPos, 1.0));
    output.worldPos = worldPos;

    float4 viewPos = mul(g_View, float4(worldPos, 1.0));
    output.depth = -viewPos.z;

    output.color = v.color;
    output.texCoord = float2(float(side), v.age);

    return output;
}

float4 PSTrail(TrailVSOutput input) : SV_Target {
    float4 texColor = g_ParticleTexture.Sample(g_LinearSampler, input.texCoord);
    float4 finalColor = texColor * input.color;

    // Edge fade
    float edgeFade = 1.0 - abs(input.texCoord.x - 0.5) * 2.0;
    finalColor.a *= edgeFade;

    // Age fade (tail fades out)
    float ageFade = 1.0 - input.texCoord.y;
    finalColor.a *= ageFade;

    return finalColor;
}
