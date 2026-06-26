#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/time.hlsli"
#include "partials/flipsurface.hlsli"

cbuffer PerDraw : register(b9)
{
    float4 u_Color;
    float  u_DPIScale;
};

struct VSInput
{
    float4 aPos : POSITION;   // location 0
};

struct VSOutput
{
    float4 pos     : SV_Position;
    float4 v_Color : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.v_Color = u_Color;

    o.pos = mul(input.aPos, u_ViewProjectionMatrix);
#ifdef ENABLE_POINT_SIZE
    // NOTE: gl_PointSize has no DX11 equivalent; engine must size points via a geometry shader or quad expansion (was: 3.0 * u_DPIScale)
#endif
    vflipsurface(o.pos);
    return o;
}
