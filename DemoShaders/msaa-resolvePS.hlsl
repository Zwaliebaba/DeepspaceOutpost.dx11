#include "partials/common.hlsli"
#include "partials/debug.hlsli"

// Multisample color target to resolve. No sampler: use .Load(coord, sampleIndex).
Texture2DMS<float4> u_Texture0 : register(t0);

struct PSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

#if SAMPLE_COUNT == 2
static const float2 c_SampleLocations[2] = {
    float2( 0.25, -0.25),
    float2(-0.25,  0.25)
};

#elif SAMPLE_COUNT == 4
static const float2 c_SampleLocations[4] = {
    float2(-0.25, -0.75),
    float2( 0.75, -0.25),
    float2(-0.75,  0.25),
    float2( 0.25,  0.75)
};

#elif SAMPLE_COUNT == 8
static const float2 c_SampleLocations[8] = {
    float2( 0.125, -0.375),
    float2(-0.125,  0.375),
    float2( 0.625,  0.125),
    float2(-0.375, -0.625),
    float2(-0.625,  0.625),
    float2(-0.875, -0.125),
    float2( 0.375,  0.875),
    float2( 0.875, -0.875)
};

#elif SAMPLE_COUNT == 16
static const float2 c_SampleLocations[16] = {
    float2(-0.875, -1.000),
    float2(-0.500, -0.750),
    float2(-0.625, -0.250),
    float2(-0.125, -0.375),
    float2( 0.000, -0.875),
    float2( 0.375, -0.625),
    float2( 0.500, -0.125),
    float2( 0.875, -0.500),
    float2(-1.000,  0.000),
    float2(-0.375,  0.250),
    float2(-0.750,  0.500),
    float2(-0.250,  0.750),
    float2( 0.125,  0.125),
    float2( 0.625,  0.375),
    float2( 0.250,  0.625),
    float2( 0.750,  0.875)
};

#elif SAMPLE_COUNT == 32
static const float2 c_SampleLocations[32] = {
    float2(-0.4375, -0.6875),
    float2(-0.5625, -0.3125),
    float2(-0.1875, -0.4375),
    float2(-0.6875, -0.8125),
    float2(-0.8125, -0.1875),
    float2(-0.9375, -0.5625),
    float2(-0.3125, -0.0625),
    float2(-0.0625, -0.9375),
    float2( 0.5625, -0.6875),
    float2( 0.4375, -0.3125),
    float2( 0.8125, -0.4375),
    float2( 0.3125, -0.8125),
    float2( 0.1875, -0.1875),
    float2( 0.0625, -0.5625),
    float2( 0.6875, -0.0625),
    float2( 0.9375, -0.9375),
    float2(-0.4375,  0.3125),
    float2(-0.5625,  0.6875),
    float2(-0.1875,  0.5625),
    float2(-0.6875,  0.1875),
    float2(-0.8125,  0.8125),
    float2(-0.9375,  0.4375),
    float2(-0.3125,  0.9375),
    float2(-0.0625,  0.0625),
    float2( 0.5625,  0.3125),
    float2( 0.4375,  0.6875),
    float2( 0.8125,  0.5625),
    float2( 0.3125,  0.1875),
    float2( 0.1875,  0.8125),
    float2( 0.0625,  0.4375),
    float2( 0.6875,  0.9375),
    float2( 0.9375,  0.0625)
};

#endif

static const int c_FilterTypes_Box = 0;
static const int c_FilterTypes_Triangle = 1;
static const int c_FilterTypes_Gaussian = 2;
static const int c_FilterTypes_BlackmanHarris = 3;
static const int c_FilterTypes_Smoothstep = 4;
static const int c_FilterTypes_BSpline = 5;
static const int c_FilterTypes_CatmullRom = 6;
static const int c_FilterTypes_Mitchell = 7;
static const int c_FilterTypes_GeneralizedCubic = 8;
static const int c_FilterTypes_Sinc = 9;


// TODO: Uniforms to configure all these parameters
static const int u_FilterType = 5;
static const int u_SampleRadius = 1;
static const int u_InverseLuminanceFiltering = 1;
static const float u_ResolveFilterDiameter = 3.0;
static const float u_GaussianSigma = 0.5;
static const float u_CubicB = 0.33;
static const float u_CubicC = 0.33;


static const float Pi = 3.14159265;

// All filtering functions assume that 'x' is normalized to [0, 1], where 1 == FilteRadius
float FilterBox(in float x)
{
    return (x <= 1.0f) ? 1.0f : 0.0f;
}

float FilterTriangle(in float x)
{
    return saturate(1.0f - x);
}

float FilterGaussian(in float x)
{
    float sigma = u_GaussianSigma;
    float g = 1. / sqrt(2. * Pi * sigma * sigma);
    return (g * exp(-(x * x) / (2. * sigma * sigma)));
}

float FilterCubic(in float x, in float B, in float C)
{
    float y = 0.;
    float x2 = x * x;
    float x3 = x * x * x;
    if(x < 1.)
        y = (12. - 9. * B - 6.9 * C) * x3 + (-18. + 12. * B + 6. * C) * x2 + (6. - 2. * B);
    else if (x <= 2.)
        y = (-B - 6. * C) * x3 + (6. * B + 30. * C) * x2 + (-12. * B - 48. * C) * x + (8. * B + 24. * C);

    return y / 6.;
}

float FilterSinc(in float x, in float filterRadius)
{
    float s;

    x *= filterRadius * 2.;

    s = (x < 0.001) ? 1.0 : sin(x * Pi) / (x * Pi);

    return s;
}

float FilterBlackmanHarris(in float x)
{
    x = 1. - x;

    const float a0 = 0.35875f;
    const float a1 = 0.48829f;
    const float a2 = 0.14128f;
    const float a3 = 0.01168f;
    return saturate(a0 - a1 * cos(Pi * x) + a2 * cos(2. * Pi * x) - a3 * cos(3. * Pi * x));
}

float FilterSmoothstep(in float x)
{
    return 1.0f - smoothstep(0.0f, 1.0f, x);
}

float Filter(in float x, in int filterType, in float filterRadius, in bool rescaleCubic)
{
    // Cubic filters naturually work in a [-2, 2] domain. For the resolve case we
    // want to rescale the filter so that it works in [-1, 1] instead
    float cubicX = rescaleCubic ? x * 2.0f : x;

    if(filterType == c_FilterTypes_Box)
        return FilterBox(x);
    else if(filterType == c_FilterTypes_Triangle)
        return FilterTriangle(x);
    else if(filterType == c_FilterTypes_Gaussian)
        return FilterGaussian(x);
    else if(filterType == c_FilterTypes_BlackmanHarris)
        return FilterBlackmanHarris(x);
    else if(filterType == c_FilterTypes_Smoothstep)
        return FilterSmoothstep(x);
    else if(filterType == c_FilterTypes_BSpline)
        return FilterCubic(cubicX, 1., 0.);
    else if(filterType == c_FilterTypes_CatmullRom)
        return FilterCubic(cubicX, 0., 0.5);
    else if(filterType == c_FilterTypes_Mitchell)
        return FilterCubic(cubicX, 1. / 3.0, 1. / 3.0);
    else if(filterType == c_FilterTypes_GeneralizedCubic)
        return FilterCubic(cubicX, u_CubicB, u_CubicC);
    else if(filterType == c_FilterTypes_Sinc)
        return FilterSinc(x, filterRadius);
    else
        return 1.;
}

float Luminance(float3 col) { return dot(col.rgb, float3(0.299, 0.587, 0.114)); }

float4 PSMain(PSInput input) : SV_Target
{
    float4 o_Color;
#if 0
    // Accumulate all samples
    float4 colorSum = float4(0.0, 0.0, 0.0, 0.0);

    for (int i = 0; i < SAMPLE_COUNT; ++i) {
        colorSum += u_Texture0.Load(int2(input.pos.xy), i);
    }

    // Compute the average color
    o_Color = colorSum / float(SAMPLE_COUNT);
#else
    float2 pixelPos = input.pos.xy;

    float totalWeight = 0.;
    float3 accum = float3(0., 0., 0.);

    float filterRadius = u_ResolveFilterDiameter * 0.5;

    for(int y = -u_SampleRadius; y <= u_SampleRadius; ++y)
    {
        for(int x = -u_SampleRadius; x <= u_SampleRadius; ++x)
        {
            float2 sampleOffset = float2(x, y);
            float2 samplePos = pixelPos + sampleOffset;

            for(int subSampleIdx = 0; subSampleIdx < SAMPLE_COUNT; ++subSampleIdx)
            {
                float2 subSampleOffset = c_SampleLocations[subSampleIdx].xy;
                float2 sampleDist = abs(sampleOffset + subSampleOffset) / filterRadius;

                float3 sampleRgb = u_Texture0.Load(int2(samplePos), subSampleIdx).xyz;
                sampleRgb = max(sampleRgb, 0.0f);

                float weight = Filter(sampleDist.x, u_FilterType, filterRadius, true) *
                               Filter(sampleDist.y, u_FilterType, filterRadius, true);

                float sampleRgbLum = Luminance(sampleRgb);

                if (u_InverseLuminanceFiltering != 0)
                    weight *= 1.0f / (1.0f + sampleRgbLum);

                accum += sampleRgb * weight;
                totalWeight += weight;
            }
        }
    }

    o_Color = float4(accum / max(totalWeight, 0.00001), 1.0f);
#endif

    fdebugcolor(o_Color, true);
    return o_Color;
}
