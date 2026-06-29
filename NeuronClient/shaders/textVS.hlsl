// World-space text (3D labels). Colour comes from the per-draw cbuffer, not vertices.
#include "partials/common.hlsli"
#include "partials/immediate-vertex.hlsli"
#include "partials/matrices.hlsli"

struct VSOutput
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.uv = input.aTexCoord0;

    float4 viewPos = mul(float4(input.aPos, 1.0), u_View);
    o.pos = mul(viewPos, u_Projection);
    return o;
}
