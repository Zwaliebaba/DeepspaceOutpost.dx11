#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/clipping.hlsli"
#include "partials/lighting.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos    : POSITION;   // location 0
    float4 aColor  : COLOR0;     // location 1
#ifdef NORMALS_PROVIDED
    float3 aNormal      : NORMAL;        // location 2
    row_major float4x4 aModelMatrix : TEXCOORD0;  // location 3 (4 slots)
#else
    row_major float4x4 aModelMatrix : TEXCOORD0;  // location 2 (4 slots)
#endif
};

struct VSOutput
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
#if FEATURE_NATIVE_CLIPPING
    float2 clipDist     : SV_ClipDistance0;
#else
    float2 clipDist     : TEXCOORD8;
#endif
#endif
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.color = input.aColor;

#ifndef NORMALS_PROVIDED
    float3 aNormal = float3(0.0, 1.0, 0.0);
#else
    float3 aNormal = input.aNormal;
#endif

    float4 mv_pos = mul(mul(input.aPos, input.aModelMatrix), u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);

#if MAX_LIGHTS > 0
    LightingVaryings lv = vlight(mv_pos, aNormal, (float3x3)input.aModelMatrix);
#if defined(NORMALS_PROVIDED)
    o.fragNormal = lv.normalOrEye;
#else
    o.eyePos = lv.normalOrEye;
#endif
    o.halfway = lv.halfway;
#endif

#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
    o.clipDist = vclipping(mv_pos);
#endif

    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
