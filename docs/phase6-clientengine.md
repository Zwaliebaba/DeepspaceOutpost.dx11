# ClientEngine / GameMain bootstrap — Phase 6 (full ownership swap)

> **Status:** landed; builds + boots on Windows. The "live window resize is not handled"
> limitation noted below was **addressed in Phase 7**, which also made the GUI
> full-window and mouse-driven. Current state + open items:
> [`gui-graphicscore-status.md`](gui-graphicscore-status.md).

Adopts the donor's engine front door: a `ClientEngine` that owns the window and the
native Direct3D 11 device, a `GameMain` application base, a dummy `GameApp`, and an
`EventManager`-based window-message fan-out. **Not compiled/run here** (Linux, no
MSVC/DX11) — needs a Windows build to verify; this replaces the live boot path, so
it is the highest-risk change in the series.

## New ownership model

Before: `game_main()` → `gfx_graphics_startup()` created the window + the `Renderer`'s
own D3D11 device/swap chain.

After: `wWinMain` → `ClientEngine::Startup()` creates the window + the
`Neuron::Graphics::Core` device/swap chain (+ `ImmediateRenderer`, `Canvas`, fonts,
`Strings`). `game_main()` runs as before, and `gfx_graphics_startup()` now **adopts**
the engine's device into the `Renderer` (`Renderer::initAdopt()`) to build the
offscreen 512×514 canvas + letterboxed present. One device, one window, shared by the
legacy `gfx_*` batch and the native `ImmediateRenderer`.

## What landed

- **`NeuronCore/EventManager.{h,cpp}` + `Event.h`** — imported (clean, no D3D). Two
  facilities: a typed pub/sub, and a **WNDPROC chain** (`AddEventProcessor` /
  `RemoveEventProcessor` / `WndProc`). The engine's window proc fans every message
  through it; a processor returns `-1` to pass the message on.
- **`NeuronClient/GameMain.h`** — trimmed native base: a `winrt::implements<GameMain,
  IInspectable>` with `Startup/Shutdown/Update/RenderScene/RenderCanvas` + window
  lifecycle hooks. (Donor's `ASyncLoader`/`IDeviceNotify`/coroutine `Startup` dropped.)
- **`NeuronClient/ClientEngine.{h,cpp}`** — native, no D3D9/`Direct3DInit`,
  `SystemInfo` or `Audio`. `Startup` brings up CoreEngine/Strings/Core/ImmediateRenderer/
  Canvas/fonts, creates the window (overlapped, 1024×1026, matching the old window),
  registers the input processor, and registers the demo GUI. `StartGame` holds the
  `GameMain` and calls its `Startup()`. `Shutdown` tears it all down.
- **`DeepspaceOutpost/GameApp.h`** — `class GameApp : public Neuron::GameMain`, a dummy
  (the legacy game still runs through `game_main()`; hooks are stubs for now).
- **`DeepspaceOutpost/WinMain.cpp`** — the donor-style `wWinMain`: CRT leak flags, anchor
  CWD + `FileSys::SetHomeDirectory`, DPI awareness, `ClientEngine::Startup` →
  `make_self<GameApp>` → `StartGame` → `game_main()` → `ClientEngine::Shutdown`.
- **`platform_win.cpp`** — window/WndProc removed; now just adopts the device
  (`gfx_graphics_startup` → `Renderer::initAdopt`), pumps messages, drives the per-frame
  flush + GUI overlay + present, and keeps the MIDI-loop message (`MM_MCINOTIFY`) via an
  EventManager processor. `platform_window()` returns `ClientEngine::Window()`.
- **`Renderer::initAdopt()`** — builds the canvas + present pipeline on Core's
  device/context/swap chain instead of creating its own.
- **`input_win`** — `input_register_event_processor()` registers a keyboard WNDPROC with
  EventManager (WM_KEYDOWN/UP/CHAR → the kbd backend), replacing the old direct WndProc
  calls.
- **`GuiOverlay`** — trimmed: ClientEngine now does the device/renderer/canvas/font
  bring-up, so the overlay just registers the demo window and does the per-frame
  F1-toggle update + canvas render. `Core::AdoptExisting` (the Phase-5 reverse-adopt)
  is removed as dead.

## Deviations from the pasted wWinMain (intentional)
- Kept `SetCurrentDirectoryW` (assets are opened relative to the CWD; `FileSys` home
  alone is not enough) and `SetProcessDpiAwarenessContext`.
- Added `#include "main.h"` (for `game_main()`) and `<crtdbg.h>`.
- Did **not** import the donor's D3D9 `Direct3DInit`, `SystemInfo`, or `Audio` init.

## Known limitations / verify on Windows (in order)
1. Builds + links: the `winrt::implements`/`make_self<GameApp>` pattern (derived from a
   `GameMain` implements base) compiles; EventManager resolves from NeuronCore.
2. Boots: one window appears, the game runs as before; closing it exits cleanly.
3. Input works (keyboard reaches the game via the EventManager processor); music loops
   (`MM_MCINOTIFY`).
4. **F1** shows the demo GUI window over the game (overlay into the canvas).
5. **Live window resize is not handled** — the swap chain stays at its initial size and
   DXGI scales. Add a WM_SIZE path (Core resize + Renderer rebuild) when needed.

## Running plan
- Phases 1–5: GraphicsCore + ImmediateRenderer + shaders; TextureManager; text +
  Strings; GuiWindow/GuiButton/Canvas; device-unified GUI overlay. ✅
- Phase 6: ClientEngine/GameMain/GameApp bootstrap + EventManager; full window/device
  ownership in the engine (this). ✅
- Follow-on: handle live resize; migrate the game onto the `GameMain` lifecycle
  (`Update`/`RenderScene`/`RenderCanvas`); convert real menus to `GuiWindow`; then
  reimplement the `gfx_*` primitives on `ImmediateRenderer` and retire the legacy
  `gfx_dx11`/`Font` path.
