#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/lighting.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

struct VSInput
{
    float4 aPos   : POSITION;   // location 0
    float4 aColor : COLOR0;     // location 1
    uint   vertexID : SV_VertexID;
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float4 color        : TEXCOORD0;
    float2 uv           : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
#if MAX_LIGHTS > 0
#if defined(NORMALS_PROVIDED)
    float3 fragNormal   : TEXCOORD3;
#else
    float3 eyePos       : TEXCOORD3;
#endif
    float3 halfway[MAX_LIGHTS] : TEXCOORD4;
#endif
};

static const float2 c_TexCoord[3] =
{
    float2(0.0f, 0.0f),
    float2(0.0f, 1.0f),
    float2(1.0f, 0.0f)
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.color = input.aColor;
    o.uv    = c_TexCoord[input.vertexID % 3];

    float4 mv_pos = mul(input.aPos, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);

#if MAX_LIGHTS > 0
    LightingVaryings lv = vlight(mv_pos, float3(0.0, 0.0, 0.0),
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
