#include "partials/common.hlsli"

// FSR [RCAS] Robust Contrast Adaptive Sharpening -- fullscreen post pass.
// Apply after EASU as a separate pass. 32-bit (F) path.
//
// Optional defines (supply via -D at compile time):
//   FSR_RCAS_DENOISE           -- reduce sharpening of detected noise.
//   FSR_RCAS_PASSTHROUGH_ALPHA -- pass input alpha through to output.

// Activate the AMD FFX HLSL GPU code paths, then bring in the math library.
#define A_GPU  1
#define A_HLSL 1
#include "partials/ffx-a.hlsli"

#define FSR_RCAS_F 1
#include "partials/ffx-fsr1.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

cbuffer PerPass : register(b10)
{
    float2 u_OutputSize;
    float2 _pad0;
    uint4  u_Con0;
};

// RCAS input callbacks.
AF4 FsrRcasLoadF(ASU2 p) { return u_Texture0.Load(int3(p, 0)); }
void FsrRcasInputF(inout AF1 r, inout AF1 g, inout AF1 b) {}

struct PSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target
{
    AU2 gxy = AU2(input.uv * u_OutputSize);
    AF3 Gamma2Color = AF3(0.0, 0.0, 0.0);
#ifdef FSR_RCAS_PASSTHROUGH_ALPHA
    AF1 alpha = 1.0;
    FsrRcasF(Gamma2Color.r, Gamma2Color.g, Gamma2Color.b, alpha, gxy, u_Con0);
    return float4(Gamma2Color, alpha);
#else
    FsrRcasF(Gamma2Color.r, Gamma2Color.g, Gamma2Color.b, gxy, u_Con0);
    return float4(Gamma2Color, 1.0);
#endif
}
