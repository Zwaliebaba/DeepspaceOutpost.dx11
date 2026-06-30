// Scene3D vertex shader: transform a model-space vertex to clip space through the
// per-model model->view->projection matrix (b0). When lighting is enabled (b2), apply
// faceted directional diffuse + ambient to the per-face colour here - with flat per-face
// normals this is constant across the face, so it reads as flat-shaded lighting. When
// disabled, the colour passes through unchanged (the faithful flat look).

#include "partials/scene3d.hlsli"

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = mul(u_MVP, float4(i.pos, 1.0));
    o.nrm = i.nrm;

    float3 base = i.col.rgb;
    if (u_LightParams.x > 0.5)
    {
        const float3 n = normalize(mul((float3x3) u_MV, i.nrm));
        const float ndl = saturate(dot(n, normalize(u_LightDir.xyz)));
        base *= (u_Ambient.rgb + u_Diffuse.rgb * ndl);
    }

    o.col = float4(base, i.col.a);
    return o;
}
