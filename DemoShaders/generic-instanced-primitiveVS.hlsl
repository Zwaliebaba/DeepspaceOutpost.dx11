#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/clipping.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4   aPos         : POSITION;     // location 0 (base)
    float4   aColor       : COLOR0;       // location 1 (instance)
    float4x4 aModelMatrix : TEXCOORD0;    // locations 2..5 (instance, mat4)
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float4 color        : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
#if FEATURE_NATIVE_CLIPPING
    float2 clipDist     : SV_ClipDistance0;
#else
    float2 clipDist     : TEXCOORD2;
#endif
#endif
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.color = input.aColor;

    // GLSL: u_ViewMatrix * (aModelMatrix * aPos)  (column-vector).
    // aModelMatrix is a per-instance attribute (uploaded as-is, not transposed),
    // so the column-vector product is preserved with mul(M, v).
    float4 worldPos = mul(input.aModelMatrix, input.aPos);
    float4 mv_pos = mul(worldPos, u_ViewMatrix);
#if (ENABLE_CLIP_PLANE0 || ENABLE_CLIP_PLANE1)
    o.clipDist = vclipping(mv_pos);
#endif
    o.fogViewSpace = vfog(mv_pos);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
