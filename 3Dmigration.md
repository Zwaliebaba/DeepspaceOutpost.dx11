# 3D Scene → GPU Rendering Migration Plan

Move the in-flight 3D scene (ships, planet, sun) from **CPU projection +
software rasterization** to a **GPU vertex/index + shader** pipeline, reusing the
DX11 stack that already exists in `NeuronClient`.

> **Owner decisions (locked for this plan):**
> 1. **Visual target — Faithful + opt-in upgrades.** Default output is
>    pixel-faithful to today (flat-shaded palette faces, wireframe option, same
>    optics). The pipeline is built so smooth shading / real lighting / specular
>    can be switched on later behind a flag, not rebuilt.
> 2. **Scope — Ships + planet + sun together.** All three in-scene 3D object
>    classes move to the GPU in this effort. Stars and the HUD/scanner stay on
>    the current 2D path (a later, separate step).
> 3. **Hidden surfaces — Real z-buffer.** The painter's-algorithm sort is
>    replaced by the hardware depth buffer.

---

## 1. Current-state analysis (grounded in the code)

### 1.1 The 3D scene is computed entirely on the CPU
`DeepspaceOutpost/threed.cpp` is the whole 3D renderer, and it produces **2D
screen pixels**, not GPU geometry:

- **Vertex transform (CPU).** For each ship, `draw_solid_ship`
  (`threed.cpp:166`) and `draw_wireframe_ship` (`threed.cpp:55`) multiply each
  model point by the object's `rotmat` (`mult_vector`), add `obj->location`,
  then call `project_to_screen` (`threed.cpp:38`) which does the perspective
  divide via `Neuron::Client::ProjectPoint` (`ViewMetrics.h:50`) and rounds to
  integer pixels.
- **Backface culling (CPU).** Wireframe uses a face-normal · camera-vector dot
  product (`threed.cpp:95`); solid uses a 2D signed-area sign test on the
  projected triangle (`threed.cpp:250`).
- **Hidden-surface ordering (CPU painter's algorithm).** Projected faces are
  emitted with a depth key `zavg` (the max model-Z of the face,
  `threed.cpp:309`) into a linked list sorted back-to-front in
  `gfx_render_polygon` (`platform/gfx2d.cpp:511`) and flushed by
  `gfx_finish_render` (`platform/gfx2d.cpp:537`). **No depth buffer is used for
  the scene.**
- **Planet & sun are software pixel rasterizers.** `render_planet_line`
  (`threed.cpp:528`) and `render_sun_line` (`threed.cpp:670`) walk each scanline
  and emit **one `FastPixel` per pixel** — the single largest CPU cost and
  command-count source in a frame.
- **Explosions** scatter `Pixel` commands from projected vertices
  (`draw_explosion`, `threed.cpp:788`).

### 1.2 The submission path (render seam)
Game logic never calls `gfx_*` directly. The flow is:

```
update_local_objects (space.cpp:524)            // sim render pass
  → StartRender (RenderQueue)                    // space.cpp:533
  → draw_ship → draw_solid_ship/draw_planet/...  // CPU projection, threed.cpp
      → ActiveRenderQueue().RenderPolygon(2D pts, colour, zavg)   // 2D + depth key
  → FinishRender                                 // space.cpp:632
RenderQueue.Replay → GfxRenderSink → gfx_* (gfx2d.cpp)            // GfxRenderSink.cpp
  → g_poly_chain painter sort → flat 2D tris/lines
  → ImmediateRenderer under Ortho2D, DEPTH TEST OFF              // gfx2d.cpp:577,583
  → off-screen 512×514 canvas → letterboxed present (Renderer)
```

The seam (`NeuronClient/RenderQueue.h`) keeps the **simulation headless** (the
dedicated server/BotClient use a `NullRenderSink`). **Any GPU 3D work must stay
on the client side of this seam** — it must not pull D3D into the sim.

### 1.3 A GPU 3D pipeline already exists — and is unused by the scene
`NeuronClient/graphics/ImmediateRenderer` is a full fixed-function-style DX11
renderer with everything the migration needs, today driven **only** by the 2D /
GUI path:

- Matrix stack with `Perspective`, `Frustum`, `LookAt`, `Push/Pop`
  (`ImmediateRenderer.h:118-131`).
- A lit, coloured 3D shader program **`Colored3D`** (`ImmediateRenderer.h:64`;
  `shaders/generic-colored-3dVS.hlsl`) — per-vertex normal + colour, directional
  lights, fog. **No game code selects it** (`grep` shows only the enum/apply
  site). It is ready-made dead code awaiting a caller.
- Depth-stencil state (`SetDepthTestEnabled/WriteEnabled/Func`) and a depth
  buffer already allocated in `GraphicsCore` (`GetDepthStencilView`,
  `GraphicsCore.cpp:260`).
- Backface cull state, blend state, scissor.
- Shaders are committed as `CompiledShaders/*.h`; `fxc` is only needed to
  regenerate after editing HLSL (`NeuronClient/CMakeLists.txt:121`).

### 1.4 The data is already GPU-friendly
- **Static model tables.** Ships are `ship_data` (`shipdata.h:43`) with
  `points[]` (int x,y,z), `lines[]`, `normals[]`; solids are
  `ship_solids[type].face_data[]` of `ship_face` (`shipface.h:4`) — each face has
  a `colour` (palette index), a vertex count `points` (2–8, fan-ordered
  `p1..p8`), and a model-space normal. These are **constant per ship type**, so
  they upload **once** into GPU vertex/index buffers and never change.
- **Per-object transform is already clean.** The scene comes from replication:
  `ReplicatedScene::BuildRenderRecords` (`ReplicatedScene.h:43`) yields, per
  entity, `{ location, rotmat[side/roof/nose], type, distance }` in the camera
  frame — i.e. **a ready-made model matrix**. The local player is the origin.
- **Palette → RGBA** is a baked table (`platform/scanner_palette.h`,
  `col_rgba()` in `gfx2d.cpp`); the GPU path reuses the identical mapping so
  colours match exactly.

### 1.5 Constraints
- **Windows / DX11 / MSVC only.** Cannot be built or run in the Linux dev
  container; **every phase must be verified by Windows CI** (`.github/workflows/ci.yml`)
  and the headless test suite.
- **Faithful look is a hard requirement.** Output must match the legacy
  projection/optics at the 4:3 viewport (the existing software path is
  bit-defined there — see `ViewMetrics.h`).
- **Headless sim must keep working** — the GPU path lives behind the render seam.
- **Off-screen canvas has no depth buffer today.** `Renderer`'s 512×514 canvas
  RT (bound in `gfx2d_flush`) is depth-less; the scene's z-buffer is a new
  resource (see §4, Phase 1).

---

## 2. Target architecture

Introduce a client-only **`SceneRenderer`** that draws the 3D scene with real GPU
geometry, fed by a new **3D submission command** on the render seam. The CPU
projection/raster in `threed.cpp` is retired for ships/planet/sun; the seam stops
carrying pre-projected 2D triangles for them and instead carries **object type +
model matrix + style**.

```
update_local_objects (sim, headless-safe)
  → ActiveRenderQueue().DrawModel(type, modelMatrix, style, flags)   // NEW: 3D, not 2D
  → (planet/sun emit DrawModel with a sphere mesh + params)
RenderQueue.Replay → RenderSink::DrawModel → SceneRenderer (client)
  SceneRenderer per frame:
    - set Perspective projection from ViewMetrics (matches legacy optics)
    - bind depth buffer; clear depth; depth test+write ON; backface cull ON
    - for each model: upload model matrix (per-draw CB), bind cached mesh VB/IB,
      draw indexed through ImmediateRenderer's Colored3D program (flat-shaded)
  → 2D HUD / scanner / text / stars draw AFTER, on the existing 2D path
```

### Key components
| Component | Where | Role |
|---|---|---|
| **`MeshLibrary`** | `NeuronClient/graphics/` (new) | Builds & caches one immutable GPU vertex+index buffer per ship type from `ship_data`/`ship_solids`, and a generated sphere mesh for planet/sun. Built once at startup. |
| **`SceneRenderer`** | `NeuronClient/graphics/` (new) | Owns the per-frame 3D pass: projection from `ViewMetrics`, depth/cull state, per-object model-matrix upload, indexed draws via `ImmediateRenderer::Colored3D`. Client-only. |
| **`RenderQueue` 3D command** | `NeuronClient/RenderQueue.h` (extend) | New `DrawModel{ type, float modelMatrix[16] or (rotmat+location), style, colourOverride, flags }` command + `RenderSink::DrawModel`. Keeps the sim headless (NullSink ignores it). |
| **`SceneRenderSink`** | client | Implements `DrawModel` by forwarding to `SceneRenderer`; the `NullRenderSink` no-ops it for headless. |
| **Flat-shaded 3D shader** | `shaders/` | Either reuse `Colored3D` with lighting **disabled** (flat palette colour per face = faithful default), or add a tiny `scene-flatPS`. The "opt-in upgrade" path turns `Colored3D` lighting **on**. |

### Why this shape
- **Faithful by construction:** flat per-face palette colour + the same FOV/optics
  reproduce the current look; the z-buffer replaces the painter's sort with the
  same back-to-front result for convex hulls.
- **Opt-in upgrades for free:** `Colored3D` already carries normals + directional
  lights + specular; flipping `SetLightingEnabled(true)` and feeding per-vertex
  normals lights the ships with no new pipeline.
- **Minimal new surface area:** reuses the existing matrix stack, depth state,
  shader, and palette mapping. The genuinely new code is mesh upload + the 3D
  seam command — real abstractions, consistent with the repo's Native-First rule.

---

## 3. The projection / coordinate mapping (the subtle part)

The legacy software projection must be reproduced by a GPU projection matrix so
the faithful target holds. Facts to honour:

- Camera space: **x right, y up, z forward (z > 0 in front)**; screen y is
  **flipped** (`ProjectPoint` negates y, `ViewMetrics.h:54`).
- Optics: focal length and principal point come from `ViewMetrics`
  (`MakeViewMetrics`), preserving the legacy vertical FOV
  (`tan(fovY/2)=192/512`, ≈41°). A wider window reveals more world horizontally
  (same focal on both axes) rather than stretching.
- `Colored3D` currently uses **right-handed** perspective
  (`XMMatrixPerspectiveFovRH`, `ImmediateRenderer.cpp:551`), which looks down
  **−z**. Legacy looks down **+z**. So either:
  - build the projection with `Frustum`/an explicit matrix that matches
    `ViewMetrics` (focal, cx, cy, the y-flip) and a +z forward convention, **or**
  - negate z when loading vertices / use a LH perspective.
- **Action:** add a `SceneRenderer` helper that derives the projection **directly
  from `ViewMetrics`** (off-centre frustum using `focal`, `cx`, `cy`, `width`,
  `height`, near/far) so projection is provably identical to `ProjectPoint` at
  any viewport, and is **unit-tested headlessly** against `ProjectPoint` for a
  grid of points (the existing `ViewMetricsTests`/`Tests/NeuronClient` pattern).
- Near/far: pick a near plane below the closest renderable z (legacy clamps
  `rz<=0` to small positive, `threed.cpp:237`) and a far plane past the cull
  distance (`distance > 57344` removes ships, `space.cpp:611`). Depth range tuned
  so co-planar faces don't z-fight (see Phase 2 risk).

---

## 4. Phase plan

Each phase is independently shippable and **Windows-CI-verified**. Phases 0–2 are
faithful-look ships; 3 adds planet/sun; 4 retires dead CPU code; 5 is the opt-in
visual upgrade.

### Phase 0 — Seam + headless scaffolding (no visual change)
- Add `CommandType::DrawModel` + `RenderQueue::DrawModel(...)` and
  `RenderSink::DrawModel`; `NullRenderSink` no-ops it (keeps server/BotClient
  building). Extend `RenderQueue::Replay`.
- Add a `SceneRenderSink` (client) that forwards `DrawModel` to a stub
  `SceneRenderer`.
- **Unit tests (headless, run in Linux CI too):** `DrawModel` round-trips through
  `RenderQueue` record/replay; `NullRenderSink` ignores it; the projection helper
  matches `ProjectPoint` for a point grid.
- **Deliverable:** nothing renders differently yet; seam + tests in place.

### Phase 1 — `MeshLibrary` + depth target (infrastructure)
- `MeshLibrary`: at startup, convert each `ship_solids[type]` into an interleaved
  `ImmediateVertex` VB + index buffer (fan-triangulate each `ship_face`'s
  `p1..pN` exactly as `gfx_polygon` does, `gfx2d.cpp:335`; per-vertex colour =
  `col_rgba(face.colour)`; per-vertex normal = the face normal for flat shading).
  Cache immutable buffers keyed by ship type.
- Allocate a **depth buffer for the scene render target** (the off-screen canvas
  is depth-less today). Decide render order: simplest faithful option is to draw
  the 3D scene **first, to the canvas, with depth**, then the 2D HUD/scanner/text
  over it with depth-test off (matches today's "HUD floats over 3D").
- **Tests:** mesh builder is pure data → unit-test triangle/index counts and
  winding against `ship_solids` for a few ships headlessly.
- **Deliverable:** buffers + depth exist; still not drawn.

### Phase 2 — Ships on the GPU (the keystone)
- `SceneRenderer::DrawModel` for ships: set the `ViewMetrics`-derived perspective
  projection; depth test+write ON, `LESS_EQUAL`; backface cull ON (winding
  chosen to match legacy front-facing); `Colored3D` with **lighting off** (flat
  per-face palette colour). Upload the per-object model matrix (`rotmat` +
  `location`) to the per-draw constant buffer; bind the cached mesh; draw
  indexed.
- Switch `draw_ship`/`draw_solid_ship` to **emit `DrawModel`** instead of CPU
  projection + `RenderPolygon`. Keep the **wireframe** option by drawing the mesh
  as line topology (or the `lines[]` index buffer) under the same projection.
- Laser lines / `FLG_FIRING` (`threed.cpp:314`) and explosions
  (`draw_explosion`): keep on the 2D path initially (project the muzzle point via
  the same matrix), or move to a 3D line pass — explosions can stay 2D for now.
- **Verify on Windows:** ships match the legacy look at 4:3 (side-by-side against
  the painter's-sort build); confirm depth ordering on overlapping ships,
  back-face hiding, and wireframe toggle. Confirm window-resize still correct.
- **Deliverable:** all ships render through the GPU; painter's sort no longer
  used for ships.

### Phase 3 — Planet & sun on the GPU
- Generate a **UV/ico sphere mesh** once in `MeshLibrary`. Draw planet/sun as a
  transformed sphere instead of the per-pixel scanline rasterizers
  (`render_planet`, `render_sun`).
- **Planet:** reproduce the current styles (wireframe / fractal-landscape /
  SNES-band, selected by `planet_render_style`, `threed.cpp:633`) as shader
  inputs — the fractal/SNES `landscape[]` colour map becomes a texture sampled on
  the sphere; wireframe = line draw. Faithful default keeps the same banding.
- **Sun:** the radial colour bands (`render_sun_line`, `threed.cpp:670`) become a
  fragment-shader radial gradient (white core → yellow → orange) with the same
  thresholds; the dither (`mix = (sx^y)&1`) is reproducible in-shader.
- Retire `render_planet*` / `render_sun*` and the `FastPixel` flood once visually
  confirmed.
- **Deliverable:** planet + sun are GPU spheres; the software pixel rasterizers
  are gone (largest per-frame CPU win).

### Phase 4 — Retire dead CPU 3D code & the painter's chain
- Remove the now-unused `gfx_render_polygon`/`gfx_finish_render` **painter's
  linked list** (`g_poly_chain`, `gfx2d.cpp:275-548`) and the
  `RenderPolygon`/`RenderLine` 2D-projection paths for the scene, plus the
  obsolete `project_to_screen` ship/planet/sun bodies in `threed.cpp`.
- Keep `ViewMetrics`/`ProjectPoint` (still the spec the GPU projection is tested
  against, and used by any remaining 2D-projected markers like the missile
  reticle).
- **Deliverable:** one 3D path (GPU); `threed.cpp` shrinks to scene-submission
  glue.

### Phase 5 — Opt-in visual upgrades (behind a flag)
- Add a client setting (config/options) `scene_shading = flat | lit`. `lit` flips
  `Colored3D` lighting on, supplies a directional light, and uses the per-vertex
  normals already in the meshes; optionally smooth normals (average face normals
  at shared vertices) for rounded hulls, and enable specular
  (`SetSpecularEnabled`).
- Optional: fog via the existing `SetFog*` for distance haze; MSAA on the scene
  RT for clean edges.
- **Deliverable:** modern shading is a toggle; faithful flat look remains the
  default.

---

## 5. Risks & mitigations
- **Projection mismatch vs legacy** — the faithful look hinges on the GPU
  projection equalling `ProjectPoint`. *Mitigation:* derive the matrix from
  `ViewMetrics` and **unit-test it against `ProjectPoint`** over a point grid
  before any Windows run (catchable in Linux CI).
- **Handedness / winding** — `Colored3D` is RH/−z; legacy is +z. Getting cull
  direction or z sign wrong shows as inside-out or invisible ships. *Mitigation:*
  pin the convention in the projection helper; verify with the cull-toggle and a
  known ship (Cobra) on Windows.
- **Z-fighting on co-planar faces** (the owner accepted minor ordering
  differences vs painter's). *Mitigation:* sufficient depth precision (near plane
  not too small), optional small depth bias; the legacy `zavg` max-Z key was
  itself approximate, so the z-buffer is generally *more* correct.
- **Canvas depth target / render order** — the off-screen canvas has no depth.
  *Mitigation:* add a scene depth buffer and draw 3D-then-HUD (Phase 1); confirm
  HUD still floats correctly in full-window and retro modes.
- **Cannot build/run locally** — Linux container is HLSL/D3D-blind. *Mitigation:*
  maximize headless-testable surface (seam, mesh builder, projection math run in
  CI), and gate each visual phase on a Windows CI build + owner visual check, as
  the GUI/GraphicsCore work already did.
- **Wireframe + explosions + laser** are not flat faces. *Mitigation:* keep them
  on existing paths until explicitly moved (Phase 2 note); they are cosmetic and
  low-risk.
- **Scope creep into stars/HUD** — explicitly **out of scope** here; the 2D path
  keeps drawing them.

## 6. Testing & CI
- **Headless (Linux CI + Windows):** projection-vs-`ProjectPoint` equivalence;
  `RenderQueue` `DrawModel` record/replay; `NullRenderSink` no-op; `MeshLibrary`
  triangle/index/winding counts vs `ship_solids`. Follows the existing
  `Tests/NeuronClient` pattern (`ViewMetricsTests`, `ReplicatedSceneTests`).
- **Windows CI:** build must stay green every phase (`.github/workflows/ci.yml`).
- **Owner visual validation** per visual phase: side-by-side vs the prior build —
  ship shapes, face colours, depth ordering, wireframe toggle, planet styles, sun
  bands, window resize, retro-vs-full-window HUD.

## 7. File-by-file change summary
| File | Change |
|---|---|
| `NeuronClient/RenderQueue.h/.cpp` | Add `DrawModel` command + sink method; extend `Replay`; `NullRenderSink` no-op. |
| `NeuronClient/graphics/MeshLibrary.{h,cpp}` | **New.** Build/cache GPU meshes from `ship_solids` + generated sphere. |
| `NeuronClient/graphics/SceneRenderer.{h,cpp}` | **New.** Per-frame 3D pass: projection, depth/cull, per-object matrix, indexed draws via `Colored3D`. |
| `NeuronClient/graphics/SceneRenderer` projection helper | **New + unit-tested** against `ViewMetrics::ProjectPoint`. |
| `NeuronClient/platform/GfxRenderSink.*` / new `SceneRenderSink` | Forward `DrawModel` to `SceneRenderer`. |
| `DeepspaceOutpost/threed.cpp` | `draw_solid_ship`/`draw_wireframe_ship`/`draw_planet`/`draw_sun` emit `DrawModel`; retire CPU projection/raster bodies (Phase 4). |
| `NeuronClient/platform/gfx2d.cpp` | Retire `g_poly_chain` painter's chain + `gfx_render_polygon/line/finish` once unused (Phase 4); add scene depth target + draw order (Phase 1). |
| `NeuronClient/graphics/GraphicsCore` / `Renderer` | Provide a depth buffer for the scene RT; ensure resize rebuilds it. |
| `NeuronClient/shaders/` | Reuse `generic-colored-3d` (flat default / lit opt-in); optional `scene-flat` PS. Regenerate `CompiledShaders/*.h` with `fxc`. |
| `Tests/NeuronClient/` | New tests: projection equivalence, `DrawModel` seam, mesh builder. |
| `docs/` | Cross-link this plan from `MIGRATION_ROADMAP.md` / `gui-graphicscore-status.md`. |

## 8. Suggested first step
Land **Phase 0 + Phase 1** together: they add the `DrawModel` seam, the
`MeshLibrary`, the projection helper, and their headless tests **without changing
a single on-screen pixel** — so they are fully verifiable in Linux CI before the
first Windows-only visual phase (Phase 2) flips ships onto the GPU.
