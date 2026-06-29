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

Demonstration: **F1** opens a centred main menu (Options/Quit) + a modal Options
sub-window — mouse hover/click/drag/close, rendered through the imported stack.

## Verified vs. unverified

- ✅ **Builds + links on the Windows CI runner** (after the `FileSys.h` include fix). All
  of NeuronCore/NeuronClient/DeepspaceOutpost compile; `fxc` regenerates the shader
  headers.
- ✅ **Boots and renders** through the GuiWindow background fix: window comes up, the
  game runs, **F1** shows the menu, and the panel/text render correctly (the interface
  texture needed a mip chain — see phase notes).
- ⏳ **Not yet rebuilt/verified on Windows:** the modal input-ownership + edge-nav change
  and **all of Phase 7** (mouse input, full-window GUI rendering, live resize, the
  MainMenu/Options screens) were committed after the last visual confirmation. Build +
  run still pending.

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
- **Deferred in this slice:** Save/Load Commander (the legacy `gfx_display_*`
  file-entry screens aren't migrated, so they're omitted from the GUI Options menu for
  now) and retiring the now-unreached `display_options`/`game_settings_screen`/
  `quit_screen` + their `current_screen` (`SCR_OPTIONS`/`SCR_SETTINGS`/`SCR_QUIT`)
  input dispatch in `options.cpp`/`main.cpp` (left intact, harmless).
- Convert the remaining game screens (docked/trade/equip, charts, market) from
  `gfx_display_*` to `GuiWindow`s with list/grid widgets — wired to actual game state,
  following this pattern.
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
