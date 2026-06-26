#include "partials/common.hlsli"

#define DEFORM_PRECISION 8.0f

Texture2D    u_Texture0         : register(t0);   // Screen colors
SamplerState u_Texture0_sampler : register(s0);
Texture2D    u_Texture1         : register(t1);   // Deform map
SamplerState u_Texture1_sampler : register(s1);

static const float kChromaShiftMag = 0.05;
static const float2 kChromaShift = float2(2.5, -0.5);

struct PSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float2 TextureOffset(float2 uv)
{
    return u_Texture1.Sample(u_Texture1_sampler, uv).rg / DEFORM_PRECISION;
}

float4 PSMain(PSInput input) : SV_Target
{
    float2 texOffset = TextureOffset(input.uv);
    float chromaMag = kChromaShiftMag * length(texOffset);
    float2 texCoords = input.uv + texOffset;

    float4 outColor;
    outColor.r = u_Texture0.Sample(u_Texture0_sampler, texCoords + kChromaShift * chromaMag).r;
    outColor.g = u_Texture0.Sample(u_Texture0_sampler, texCoords).g;
    outColor.b = u_Texture0.Sample(u_Texture0_sampler, texCoords - kChromaShift * chromaMag).b;
    outColor.a = 1.0;

    return outColor;
}
