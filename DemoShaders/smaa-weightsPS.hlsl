#include "partials/common.hlsli"

// SMAA blending weight calculation -- pixel stage.
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1

#include "partials/smaa-header.hlsli"
#include "partials/smaa.hlsli"

Texture2D edgesTex  : register(t0);
Texture2D areaTex   : register(t1);
Texture2D searchTex : register(t2);
// subsampleIndices: GLSL `uniform vec4 subsampleIndices` -> u_SubsampleIndices
// in PerPass (b10), declared in smaa.hlsli. Pass zero for SMAA 1x.

struct PSInput
{
    float4 pos      : SV_Position;
    float2 texcoord : TEXCOORD0;
    float2 pixcoord : TEXCOORD1;
    float4 offset0  : TEXCOORD2;
    float4 offset1  : TEXCOORD3;
    float4 offset2  : TEXCOORD4;
};

float4 PSMain(PSInput input) : SV_Target
{
    float4 offset[3];
    offset[0] = input.offset0;
    offset[1] = input.offset1;
    offset[2] = input.offset2;
    return SMAABlendingWeightCalculationPS(input.texcoord, input.pixcoord, offset,
                                           edgesTex, areaTex, searchTex,
                                           u_SubsampleIndices);
}
