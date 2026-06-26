#include "partials/common.hlsli"

struct VSInput
{
    float2 aInstanceMult : POSITION;   // location 0
    float2 aPosCenter    : TEXCOORD0;  // location 1
    float2 aSizeOffset   : TEXCOORD1;  // location 2
    float  aHeight       : TEXCOORD2;  // location 3
    float  aTime         : TEXCOORD3;  // location 4
};

struct VSOutput
{
    float4 pos        : SV_Position;
    float2 v_TexCoord : TEXCOORD0;
    float2 v_Center   : TEXCOORD1;
    float  v_InvSize  : TEXCOORD2;
    float  v_Height   : TEXCOORD3;
    float  v_Time     : TEXCOORD4;
};

static const float c_Redux = 0.8;

VSOutput VSMain(VSInput input)
{
    VSOutput o;
    float2 pos = (input.aPosCenter + (input.aSizeOffset * c_Redux) * input.aInstanceMult) * 2.0 - float2(1.0, 1.0);
    o.pos = float4(pos, 0.0, 1.0);
    o.v_TexCoord = float2(
        0.5 * (1.0 + pos.x),
        0.5 * (1.0 - pos.y)
    );

    o.v_Center = float2(input.aPosCenter.x, 1.0 - input.aPosCenter.y);
    o.v_InvSize = 1.0 / input.aSizeOffset.x;
    o.v_Height = input.aHeight;
    o.v_Time = input.aTime;
    return o;
}
