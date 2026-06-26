#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/clipping.hlsli"
#include "partials/lighting.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

cbuffer PerDraw : register(b9)
{
    float u_PointSize;
    float u_DPIScale;
};

struct VSInput
{
    float4 aPos    : POSITION;   // location 0
    float4 aColor  : COLOR0;     // location 1
#ifdef HAS_TEXTURE0
    float2 aTexCoord0 : TEXCOORD0;  // location 2
#endif
#ifdef HAS_TEXTURE1
    float2 aTexCoord1 : TEXCOORD1;  // location 3
#endif
#ifdef NORMALS_PROVIDED
    float3 aNormal : NORMAL;         // location 4
#endif
};

struct VSOutput
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
#if FEATURE_NATIVE_CLIPPING
    float2 clipDist     : SV_ClipDistance0;
#else
    float2 clipDist     : TEXCOORD9;
#endif
#endif
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.color = input.aColor;
#ifdef HAS_TEXTURE0
    o.uv0 = input.aTexCoord0;
#endif
#ifdef HAS_TEXTURE1
    o.uv1 = input.aTexCoord1;
#endif

#ifdef NORMALS_PROVIDED
    float3 aNormal = input.aNormal;
#else
    float3 aNormal = float3(0.0, 1.0, 0.0);
#endif

    float4 mv_pos = mul(input.aPos, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);

#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
    o.clipDist = vclipping(mv_pos);
#endif

#if MAX_LIGHTS > 0
    LightingVaryings lv = vlight(mv_pos, aNormal,
                                float3x3(1, 0, 0, 0, 1, 0, 0, 0, 1));
#if defined(NORMALS_PROVIDED)
    o.fragNormal = lv.normalOrEye;
#else
    o.eyePos = lv.normalOrEye;
#endif
    o.halfway = lv.halfway;
#endif

    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
