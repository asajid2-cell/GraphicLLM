Texture2D<float4> g_HDRScene : register(t0);
RWStructuredBuffer<float4> g_ExposureState : register(u0);

cbuffer ExposureControls : register(b0)
{
    float g_ManualExposureCompensation;
    float g_DeltaTime;
    uint  g_FrameWidth;
    uint  g_FrameHeight;
};

groupshared uint g_Histogram[64];

static const float kMinLogLum = -10.0f;
static const float kMaxLogLum =  6.0f;

float Luminance(float3 color)
{
    return dot(max(color, 0.0f.xxx), float3(0.2126f, 0.7152f, 0.0722f));
}

uint HistogramBin(float lum)
{
    float logLum = log2(max(lum, 1e-5f));
    float t = saturate((logLum - kMinLogLum) / (kMaxLogLum - kMinLogLum));
    return min(63u, (uint)(t * 63.0f + 0.5f));
}

float BinToLuminance(float bin)
{
    float t = saturate((bin + 0.5f) / 64.0f);
    return exp2(lerp(kMinLogLum, kMaxLogLum, t));
}

[numthreads(256, 1, 1)]
void CSMain(uint3 groupThreadId : SV_GroupThreadID)
{
    const uint tid = groupThreadId.x;
    if (tid < 64u)
    {
        g_Histogram[tid] = 0u;
    }
    GroupMemoryBarrierWithGroupSync();

    const uint width = max(g_FrameWidth, 1u);
    const uint height = max(g_FrameHeight, 1u);
    const uint sampleCountX = 64u;
    const uint sampleCountY = 36u;
    const uint sampleCount = sampleCountX * sampleCountY;

    for (uint sampleIndex = tid; sampleIndex < sampleCount; sampleIndex += 256u)
    {
        uint sx = sampleIndex % sampleCountX;
        uint sy = sampleIndex / sampleCountX;
        uint2 pixel = uint2(
            min(width - 1u, (sx * width) / sampleCountX),
            min(height - 1u, (sy * height) / sampleCountY));

        float lum = Luminance(g_HDRScene.Load(int3(pixel, 0)).rgb);
        InterlockedAdd(g_Histogram[HistogramBin(lum)], 1u);
    }

    GroupMemoryBarrierWithGroupSync();

    if (tid == 0u)
    {
        const uint lowCut = sampleCount / 50u;             // 2% black trim
        const uint highCut = sampleCount - sampleCount / 8u; // 12.5% highlight/window trim
        const uint targetRank = lowCut + ((highCut - lowCut) * 45u) / 100u;
        uint cumulative = 0u;
        uint meteredBin = 28u;

        [unroll]
        for (uint bin = 0u; bin < 64u; ++bin)
        {
            uint count = g_Histogram[bin];
            uint next = cumulative + count;
            if (targetRank >= cumulative && targetRank < next)
            {
                meteredBin = bin;
            }
            cumulative = next;
        }

        float meteredLum = (cumulative > lowCut) ? BinToLuminance((float)meteredBin) : 0.18f;

        float compensation = clamp(g_ManualExposureCompensation, 0.05f, 8.0f);
        float targetExposure = clamp((0.18f / max(meteredLum, 1e-4f)) * compensation, 0.08f, 8.0f);

        float4 previous = g_ExposureState[0];
        float previousExposure = (previous.w > 0.5f) ? previous.x : compensation;
        float speedUp = 1.8f;
        float speedDown = 1.2f;
        float adaptSpeed = (targetExposure > previousExposure) ? speedUp : speedDown;
        float blend = 1.0f - exp(-max(g_DeltaTime, 1e-4f) * adaptSpeed);
        float adaptedExposure = lerp(previousExposure, targetExposure, saturate(blend));

        g_ExposureState[0] = float4(adaptedExposure, meteredLum, targetExposure, 1.0f);
    }
}
