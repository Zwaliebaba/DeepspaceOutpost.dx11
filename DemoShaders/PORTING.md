# GLSL → HLSL (DirectX 11 / SM 5.0) Porting Guide

This folder is the HLSL port of the engine's GLSL shaders. Read this before
translating any shader so the output stays consistent. The canonical reference
is `partials/common.hlsli` (register map + conventions) and the already-ported
`generic-textured-quad{VS,PS}.hlsl` (the worked example).

## Output shape
- **One file per stage**: `<name>VS.hlsl` (entry `VSMain`) and `<name>PS.hlsl`
  (entry `PSMain`). Compile `vs_5_0` / `ps_5_0`.
- Partials become `partials/<name>.hlsli` with `#include` guards.
- `%include partials/x` → `#include "partials/x.hlsli"`.
- `%version` is dropped. `%uses`/`%shader`/`%common` markers are dropped (the
  `%common` body goes into the `.hlsli` and is shared by both stages).
- Drop the leading UTF-8 BOM.

## Matrix / coordinate convention (DX-native, row-vector)
- Transforms use **row vectors**: GLSL `M * v` → HLSL `mul(v, M)`.
  - `mv_pos = u_ViewMatrix * aPos`  → `mul(aPos, u_ViewMatrix)`
  - `gl_Position = u_ProjectionMatrix * mv_pos` → `mul(mv_pos, u_ProjectionMatrix)`
  - Chained `A * B * v` → `mul(v, mul(B, A))` (reverse order).
- Matrices are uploaded **row-major** (transpose of the GL upload); cbuffer
  matrix members are declared `row_major`.
- Clip-space depth is **[0,1]** (D3D), not [-1,1]. Adjust any shader that builds
  projections or reads/writes depth by hand.

## Stage I/O
- VS input attributes (`layout(location=N) in ...`) → `struct VSInput` members
  with semantics. Mapping used: loc0 `POSITION`, texcoords `TEXCOORD0..`,
  color `COLOR0`, normal `NORMAL`, plus extra generic data as `TEXCOORD#`.
  Keep order matching the engine's input layout (location index order).
- Varyings (`out`/`in`) → fields of a shared `VSOutput` struct (= PS input),
  packed `TEXCOORD0, TEXCOORD1, ...`. Clip position is `float4 pos : SV_Position`.
- `flat out`/`flat in` → prefix the field with `nointerpolation`.
- PS color outputs (`layout(location=N) out vec4`) → return value
  `: SV_Target` (or `SV_TargetN` / an output struct for MRT).
- `gl_VertexID` → `uint vertexID : SV_VertexID`.
- `gl_InstanceID` → `uint instanceID : SV_InstanceID`.
- `gl_FragCoord` → `float4 ... : SV_Position` PS input (note: `.xy` are pixel
  centers, same as GL; `.z` is [0,1] depth; `.w` is 1/clip.w as in GL).
- `gl_FrontFacing` → `bool isFrontFace : SV_IsFrontFace` PS input; thread it
  into helpers (`fdebugcolor`, lighting) that need it.
- `discard;` stays `discard;`.
- `gl_Position.y` flips etc. → operate on the `SV_Position` output field.

## Cross-stage helpers (IMPORTANT)
GLSL partials declare their own global varyings and read/write them inside
helpers. HLSL can't do that — the **shader owns the I/O struct**, and partial
helpers are **pure functions** taking/returning values:
- `vfog(mv_pos)` returns the fog varying value → store in `o.fogViewSpace`.
  `ffog(color, input.fogViewSpace)` consumes it.
- `vclipping(mv_pos)` returns `float2` distances → store in clip field.
  `fclipping(input.clipDist)` discards.
- Lighting: the shader declares the lighting varyings (see `lighting.hlsli`
  header comment for the exact field list + semantics) and passes them to the
  helpers.

## Types & intrinsics
| GLSL | HLSL |
|------|------|
| `vec2/3/4` | `float2/3/4` |
| `ivec*` `uvec*` `bvec*` | `int*` `uint*` `bool*` |
| `mat3/mat4` | `float3x3/float4x4` (declare `row_major` in cbuffers) |
| `mix` `fract` `mod` `dFdx/dFdy` `inversesqrt` | `lerp` `frac` `fmod`* `ddx/ddy` `rsqrt` (aliased in common.hlsli) |
| `texture(s, uv)` | `s.Sample(s_sampler, uv)` |
| `textureLod(s, uv, l)` | `s.SampleLevel(s_sampler, uv, l)` |
| `texelFetch(s, ip, lod)` | `s.Load(int3(ip, lod))` |
| `textureSize(s, lod)` | `s.GetDimensions(...)` |
| `mat * vec` | `mul(vec, mat)` (row-vector!) |
| `m[i]` (column) | transposed layout — index carefully |
| `lessThan(a,b)` etc. | `(a < b)` (component compare returns bool vector) |
| `mix(a,b,bvec)` | `lerp(a, b, (float-cast of bvec))` |
| `vecN(x)` splat | `(typeN)x` or `floatN(x,x,..)` |
| array literal `T[](..)` | `{ .. }` initializer; `static const T name[] = {..};` |
| `const` global | `static const` |
| `mat3(m4)` | cast: `(float3x3)m4` |
| `inverse()` / `transpose()` | HLSL has `transpose`; **no `inverse`** — provide a helper |
| `*=` on swizzle, `a.bg` etc. | identical |
| literals `1.0f` | fine in HLSL |
| `bool` uniform | `int` in cbuffer, compare `!= 0` |

\* Note: GLSL `mod` has different sign behavior than HLSL `fmod`; common.hlsli
defines `mod(x,y)` via `floor` to match GLSL. Use `mod(...)` not `fmod`.

## Uniforms → constant buffers (register map in common.hlsli)
- Group by concept into the fixed `b#` slots from common.hlsli.
- Per-shader leftover uniforms → `cbuffer PerDraw : register(b9)` (per-object)
  or `cbuffer PerPass : register(b10)` (fullscreen/post params).
- Respect HLSL cbuffer packing: a `float`/`int`/`bool`(→int) won't straddle a
  16-byte boundary; arrays pad each element to 16 bytes (matches GL std140-ish,
  but verify struct sizes against the engine upload).
- `uniform sampler2D u_TextureN` → `Texture2D u_TextureN : register(tN);` +
  `SamplerState u_TextureN_sampler : register(sN);` (shared index N).
- `sampler2DMS` → `Texture2DMS<float4> u_TextureN : register(tN);` (no sampler;
  use `.Load(coord, sampleIndex)`).

## Verify every file
Compile before declaring done (features off, then a couple flag combos):
```
fxc /nologo /T vs_5_0 /E VSMain <name>VS.hlsl
fxc /nologo /T ps_5_0 /E PSMain <name>PS.hlsl
```
fxc path: `C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x86\fxc.exe`
