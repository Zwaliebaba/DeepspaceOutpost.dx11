#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/clipping.hlsli"
#include "partials/lighting.hlsli"
#include "partials/fog.hlsli"
#include "partials/alphatest.hlsli"
#include "partials/texenv.hlsli"
#include "partials/hdr.hlsli"
#include "partials/debug.hlsli"

#ifdef HAS_TEXTURE0
Texture2D    u_Texture0         : register(t0);
SamplerState u_Texture0_sampler : register(s0);
#endif
#ifdef HAS_TEXTURE1
Texture2D    u_Texture1         : register(t1);
SamplerState u_Texture1_sampler : register(s1);
#endif

struct PSInput
{
    float4 pos          : SV_Position;
    float4 color        : TEXCOORD0;
#ifdef HAS_TEXTURE0
    float2 uv0          : TEXCOORD1;
#endif
#ifdef HAS_TEXTURE1
    float2 uv1          : TEXCOORD2;
#endif
    float4 fogViewSpace : TEXCOORD3;
#if MAX_LIGHTS > 0
#if defined(NORMALS_PROVIDED)
    float3 fragNormal   : TEXCOORD4;
#else
    float3 eyePos       : TEXCOORD4;
#endif
    float3 halfway[MAX_LIGHTS] : TEXCOORD5;
#endif
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if !FEATURE_NATIVE_CLIPPING
    float2 clipDist     : TEXCOORD9;
#endif
#endif
};

float4 PSMain(PSInput input, bool isFrontFace : SV_IsFrontFace) : SV_Target
{
    float4 color = input.color;

#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1) && !FEATURE_NATIVE_CLIPPING
    fclipping(input.clipDist);
#endif

#ifdef HAS_TEXTURE0
    color = fcolormix(0, color, u_Texture0.Sample(u_Texture0_sampler, input.uv0));
#endif
#ifdef HAS_TEXTURE1
    color = fcolormix(1, color, u_Texture1.Sample(u_Texture1_sampler, input.uv1));
#endif

    color = hdrcolor(color);

#if MAX_LIGHTS > 0
#if defined(NORMALS_PROVIDED)
    color = flighting(color, input.fragNormal, input.halfway, isFrontFace);
#else
    color = flighting(color, input.eyePos, input.halfway, isFrontFace);
#endif
#endif

    color = ffog(color, input.fogViewSpace);
    color = falphatest(color);

    fdebugcolor(color, isFrontFace);
    return color;
}
