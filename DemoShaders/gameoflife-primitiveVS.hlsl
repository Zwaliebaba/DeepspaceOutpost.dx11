#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos   : POSITION;   // location 0
    float  aAlpha : TEXCOORD0;  // location 1
};

struct VSOutput
{
    float4 pos     : SV_Position;
    float  v_Alpha : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.v_Alpha = input.aAlpha;
    o.pos = mul(input.aPos, u_ViewProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
