#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos : POSITION;   // location 0
};

struct VSOutput
{
    float4 pos : SV_Position;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.pos = mul(input.aPos, u_ViewProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
