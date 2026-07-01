// Cube skybox pixel shader: rotate the interpolated view ray into world space by the camera
// orientation and sample the environment cubemap. The gradient/procedural star field is gone
// - the look now lives entirely in the cubemap art.

#include "partials/skybox.hlsli"

float4 PSMain(VSOut _i) : SV_Target
{
    const float3 v = normalize(_i.viewDir);

    // camera->world = R * v, with R's rows in u_Rot0..2.
    const float3 dir = float3(dot(u_Rot0.xyz, v), dot(u_Rot1.xyz, v), dot(u_Rot2.xyz, v));

    // The source cubemap is very dark (average ~3/255); lift it by a tunable exposure so the
    // whole sky is visible, not just the brightest cluster. Bright stars simply clip to white.
    const float3 col = g_Sky.Sample(g_SkySampler, dir).rgb * u_Params.z;
    return float4(saturate(col), 1.0);
}
