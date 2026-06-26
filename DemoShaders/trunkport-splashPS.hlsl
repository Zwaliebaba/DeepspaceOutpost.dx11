#include "partials/common.hlsli"
#include "partials/fog.hlsli"
#include "partials/matrices.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

cbuffer PerDraw : register(b9)
{
    float4 u_Color;
};

struct PSInput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord   : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
};

static const float c_TrunkPortSplashLODBias = 1.25;

float4 PSMain(PSInput input) : SV_Target
{
    float4 color = u_Color;
    color *= u_Texture0.SampleBias(u_Texture0_sampler, input.v_TexCoord, c_TrunkPortSplashLODBias);
    return ffog(color, input.fogViewSpace);
}
