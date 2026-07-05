# Making the starfield look natural

## Goal
The stars currently read as flat white squares with no depth. This plan turns them
into soft, varied points of light with real depth cues, while keeping the existing
Elite-style streaming starfield (the speed cue) intact.

---

## Why it looks unnatural today

Current pipeline: [stars.cpp](DeepspaceOutpost/stars.cpp) builds screen-space quads →
`Scene3D::SetDust` → [renderDust](NeuronClient/graphics/Scene3D.cpp#L307) →
[dustVS](NeuronClient/shaders/dustVS.hlsl)/[dustPS](NeuronClient/shaders/dustPS.hlsl).

Concrete problems:

1. **Uniform pure white.** `DustVertex{x,y,bright}` has a `bright` field, but
   [`push_dust`](DeepspaceOutpost/stars.cpp#L91) always writes `b = 1.0f`, so every
   star is identical full-white. Real skies are dominated by *faint* stars with a few
   bright ones (a power-law magnitude distribution).
2. **Hard square dots.** Each star is a solid 6-vertex quad with no UV and a constant-
   color pixel shader. No soft core, no falloff, no glow — so they look like pixels.
3. **Only 3 sizes**, hard-stepped off `z` (`2.4 / 1.8 / 1.2px`), with size *uncorrelated*
   to brightness. Nothing dims with distance, so there's no depth read.
4. **Opaque blend** (`BlendEnable = FALSE` at [Scene3D.cpp:180](NeuronClient/graphics/Scene3D.cpp#L179)).
   Overlapping stars don't accumulate; there's no bloom/halo against the black.
5. **Single thin depth layer** of ~48 stars over `z ∈ [~0x90, 0x1FF]`. The whole field
   streams at one rate — there is no distant, near-static backdrop behind the motion.
6. **No color temperature.** Real stars span blue-white → white → yellow → orange → red.

---

## Asset: `Textures/Starburst.dds`

We have [GameData/Textures/Starburst.dds](GameData/Textures/Starburst.dds) (a 128×128
glow/star sprite, same size class as `Glow.dds`). **Use it as the star sprite** — sample
it per-fragment instead of computing the falloff procedurally. It bakes the soft core,
the halo, and (if the art has them) faint diffraction spikes into one texture read, which
looks richer than a hand-tuned gaussian and makes Phase 4b essentially free.

- Load once via `TextureManager::LoadTexture("Textures\\Starburst.dds")` (same idiom as
  [space.cpp:789](DeepspaceOutpost/space.cpp#L789) / [GuiWindow.cpp:85](NeuronClient/gui/GuiWindow.cpp#L85)),
  hold the `shared_ptr<Texture>` in Scene3D, bind its SRV in `renderDust`.
- **Scene3D has no sampler today** (the billboard PS is fully procedural) — the dust pass
  would be the first textured pass there, so we add a linear-clamp `ID3D11SamplerState`
  alongside the new blend state.
- Tint the sampled texture by the per-star color × intensity (below). Keep the DDS
  itself neutral white so tint/brightness are driven from the vertex.

> Alternative if we'd rather stay texture-free: the procedural core+halo in the collapsed
> block below also works. The texture path is preferred now that the asset exists.

## The plan (ordered by impact-to-effort)

### Phase 1 — Soft, varied point sprites (biggest visual win)

Core fix: textured sprite + per-star variation. Touches the shader, `push_dust`, and the
dust pass state.

**1a. Sample `Starburst.dds` for the star's shape instead of a hard square.**
- Add a UV to the dust vertex. Change `DustVertex{float x,y,bright}` →
  `DustVertex{float x,y; float u,v; float r,g,b; float intensity;}` (color+intensity from
  1c). Update the input layout in [Scene3D.cpp:224](NeuronClient/graphics/Scene3D.cpp#L224)
  and the HLSL structs in [dust.hlsli](NeuronClient/shaders/partials/dust.hlsli). Emit the
  four corner UVs (`0..1`) from [`push_dust`](DeepspaceOutpost/stars.cpp#L91) alongside the
  corner positions.
- In [dustPS](NeuronClient/shaders/dustPS.hlsl), sample and tint:
  ```hlsl
  Texture2D    starTex : register(t0);
  SamplerState samp    : register(s0);
  float4 PSMain(VSOut i) : SV_Target {
      float a = starTex.Sample(samp, i.uv).r;      // sprite alpha/luma
      return float4(i.color * i.intensity * a, a);  // premultiplied, additive
  }
  ```
- Bind the SRV + sampler in [`renderDust`](NeuronClient/graphics/Scene3D.cpp#L307) before
  the `Draw` (`PSSetShaderResources(0,1,&srv)`, `PSSetSamplers(0,1,&samp)`).

> **Procedural alternative (no texture):** compute `d = length(uv)` (uv `[-1,1]`),
> `core = saturate(1-d*d)`, `halo = exp(-d*d*4)`, `a = saturate(core*0.7 + halo*0.6)`,
> return `float4(color*intensity*a, a)`. Tune constants to taste.

**1b. Switch the dust pass to additive (or premultiplied) blending.**
- Add a dedicated additive blend state next to `s_blend`
  ([Scene3D.cpp:179](NeuronClient/graphics/Scene3D.cpp#L179)):
  `SrcBlend = ONE (or SRC_ALPHA), DestBlend = ONE, BlendOp = ADD`.
- Bind it in `renderDust` instead of the opaque `s_blend`
  ([Scene3D.cpp:339](NeuronClient/graphics/Scene3D.cpp#L339)).
- Additive makes soft stars sit on black correctly, lets overlapping glows bloom, and
  makes dim stars genuinely dim rather than gray squares. (Depth already off — good.)

**1c. Per-star brightness from a magnitude distribution.**
- In [`create_new_stars`](DeepspaceOutpost/stars.cpp#L116) / the respawn block of
  [`front_starfield`](DeepspaceOutpost/stars.cpp#L179), assign each star an intrinsic
  **magnitude** `mag ∈ [0,1]` skewed toward faint: e.g. `mag = pow(rand01(), 3.0)`
  → most stars near 0 (faint), a few near 1 (brilliant). Store it in the `star` struct.
- Carry it into the sprite as `intensity` so the PS scales by it.

### Phase 2 — Depth cues

**2a. Brightness falls off with distance.**
- Combine intrinsic magnitude with `z`: nearer stars (small `z`) brighter, far stars
  dimmer. e.g. `intensity = mag * saturate(1.2 - z / 320.0)`. This ties luminance to
  depth so the field reads as a volume, not a plane.

**2b. Size correlates with brightness, continuously.**
- Replace the 3-way `if` in [`push_dust`](DeepspaceOutpost/stars.cpp#L100) with a
  continuous size, e.g. `sizePx = lerp(1.0, 3.5, intensity)` (clamp to a max). Bright
  = larger soft disc, faint = near-pixel. Removes the stepped, banded look.

**2c. Add a distant, near-static backdrop layer (parallax).**
- Generate a second, denser set (e.g. 150–250) of very faint, high-`z` stars that
  barely stream (scale their `delta` by ~0.1–0.2). This deep layer stays almost still
  while the near layer streams past it — the classic two-layer parallax that sells
  depth and space "vastness." Keep it cheap: same dust buffer, just more verts.

### Phase 3 — Color temperature (subtle but sells realism)

**3a. Assign each star a spectral color.**
- Pick a temperature-like tint per star at spawn (weight toward white):
  blue-white `(0.75,0.83,1.0)`, white `(1,1,1)`, yellow `(1,0.95,0.82)`,
  orange `(1,0.83,0.63)`, red `(1,0.72,0.6)`. Store RGB in the `star` struct and pass
  through to the sprite `color`. Keep saturation low so it stays tasteful, not a
  disco. A faint majority-white field with occasional warm/cool accents looks real.

### Phase 4 — Optional polish

**4a. Gentle twinkle.** Feed frame time into the PS (or precompute on CPU) and modulate
   the *brightest* near stars by a small per-star sinusoid (±10–15%, random phase).
   Skip for distant/faint stars. Subtle — avoid a christmas-lights effect.

**4b. Diffraction spikes on the few brightest stars.** Largely handled for free if
   `Starburst.dds` already contains spikes — just scale the sprite size up for the
   brightest stars so the spikes become visible only on them. (Procedural fallback: add a
   thin 4-point cross via `abs(uv.x)`/`abs(uv.y)` for stars above a magnitude threshold.)

**4c. Density / count.** Bump `star_count()`
   ([stars.cpp:37](DeepspaceOutpost/stars.cpp#L37)) for the near layer once sprites are
   soft (denser reads fine when stars aren't all full-white). Tune against a dark HUD.

---

## Suggested implementation order
1. Phase 1a+1b+1c together (shader + blend + magnitude) — this alone transforms the look.
2. Phase 2 (depth: distance dimming, continuous size, parallax backdrop).
3. Phase 3 (color).
4. Phase 4 as taste/perf allows.

## Bonus: SceneGlow already exists
[SceneGlow](NeuronClient/graphics/SceneGlow.h) is an opt-in bloom post-pass (default off).
If enabled, the new soft *additive* stars would bloom through it for free — the brightest
stars gain a natural halo with no extra work. Worth a look once Phase 1 lands.

## Files to touch
- [DeepspaceOutpost/stars.cpp](DeepspaceOutpost/stars.cpp) — `star` struct, `push_dust`
  (emit UVs + color + intensity), `create_new_stars`, `front_starfield`
  (magnitude/color/parallax; wider `DustVertex`).
- [NeuronClient/graphics/Scene3D.h](NeuronClient/graphics/Scene3D.h) — widen `DustVertex`;
  add `shared_ptr<Texture>` for Starburst + sampler/blend state members.
- [NeuronClient/graphics/Scene3D.cpp](NeuronClient/graphics/Scene3D.cpp) — dust input
  layout (L224), additive blend + linear-clamp sampler state (near L179), load
  `Textures\\Starburst.dds`, and in `renderDust` (L307) bind blend/SRV/sampler.
- [NeuronClient/shaders/partials/dust.hlsli](NeuronClient/shaders/partials/dust.hlsli) —
  `VSIn`/`VSOut` (add uv + color + intensity).
- [NeuronClient/shaders/dustVS.hlsl](NeuronClient/shaders/dustVS.hlsl) — pass uv/color through.
- [NeuronClient/shaders/dustPS.hlsl](NeuronClient/shaders/dustPS.hlsl) — sample Starburst,
  tint by color × intensity (procedural core+halo as fallback).

## Cost / risk
- Vertex size grows (3 floats → ~8); at a few hundred stars this is negligible.
- One texture bound once + additive blend; no new passes, same single `Draw`.
- Scene3D gains its first sampler — trivial, but it's new state in that pass.
- Main risk is *tuning* (too bright/bloomy or too sparse) — all values above are knobs,
  so iterate live against the actual scene and HUD.
