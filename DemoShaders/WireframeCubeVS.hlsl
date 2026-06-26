#include "partials/common.hlsli"
#include "partials/clipping.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float3 aPos       : POSITION;   // location 0 (base position)
    float3 aCenterPos : TEXCOORD0;  // location 1 (instance)
    float3 aScale     : TEXCOORD1;  // location 2 (instance)
    float4 aColor     : COLOR0;     // location 3 (instance)
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
    o.v_Color = input.aColor;

    float3 pos = input.aCenterPos + (input.aPos * input.aScale);

    float4 mv_pos = mul(float4(pos, 1.0), u_ViewMatrix);
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
    o.clipDist = vclipping(mv_pos);
#endif
    o.fogViewSpace = vfog(mv_pos);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
