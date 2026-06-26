#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
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
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = u_Color;
    color = color * u_Texture0.Sample(u_Texture0_sampler, input.v_TexCoord);
    color *= 4.0f;
    float4 o_Color = clamp(color, 0.0, 1.0);
    fdebugcolor(o_Color, isFrontFace);
    return o_Color;
}
