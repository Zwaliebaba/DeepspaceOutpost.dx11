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
#include "HudRender.h" // RenderGameHud - the native Render2D HUD pass (replaced the gfx2d batch)

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

    // Scene hook: the game records its 2D HUD/menu batch and draws its 3D scene itself. At the
    // end of its world draw it submits the models (Scene3D::SubmitModel) and calls
    // gfx_render_3d_scene(), which draws the depth-tested 3D pass onto the (already-cleared) back
    // buffer. RenderCanvas then composites the 2D over it. The game drives the 3D pass directly -
    // there is no scene-marker flag or separate render-scene hook.
    void RenderScene() override { game_render_scene(); }

    // The whole 2D phase: refresh the GUI overlay (input / auto-hide), draw the native HUD
    // pass (flight dashboard, overlays, scene overlays, centred text), then draw the GUI
    // overlay (windows/menus) on top. Every screen redraws every frame, so this always paints
    // and the caller always presents (FLIP_DISCARD keeps no retained content). The gfx2d 2D
    // batch is gone - all 2D is native Render2D now.
    void RenderCanvas() override
    {
      GuiOverlay::Update();
      RenderGameHud();     // the native Render2D HUD pass (replaced the gfx2d batch)
      if (Renderer* r = platform_renderer())
        GuiOverlay::Render(r->clientWidth(), r->clientHeight());
    }
};
