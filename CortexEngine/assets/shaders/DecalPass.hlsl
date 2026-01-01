// DecalPass.hlsl
// Deferred decal rendering shader.
// Projects decals onto GBuffer surfaces.
// Reference: "Deferred Decals" - Wicked Engine
// Reference: "Volume Decals" - Killzone Shadow Fall

#include "Common.hlsli"

// Blend modes (must match DecalBlendMode enum)
#define BLEND_REPLACE           0
#define BLEND_MULTIPLY          1
#define BLEND_ADDITIVE          2
#define BLEND_ALPHA_BLEND       3
#define BLEND_OVERLAY           4

// Channel flags (must match DecalChannels enum)
#define CHANNEL_ALBEDO          1
#define CHANNEL_NORMAL          2
#define CHANNEL_ROUGHNESS       4
#define CHANNEL_METALLIC        8
#define CHANNEL_EMISSIVE        16

// Decal constant buffer
cbuffer DecalCB : register(b0) {
    float4x4 g_DecalMatrix;         // World to decal local space
    float4x4 g_DecalMatrixInv;      // Decal local to world space
    float4 g_DecalColor;            // RGBA tint
    float4 g_DecalParams;           // x=normalStrength, y=roughnessMod, z=metallicMod, w=angleFade
    float4 g_DecalParams2;          // x=fadeDistance, y=ageRatio, z=blendMode, w=channels
    float4 g_DecalSize;             // xyz=size, w=unused
};

// View constant buffer
cbuffer ViewCB : register(b1) {
    float4x4 g_View;
    float4x4 g_Proj;
    float4x4 g_ViewProj;
    float4x4 g_InvViewProj;
    float4 g_CameraPosition;
    float4 g_ScreenSize;            // xy=size, zw=1/size
};

// GBuffer inputs (read)
Texture2D<float4> g_GBufferAlbedo : register(t0);       // RGB=albedo, A=alpha
Texture2D<float4> g_GBufferNormal : register(t1);       // RGB=normal (world), A=unused
Texture2D<float4> g_GBufferMaterial : register(t2);     // R=roughness, G=metallic, B=ao, A=unused
Texture2D<float> g_DepthBuffer : register(t3);

// Decal textures
Texture2D<float4> g_DecalAlbedo : register(t4);         // RGB=color, A=opacity
Texture2D<float4> g_DecalNormal : register(t5);         // RGB=tangent-space normal, A=unused
Texture2D<float4> g_DecalMask : register(t6);           // R=roughness, G=metallic, B=emissive, A=mask

SamplerState g_LinearClamp : register(s0);
SamplerState g_LinearWrap : register(s1);

// Vertex shader output
struct VSOutput {
    float4 position : SV_Position;
    float3 worldPos : TEXCOORD0;
    float4 screenPos : TEXCOORD1;
};

// Pixel shader output (writes to same GBuffer targets)
struct PSOutput {
    float4 albedo : SV_Target0;
    float4 normal : SV_Target1;
    float4 material : SV_Target2;
};

// Reconstruct world position from depth
float3 ReconstructWorldPos(float2 screenUV, float depth) {
    float4 clipPos = float4(screenUV * 2.0 - 1.0, depth, 1.0);
    clipPos.y = -clipPos.y;

    float4 worldPos = mul(g_InvViewProj, clipPos);
    return worldPos.xyz / worldPos.w;
}

// Vertex shader: render decal volume (box)
VSOutput VSMain(uint vertexID : SV_VertexID) {
    VSOutput output;

    // Unit cube vertices (24 vertices for 12 triangles)
    static const float3 cubeVerts[36] = {
        // Front face
        float3(-0.5, -0.5,  0.5), float3( 0.5, -0.5,  0.5), float3( 0.5,  0.5,  0.5),
        float3(-0.5, -0.5,  0.5), float3( 0.5,  0.5,  0.5), float3(-0.5,  0.5,  0.5),
        // Back face
        float3( 0.5, -0.5, -0.5), float3(-0.5, -0.5, -0.5), float3(-0.5,  0.5, -0.5),
        float3( 0.5, -0.5, -0.5), float3(-0.5,  0.5, -0.5), float3( 0.5,  0.5, -0.5),
        // Left face
        float3(-0.5, -0.5, -0.5), float3(-0.5, -0.5,  0.5), float3(-0.5,  0.5,  0.5),
        float3(-0.5, -0.5, -0.5), float3(-0.5,  0.5,  0.5), float3(-0.5,  0.5, -0.5),
        // Right face
        float3( 0.5, -0.5,  0.5), float3( 0.5, -0.5, -0.5), float3( 0.5,  0.5, -0.5),
        float3( 0.5, -0.5,  0.5), float3( 0.5,  0.5, -0.5), float3( 0.5,  0.5,  0.5),
        // Top face
        float3(-0.5,  0.5,  0.5), float3( 0.5,  0.5,  0.5), float3( 0.5,  0.5, -0.5),
        float3(-0.5,  0.5,  0.5), float3( 0.5,  0.5, -0.5), float3(-0.5,  0.5, -0.5),
        // Bottom face
        float3(-0.5, -0.5, -0.5), float3( 0.5, -0.5, -0.5), float3( 0.5, -0.5,  0.5),
        float3(-0.5, -0.5, -0.5), float3( 0.5, -0.5,  0.5), float3(-0.5, -0.5,  0.5)
    };

    // Get local position
    float3 localPos = cubeVerts[vertexID];

    // Transform to world space
    float4 worldPos = mul(g_DecalMatrixInv, float4(localPos, 1.0));
    output.worldPos = worldPos.xyz;

    // Transform to clip space
    output.position = mul(g_ViewProj, worldPos);
    output.screenPos = output.position;

    return output;
}

// Overlay blend mode
float3 BlendOverlay(float3 base, float3 blend) {
    float3 result;
    result.r = base.r < 0.5 ? (2.0 * base.r * blend.r) : (1.0 - 2.0 * (1.0 - base.r) * (1.0 - blend.r));
    result.g = base.g < 0.5 ? (2.0 * base.g * blend.g) : (1.0 - 2.0 * (1.0 - base.g) * (1.0 - blend.g));
    result.b = base.b < 0.5 ? (2.0 * base.b * blend.b) : (1.0 - 2.0 * (1.0 - base.b) * (1.0 - blend.b));
    return result;
}

// Reoriented Normal Mapping blend
float3 BlendNormals(float3 baseNormal, float3 detailNormal, float strength) {
    // RNM blend for combining normal maps
    float3 t = baseNormal + float3(0, 0, 1);
    float3 u = detailNormal * float3(-1, -1, 1);

    float3 result = normalize(t * dot(t, u) - u * t.z);

    // Blend with original based on strength
    return normalize(lerp(baseNormal, result, strength));
}

// Pixel shader: project decal onto GBuffer
PSOutput PSMain(VSOutput input) {
    PSOutput output;

    // Get screen UV
    float2 screenUV = input.screenPos.xy / input.screenPos.w;
    screenUV = screenUV * 0.5 + 0.5;
    screenUV.y = 1.0 - screenUV.y;

    // Sample depth
    float depth = g_DepthBuffer.Sample(g_LinearClamp, screenUV).r;

    // Reconstruct world position
    float3 worldPos = ReconstructWorldPos(screenUV, depth);

    // Transform to decal local space
    float4 decalPos = mul(g_DecalMatrix, float4(worldPos, 1.0));
    decalPos.xyz /= decalPos.w;

    // Reject pixels outside decal volume
    if (any(abs(decalPos.xyz) > 0.5)) {
        discard;
    }

    // Get decal UV (XY in local space, remapped to 0-1)
    float2 decalUV = decalPos.xy + 0.5;

    // Sample existing GBuffer
    float4 existingAlbedo = g_GBufferAlbedo.Sample(g_LinearClamp, screenUV);
    float4 existingNormal = g_GBufferNormal.Sample(g_LinearClamp, screenUV);
    float4 existingMaterial = g_GBufferMaterial.Sample(g_LinearClamp, screenUV);

    // World-space surface normal
    float3 surfaceNormal = normalize(existingNormal.rgb * 2.0 - 1.0);

    // Angle-based fade: reject decals on surfaces facing away
    float3 decalForward = normalize(mul((float3x3)g_DecalMatrixInv, float3(0, 0, 1)));
    float angleFade = saturate(dot(surfaceNormal, -decalForward));

    // Apply angle threshold
    float angleThreshold = g_DecalParams.w;
    if (angleFade < angleThreshold) {
        discard;
    }

    // Remap angle fade for smooth transition
    angleFade = saturate((angleFade - angleThreshold) / (1.0 - angleThreshold));

    // Distance fade
    float3 toCamera = g_CameraPosition.xyz - worldPos;
    float distance = length(toCamera);
    float fadeDistance = g_DecalParams2.x;
    float distanceFade = saturate(1.0 - distance / fadeDistance);

    // Edge fade (soft edges in decal space)
    float2 edgeDist = 0.5 - abs(decalPos.xy);
    float edgeFade = saturate(min(edgeDist.x, edgeDist.y) * 10.0);

    // Depth fade (soft intersection with surfaces)
    float depthFade = saturate((0.5 - abs(decalPos.z)) * 4.0);

    // Combined fade
    float fade = angleFade * distanceFade * edgeFade * depthFade;

    // Sample decal textures
    float4 decalAlbedo = g_DecalAlbedo.Sample(g_LinearWrap, decalUV) * g_DecalColor;
    float4 decalNormalSample = g_DecalNormal.Sample(g_LinearWrap, decalUV);
    float4 decalMask = g_DecalMask.Sample(g_LinearWrap, decalUV);

    // Apply mask alpha
    float alpha = decalAlbedo.a * decalMask.a * fade;

    if (alpha < 0.01) {
        discard;
    }

    // Get blend mode and channels
    uint blendMode = uint(g_DecalParams2.z);
    uint channels = uint(g_DecalParams2.w);

    // Initialize output with existing values
    output.albedo = existingAlbedo;
    output.normal = existingNormal;
    output.material = existingMaterial;

    // Blend albedo
    if (channels & CHANNEL_ALBEDO) {
        float3 blendedAlbedo;

        switch (blendMode) {
            case BLEND_REPLACE:
                blendedAlbedo = decalAlbedo.rgb;
                break;

            case BLEND_MULTIPLY:
                blendedAlbedo = existingAlbedo.rgb * decalAlbedo.rgb;
                break;

            case BLEND_ADDITIVE:
                blendedAlbedo = existingAlbedo.rgb + decalAlbedo.rgb * alpha;
                output.albedo.rgb = saturate(blendedAlbedo);
                alpha = 0;  // Additive doesn't use alpha blend
                break;

            case BLEND_OVERLAY:
                blendedAlbedo = BlendOverlay(existingAlbedo.rgb, decalAlbedo.rgb);
                break;

            case BLEND_ALPHA_BLEND:
            default:
                blendedAlbedo = decalAlbedo.rgb;
                break;
        }

        if (blendMode != BLEND_ADDITIVE) {
            output.albedo.rgb = lerp(existingAlbedo.rgb, blendedAlbedo, alpha);
        }
    }

    // Blend normal
    if (channels & CHANNEL_NORMAL) {
        // Decode tangent-space decal normal
        float3 decalNormal = decalNormalSample.rgb * 2.0 - 1.0;

        // Build TBN matrix for the surface
        // Approximate tangent from decal orientation
        float3 tangent = normalize(mul((float3x3)g_DecalMatrixInv, float3(1, 0, 0)));
        float3 bitangent = normalize(cross(surfaceNormal, tangent));
        tangent = cross(bitangent, surfaceNormal);

        float3x3 TBN = float3x3(tangent, bitangent, surfaceNormal);

        // Transform decal normal to world space
        float3 worldDecalNormal = normalize(mul(decalNormal, TBN));

        // Blend normals with strength
        float normalStrength = g_DecalParams.x * alpha;
        float3 blendedNormal = BlendNormals(surfaceNormal, worldDecalNormal, normalStrength);

        // Encode back to GBuffer format
        output.normal.rgb = blendedNormal * 0.5 + 0.5;
    }

    // Blend roughness
    if (channels & CHANNEL_ROUGHNESS) {
        float roughnessModifier = g_DecalParams.y;
        float decalRoughness = decalMask.r;

        float newRoughness = existingMaterial.r + (decalRoughness + roughnessModifier) * alpha;
        output.material.r = saturate(newRoughness);
    }

    // Blend metallic
    if (channels & CHANNEL_METALLIC) {
        float metallicModifier = g_DecalParams.z;
        float decalMetallic = decalMask.g;

        float newMetallic = existingMaterial.g + (decalMetallic + metallicModifier) * alpha;
        output.material.g = saturate(newMetallic);
    }

    return output;
}

// Instanced version for batched decal rendering
struct VSInstanceInput {
    float4x4 decalMatrix : INSTANCE_MATRIX;
    float4x4 decalMatrixInv : INSTANCE_MATRIX_INV;
    float4 color : INSTANCE_COLOR;
    float4 params : INSTANCE_PARAMS;
    float4 params2 : INSTANCE_PARAMS2;
};

VSOutput VSMainInstanced(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID,
                          VSInstanceInput instanceData) {
    VSOutput output;

    static const float3 cubeVerts[36] = {
        float3(-0.5, -0.5,  0.5), float3( 0.5, -0.5,  0.5), float3( 0.5,  0.5,  0.5),
        float3(-0.5, -0.5,  0.5), float3( 0.5,  0.5,  0.5), float3(-0.5,  0.5,  0.5),
        float3( 0.5, -0.5, -0.5), float3(-0.5, -0.5, -0.5), float3(-0.5,  0.5, -0.5),
        float3( 0.5, -0.5, -0.5), float3(-0.5,  0.5, -0.5), float3( 0.5,  0.5, -0.5),
        float3(-0.5, -0.5, -0.5), float3(-0.5, -0.5,  0.5), float3(-0.5,  0.5,  0.5),
        float3(-0.5, -0.5, -0.5), float3(-0.5,  0.5,  0.5), float3(-0.5,  0.5, -0.5),
        float3( 0.5, -0.5,  0.5), float3( 0.5, -0.5, -0.5), float3( 0.5,  0.5, -0.5),
        float3( 0.5, -0.5,  0.5), float3( 0.5,  0.5, -0.5), float3( 0.5,  0.5,  0.5),
        float3(-0.5,  0.5,  0.5), float3( 0.5,  0.5,  0.5), float3( 0.5,  0.5, -0.5),
        float3(-0.5,  0.5,  0.5), float3( 0.5,  0.5, -0.5), float3(-0.5,  0.5, -0.5),
        float3(-0.5, -0.5, -0.5), float3( 0.5, -0.5, -0.5), float3( 0.5, -0.5,  0.5),
        float3(-0.5, -0.5, -0.5), float3( 0.5, -0.5,  0.5), float3(-0.5, -0.5,  0.5)
    };

    float3 localPos = cubeVerts[vertexID];

    float4 worldPos = mul(instanceData.decalMatrixInv, float4(localPos, 1.0));
    output.worldPos = worldPos.xyz;

    output.position = mul(g_ViewProj, worldPos);
    output.screenPos = output.position;

    return output;
}

// Emissive decal pass (separate to allow additive blending)
struct PSEmissiveOutput {
    float4 emissive : SV_Target0;
};

PSEmissiveOutput PSEmissive(VSOutput input) {
    PSEmissiveOutput output;

    float2 screenUV = input.screenPos.xy / input.screenPos.w;
    screenUV = screenUV * 0.5 + 0.5;
    screenUV.y = 1.0 - screenUV.y;

    float depth = g_DepthBuffer.Sample(g_LinearClamp, screenUV).r;
    float3 worldPos = ReconstructWorldPos(screenUV, depth);

    float4 decalPos = mul(g_DecalMatrix, float4(worldPos, 1.0));
    decalPos.xyz /= decalPos.w;

    if (any(abs(decalPos.xyz) > 0.5)) {
        discard;
    }

    float2 decalUV = decalPos.xy + 0.5;

    // Sample emissive from mask blue channel
    float4 decalMask = g_DecalMask.Sample(g_LinearWrap, decalUV);
    float4 decalAlbedo = g_DecalAlbedo.Sample(g_LinearWrap, decalUV);

    float emissive = decalMask.b;
    float alpha = decalAlbedo.a * decalMask.a;

    output.emissive = float4(decalAlbedo.rgb * g_DecalColor.rgb * emissive, alpha);

    return output;
}

// Debug visualization
float4 PSDebug(VSOutput input) : SV_Target {
    float2 screenUV = input.screenPos.xy / input.screenPos.w;
    screenUV = screenUV * 0.5 + 0.5;
    screenUV.y = 1.0 - screenUV.y;

    float depth = g_DepthBuffer.Sample(g_LinearClamp, screenUV).r;
    float3 worldPos = ReconstructWorldPos(screenUV, depth);

    float4 decalPos = mul(g_DecalMatrix, float4(worldPos, 1.0));
    decalPos.xyz /= decalPos.w;

    if (any(abs(decalPos.xyz) > 0.5)) {
        discard;
    }

    // Visualize decal space position
    float3 color = decalPos.xyz + 0.5;

    return float4(color, 0.5);
}
