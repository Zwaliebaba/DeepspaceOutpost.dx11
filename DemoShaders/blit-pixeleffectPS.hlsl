#include "partials/common.hlsli"
#include "partials/debug.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

cbuffer PerPass : register(b10)
{
    float4 u_Color;
};

struct PSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target
{
    float4 texColor = u_Texture0.Sample(u_Texture0_sampler, input.uv);
#if DISCARD_OPTIMAL
    if (texColor.a < 0.025)
        discard;
#endif
    float4 outColor = u_Color * texColor;
    fdebugcolor(outColor, true);
    return outColor;
}
