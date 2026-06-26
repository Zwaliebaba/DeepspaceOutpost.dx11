#ifndef TONEMAP_HLSLI
#define TONEMAP_HLSLI

#include "common.hlsli"

//
// Tone mapping utility functions (fragment stage)
//
// TONEMAP_TYPE values:
//    0 = no tone mapping
//    1 = use in-level tone mapping parameters
//    2 = use sphere world tone mapping parameters
//

#if TONEMAP_TYPE != 0

float rgb_to_luma(float3 color)
{
    return dot(color, float3(0.2126, 0.7152, 0.0722));
}

float3 tonemap_uncharted2_apply(float3 x)
{
    float A = 0.15; float B = 0.50; float C = 0.10;
    float D = 0.20; float E = 0.02; float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float tonemap_uncharted2_apply(float x)
{
    float A = 0.15; float B = 0.50; float C = 0.10;
    float D = 0.20; float E = 0.02; float F = 0.30;
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

static const float uncharted_W = 11.2;
static const float uncharted_exposure = 3.0;

float3 tonemap_uncharted2(float3 color)
{
    float3 curr = tonemap_uncharted2_apply(uncharted_exposure * color);
    float3 whiteScale = 1.0 / tonemap_uncharted2_apply((float3)uncharted_W);
    return curr * whiteScale;
}

float tonemap_uncharted2(float color)
{
    float curr = tonemap_uncharted2_apply(uncharted_exposure * color);
    float whiteScale = 1.0 / tonemap_uncharted2_apply(uncharted_W);
    return curr * whiteScale;
}

float3 tonemap_unreal(float3 x)
{
    // Unreal 3 "Color Grading"; gamma 2.2 baked in (don't use with sRGB conversion)
    return x / (x + 0.155) * 1.019;
}

float tonemap_unreal(float x)
{
    return x / (x + 0.155) * 1.019;
}

float3 tonemap_reinhard(float3 x) { return x / (1.0 + x); }
float  tonemap_reinhard(float x)  { return x / (1.0 + x); }

#if TONEMAP_TYPE == 1
static const float reinhard2_white = 1.1;
static const float reinhard2_white_factor = 1.0 / (1.1 * 1.1);
#elif TONEMAP_TYPE == 2
static const float reinhard2_white = 1.2;
static const float reinhard2_white_factor = 1.0 / (1.2 * 1.2);
#else
static const float reinhard2_white = 1.0;
static const float reinhard2_white_factor = 1.0;
#endif

float3 tonemap_reinhard2(float3 x)
{
    return (x * (1.0 + x * reinhard2_white_factor)) / (1.0 + x);
}
float tonemap_reinhard2(float x)
{
    return (x * (1.0 + x * reinhard2_white_factor)) / (1.0 + x);
}

float3 tonemap_reinhard2_luma(float3 color)
{
    float luma = rgb_to_luma(color);
    float toneMappedLuma = luma * (1.0 + luma * reinhard2_white_factor) / (1.0 + luma);
    color *= toneMappedLuma / luma;
    return color;
}

// Uchimura 2017, "HDR theory and practice"
float3 tonemap_uchimura(float3 x, float P, float a, float m, float l, float c, float b)
{
    float l0 = ((P - m) * l) / a;
    float L0 = m - m / a;
    float L1 = m + (1.0 - m) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    float3 w0 = (float3)(1.0 - smoothstep(0.0, m, x));
    float3 w2 = (float3)(step(m + l0, x));
    float3 w1 = (float3)(1.0 - w0 - w2);

    float3 T = (float3)(m * pow(x / m, (float3)c) + b);
    float3 S = (float3)(P - (P - S1) * exp(CP * (x - S0)));
    float3 L = (float3)(m + a * (x - m));

    return T * w0 + L * w1 + S * w2;
}

float tonemap_uchimura(float x, float P, float a, float m, float l, float c, float b)
{
    float l0 = ((P - m) * l) / a;
    float L0 = m - m / a;
    float L1 = m + (1.0 - m) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    float w0 = 1.0 - smoothstep(0.0, m, x);
    float w2 = step(m + l0, x);
    float w1 = 1.0 - w0 - w2;

    float T = m * pow(x / m, c) + b;
    float S = P - (P - S1) * exp(CP * (x - S0));
    float L = m + a * (x - m);

    return T * w0 + L * w1 + S * w2;
}

float3 tonemap_uchimura(float3 x)
{
    const float P = 1.0; const float a = 1.0; const float m = 0.22;
    const float l = 0.4; const float c = 1.33; const float b = 0.0;
    return tonemap_uchimura(x, P, a, m, l, c, b);
}

float tonemap_uchimura(float x)
{
    const float P = 1.0; const float a = 1.0; const float m = 0.22;
    const float l = 0.4; const float c = 1.33; const float b = 0.0;
    return tonemap_uchimura(x, P, a, m, l, c, b);
}

float3 tonemap_aces(float3 x)
{
    // Narkowicz 2015, "ACES Filmic Tone Mapping Curve"
    const float a = 2.51; const float b = 0.03; const float c = 2.43;
    const float d = 0.59; const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float tonemap_aces(float x)
{
    const float a = 2.51; const float b = 0.03; const float c = 2.43;
    const float d = 0.59; const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

float tonemap_lottes(float x)
{
    // Lottes 2016, "Advanced Techniques and Optimization of HDR Color Pipelines"
    const float a = 1.6; const float d = 0.977; const float hdrMax = 8.0;
    const float midIn = 0.18; const float midOut = 0.267;
    const float b =
        (-pow(midIn, a) + pow(hdrMax, a) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    const float c =
        (pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    return pow(x, a) / (pow(x, a * d) * b + c);
}

float3 tonemap_lottes(float3 x)
{
#if TONEMAP_TYPE == 1
    const float a = 1.3;
#elif TONEMAP_TYPE == 2
    const float a = 0.7;
#else
    const float a = 1.6;
#endif
    const float d = 0.977; const float hdrMax = 8.0;
    const float midIn = 0.18; const float midOut = 0.267;
    const float b =
        (-pow(midIn, a) + pow(hdrMax, a) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    const float c =
        (pow(hdrMax, a * d) * pow(midIn, a) - pow(hdrMax, a) * pow(midIn, a * d) * midOut) /
        ((pow(hdrMax, a * d) - pow(midIn, a * d)) * midOut);
    return pow(x, (float3)a) / (pow(x, (float3)(a * d)) * b + c);
}

// Applies the default tone mapping algorithm. Alpha is discarded.
float4 tonemap(float4 color)
{
    float4 outColor = color;
#if USE_HDR
    // Don't tone map, just amplify color values for HDR
    outColor.rgb *= 1.5;
#else
#if TONEMAP_TYPE == 1 || TONEMAP_TYPE == 2
    outColor = float4(tonemap_reinhard2(outColor.rgb), 1.0);
#endif
#endif
    return outColor;
}

#else
// Tone mapping disabled, just pass through.
float4 tonemap(float4 color) { return color; }
#endif

#endif // TONEMAP_HLSLI
