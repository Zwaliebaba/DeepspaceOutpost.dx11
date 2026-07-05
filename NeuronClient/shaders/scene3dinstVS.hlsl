// Scene3D INSTANCED ship vertex shader (H2). Rebuilds the per-instance world matrix from
// the four world rows in the instance stream, transforms the model-space vertex to clip
// through world -> u_ViewProj (row-vector), and applies the same opt-in flat directional
// lighting the per-model VS does. The colour is the instance tint when its alpha >= 0,
// otherwise the mesh's per-face colour - identical output to the per-model path.

#include "partials/scene3dinst.hlsli"

InstOut VSMain(InstIn i)
{
    InstOut o;

    // Per-instance world matrix (rows 0..3 = side / roof / nose / translation).
    float4x4 W = float4x4(i.w0, i.w1, i.w2, i.w3);
    float4 wpos = mul(float4(i.pos, 1.0), W);   // p_world = p_model * W
    o.pos = mul(wpos, u_ViewProj);              // clip = p_world * VP
    o.nrm = i.nrm;

    float3 base = (i.tint.a >= 0.0) ? i.tint.rgb : i.col.rgb;
    if (u_LightParams.x > 0.5)
    {
        // Model-view for this instance; the flat per-face normal into view space.
        float4x4 MV = mul(W, u_View);
        const float3 n = normalize(mul(i.nrm, (float3x3) MV));
        const float ndl = saturate(dot(n, normalize(u_LightDir.xyz)));
        base *= (u_Ambient.rgb + u_Diffuse.rgb * ndl);
    }

    o.col = float4(base, i.col.a);
    return o;
}
