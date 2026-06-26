#include "partials/common.hlsli"

// SMAA separate multisamples -- pixel stage (splits an MSAA 2x buffer into two
// single-sample targets). Requires the multisample SMAA helpers.
#define SMAA_INCLUDE_VS 0
#define SMAA_INCLUDE_PS 1
#define HAS_MULTISAMPLE 1

#include "partials/smaa-header.hlsli"
#include "partials/smaa.hlsli"
#include "partials/tonemap.hlsli"

// GLSL: uniform SMAATexture2DMS2(u_Texture0)  ->  Texture2DMS<float4, 2>
Texture2DMS<float4, 2> u_Texture0 : register(t0);

struct PSInput
{
    float4 pos      : SV_Position;
    float2 texcoord : TEXCOORD0;
};

struct PSOutput
{
    float4 color0 : SV_Target0;
    float4 color1 : SV_Target1;
};

PSOutput PSMain(PSInput input)
{
    PSOutput o;
    // input.pos is SV_Position, the HLSL equivalent of gl_FragCoord.
    SMAASeparatePS(input.pos, input.texcoord, o.color0, o.color1, u_Texture0);
    o.color0 = tonemap(o.color0);
    o.color1 = tonemap(o.color1);
    return o;
}
