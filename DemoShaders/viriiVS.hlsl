#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/fog.hlsli"
#include "partials/flipsurface.hlsli"

// Instance
struct VSInput
{
    float3 aCenter          : TEXCOORD0; // location 0: midpoint of the line segment
    float3 aRight           : TEXCOORD1; // location 1: normalized right side vector
    float3 aFront           : TEXCOORD2; // location 2: vector from aCenter to the front
    float  aDistanceHead    : TEXCOORD3; // location 3: total distance from head
    float  aPredictedHealth : TEXCOORD4; // location 4: entity health, normalized 0..1
    float  aDead            : TEXCOORD5; // location 5: 0 if alive, 1 if dead
};

struct VSOutput
{
    float4 pos          : SV_Position;
    float2 v_TexCoord   : TEXCOORD0;
    float4 v_Color      : TEXCOORD1;
    float4 fogViewSpace : TEXCOORD2;
};

cbuffer PerDraw : register(b9)
{
    float4 u_WormColor;
    float4 u_GlowColor;
};

static const float WORM_TEX_WIDTH = 32.0f / (32.0f + 128.0f);
static const float GLOW_TEX_HEIGHT = 128.0f / 512.0f;

float3 position(VSInput input, bool isGlow, bool isFront, bool isRight)
{
    float rightFactor = isGlow ? 12.0 : 3.0;
    float3 right = normalize(input.aRight) * rightFactor;
    float3 glowDiff = float3(0.0, 0.0, 0.0);

    // Note: sqrt and div are expensive operations; prefer them to be under `if`.
    if (isGlow)
        glowDiff = input.aFront * 10.0f * inversesqrt(dot(input.aFront, input.aFront));

    // Note: multiply-add is one instruction
    float3 result = input.aCenter + right * (isRight ? 1.0 : -1.0);
    result += (input.aFront + glowDiff) * (isFront ? 1.0 : -1.0);

    return result;
}

float2 texCoordWorm(VSInput input, bool isFront, bool isRight)
{
    float yDeltaBase = input.aDistanceHead * 6.0f;
    float yDeltaNext = (input.aDistanceHead + length(input.aFront) * 2.0f) * 6.0f;
    float yOff = yDeltaBase / 512.0f;
    float yOffNext = yDeltaNext / 512.0f;

    return float2(isRight ? WORM_TEX_WIDTH : 0.0f, isFront ? yOffNext : yOff);
}

float2 texCoordGlow(bool isFront, bool isRight)
{
    return float2(isRight ? 1.0 : WORM_TEX_WIDTH, isFront ? GLOW_TEX_HEIGHT : 0.0);
}

float alphaDelta(VSInput input, bool isGlow, bool isFront, bool isRight)
{
    float thisDistance = length(input.aFront) * 2.0f;
    float nextDistance = input.aDistanceHead + thisDistance;
    float delta = isFront ? nextDistance : input.aDistanceHead;

    delta *= 2.0f;

    // Note: folded ! to reduce the number of instructions
    if (isGlow && input.aDistanceHead < 6.0f && thisDistance < 6.0f)
        delta = 255.0f;

    return delta;
}

struct RenderResult
{
    float4 v_Color;
    float2 v_TexCoord;
};

RenderResult render(VSInput input, bool isGlow, bool isFront, bool isRight)
{
    RenderResult r;

    float baseAlpha = isGlow ? 0.62 : 0.78;
    float3 baseColor = isGlow ? u_GlowColor.rgb : u_WormColor.rgb * input.aPredictedHealth;
    float deadAlphaMultiplier = isGlow ? 1.25 : 0.5;

    float3 color = input.aDead > 0.5f ? float3(0.78, 0.78, 0.78) : baseColor;
    float alpha;

    if (input.aDead > 0.5f)
        alpha = input.aPredictedHealth * deadAlphaMultiplier;
    else
    {
        float delta = alphaDelta(input, isGlow, isFront, isRight);
        alpha = clamp(baseAlpha - (delta / 255.0f), 0.0f, 1.0f);
    }

    r.v_Color = float4(color, alpha);

    if (isGlow)
        r.v_TexCoord = texCoordGlow(isFront, isRight);
    else
        r.v_TexCoord = texCoordWorm(input, isFront, isRight);

    return r;
}

VSOutput VSMain(VSInput input, uint vertexID : SV_VertexID)
{
    VSOutput o;
    float4 worldPos;

    // Avoid redundant calculations, cache them upfront.
    int quadVertexId = vertexID & 0x3;
    bool isFront = quadVertexId < 2;
    bool isRight = quadVertexId == 1 || quadVertexId == 2;
    bool isGlow = vertexID > 3;

    RenderResult r = render(input, isGlow, isFront, isRight);
    o.v_Color = r.v_Color;
    o.v_TexCoord = r.v_TexCoord;
    worldPos = float4(position(input, isGlow, isFront, isRight), 1.0f);

    float4 mv_pos = mul(worldPos, u_ViewMatrix);
    o.fogViewSpace = vfog(mv_pos);
    o.pos = mul(mv_pos, u_ProjectionMatrix);
    vflipsurface(o.pos);
    return o;
}
