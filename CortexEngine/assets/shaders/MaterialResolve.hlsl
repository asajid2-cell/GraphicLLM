// MaterialResolve.hlsl
// Phase 2.2: Full material evaluation compute shader
// Input: Visibility buffer (triangle/instance IDs)
// Output: G-buffer textures (albedo, normal+roughness, emissive+metallic)

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
// s0: Static sampler (linear wrap) for bindless textures

cbuffer ResolutionConstants : register(b0) {
    uint g_Width;
    uint g_Height;
    float g_RcpWidth;
    float g_RcpHeight;
    float4x4 g_ViewProj;  // View-projection matrix for computing clip-space barycentrics
    uint g_MaterialCount;
    uint g_MeshCount;
    uint2 _pad2;
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
    uint3 _pad;
};

// Vertex structure (matches C++ vertex layout: 48 bytes)
struct Vertex {
    float3 position;    // 12 bytes
    float3 normal;      // 12 bytes
    float4 tangent;     // 16 bytes
    float2 texCoord;    // 8 bytes
    // Total: 48 bytes
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
    float4 extraParams;            // x occlusion strength, y normal scale, z/w reserved
    float alphaCutoff;
    uint alphaMode; // 0=opaque, 1=mask, 2=blend
    uint doubleSided;
    uint _pad1;
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

SamplerState g_Sampler : register(s0);

static const uint INVALID_BINDLESS_INDEX = 0xFFFFFFFFu;

float2 ComputeUVGrad(float2 uv0, float2 uv1, float2 uv2,
                     float2 screen0, float2 screen1, float2 screen2,
                     bool wantDx)
{
    // Compute dUV/dx and dUV/dy from triangle UVs and screen-space positions.
    // screen* are in normalized [0,1]; convert to pixel units for stability.
    float2 p0 = screen0 * float2((float)g_Width, (float)g_Height);
    float2 p1 = screen1 * float2((float)g_Width, (float)g_Height);
    float2 p2 = screen2 * float2((float)g_Width, (float)g_Height);

    float2 dp1 = p1 - p0;
    float2 dp2 = p2 - p0;
    float2 duv1 = uv1 - uv0;
    float2 duv2 = uv2 - uv0;

    float det = dp1.x * dp2.y - dp1.y * dp2.x;
    if (abs(det) < 1e-8f) {
        return float2(0.0f, 0.0f);
    }
    float invDet = 1.0f / det;

    float2 dUVdx = (duv1 * dp2.y - duv2 * dp1.y) * invDet;
    float2 dUVdy = (-duv1 * dp2.x + duv2 * dp1.x) * invDet;
    return wantDx ? dUVdx : dUVdy;
}

// Load a vertex from the per-mesh vertex buffer (raw SRV -> ByteAddressBuffer)
Vertex LoadVertex(ByteAddressBuffer vertexBuffer, uint vertexIndex, uint vertexStrideBytes) {
    uint offset = vertexIndex * vertexStrideBytes;

    Vertex v;
    v.position = asfloat(vertexBuffer.Load3(offset + 0));
    v.normal = asfloat(vertexBuffer.Load3(offset + 12));
    v.tangent = asfloat(vertexBuffer.Load4(offset + 24));
    v.texCoord = asfloat(vertexBuffer.Load2(offset + 40));

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

// Compute barycentric coordinates for a point inside a triangle (2D screen space version)
float3 ComputeScreenSpaceBarycentrics(float2 p, float2 v0, float2 v1, float2 v2) {
    float2 e0 = v1 - v0;
    float2 e1 = v2 - v0;
    float2 e2 = p - v0;

    float d00 = dot(e0, e0);
    float d01 = dot(e0, e1);
    float d11 = dot(e1, e1);
    float d20 = dot(e2, e0);
    float d21 = dot(e2, e1);

    float denom = d00 * d11 - d01 * d01;

    // Handle degenerate triangles
    if (abs(denom) < 1e-7) {
        return float3(0.33, 0.33, 0.34);
    }

    float v = (d11 * d20 - d01 * d21) / denom;
    float w = (d00 * d21 - d01 * d20) / denom;
    float u = 1.0 - v - w;

    return float3(u, v, w);
}

// Compute screen-space derivatives for texture sampling
void ComputeScreenSpaceDerivatives(
    float2 uv0, float2 uv1, float2 uv2,
    float3 bary,
    out float2 ddx, out float2 ddy
) {
    // Approximate derivatives using barycentric gradients
    // This is a simplified version - proper implementation would use screen-space gradients
    ddx = (uv1 - uv0) * g_RcpWidth * 2.0;
    ddy = (uv2 - uv0) * g_RcpHeight * 2.0;
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
        g_NormalRoughnessOut[pixelCoord] = float4(0.0, 0.0, 1.0, 1.0);  // Up normal (world) + max roughness
        g_EmissiveMetallicOut[pixelCoord] = float4(0, 0, 0, 0);
        return;
    }

    // Fetch instance data
    VBInstanceData instance = g_Instances[instanceID];

    if (g_MeshCount == 0 || instance.meshIndex >= g_MeshCount) {
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

    // Apply perspective correction using 1/w interpolation
    float3 baryPersp = bary / float3(clipPos0.w, clipPos1.w, clipPos2.w);
    float baryPerspSum = baryPersp.x + baryPersp.y + baryPersp.z;
    if (baryPerspSum > 1e-7) {
        bary = baryPersp / baryPerspSum;
    }

    // Clamp to valid range for safety
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

    // Material evaluation: constants in g_Materials[instance.materialIndex] and optional bindless textures.
    // Default PBR values (mid-roughness, non-metallic)
    float3 albedo = float3(0.5, 0.5, 0.5);
    float roughness = 0.5;
    float metallic = 0.0;
    float ao = 1.0;
    float3 emissive = float3(0, 0, 0);

    float2 ddxUV = float2(0.0f, 0.0f);
    float2 ddyUV = float2(0.0f, 0.0f);

    if (g_MaterialCount > 0 && instance.materialIndex < g_MaterialCount) {
        VBMaterialConstants mat = g_Materials[instance.materialIndex];
        albedo = mat.albedo.rgb;
        metallic = mat.metallic;
        roughness = mat.roughness;
        ao = mat.ao;
        float occlusionStrength = saturate(mat.extraParams.x);
        float normalScale = max(mat.extraParams.y, 0.0f);
        emissive = max(mat.emissiveFactorStrength.rgb, 0.0f) * max(mat.emissiveFactorStrength.w, 0.0f);

        const bool wantsGrad =
            (mat.textureIndices.x != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices.y != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices.z != INVALID_BINDLESS_INDEX) ||
            (mat.textureIndices.w != INVALID_BINDLESS_INDEX);
        if (wantsGrad) {
            ddxUV = ComputeUVGrad(v0.texCoord, v1.texCoord, v2.texCoord, screen0, screen1, screen2, true);
            ddyUV = ComputeUVGrad(v0.texCoord, v1.texCoord, v2.texCoord, screen0, screen1, screen2, false);
        }

        if (mat.textureIndices.x != INVALID_BINDLESS_INDEX) {
            Texture2D albedoTex = ResourceDescriptorHeap[mat.textureIndices.x];
            albedo = albedoTex.SampleGrad(g_Sampler, texCoord, ddxUV, ddyUV).rgb;
        }

        if (mat.textureIndices.y != INVALID_BINDLESS_INDEX) {
            Texture2D normalTex = ResourceDescriptorHeap[mat.textureIndices.y];
            float3 nTS = normalTex.SampleGrad(g_Sampler, texCoord, ddxUV, ddyUV).xyz * 2.0f - 1.0f;
            nTS.xy *= normalScale;

            float3 T = tangent.xyz;
            T = normalize(T - normalWS * dot(normalWS, T));
            float3 B = normalize(cross(normalWS, T) * tangent.w);
            float3x3 TBN = float3x3(T, B, normalWS);
            normalWS = normalize(mul(TBN, nTS));
        }

        // Occlusion texture (glTF): stored in R, applied only to indirect (AO).
        if (mat.textureIndices2.x != INVALID_BINDLESS_INDEX && occlusionStrength > 0.0f) {
            Texture2D occTex = ResourceDescriptorHeap[mat.textureIndices2.x];
            float occ = occTex.SampleGrad(g_Sampler, texCoord, ddxUV, ddyUV).r;
            ao *= lerp(1.0f, occ, occlusionStrength);
        }

        // Emissive texture (glTF): emissiveFactor * emissiveStrength * emissiveTexture.
        if (mat.textureIndices2.y != INVALID_BINDLESS_INDEX) {
            Texture2D emissiveTex = ResourceDescriptorHeap[mat.textureIndices2.y];
            emissive *= emissiveTex.SampleGrad(g_Sampler, texCoord, ddxUV, ddyUV).rgb;
        }

        // glTF metallic-roughness convention:
        // - If metallic+roughness are the same texture (or one is missing), treat it as packed:
        //   roughness = G, metallic = B.
        // - Otherwise, sample separate scalar maps (assume scalar stored in R).
        const uint metalIdx = mat.textureIndices.z;
        const uint roughIdx = mat.textureIndices.w;
        if (metalIdx != INVALID_BINDLESS_INDEX || roughIdx != INVALID_BINDLESS_INDEX) {
            if (metalIdx == roughIdx || metalIdx == INVALID_BINDLESS_INDEX || roughIdx == INVALID_BINDLESS_INDEX) {
                const uint mrIdx = (roughIdx != INVALID_BINDLESS_INDEX) ? roughIdx : metalIdx;
                Texture2D mrTex = ResourceDescriptorHeap[mrIdx];
                float4 mr = mrTex.SampleGrad(g_Sampler, texCoord, ddxUV, ddyUV);
                roughness = mr.g;
                metallic = mr.b;
            } else {
                Texture2D metalTex = ResourceDescriptorHeap[metalIdx];
                Texture2D roughTex = ResourceDescriptorHeap[roughIdx];
                metallic = metalTex.SampleGrad(g_Sampler, texCoord, ddxUV, ddyUV).r;
                roughness = roughTex.SampleGrad(g_Sampler, texCoord, ddxUV, ddyUV).r;
            }
        }

        metallic = saturate(metallic);
        roughness = saturate(roughness);
        ao = saturate(ao);
    }

    // Write to G-buffers
    // Match the engine-wide G-buffer convention used by Basic.hlsl / post-process:
    // normals encoded to 0..1, roughness in .w.
    float3 nEnc = normalWS * 0.5f + 0.5f;
    g_AlbedoOut[pixelCoord] = float4(albedo, ao);
    g_NormalRoughnessOut[pixelCoord] = float4(nEnc, roughness);
    g_EmissiveMetallicOut[pixelCoord] = float4(emissive, metallic);
}
