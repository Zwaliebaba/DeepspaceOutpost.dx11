# Client render-loop simplification plan

**End goal.** Collapse the client's per-frame work into three explicit hooks with a
single bracketed 2D pass:

```
ClientEngine::Frame(dt):
    Update(dt)        // all client logic
    RenderScene()     // the 3D scene (Scene3D) → back buffer + depth
    RenderCanvas()    // Canvas::Start();  all 2D (HUD + menus + GUI);  Canvas::End();
    Present()
```

**Decided sequencing (this doc):**

1. **Phase 1 — Unify the 2D coordinate space to client pixels (remove the letterbox /
   virtual 512×514 canvas).** This is the prerequisite: it removes the dual-coordinate-space
   coupling that would otherwise make the single `Canvas::Start/End` pass awkward.
2. **Phase 2 — The render-loop refactor** (3 hooks + `Canvas::Start/End`), which becomes
   straightforward once there is one coordinate space.
3. **Delta-time — deferred.** The fixed timestep stays for now; revisit as its own track.

Scope is the **client** (`NeuronClient` + `DeepspaceOutpost`). The headless
server/bot/test path (`NullRenderSink`) is preserved throughout — see §5.

> ⚠️ Phase 1 is the **largest** piece of this effort and, unlike Phase 2, it
> **deliberately changes frame output** (the black letterbox bars go away and fixed
> layouts adapt to the window). Phase 2 is behaviour-preserving. Keep them in separate
> commits/PRs.

---

## Decisions (locked)

| # | Decision | Choice |
|---|---|---|
| D1 | Aspect / responsive policy (Phase 1) | **Anchored HUD + centred fixed-size panels.** HUD furniture anchors to the client-rect edges/corners; menus, charts, station and dialogs stay fixed-size and centred — the model the GUI overlay windows already use. No per-screen reflow. |
| D2 | `Canvas::Start/End` home (Phase 2) | **Extend the existing `Canvas` class** with `Start()/End()` (the GUI window manager gains the 2D-pass bracket). |
| D3 | Skybox (star migration) | **Procedural in-shader** — gradient + procedurally-placed stars, no texture assets; density/colour via shader constants. Structured so a cubemap can drop in later. |
| D4 | Dust speed cue (star migration) | **Density + parallax** — fixed-size points; nearer points parallax faster and spawn density/rate scales with speed. |
| D5 | Idle-frame present (Phase 2) | **Always present.** The three hooks always render the current screen, so there is no empty frame to skip; drop the `forcePresent`/`painted` idle gate. |
| D6 | Nested blocking sequences (Phase 2) | **Keep the `s_inLifecycle` guard** (break pattern / mission briefs) for now; revisit only if it obstructs the refactor. |
| — | Delta-time | **Deferred** to its own track (§3). |

---

## Phase 1 — Unify the 2D coordinate space to client pixels

### 1.1 Where we are

Three coordinate regimes coexist today:

- **Virtual 512×514 letterboxed** — menus, charts, station/docked, and the retro HUD.
  Authored in a fixed `Renderer::kCanvasWidth×kCanvasHeight` space and scaled onto the back
  buffer with integer scale + black bars (`gfx2d.cpp:599-606`, `Render2D::Begin`'s
  `dstX/dstY/dstScale`, `SetClip`'s scale).
- **Full-window flight** — `g_scene_full` makes the canvas the client area (scale 1, no
  bars); the HUD is "floated" by adding `(g_origin_x,g_origin_y)` to emitted coordinates
  (`gfx2d.cpp:84-98`). This path is **already effectively client-space**, via
  `gfx_set_draw_origin` / `gfx_hud_anchor`.
- **GUI overlay** — client-pixel full-window already (`GuiOverlay::Render`,
  `Canvas.cpp` uses `Core::GetOutputSize()`).

So one of the three is fixed-virtual and two are already client-space. Phase 1 makes the
first match the other two.

### 1.2 Migration surface (measured)

~200 fixed-coordinate 2D call-sites authored against 512×514:
`docked.cpp` (~90), `main.cpp` (~34), `missions.cpp` (~31), `space.cpp` (~27),
`intro.cpp` (~8), plus the HUD. 66 letterbox/virtual-canvas coupling points in
`NeuronClient`. This is a screen-by-screen content migration, not a mechanical rename —
plan it as a series of shippable steps, not a big bang.

### 1.3 Strategy

Author 2D directly in **client pixels** with an **anchor model** for the fixed furniture
(D1): the HUD anchors to the client-rect edges/corners, and menus/charts/station/dialogs
are **fixed-size panels centred** in the window — the same model the GUI overlay windows
already use (`Canvas.cpp` centres via `Core::GetOutputSize()`). Reuse the existing
`gfx_set_draw_origin` / `gfx_hud_anchor` mechanism as the anchoring foundation rather than
inventing a new one. No per-screen responsive reflow is needed.

**Transitional shim (keeps the game playable during migration).** Before touching any
screen, replace the *letterbox* mapping with an aspect-preserving *scale-to-fit* mapping in
one place (`gfx2d_flush` + `Render2D::Begin`), so un-migrated fixed-512×514 screens still
render sensibly while each is moved to anchored client-space one at a time. The shim is
deleted when the last screen is migrated.

### 1.4 Steps

1. **Single source of canvas placement. [DONE]** Centralise the virtual→client mapping into
   one `canvasPlacement()` helper (vw/vh/offset/scale) that both the 2D replay and the 3D
   scene pass consume. Per D1 the retro 512×514 canvas is placed **native size, centred**
   (scale 1, black margins; downscale only if the window is smaller than the canvas — never
   upscale, so pixel art stays crisp). The full-window scene/HUD authors at the client size,
   so it fills the window. The 512×514 authoring space is kept for the fixed-2D screens
   (making `canvasW/H` return the client size is the *end* state, step 5). Input is
   unaffected (retro screens are keyboard-driven; the GUI overlay is already client-space).
   *(Started as a scale-to-fit shim; revised to native-centred once D1 was fixed.)*
2. **Anchor model. [DONE]** Added `gfx_anchor(where, w, h, dx, dy, &ox, &oy)` (9-point
   `gfx_anchor_point`: corners/edges/centre + a canvas-pixel nudge) that computes the draw
   origin placing a `w×h` block within the current canvas rect, on top of the existing
   `gfx_set_draw_origin`/`g_origin` translation. `gfx_hud_anchor` is now the bottom-centre
   512×514 case of it (behaviour-preserving, verified). Screens migrate by anchoring their
   fixed-size panels/HUD with this instead of hard-coded 512×514 coordinates. No screen
   changed yet — this is the mechanism Step 3 uses.
3. **Migrate screens in groups**, each its own commit, each verified:
   intro → charts (galactic/short-range) → docked/station/trade/market → missions →
   flight HUD/overlays. Convert fixed 512×514 coords to anchored client-space (HUD anchored
   to edges; menus/panels fixed-size centred, per D1).
   - Enablers added with the first group: `gfx_canvas_size(&w,&h)` (screens read the live
     canvas rect) and a fix to `gfx_draw_sprite`'s `x==-1` auto-centre (was hard-coded 512,
     now `canvasW()` — correct in client-space, identical in retro).
   - **intro [DONE]** — `update_intro1/2` now opt into full-window (`gfx_set_scene_fullwindow`,
     FOV-preserving so the hero ship keeps its apparent size), centre the title sprite on the
     window, and anchor the prompts to the bottom edge. Needs an eyeball for exact framing.
   - **fixed-2D screens (charts / docked / station / market / missions) [DONE globally, no
     per-screen edits]** — per D1 these stay authored in 512×514 and are placed native-size
     centred by `canvasPlacement()` (step 1). Nothing to migrate per screen; they render 1:1
     centred with black margins. Only *scenes* (which should fill the window) and per-gauge
     HUD anchoring need active work.
   - **scenes still on the retro path** — the game-over screen (`game_render_scene`, GameState
     GameOver: starfield + ships + "GAME OVER") is a scene, so like the intro it should opt
     into full-window (`gfx_set_scene_fullwindow`) rather than render native-centred. Small
     follow-up, same pattern as the intro.
   - **flight HUD** — already floats as a 512×514 block bottom-centred (`gfx_hud_anchor`).
     Breaking it into individually edge/corner-anchored gauges (finer D1) is an optional
     later refinement, not required for correctness.
   - **Migration gotcha (learned on the intro):** the 2D scissor (`g_scissor`) defaults to the
     retro 512×514 rect and is not reset per frame, so a screen that opts into **full-window**
     (scenes) **must** also set the full-canvas clip (`gfx_set_scene_clip()` after
     `gfx_set_scene_fullwindow(1)`) or its client-space 2D is silently clipped away (the 3D
     still shows — it uses its own viewport). Fixed-2D screens stay retro, so their default
     512×514 scissor matches their authoring and they are unaffected.
4. **Migrate input hit-testing** to client space in lockstep with each screen: mouse
   mapping, and the chart crosshair (`cross_x/cross_y`) which is authored in virtual space.
5. **Remove the letterbox machinery.** Delete `dstX/dstY/dstScale` from `Render2D::Begin`
   and `gfx2d_flush`, the `SetClip` scale, `g_origin` float, `g_scene_full` branch, and
   `Renderer::kCanvasWidth/Height`. `Render2D::Begin` collapses to a client-space ortho.
6. **Delete the shim.** One coordinate space remains: client pixels.

### 1.5 Phase-1 risks

- **Deliberate visual change** on every retro screen — needs a screenshot pass per screen
  group (intro, both charts, docked/market/trade, missions, HUD, flight views).
- **Aspect ratio** — resolved (D1): HUD anchors, panels stay fixed-size and centred, so no
  stretch/distortion of pixel art; wider/taller windows just show more space around the
  centred panels. The transitional shim preserves aspect.
- **Input parity** — mouse/crosshair hit-testing must move to client space together with
  each screen or clicks land wrong.
- **Non-16:9 / resize** — client-space layouts must tolerate arbitrary window sizes
  (`OnResize` already rebuilds the swap chain).

---

## Phase 2 — The render-loop refactor

Once there is one coordinate space, the loop refactor from the original plan applies, now
simpler (no letterbox params in the 2D bracket).

### 2.1 Target

```
ClientEngine::Frame(dt):
    if (!nested):                  // D6: s_inLifecycle guard kept
        m_main->Update(dt)         // logic/input/net/sound (fixed-step for now)
        m_main->RenderScene()      // skybox + dust + Scene3D ships (depth)
        m_main->RenderCanvas()     // Canvas::Start(); 2D HUD + menus + GUI; Canvas::End();
    Core::Present()                // D5: always present
    platform_pump_messages(); pace();
```

Layer order is fixed by **function order**, replacing the in-band scene marker:

| Layer | Owner | Contents |
|---|---|---|
| under | `RenderScene()` | procedural skybox, dust particles, 3D ships/planets/sun (all depth-tested) |
| over  | `RenderCanvas()` | HUD, charts, docked screens, GUI overlay windows |

`Canvas::Start()/End()` is the single 2D-pass bracket (one `Render2D::Begin/End`, now pure
client-space). Both the game HUD batch and the GUI windows submit between them, in
submission order.

### 2.2 The one hard problem: the starfield background

Today the 3D pass is spliced into the *middle* of the 2D batch (`gfx2d.cpp:639-653`)
because the **starfield is 2D drawn under the ships** while the **HUD is 2D drawn over
them**. A naive "all 3D in `RenderScene`, all 2D in `RenderCanvas`" would paint the
starfield over the ships. This mid-batch splice is the reason the clean 3-hook split is
non-trivial.

**The clean fix is the star migration (§2.3), not a 2D workaround.** Once the starfield
becomes a 3D skybox + depth-tested dust, the background moves *into* the scene pass and
this problem disappears: `RenderScene()` = skybox → dust → ships (one 3D pass, ordered by
depth), `RenderCanvas()` = pure 2D over-layer. The `Kind::Scene` marker and the
`g_models_marked` bookkeeping are then deleted outright.

**Do not build throwaway scaffolding.** Because the star migration is planned (§2.3), do
*not* invest in a "move the 2D starfield into a RenderScene 2D sub-batch" step — that would
be deleted the moment the skybox lands. Sequence the star migration **before** the
RenderScene/RenderCanvas split so the split is built once, in its final shape. If for
scheduling reasons the split must land first, keep the existing marker as-is until §2.3
retires it — don't half-migrate the 2D starfield.

**Primary Phase-2 behaviour risk** — verify skybox → dust → ships → HUD ordering on all
flight views + witchspace + game-over.

### 2.3 Star migration (skybox + 3D dust) — its own PR, sequenced here

Replace the 2D `update_starfield` point cloud (`DeepspaceOutpost/stars.cpp`) with:
- a **procedural in-shader skybox** (D3) — gradient + procedurally-placed stars from a
  seed, no texture assets, density/colour via constants; drawn first at far depth and
  structured so a cubemap can drop in later.
- small **3D "dust" particles** (depth-tested, camera-relative) whose speed cue is
  **density + parallax** (D4): nearer points parallax faster, and spawn density/rate scales
  with speed.

This is net-new rendering scope, so treat it as its **own change / PR**, not part of the
loop-refactor commits:

- It is the **one sanctioned extension to `Scene3D`** in this effort (new shader + a small
  particle buffer + a skybox draw), which §7 otherwise freezes.
- It is **independent of Phase 1** (3D, not 2D client-space), so it can be prototyped in
  parallel.
- It reuses the existing perspective/projection + depth state already in `Scene3D`
  (`NeuronClient/graphics/Scene3D.cpp`); no new pipeline concepts.
- On landing, retire `stars.cpp`'s 2D path and the 2D-background dependency in
  `game_render_flight` / game-over.

**Why here and not "later":** it is on the critical path of Phase 2's layering split (§2.2).
Landing it first makes `RenderScene()` own the whole under-layer cleanly; deferring it
past the split forces throwaway 2D-background scaffolding.

### 2.4 Steps (each behaviour-preserving)

1. `Canvas::Start()/End()` as a pure rename of the current `gfx2d_flush` `Begin/End`
   bracket (now client-space, no letterbox params).
2. Fold the GUI overlay into that one pass — remove `GuiOverlay::Render`'s private
   `Begin/End` (`GuiOverlay.cpp:175-177`). One coordinate space makes this trivial now.
3. Lift the `Scene3D::RenderModels` call out of the `Kind::Scene` marker into
   `RenderScene()` (after the background, §2.2); delete the marker + batch-split.
4. Retire the idle-frame present gate (D5) — drop `gfx2d_flush`'s `forcePresent`/return-bool
   and the `painted` guard (`ClientEngine.cpp:210-220`); always render + present.
5. **(Optional, larger)** Short-circuit the client `RenderQueue` round-trip: have
   `RenderScene()` consume recorded `ModelDraw`s directly for `Scene3D`, instead of
   `DrawModel → FlushRenderQueue → GfxRenderSink → gfx2d_submit_model`. Keep the queue for
   headless (§5).
6. Delete dead machinery: `Kind::Scene`, `gfx2d_submit_model`,
   `gfx_start_render/gfx_finish_render`, `forcePresent`, and — if nesting is reworked —
   `s_inLifecycle`. Sweep references before each removal.

### 2.5 Phase-2 preservation checklist

- Layer order (§2.2): skybox → dust → ships → HUD on all flight views + game-over.
- Nested blocking sequences (break pattern `SCR_BREAK_PATTERN`, mission briefs) call
  `gfx_update_screen()` recursively; the new `Frame` must still pump + present the inner
  sequence without re-running the outer `Update/RenderScene` — the `s_inLifecycle` guard is
  kept for this (D6).
- Overlay auto-hide + input suppression still run each frame before the 2D pass.
- Device-lost stays on `Core::Present()` (already unified).

---

## 3. Delta-time (deferred)

Left as a separate future track. When taken up: the cheap part is plumbing real `dt` (from
the QPC timestamps already in `Frame`, `ClientEngine.cpp:231-250`) into `Update`; the
expensive part is converting the fixed-step game logic (`mcount` counters, per-frame
cooldowns, motion) to dt-based, which interacts with the server tick + snapshot
interpolation. Not required by either phase above.

---

## 4. Suggested order of work

1. Phase 1 (client-space migration) — the big one; land it screen-group by screen-group
   behind the scale-to-fill shim so the game stays playable each commit.
2. Star migration (§2.3) — skybox + 3D dust, its own PR; lands **before** the
   RenderScene/RenderCanvas split so that split is built once. Independent of Phase 1, so
   it can start in parallel.
3. Phase 2 steps 1–4 — the behaviour-preserving loop refactor (layering split now trivial).
4. Phase 2 step 5–6 — the `RenderQueue` short-circuit + dead-code deletion.
5. Delta-time — separate track, later.

---

## 5. Server / headless — untouched

The `RenderQueue` + `RenderSink` seam stays so the dedicated server, bot client, and
golden-run tests keep recording into a queue and replaying into `NullRenderSink`
(`RenderQueue.h`). Phase 2 step 5 only changes **which** sink the *client* uses; it does
not remove the queue or the null path. `RenderQueueTests` keep passing.

---

## 6. Open questions

None outstanding — all design decisions are locked in the **Decisions** table above
(D1–D6, plus deferred delta-time). The document is complete enough to start implementation
at Phase 1, Step 1. New questions that surface during implementation should be appended to
the Decisions table with the next Dn id.

---

## 7. Do NOT touch / out of scope

- The `Scene3D` GPU pipeline, `Render2D` batcher internals, and constant-buffer layouts —
  this is control-flow + coordinate-space work, not a renderer rewrite. **Sole exception:**
  the star migration (§2.3) extends `Scene3D` with a skybox draw + a dust-particle program;
  that is a deliberate, isolated feature change, not part of the structural refactor.
- `Core` device/present/device-lost — already unified.
- The `RenderQueue`/`RenderSink` contract and its headless path (§5).
- Frame pacing / message pump (`ClientEngine.cpp:227-250`).
- Delta-time logic conversion (§3) — explicitly deferred.
