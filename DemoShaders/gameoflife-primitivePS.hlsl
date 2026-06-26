#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/hdr.hlsli"
#include "partials/debug.hlsli"

struct PSInput
{
    float4 pos     : SV_Position;
    float  v_Alpha : TEXCOORD0;
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = float4(0.4f, 1.0f, 0.4f, input.v_Alpha);
    color = hdrcolor(color);
    float4 o_Color = color;
    fdebugcolor(o_Color, isFrontFace);
    return o_Color;
}
