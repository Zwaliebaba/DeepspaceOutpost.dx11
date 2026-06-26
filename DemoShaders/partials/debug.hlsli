#ifndef DEBUG_HLSLI
#define DEBUG_HLSLI

#include "common.hlsli"

// Fragment-stage debug overlays.
//
// GLSL used gl_FrontFacing; in HLSL the facing bit arrives as a PS input with
// the SV_IsFrontFace semantic, so callers pass it in. Pass `true` from shaders
// that have no facing input (DEBUG_CULL_FACE then never triggers, matching the
// GLSL behavior for non-culled draws).
void fdebugcolor(inout float4 color, bool isFrontFace)
{
#if DEBUG_CULL_FACE
    if (!isFrontFace)
        color = float4(1.0, 0.0, 1.0, 1.0);
#endif
}

#endif // DEBUG_HLSLI
