// GUI window / panel geometry: per-vertex colour, sampled interface texture.
#include "partials/common.hlsli"
#include "partials/immediate-vertex.hlsli"
#include "partials/matrices.hlsli"

struct VSOutput
{
    float4 pos   : SV_Position;
    float2 uv    : TEXCOORD0;
    float4 color : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.uv = input.aTexCoord0;
    o.color = input.aColor;

    float4 viewPos = mul(float4(input.aPos, 1.0), u_View);
    o.pos = mul(viewPos, u_Projection);
    return o;
}
