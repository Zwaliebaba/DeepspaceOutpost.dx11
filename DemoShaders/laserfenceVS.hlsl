#include "partials/common.hlsli"
#include "partials/clipping.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/time.hlsli"
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
    float2 v_TexCoord0  : TEXCOORD0;
    float2 v_TexCoord1  : TEXCOORD1;
    float2 v_TexCoordBurn : TEXCOORD2;
    float4 v_Color      : TEXCOORD3;
    float4 fogViewSpace : TEXCOORD4;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if FEATURE_NATIVE_CLIPPING
    float2 clipDist     : SV_ClipDistance0;
#else
    float2 clipDist     : TEXCOORD5;
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
    o.v_TexCoord0 = input.aTexCoord0 + float2(u_ShaderTime * 0.06, 0.0);
    o.v_TexCoord1 = input.aTexCoord1 + float2(u_ShaderTime * 0.06, 0.0);
    o.v_TexCoordBurn = input.aTexCoord1;
    o.v_Color = input.aColor;
    vflipsurface(o.pos);
    return o;
}
