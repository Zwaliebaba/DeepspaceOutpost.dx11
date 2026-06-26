#include "partials/common.hlsli"

// FSR [EASU] Edge Adaptive Spatial Upsampling -- fullscreen post pass.
// 32-bit (F) path. (The 16-bit A_HALF path is available in the headers but is not
// selected here for broad SM5.0 compatibility.)

// Activate the AMD FFX HLSL GPU code paths, then bring in the math library.
#define A_GPU  1
#define A_HLSL 1
#include "partials/ffx-a.hlsli"

#define FSR_EASU_F 1
#include "partials/ffx-fsr1.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

cbuffer PerPass : register(b10)
{
    float2 u_OutputSize;
    float2 _pad0;
    uint4  u_Con0;
    uint4  u_Con1;
    uint4  u_Con2;
    uint4  u_Con3;
};

// EASU input callbacks: gather4 for each color channel at texture coordinate 'p'.
AF4 FsrEasuRF(AF2 p) { return u_Texture0.GatherRed  (u_Texture0_sampler, p); }
AF4 FsrEasuGF(AF2 p) { return u_Texture0.GatherGreen(u_Texture0_sampler, p); }
AF4 FsrEasuBF(AF2 p) { return u_Texture0.GatherBlue (u_Texture0_sampler, p); }

struct PSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target
{
    AU2 gxy = AU2(input.uv * u_OutputSize);
    AF3 Gamma2Color = AF3(0.0, 0.0, 0.0);
    FsrEasuF(Gamma2Color, gxy, u_Con0, u_Con1, u_Con2, u_Con3);
    return float4(Gamma2Color, 1.0);
}
