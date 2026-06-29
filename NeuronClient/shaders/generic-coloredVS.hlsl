// Colored immediate-mode geometry (no texture). Replaces fixed-function
// glColor/glVertex draws. Per-vertex color, optional linear fog.
#include "partials/common.hlsli"
#include "partials/immediate-vertex.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"

struct VSOutput
{
    float4 pos          : SV_Position;
    float4 color        : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.color = input.aColor;

    float4 viewPos = mul(float4(input.aPos, 1.0), u_View);
    o.fogViewSpace = vfog(viewPos);
    o.pos = mul(viewPos, u_Projection);
    return o;
}
