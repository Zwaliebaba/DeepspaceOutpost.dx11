// Scene3D INSTANCED ship pixel shader (H2). The instanced VS already folded the tint
// override and any lighting into o.col, so this is a straight opaque emit - matching the
// per-model scene3dPS's faithful flat look, minus the b0 u_Color override (which the
// instanced path carries per instance instead).

#include "partials/scene3dinst.hlsli"

float4 PSMain(InstOut i) : SV_Target
{
    return float4(i.col.rgb, 1.0);
}
