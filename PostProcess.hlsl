// Matches the output signature of FullscreenVS in Deferred.hlsl.  The same
// bufferless fullscreen vertex shader is shared by lighting and post-process.
struct FullscreenVaryings
{
    float4 Position : SV_POSITION;
};

// These four descriptors are contiguous in the G-buffer SRV heap.  The
// geometry attachments are available to effects together with the HDR result
// produced by the deferred-lighting passes.
Texture2D gAlbedoBuffer : register(t0);
Texture2D gNormalBuffer : register(t1);
Texture2D gDepthBuffer : register(t2);
Texture2D gSceneColor : register(t3);
Texture2D gExposureHistory : register(t4);
SamplerState gLinearClamp : register(s0);

cbuffer PostProcessSettings : register(b0)
{
    uint gEyeAdaptationEnabled;
    uint gVignetteEnabled;
    float gExposureKey;
    float gVignetteStrength;
    float gVignetteInnerRadius;
    float gVignetteOuterRadius;
    float gDeltaTime;
    uint gResetExposure;
};

float Luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float AutomaticExposure()
{
    // Bilinear samples make the estimate vary continuously as geometry moves
    // across the grid. Including the background also avoids the abrupt sample
    // count changes that caused exposure flicker during small camera motions.
    static const uint sampleColumns = 8;
    static const uint sampleRows = 6;
    float luminanceSum = 0.0f;

    [unroll]
    for (uint y = 0; y < sampleRows; ++y)
    {
        [unroll]
        for (uint x = 0; x < sampleColumns; ++x)
        {
            const float2 uv = (float2(x, y) + 0.5f) /
                float2(sampleColumns, sampleRows);
            luminanceSum += Luminance(gSceneColor.SampleLevel(gLinearClamp, uv, 0).rgb);
        }
    }

    const float averageLuminance = luminanceSum / (sampleColumns * sampleRows);
    return clamp(gExposureKey / max(averageLuminance, 0.02f), 0.55f, 1.8f);
}

float ExposurePS(FullscreenVaryings input) : SV_TARGET
{
    const float targetExposure = AutomaticExposure();
    if (gResetExposure != 0)
        return targetExposure;

    const float previousExposure = gExposureHistory.Load(int3(0, 0, 0)).r;
    // Human vision responds to a bright scene more quickly than to darkness.
    const float adaptationRate = targetExposure < previousExposure ? 3.0f : 1.2f;
    const float blend = 1.0f - exp(-min(gDeltaTime, 0.1f) * adaptationRate);
    return lerp(previousExposure, targetExposure, blend);
}

float3 AcesToneMap(float3 color)
{
    return saturate((color * (2.51f * color + 0.03f)) /
        (color * (2.43f * color + 0.59f) + 0.14f));
}

float4 PostProcessPS(FullscreenVaryings input) : SV_TARGET
{
    uint width;
    uint height;
    gSceneColor.GetDimensions(width, height);

    const int3 pixel = int3(input.Position.xy, 0);
    float3 color = gSceneColor.Load(pixel).rgb;

    if (gEyeAdaptationEnabled != 0)
        color *= gExposureHistory.Load(int3(0, 0, 0)).r;

    if (gVignetteEnabled != 0)
    {
        const float2 uv = input.Position.xy / float2(width, height);
        float2 centered = uv * 2.0f - 1.0f;
        centered.x *= float(width) / float(height);
        const float edgeDistance = length(centered);
        const float vignette = 1.0f - smoothstep(
            gVignetteInnerRadius, gVignetteOuterRadius, edgeDistance);
        color *= lerp(1.0f, vignette, gVignetteStrength);
    }

    // Lighting is accumulated in HDR; tone mapping and gamma conversion are
    // output transforms rather than optional effects.
    color = AcesToneMap(color);
    color = pow(color, 1.0f / 2.2f);
    return float4(color, 1.0f);
}
