#include "partials/common.hlsli"
#include "partials/clipping.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos       : POSITION;   // location 0
    float2 aTexCoords : TEXCOORD0;  // location 1
    float4 aColor     : COLOR0;     // location 2
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord   : TEXCOORD0;
    float4 v_Color      : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if FEATURE_NATIVE_CLIPPING
    float2 clipDist     : SV_ClipDistance0;
#else
    float2 clipDist     : TEXCOORD3;
#endif
#endif
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.v_TexCoord = input.aTexCoords;
    o.v_Color = input.aColor;

    float4 mv_pos = mul(input.aPos, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
    o.clipDist = vclipping(mv_pos);
#endif
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
