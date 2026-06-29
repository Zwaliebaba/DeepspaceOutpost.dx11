# GraphicsCore goes live — Phase 5 (device unification + GUI overlay)

> **Status — partly superseded (kept for history).** This phase had the platform
> `Renderer` create the device and `Core` *adopt* it (`Core::AdoptExisting`), and
> `GuiOverlay` do the engine bring-up + render into the 512×514 canvas. Phase 6 reversed
> ownership (**`ClientEngine`/`Core` create the device; `Renderer` adopts it**;
> `Core::AdoptExisting` was removed), and Phase 7 moved the GUI to **full-window** client
> space (no longer the letterboxed canvas). The "device unification" idea is current; the
> specific mechanics below are not. Current state:
> [`gui-graphicscore-status.md`](gui-graphicscore-status.md).

Builds on Phases 1–4. This phase makes the imported GraphicsCore / ImmediateRenderer
/ text / GuiWindow stack **actually run**, on the **same Direct3D 11 device** as the
existing game, without disturbing the working render path. **Not compiled/run here**
(Linux, no MSVC/DX11) — needs a Windows build to verify.

## What landed

### 1. Device unification — `Core::AdoptExisting(...)`
`Neuron::Graphics::Core` no longer needs to create its own device/swap chain. The new
`AdoptExisting(device, context, swapChain, hwnd, w, h)` takes owning references on the
objects the platform `Renderer` already created (added `Renderer::swapChain()`), builds
a back-buffer RTV (no depth — the overlay is 2D), and sets the viewport. Result: **one
device** shared by the legacy `gfx_dx11` batch and the native `ImmediateRenderer`.

### 2. GUI overlay — `gui/GuiOverlay.{h,cpp}`
- `Startup()` (best-effort, `try/catch`): adopt the device → `ImmediateRenderer::Startup`
  → `Strings::Startup` → load `g_gameFont`/`g_editorFont` from `Fonts/SpeccyFontENG.dds`
  → `Canvas::Startup` → register a demo `GuiWindow`.
- `Render(canvasW, canvasH)`: draws the registered windows **into the existing 512×514
  canvas** (after the game's 2D batch, before the letterboxed present), under a
  canvas-space ortho. So the GUI is letterboxed identically to everything else and we
  never fight `Renderer` over the back buffer / `Present`.
- `Update()`: edge-detected **F1 toggle**; when shown, drives `Canvas::EclUpdate()` for
  keyboard menu navigation.
- A `DemoMenuWindow` exercises the whole stack end to end: title bar (interface `.dds`
  texture + `g_gameFont`), localized button captions (`Strings`), keyboard nav, and a
  close button.

### 3. Hooks — `platform/platform_win.cpp`
- `gfx_graphics_startup()` → `GuiOverlay::Startup()` after the Renderer is ready.
- `gfx_update_screen()` → `GuiOverlay::Update()` + `GuiOverlay::Render(...)` between
  `gfx_dx11_flush()` and `Renderer::present()`.
- `gfx_graphics_shutdown()` → `GuiOverlay::Shutdown()`.

## Why this shape (and what was deliberately NOT done)

The original Phase-5 sketch said "reimplement the entire `gfx_*` contract on
ImmediateRenderer and delete `Renderer`/`gfx_dx11`/`Font`." Done blind (no Windows
build) that is a near-certain way to break the whole game: every screen — 3D wireframe
space, HUD, charts, station, menus — currently renders through `gfx_*` →
`gfx_dx11` (depth-sorted polys, scanner, XOR, palette, scissor, sprites, fonts).

So this phase makes the imported stack **live and correct in isolation** (the user's
actual ask: Strings printing + GuiWindow/GuiButton menus, standardized on GraphicsCore)
as a **fail-safe, opt-in overlay**, and leaves the wholesale `gfx_*` re-implementation
as an explicit, incremental follow-on to be done against a Windows build. If the new
path fails to initialise, `Startup`'s `try/catch` disables it and the game renders
exactly as before.

## Verify on Windows (in order)
1. Builds and links (NeuronClient + the new `gui/GuiOverlay.cpp`).
2. Game runs unchanged with the overlay hidden (default).
3. **F1** shows the demo window: panel + title text + three localized button captions;
   **Up/Down** move the highlight, **Enter** activates, **Esc**/Close dismiss.
4. If glyphs look misplaced: `Fonts/SpeccyFontENG.dds` doesn't match the donor text
   renderer's **16×14 ASCII-32 grid** — swap the sheet or adjust
   `TextRenderer::GetTexCoordX/Y`. (Won't crash.)

## Known limitations
- **Input isn't captured**: while shown, Up/Down/Enter/Esc drive both the menu and the
  game (ship pitch, etc.). Add input capture/an input mode when promoting the overlay
  from a demo to real menus.
- **No mouse**: the platform layer is keyboard-only, so `Canvas` mouse features (drag,
  click, hover tooltips) are dormant; navigation is keyboard-only.
- **`GameExitButton`** calls `platform_request_quit()`, which posts `WM_CLOSE` and
  exits the game the same way closing the window does.

## Running plan
- Phases 1–4: GraphicsCore + ImmediateRenderer + shaders; TextureManager; text +
  Strings; GuiWindow/GuiButton/Canvas. ✅
- Phase 5: device unification + live GUI overlay (this). ✅
- **Follow-on (incremental, needs Windows build):** capture input for menus; convert
  real game menus (options/prefs/station) to `GuiWindow`; then, screen by screen,
  re-implement the `gfx_*` primitives on `ImmediateRenderer` and retire
  `Renderer`/`gfx_dx11`/`platform/Font` once every screen is migrated.
