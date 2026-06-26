#include "partials/common.hlsli"

// SMAA edge detection (luma) -- pixel stage.
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1

#include "partials/smaa-header.hlsli"
#include "partials/smaa.hlsli"

Texture2D colorTex : register(t0);

struct PSInput
{
    float4 pos      : SV_Position;
    float2 texcoord : TEXCOORD0;
    float4 offset0  : TEXCOORD1;
    float4 offset1  : TEXCOORD2;
    float4 offset2  : TEXCOORD3;
};

float4 PSMain(PSInput input) : SV_Target
{
    float4 offset[3];
    offset[0] = input.offset0;
    offset[1] = input.offset1;
    offset[2] = input.offset2;
    float2 edges = SMAALumaEdgeDetectionPS(input.texcoord, offset, colorTex);
    return float4(edges, 0.0, 0.0);
}
