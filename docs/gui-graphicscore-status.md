# Client GUI / 2D renderer — status & next steps

Single source of truth for the native Direct3D 11 client rendering stack: the
GraphicsCore + GUI/text import, the retirement of the legacy platform 2D layer, and the
consolidation of all 2D onto one batched renderer. The early step-by-step history
(`phase1`…`phase7`) lived here as separate docs; those phases are complete and several
described systems that have since been retired, so they were removed — the history is in
git.

_Last updated: after moving the whole game onto the engine-driven GameMain lifecycle (a
top-level state machine; the engine owns the frame), restoring the offline fxc /
`CompiledShaders` pipeline for the built-in Render2D shaders, and removing the legacy
gfx_display_* screens that the GUI overlay replaced. The off-screen canvas and
`ImmediateRenderer` remain retired._

## The stack today

The whole client renders through one native D3D11 stack, with a **single 2D layer**
(`Render2D`) shared by the game and the GUI.

- **`Neuron::Graphics::Core`** (`graphics/GraphicsCore`) — owns the D3D11 device, swap
  chain and back-buffer RTV. Brought up by `ClientEngine`.
- **`Neuron::Graphics::Render2D`** (`graphics/Render2D`) — **the** native 2D batched
  renderer. A `Begin(rtv, virtualW, virtualH, dstX, dstY, scale, filter) … End` scope
  sets the pipeline once and batches colored + textured primitives in submission order
  (keyed by topology / texture / scissor / program). One interleaved vertex format and
  one shader serve both (colored prims sample a built-in 1×1 white texture). Features: a
  virtual→target **letterbox transform** (placement + scissor mapping), a runtime
  **shader-program registry** (`RegisterProgram`/`SetProgram`, compiled via `D3DCompile`),
  and a built-in **text-outline** program (with a `b1` params cbuffer). The built-in
  programs (default + text-outline) live in `NeuronClient/shaders/*.hlsl` and are compiled
  offline by **fxc** into `shaders/CompiledShaders/*.h` byte arrays at build time (the
  project's committed-header convention); only caller-supplied custom programs are compiled
  at runtime. Used by the GUI overlay (→ back buffer, 1:1) and the in-game batch
  (→ back buffer, letterboxed).
- **`TextureManager` + `DDSTextureLoader`** — native `.dds` loading with a generated mip
  chain; all game art (sprites, scanner HUD, fonts, interface) loads through here.
- **Text + Strings** (`gui/TextRenderer`, `Strings`) — one bitmap font
  (`Fonts/SpeccyFontENG.dds`) shared by the menus and in-game `gfx_display_*` text; JSON
  localization (`GameData/Strings/<lang>/*.json`). Text gets a **shader-side outline**
  (Render2D's built-in program); tint via `SetColor`; the printf overloads were removed
  (plain strings — no format-string / buffer-overflow hazard).
- **GUI** (`gui/Widget|GuiButton|GuiWindow|Canvas|GuiOverlay`) — the ECL window/menu
  system, full-window + mouse-driven (keyboard nav is a transitional fallback), drawn
  through Render2D. Opening a window suppresses game input; the overlay auto-hides when
  the last window closes.
- **`gfx.h` 2D contract** — `platform/gfx2d.cpp`: a submission-order batch (primitives /
  sprites / text / depth-sorted 3D / scanner / clip) replayed **directly to the back
  buffer** each frame through Render2D, letterboxed by the viewport (integer scale, point
  sampled — pixel-exact retro). The depth-sorted 3D flight wireframe rides along
  (`gfx_polygon` / `gfx_draw_colour_line`).
- **`platform/Renderer`** — now a thin adopter of Core's device/context/swap chain plus
  the master palette (`scanner_palette.h`); it owns no render targets and just `swap()`s.
  (The off-screen 512×514 canvas, the letterboxed-present pipeline, and its own
  back-buffer RTV are gone.)
- **`GfxRenderSink`** replays the `RenderQueue` (the A1 render seam) through the `gfx.h`
  contract — the in-flight 3D wireframe path. Client-only; the queue/sink become a client
  per-frame render context in A4 (see `RenderContext.h`).
- **Engine bootstrap + frame loop** (`ClientEngine`, `GameMain`, `DeepspaceOutpost/GameApp`,
  `NeuronCore/EventManager`) — `ClientEngine` owns the window + Core; `wWinMain` boots it
  → `make_self<GameApp>` → `game_main()`. The **engine owns the frame**:
  `ClientEngine::Frame()` drives the GameMain lifecycle (`Update(dt)` / `RenderScene` /
  `RenderCanvas`) + flush + present, then the OS message pump and frame pacing.
  `game_main()` just boots the first game and pumps frames; the whole game (intro → flight
  → game-over → new game) runs as a **state machine** in `game_update()`/`game_render_scene()`
  (see `DeepspaceOutpost/main.cpp`). Deeply nested blocking sequences (the docking break
  pattern, mission briefs) keep working via a re-entrancy guard in `Frame()` — they only
  present, they don't re-enter the state step.

## Game screens — list/form screens are GUI windows

Reached over the running game via the overlay. `DeepspaceOutpost/GameWindows.{h,cpp}`
holds the game-specific windows, handed to the engine through `GuiOverlay` seams
(`SetOptionsWindowFactory`, `ShowWindow`, `Open`).

| Key | Screen | Window | Notes |
|---|---|---|---|
| F4 (docked) | Equip Ship | `EquipWindow` | buy-list; rebuilds rows on laser sub-menu expand |
| F7 | Data on Planet | `InfoWindow` | single-player local data; **MMO keeps legacy `display_data_on_planet`** |
| F8 | Market Prices | `MarketWindow` | inline Buy/Sell + live cash; local *and* server trades |
| F9 | Commander Status | `InfoWindow` | read-only panel |
| F10 | Inventory | `InfoWindow` | read-only panel |
| F11 | Options → Game Settings / Quit | `OptionsMenuWindow` + `SettingsWindow` + Quit confirm | Settings bound to config globals |

The trade / equip / read-only logic was split into **render-free accessors** in
`docked.cpp` (`market_*`, `equip_*`, `cmdr_status_*` / `inventory_*` / `planet_data_*`) so
the GUI never touches the legacy gfx drawing.

- **Still legacy, by design:** the **charts** (F5 galactic / F6 short-range) — interactive
  spatial maps (crosshair nav, fuel circle), not list/form screens, so a `GuiWindow` is
  the wrong fit.
- **Removed:** Save/Load Commander (boots the compiled-in default commander); the early
  F1 demo main menu.

## Done (was "open"; now retired/shipped)

- **Legacy platform 2D layer** (`Font.cpp`, `Image.cpp`, the bespoke `gfx_dx11`) — gone;
  the master palette is baked into the engine (`scanner_palette.h`).
- **`ImmediateRenderer`** — removed. All 2D goes through `Render2D`.
- **Off-screen canvas + letterboxed present** — removed. The game renders full-window
  straight to the back buffer (letterbox via the viewport); `Renderer` shrank to a thin
  device/palette adopter.
- **Render2D shaders on the offline fxc / `CompiledShaders` convention** — the built-in
  programs moved from inline runtime `D3DCompile` strings into `shaders/*.hlsl`, compiled by
  fxc into committed `CompiledShaders/*.h` byte arrays at build time (custom programs still
  compile at runtime).
- **Shader-side text outline** — replaces the old two-pass offset drop-shadow, for both
  GUI and in-game text. Glyph UVs are **pixel-aligned** (exact texel cells) so the bitmap
  font stays crisp at small sizes under point sampling.
- **Idle-frame present fix** — an empty 2D batch is no longer cleared+presented (FLIP_DISCARD
  keeps no retained content), so menu/station screens that repaint on demand no longer flash
  to black between inputs.
- **Whole game on the GameMain lifecycle** — the nested blocking screen loops in `game_main()`
  became an engine-driven state machine (intro → flight → game-over); the engine owns the
  frame (`ClientEngine::Frame`: lifecycle + present + pump + pace).
- **Chart crosshair as a texture** — the F5/F6 charts redraw every frame (the replicated
  chart builders are idempotent) with the crosshair drawn as a `Textures/Crosshair.dds`
  sprite (`IMG_CROSSHAIR`) on top, replacing the dropped XOR-erase toggle.
- **Legacy gfx_display_* screens removed** — `options.cpp` (Options/Settings/Quit), the
  unreachable `display_market_prices` / `display_inventory` / `equip_ship`, and the whole
  dead-by-guard cluster behind them: the `SCR_MARKET_PRICES` / `SCR_EQUIP_SHIP` / `SCR_QUIT`
  keyboard cases, the `return_pressed` / `y_pressed` / `n_pressed` handlers + `finish_game`,
  the legacy market/equip nav functions (`buy_stock`, `select_*_stock`, `highlight_stock`,
  `buy_equip`, `select_*_equip`, …), and the matching `SCR_*` defines. The render-free GUI
  accessors (`market_*`, `equip_do`/`equip_reset`/`equip_row_text`/`equip_buyable`) are kept;
  `display_data_on_planet` is kept (F7 in MMO mode by design).
- **Review hardening** — `vsnprintf` bounds (+ no `string_view`-as-format), the present
  binds its own opaque/no-depth OM state, `gfx_display_pretty_text` wrap bounded, the GUI
  panel SRV cached, and topology/shader asserts.
- **`dialog_win.cpp`** (dead save/load file picker) — removed.

## Windows verification

Confirmed on Windows: intro screens, the letterboxed menu/station screens (no black-flash),
crisp outlined text, the **charts** (F5/F6, scissor-clipped at scale ≠ 1), in-flight 3D
(planet/ships render steady after the server landmark AOI fix — see `MIGRATION_ROADMAP.md`),
and the **GameMain state-machine arc** (intro → launch → flight → death → game-over →
respawn, incl. the docking break pattern). GUI menus, the Options/Settings/Quit + Market
migrations, and palette colour were confirmed earlier.

Still wants a Windows pass (compile-gated by CI, not yet exercised end-to-end):

- the new **chart crosshair sprite** (sized/centred correctly over F5/F6);
- **mission briefs** (the other nested blocking sequence that rides the `Frame()`
  re-entrancy guard);
- **live window resize** (`WM_SIZE` → Core swap-chain rebuild) — no crash, correct layout
  (note: resizing while parked on a static menu can briefly drop the image — no retained
  canvas; cosmetic).

## Next steps (in order)

1. **Finish the sim/draw split in the flight state** — `game_render_scene`'s flight path
   still runs fused simulation-and-draw steps (the `mcount` bookkeeping, `update_local_objects`,
   `render_replicated_objects`). Separating pure simulation into `game_update` is blocked by
   the `mcount` interleaving; a deliberate pass could untangle it.
2. **A4 render-seam ownership** — give a client per-frame render context the `RenderQueue`
   + `GfxRenderSink`, retiring the `ActiveRenderQueue()` / `g_gfxSink` globals
   (`RenderContext.h`). Tied to the GameLogic split.
3. **Retire keyboard GUI nav** once mouse-driven menus fully cover the screens (then
   simplify `GuiWindow::Update`).
4. **Smaller:** port `InputField` / `InputScroller` value sliders onto Render2D if needed;
   `WM_POINTER` touch is wired but dormant (`EnableMouseInPointer` left off); re-check the
   monospaced font metrics on the charts / dense screens.

> The MMO/server migration is tracked separately in `MIGRATION_ROADMAP.md`; this file
> covers only the client GUI / 2D-renderer stack.
