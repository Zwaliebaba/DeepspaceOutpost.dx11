#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/time.hlsli"
#include "partials/flipsurface.hlsli"

cbuffer PerDraw : register(b9)
{
    row_major float4x4 u_Instance;
    float u_TimeOpen;
    float u_MeshScale;
    float u_MeshScaleInv;
};

static const float PI = 3.1415926535;

struct VSInput
{
    float2 aPos : POSITION;   // location 0
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord   : TEXCOORD0;
    float4 fogViewSpace : TEXCOORD1;
};

float3 generateSplash(float3 _pos)
{
    float gameTime = u_ShaderTime;
    float timeScale = clamp(5.0f - u_TimeOpen, 1.0f, 10.0f);
    float centreDif = length(_pos);
    float fractionIn = 1.0f - clamp(centreDif / 40.0f, 0.0f, 1.0f);

    float wave1 = cos(centreDif * 0.15f);
    float wave2 = cos(centreDif * 0.05f);

    float yScale = fractionIn * 15.0f * timeScale;
    float zScale = wave1 * fractionIn * 10.0f * timeScale;

    float3 thisDif = (float3)0.0f;

    thisDif.y += sin(gameTime * PI * 0.47f) * wave1 * yScale;
    thisDif.y += sin(gameTime * PI * 0.59f) * wave2 * yScale;
    thisDif.z += cos(gameTime * PI * 0.75f) * zScale;

    return _pos + thisDif;
}

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.v_TexCoord = float2(input.aPos.x + u_MeshScale * 0.5f, input.aPos.y + u_MeshScale * 0.5f) * u_MeshScaleInv;

    float4 pos = mul(float4(generateSplash(float3(input.aPos.x, 0.0f, input.aPos.y)), 1.0), u_Instance);
    float4 mv_pos = mul(pos, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
