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
  game's uncompressed `.dds` via the platform image loader.
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
- **Font sheet**: `Fonts/SpeccyFontENG.dds` is assumed to match the renderer's 16×14
  ASCII-32 grid; confirm glyph placement and adjust `TextRenderer::GetTexCoordX/Y` if not.

**Next migration work (Phase 8+):**
- Convert real game screens (options/prefs, docked/trade/equip, charts) from
  `gfx_display_*` to `GuiWindow`s with list/grid widgets — wired to actual game state.
- Migrate the game render into the `GameMain` lifecycle
  (`Update`/`RenderScene`/`RenderCanvas`) instead of `game_main()` driving everything.
- **Eventually render the world full-window** (drop the 512×514 letterboxed canvas);
  the seams are pointed that way.
- Once every 2D screen is migrated, **retire** the `gfx_dx11` 2D batch and
  `platform/Font`, and shrink/replace `Renderer`.

## Phase index
- `phase1-graphicscore.md` — GraphicsCore + ImmediateRenderer + shaders.
- `phase2-4-gui-text.md` — TextureManager; TextRenderer + Strings; GuiWindow/GuiButton/Canvas.
- `phase5-graphicscore-live.md` — first device-unified GUI overlay *(superseded by 6–7)*.
- `phase6-clientengine.md` — ClientEngine/GameMain/EventManager bootstrap; full device/window ownership.
- `phase7-canvas-mouse.md` — full-window, mouse + touch Canvas; live resize; real menu.

> Note: the MMO/server migration is tracked separately in `MIGRATION_ROADMAP.md`; this
> file only covers the client GUI/GraphicsCore import.
