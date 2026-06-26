#include "partials/common.hlsli"
#include "partials/clipping.hlsli"
#include "partials/fog.hlsli"
#include "partials/matrices.hlsli"
#include "partials/debug.hlsli"

struct PSInput
{
    float4 pos          : SV_Position;
    float4 v_Color      : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if !FEATURE_NATIVE_CLIPPING
    float2 clipDist     : TEXCOORD2;
#endif
#endif
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1) && !FEATURE_NATIVE_CLIPPING
    fclipping(input.clipDist);
#endif

    float4 color = input.v_Color;
#if USE_HDR
    color *= 1.25;
#endif
    color = ffog(color, input.fogViewSpace);
    fdebugcolor(color, isFrontFace);
    return color;
}
