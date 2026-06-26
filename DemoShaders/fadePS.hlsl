#include "partials/common.hlsli"
#include "partials/debug.hlsli"

cbuffer PerPass : register(b10)
{
    float u_Fadedness;
};

struct PSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target
{
    float4 outColor = float4(0.0, 0.0, 0.0, u_Fadedness);
    fdebugcolor(outColor, true);
    return outColor;
}
