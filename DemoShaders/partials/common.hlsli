#ifndef COMMON_HLSLI
#define COMMON_HLSLI

//==============================================================================
// common.hlsli
//
// Shared conventions for the HLSL (DirectX 11 / Shader Model 5.0) port of the
// engine's GLSL shaders.
//
// MATRIX CONVENTION (DX-native, row-vector / row-major):
//   - All transforms use row vectors:  clip = mul(worldPos, M)  ==  worldPos * M
//   - Matrices must be uploaded ROW-MAJOR, i.e. the *transpose* of what the GL
//     path uploaded (GL used column vectors: M * v). The engine's constant
//     buffer upload must transpose accordingly (or compile with the default
//     row_major and store transposed). float4x4 here is treated as row-major.
//   - Clip-space depth range is [0, 1] (D3D), not [-1, 1] (GL). Any shader that
//     constructs projections or reads depth must account for this.
//
// CONSTANT BUFFER REGISTER MAP (b#)  -- engine bind points must match these:
//   b0  Matrices       (matrices.hlsli)
//   b1  Camera         (camera.hlsli)
//   b2  Time           (time.hlsli)
//   b3  Fog            (fog.hlsli)
//   b4  Hdr            (hdr.hlsli)
//   b5  Clipping       (clipping.hlsli / clipping-object.hlsli)
//   b6  Lighting       (lighting.hlsli / lighting-landscape.hlsli)
//   b7  AlphaTest      (alphatest.hlsli)
//   b8  TextureEnv     (texenv.hlsli)
//   b9  PerDraw        (per-shader material / misc uniforms)
//   b10 PerPass        (post-process / fullscreen pass parameters)
//
// TEXTURE / SAMPLER REGISTER MAP (t# / s#):
//   Each GLSL `uniform sampler2D u_TextureN` becomes:
//       Texture2D    u_TextureN         : register(tN);
//       SamplerState u_TextureN_sampler : register(sN);
//   i.e. texture and its sampler share the same index N.
//
// FEATURE FLAGS:
//   The same #if / #ifdef feature flags as the GLSL sources are used and must
//   be supplied at compile time (D3D_SHADER_MACRO / -D). Defaults below mirror
//   "feature off" so a bare compile still succeeds.
//==============================================================================

// ----- Feature-flag defaults (mirror GLSL "off" state) -----------------------
#ifndef FEATURE_NATIVE_CLIPPING
#define FEATURE_NATIVE_CLIPPING 0
#endif
#ifndef ENABLE_CLIP_PLANE0
#define ENABLE_CLIP_PLANE0 0
#endif
#ifndef ENABLE_CLIP_PLANE1
#define ENABLE_CLIP_PLANE1 0
#endif
#ifndef FLIP_Y_WORLD
#define FLIP_Y_WORLD 0
#endif
#ifndef FLIP_Y_BLIT
#define FLIP_Y_BLIT 0
#endif
#ifndef FLIP_Y_FINAL
#define FLIP_Y_FINAL 0
#endif
#ifndef FLIP_Y_SAMPLE_POS
#define FLIP_Y_SAMPLE_POS 0
#endif
#ifndef USE_HDR
#define USE_HDR 0
#endif
#ifndef MAX_LIGHTS
#define MAX_LIGHTS 0
#endif
#ifndef TONEMAP_TYPE
#define TONEMAP_TYPE 0
#endif
#ifndef DEBUG_CULL_FACE
#define DEBUG_CULL_FACE 0
#endif

// ----- GLSL-compatibility helpers --------------------------------------------
// HLSL already provides: saturate, lerp(=mix), frac(=fract), ddx/ddy(=dFdx/dFdy),
// rsqrt, atan2. The aliases below let lightly-edited GLSL math read naturally.
#define mix       lerp
#define fract     frac
#define dFdx      ddx
#define dFdy      ddy
#define mod(x, y) ((x) - (y) * floor((x) / (y)))
#define inversesqrt rsqrt

#endif // COMMON_HLSLI
