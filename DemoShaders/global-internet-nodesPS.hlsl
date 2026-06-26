#include "partials/common.hlsli"
#include "partials/fog.hlsli"
#include "partials/matrices.hlsli"

cbuffer PerDraw : register(b9)
{
    float4 u_Color;
};

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

struct PSInput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord   : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_Target
{
    float4 o_Color = ffog(u_Color * u_Texture0.Sample(u_Texture0_sampler, input.v_TexCoord), input.fogViewSpace);
    o_Color.a = u_Color.a;
    return o_Color;
}
