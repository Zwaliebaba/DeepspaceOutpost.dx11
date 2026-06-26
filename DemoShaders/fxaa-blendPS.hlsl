#include "partials/common.hlsli"
#include "partials/fxaa.hlsli"
#include "partials/tonemap.hlsli"

// Color texture to be antialiased + its sampler (shared index 0).
Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

// Fullscreen post-pass parameters.
//   u_ScreenSize.xy = (width, height)
//   u_ScreenSize.zw = (1/width, 1/height)  -> FXAA rcpFrame
cbuffer PerPass : register(b10)
{
    float4 u_ScreenSize;
};

struct PSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target
{
    // Bundle the register-bound texture + sampler for the FXAA library.
    FxaaTex tex;
    tex.smp = u_Texture0_sampler;
    tex.tex = u_Texture0;

    float4 rgba = FxaaPixelShader(
        // pos
        input.uv,
        // tex
        tex,
        // fxaaQualityRcpFrame
        u_ScreenSize.zw,
        // fxaaQualitySubpix
        1.0,
        // fxaaQualityEdgeThreshold
        0.063,
        // fxaaQualityEdgeThresholdMin
        0.0312);

    return tonemap(rgba);
}
