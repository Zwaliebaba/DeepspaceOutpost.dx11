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

> **Re-validation — 2026-06-30 (after merging `main`).** This plan was first
> written against a client built on `ImmediateRenderer` (a fixed-function-style
> renderer with a matrix stack, a lit `Colored3D` 3D shader, and depth state) and
> an off-screen 512×514 canvas. **Both are gone.** `main` has since:
> - **Removed `ImmediateRenderer` and its whole shader pipeline** (incl. the
>   `Colored3D` lit 3D shader, the matrix stack, `Perspective`/`LookAt`, the
>   lighting/fog API). The replacement, **`Render2D`**, is a **strictly 2D**
>   batched layer: its vertex is `{x, y, u, v, rgba}` (no z, no normal), depth and
>   cull are always off, the projection is a hardcoded ortho, and custom shader
>   programs *must* keep the 2D vertex signature + ortho `b0`.
> - **Dropped the off-screen canvas** — the game now renders **directly to the
>   back buffer** (default 1920×1080, aspect-locked letterbox), via a virtual→target
>   mapping inside `Render2D::Begin`.
> - **Wired the GameMain lifecycle**: `ClientEngine::Frame` drives
>   `RenderScene()` → `GameApp::RenderScene()` → `game_render_scene()` ("draw the
>   3D + HUD into the back buffer"), then `gfx2d_flush`, then `RenderCanvas()` (GUI
>   overlay). There is now a real, named 3D hook.
>
> **Net effect on this plan:** the *strategy is unchanged and still valid* (extend
> the render seam with a 3D model command; build GPU meshes from the static ship
> tables; perspective + z-buffer; faithful flat shading with opt-in lighting;
> ships + planet + sun). **What changed is the premise:** there is **no longer an
> existing 3D pipeline to wire up** — the GPU 3D renderer must be **built new** as
> a sibling to `Render2D` (its own 3D vertex format, perspective/model constant
> buffers, depth-stencil + cull state, and new 3D shaders). The depth buffer, the
> palette→RGBA mapping, the `RenderQueue` seam, `ViewMetrics`/`ProjectPoint`,
> `ReplicatedScene`, and the static `shipdata`/`shipface` tables all survive
> unchanged. Sections below are updated to this reality; the changes are flagged
> **[updated 2026-06-30]**.

---

## Implementation status — 2026-06-30 (Phases 0–5 complete)

The migration is **implemented and visually verified on Windows**. The in-flight 3D
scene now renders entirely on the GPU through the new `Scene3D` renderer.

| Phase | Status | What landed |
|---|---|---|
| **0** Seam + headless scaffolding | ✅ Done | `RenderQueue::DrawModel` + `ModelDraw` seam; `NullRenderSink` no-op; headless tests (record/replay). |
| **1** `Scene3D` skeleton + mesh/projection | ✅ Done | `SceneProjection.h` (perspective derived from `ViewMetrics`, unit-tested vs `ProjectPoint`); `Mesh.h` `BuildSolidMesh`; `Scene3D` renderer + lazy GPU mesh cache. |
| **2** Ships on the GPU | ✅ Done | Solid ships render as GPU geometry with a real z-buffer (`CULL_NONE` + depth); painter's sort no longer used for ships. Fixed a blank-scene bug: the device came up with no depth buffer (`Core::Startup(..., D32_FLOAT)`). |
| **3** Planet + sun on the GPU | ✅ Done | Planet/sun render as depth-tested **billboards** in the same pass — retiring the software pixel rasterizers and fixing ship↔planet/sun occlusion. Sun = radial gradient; planet = ring / disk / banded by style. |
| **4** Retire dead CPU 3D code | ✅ Done | Deleted ~446 lines: the software planet/sun rasterizers + landscape subsystem, and the painter's linked list. `gfx_render_polygon/line` are now thin 2D forwarders (the laser bolt still uses them). |
| **5** Opt-in lit shading | ✅ Done | A "Ship Shading" setting (`scene_shading`, default **Flat**) toggles faceted directional lighting on ships via a ship-only `b2` light buffer; the flat path and billboards are untouched. |

**Faithful-default holds:** with `scene_shading = Flat` and the default planet style
(wireframe), the output matches the legacy look; lighting is purely opt-in.

**Known follow-ups (deferred, low value/used rarely):**
- SNES/fractal planet styles are a *banded approximation*, not the exact rotated
  landscape texture (the default wireframe + green styles are faithful).
- The laser bolt and explosions remain on the 2D path.
- The `RenderPolygon`/`RenderLine` seam vocabulary is kept as 2D forwarders rather
  than fully removed (broad, low-value churn).
- Per-vertex *smooth* normals + specular (the meshes are flat-normal today) and
  scene MSAA are possible future polish on the opt-in lit path.

---

## 1. Current-state analysis (grounded in the code)

> **Note:** §1–§7 below describe the pre-migration state and the plan as authored.
> They are retained as the design record; see the status table above for what
> actually shipped.

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
  → g_poly_chain painter sort → flat 2D tris/lines               // gfx2d.cpp:530-567
  → Render2D batch (ortho, DEPTH OFF) drawn DIRECTLY to the      // [updated] gfx2d_flush
    back buffer, virtual-space letterboxed (no canvas anymore)
```

> **[updated 2026-06-30]** The tail of this path changed on `main`: the flat 2D
> triangles/lines are now replayed through **`Render2D`** (`gfx2d_flush`), which
> clears the back buffer to black and draws the letterboxed batch **straight to
> the back buffer** — the old `ImmediateRenderer` + off-screen 512×514 canvas are
> gone. Everything *above* the `gfx_*` line (the CPU projection in `threed.cpp`,
> the `g_poly_chain` painter sort) is **unchanged** and still the live path.

The seam (`NeuronClient/RenderQueue.h`) keeps the **simulation headless** (the
dedicated server/BotClient use a `NullRenderSink`). **Any GPU 3D work must stay
on the client side of this seam** — it must not pull D3D into the sim.

### 1.3 The GPU 3D pipeline must be built new — *what survives, what doesn't* [updated 2026-06-30]
The original plan noted that a full 3D pipeline (`ImmediateRenderer` + the
`Colored3D` lit shader) already existed and merely needed a caller. **That code
has since been deleted from `main`.** The current 2D layer, `Render2D`, is
deliberately *not* a 3D renderer:

- **Gone:** the matrix stack, `Perspective`/`Frustum`/`LookAt`, the `Colored3D`
  lit shader, per-vertex normals in the vertex format, the directional-light/fog
  API, and the immediate depth/cull toggles that game code could drive.
- **`Render2D` cannot be extended into 3D in place:** its vertex is
  `{x, y, u, v, rgba}` (POSITION **float2**), it forces depth/cull off, and its
  custom-program contract (`RegisterProgram`) *requires* programs to keep the 2D
  vertex signature and the ortho matrix in `b0` (`Render2D.h:113-122`). A 3D pass
  needs a 3-component position, a normal, a perspective+model CB, and depth/cull
  state — none of which fit that contract.

**What still exists and is reusable (so this is not a from-zero build):**
- **The depth buffer.** `GraphicsCore` still allocates a `D32_FLOAT`
  depth-stencil bound alongside the back buffer (`GetDepthStencilView`,
  `GraphicsCore.h:63`; comment: *"Required for 3D"*). The 3D pass can use it
  directly — and because the canvas round-trip is gone, the scene renders at the
  real back-buffer resolution with depth, no extra target needed.
- **The palette→RGBA mapping** (`platform/scanner_palette.h`, `col_rgba()`).
- **The shader build system** — `fxc`-compiled `CompiledShaders/*.h` committed at
  build time (`NeuronClient/CMakeLists.txt`); new 3D shaders follow the same
  pattern (`render2dVS/PS.hlsl` is the live example).

**So the new work is a `Scene3D` renderer sibling to `Render2D`** (own 3D vertex
format, perspective/model constant buffers, depth-stencil + backface-cull state,
and two small 3D shaders). This is more net-new code than the first draft assumed,
but it is a self-contained, well-scoped addition — and `GraphicsCore` already
provides the device, swap chain, and depth buffer it needs.

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
- **[updated 2026-06-30] Render order / clear hazard.** The scene now renders
  **directly to the back buffer**, and `gfx2d_flush` **clears the back buffer to
  black** at the start of its 2D replay (`gfx2d.cpp`). The 3D scene pass must run
  **inside `game_render_scene()`** in the right order relative to that clear and
  the depth clear — draw 3D (clearing colour+depth once), *then* the 2D HUD on top
  with depth off — so the HUD does not wipe the scene and the scene does not z-test
  against stale depth. The integration point is `game_render_scene()` /
  `GameApp::RenderScene` (`ClientEngine.cpp:196-209`).
- **[updated 2026-06-30] No off-screen canvas** — the previous depth-less-canvas
  concern is moot; depth already lives alongside the back buffer.

---

## 2. Target architecture

Introduce a client-only **`Scene3D`** renderer that draws the 3D scene with real
GPU geometry, fed by a new **3D submission command** on the render seam. The CPU
projection/raster in `threed.cpp` is retired for ships/planet/sun; the seam stops
carrying pre-projected 2D triangles for them and instead carries **object type +
model matrix + style**.

```
update_local_objects (sim, headless-safe)
  → ActiveRenderQueue().DrawModel(type, modelMatrix, style, flags)   // NEW: 3D, not 2D
  → (planet/sun emit DrawModel with a sphere mesh + params)
RenderQueue.Replay → RenderSink::DrawModel → Scene3D (client)
  Scene3D per frame:
    - set Perspective projection from ViewMetrics (matches legacy optics)
    - bind depth buffer; clear depth; depth test+write ON; backface cull ON
    - for each model: upload model+view matrix (per-draw CB), bind cached mesh
      VB/IB, draw indexed through Scene3D's own 3D shader (flat-shaded default)
  → 2D HUD / scanner / text / stars draw AFTER, on the existing Render2D path
```

> **[updated 2026-06-30]** `Scene3D` is a **new renderer sibling to `Render2D`**,
> not a reuse of the deleted `ImmediateRenderer`. Both draw to the same back
> buffer + depth buffer from `GraphicsCore`, in the order set by
> `game_render_scene()`: 3D first (depth on), then the 2D HUD batch (depth off).

### Key components [updated 2026-06-30]
| Component | Where | Role |
|---|---|---|
| **`Scene3D`** | `NeuronClient/graphics/` (new) | **New 3D renderer**, sibling to `Render2D`. Owns the 3D vertex format (pos3 + normal + colour), the perspective+model/view constant buffers, the depth-stencil + backface-cull states, the 3D shaders, and a dynamic VB/IB. Per frame: set projection from `ViewMetrics`, clear/bind depth, draw each submitted model. Client-only, all-static (mirrors `Render2D`/`Core`). |
| **`MeshLibrary`** | `NeuronClient/graphics/` (new, may live inside `Scene3D`) | Builds & caches one immutable GPU vertex+index buffer per ship type from `ship_data`/`ship_solids`, plus a generated sphere mesh for planet/sun. Built once at startup. |
| **`RenderQueue` 3D command** | `NeuronClient/RenderQueue.h` (extend) | New `DrawModel{ type, rotmat+location (or modelMatrix[16]), style, colourOverride, flags }` command + `RenderSink::DrawModel`. Keeps the sim headless (NullSink ignores it). |
| **`SceneRenderSink`** | client | Implements `DrawModel` by forwarding to `Scene3D`; the `NullRenderSink` no-ops it for headless. |
| **3D shaders** | `shaders/` (new) | A new `scene3dVS/PS.hlsl` pair (`fxc` → committed `CompiledShaders/*.h`, same build path as `render2dVS/PS`). VS does model·view·projection; PS outputs flat per-face palette colour (faithful default). The opt-in upgrade adds a lit variant (per-vertex normal · directional light, optional specular). |

### Why this shape
- **Faithful by construction:** flat per-face palette colour + a projection derived
  from `ViewMetrics` reproduce the current look; the z-buffer replaces the
  painter's sort with the same back-to-front result for convex hulls.
- **Opt-in upgrades are a shader variant, not a new pipeline:** the 3D vertex
  format carries normals from day one, so turning on lighting is a second PS (and
  a light CB), reusing the same meshes and `Scene3D` plumbing.
- **Self-contained new surface area:** `Scene3D` is genuinely new (the old 3D
  renderer is gone), but it is one cohesive module reusing `GraphicsCore`'s device
  + depth buffer, the palette mapping, and the shader build system — a real
  abstraction, consistent with the repo's Native-First rule. It does **not** touch
  `Render2D` (whose strict-2D contract stays clean).

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
- **[updated 2026-06-30]** Handedness is now a free choice (the old `Colored3D`
  RH perspective is gone — `Scene3D` builds its own matrix). Legacy looks down
  **+z**, so build a projection that keeps +z forward and applies the y-flip, e.g.
  a left-handed perspective or an explicit off-centre frustum from `ViewMetrics`.
  Get the cull winding to match legacy front-facing once, in `Scene3D`.
- **Action:** add a `Scene3D` helper that derives the projection **directly
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

### Phase 0 — Seam + headless scaffolding (no visual change) ✅ DONE
- Add `CommandType::DrawModel` + `RenderQueue::DrawModel(...)` and
  `RenderSink::DrawModel`; `NullRenderSink` no-ops it (keeps server/BotClient
  building). Extend `RenderQueue::Replay`.
- Add a `SceneRenderSink` (client) that forwards `DrawModel` to a stub
  `Scene3D`.
- **Unit tests (headless, run in Linux CI too):** `DrawModel` round-trips through
  `RenderQueue` record/replay; `NullRenderSink` ignores it; the projection helper
  matches `ProjectPoint` for a point grid.
- **Deliverable:** nothing renders differently yet; seam + tests in place.

### Phase 1 — `Scene3D` skeleton + `MeshLibrary` (infrastructure) ✅ DONE [updated 2026-06-30]
- Stand up the **`Scene3D`** renderer (sibling to `Render2D`): device resources
  from `GraphicsCore`, a 3D input layout (pos3 + normal + RGBA8), a dynamic VB/IB,
  the perspective+model/view constant buffers, depth-stencil (test+write,
  `LESS_EQUAL`) and backface-cull states, and the new `scene3dVS/PS` shaders.
- `MeshLibrary`: at startup, convert each `ship_solids[type]` into an interleaved
  3D-vertex VB + index buffer (fan-triangulate each `ship_face`'s `p1..pN` exactly
  as `gfx_polygon` does, `gfx2d.cpp`; per-vertex colour = `col_rgba(face.colour)`;
  per-vertex normal = the face normal for flat shading). Cache immutable buffers
  keyed by ship type.
- **Depth is already available** — `GraphicsCore` allocates a `D32_FLOAT`
  depth-stencil bound with the back buffer (no canvas anymore). `Scene3D` just
  binds + clears it. Nail down render order in `game_render_scene()`: clear colour
  + depth once, draw 3D (depth on), then the 2D HUD batch (depth off) — and make
  sure `gfx2d_flush`'s own back-buffer clear does not wipe the scene (move/skip
  that clear when a 3D pass ran this frame).
- **Tests:** mesh builder is pure data → unit-test triangle/index counts and
  winding against `ship_solids` for a few ships headlessly.
- **Deliverable:** `Scene3D` + buffers + depth wired; still drawing nothing.

### Phase 2 — Ships on the GPU (the keystone) ✅ DONE
- `Scene3D::DrawModel` for ships: set the `ViewMetrics`-derived perspective
  projection; depth test+write ON, `LESS_EQUAL`; backface cull ON (winding
  chosen to match legacy front-facing); the **flat** `scene3dPS` (per-face palette
  colour, no lighting). Upload the per-object model matrix (`rotmat` + `location`)
  to the per-draw constant buffer; bind the cached mesh; draw indexed.
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

### Phase 3 — Planet & sun on the GPU ✅ DONE
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

### Phase 4 — Retire dead CPU 3D code & the painter's chain ✅ DONE
- Remove the now-unused `gfx_render_polygon`/`gfx_finish_render` **painter's
  linked list** (`g_poly_chain`, `gfx2d.cpp:275-548`) and the
  `RenderPolygon`/`RenderLine` 2D-projection paths for the scene, plus the
  obsolete `project_to_screen` ship/planet/sun bodies in `threed.cpp`.
- Keep `ViewMetrics`/`ProjectPoint` (still the spec the GPU projection is tested
  against, and used by any remaining 2D-projected markers like the missile
  reticle).
- **Deliverable:** one 3D path (GPU); `threed.cpp` shrinks to scene-submission
  glue.

### Phase 5 — Opt-in visual upgrades (behind a flag) ✅ DONE
- Add a client setting (config/options) `scene_shading = flat | lit`. `lit`
  selects a **lit `scene3dPS` variant** (directional light · the per-vertex
  normals already in the meshes), supplied by a small light constant buffer;
  optionally smooth normals (average face normals at shared vertices) for rounded
  hulls, and add a specular term.
- Optional: distance fog in the PS; MSAA on the back buffer / a multisampled
  scene pass for clean edges.
- **Deliverable:** modern shading is a toggle; faithful flat look remains the
  default.

---

## 5. Risks & mitigations
- **Projection mismatch vs legacy** — the faithful look hinges on the GPU
  projection equalling `ProjectPoint`. *Mitigation:* derive the matrix from
  `ViewMetrics` and **unit-test it against `ProjectPoint`** over a point grid
  before any Windows run (catchable in Linux CI).
- **Handedness / winding** — `Scene3D` builds its own matrix; legacy is +z.
  Getting cull direction or z sign wrong shows as inside-out or invisible ships.
  *Mitigation:* pin the convention in the projection helper; verify with the
  cull-toggle and a known ship (Cobra) on Windows.
- **Z-fighting on co-planar faces** (the owner accepted minor ordering
  differences vs painter's). *Mitigation:* sufficient depth precision (near plane
  not too small), optional small depth bias; the legacy `zavg` max-Z key was
  itself approximate, so the z-buffer is generally *more* correct.
- **[updated 2026-06-30] Render-order / clear hazard** — the scene draws directly
  to the back buffer, which `gfx2d_flush` clears each frame. *Mitigation:* drive
  the 3D pass before the 2D HUD in `game_render_scene()` and make the back-buffer
  clear happen once (in the 3D pass), not again in `gfx2d_flush`; confirm the HUD
  still floats correctly over the scene.
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
| `NeuronClient/graphics/Scene3D.{h,cpp}` | **New.** 3D renderer sibling to `Render2D`: 3D vertex format, perspective/model CBs, depth-stencil + cull states, dynamic VB/IB, per-frame 3D pass. May host `MeshLibrary`. |
| `NeuronClient/graphics/MeshLibrary.{h,cpp}` | **New.** Build/cache GPU meshes from `ship_solids` + generated sphere. |
| `NeuronClient/graphics/Scene3D` projection helper | **New + unit-tested** against `ViewMetrics::ProjectPoint`. |
| `NeuronClient/platform/GfxRenderSink.*` / new `SceneRenderSink` | Forward `DrawModel` to `Scene3D`. |
| `DeepspaceOutpost/main.cpp` (`game_render_scene`) | Drive the `Scene3D` pass before the 2D HUD flush; own the single colour+depth clear. |
| `DeepspaceOutpost/threed.cpp` | `draw_solid_ship`/`draw_wireframe_ship`/`draw_planet`/`draw_sun` emit `DrawModel`; retire CPU projection/raster bodies (Phase 4). |
| `NeuronClient/platform/gfx2d.cpp` | Stop clearing the back buffer when a 3D pass ran (Phase 1); retire `g_poly_chain` painter's chain + `gfx_render_polygon/line/finish` once unused (Phase 4). |
| `NeuronClient/graphics/GraphicsCore` | Reuse the existing `D32_FLOAT` depth buffer; confirm resize rebuilds it (already wired). |
| `NeuronClient/shaders/` | **New** `scene3dVS.hlsl` + `scene3dPS.hlsl` (flat default; lit variant for the opt-in upgrade). `fxc` → committed `CompiledShaders/*.h`, same path as `render2dVS/PS`. |
| `Tests/NeuronClient/` | New tests: projection equivalence, `DrawModel` seam, mesh builder. |
| `docs/` | Cross-link this plan from `MIGRATION_ROADMAP.md` / `gui-graphicscore-status.md`. |

## 8. Suggested first step
Land **Phase 0 + Phase 1** together: they add the `DrawModel` seam, the
`MeshLibrary`, the projection helper, and their headless tests **without changing
a single on-screen pixel** — so they are fully verifiable in Linux CI before the
first Windows-only visual phase (Phase 2) flips ships onto the GPU.
