#ifndef COMMON_HLSLI
#define COMMON_HLSLI

//==============================================================================
// common.hlsli  -- shared conventions for the native DirectX 11 renderer.
//
// Matrix convention: row-major, row-vector. A transform is `mul(v, M)` and the
// engine uploads matrices already transposed for this. Clip-space depth is [0,1].
//
// Constant-buffer register map (must match NeuronClient/ConstantBuffers.h):
//   b0 PerView   b3 Fog   b5 Clip   b6 Lighting   b7 AlphaTest   b8 TexEnv   b9 PerDraw
//
// Texture/sampler map: Texture2D u_TextureN : register(tN) with its sampler at sN.
//
// Feature flags are supplied at compile time (D3D_SHADER_MACRO). Defaults below
// mirror "feature off" so a bare compile succeeds.
//==============================================================================

#ifndef ENABLE_CLIP_PLANE0
#define ENABLE_CLIP_PLANE0 0
#endif
#ifndef ENABLE_CLIP_PLANE1
#define ENABLE_CLIP_PLANE1 0
#endif
#ifndef ENABLE_ALPHA_TEST
#define ENABLE_ALPHA_TEST 0
#endif
#ifndef MAX_LIGHTS
#define MAX_LIGHTS 0
#endif
#ifndef USE_HDR
#define USE_HDR 0
#endif

// GLSL-compatibility helpers, so lightly-edited math from the original shaders reads
// naturally. HLSL already provides saturate/lerp/frac/ddx/ddy/rsqrt/atan2.
#define mix lerp
#define fract frac
#define inversesqrt rsqrt
#define mod(x, y) ((x) - (y) * floor((x) / (y)))

#endif // COMMON_HLSLI
