#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/hdr.hlsli"
#include "partials/debug.hlsli"

struct PSInput
{
    float4 pos        : SV_Position;
    float  v_Alpha    : TEXCOORD0;
    float2 v_TexCoord : TEXCOORD1;
};

Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = u_Texture0.Sample(u_Texture0_sampler, input.v_TexCoord) * float4(0.4f, 1.0f, 0.4f, input.v_Alpha);
#if USE_HDR
    color.rgb *= 1.25;
#endif
    float4 o_Color = color;
    fdebugcolor(o_Color, isFrontFace);
    return o_Color;
}
