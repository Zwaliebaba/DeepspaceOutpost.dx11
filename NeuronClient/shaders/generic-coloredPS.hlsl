#include "partials/common.hlsli"
#include "partials/fog.hlsli"
#include "partials/alphatest.hlsli"

struct PSInput
{
    float4 pos          : SV_Position;
    float4 color        : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_Target
{
    float4 color = input.color;
    falphatest(color);
    color = ffog(color, input.fogViewSpace);
    return color;
}
