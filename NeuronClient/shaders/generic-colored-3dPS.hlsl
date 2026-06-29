#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/lighting.hlsli"
#include "partials/fog.hlsli"
#include "partials/alphatest.hlsli"

struct PSInput
{
    float4 pos          : SV_Position;
    float4 color        : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
#if MAX_LIGHTS > 0
    float3 fragNormal   : TEXCOORD2;
    float3 halfway[MAX_LIGHTS] : TEXCOORD3;
#endif
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = input.color;
#if MAX_LIGHTS > 0
    color = flighting(color, input.fragNormal, input.halfway, isFrontFace);
#endif
    falphatest(color);
    color = ffog(color, input.fogViewSpace);
    return color;
}
