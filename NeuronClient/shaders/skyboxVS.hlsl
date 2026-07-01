// Cube skybox vertex shader: emit a full-screen triangle straight from the vertex id (no
// vertex buffer / input layout) and, for each corner, the view-space ray through it. The
// oversized triangle covers the screen; the rasterizer interpolates the ray per pixel.

#include "partials/skybox.hlsli"

VSOut VSMain(uint _vid : SV_VertexID)
{
    VSOut o;
    const float2 uv = float2(float((_vid << 1) & 2u), float(_vid & 2u)); // (0,0) (2,0) (0,2)
    const float2 clip = uv * 2.0 - 1.0;                                  // covers clip [-1,1]
    o.pos = float4(clip, 1.0, 1.0);                                      // z = 1 (far plane)

    // View ray: clip.x/y scaled by the frustum half-extents at z = 1. clip.y = +1 is the top
    // of the screen (NDC y up), which matches view-space +y up, so no flip is needed.
    const float tanY = u_Params.x;
    const float tanX = u_Params.x * u_Params.y; // tanY * aspect
    o.viewDir = float3(clip.x * tanX, clip.y * tanY, 1.0);
    return o;
}
