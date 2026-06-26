#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/hdr.hlsli"
#include "partials/debug.hlsli"

Texture2DArray u_Texture0         : register(t0);
SamplerState   u_Texture0_sampler : register(s0);

struct PSInput
{
    float4 pos          : SV_Position;
    float3 uv           : TEXCOORD0;
    float4 color        : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = input.color;
    color = color * u_Texture0.Sample(u_Texture0_sampler, input.uv);
    color = hdrcolor(color);
    color = ffog(color, input.fogViewSpace);
    fdebugcolor(color, isFrontFace);
    return color;
}
