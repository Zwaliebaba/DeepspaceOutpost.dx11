#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/clipping.hlsli"

struct PSInput
{
    float4 pos : SV_Position;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if !FEATURE_NATIVE_CLIPPING
    float2 clipDist : TEXCOORD0;
#endif
#endif
};

float4 PSMain(PSInput input) : SV_Target
{
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1) && !FEATURE_NATIVE_CLIPPING
    fclipping(input.clipDist);
#endif
    return float4(0.0f, 0.0f, 0.0f, 1.0f);
}
