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
    // (windows/menus, client-space) on top. Every screen redraws every frame, so this always
    // paints and returns true -> the frame is presented. The one exception is a paused game:
    // it draws nothing, so we return false and the caller does not present, leaving the last
    // frame on screen (FLIP_DISCARD keeps no retained content). The two 2D layers are separate
    // Canvas passes (the game HUD is native-centred 512x514; the overlay is full-window pixels).
    bool RenderCanvas() override
    {
      GuiOverlay::Update();

      // Paused with nothing else to draw: keep the last presented frame on screen (don't
      // present). If a GUI overlay window is up it must keep compositing/animating, so still
      // present in that case (matches the old forcePresent=overlayShown behaviour).
      if (game_paused && !GuiOverlay::IsShown())
        return false;

      gfx2d_flush();
      if (Renderer* r = platform_renderer())
        GuiOverlay::Render(r->clientWidth(), r->clientHeight());
      return true;
    }
};
