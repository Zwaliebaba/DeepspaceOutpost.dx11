#pragma once

// GUI overlay integration (Phase 5).
//
// Makes the imported GraphicsCore / Render2D / text / GuiWindow stack LIVE without
// disturbing the existing game render path. It:
//   - unifies the device (Neuron::Graphics::Core adopts the platform Renderer's
//     device/context/swap chain), then initialises Render2D + fonts + Strings + Canvas;
//   - renders registered GuiWindows as a full-window overlay on the back buffer (after
//     the game's 2D canvas has been blitted, before the present);
//   - is fail-safe (best-effort Startup guarded by try/catch) and hidden by default;
//     the game opens it on demand (F8 market, F11 options) so the normal game is
//     unaffected if the new path misbehaves.
//
// The game's gfx_* 2D batch (gfx2d) also renders through Render2D, so the overlay and
// the game share one 2D layer; Renderer remains only for the off-screen canvas +
// letterboxed present.

#include <functional>
#include <string_view>

class GuiWindow;

namespace GuiOverlay
{
  void Startup();                          // best-effort; safe to call once after the Renderer is up
  void Shutdown();
  bool IsReady();

  void Update();                           // auto-hide when empty + keyboard/mouse nav
  void Render(int canvasWidth, int canvasHeight); // draw into the currently-bound canvas

  // Show the overlay and open the Options window directly (the game's in-game options
  // entry, e.g. F11, routes here instead of drawing a legacy gfx_display_* screen).
  void Open();
  bool IsShown();

  // Show the overlay and open (or focus, if already present) a game-supplied window.
  // The factory is only invoked when the named window isn't already registered; the
  // returned window (Canvas takes ownership) must carry that same name. Used to route
  // other in-game screens (e.g. F8 market) onto the GUI stack.
  void ShowWindow(std::string_view _name, const std::function<GuiWindow*()>& _factory);

  // The game supplies the real Options/Settings window: it wires controls to game
  // state (config globals, save), which this engine layer can't see. The factory must
  // return a heap GuiWindow named "Options" (Canvas takes ownership). When unset, a
  // built-in placeholder window is shown instead. Set from GameApp::Startup().
  void SetOptionsWindowFactory(std::function<GuiWindow*()> _factory);
}
