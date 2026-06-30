# Client GUI / 2D renderer — status & next steps

Single source of truth for the native Direct3D 11 client rendering stack: the
GraphicsCore + GUI/text import, the retirement of the legacy platform 2D layer, and the
consolidation of all 2D onto one batched renderer. The early step-by-step history
(`phase1`…`phase7`) lived here as separate docs; those phases are complete and several
described systems that have since been retired, so they were removed — the history is in
git.

_Last updated: after consolidating all 2D onto `Render2D` and rendering the game
directly to the back buffer (off-screen canvas, `ImmediateRenderer`, and the fxc shader
pipeline all removed)._

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
  **shader-program registry** (`RegisterProgram`/`SetProgram`, compiled via `D3DCompile`
  — no fxc/offline step), and a built-in **text-outline** program (with a `b1` params
  cbuffer). Used by the GUI overlay (→ back buffer, 1:1) and the in-game batch
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
- **Engine bootstrap** (`ClientEngine`, `GameMain`, `DeepspaceOutpost/GameApp`,
  `NeuronCore/EventManager`) — `ClientEngine` owns the window + Core; `wWinMain` boots it
  → `make_self<GameApp>` → `game_main()`.

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
- **`ImmediateRenderer` + the fxc / HLSL / `CompiledShaders` pipeline** — removed. All 2D
  goes through `Render2D`, which compiles its (inline) shaders at runtime.
- **Off-screen canvas + letterboxed present** — removed. The game renders full-window
  straight to the back buffer (letterbox via the viewport); `Renderer` shrank to a thin
  device/palette adopter.
- **Shader-side text outline** — replaces the old two-pass offset drop-shadow, for both
  GUI and in-game text.
- **Review hardening** — `vsnprintf` bounds (+ no `string_view`-as-format), the present
  binds its own opaque/no-depth OM state, `gfx_display_pretty_text` wrap bounded, the GUI
  panel SRV cached, and topology/shader asserts.
- **`dialog_win.cpp`** (dead save/load file picker) — removed.

## Not yet verified on Windows ⏳

This 2D consolidation has only been **compile-gated by CI**; it has **not** been run on
Windows. The arc most likely to hide a visual regression:

- the **direct-to-back-buffer + letterbox/scissor transform** (gfx2d → Render2D): confirm
  the letterboxed retro screens (status / charts / station / intro) center at the right
  integer scale with black bars, scissor-clipped charts land correctly at scale ≠ 1, and
  full-window flight fills the window with the HUD floated;
- the **GUI overlay** draws correctly on top of the letterboxed game;
- **outlined text** over busy backgrounds;
- **live window resize** (`WM_SIZE` → Core swap-chain rebuild) — no crash, correct layout.

(Older, previously confirmed on Windows: GUI menus render; the Options/Settings/Quit and
Market migrations; palette colour.)

## Next steps (in order)

1. **Windows smoke test** of the items above — the validation gate before building
   further on this stack.
2. **Chart cross-hair as a texture** — the XOR logic-op path was dropped in the Render2D
   move (it never worked reliably); draw the crosshair as a sprite/quad instead.
3. **Move the game render onto the `GameMain` lifecycle** (`Update` / `RenderScene` /
   `RenderCanvas`) instead of `game_main()` driving everything.
4. **Dead-code cleanup** — the unreachable legacy screens (`display_options` /
   `game_settings_screen` / `quit_screen`, `display_market_prices`, `display_inventory` /
   `display_data_on_planet`, `equip_ship`) and their `SCR_*` keyboard dispatch (note
   `display_commander_status` still serves the docked backdrop).
5. **A4 render-seam ownership** — give a client per-frame render context the `RenderQueue`
   + `GfxRenderSink`, retiring the `ActiveRenderQueue()` / `g_gfxSink` globals
   (`RenderContext.h`). Tied to the GameLogic split.
6. **Retire keyboard GUI nav** once mouse-driven menus fully cover the screens (then
   simplify `GuiWindow::Update`).
7. **Smaller:** port `InputField` / `InputScroller` value sliders onto Render2D if needed;
   `WM_POINTER` touch is wired but dormant (`EnableMouseInPointer` left off); re-check the
   monospaced font metrics on the charts / dense screens.

> The MMO/server migration is tracked separately in `MIGRATION_ROADMAP.md`; this file
> covers only the client GUI / 2D-renderer stack.
