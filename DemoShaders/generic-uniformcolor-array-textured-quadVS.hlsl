#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos       : POSITION;   // location 0
    float3 aTexCoords : TEXCOORD0;  // location 1
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float3 uv           : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.uv = input.aTexCoords;

    float4 mv_pos = mul(input.aPos, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
