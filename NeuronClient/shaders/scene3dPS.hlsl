// Scene3D pixel shader: flat, opaque per-face colour - faithful to the legacy solid
// ship look (no lighting). The opt-in lit variant (Phase 5) replaces this PS with one
// that uses the interpolated normal + a directional light; the geometry already
// carries normals for that.

#include "partials/scene3d.hlsli"

float4 PSMain(VSOut i) : SV_Target
{
    float3 rgb = (u_Color.a >= 0.0) ? u_Color.rgb : i.col.rgb;
    return float4(rgb, 1.0);
}
