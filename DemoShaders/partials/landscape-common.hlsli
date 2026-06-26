#ifndef LANDSCAPE_COMMON_HLSLI
#define LANDSCAPE_COMMON_HLSLI

#include "common.hlsli"

//
// Landscape common helpers (port of partials/landscape-common.glsl).
//
// The GLSL source read/wrote global varyings inside vlandscape() /
// flandscape_colortexcoord(). In HLSL the shader OWNS the I/O struct fields and
// passes them into these pure helpers. The varyings a landscape shader carries:
//
//   float2 v_ColorTexCoord : TEXCOORD<n>;        // always
//   #if !defined(NORMALS_PROVIDED)
//   nointerpolation float2 v_ColorTexCoordFlat : TEXCOORD<n+1>;  // 'flat'
//   float3 v_EyeRelativePos : TEXCOORD<n+2>;     // aPos.xyz - u_CameraPos
//   #endif
//
// VS side: the vlandscape() body (mv_pos, vfog, vlight, gl_Position,
// vflipsurface) is kept INLINE in the shader's VSMain because it must write
// shader-owned varying fields and the clip-space position. This header only
// provides the pure fragment-stage helper.
//

#if !defined(NORMALS_PROVIDED)
float2 colorTexCoordForSlope(float steepness, float2 colorTexCoord, float2 colorTexCoordFlat)
{
#if defined(USE_FLAT)
    return colorTexCoordFlat;
#elif defined(USE_SMOOTH)
    return colorTexCoord;
#else
    return (steepness > 0.15) ? colorTexCoord : colorTexCoordFlat;
#endif
}
#endif

// Fragment stage: compute the color texture coordinate. For the !NORMALS_PROVIDED
// path this derives steepness from the screen-space slope of the eye-relative
// position (matches the GLSL dFdx/dFdy face-normal math; FLIP_Y_WORLD swaps the
// cross-product order).
#if !defined(NORMALS_PROVIDED)
float2 flandscape_colortexcoord(float3 eyeRelativePos, float2 colorTexCoord, float2 colorTexCoordFlat)
{
#if FLIP_Y_WORLD
    float3 faceNormal = normalize(cross(ddy(eyeRelativePos), ddx(eyeRelativePos)));
#else
    float3 faceNormal = normalize(cross(ddx(eyeRelativePos), ddy(eyeRelativePos)));
#endif
    float steepness = clamp(1.0 - faceNormal.y, 0.0, 1.0);
    float2 result = colorTexCoordForSlope(steepness, colorTexCoord, colorTexCoordFlat);
    result.x = clamp(pow(steepness, 0.4f), 0.0f, 1.0f);
    return result;
}
#else
float2 flandscape_colortexcoord(float2 colorTexCoord)
{
    return colorTexCoord;
}
#endif

#endif // LANDSCAPE_COMMON_HLSLI
