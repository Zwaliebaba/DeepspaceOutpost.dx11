# GUI / GraphicsCore import — status & open items

Single source of truth for the GraphicsCore + GUI/text import work (phases 1–7). The
per-phase docs (`phase1`…`phase7`) are the historical record of each step; this file
tracks the **current** state and what's left.

_Last updated: end of Phase 7._

## Where we are

The donor `NeuronClient`'s native Direct3D 11 stack has been imported and is now the
client's foundation:

- **GraphicsCore + ImmediateRenderer** (`NeuronClient/graphics/`) — native D3D11 device
  + glBegin/glVertex-style immediate renderer; HLSL shaders in `NeuronClient/shaders`
  (fxc → `CompiledShaders/*.h`, committed).
- **TextureManager** — native D3D11 textures with a generated mip chain; loads the
  game's `.dds` via its own native `graphics/DDSTextureLoader` (no platform image dep).
- **Text + Strings** (`gui/TextRenderer`, `Strings`) — bitmap-font 2D text + JSON
  localization (`GameData/Strings/<lang>/*.json`).
- **GUI** (`gui/Widget|GuiButton|GuiWindow|Canvas|GuiOverlay`) — the ECL window/menu
  system, drawn through ImmediateRenderer.
- **Engine bootstrap** (`NeuronClient/ClientEngine`, `GameMain`, `DeepspaceOutpost/GameApp`,
  `NeuronCore/EventManager`) — `ClientEngine` owns the window + the one D3D11 device
  (`Neuron::Graphics::Core`); the legacy platform `Renderer` adopts that device for the
  `gfx_*` path. `wWinMain` boots `ClientEngine` → `make_self<GameApp>` → `game_main()`.
- **GUI is full-window + mouse-driven** (Phase 7) — the GUI renders in client/window
  space on top of the still-letterboxed game; mouse (and a dormant minimal touch path)
  drive `Canvas`; keyboard nav remains as a transitional fallback; opening a modal menu
  suppresses game input.

In-game entry points: **F11** opens the Options menu (Game Settings / Quit) and **F8**
opens Market Prices — both as mouse-driven GuiWindows over the running game. (The early
F1 demo main menu has been removed now that real screens drive the overlay.)

## Verified vs. unverified

- ✅ **Builds + links on the Windows CI runner** (after the `FileSys.h` include fix). All
  of NeuronCore/NeuronClient/DeepspaceOutpost compile; `fxc` regenerates the shader
  headers.
- ✅ **Boots and renders** through the GuiWindow background fix: window comes up, the
  game runs, the overlay menu shows, and the panel/text render correctly (the interface
  texture needed a mip chain — see phase notes).
- ⏳ **Not yet rebuilt/verified on Windows:** the F1-demo removal and the migrated game
  screens (Options/Settings via F11, Market via F8) plus the Save/Load Commander
  removal were committed after the last visual confirmation. Build + run still pending.

## Open items

**Verify next (Windows build):**
1. Phase 7 builds and the menu appears centred in client space (not the 512 region).
2. Mouse: hover highlights, click activates, title-bar drag moves, Options opens/closes.
3. **Live window resize** (drag the edges) — the riskiest new code (Core swap-chain +
   Renderer back-buffer rebuild on `WM_SIZE`); confirm no crash and correct layout.
4. Game input is locked out while a menu is open and returns when it closes.

**Functional gaps / deferred:**
- **Touch** is wired but dormant — `WM_POINTER` only fires if we call
  `EnableMouseInPointer(TRUE)` (left off so mouse keeps using `WM_MOUSE*`). Enable when a
  touch target is real.
- **`InputField`/`InputScroller`** (value-slider widgets) were not imported — they still
  used the legacy `gl*` path; `GuiWindow::CreateValueControl` is omitted. Port onto
  `ImmediateRenderer` if value controls are needed.
- **Keyboard GUI nav** is transitional; retire it once mouse-driven menus cover the real
  screens (then simplify `GuiWindow::Update`).
- **Font sheet**: `Fonts/SpeccyFontENG.dds` is assumed to match the 16×14 ASCII-32
  grid used by both `TextRenderer` (menus) and now the game's `gfx_display_*` text;
  confirm glyph placement and adjust the grid math if not. The game text switched
  from the old proportional verd font to this monospaced sheet (~8px advance), so
  line/column layout should be visually re-checked on Windows.

**Next migration work (Phase 8+):**
- ✅ **Migrated: the Options menu, Game Settings, and Quit confirmation.**
  `DeepspaceOutpost/GameWindows` defines, on the engine's `GuiWindow`/`Canvas` stack:
  - an **Options menu** (`OptionsMenuWindow`, named "Options") → Game Settings / Quit;
  - **Game Settings** (`SettingsWindow`) — value-cycling rows bound straight to the
    config globals (`wireframe`, `anti_alias_gfx`, `planet_render_style`,
    `hoopy_casinos`, `instant_dock`) + a Save button → `write_config_file()`;
  - a **Quit confirmation** (Yes via the engine `GameExitButton` / No via `CloseButton`).

  Engine seams: `GuiOverlay::SetOptionsWindowFactory` lets the game hand the overlay a
  window the engine can't build (it knows nothing about game state); `GameApp::Startup`
  registers it. `GuiOverlay::Open()` shows the overlay straight at the Options menu, and
  **`main.cpp`'s F11 now calls `GuiOverlay::Open()`** instead of the legacy
  `display_options()` — so the in-game options entry runs through the GUI, floating over
  the running game with input suppressed. The engine's placeholder `OptionsWindow`
  remains only as the no-factory fallback.
- ✅ **Removed: Save/Load Commander.** Commander file persistence is gone entirely
  (`save/load_commander_file` + `checksum`, the two `*_screen` functions, menu entries,
  and the intro "Load (Y/N)" prompt → now "Press Space to Begin"). The game boots with
  the compiled-in default commander via `restore_saved_commander()`.
- ✅ **Migrated: Market Prices (F8).** `MarketWindow` (in `GameWindows`) lists every
  commodity (product / unit / price / for-sale / in-hold — column-aligned, since the
  GUI font is monospaced) with inline **Buy**/**Sell** buttons and a live cash readout;
  it refreshes from game state every frame (so local *and* thin-client/server trades
  show immediately). Engine seam: `GuiOverlay::ShowWindow(name, factory)` shows the
  overlay and opens/focuses a game window; **`main.cpp`'s F8 calls `OpenMarketWindow()`**
  instead of `display_market_prices()`. Trade logic was split into render-free
  `market_buy`/`market_sell` (+ `market_format_row`/`market_credits`/`market_item_count`)
  in `docked.cpp`; the legacy `buy_stock`/`sell_stock` now wrap those.
- **Deferred / dead-but-compiled:** the now-unreached legacy screens
  (`display_options`/`game_settings_screen`/`quit_screen`, `display_market_prices` +
  `highlight_stock`/`display_stock_price`) and their `current_screen` keyboard dispatch
  in `options.cpp`/`docked.cpp`/`main.cpp` are left intact (harmless) until a later
  cleanup pass retires them.
- ✅ **Migrated: Commander Status (F9), Inventory (F10), Data on Planet (F7).** All three
  are read-only panels, so they share a generic `InfoWindow` (a list of text lines + a
  Close button, rebuilt from live game state each frame). `docked.cpp` gained render-free
  line accessors (`cmdr_status_line_count`/`_line`/`_title`, `inventory_*`, `planet_data_*`)
  that reuse the exact field logic; `GameWindows` adds `Open{Commander,Inventory,PlanetData}Window`
  and `main.cpp` routes F9/F10/F7 to them. **F7 caveat:** in thin-client (MMO) mode it
  still calls the legacy `display_data_on_planet` (server-replicated data + chart-cursor
  selection); single-player uses the GUI window with locally generated data.
- **Still legacy (intentionally):** the **charts** (F5 galactic / F6 short-range) stay on
  `gfx_display_*` — they're interactive spatial maps (crosshair nav, fuel circle), not
  list/form screens, so a `GuiWindow` is the wrong fit.
- ✅ **Migrated: Equip Ship (F4).** `EquipWindow` is the interactive buy-list: each row
  buys its item (or expands a laser sub-menu) via the render-free `equip_do`, and the
  window **rebuilds its rows when the visible set changes** (tech-level/`show` filtering,
  laser sub-menu expand) — rebuild happens at frame start, before clicks are processed,
  and Canvas tracks the clicked button by name, so recreating rows across frames is safe.
  `docked.cpp` gained `equip_do`/`equip_reset`/`equip_visible_count`/`equip_visible_index`/
  `equip_row_text`/`equip_buyable` (legacy `buy_equip` now wraps `equip_do`); `main.cpp`
  routes docked F4 to `OpenEquipWindow()`.
- **All list/form game screens are now GUI windows.** What remains on `gfx_display_*`:
  the charts (by design) and the flight HUD / 3D — i.e. the gfx_dx11 backend itself,
  whose retirement is the separate, larger effort tracked above.
- Migrate the game render into the `GameMain` lifecycle
  (`Update`/`RenderScene`/`RenderCanvas`) instead of `game_main()` driving everything.
- **Eventually render the world full-window** (drop the 512×514 letterboxed canvas);
  the seams are pointed that way.

## Retiring the legacy platform 2D layer (`gfx_dx11.cpp` / `Image.cpp`)

`Font.cpp` is **gone** (see below). The two remaining files and the dependency web
(verified by grep):

| File | What it is | Who depends on it |
|---|---|---|
| `platform/gfx_dx11.cpp` | Implements the **entire `gfx.h` contract** — 2D primitives (pixel/line/circle/tri/rect/poly), sprites, text (`gfx_display_*`), **depth-sorted 3D** (`gfx_render_polygon/line/start/finish`), the **scanner HUD**, clip regions, `xor_mode`, palette-index colour. | The whole game (`docked/intro/main/missions/options/space`, …) **and** `GfxRenderSink` (the `RenderQueue` replay). |
| `platform/Image.cpp` | BMP/PCX/uncompressed-DDS decoders (`load_image_rgba`, `load_indexed`). | **Only** `gfx_dx11.cpp` (sprite loading). **No longer used by the new code** — `TextureManager` decodes `.dds` via `graphics/DDSTextureLoader`. |

✅ **Done — `Font.cpp` deleted.** The game's `gfx_display_*` text no longer uses the
verd2/verd4 PCX grabber atlas. `drawString` now draws monospaced glyph cells from the
**same `Fonts/SpeccyFontENG.dds` sheet `TextRenderer` uses** (16×14 ASCII-32 grid,
loaded once via `TextureManager`), still emitted through `gfx_dx11`'s deferred batch so
in-game text keeps submission-order compositing and scissor clipping. Net effect: one
font system across menus and game; metrics changed proportional → monospaced (re-verify
layout on Windows).

**Prerequisites for removing `gfx_dx11.cpp` itself, in order:**

1. **Reimplement the full `gfx_*` contract on the new stack** before `gfx_dx11.cpp` can
   go. This is the big one — it means moving **all** game rendering (not just menus):
   - 2D primitives + sprites → `ImmediateRenderer`;
   - all `gfx_display_*` text → `TextRenderer` (the font sheet is already unified);
   - the **depth-sorted 3D wireframe** path (`gfx_render_polygon/line` + `gfx_start/finish_render`) → an `ImmediateRenderer` equivalent (this is the in-flight ship/scene render);
   - the **scanner/HUD**, clip-region → scissor, `xor_mode` cross-hairs, and palette-index → RGBA colour resolution.
   - Then either rewrite `GfxRenderSink` to target the new backend, or replace the `RenderQueue` replay entirely.
   - The 512×514 canvas + `Renderer` present pipeline exist to host this 2D batch; retiring `gfx_dx11` ties into the "render the world full-window" goal and shrinking/removing `Renderer`.
2. **`Image.cpp`** is now used **only** by `gfx_dx11.cpp` (sprite loading) — the new
   `TextureManager` has its own `graphics/DDSTextureLoader`. So `Image.cpp` can be
   deleted **together with** `gfx_dx11.cpp`; no re-homing is needed. (Until then it
   stays for the legacy sprite path. Once the sprites are `.dds` they can load through
   `TextureManager` instead, dropping the last `Image.cpp` consumer.)

**Net:** `Image.cpp` goes away together with `gfx_dx11.cpp`; the hard part is
`gfx_dx11.cpp` itself — a large, whole-renderer migration of the `gfx_*` contract onto
the new stack. ✅ Done so far: `TextureManager` decoupled from `Image.cpp` via the native
`graphics/DDSTextureLoader`; **`Font.cpp` removed** and game text unified onto the `.dds`
sheet (this change).

## Phase index
- `phase1-graphicscore.md` — GraphicsCore + ImmediateRenderer + shaders.
- `phase2-4-gui-text.md` — TextureManager; TextRenderer + Strings; GuiWindow/GuiButton/Canvas.
- `phase5-graphicscore-live.md` — first device-unified GUI overlay *(superseded by 6–7)*.
- `phase6-clientengine.md` — ClientEngine/GameMain/EventManager bootstrap; full device/window ownership.
- `phase7-canvas-mouse.md` — full-window, mouse + touch Canvas; live resize; real menu.

> Note: the MMO/server migration is tracked separately in `MIGRATION_ROADMAP.md`; this
> file only covers the client GUI/GraphicsCore import.
