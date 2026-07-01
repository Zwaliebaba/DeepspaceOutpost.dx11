# Client render-loop simplification plan

**Goal.** Collapse the client's per-frame work into three explicit hooks and make the
2D layer a single bracketed pass:

```
ClientEngine::Frame(dt):
    Update(dt)        // all client logic, paced by delta time
    RenderScene()     // the 3D scene (Scene3D) → back buffer + depth
    RenderCanvas()    // Canvas::Start();  all 2D (HUD + menus + GUI);  Canvas::End();
    Present()
```

This removes the deferred-batch "render-sync" machinery: the mid-batch 3D **scene
marker**, the **idle-frame present gate**, the client-side **RenderQueue round-trip**,
and the **second** 2D pass the GUI overlay runs today.

Scope is the **client** (`NeuronClient` + `DeepspaceOutpost`) only. The headless
server/bot/test path (`NullRenderSink`) is preserved — see §6.

---

## 1. Where we are today

The lifecycle hooks already exist but are thin:

- `Neuron::GameMain::Update/RenderScene/RenderCanvas` are stubs (`NeuronClient/GameMain.h:26-28`).
- `GameApp` wires them to the legacy entry points (`DeepspaceOutpost/GameApp.h`):
  `Update → game_update()`, `RenderScene → game_render_scene()`,
  `RenderCanvas → GuiOverlay::Render(...)`.
- `ClientEngine::Frame` calls them in order, then flushes + presents
  (`NeuronClient/ClientEngine.cpp:187-225`).

The catch: **`RenderScene()` does not render the 3D scene.** It records draw commands
into a deferred 2D batch, and the real GPU work happens later inside `gfx2d_flush`:

1. `game_render_scene()` (`DeepspaceOutpost/main.cpp:1523`) emits, in submission order:
   - **2D background** — `gfx_clear_display()` + `update_starfield()` (2D points).
   - **3D ships/planets/sun** — via `ActiveRenderQueue().DrawModel(...)`
     (`DeepspaceOutpost/threed.cpp:182,263,282`), bracketed by `StartRender()/FinishRender()`
     (`DeepspaceOutpost/space.cpp:533,632,661,757`).
   - **2D HUD** — gauges, text, scanner.
2. The `RenderQueue` is replayed **within the same frame** by `FlushRenderQueue()`
   (`DeepspaceOutpost/space.cpp:637,758`) into `GfxRenderSink`, which forwards to `gfx_*`
   (`NeuronClient/platform/GfxRenderSink.cpp`). Ship `DrawModel`s land in the `gfx2d`
   model arena via `gfx2d_submit_model`; `FinishRender` drops a `Kind::Scene` **marker**
   into the 2D command list (`NeuronClient/platform/gfx2d.cpp:554-572`).
3. `gfx2d_flush` (`NeuronClient/platform/gfx2d.cpp:577`) replays the batch through
   `Render2D`. When it hits the `Kind::Scene` marker it **ends the 2D batch, runs
   `Scene3D::RenderModels`, and re-opens a 2D batch** for the HUD
   (`gfx2d.cpp:639-653`). This mid-batch split is what preserves the layer order
   *background-2D → 3D → HUD-2D*.
4. `RenderCanvas()` → `GuiOverlay::Render()` opens a **separate** full-window
   `Render2D::Begin/End` pass for GUI windows (`NeuronClient/gui/GuiOverlay.cpp:175-177`).
5. `Core::Present()` (`ClientEngine.cpp:219`).

### The "render-sync" complexity to remove

- **Mid-batch scene marker** + `g_models` / `g_models_marked` bookkeeping
  (`gfx2d.cpp:75-80,554-572,639-653`) — the 3D pass is spliced into the 2D batch.
- **Idle-frame present gate** — `gfx2d_flush(bool forcePresent)` returns `false` on an
  empty batch, and `Frame` only presents when `painted` (`ClientEngine.cpp:210-220`,
  `gfx2d.cpp:577-593`). Couples "did we draw 2D" to "do we present".
- **Client-side `RenderQueue` round-trip** — the client records into a queue only to
  immediately replay it into itself the same frame. The deferral exists for the
  *headless* sim; on the client it is pure indirection (`RenderContext.cpp`).
- **Two 2D passes** — the letterboxed game batch and the full-window overlay open
  `Render2D::Begin/End` independently.
- **Re-entrancy guard** — `s_inLifecycle` protects against nested `gfx_update_screen()`
  calls from blocking sequences (break pattern, mission briefs) (`ClientEngine.cpp:189-205`).

---

## 2. Target design

```
ClientEngine::Frame(dt):
    if (!nested):
        m_main->Update(dt)         // logic/input/net/sound
        m_main->RenderScene()      // clear + starfield background + Scene3D ships
        m_main->RenderCanvas()     // Canvas::Start(); 2D HUD + menus + GUI; Canvas::End();
    Core::Present()                // always; device-lost recovery already lives here
    platform_pump_messages(); pace(dt);
```

**Layer order is fixed by function order**, not by an in-band marker:

| Layer | Owner | Contents |
|---|---|---|
| under | `RenderScene()` | back-buffer clear, starfield background, 3D ships/planets/sun (depth-tested) |
| over  | `RenderCanvas()` | HUD gauges/text/scanner, charts, docked screens, GUI overlay windows |

**`Canvas::Start()` / `Canvas::End()`** become the single 2D-pass bracket
(one `Render2D::Begin`/`End`). Everything 2D — the game HUD replayed from the `gfx2d`
batch *and* the GUI overlay windows — submits between them, in submission order, so the
existing compositing is preserved with one pass instead of two.

> Naming note: `Canvas` today is the GUI window manager (the `Ecl*` API,
> `NeuronClient/gui/Canvas.cpp`). Adding `Start()/End()` there fits the user's phrasing,
> but consider a distinct type (e.g. `Canvas2D` / `Overlay2D`) if we want to keep the
> window-manager and the 2D-pass concerns separate. Decision for the user (§7).

---

## 3. The one hard problem: the starfield background

Today the starfield is 2D drawn **before** the 3D ships, and the HUD 2D is drawn
**after** — that is the whole reason the scene pass is spliced into the middle of the 2D
batch. If we naively do "all 3D in `RenderScene`, all 2D in `RenderCanvas`", the starfield
(2D) would paint **over** the ships. So the split only works if the background moves under
the 3D.

**Recommended:** `RenderScene()` owns everything beneath the HUD — the black clear, the
starfield, then the `Scene3D` ship/billboard pass. Concretely:

- Move `gfx_clear_display()` + `update_starfield()` out of the 2D-HUD flow and into the
  scene step (they run first, as the scene background).
- Then run the 3D `Scene3D::RenderModels` pass with depth.
- `RenderCanvas()` then draws only the over-layer (HUD + menus + GUI).

The starfield can stay a 2D point cloud drawn just before the 3D pass (a tiny `Render2D`
background sub-batch inside `RenderScene`), or later be promoted to actual 3D points. Either
way the mid-batch marker disappears.

**This reordering is the primary behaviour risk** and must be verified visually on the
flight views (front/rear/left/right), witchspace, and the game-over animation, which all
draw starfield-under-ships-under-HUD.

Screens with **no** 3D (galactic/short-range charts, docked/trade/market, intro) are
unaffected: they only ever emit 2D, so they collapse cleanly into `RenderCanvas()`.

---

## 4. Incremental migration (each step ships & is verifiable on its own)

Ordered to keep every intermediate state working and frame-output-identical until the
final simplification.

**Step 0 — Real delta time (isolated, low risk).**
Compute `dt` from the QPC timestamps already in `Frame` (`ClientEngine.cpp:231-250`) and
pass it to `Update(dt)` instead of the fixed `capMs/1000`. Keep the game logic fixed-step
for now (see §7); this just makes `dt` truthful. No render change.

**Step 1 — Introduce `Canvas::Start()/End()` as a pure rename of the current bracket.**
Wrap the `Render2D::Begin(...)`/`Render2D::End()` inside `gfx2d_flush` in
`Canvas::Start()/End()` (letterbox params computed as today). Behaviour identical; this
just gives us the seam. Verify: pixel-identical frames.

**Step 2 — Fold the GUI overlay into the single 2D pass.**
Have `RenderCanvas()` open `Canvas::Start()`, replay the game HUD batch, draw
`Canvas::Render()` (windows), then `Canvas::End()` — one `Render2D` pass. Remove the
overlay's private `Begin/End` (`GuiOverlay.cpp:175-177`). Watch the coordinate-space
difference: the HUD is virtual/letterboxed, the GUI is client-pixel full-window (§7).
Verify: HUD unchanged; F8/F11 overlay composites as before.

**Step 3 — Lift the 3D pass out of the 2D batch into `RenderScene()`.**
Move the `Scene3D::RenderModels` call from the `Kind::Scene` marker path
(`gfx2d.cpp:639-653`) into `RenderScene()`, running after the background (§3). Delete the
marker, `g_models_marked`, and the batch-split logic. `gfx2d_flush` becomes a straight 2D
replay. Verify: ships/planets/sun render with correct depth and HUD-over-scene order.

**Step 4 — Retire the idle-frame present gate.**
With scene + 2D as explicit steps, drop `gfx2d_flush`'s `forcePresent`/return-bool and the
`painted` guard; `Frame` always clears, renders, and presents. Confirm the intended
"persist last frame when idle" cases are either gone (we always draw) or handled by not
running the hooks. Verify: no flicker on menu-idle frames.

**Step 5 — Short-circuit the client `RenderQueue` round-trip (optional, larger).**
On the client, let `RenderScene()` consume the recorded `ModelDraw`s directly and call
`Scene3D::RenderModels`, instead of `DrawModel → FlushRenderQueue → GfxRenderSink →
gfx2d_submit_model`. Keep `RenderQueue`/`NullRenderSink` for headless (§6). This is the
biggest structural change and should land last, behind the others.

**Step 6 — Delete the now-dead machinery.**
`Kind::Scene`, `gfx2d_submit_model`, `gfx_start_render/gfx_finish_render`,
`forcePresent`, and — if nesting is reworked — `s_inLifecycle`. Sweep for references
before each removal (same discipline as the RenderSink cleanup).

---

## 5. Behaviour-preservation checklist

- **Layer order** (§3): starfield → ships → HUD on all flight views + game-over.
- **Letterboxing**: retro screens stay 512×514 centred with integer scale
  (`gfx2d.cpp:599-606`); full-window flight stays scale 1.
- **Nested blocking sequences**: break pattern (`SCR_BREAK_PATTERN`) and mission briefs
  call `gfx_update_screen()` recursively; the new `Frame` must still pump + present the
  inner sequence without re-running the outer `Update/RenderScene` (keep or replace the
  `s_inLifecycle` guard).
- **Charts**: per-frame clear-and-redraw crosshair still works (the XOR path is already
  retired).
- **Overlay auto-hide + input suppression**: `GuiOverlay::Update()` still runs each frame
  before the 2D pass.
- **Device-lost**: presentation stays on `Core::Present()` (already unified).

---

## 6. Server / headless — explicitly untouched

The `RenderQueue` + `RenderSink` seam stays so the dedicated server, bot client, and
golden-run tests keep recording into a queue and replaying into `NullRenderSink`
(`RenderQueue.h`). Step 5 only changes **which** sink the *client* uses (direct Scene3D
vs the gfx round-trip); it does not remove the queue or the null path. `RenderQueueTests`
continue to exercise record/replay.

---

## 7. Decisions needed before implementing

1. **`Canvas::Start/End` home** — add to the existing `Canvas` (window manager) per the
   request, or a new 2D-pass type to keep concerns separated? (Recommend a small new type;
   `Canvas` is already large.)
2. **HUD vs GUI coordinate space** — the game HUD is authored in the letterboxed virtual
   space; the GUI overlay is client-pixel full-window. Unify into one space, or let
   `Canvas::Start(mode)` take the target mapping and run the HUD and GUI as two sub-batches
   inside one `Begin/End`? (Recommend the latter first; unify later.)
3. **Real dt vs fixed-step logic** — the legacy game is fixed-timestep (`mcount` counters,
   frame-based cooldowns). Do we (a) pass real `dt` but keep logic fixed-step for now, or
   (b) start converting logic to dt-based? (Recommend (a); (b) is a separate track.)
4. **Idle persistence** — is "don't present idle frames" a feature we must keep, or can we
   always present (simpler)? Affects Step 4.

---

## 8. Do NOT touch / out of scope

- The `Scene3D` GPU pipeline, `Render2D` batcher, and constant-buffer layouts — unchanged;
  this is a control-flow refactor, not a renderer rewrite.
- `Core` device/present/device-lost — already unified in the prior pass.
- The `RenderQueue`/`RenderSink` contract and its headless path (§6).
- Frame pacing / message pump (`ClientEngine.cpp:227-250`) — keep as-is aside from feeding
  `dt` to `Update`.
