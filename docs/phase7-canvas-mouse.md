# Phase 7 — Full-window, mouse + touch driven Canvas

## Goal
Move the GUI `Canvas` out of the letterboxed 512×514 game canvas and into **full
window / client space**, and drive it with **mouse + touch** (keyboard nav kept only
as a transitional fallback). This is the foundation for migrating all 2D UI (menus,
charts, station, eventually the HUD) onto `Canvas`/`GuiWindow` widgets and retiring the
`gfx_*` 2D path.

## Why this is the right shape (and not harder than keyboard)
- `Canvas` is **already** authored for output/client space: `GuiWindow::MakeAllOnScreen`
  and `Canvas::EclRegisterWindow` clamp/position windows using
  `Neuron::Graphics::Core::GetOutputSize()` (the client size), and
  `Canvas::EclUpdateMouse` compares the cursor against window coords in that same space.
- The only thing in canvas (512×514) space today is `GuiOverlay::Render`, which binds
  the retro canvas RTV and uses a 512×514 ortho. That's a latent mismatch (a window
  placed at client x≈900 would render off the 512 ortho). Phase 7 aligns rendering with
  the space `Canvas` already assumes.
- Mouse in client space needs **no letterbox transform**: `WM_MOUSEMOVE` lParam is
  already client pixels → feed straight to `EclUpdateMouse`. (Mapping into the
  letterboxed canvas would have been the fiddly part; we skip it entirely.)

## Target architecture
- The game still renders its world + HUD into the 512×514 canvas and it is still
  letterboxed onto the back buffer (unchanged for now).
- The GUI is a **separate full-window overlay** drawn directly onto the back buffer in
  client space, on top of the letterboxed game, before Present.
- Input: a platform mouse/touch source feeds `Canvas::EclUpdateMouse(x, y, lmb, rmb)`
  in client pixels every frame. `Canvas` does its own hit-testing (`EclMouseInWindow`),
  so clicks over a window are consumed by the GUI and clicks elsewhere fall through to
  the game.

## Work breakdown

### 1. Mouse + touch input (platform)
- `input_win`: add cursor state (`int g_mouseX/Y`, `bool g_lmb/g_rmb`) and handle, in the
  `InputWndProc` EventManager processor:
  - `WM_MOUSEMOVE` → update x/y from `GET_X/Y_LPARAM`.
  - `WM_LBUTTONDOWN/UP`, `WM_RBUTTONDOWN/UP` → update button state (and `SetCapture`/
    `ReleaseCapture` so drags outside the window still report up).
  - `WM_POINTERDOWN/UPDATE/UP` (touch): `GetPointerInfo` + `ScreenToClient`; map the
    primary pointer to (x, y, lmb). Multi-touch/gestures are out of scope here.
- Expose `void input_mouse_state(int& x, int& y, bool& lmb, bool& rmb)`.
- Cursor visibility: show the OS cursor while the GUI is interactive (and/or draw a
  custom one later); the engine currently leaves the default arrow.

### 2. Decouple GUI rendering from the letterboxed canvas
- Split `Renderer::present()` into:
  - `blitCanvasToBackBuffer()` — bind the back buffer, draw the letterboxed canvas, and
    **leave the back buffer bound** with a full client-area viewport.
  - `swap()` — `swap_chain_->Present(...)`.
- `gfx_update_screen()` ordering becomes:
  `gfx_dx11_flush()` → `r.blitCanvasToBackBuffer()` → `GuiOverlay::Render(clientW, clientH)`
  → `r.swap()`.
- `GuiOverlay::Render` uses a **client-space** ortho `Ortho2D(0, clientW, clientH, 0)`
  and the bound back buffer (not `bindCanvasTarget()`), so windows render where `Canvas`
  positions them.

### 3. Coordinate space
- GUI windows are sized/placed in client pixels (already true via `OutputSize`).
- Feed `EclUpdateMouse` raw client pixels each frame from `input_mouse_state`.
- `GuiOverlay::Render` ortho = client size; remove the 512×514 assumption.

### 4. Live window resize (now required)
A full-window GUI must track the client area. Implement the WM_SIZE path deferred in
Phase 6:
- Engine `WM_SIZE` (non-minimized) → `Graphics::Core::WindowSizeChanged(w, h)` (recreate
  swap chain RTV/viewport) → a platform hook to rebuild the `Renderer` back buffer/canvas
  → `GameMain::OnWindowSizeChanged`.
- GUI auto-follows because ortho + `MakeAllOnScreen` read the current output size; add a
  re-clamp of open windows on resize.

### 5. Input arbitration + keyboard sunset
- Per frame: feed mouse to `Canvas`. A click over a window is consumed by the GUI;
  otherwise it falls through to the game (the game is mouse-agnostic today, so "fall
  through" is initially a no-op).
- Keep the Phase-6 keyboard nav + `input_suppress_game_keys` as a **transitional**
  fallback so nothing regresses while screens are converted. Once mouse-driven menus
  cover the real screens, retire keyboard GUI nav (and simplify `GuiWindow::Update`).

### 6. Proof-of-life
Convert the F1 demo (or a real modal screen) to be **mouse-clickable**: hover
highlights, click activates, drag moves the window, close button works — validating
`EclUpdateMouse` end to end in client space.

## After Phase 7 — migrating the 2D content (subsequent phases)
With a real full-window, mouse-driven `Canvas`, migrate screen by screen:
1. Modal menus first (options/prefs, confirm-quit, commander load) → `GuiWindow`s.
2. Docked screens (trade, equip, charts) → `GuiWindow`s with list/grid widgets.
3. Persistent HUD elements last (these are the most coupled to `gfx_*`).
Each converted screen stops calling `gfx_display_*`/`gfx_draw_*` for that UI and renders
through `ImmediateRenderer`/`TextRenderer` widgets. When every 2D screen is migrated,
retire `gfx_dx11`'s 2D batch and `platform/Font` and shrink `Renderer` to canvas-less
full-window present (or drop the 512×514 canvas entirely if the world is rendered
full-window through `ImmediateRenderer`).

## Risks / decisions
- **Cursor model**: OS cursor vs a drawn cursor; show only when GUI is interactive vs
  always. Recommend OS cursor initially.
- **Resize correctness**: the Core+Renderer resize coordination is the trickiest new
  code; gate it behind the existing "rebuild back buffer" helper and test by dragging
  the window edges.
- **Touch scope**: single primary pointer → mouse only for now; real multi-touch is a
  later, separate effort.
- **Big picture**: migrating *all* 2D actions is a large, multi-phase undertaking;
  Phase 7 is only the foundation + one converted screen. Keep `gfx_*` working until each
  screen is moved.

## Verification (on Windows)
1. Build + boot unchanged; F1 still opens the demo.
2. The demo window renders at the correct on-screen position in client space (not
   confined to the 512 region) and scales/letterboxes independently of the game canvas.
3. Mouse: hover highlights buttons, left-click activates, dragging the title bar moves
   the window, the close button closes it.
4. Resize the window: the GUI stays correctly placed and crisp; the game stays
   letterboxed.
5. Game input still works when no window is open; is suppressed (or falls through
   harmlessly) when one is.

## Running plan
- Phases 1–6: GraphicsCore/ImmediateRenderer/shaders; TextureManager; text+Strings;
  GuiWindow/GuiButton/Canvas; device-unified overlay; ClientEngine/GameMain/EventManager
  bootstrap. ✅
- Phase 6.5: modal keyboard input ownership + edge nav. ✅
- **Phase 7 (this): full-window, mouse+touch `Canvas`.**
- Phase 8+: migrate 2D screens onto `Canvas`; retire the `gfx_*` 2D path.
