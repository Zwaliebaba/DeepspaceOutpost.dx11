#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/clipping.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos       : POSITION;   // location 0
    float2 aTexCoord0 : TEXCOORD0;  // location 1
    float2 aTexCoord1 : TEXCOORD1;  // location 2
    float4 aColor     : COLOR0;     // location 3
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float2 uv0          : TEXCOORD0;
    float2 uv1          : TEXCOORD1;
    float4 color        : TEXCOORD2;
    float4 fogViewSpace : TEXCOORD3;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if FEATURE_NATIVE_CLIPPING
    float2 clipDist     : SV_ClipDistance0;
#else
    float2 clipDist     : TEXCOORD4;
#endif
#endif
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.uv0   = input.aTexCoord0;
    o.uv1   = input.aTexCoord1;
    o.color = input.aColor;

    float4 mv_pos = mul(input.aPos, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
    o.clipDist = vclipping(mv_pos);
#endif
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
