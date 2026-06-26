#include "partials/common.hlsli"
#include "partials/clipping.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/time.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos   : POSITION;   // location 0
    float4 aColor : COLOR0;     // location 3
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float4 v_Color      : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if FEATURE_NATIVE_CLIPPING
    float2 clipDist     : SV_ClipDistance0;
#else
    float2 clipDist     : TEXCOORD2;
#endif
#endif
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;

    float4 mv_pos = mul(input.aPos, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
    o.clipDist = vclipping(mv_pos);
#endif

    o.pos = mul(mv_pos, u_ProjectionMatrix);
    o.v_Color = input.aColor;
    vflipsurface(o.pos);
    return o;
}
