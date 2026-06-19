// MaterialResolve.hlsl
// Phase 2.2: Full material evaluation compute shader
// Input: Visibility buffer (triangle/instance IDs)
// Output: G-buffer textures (albedo, normal+roughness, emissive+metallic)

// Include biome materials for terrain rendering
#include "PBR_Lighting.hlsli"
#include "BiomeMaterials.hlsli"
#include "SurfaceClassification.hlsli"

// Root signature:
// b0: Resolution constants
// t0: Visibility buffer SRV
// t1: Instance data
// t2: Depth buffer SRV
// t3: Mesh table (StructuredBuffer)
// t5: Material constants (StructuredBuffer)
// u0: Albedo UAV output
// u1: Normal+Roughness UAV output
// u2: Emissive+Metallic UAV output
// u3: MaterialExt0 UAV output
// u4: MaterialExt1 UAV output
// u5: MaterialExt2 UAV output
// s0: Static sampler (linear wrap) for bindless textures

cbuffer ResolutionConstants : register(b0) {
    uint g_Width;
    uint g_Height;
    float g_RcpWidth;
    float g_RcpHeight;
    float4x4 g_ViewProj;  // View-projection matrix for computing clip-space barycentrics
    float4 g_CameraPosition; // xyz = camera world position
    uint g_MaterialCount;
    uint g_MeshCount;
    uint g_InstanceCount;  // For bounds checking
    uint _pad2;
};

// Instance data structure (matches VBInstanceData in C++)
struct VBInstanceData {
    float4x4 worldMatrix;
    float4x4 prevWorldMatrix;
    float4x4 normalMatrix; // inverse-transpose (world-space normal transform)
    uint meshIndex;
    uint materialIndex;
    uint firstIndex;
    uint indexCount;
    uint baseVertex;
    uint _padAlign[3]; // explicit padding
    float4 boundingSphere;  // xyz = center (object space), w = radius
    float4 prevCenterWS;    // xyz = previous frame center (world space)
    uint cullingId;         // packed gen<<16|slot
    uint flags;
    float depthBiasNdc;
    uint _pad0;
};

// Vertex structure (matches C++ vertex layout: 64 bytes)
struct Vertex {
    float3 position;    // 12 bytes (offset 0)
    float3 normal;      // 12 bytes (offset 12)
    float4 tangent;     // 16 bytes (offset 24)
    float2 texCoord;    // 8 bytes  (offset 40)
    float4 color;       // 16 bytes (offset 48) - vertex color for biome blending
    // Total: 64 bytes
};

// Minimal material constants (matches VBMaterialConstants in C++)
struct VBMaterialConstants {
    float4 albedo;
    float metallic;
    float roughness;
    float ao;
    float _pad0;
    uint4 textureIndices; // bindless indices: albedo, normal, metallic, roughness
    uint4 textureIndices2; // bindless indices: occlusion, emissive, unused, unused
    float4 emissiveFactorStrength; // rgb emissive factor, w emissive strength
    float4 extraParams;            // x occlusion strength, y normal scale, z anisotropy, w wetness
    // x = clear-coat weight, y = clear-coat roughness, z = sheen weight, w = SSS wrap
    float4 coatParams;
    // Transmission + IOR (KHR_materials_transmission / KHR_materials_ior).
    // x = transmission factor (0..1), y = IOR (>= 1), z = emissive bloom boost, w = procedural mask.
    float4 transmissionParams;
    // Specular extension (KHR_materials_specular).
    // rgb = specular color factor (linear), w = specular factor.
    float4 specularParams;
    // Bindless texture indices for extensions:
    // textureIndices3: x=transmission, y=clearcoat, z=clearcoatRoughness, w=specular
    // textureIndices4: x=specularColor, y/z/w unused
    uint4 textureIndices3;
    uint4 textureIndices4;
    float alphaCutoff;
    uint alphaMode; // 0=opaque, 1=mask, 2=blend
    uint doubleSided;
    uint materialClass;
    // x = named scene material class, y = reflection preference,
    // z = temporal policy, w = post sensitivity.
    uint4 policyParams;
};

// Per-mesh table entry (matches VBMeshTableEntry in C++)
struct VBMeshTableEntry {
    uint vertexBufferIndex;
    uint indexBufferIndex;
    uint vertexStrideBytes;
    uint indexFormat; // 0 = R32_UINT, 1 = R16_UINT
};

// Visibility buffer input
Texture2D<uint2> g_VisibilityBuffer : register(t0);
StructuredBuffer<VBInstanceData> g_Instances : register(t1);
Texture2D<float> g_DepthBuffer : register(t2);
StructuredBuffer<VBMeshTableEntry> g_MeshTable : register(t3);
StructuredBuffer<VBMaterialConstants> g_Materials : register(t5);

// G-buffer UAV outputs
RWTexture2D<unorm float4> g_AlbedoOut : register(u0);        // RGBA8_UNORM (linear)
RWTexture2D<float4> g_NormalRoughnessOut : register(u1);     // RGBA16F
RWTexture2D<float4> g_EmissiveMetallicOut : register(u2);    // RGBA16F
RWTexture2D<float4> g_MaterialExt0Out : register(u3);        // RGBA16F: clearcoat/IOR/specularFactor
RWTexture2D<float4> g_MaterialExt1Out : register(u4);        // RGBA16F: specularColor/transmission
RWTexture2D<unorm float4> g_MaterialExt2Out : register(u5);  // RGBA8: surface class, anisotropy, sheen, named scene material class

SamplerState g_Sampler : register(s0);

static const uint INVALID_BINDLESS_INDEX = 0xFFFFFFFFu;

// Compute perspective-correct UV gradients for proper mip selection.
// This is critical for VB renderers - wrong gradients cause texture shimmer.
//
// The key insight is that we need gradients of the PERSPECTIVE-CORRECT UVs,
// not just screen-space gradients. The perspective-correct UV at a pixel is:
//   UV = (b0*UV0/w0 + b1*UV1/w1 + b2*UV2/w2) / (b0/w0 + b1/w1 + b2/w2)
// where b0,b1,b2 are screen-space barycentrics and w0,w1,w2 are clip.w values.
//
// For stability, we compute gradients using the Jacobian of the screen-to-UV mapping.
struct UVGradients {
    float2 ddx;
    float2 ddy;
};

struct WorldGradients {
    float3 ddx;
    float3 ddy;
};

UVGradients ComputePerspectiveCorrectUVGradients(
    float2 uv0, float2 uv1, float2 uv2,
    float2 screen0, float2 screen1, float2 screen2,
    float w0, float w1, float w2,
    float3 screenBary)
{
    UVGradients result;
    result.ddx = float2(0.0f, 0.0f);
    result.ddy = float2(0.0f, 0.0f);

    // Convert screen coords to pixel units for numerical stability
    float2 p0 = screen0 * float2((float)g_Width, (float)g_Height);
    float2 p1 = screen1 * float2((float)g_Width, (float)g_Height);
    float2 p2 = screen2 * float2((float)g_Width, (float)g_Height);

    // Edge vectors in screen space
    float2 e01 = p1 - p0;
    float2 e02 = p2 - p0;

    // Triangle area (2x) via cross product
    float area2 = e01.x * e02.y - e01.y * e02.x;
    if (abs(area2) < 1e-6f) {
        return result; // Degenerate triangle
    }
    float invArea2 = 1.0f / area2;

    // Compute perspective-correct UV values at vertices (UV/w)
    float2 uvOverW0 = uv0 / w0;
    float2 uvOverW1 = uv1 / w1;
    float2 uvOverW2 = uv2 / w2;
    float invW0 = 1.0f / w0;
    float invW1 = 1.0f / w1;
    float invW2 = 1.0f / w2;

    // Gradient of 1/w across screen space
    float dInvWdx = (invW1 - invW0) * e02.y * invArea2 - (invW2 - invW0) * e01.y * invArea2;
    float dInvWdy = -(invW1 - invW0) * e02.x * invArea2 + (invW2 - invW0) * e01.x * invArea2;

    // Gradient of UV/w across screen space
    float2 dUVOverWdx = (uvOverW1 - uvOverW0) * e02.y * invArea2 - (uvOverW2 - uvOverW0) * e01.y * invArea2;
    float2 dUVOverWdy = -(uvOverW1 - uvOverW0) * e02.x * invArea2 + (uvOverW2 - uvOverW0) * e01.x * invArea2;

    // Apply the quotient rule at this pixel, not at the triangle center.
    // The previous center-average approximation selected the wrong mip on
    // large oblique room-shell triangles, which made broad floors/walls crawl
    // during mouse-look in the visibility-buffer path.
    float denom =
        screenBary.x * invW0 +
        screenBary.y * invW1 +
        screenBary.z * invW2;
    float2 uvOverW =
        screenBary.x * uvOverW0 +
        screenBary.y * uvOverW1 +
        screenBary.z * uvOverW2;
    denom = max(abs(denom), 1e-7f) * ((denom < 0.0f) ? -1.0f : 1.0f);
    float invDenom2 = 1.0f / (denom * denom);

    result.ddx = (dUVOverWdx * denom - uvOverW * dInvWdx) * invDenom2;
    result.ddy = (dUVOverWdy * denom - uvOverW * dInvWdy) * invDenom2;

    // Clamp gradients to prevent extreme mip selection (prevents shimmer)
    const float maxGrad = 4.0f; // Maximum 4 texels per pixel
    result.ddx = clamp(result.ddx, -maxGrad, maxGrad);
    result.ddy = clamp(result.ddy, -maxGrad, maxGrad);

    return result;
}

WorldGradients ComputePerspectiveCorrectWorldGradients(
    float3 world0, float3 world1, float3 world2,
    float2 screen0, float2 screen1, float2 screen2,
    float w0, float w1, float w2,
    float3 screenBary)
{
    WorldGradients result;
    result.ddx = float3(0.0f, 0.0f, 0.0f);
    result.ddy = float3(0.0f, 0.0f, 0.0f);

    float2 p0 = screen0 * float2((float)g_Width, (float)g_Height);
    float2 p1 = screen1 * float2((float)g_Width, (float)g_Height);
    float2 p2 = screen2 * float2((float)g_Width, (float)g_Height);

    float2 e01 = p1 - p0;
    float2 e02 = p2 - p0;
    float area2 = e01.x * e02.y - e01.y * e02.x;
    if (abs(area2) < 1e-6f) {
        return result;
    }
    float invArea2 = 1.0f / area2;

    float3 worldOverW0 = world0 / w0;
    float3 worldOverW1 = world1 / w1;
    float3 worldOverW2 = world2 / w2;
    float invW0 = 1.0f / w0;
    float invW1 = 1.0f / w1;
    float invW2 = 1.0f / w2;

    float dInvWdx = (invW1 - invW0) * e02.y * invArea2 - (invW2 - invW0) * e01.y * invArea2;
    float dInvWdy = -(invW1 - invW0) * e02.x * invArea2 + (invW2 - invW0) * e01.x * invArea2;

    float3 dWorldOverWdx = (worldOverW1 - worldOverW0) * e02.y * invArea2 - (worldOverW2 - worldOverW0) * e01.y * invArea2;
    float3 dWorldOverWdy = -(worldOverW1 - worldOverW0) * e02.x * invArea2 + (worldOverW2 - worldOverW0) * e01.x * invArea2;

    float denom =
        screenBary.x * invW0 +
        screenBary.y * invW1 +
        screenBary.z * invW2;
    float3 worldOverW =
        screenBary.x * worldOverW0 +
        screenBary.y * worldOverW1 +
        screenBary.z * worldOverW2;
    denom = max(abs(denom), 1e-7f) * ((denom < 0.0f) ? -1.0f : 1.0f);
    float invDenom2 = 1.0f / (denom * denom);

    result.ddx = (dWorldOverWdx * denom - worldOverW * dInvWdx) * invDenom2;
    result.ddy = (dWorldOverWdy * denom - worldOverW * dInvWdy) * invDenom2;

    const float maxGrad = 2.0f;
    result.ddx = clamp(result.ddx, -maxGrad, maxGrad);
    result.ddy = clamp(result.ddy, -maxGrad, maxGrad);
    return result;
}

float ProceduralMaskFootprintFilter(float2 ddxUV, float2 ddyUV,
                                    float3 ddxWorld, float3 ddyWorld,
                                    uint materialClass)
{
    float worldFrequency = 9.0f;
    float uvFrequency = 9.0f;
    if (materialClass == SURFACE_CLASS_WOOD) {
        worldFrequency = 24.0f;
        uvFrequency = 22.0f;
    } else if (materialClass == SURFACE_CLASS_MASONRY) {
        worldFrequency = 11.0f;
        uvFrequency = 9.0f;
    } else if (materialClass == SURFACE_CLASS_BRUSHED_METAL) {
        worldFrequency = 36.0f;
        uvFrequency = 64.0f;
    } else if (materialClass == SURFACE_CLASS_PLASTIC) {
        worldFrequency = 16.0f;
        uvFrequency = 16.0f;
    } else if (materialClass == SURFACE_CLASS_DEFAULT) {
        worldFrequency = 42.0f;
        uvFrequency = 9.0f;
    }

    float worldFootprint = max(length(ddxWorld.xz), length(ddyWorld.xz)) * worldFrequency;
    float uvFootprint = max(length(ddxUV), length(ddyUV)) * uvFrequency;
    float footprint = max(worldFootprint, uvFootprint);

    // Procedural masks are analytic, so they do not get hardware mip
    // selection. Fade the micro-detail out when it projects near/sub-pixel;
    // otherwise shallow floors and coping produce dark/light crawling during
    // mouse rotation even though texture maps are stable.
    return 1.0f - smoothstep(0.28f, 0.78f, footprint);
}

// Global minimum roughness floor to prevent disco ball effect.
// The disco ball effect on metallic surfaces is caused by the specular lobe being
// too sharp, making per-triangle normal variations visible as brightness differences.
// A minimum roughness floor ensures the specular lobe is wide enough to hide these.
float ApplyRoughnessFloor(float baseRoughness)
{
    // Minimum roughness for ALL surfaces.
    // This ensures specular highlights are spread enough to hide discretization.
    const float kMinRoughness = 0.08f;  // Small floor for non-metals
    return max(baseRoughness, kMinRoughness);
}

float ApplyMetallicRoughnessFloor(float baseRoughness, float metallic)
{
    // Preserve authored mirror-class materials. The old global 0.25 metallic
    // floor made real mirrors behave like brushed metal in the G-buffer and
    // forced the RT path to compensate with excessive post blending.
    if (metallic > 0.9f && baseRoughness <= 0.06f) {
        return max(baseRoughness, 0.02f);
    }

    // Additional roughness floor for ordinary metals. Metals have strong
    // specular and no diffuse, so they need a modest floor to avoid triangle-
    // scale sparkle, but not enough to erase glossy/chrome material classes.
    const float kMetallicMinRoughness = 0.12f;
    float minRoughness = metallic * kMetallicMinRoughness;
    float floor = max(kMetallicMinRoughness * 0.35f, minRoughness);
    return max(baseRoughness, floor);
}

// Load a vertex from the per-mesh vertex buffer (raw SRV -> ByteAddressBuffer)
Vertex LoadVertex(ByteAddressBuffer vertexBuffer, uint vertexIndex, uint vertexStrideBytes) {
    uint offset = vertexIndex * vertexStrideBytes;

    Vertex v;
    v.position = asfloat(vertexBuffer.Load3(offset + 0));
    v.normal = asfloat(vertexBuffer.Load3(offset + 12));
    v.tangent = asfloat(vertexBuffer.Load4(offset + 24));
    v.texCoord = asfloat(vertexBuffer.Load2(offset + 40));
    v.color = asfloat(vertexBuffer.Load4(offset + 48));

    return v;
}

// Load triangle indices
uint LoadIndex16(ByteAddressBuffer indexBuffer, uint byteOffset) {
    uint word = indexBuffer.Load(byteOffset & ~3u);
    return ((byteOffset & 2u) != 0u) ? ((word >> 16) & 0xFFFFu) : (word & 0xFFFFu);
}

uint LoadIndex(ByteAddressBuffer indexBuffer, uint byteOffset, uint indexFormat) {
    // indexFormat: 0=R32_UINT, 1=R16_UINT
    return (indexFormat == 1u) ? LoadIndex16(indexBuffer, byteOffset) : indexBuffer.Load(byteOffset);
}

uint3 LoadTriangleIndices(ByteAddressBuffer indexBuffer, uint triangleID, uint firstIndex, uint baseVertex, uint indexFormat) {
    uint indexStrideBytes = (indexFormat == 1u) ? 2u : 4u;
    uint indexOffset = (firstIndex + triangleID * 3u) * indexStrideBytes;

    uint3 indices;
    indices.x = LoadIndex(indexBuffer, indexOffset + indexStrideBytes * 0u, indexFormat) + baseVertex;
    indices.y = LoadIndex(indexBuffer, indexOffset + indexStrideBytes * 1u, indexFormat) + baseVertex;
    indices.z = LoadIndex(indexBuffer, indexOffset + indexStrideBytes * 2u, indexFormat) + baseVertex;
    return indices;
}

// Reconstruct world position from depth buffer
float3 ReconstructWorldPosition(float2 uv, float depth, float4x4 invViewProj) {
    // Convert to NDC
    float4 ndc = float4(
        uv.x * 2.0 - 1.0,
        (1.0 - uv.y) * 2.0 - 1.0,
        depth,
        1.0
    );

    // Transform to world space
    float4 worldPos = mul(invViewProj, ndc);
    return worldPos.xyz / worldPos.w;
}

// Compute screen-space barycentrics using edge functions.
// This matches the GPU rasterizer's approach for determining triangle coverage.
// Edge functions are more numerically stable for thin triangles.
float3 ComputeScreenSpaceBarycentrics(float2 p, float2 v0, float2 v1, float2 v2) {
    // Use edge function (signed area) method - same as GPU rasterizers
    // Edge function E(p) = (p - v0) x (v1 - v0) where x is 2D cross product

    // Edge vectors
    float2 e01 = v1 - v0;  // Edge from v0 to v1
    float2 e12 = v2 - v1;  // Edge from v1 to v2
    float2 e20 = v0 - v2;  // Edge from v2 to v0

    // Vectors from each vertex to point p
    float2 d0 = p - v0;
    float2 d1 = p - v1;
    float2 d2 = p - v2;

    // Edge functions (2D cross products give signed areas)
    // These are proportional to the barycentric weights
    float w0 = e12.x * d1.y - e12.y * d1.x;  // Area opposite to v0
    float w1 = e20.x * d2.y - e20.y * d2.x;  // Area opposite to v1
    float w2 = e01.x * d0.y - e01.y * d0.x;  // Area opposite to v2

    // Total signed area (for normalization)
    float area = w0 + w1 + w2;

    // Handle degenerate triangles
    if (abs(area) < 1e-10f) {
        return float3(0.333333f, 0.333333f, 0.333334f);
    }

    // Normalize to get barycentrics that sum to 1
    float invArea = 1.0f / area;
    return float3(w0 * invArea, w1 * invArea, w2 * invArea);
}

float Hash21(float2 p) {
    float3 p3 = frac(float3(p.x, p.y, p.x) * 0.1031f);
    p3 += dot(p3, p3.yzx + 33.33f);
    return frac((p3.x + p3.y) * p3.z);
}

float ValueNoise2D(float2 p) {
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0f - 2.0f * f);
    float a = Hash21(i);
    float b = Hash21(i + float2(1, 0));
    float c = Hash21(i + float2(0, 1));
    float d = Hash21(i + float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float ProceduralMaterialMask(float2 uv, float3 worldPos, uint materialClass) {
    float classOffset = (float)(materialClass % 17u) * 13.71f;
    float2 p = uv * 9.0f + worldPos.xz * 0.21f + float2(classOffset, classOffset);
    float mask = 0.55f * ValueNoise2D(p) +
                 0.30f * ValueNoise2D(p * 2.17f + 19.3f) +
                 0.15f * ValueNoise2D(p * 4.41f - 7.1f);

    if (materialClass == SURFACE_CLASS_WOOD) {
        float grain = 0.52f * ValueNoise2D(float2(worldPos.x * 4.0f + worldPos.z * 1.5f, worldPos.y * 7.0f) + classOffset);
        grain += 0.28f * sin((worldPos.x + worldPos.z * 0.35f) * 24.0f + grain * 4.0f) * 0.5f + 0.14f;
        grain += 0.20f * ValueNoise2D(uv * float2(22.0f, 5.0f) + classOffset * 0.37f);
        mask = lerp(mask, saturate(grain), 0.68f);
    } else if (materialClass == SURFACE_CLASS_MASONRY) {
        float2 brick = frac(worldPos.xz * float2(2.4f, 3.1f) + floor(worldPos.y * 2.0f) * 0.23f);
        float mortar = 1.0f - smoothstep(0.015f, 0.055f, min(min(brick.x, 1.0f - brick.x), min(brick.y, 1.0f - brick.y)));
        float pores = ValueNoise2D(worldPos.xz * 11.0f + worldPos.xy * 0.7f + classOffset);
        mask = saturate(lerp(mask, pores * 0.72f + mortar * 0.28f, 0.62f));
    } else if (materialClass == SURFACE_CLASS_BRUSHED_METAL) {
        float streaks = ValueNoise2D(float2(worldPos.x * 36.0f, worldPos.y * 2.0f + worldPos.z * 3.0f) + classOffset);
        float fine = ValueNoise2D(float2(uv.x * 64.0f, uv.y * 4.0f) + classOffset * 0.19f);
        mask = saturate(lerp(mask, streaks * 0.58f + fine * 0.42f, 0.70f));
    } else if (materialClass == SURFACE_CLASS_PLASTIC) {
        float molded = 0.65f * ValueNoise2D(uv * 16.0f + classOffset);
        molded += 0.35f * ValueNoise2D(worldPos.xz * 5.5f + classOffset * 0.31f);
        mask = saturate(lerp(mask, molded, 0.45f));
    } else if (materialClass == SURFACE_CLASS_DEFAULT) {
        float broadMottle = ValueNoise2D(worldPos.xz * 1.55f + classOffset);
        float dampMottle = ValueNoise2D(worldPos.xz * 3.2f + float2(classOffset, -classOffset));
        float fineGrain = ValueNoise2D(worldPos.xz * 38.0f + uv * 9.0f + classOffset * 0.41f);
        float saltPepper = ValueNoise2D(worldPos.xz * 82.0f + classOffset * 0.17f);
        float sandLike = broadMottle * 0.22f + dampMottle * 0.34f + fineGrain * 0.34f + saltPepper * 0.10f;
        mask = saturate(lerp(mask, sandLike, 0.38f));
    }

    return saturate(mask);
}

float3 FallbackTangentFromNormal(float3 normalWS) {
    float3 helper = (abs(normalWS.y) < 0.88f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    return normalize(cross(helper, normalWS));
}

float3 ProceduralTriplanarWeights(float3 normalWS);
float FineWeavePlane(float2 p);
float WoodGrainPlane(float2 p);

float3 ApplyProceduralMicroNormal(float3 normalWS,
                                  float3 tangentWS,
                                  float tangentSign,
                                  float2 uv,
                                  float3 worldPos,
                                  uint materialClass,
                                  float maskStrength) {
    maskStrength = saturate(maskStrength);
    if (maskStrength <= 0.001f) {
        return normalWS;
    }

    float3 N = normalize(normalWS);
    float3 T = tangentWS - N * dot(tangentWS, N);
    if (!all(isfinite(T)) || dot(T, T) < 1e-6f) {
        T = FallbackTangentFromNormal(N);
    } else {
        T = normalize(T);
    }
    float3 B = normalize(cross(N, T)) * ((tangentSign >= 0.0f) ? 1.0f : -1.0f);

    const float uvEps = 0.018f;
    const float worldEps = 0.035f;
    float h  = ProceduralMaterialMask(uv, worldPos, materialClass);
    float hx = ProceduralMaterialMask(uv + float2(uvEps, 0.0f), worldPos + T * worldEps, materialClass);
    float hy = ProceduralMaterialMask(uv + float2(0.0f, uvEps), worldPos + B * worldEps, materialClass);

    float2 slope = float2(hx - h, hy - h);
    float classGain = 1.0f;
    if (materialClass == SURFACE_CLASS_WOOD) {
        classGain = 1.35f;
    } else if (materialClass == SURFACE_CLASS_MASONRY) {
        classGain = 1.20f;
    } else if (materialClass == SURFACE_CLASS_BRUSHED_METAL || materialClass == SURFACE_CLASS_PLASTIC) {
        classGain = 0.72f;
    }

    float bump = lerp(0.30f, 1.10f, maskStrength) * classGain;
    float3 bumped = normalize(N - (T * slope.x + B * slope.y) * bump);
    return normalize(lerp(N, bumped, saturate(maskStrength * 0.85f)));
}

float MaterialHeightDetailStrength(uint materialClass,
                                   uint sceneMaterialClass,
                                   float proceduralMaskStrength,
                                   float roughness,
                                   float metallic)
{
    float nonMetal = 1.0f - smoothstep(0.08f, 0.35f, saturate(metallic));
    float classStrength = 0.0f;
    if (sceneMaterialClass == SCENE_MATERIAL_CERAMIC_TILE) {
        classStrength = 0.30f;
    } else if (sceneMaterialClass == SCENE_MATERIAL_POLISHED_WOOD) {
        classStrength = 0.28f;
    } else if (sceneMaterialClass == SCENE_MATERIAL_CONCRETE ||
               sceneMaterialClass == SCENE_MATERIAL_PAINTED_WALL) {
        classStrength = 0.22f;
    } else if (sceneMaterialClass == SCENE_MATERIAL_FABRIC) {
        classStrength = 0.18f;
    } else if (materialClass == SURFACE_CLASS_MASONRY) {
        classStrength = 0.25f;
    } else if (materialClass == SURFACE_CLASS_WOOD) {
        classStrength = 0.24f;
    } else if (materialClass == SURFACE_CLASS_DEFAULT) {
        classStrength = 0.10f;
    }

    float roughEligible = 1.0f - smoothstep(0.02f, 0.16f, saturate(roughness));
    roughEligible = max(roughEligible, smoothstep(0.18f, 0.86f, saturate(roughness)));
    return saturate(max(classStrength, proceduralMaskStrength * 0.65f) * nonMetal * roughEligible);
}

float TileGroutHeightPlane(float2 p)
{
    float2 cell = frac(p * float2(2.85f, 2.85f));
    float edge = min(min(cell.x, 1.0f - cell.x), min(cell.y, 1.0f - cell.y));
    float grout = 1.0f - smoothstep(0.018f, 0.060f, edge);
    float chip = ValueNoise2D(p * 11.0f + 4.7f) * 0.11f;
    return saturate(0.62f + chip - grout * 0.52f);
}

float WoodPlankHeightPlane(float2 p)
{
    float plank = frac(p.x * 1.55f + ValueNoise2D(p * 0.42f) * 0.18f);
    float seam = 1.0f - smoothstep(0.020f, 0.065f, min(plank, 1.0f - plank));
    float grain = WoodGrainPlane(p * 0.72f) * 0.22f + ValueNoise2D(p * 7.5f + 9.1f) * 0.10f;
    return saturate(0.58f + grain - seam * 0.46f);
}

float FabricPileHeightPlane(float2 p)
{
    float weave = FineWeavePlane(p * 1.35f);
    float pile = ValueNoise2D(p * 18.0f + 2.3f) * 0.18f + ValueNoise2D(p * 41.0f - 7.1f) * 0.08f;
    return saturate(0.50f + weave * 0.80f + pile);
}

float TriplanarHeightProxy(float3 worldPos, float3 normalWS, uint materialClass, uint sceneMaterialClass)
{
    float3 w = ProceduralTriplanarWeights(normalWS);
    float hx;
    float hy;
    float hz;

    if (sceneMaterialClass == SCENE_MATERIAL_CERAMIC_TILE ||
        sceneMaterialClass == SCENE_MATERIAL_CONCRETE ||
        sceneMaterialClass == SCENE_MATERIAL_PAINTED_WALL ||
        materialClass == SURFACE_CLASS_MASONRY) {
        hx = TileGroutHeightPlane(worldPos.zy);
        hy = TileGroutHeightPlane(worldPos.xz);
        hz = TileGroutHeightPlane(worldPos.xy);
    } else if (sceneMaterialClass == SCENE_MATERIAL_POLISHED_WOOD ||
               materialClass == SURFACE_CLASS_WOOD) {
        hx = WoodPlankHeightPlane(worldPos.zy);
        hy = WoodPlankHeightPlane(worldPos.xz);
        hz = WoodPlankHeightPlane(worldPos.xy);
    } else if (sceneMaterialClass == SCENE_MATERIAL_FABRIC) {
        hx = FabricPileHeightPlane(worldPos.zy);
        hy = FabricPileHeightPlane(worldPos.xz);
        hz = FabricPileHeightPlane(worldPos.xy);
    } else {
        hx = ProceduralMaterialMask(worldPos.zy, worldPos, materialClass);
        hy = ProceduralMaterialMask(worldPos.xz, worldPos, materialClass);
        hz = ProceduralMaterialMask(worldPos.xy, worldPos, materialClass);
    }

    return saturate(dot(float3(hx, hy, hz), w));
}

float TextureHeightProxy(float3 albedoSample,
                         float4 normalSample,
                         bool hasAlbedo,
                         bool hasNormal)
{
    float height = 0.5f;
    if (hasNormal) {
        float2 nxy = normalSample.xy * 2.0f - 1.0f;
        float nz = sqrt(saturate(1.0f - dot(nxy, nxy)));
        float blueOrReconstructed = max(normalSample.b, nz);
        height = lerp(height, saturate(blueOrReconstructed), 0.62f);
    }
    if (hasAlbedo) {
        float luma = dot(saturate(albedoSample), float3(0.2126f, 0.7152f, 0.0722f));
        height = lerp(height, luma, hasNormal ? 0.22f : 0.52f);
    }
    return saturate(height);
}

float2 ApplyBoundedParallaxOffset(float2 uv,
                                  float3 worldPos,
                                  float3 normalWS,
                                  float3 tangentWS,
                                  float tangentSign,
                                  uint materialClass,
                                  uint sceneMaterialClass,
                                  float strength,
                                  float3 albedoForHeight,
                                  float4 normalForHeight,
                                  bool hasAlbedo,
                                  bool hasNormal)
{
    strength = saturate(strength);
    if (strength <= 0.001f) {
        return uv;
    }

    float3 N = normalize(normalWS);
    float3 T = tangentWS - N * dot(tangentWS, N);
    if (!all(isfinite(T)) || dot(T, T) < 1.0e-6f) {
        T = FallbackTangentFromNormal(N);
    } else {
        T = normalize(T);
    }
    float3 B = normalize(cross(N, T)) * ((tangentSign >= 0.0f) ? 1.0f : -1.0f);

    float3 V = normalize(g_CameraPosition.xyz - worldPos);
    float3 viewTS = float3(dot(V, T), dot(V, B), max(dot(V, N), 0.0f));
    float grazingFade = smoothstep(0.10f, 0.32f, viewTS.z);
    float distanceFade = 1.0f - smoothstep(7.0f, 19.0f, distance(g_CameraPosition.xyz, worldPos));
    float detailFade = saturate(grazingFade * distanceFade);
    if (detailFade <= 0.001f) {
        return uv;
    }

    float textureHeight = TextureHeightProxy(albedoForHeight, normalForHeight, hasAlbedo, hasNormal);
    float2 parallaxDir = -viewTS.xy / max(viewTS.z, 0.34f);
    float maxOffset = lerp(0.006f, 0.038f, strength) * detailFade;
    float2 offset = float2(0.0f, 0.0f);
    uint stepCount = (strength > 0.24f) ? 5u : 4u;

    [unroll]
    for (uint i = 0u; i < 5u; ++i) {
        if (i < stepCount) {
            float proceduralHeight = TriplanarHeightProxy(worldPos + T * offset.x + B * offset.y,
                                                          N,
                                                          materialClass,
                                                          sceneMaterialClass);
            float height = lerp(proceduralHeight, textureHeight, hasNormal ? 0.42f : (hasAlbedo ? 0.24f : 0.0f));
            float centered = height - 0.5f;
            float2 target = parallaxDir * centered * maxOffset;
            offset = lerp(offset, target, 0.65f);
            offset = clamp(offset, -float2(maxOffset, maxOffset), float2(maxOffset, maxOffset));
        }
    }

    return uv + offset;
}

float DetailNormalLayerStrength(uint materialClass,
                                uint sceneMaterialClass,
                                float proceduralMaskStrength,
                                float metallic)
{
    float nonMetal = 1.0f - smoothstep(0.06f, 0.30f, saturate(metallic));
    float semantic = 0.0f;
    if (sceneMaterialClass == SCENE_MATERIAL_CERAMIC_TILE ||
        sceneMaterialClass == SCENE_MATERIAL_CONCRETE ||
        materialClass == SURFACE_CLASS_MASONRY) {
        semantic = 0.24f;
    } else if (sceneMaterialClass == SCENE_MATERIAL_POLISHED_WOOD ||
               materialClass == SURFACE_CLASS_WOOD) {
        semantic = 0.30f;
    } else if (sceneMaterialClass == SCENE_MATERIAL_FABRIC) {
        semantic = 0.26f;
    } else if (sceneMaterialClass == SCENE_MATERIAL_PAINTED_WALL) {
        semantic = 0.18f;
    }
    return saturate(max(semantic, proceduralMaskStrength * 0.72f) * nonMetal);
}

float3 ApplyDetailNormalLayer(float3 normalWS,
                              float3 tangentWS,
                              float tangentSign,
                              float2 uv,
                              float3 worldPos,
                              uint materialClass,
                              uint sceneMaterialClass,
                              float strength)
{
    strength = saturate(strength);
    if (strength <= 0.001f) {
        return normalWS;
    }

    float3 N = normalize(normalWS);
    float3 T = tangentWS - N * dot(tangentWS, N);
    if (!all(isfinite(T)) || dot(T, T) < 1.0e-6f) {
        T = FallbackTangentFromNormal(N);
    } else {
        T = normalize(T);
    }
    float3 B = normalize(cross(N, T)) * ((tangentSign >= 0.0f) ? 1.0f : -1.0f);

    float eps = 0.018f;
    float h = TriplanarHeightProxy(worldPos, N, materialClass, sceneMaterialClass);
    float hx = TriplanarHeightProxy(worldPos + T * eps, N, materialClass, sceneMaterialClass);
    float hy = TriplanarHeightProxy(worldPos + B * eps, N, materialClass, sceneMaterialClass);
    float2 slope = float2(hx - h, hy - h);
    float3 bumped = normalize(N - (T * slope.x + B * slope.y) * lerp(0.42f, 1.22f, strength));
    return normalize(lerp(N, bumped, saturate(strength * 0.62f)));
}

float RoughnessBreakupStrength(uint materialClass,
                               uint sceneMaterialClass,
                               float proceduralMaskStrength,
                               float metallic)
{
    float nonMetal = 1.0f - smoothstep(0.08f, 0.35f, saturate(metallic));
    float semantic = 0.0f;
    if (sceneMaterialClass == SCENE_MATERIAL_CERAMIC_TILE ||
        sceneMaterialClass == SCENE_MATERIAL_POLISHED_WOOD ||
        sceneMaterialClass == SCENE_MATERIAL_CONCRETE ||
        sceneMaterialClass == SCENE_MATERIAL_FABRIC ||
        materialClass == SURFACE_CLASS_MASONRY ||
        materialClass == SURFACE_CLASS_WOOD) {
        semantic = 0.16f;
    } else if (sceneMaterialClass == SCENE_MATERIAL_PAINTED_WALL) {
        semantic = 0.10f;
    }
    return saturate(max(semantic, proceduralMaskStrength * 0.38f) * nonMetal);
}

float3 ProceduralTriplanarWeights(float3 normalWS)
{
    float3 w = pow(abs(normalize(normalWS)), 4.0f);
    return w / max(w.x + w.y + w.z, 1.0e-4f);
}

float FineWeavePlane(float2 p)
{
    float2 q = p * 12.0f;
    float warp = (ValueNoise2D(q * 0.075f) * 2.0f - 1.0f) * 0.42f;
    float weave = sin((q.x + warp) * 3.14159265f) * sin((q.y - warp) * 3.14159265f);
    float speckle = (ValueNoise2D(q * 2.1f + float2(5.7f, 1.9f)) * 2.0f - 1.0f) * 0.50f +
                    (ValueNoise2D(q * 6.3f + float2(17.3f, 9.1f)) * 2.0f - 1.0f) * 0.22f;
    return weave * 0.080f + speckle * 0.125f;
}

float WoodGrainPlane(float2 p)
{
    float2 q = p * 1.65f;
    float slowWarp = (ValueNoise2D(q * 0.55f + float2(3.1f, 8.7f)) * 2.0f - 1.0f) * 2.4f;
    float fineWarp = (ValueNoise2D(float2(q.x * 2.8f, q.y * 0.45f) + float2(11.0f, 2.0f)) * 2.0f - 1.0f) * 0.75f;
    float grain = sin(q.x * 23.0f + slowWarp + fineWarp);
    float streak = (ValueNoise2D(float2(q.x * 10.0f + slowWarp, q.y * 0.75f)) * 2.0f - 1.0f) * 0.55f;
    float broad = (ValueNoise2D(float2(q.x * 1.4f, q.y * 0.18f) + float2(21.0f, 4.0f)) * 2.0f - 1.0f) * 0.25f;
    return grain * 0.22f + streak * 0.48f + broad;
}

float TriplanarFineWeave(float3 worldPos, float3 normalWS)
{
    float3 w = ProceduralTriplanarWeights(normalWS);
    float x = FineWeavePlane(worldPos.yz);
    float y = FineWeavePlane(worldPos.xz);
    float z = FineWeavePlane(worldPos.xy);
    return dot(float3(x, y, z), w);
}

float TriplanarWoodGrain(float3 worldPos, float3 normalWS)
{
    float3 w = ProceduralTriplanarWeights(normalWS);
    float x = WoodGrainPlane(worldPos.zy);
    float y = WoodGrainPlane(worldPos.xz);
    float z = WoodGrainPlane(worldPos.xy);
    return dot(float3(x, y, z), w);
}

float WearPlane(float2 p)
{
    float2 q = p * 0.82f;
    float broad = ValueNoise2D(q + float2(4.7f, 1.3f));
    float mid = ValueNoise2D(q * 2.35f + float2(17.1f, 6.4f));
    return broad * 0.68f + mid * 0.32f;
}

float TriplanarWearField(float3 worldPos, float3 normalWS)
{
    float3 w = ProceduralTriplanarWeights(normalWS);
    float x = WearPlane(worldPos.zy);
    float y = WearPlane(worldPos.xz);
    float z = WearPlane(worldPos.xy);
    return dot(float3(x, y, z), w);
}

void ApplyProceduralSurfaceDetailVB(float3 worldPos,
                                    float3 tangentWS,
                                    float tangentSign,
                                    inout float3 normalWS,
                                    inout float3 albedo,
                                    inout float roughness,
                                    inout float ao,
                                    float metallic,
                                    uint materialClass)
{
    float nonMetal = 1.0f - smoothstep(0.08f, 0.28f, saturate(metallic));
    float semanticWood = (materialClass == SURFACE_CLASS_WOOD) ? 1.0f : 0.0f;
    float highRough = nonMetal * smoothstep(0.62f, 0.82f, saturate(roughness));
    float midRough = nonMetal *
                     smoothstep(0.34f, 0.48f, saturate(roughness)) *
                     (1.0f - smoothstep(0.62f, 0.76f, saturate(roughness)));
    midRough = saturate(max(midRough, semanticWood * nonMetal * 0.85f));
    highRough *= (1.0f - semanticWood);

    float fine = TriplanarFineWeave(worldPos, normalWS);
    float wood = TriplanarWoodGrain(worldPos, normalWS);

    float3 N = normalize(normalWS);
    float3 T = tangentWS - N * dot(tangentWS, N);
    if (!all(isfinite(T)) || dot(T, T) < 1.0e-6f) {
        T = FallbackTangentFromNormal(N);
    } else {
        T = normalize(T);
    }
    float3 B = normalize(cross(N, T)) * ((tangentSign >= 0.0f) ? 1.0f : -1.0f);

    const float worldEps = 0.035f;
    float fineT = TriplanarFineWeave(worldPos + T * worldEps, N);
    float fineB = TriplanarFineWeave(worldPos + B * worldEps, N);
    float2 slope = float2(fineT - fine, fineB - fine);
    float3 bumped = normalize(N - (T * slope.x + B * slope.y) * 0.32f);
    normalWS = normalize(lerp(N, bumped, saturate(highRough * 0.45f)));

    float3 woodTint = float3(1.0f + wood * 0.060f,
                             1.0f + wood * 0.042f,
                             1.0f + wood * 0.024f);
    albedo = saturate(albedo * lerp(float3(1.0f, 1.0f, 1.0f), woodTint, midRough));

    float roughDelta = highRough * fine * 0.018f + midRough * wood * 0.045f;
    roughness = saturate(roughness + roughDelta);

    float cleanClass =
        (materialClass == SURFACE_CLASS_GLASS ||
         materialClass == SURFACE_CLASS_MIRROR ||
         materialClass == SURFACE_CLASS_WATER ||
         materialClass == SURFACE_CLASS_EMISSIVE) ? 1.0f : 0.0f;
    float grimeEligible = nonMetal * (1.0f - cleanClass) *
                          smoothstep(0.30f, 0.62f, saturate(roughness));

    float lowBand = 1.0f - smoothstep(0.03f, 0.46f, max(worldPos.y, 0.0f));
    float wallJunction = lowBand * smoothstep(0.22f, 0.88f, 1.0f - abs(N.y));
    float floorFilm = lowBand * smoothstep(0.34f, 0.92f, N.y) * 0.22f;
    float underside = (1.0f - smoothstep(0.30f, 1.15f, max(worldPos.y, 0.0f))) *
                      (1.0f - smoothstep(-0.78f, -0.18f, N.y)) * 0.42f;
    float grimeMottle = lerp(0.76f, 1.0f,
                             TriplanarWearField(worldPos * 0.72f + float3(13.0f, 13.0f, 13.0f), N));
    float contactGrime = saturate(wallJunction + floorFilm + underside) *
                         grimeMottle * grimeEligible;
    albedo = saturate(albedo * (1.0f - contactGrime * 0.16f));  // visible contact darkening (grounds junctions/undersides)
    roughness = saturate(roughness + contactGrime * 0.055f);
    ao = saturate(ao * (1.0f - contactGrime * 0.10f));

    float paintedOrWood = saturate(max(midRough, semanticWood * nonMetal) * (1.0f - cleanClass));
    float wearBase = TriplanarWearField(worldPos * 0.95f, N);
    const float wearEps = 0.090f;
    float wearT = TriplanarWearField((worldPos + T * wearEps) * 0.95f, N);
    float wearB = TriplanarWearField((worldPos + B * wearEps) * 0.95f, N);
    float wearN = TriplanarWearField((worldPos + N * wearEps) * 0.95f, N);
    float wearGradient = abs(wearT - wearBase) + abs(wearB - wearBase) + abs(wearN - wearBase);
    float handledHeight = 1.0f - smoothstep(2.6f, 4.8f, max(worldPos.y, 0.0f));
    float edgeWear = smoothstep(0.050f, 0.155f, wearGradient) *
                     paintedOrWood * handledHeight * (1.0f - contactGrime * 0.55f);
    float luma = dot(albedo, float3(0.2126f, 0.7152f, 0.0722f));
    float3 softened = lerp(albedo, luma.xxx, edgeWear * 0.16f);
    float3 worn = saturate(softened + (1.0f - softened) * (edgeWear * 0.035f));
    albedo = lerp(albedo, worn, edgeWear);
    roughness = saturate(roughness - edgeWear * 0.020f);
}

// Compute shader: One thread per pixel
[numthreads(8, 8, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID) {
    uint2 pixelCoord = dispatchThreadID.xy;

    // Early out if pixel is outside render target
    if (pixelCoord.x >= g_Width || pixelCoord.y >= g_Height) {
        return;
    }

    // Read visibility buffer
    uint2 visData = g_VisibilityBuffer[pixelCoord];
    uint triangleID = visData.x;
    uint instanceID = visData.y;

    // Check for background pixels (cleared to 0xFFFFFFFF)
    if (triangleID == 0xFFFFFFFF && instanceID == 0xFFFFFFFF) {
        // Write black/default values for background
        g_AlbedoOut[pixelCoord] = float4(0, 0, 0, 1);
        g_NormalRoughnessOut[pixelCoord] = float4(0.5, 0.5, 1.0, 1.0);  // Encoded +Z normal + max roughness
        g_EmissiveMetallicOut[pixelCoord] = float4(0, 0, 0, 0);
        g_MaterialExt0Out[pixelCoord] = float4(0.0f, 1.0f, 1.5f, 1.0f);
        g_MaterialExt1Out[pixelCoord] = float4(1.0f, 1.0f, 1.0f, 0.0f);
        g_MaterialExt2Out[pixelCoord] = float4(EncodeSurfaceClass(SURFACE_CLASS_DEFAULT), 0, 0, 0);
        return;
    }

    // CRITICAL: Bounds check instanceID before accessing g_Instances buffer
    // Without this, out-of-bounds instanceID values (from visibility buffer corruption
    // or race conditions) cause random garbage reads, leading to "random terrain" glitching.
    if (g_InstanceCount == 0 || instanceID >= g_InstanceCount) {
        g_AlbedoOut[pixelCoord] = float4(0, 0, 0, 1);
        g_NormalRoughnessOut[pixelCoord] = float4(0.5, 0.5, 1.0, 1.0);
        g_EmissiveMetallicOut[pixelCoord] = float4(0, 0, 0, 0);
        g_MaterialExt0Out[pixelCoord] = float4(0.0f, 1.0f, 1.5f, 1.0f);
        g_MaterialExt1Out[pixelCoord] = float4(1.0f, 1.0f, 1.0f, 0.0f);
        g_MaterialExt2Out[pixelCoord] = float4(EncodeSurfaceClass(SURFACE_CLASS_DEFAULT), 0, 0, 0);
        return;
    }

    // Fetch instance data
    VBInstanceData instance = g_Instances[instanceID];

    if (g_MeshCount == 0 || instance.meshIndex >= g_MeshCount) {
        return;
    }

    // Robustness: guard against invalid triangle IDs (can happen if the
    // visibility pass emits a global SV_PrimitiveID across instances on some
    // backends, or if instance/index counts are inconsistent).
    const uint first = instance.firstIndex;
    const uint count = instance.indexCount;
    const uint triFirst = first + triangleID * 3u;
    if (count < 3u || triFirst + 2u >= first + count) {
        g_AlbedoOut[pixelCoord] = float4(0, 0, 0, 1);
        g_NormalRoughnessOut[pixelCoord] = float4(0.5, 0.5, 1.0, 1.0);
        g_EmissiveMetallicOut[pixelCoord] = float4(0, 0, 0, 0);
        g_MaterialExt0Out[pixelCoord] = float4(0.0f, 1.0f, 1.5f, 1.0f);
        g_MaterialExt1Out[pixelCoord] = float4(1.0f, 1.0f, 1.0f, 0.0f);
        g_MaterialExt2Out[pixelCoord] = float4(EncodeSurfaceClass(SURFACE_CLASS_DEFAULT), 0, 0, 0);
        return;
    }

    VBMeshTableEntry mesh = g_MeshTable[instance.meshIndex];
    ByteAddressBuffer vertexBuffer = ResourceDescriptorHeap[mesh.vertexBufferIndex];
    ByteAddressBuffer indexBuffer = ResourceDescriptorHeap[mesh.indexBufferIndex];

    // Load triangle indices
    uint3 indices = LoadTriangleIndices(indexBuffer, triangleID, instance.firstIndex, instance.baseVertex, mesh.indexFormat);

    // Load vertices
    Vertex v0 = LoadVertex(vertexBuffer, indices.x, mesh.vertexStrideBytes);
    Vertex v1 = LoadVertex(vertexBuffer, indices.y, mesh.vertexStrideBytes);
    Vertex v2 = LoadVertex(vertexBuffer, indices.z, mesh.vertexStrideBytes);

    // Transform vertices to world space
    float3 worldPos0 = mul(instance.worldMatrix, float4(v0.position, 1.0)).xyz;
    float3 worldPos1 = mul(instance.worldMatrix, float4(v1.position, 1.0)).xyz;
    float3 worldPos2 = mul(instance.worldMatrix, float4(v2.position, 1.0)).xyz;

    // Read depth for current pixel
    float depth = g_DepthBuffer[pixelCoord];

    // Transform vertices to clip space
    float4 clipPos0 = mul(g_ViewProj, float4(worldPos0, 1.0));
    float4 clipPos1 = mul(g_ViewProj, float4(worldPos1, 1.0));
    float4 clipPos2 = mul(g_ViewProj, float4(worldPos2, 1.0));

    // Perspective divide to get NDC positions
    float3 ndc0 = clipPos0.xyz / clipPos0.w;
    float3 ndc1 = clipPos1.xyz / clipPos1.w;
    float3 ndc2 = clipPos2.xyz / clipPos2.w;

    // Convert to screen space [0, 1]
    float2 screen0 = float2(ndc0.x * 0.5 + 0.5, 0.5 - ndc0.y * 0.5);
    float2 screen1 = float2(ndc1.x * 0.5 + 0.5, 0.5 - ndc1.y * 0.5);
    float2 screen2 = float2(ndc2.x * 0.5 + 0.5, 0.5 - ndc2.y * 0.5);

    // Current pixel in screen space [0, 1]
    float2 pixelUV = (float2(pixelCoord) + 0.5) * float2(g_RcpWidth, g_RcpHeight);

    // Compute 2D screen-space barycentrics using edge function
    float3 bary = ComputeScreenSpaceBarycentrics(pixelUV, screen0, screen1, screen2);
    float3 screenBary = bary;

    // Apply perspective correction using 1/w interpolation
    float3 baryPersp = bary / float3(clipPos0.w, clipPos1.w, clipPos2.w);
    float baryPerspSum = baryPersp.x + baryPersp.y + baryPersp.z;
    if (baryPerspSum > 1e-7) {
        bary = baryPersp / baryPerspSum;
    }

    // Clamp to valid range for safety (saturate clamps each component to [0,1])
    bary = saturate(bary);

    // Interpolate vertex attributes using barycentric coordinates
    float2 texCoord = v0.texCoord * bary.x + v1.texCoord * bary.y + v2.texCoord * bary.z;

    float3 normalOS = v0.normal * bary.x + v1.normal * bary.y + v2.normal * bary.z;

    // Correct normal transform for non-uniform scale: inverse-transpose of upper-left 3x3.
    float3 normalWS = mul((float3x3)instance.normalMatrix, normalOS);
    if (!all(isfinite(normalWS)) || dot(normalWS, normalWS) < 1e-12f) {
        normalWS = float3(0.0f, 0.0f, 1.0f);
    } else {
        normalWS = normalize(normalWS);
    }

    float4 tangent = v0.tangent * bary.x + v1.tangent * bary.y + v2.tangent * bary.z;
    tangent.xyz = normalize(mul((float3x3)instance.worldMatrix, tangent.xyz));

    // Interpolate vertex color (used for biome data on terrain)
    float4 vertexColor = v0.color * bary.x + v1.color * bary.y + v2.color * bary.z;

    // Interpolate world position for biome sampling
    float3 worldPos = worldPos0 * bary.x + worldPos1 * bary.y + worldPos2 * bary.z;

    // Material evaluation: constants in g_Materials[instance.materialIndex] and optional bindless textures.
    // Default PBR values (mid-roughness, non-metallic)
    float3 albedo = float3(0.5, 0.5, 0.5);
    float roughness = 0.5;
    float metallic = 0.0;
    float ao = 1.0;
    float3 emissive = float3(0, 0, 0);
    float clearCoatWeight = 0.0f;
    float clearCoatRoughness = 1.0f;
    float transmission = 0.0f;
    float ior = 1.5f;
    float specularFactor = 1.0f;
    float3 specularColor = 1.0f;
    float anisotropy = 0.0f;
    float sheenWeight = 0.0f;
    float subsurfaceWrap = 0.0f;
    float normalMapSpecularVariance = 0.0f;
    uint materialClass = SURFACE_CLASS_DEFAULT;
    uint sceneMaterialClass = SCENE_MATERIAL_DEFAULT;

    float2 ddxUV = float2(0.0f, 0.0f);
    float2 ddyUV = float2(0.0f, 0.0f);
    float3 ddxWorld = float3(0.0f, 0.0f, 0.0f);
    float3 ddyWorld = float3(0.0f, 0.0f, 0.0f);

    if (g_MaterialCount > 0 && instance.materialIndex < g_MaterialCount) {
        VBMaterialConstants mat = g_Materials[instance.materialIndex];
        albedo = mat.albedo.rgb;
        metallic = mat.metallic;
        roughness = mat.roughness;
        ao = mat.ao;
        float occlusionStrength = saturate(mat.extraParams.x);
        float normalScale = max(mat.extraParams.y, 0.0f);
        anisotropy = saturate(mat.extraParams.z);
        float wetnessFactor = saturate(mat.extraParams.w);
        float proceduralMaskStrength = saturate(mat.transmissionParams.w);
        emissive = max(mat.emissiveFactorStrength.rgb, 0.0f) * max(mat.emissiveFactorStrength.w, 0.0f);
        emissive *= (1.0f + saturate(mat.transmissionParams.z) * 2.0f);
        clearCoatWeight = saturate(mat.coatParams.x);
        clearCoatRoughness = saturate(mat.coatParams.y);
        sheenWeight = saturate(mat.coatParams.z);
        subsurfaceWrap = saturate(mat.coatParams.w);
        transmission = saturate(mat.transmissionParams.x);
        ior = max(mat.transmissionParams.y, 1.0f);
        specularColor = saturate(mat.specularParams.rgb);
        specularFactor = saturate(mat.specularParams.w);
        materialClass = mat.materialClass;
        sceneMaterialClass = mat.policyParams.x;
        normalScale = min(normalScale, SurfaceNormalScaleCeiling(materialClass, roughness, metallic));
        float cinematicClearcoatBoost = SceneMaterialCinematicClearcoatBoost(sceneMaterialClass);
        float cinematicWetnessBoost = SceneMaterialCinematicWetnessBoost(sceneMaterialClass);
        float cinematicEmissiveBoost = SceneMaterialCinematicEmissiveBoost(sceneMaterialClass);
        proceduralMaskStrength = max(
            proceduralMaskStrength,
            SceneMaterialCinematicDetailFloor(sceneMaterialClass, materialClass));
        emissive *= (1.0f + cinematicEmissiveBoost);

        const bool wantsGrad =
            (mat.textureIndices.x != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices.y != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices.z != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices.w != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices2.x != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices2.y != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices3.x != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices3.y != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices3.z != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices3.w != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices4.x != INVALID_BINDLESS_INDEX) ||
            (proceduralMaskStrength > 0.001f);
        if (wantsGrad) {
            // Use perspective-correct UV gradients for proper mip selection.
            // This prevents texture shimmer that occurs when gradients ignore perspective.
            UVGradients uvGrad = ComputePerspectiveCorrectUVGradients(
                v0.texCoord, v1.texCoord, v2.texCoord,
                screen0, screen1, screen2,
                clipPos0.w, clipPos1.w, clipPos2.w,
                screenBary);
            ddxUV = uvGrad.ddx;
            ddyUV = uvGrad.ddy;
            WorldGradients worldGrad = ComputePerspectiveCorrectWorldGradients(
                worldPos0, worldPos1, worldPos2,
                screen0, screen1, screen2,
                clipPos0.w, clipPos1.w, clipPos2.w,
                screenBary);
            ddxWorld = worldGrad.ddx;
            ddyWorld = worldGrad.ddy;
        }

        float2 materialUV = texCoord;
        const bool hasAlbedoTexture = mat.textureIndices.x != INVALID_BINDLESS_INDEX;
        const bool hasNormalTexture = mat.textureIndices.y != INVALID_BINDLESS_INDEX;
        float3 albedoForHeight = albedo;
        float4 normalForHeight = float4(0.5f, 0.5f, 1.0f, 1.0f);
        if (hasAlbedoTexture) {
            Texture2D albedoHeightTex = ResourceDescriptorHeap[mat.textureIndices.x];
            albedoForHeight = albedoHeightTex.SampleGrad(g_Sampler, texCoord, ddxUV, ddyUV).rgb;
        }
        if (hasNormalTexture) {
            Texture2D normalHeightTex = ResourceDescriptorHeap[mat.textureIndices.y];
            normalForHeight = normalHeightTex.SampleGrad(g_Sampler, texCoord, ddxUV, ddyUV);
        }

        float parallaxStrength = MaterialHeightDetailStrength(
            mat.materialClass,
            sceneMaterialClass,
            proceduralMaskStrength,
            roughness,
            metallic);
        if (parallaxStrength > 0.001f) {
            materialUV = ApplyBoundedParallaxOffset(
                texCoord,
                worldPos,
                normalWS,
                tangent.xyz,
                tangent.w,
                mat.materialClass,
                sceneMaterialClass,
                parallaxStrength,
                albedoForHeight,
                normalForHeight,
                hasAlbedoTexture,
                hasNormalTexture);
        }

        if (mat.textureIndices.x != INVALID_BINDLESS_INDEX) {
            Texture2D albedoTex = ResourceDescriptorHeap[mat.textureIndices.x];
            // glTF/PBR: finalAlbedo = baseColorFactor * textureColor
            albedo *= albedoTex.SampleGrad(g_Sampler, materialUV, ddxUV, ddyUV).rgb;
        }

        // Biome terrain handling: vertex color encodes biome indices and blend weights
        // For terrain meshes, use the biome material system for proper PBR properties
        if (IsBiomeTerrain(vertexColor) && g_BiomeCount > 0) {
            // Sample biome material using world position and normal
            BiomeSurfaceData biomeSurface = SampleBiomeMaterial(worldPos, normalWS, vertexColor);
            // Override albedo with biome-computed color (includes height layers, slope blending)
            albedo = biomeSurface.albedo.rgb;
            // Use biome roughness/metallic (these will be overwritten below by texture sampling if present)
            roughness = biomeSurface.roughness;
            metallic = biomeSurface.metallic;
        } else {
            // Non-terrain: apply vertex color as a simple tint multiplier
            albedo *= vertexColor.rgb;
        }

        if (mat.textureIndices.y != INVALID_BINDLESS_INDEX) {
            Texture2D normalTex = ResourceDescriptorHeap[mat.textureIndices.y];
            // Most authored showcase normal maps are BC5: XY are stored and Z
            // must be reconstructed. Treat all tangent-space normal maps this
            // way so BC5 assets do not decode with a bogus blue channel.
            float4 normalSample = normalTex.SampleGrad(g_Sampler, materialUV, ddxUV, ddyUV);
            float2 nXY = normalSample.xy * 2.0f - 1.0f;
            nXY *= normalScale;
            float3 sampledNormalTS = normalSample.xyz * 2.0f - 1.0f;
            sampledNormalTS.xy *= normalScale;
            normalMapSpecularVariance = SpecularAAToksvigAlpha2Variance(
                sampledNormalTS,
                nXY,
                SurfaceNormalVarianceRoughnessBoost(materialClass, roughness, metallic));
            float3 nTS = normalize(float3(nXY, sqrt(saturate(1.0f - dot(nXY, nXY)))));

            float3 T = tangent.xyz;
            T = normalize(T - normalWS * dot(normalWS, T));
            float3 B = normalize(cross(normalWS, T) * tangent.w);
            float3x3 TBN = float3x3(T, B, normalWS);
            normalWS = normalize(mul(TBN, nTS));
        }

        // Occlusion texture (glTF): stored in R, applied only to indirect (AO).
        if (mat.textureIndices2.x != INVALID_BINDLESS_INDEX && occlusionStrength > 0.0f) {
            Texture2D occTex = ResourceDescriptorHeap[mat.textureIndices2.x];
            float occ = occTex.SampleGrad(g_Sampler, materialUV, ddxUV, ddyUV).r;
            ao *= lerp(1.0f, occ, occlusionStrength);
        }

        // Emissive texture (glTF): emissiveFactor * emissiveStrength * emissiveTexture.
        if (mat.textureIndices2.y != INVALID_BINDLESS_INDEX) {
            Texture2D emissiveTex = ResourceDescriptorHeap[mat.textureIndices2.y];
            emissive *= emissiveTex.SampleGrad(g_Sampler, materialUV, ddxUV, ddyUV).rgb;
        }

        // Metallic/roughness sampling:
        // - Treat as packed only when BOTH maps are present and refer to the same texture:
        //   roughness = G, metallic = B (glTF metallic-roughness convention).
        // - Otherwise treat them as separate scalar maps (scalar stored in R). If one map is
        //   missing, keep the constant material value for that channel.
        const uint metalIdx = mat.textureIndices.z;
        const uint roughIdx = mat.textureIndices.w;
        if (metalIdx != INVALID_BINDLESS_INDEX || roughIdx != INVALID_BINDLESS_INDEX) {
            const bool packedMR =
                (metalIdx != INVALID_BINDLESS_INDEX) &&
                (roughIdx != INVALID_BINDLESS_INDEX) &&
                (metalIdx == roughIdx);

            if (packedMR) {
                Texture2D mrTex = ResourceDescriptorHeap[metalIdx];
                float4 mr = mrTex.SampleGrad(g_Sampler, materialUV, ddxUV, ddyUV);
                roughness = mr.g;
                metallic = mr.b;
            } else {
                if (metalIdx != INVALID_BINDLESS_INDEX) {
                    Texture2D metalTex = ResourceDescriptorHeap[metalIdx];
                    metallic = metalTex.SampleGrad(g_Sampler, materialUV, ddxUV, ddyUV).r;
                }
                if (roughIdx != INVALID_BINDLESS_INDEX) {
                    Texture2D roughTex = ResourceDescriptorHeap[roughIdx];
                    roughness = roughTex.SampleGrad(g_Sampler, materialUV, ddxUV, ddyUV).r;
                }
            }
        }

        metallic = saturate(metallic);
        roughness = saturate(roughness);
        ao = saturate(ao);
        if (proceduralMaskStrength > 0.001f) {
            proceduralMaskStrength *= SurfaceProceduralDetailCeiling(
                mat.materialClass,
                roughness,
                metallic);
            proceduralMaskStrength *= ProceduralMaskFootprintFilter(
                ddxUV, ddyUV, ddxWorld, ddyWorld, mat.materialClass);
        }
        if (proceduralMaskStrength > 0.001f) {
            float mask = ProceduralMaterialMask(materialUV, worldPos, mat.materialClass);
            float albedoVariation = lerp(0.78f, 1.16f, mask);
            albedo = saturate(albedo * lerp(1.0f, albedoVariation, proceduralMaskStrength));
            albedo = ApplySceneMaterialCinematicColorLayer(
                albedo,
                sceneMaterialClass,
                mat.materialClass,
                mask,
                proceduralMaskStrength);
            roughness = saturate(lerp(roughness, roughness + (mask - 0.5f) * 0.35f, proceduralMaskStrength));
            normalWS = ApplyProceduralMicroNormal(
                normalWS,
                tangent.xyz,
                tangent.w,
                materialUV,
                worldPos,
                mat.materialClass,
                proceduralMaskStrength);
        }

        float detailNormalStrength = DetailNormalLayerStrength(
            mat.materialClass,
            sceneMaterialClass,
            proceduralMaskStrength,
            metallic);
        detailNormalStrength *= 1.0f - smoothstep(5.5f, 16.0f, distance(g_CameraPosition.xyz, worldPos));
        if (detailNormalStrength > 0.001f) {
            normalWS = ApplyDetailNormalLayer(
                normalWS,
                tangent.xyz,
                tangent.w,
                materialUV,
                worldPos,
                mat.materialClass,
                sceneMaterialClass,
                detailNormalStrength);
        }

        float roughnessBreakup = RoughnessBreakupStrength(
            mat.materialClass,
            sceneMaterialClass,
            proceduralMaskStrength,
            metallic);
        if (roughnessBreakup > 0.001f) {
            float wear = TriplanarWearField(worldPos * 1.37f + normalWS * 0.19f, normalWS);
            float fineWear = ValueNoise2D(materialUV * 37.0f + worldPos.xz * 0.11f);
            roughness = saturate(roughness + ((wear * 0.68f + fineWear * 0.32f) - 0.5f) * roughnessBreakup);
        }

        if (sceneMaterialClass == SCENE_MATERIAL_FABRIC) {
            subsurfaceWrap = max(subsurfaceWrap, 0.30f);
            transmission = max(transmission, 0.045f);
        }

        ApplyProceduralSurfaceDetailVB(
            worldPos,
            tangent.xyz,
            tangent.w,
            normalWS,
            albedo,
            roughness,
            ao,
            metallic,
            mat.materialClass);

        // KHR_materials_transmission: transmissionTexture stored in R.
        if (mat.textureIndices3.x != INVALID_BINDLESS_INDEX) {
            Texture2D transTex = ResourceDescriptorHeap[mat.textureIndices3.x];
            transmission *= transTex.SampleGrad(g_Sampler, materialUV, ddxUV, ddyUV).r;
        }

        // KHR_materials_clearcoat: clearcoatTexture stored in R.
        if (mat.textureIndices3.y != INVALID_BINDLESS_INDEX) {
            Texture2D ccTex = ResourceDescriptorHeap[mat.textureIndices3.y];
            clearCoatWeight *= ccTex.SampleGrad(g_Sampler, materialUV, ddxUV, ddyUV).r;
        }

        // KHR_materials_clearcoat: clearcoatRoughnessTexture stored in G (fallback to R if G is zero).
        if (mat.textureIndices3.z != INVALID_BINDLESS_INDEX) {
            Texture2D ccrTex = ResourceDescriptorHeap[mat.textureIndices3.z];
            float4 sample = ccrTex.SampleGrad(g_Sampler, materialUV, ddxUV, ddyUV);
            float ccr = sample.g;
            if (ccr == 0.0f && sample.r != 0.0f) {
                ccr = sample.r;
            }
            clearCoatRoughness *= ccr;
        }

        // KHR_materials_specular: specularTexture stored in A.
        if (mat.textureIndices3.w != INVALID_BINDLESS_INDEX) {
            Texture2D specTex = ResourceDescriptorHeap[mat.textureIndices3.w];
            specularFactor *= specTex.SampleGrad(g_Sampler, materialUV, ddxUV, ddyUV).a;
        }

        // KHR_materials_specular: specularColorTexture stored in RGB.
        if (mat.textureIndices4.x != INVALID_BINDLESS_INDEX) {
            Texture2D specColorTex = ResourceDescriptorHeap[mat.textureIndices4.x];
            specularColor *= specColorTex.SampleGrad(g_Sampler, materialUV, ddxUV, ddyUV).rgb;
        }

        clearCoatWeight = max(clearCoatWeight, cinematicClearcoatBoost);
        wetnessFactor = max(wetnessFactor, cinematicWetnessBoost);
        clearCoatWeight = saturate(clearCoatWeight);
        clearCoatRoughness = saturate(clearCoatRoughness);
        if (wetnessFactor > 0.001f) {
            float wetPatch = ProceduralMaterialMask(materialUV * 0.65f + worldPos.xz * 0.035f,
                                                    worldPos,
                                                    mat.materialClass);
            float wetStreak = smoothstep(0.18f, 0.82f, wetPatch);
            float pooledWetness = saturate(wetnessFactor * lerp(0.46f, 1.0f, wetStreak));
            float targetWetRoughness = lerp(0.18f, 0.045f, wetStreak);
            roughness = lerp(roughness, min(roughness, targetWetRoughness), pooledWetness);
            clearCoatWeight = saturate(max(clearCoatWeight, pooledWetness * 0.92f));
            clearCoatRoughness = lerp(clearCoatRoughness, min(clearCoatRoughness, lerp(0.18f, 0.055f, wetStreak)), pooledWetness);
            albedo = lerp(albedo, albedo * lerp(0.92f, 0.72f, wetStreak), wetnessFactor * 0.28f);
        }
        albedo = ApplySceneMaterialAlbedoPolicy(albedo, sceneMaterialClass);
        transmission = saturate(transmission);
        specularFactor = saturate(specularFactor);
        specularColor = saturate(specularColor);
    }

    // Apply class-owned roughness floors to prevent sparkle without flattening
    // intentionally glossy classes such as glass, water, and mirrors.
    {
        roughness = SpecularAAToksvigRoughness(roughness, normalMapSpecularVariance);
        roughness = max(saturate(roughness), SurfaceRoughnessFloor(materialClass, metallic));
        roughness = ApplyMetallicRoughnessFloor(roughness, metallic);

        if (clearCoatWeight > 0.01f) {
            clearCoatRoughness = max(saturate(clearCoatRoughness),
                                     SurfaceRoughnessFloor(materialClass, metallic));
            clearCoatRoughness = ApplyMetallicRoughnessFloor(clearCoatRoughness, metallic);
        }
    }

    // Write to G-buffers. Albedo alpha carries material AO; deferred lighting
    // uses the same channel for diffuse AO and horizon/specular occlusion.
    // Match the engine-wide convention used by Basic.hlsl / post-process:
    // normals are encoded to 0..1, roughness in .w.
    float3 nEnc = normalWS * 0.5f + 0.5f;
    g_AlbedoOut[pixelCoord] = float4(albedo, ao);
    g_NormalRoughnessOut[pixelCoord] = float4(nEnc, roughness);
    g_EmissiveMetallicOut[pixelCoord] = float4(emissive, metallic);
    g_MaterialExt0Out[pixelCoord] = float4(clearCoatWeight, clearCoatRoughness, ior, specularFactor);
    g_MaterialExt1Out[pixelCoord] = float4(specularColor, transmission);
    g_MaterialExt2Out[pixelCoord] =
        float4(EncodeSurfaceClass(materialClass), anisotropy, sheenWeight, EncodeSceneMaterialClass(sceneMaterialClass));
}
