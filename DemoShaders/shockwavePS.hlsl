#include "partials/common.hlsli"
#include "partials/fog.hlsli"
#include "partials/matrices.hlsli"
#include "partials/debug.hlsli"

struct PSInput
{
    float4 pos          : SV_Position;
    float4 v_Color      : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = input.v_Color;
    float4 o_Color = ffog(color, input.fogViewSpace);
    fdebugcolor(o_Color, isFrontFace);
    return o_Color;
}
