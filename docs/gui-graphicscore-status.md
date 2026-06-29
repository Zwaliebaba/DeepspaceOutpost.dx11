# GUI / GraphicsCore import — status & open items

Single source of truth for the GraphicsCore + GUI/text import and the retirement of the
legacy platform 2D layer. The per-phase docs (`phase1`…`phase7`) are the historical
record of the early steps; this file tracks the **current** state and what's left.

_Last updated: after retiring `gfx_dx11` + `Image` — the whole client now renders through
one stack._

## Where we are

The donor `NeuronClient` native Direct3D 11 stack is the client's foundation, and the
game now renders **entirely** through it — there is no longer a second, bespoke 2D
renderer.

- **GraphicsCore + ImmediateRenderer** (`NeuronClient/graphics/`) — native D3D11 device +
  glBegin/glVertex-style immediate renderer; HLSL in `NeuronClient/shaders` (fxc →
  committed `CompiledShaders/*.h`). Now also exposes **scissor clip** and an **XOR
  logic-op** blend (both default off).
- **TextureManager + DDSTextureLoader** — native D3D11 `.dds` loading with a generated
  mip chain. All game art (sprites, scanner HUD, fonts, interface) loads through here.
- **Text + Strings** (`gui/TextRenderer`, `Strings`) — one bitmap-font system
  (`Fonts/SpeccyFontENG.dds`) shared by the menus and in-game `gfx_display_*` text; JSON
  localization (`GameData/Strings/<lang>/*.json`).
- **GUI** (`gui/Widget|GuiButton|GuiWindow|Canvas|GuiOverlay`) — the ECL window/menu
  system, drawn through ImmediateRenderer, full-window + mouse-driven (keyboard nav is a
  transitional fallback). Opening a window suppresses game input; the overlay auto-hides
  when the last window closes.
- **Engine bootstrap** (`ClientEngine`, `GameMain`, `DeepspaceOutpost/GameApp`,
  `NeuronCore/EventManager`) — `ClientEngine` owns the window + the one D3D11 device
  (`Neuron::Graphics::Core`). `wWinMain` boots `ClientEngine` → `make_self<GameApp>` →
  `game_main()`.
- **`gfx.h` 2D contract** — implemented by `platform/gfx2d.cpp` (formerly `gfx_dx11.cpp`):
  a submission-order batch (primitives / sprites / text / depth-sorted 3D / scanner / clip
  / xor) **replayed each frame through ImmediateRenderer** into the off-screen 512×514
  canvas. `platform/Renderer` still owns that canvas + the letterboxed present + the
  master palette. `GfxRenderSink` replays the `RenderQueue` through the same contract.

## Game screens — all list/form screens are GUI windows

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
| F11 | Options → Game Settings / Quit | `OptionsMenuWindow` + `SettingsWindow` + Quit confirm | Settings bound to config globals + Save |

The trade / equip / read-only logic was split into **render-free accessors** in
`docked.cpp` (`market_*`, `equip_*`, `cmdr_status_*` / `inventory_*` / `planet_data_*`) so
the GUI never touches the legacy gfx drawing; the legacy `buy_stock`/`buy_equip` now wrap
those.

- **Still legacy, by design:** the **charts** (F5 galactic / F6 short-range) — interactive
  spatial maps (crosshair nav, fuel circle), not list/form screens, so a `GuiWindow` is
  the wrong fit.
- **Removed:** Save/Load Commander (file persistence gone; boots the compiled-in default
  commander). The early **F1 demo main menu** (real screens drive the overlay now).

## Retired: the legacy platform 2D layer — ✅ done

`Font.cpp`, `Image.cpp`, and the bespoke `gfx_dx11` Direct3D 11 renderer are all gone;
the client renders through one stack (`Core` + `ImmediateRenderer` + `TextureManager`).

- **`Font.cpp`** — verd2/verd4 PCX grabber atlas. Game text draws from the same
  `SpeccyFontENG.dds` sheet as the GUI; one font system everywhere.
- **`Image.cpp`** — BMP/PCX decoder. Sprites + scanner HUD ship as `.dds` via
  `TextureManager`; `Renderer` keeps its own small BMP parser for the `scanner.bmp`
  master palette, so that file stays.
- **`gfx_dx11.cpp` → `gfx2d.cpp`** — keeps the proven submission-order batch (command
  list + scissor + `xor_mode` + 512×514 canvas) but deletes its bespoke D3D11 pipeline
  (inline HLSL, vertex/constant buffers, blend/raster/sampler states, manual draw loop).
  `gfx2d_flush()` binds the canvas and replays the batch through ImmediateRenderer under
  a screen-space ortho with per-command scissor/blend/texture. The depth-sorted 3D flight
  wireframe rides along for free (it reduces to `gfx_polygon`/`gfx_draw_colour_line`).
- **`ImmediateRenderer`** gained scissor clip + the XOR logic-op blend the batch needs.
- **`GfxRenderSink`** only used the `gfx.h` contract, so it was unaffected.

## Verified vs. unverified

- ✅ **Builds + boots on Windows**, GUI menus render correctly (the interface texture
  needed a mip chain). The Options/Settings/Quit and Market migrations were visually
  confirmed.
- ⏳ **Committed but not yet re-verified on Windows:**
  - the remaining screen migrations — Equip (F4), Data on Planet (F7), Commander (F9),
    Inventory (F10);
  - **the `gfx_dx11 → gfx2d` backend swap + `Image` retirement** — this is the big one: it
    touches **all** game rendering (flight 3D wireframe, HUD, scanner, sprites, the chart
    XOR cross-hairs, text). Most-likely failure points: the ortho / Y-orientation and the
    per-command blend in `gfx2d_flush`, and the XOR path on GPUs lacking the output-merger
    logic op (silent fallback to opaque).
- ⏳ **Live window resize** (`WM_SIZE` → Core swap-chain + Renderer back-buffer rebuild) —
  confirm no crash and correct layout.

## Open items / next

- **Shrink/remove `Renderer` and render the world full-window** — drop the 512×514
  letterboxed canvas. The seams point this way; the next big architectural step.
- **Move the game render into the `GameMain` lifecycle** (`Update`/`RenderScene`/
  `RenderCanvas`) instead of `game_main()` driving everything.
- **Dead-code cleanup:** the now-unreachable legacy screens (`display_options`/
  `game_settings_screen`/`quit_screen`, `display_market_prices`,
  `display_commander_status`/`display_inventory`/`display_data_on_planet`, `equip_ship`)
  and their `SCR_*` keyboard dispatch are left compiled but unreached via the F-keys
  (`display_commander_status` also still serves the docked backdrop) — delete in a pass.
- **Retire keyboard GUI nav** once mouse-driven menus fully cover the screens (then
  simplify `GuiWindow::Update`).
- **Touch** is wired but dormant — `WM_POINTER` only fires if `EnableMouseInPointer(TRUE)`
  is called (left off so the mouse keeps using `WM_MOUSE*`).
- **`InputField`/`InputScroller`** value-slider widgets were not imported
  (`GuiWindow::CreateValueControl` omitted); port onto ImmediateRenderer if needed.
- **Font metrics:** in-game text switched proportional → monospaced (~8px advance);
  re-check column/line layout on the charts and any dense screens.

## Phase index (historical)
- `phase1-graphicscore.md` — GraphicsCore + ImmediateRenderer + shaders.
- `phase2-4-gui-text.md` — TextureManager; TextRenderer + Strings; GuiWindow/GuiButton/Canvas.
- `phase5-graphicscore-live.md` — first device-unified GUI overlay *(superseded by 6–7)*.
- `phase6-clientengine.md` — ClientEngine/GameMain/EventManager bootstrap; device/window ownership.
- `phase7-canvas-mouse.md` — full-window, mouse + touch Canvas; live resize; first real menu.

> Note: the MMO/server migration is tracked separately in `MIGRATION_ROADMAP.md`; this
> file only covers the client GUI/GraphicsCore import.
