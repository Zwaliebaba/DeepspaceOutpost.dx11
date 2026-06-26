#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/camera.hlsli"
#include "partials/lighting-landscape.hlsli"
#include "partials/fog.hlsli"
#include "partials/landscape-common.hlsli"
#include "partials/barycentric.hlsli"
#include "partials/flipsurface.hlsli"

cbuffer PerDraw : register(b9)
{
    float2 u_CellFactor;
};

struct VSInput
{
#if defined(NORMALS_PROVIDED)
    float4 aPos           : POSITION;   // location 0
    float3 aNormal        : NORMAL;     // location 1
    float2 aColorTexCoord : TEXCOORD0;  // location 2
#else
    float4 aPos           : POSITION;   // location 0
    float  aColorTexCoord : TEXCOORD0;  // location 1
#endif
    uint   vertexID       : SV_VertexID;
};

struct VSOutput
{
    float4 pos             : SV_Position;
    float2 v_TexCoord      : TEXCOORD0;
    float2 v_ColorTexCoord : TEXCOORD1;
    float4 fogViewSpace    : TEXCOORD2;
    float3 baryCoord       : TEXCOORD3;
#if !defined(NORMALS_PROVIDED)
    nointerpolation float2 v_ColorTexCoordFlat : TEXCOORD4;
    float3 v_EyeRelativePos                    : TEXCOORD5;
#endif
#if MAX_LIGHTS > 0
#if defined(NORMALS_PROVIDED)
    float3 fragNormal      : TEXCOORD6;
#else
    float3 eyePos          : TEXCOORD6;
#endif
    float3 halfway[MAX_LIGHTS] : TEXCOORD7;
#endif
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;

#if defined(NORMALS_PROVIDED)
    float3 aNormal = input.aNormal;
#else
    const float3 aNormal = float3(0.0, 1.0, 0.0);
#endif

    // landscape.glsl main(): overlay tex coord
    o.v_TexCoord = input.aPos.xz * u_CellFactor;

    // barycentric (vbarycentric())
    o.baryCoord = vbarycentric(input.vertexID);

    // vlandscape() body (inlined; writes shader-owned varyings)
    o.v_ColorTexCoord = (float2)input.aColorTexCoord;
#if !defined(NORMALS_PROVIDED)
    o.v_ColorTexCoordFlat = (float2)input.aColorTexCoord;
    o.v_EyeRelativePos    = input.aPos.xyz - u_CameraPos;
#endif

    float4 mv_pos = mul(input.aPos, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);

    LightingVaryings lv = vlight(mv_pos, aNormal, (float3x3)1.0);
#if MAX_LIGHTS > 0
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
