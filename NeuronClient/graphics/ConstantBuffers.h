#pragma once

// GPU constant-buffer ABI for the native Direct3D 11 renderer.
//
// Each struct here is the CPU mirror of an HLSL `cbuffer` declared in
// NeuronClient/shaders/partials. The layouts are flattened (no nested structs) and
// padded explicitly so the C++ size and field offsets match the HLSL constant-buffer
// packing rules exactly:
//   - a scalar/vector never straddles a 16-byte boundary,
//   - arrays pad each element to 16 bytes,
//   - the buffer total is rounded up to 16 bytes.
// static_assert guards every size so a layout drift fails the build instead of
// corrupting rendering. Relies on the precompiled umbrella (DirectXHelper.h /
// GameMath.h) for DirectXMath and the `DirectX` namespace.

namespace Neuron::Graphics
{
  // Constant-buffer bind slots (b#). Must match register(bN) in the shaders.
  namespace ConstantRegister
  {
    inline constexpr UINT PER_VIEW = 0;   // matrices.hlsli
    inline constexpr UINT FOG = 3;        // fog.hlsli
    inline constexpr UINT CLIP = 5;       // clipping.hlsli
    inline constexpr UINT LIGHTING = 6;   // lighting.hlsli
    inline constexpr UINT ALPHA_TEST = 7; // alphatest.hlsli
    inline constexpr UINT TEX_ENV = 8;    // texenv.hlsli
    inline constexpr UINT PER_DRAW = 9;   // per-shader material/flags
  }

  // Alpha-test comparison functions (mirror alphatest.hlsli constants).
  namespace AlphaFunc
  {
    inline constexpr int NEVER = 0;
    inline constexpr int LESS = 1;
    inline constexpr int EQUAL = 2;
    inline constexpr int LESS_EQUAL = 3;
    inline constexpr int GREATER = 4;
    inline constexpr int NOT_EQUAL = 5;
    inline constexpr int GREATER_EQUAL = 6;
    inline constexpr int ALWAYS = 7;
  }

  // Texture-environment combine modes (mirror texenv.hlsli constants).
  namespace TexEnvMode
  {
    inline constexpr int MODULATE = 1;
    inline constexpr int COMBINE = 2;
    inline constexpr int REPLACE = 3;
    inline constexpr int DECAL = 4;
  }

  // b0 : view/projection state. Row-major, row-vector convention: clip = mul(pos, M).
  // Matrices are uploaded as the transpose of the legacy column-vector GL upload.
  struct PerViewConstants
  {
    XMFLOAT4X4 view;            // GL "modelview": object -> eye
    XMFLOAT4X4 projection;      // eye -> clip ([0,1] depth)
    XMFLOAT4X4 viewProjection;  // convenience: view * projection
    // float3x3 normal matrix, one row per 16-byte slot (last 4 bytes padding).
    XMFLOAT3 normalMatrix0; float _padN0;
    XMFLOAT3 normalMatrix1; float _padN1;
    XMFLOAT3 normalMatrix2; float _padN2;
  };
  static_assert(sizeof(PerViewConstants) == 240, "PerViewConstants must match cbuffer b0");

  // b3 : linear fog.
  struct FogConstants
  {
    XMFLOAT4 color;
    float start;
    float end;
    int enable;        // bool -> int
    int inPixelEffect; // bool -> int
  };
  static_assert(sizeof(FogConstants) == 32, "FogConstants must match cbuffer b3");

  // b5 : user clip planes (view-space plane equations).
  struct ClipConstants
  {
    XMFLOAT4 clipPlane[2];
  };
  static_assert(sizeof(ClipConstants) == 32, "ClipConstants must match cbuffer b5");

  // Max directional lights. MUST equal the MAX_LIGHTS macro the lit shaders are
  // compiled with (see NeuronClient/CMakeLists.txt).
  inline constexpr int MAX_LIGHTS = 8;

  // b6 : fixed-function directional lighting. lightEnable uses a 16-byte stride per
  // element to match an HLSL int[] array; only the .x lane is meaningful.
  struct LightingConstants
  {
    XMFLOAT4 lightPos[MAX_LIGHTS];      // direction (w = 0)
    XMFLOAT4 lightDiffuse[MAX_LIGHTS];
    XMFLOAT4 lightAmbient[MAX_LIGHTS];
    XMFLOAT4 lightSpecular[MAX_LIGHTS];
    XMINT4 lightEnable[MAX_LIGHTS];
    int lightingEnable;
    float materialShininess;
    int specularEnable; // D3DRS_SPECULARENABLE analog; 0 = off (the fixed-function default)
    int _pad1;
  };
  static_assert(sizeof(LightingConstants) == 80 * MAX_LIGHTS + 16, "LightingConstants must match cbuffer b6");

  // b7 : alpha test.
  struct AlphaTestConstants
  {
    int function;     // AlphaFunc::*
    float clampValue; // reference value
    int _pad0;
    int _pad1;
  };
  static_assert(sizeof(AlphaTestConstants) == 16, "AlphaTestConstants must match cbuffer b7");

  // b8 : per-unit texture environment. .x = mode (TexEnvMode::*), .y = combineRGB.
  struct TexEnvConstants
  {
    XMINT4 unit[2];
  };
  static_assert(sizeof(TexEnvConstants) == 32, "TexEnvConstants must match cbuffer b8");

  // b9 : per-draw material parameters for the dedicated 2D/GUI programs (text,
  // gui-window, window-background). color is a global tint; colorEdge/colorCenter
  // drive the window-background vertical gradient. Matches perdraw.hlsli.
  struct PerDrawConstants
  {
    XMFLOAT4 color;
    XMFLOAT4 colorEdge;
    XMFLOAT4 colorCenter;
  };
  static_assert(sizeof(PerDrawConstants) == 48, "PerDrawConstants must match cbuffer b9");
}
