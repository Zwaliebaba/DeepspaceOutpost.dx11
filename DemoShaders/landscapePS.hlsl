#include "partials/common.hlsli"
#include "partials/matrices.hlsli"
#include "partials/lighting-landscape.hlsli"
#include "partials/fog.hlsli"
#include "partials/landscape-common.hlsli"
#include "partials/barycentric.hlsli"

Texture2D    u_Texture0         : register(t0); // landscape color
SamplerState u_Texture0_sampler : register(s0);
Texture2D    u_Texture1         : register(t1); // landscape overlay pattern
SamplerState u_Texture1_sampler : register(s1);

static const float c_ColorLODBias = 0.5;

// NORMALS_PROVIDED indicates this is a non-indexed draw, and thus that the
// per-vertex emulated barycentric coordinates are valid. HAS_INDEXED_BARYCENTRIC
// indicates that an extension is available to give us barycentric coordinates
// even for indexed draws.
#if defined(NORMALS_PROVIDED) || defined(HAS_INDEXED_BARYCENTRIC)
//#define USE_BARYCENTRIC 1
#endif

struct PSInput
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

#ifdef USE_BARYCENTRIC
// Fog out the wireframe at distance so we don't get a lot of moire-like patterns
float OverlayAlpha(float4 fogViewSpace)
{
    const float fogStart = 1.0;
    const float fogEnd = 2000.0;
    float dist = length(fogViewSpace);
    float alpha = (fogEnd - dist) / (fogEnd - fogStart);
    return clamp(alpha, 0.0, 1.0);
}
#endif

float4 OverlayColor(PSInput input)
{
#if defined(USE_BARYCENTRIC)
    float SqrtDepth = sqrt(input.pos.w);
    float3 dBaryCoordX = ddx(input.baryCoord);
    float3 dBaryCoordY = ddy(input.baryCoord);
    float3 dBaryCoord = sqrt(dBaryCoordX * dBaryCoordX + dBaryCoordY * dBaryCoordY);
    float Falloff = SqrtDepth * 35.0;
    float3 dFalloff = dBaryCoord * Falloff;
    float3 Remap = smoothstep((float3)0.0, dFalloff, input.baryCoord);
    float ClosestEdge = min(Remap.x, min(Remap.y, Remap.z));
    float alpha = OverlayAlpha(input.fogViewSpace);
    return float4((float3)(1.0 - ClosestEdge), alpha);
#else
    return u_Texture1.Sample(u_Texture1_sampler, input.v_TexCoord);
#endif
}

float4 applyOverlay(float4 scolor, float4 dcolor)
{
    // sfactor = GL_SRC_ALPHA, dfactor = GL_ONE
    float4 sfactor = (float4)scolor.a;
    float4 dfactor = (float4)1.0;
    return ((scolor * sfactor) + (dcolor * dfactor));
}

float4 PSMain(PSInput input) : SV_Target
{
    float4 overlayColor = OverlayColor(input);

#if !defined(NORMALS_PROVIDED)
    float2 colorTexCoord = flandscape_colortexcoord(input.v_EyeRelativePos, input.v_ColorTexCoord, input.v_ColorTexCoordFlat);
#else
    float2 colorTexCoord = flandscape_colortexcoord(input.v_ColorTexCoord);
#endif
    float4 baseColor = u_Texture0.SampleBias(u_Texture0_sampler, colorTexCoord, c_ColorLODBias);

    // Do normal calculation once, shared for all lighting calculations
#if MAX_LIGHTS > 0
#if defined(NORMALS_PROVIDED)
    float3 normal = flighting_normal_landscape(input.fragNormal);
#else
    float3 normal = flighting_normal_landscape(input.eyePos);
#endif
#else
    float3 normal = flighting_normal_landscape(float3(0.0, 1.0, 0.0));
#endif

    float4 finalBaseColor;
    float4 finalColor;

    // Apply lighting to base color only
#if MAX_LIGHTS > 0
    finalBaseColor = flighting(0, normal, baseColor, input.halfway);
    // Apply lighting with 2nd set of material values to overlay
    overlayColor *= flighting(1, normal, baseColor, input.halfway);
#else
    finalBaseColor = flighting(0, normal, baseColor);
    overlayColor *= flighting(1, normal, baseColor);
#endif

    // glBlendFunc(GL_SRC_ALPHA, GL_ONE) equivalent
    finalColor = applyOverlay(overlayColor, finalBaseColor);

    // End result should always have exactly an alpha of 1.0
    finalColor.a = 1.0;

    return ffog(finalColor, input.fogViewSpace);
}
