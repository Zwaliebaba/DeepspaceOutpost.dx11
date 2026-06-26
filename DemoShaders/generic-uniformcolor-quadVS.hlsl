#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos : POSITION;   // location 0
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float4 fogViewSpace : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;

    float4 mv_pos = mul(input.aPos, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
