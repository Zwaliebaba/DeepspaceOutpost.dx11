#pragma once

// GUI overlay integration (Phase 5).
//
// Makes the imported GraphicsCore / ImmediateRenderer / text / GuiWindow stack LIVE
// without disturbing the existing game render path. It:
//   - unifies the device (Neuron::Graphics::Core adopts the platform Renderer's
//     device/context/swap chain), then initialises ImmediateRenderer + fonts +
//     Strings + Canvas;
//   - renders registered GuiWindows as an overlay INTO the existing 512x514 canvas
//     (after the game's 2D batch, before the letterboxed present);
//   - is fail-safe (best-effort Startup guarded by try/catch) and opt-in (toggled
//     with F1, hidden by default) so the normal game is unaffected if the new path
//     misbehaves.
//
// Fully porting every gfx_* primitive onto ImmediateRenderer and retiring
// Renderer/gfx_dx11/Font is a separate, incremental follow-on (see
// docs/phase5-graphicscore-live.md).

namespace GuiOverlay
{
  void Startup();                          // best-effort; safe to call once after the Renderer is up
  void Shutdown();
  bool IsReady();

  void Update();                           // F1 toggle + keyboard menu navigation
  void Render(int canvasWidth, int canvasHeight); // draw into the currently-bound canvas
}
