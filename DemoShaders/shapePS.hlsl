#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/clipping.hlsli"
#include "partials/lighting.hlsli"
#include "partials/fog.hlsli"

cbuffer PerDraw : register(b9)
{
    float4 u_Color;
};

struct PSInput
{
    float4 pos          : SV_Position;
    float4 color        : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
#if MAX_LIGHTS > 0
#if defined(NORMALS_PROVIDED)
    float3 fragNormal   : TEXCOORD2;
#else
    float3 eyePos       : TEXCOORD2;
#endif
    float3 halfway[MAX_LIGHTS] : TEXCOORD3;
#endif
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if !FEATURE_NATIVE_CLIPPING
    float2 clipDist     : TEXCOORD8;
#endif
#endif
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1) && !FEATURE_NATIVE_CLIPPING
    fclipping(input.clipDist);
#endif

    float4 color = input.color * u_Color;

#if MAX_LIGHTS > 0
#if defined(NORMALS_PROVIDED)
    color = flighting(color, input.fragNormal, input.halfway, isFrontFace);
#else
    color = flighting(color, input.eyePos, input.halfway, isFrontFace);
#endif
#endif

    color = ffog(color, input.fogViewSpace);
    return color;
}
