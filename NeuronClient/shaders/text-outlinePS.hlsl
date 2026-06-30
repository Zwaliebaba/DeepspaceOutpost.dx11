// Built-in text-outline program (pairs with render2dVS). Gives the bitmap font a crisp
// outline in the shader: sample the glyph coverage (alpha) plus an 8-tap ring one
// outline-width out, and composite the per-vertex text colour over the outline colour.
// Straight-alpha blend: returns the coverage-normalised colour and total coverage.

#include "partials/render2d.hlsli"

// b1: outline colour + atlas texel size, set by Render2D::SetTextOutline.
cbuffer TextParams : register(b1)
{
    float4 u_OutlineColor;   // rgb + a
    float4 u_OutlineParams;  // x=texelW, y=texelH, z=widthTexels, w=unused
};

float4 PSMain(VSOut i) : SV_Target
{
    float2 d = u_OutlineParams.xy * max(u_OutlineParams.z, 0.0);
    float c = u_Tex.Sample(u_Smp, i.uv).a;                    // glyph coverage at centre
    float o = 0.0;                                            // max coverage in the ring
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2( d.x, 0.0)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2(-d.x, 0.0)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2(0.0,  d.y)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2(0.0, -d.y)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2( d.x,  d.y)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2(-d.x,  d.y)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2( d.x, -d.y)).a);
    o = max(o, u_Tex.Sample(u_Smp, i.uv + float2(-d.x, -d.y)).a);

    float textA = c * i.col.a;
    float outA  = saturate(o) * (1.0 - c) * u_OutlineColor.a;
    float a = textA + outA;
    if (a <= 0.00390625) discard;                            // < 1/256: nothing to draw
    float3 rgb = (i.col.rgb * textA + u_OutlineColor.rgb * outA) / a;
    return float4(rgb, a);
}
