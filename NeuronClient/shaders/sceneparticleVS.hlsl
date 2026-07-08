// SceneParticles vertex shader: transform a render-frame particle-quad corner to clip
// space through the shared camera view-projection (b0). The CPU has already placed the
// corner (a camera-facing billboard built by the effects subsystem), so this is a straight
// transform; the sprite uv and per-vertex colour ride through to the additive pixel shader.

#include "partials/sceneparticle.hlsli"

VSOut VSMain(VSIn i)
{
    VSOut o;
    o.pos = mul(u_MVP, float4(i.pos, 1.0));
    o.uv = i.uv;
    o.col = i.col;
    return o;
}
