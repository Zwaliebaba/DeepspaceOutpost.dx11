#include "pch.h"
#include "HudRender.h"

#include "Render2D.h"
#include "TextRenderer.h"
#include "GraphicsCore.h"

#include <d3d11.h>

using Neuron::Graphics::Render2D;

// Render-free game-state accessors (defined in the legacy TUs) so this winrt TU stays off
// the game headers, mirroring the market_*/ChartData pattern.
extern bool game_client_connected();

// The in-flight cockpit dashboard + I2/I3/I4 overlays (space.cpp). It draws straight into
// the open Render2D pass and gates itself (only while flying), so it is safe to call every
// frame: nothing draws when docked / in menus / disconnected.
extern void update_console();

// Centred overlay text queued during RenderScene (intro titles/prompts, the flight info
// message, GAME OVER). Drawn natively here; the queue is empty in states that emit none.
extern void RenderOverlayText();

// Scene overlays queued during RenderScene: the ship-death debris points, the target
// reticle and the intro title sprite. Drawn first (under the dashboard + text), matching
// the old batch-flush-under-HUD order.
extern void RenderSceneOverlays();

void RenderGameHud()
{
  ID3D11RenderTargetView* rtv = Neuron::Graphics::Core::GetRenderTargetView();
  const auto sz = Neuron::Graphics::Core::GetOutputSize();
  const int w = static_cast<int>(sz.Width);
  const int h = static_cast<int>(sz.Height);
  if (!rtv || w <= 0 || h <= 0)
    return;

  // One full-window client-pixel pass (like the GUI overlay). Point sampling keeps the
  // bitmap font / sprites crisp, matching the old gfx2d flush.
  Render2D::Begin(rtv, w, h, 0, 0, 1.0f, D3D11_FILTER_MIN_MAG_MIP_POINT);

  // --- Connection-lost banner (first HUD element off gfx2d) -----------------
  // Shown while the socket is down; ensure_connection keeps retrying, and there is no world
  // to render meanwhile (main's game_render_flight early-returns).
  if (!game_client_connected())
  {
    g_gameFont.SetRenderShadow(true);
    g_gameFont.SetColor(230, 180, 40, 255);
    g_gameFont.DrawText2DCenter(w / 2.0f, h / 2.0f - 10.0f, 14, "CONNECTION LOST - RECONNECTING");
    g_gameFont.SetRenderShadow(false);
  }

  // Scene overlays (debris / reticle / intro sprite) go under the dashboard and text.
  RenderSceneOverlays();

  // The cockpit dashboard + flight overlays (self-gated: draws only while flying).
  update_console();

  // Centred overlay text queued from the scene phase (intro / message / GAME OVER).
  RenderOverlayText();

  Render2D::End();
}
