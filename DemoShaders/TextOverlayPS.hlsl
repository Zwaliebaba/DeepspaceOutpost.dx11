#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/hdr.hlsli"
#include "partials/debug.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

cbuffer PerDraw : register(b9)
{
    float4 u_Color;
};

struct PSInput
{
    float4 pos        : SV_Position;
    float2 v_TexCoord : TEXCOORD0;
    float4 v_Color    : TEXCOORD1;
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 texColor = u_Texture0.SampleBias(u_Texture0_sampler, input.v_TexCoord, -0.75);
    float4 color = u_Color * (input.v_Color * texColor);
    color = hdrcolor(color);
    fdebugcolor(color, isFrontFace);
    return color;
}
