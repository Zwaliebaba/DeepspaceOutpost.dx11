// Cube skybox pixel shader: rotate the interpolated view ray into world space by the camera
// orientation and sample the environment cubemap. The gradient/procedural star field is gone
// - the look now lives entirely in the cubemap art.

#include "partials/skybox.hlsli"

float4 PSMain(VSOut _i) : SV_Target
{
    const float3 v = normalize(_i.viewDir);

    // camera->world = R * v, with R's rows in u_Rot0..2.
    const float3 dir = float3(dot(u_Rot0.xyz, v), dot(u_Rot1.xyz, v), dot(u_Rot2.xyz, v));

    return float4(g_Sky.Sample(g_SkySampler, dir).rgb, 1.0);
}
