#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/hdr.hlsli"

struct PSInput
{
    float4 pos     : SV_Position;
    float4 v_Color : TEXCOORD0;
};

float4 PSMain(PSInput input) : SV_Target
{
    return hdrcolor(input.v_Color);
}
