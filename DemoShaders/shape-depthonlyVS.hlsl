#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/clipping.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos         : POSITION;     // location 0
#ifdef NORMALS_PROVIDED
    float4x4 aModelMatrix : TEXCOORD0;  // location 3..6
#else
    float4x4 aModelMatrix : TEXCOORD0;  // location 2..5
#endif
};

struct VSOutput
{
    float4 pos : SV_Position;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if FEATURE_NATIVE_CLIPPING
    float2 clipDist : SV_ClipDistance0;
#else
    float2 clipDist : TEXCOORD0;
#endif
#endif
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;

    float4 mv_pos = mul(mul(input.aPos, input.aModelMatrix), u_ViewMatrix);
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
    o.clipDist = vclipping(mv_pos);
#endif
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
