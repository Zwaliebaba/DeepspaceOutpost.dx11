#include "partials/common.hlsli"
#include "partials/colorspace.hlsli"
#include "partials/debug.hlsli"

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

struct PSInput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target
{
    float4 outColor = u_Texture0.Sample(u_Texture0_sampler, input.uv);
#if USE_HDR
    outColor.rgb = srgb_to_rgb(outColor.rgb);
#endif
    fdebugcolor(outColor, true);
    return outColor;
}
