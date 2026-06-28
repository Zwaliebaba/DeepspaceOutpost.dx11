#include "partials/common.hlsli"
#include "partials/perdraw.hlsli"
#include "partials/hdr.hlsli"

Texture2D u_Texture0 : register(t0);
SamplerState u_Texture0_sampler : register(s0);

struct PSInput
{
    float4 pos   : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 color : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_Target
{
    // Negative LOD bias keeps small text crisp (matches the sample TextOverlay).
    float4 texColor = u_Texture0.SampleBias(u_Texture0_sampler, input.uv, -0.75);
    float4 color = u_Color * (input.color * texColor);
    return hdrcolor(color);
}
