#include "partials/common.hlsli"
#include "partials/fog.hlsli"
#include "partials/alphatest.hlsli"
#include "partials/texenv.hlsli"

Texture2D u_Texture0 : register(t0);
SamplerState u_Texture0_sampler : register(s0);

struct PSInput
{
    float4 pos          : SV_Position;
    float4 color        : TEXCOORD0;
    float2 uv           : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
};

float4 PSMain(PSInput input) : SV_Target
{
    float4 texel = u_Texture0.Sample(u_Texture0_sampler, input.uv);
    float4 color = fcolormix(0, input.color, texel);
    falphatest(color);
    color = ffog(color, input.fogViewSpace);
    return color;
}
