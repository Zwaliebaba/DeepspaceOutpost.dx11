#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/clipping.hlsli"

struct VSInput
{
    float4 aPos           : POSITION;   // location 0
    float2 aTexCoord      : TEXCOORD0;  // location 1
    float2 aGradientCoord : TEXCOORD1;  // location 2
};

struct VSOutput
{
    float4 pos             : SV_Position;
    float2 v_TexCoord      : TEXCOORD0;
    float2 v_GradientCoord : TEXCOORD1;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if FEATURE_NATIVE_CLIPPING
    float2 clipDist        : SV_ClipDistance0;
#else
    float2 clipDist        : TEXCOORD2;
#endif
#endif
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.v_TexCoord = input.aTexCoord;
    o.v_GradientCoord = input.aGradientCoord;

    float4 mv_pos = mul(input.aPos, u_ViewMatrix);
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
    o.clipDist = vclipping(mv_pos);
#endif
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    return o;
}
