#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/time.hlsli"
#include "partials/flipsurface.hlsli"

cbuffer PerDraw : register(b9)
{
    float u_ScreenWidth;
    float u_DPIScale;
};

struct VSInput
{
    float4 aPos   : POSITION;   // location 0
    float  aSpeed : TEXCOORD0;  // location 1
    float  aColor : TEXCOORD1;  // location 2
};

struct VSOutput
{
    float4 pos     : SV_Position;
    float4 v_Color : TEXCOORD0;
};

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    o.v_Color = float4(input.aColor, input.aColor, input.aColor, 1.0);

    float4 pos = input.aPos;
    pos.x = mod(pos.x + input.aSpeed * u_GameTime, u_ScreenWidth);

    o.pos = mul(pos, u_ViewProjectionMatrix);
#ifdef ENABLE_POINT_SIZE
    // NOTE: gl_PointSize has no DX11 equivalent; engine must size points via a geometry shader or quad expansion (was: 2.0 * u_DPIScale)
#endif
    vflipsurface(o.pos);
    return o;
}
