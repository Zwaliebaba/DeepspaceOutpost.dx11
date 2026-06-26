#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/debug.hlsli"

Texture2DArray u_Texture0         : register(t0);
SamplerState   u_Texture0_sampler : register(s0);

cbuffer PerDraw : register(b9)
{
    float4 u_Color;
};

struct PSInput
{
    float4 pos          : SV_Position;
    float3 uv           : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = u_Color;
    color = color * u_Texture0.Sample(u_Texture0_sampler, input.uv);
    color = ffog(color, input.fogViewSpace);
    fdebugcolor(color, isFrontFace);
    return color;
}
