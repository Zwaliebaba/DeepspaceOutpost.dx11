#include "partials/common.hlsli"

// SMAA neighborhood blending -- pixel stage.
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1

#include "partials/smaa-header.hlsli"
#include "partials/smaa.hlsli"
#include "partials/tonemap.hlsli"

Texture2D colorTex : register(t0);
Texture2D blendTex : register(t1);

struct PSInput
{
    float4 pos      : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 offset   : TEXCOORD1;
};

float4 PSMain(PSInput input) : SV_Target
{
    return tonemap(SMAANeighborhoodBlendingPS(input.texcoord, input.offset,
                                              colorTex, blendTex));
}
