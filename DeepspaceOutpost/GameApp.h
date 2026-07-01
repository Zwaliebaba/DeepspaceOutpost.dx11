#pragma once

// GameApp - the concrete application object the engine drives.
//
// The game is migrating onto the GameMain lifecycle (Update/RenderScene/RenderCanvas),
// driven once per frame by ClientEngine::Frame (called from gfx_update_screen). RenderCanvas
// already owns the 2D UI (GUI overlay); Update/RenderScene are still stubs while the legacy
// game_main() loop drives gameplay and the legacy gfx_* path fills the scene batch.
// ClientEngine holds one of these (created with winrt::make_self) for its lifecycle dispatch.

#include "GameMain.h"
#include "ClientEngine.h"
#include "GameWindows.h"
#include "SceneMeshes.h"
#include "main.h"

#include "GuiOverlay.h"
#include "Renderer.h"
#include "gfx2d.h" // gfx2d_flush - the game's 2D batch replay

class GameApp : public Neuron::GameMain
{
  public:
    // Register the game's GUI windows (Options/Settings) with the overlay, and wire the
    // ship geometry into the 3D scene renderer (Scene3D builds the GPU meshes lazily).
    void Startup() override
    {
      RegisterGameWindows();
      register_scene_meshes();
    }
    void Shutdown() override {}

    // Per-frame in-flight/docked logic and scene draw. Both no-op unless the game's main
    // loop is active (game_main gates them), so the intro/game-over/mission sequences keep
    // driving their own frames.
    void Update(float _deltaSeconds) override { game_update(); }
    void RenderScene() override { game_render_scene(); }

    // The whole 2D phase: refresh the GUI overlay (input / auto-hide), replay the game's
    // 2D batch (HUD + menus, letterboxed) to the back buffer, then draw the GUI overlay
    // (windows/menus, client-space) on top. Returns whether anything was painted - an idle
    // frame (empty batch, overlay hidden) paints nothing and is left unpresented so the
    // previous frame persists. The two 2D layers are separate Canvas passes (the game HUD
    // is native-centred 512x514; the overlay is full-window client pixels).
    bool RenderCanvas() override
    {
      GuiOverlay::Update();
      const bool overlayShown = GuiOverlay::IsShown();
      const bool painted = gfx2d_flush(overlayShown);
      if (painted)
      {
        if (Renderer* r = platform_renderer())
          GuiOverlay::Render(r->clientWidth(), r->clientHeight());
      }
      return painted;
    }
};
