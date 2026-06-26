#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/lighting.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos    : POSITION;   // location 0
    float3 aNormal : NORMAL;     // location 1
    float4 aColor  : COLOR0;     // location 2
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
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.color = input.aColor;

    float4 mv_pos = mul(input.aPos, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);

#if MAX_LIGHTS > 0
    LightingVaryings lv = vlight(mv_pos, input.aNormal,
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
