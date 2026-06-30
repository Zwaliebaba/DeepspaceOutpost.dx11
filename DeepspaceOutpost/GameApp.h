#pragma once

// GameApp - the concrete application object the engine drives.
//
// The game is migrating onto the GameMain lifecycle (Update/RenderScene/RenderCanvas),
// driven once per frame by ClientEngine::Tick (called from gfx_update_screen). RenderCanvas
// already owns the 2D UI (GUI overlay); Update/RenderScene are still stubs while the legacy
// game_main() loop drives gameplay and the legacy gfx_* path fills the scene batch.
// ClientEngine holds one of these (created with winrt::make_self) for its lifecycle dispatch.

#include "GameMain.h"
#include "ClientEngine.h"
#include "GameWindows.h"

#include "GuiOverlay.h"
#include "Renderer.h"

class GameApp : public Neuron::GameMain
{
  public:
    // Register the game's GUI windows (Options/Settings) with the overlay.
    void Startup() override { RegisterGameWindows(); }
    void Shutdown() override {}

    void Update(float _deltaSeconds) override {}
    void RenderScene() override {}

    // 2D UI on top of the scene: the GUI overlay (windows/menus), full-window in client
    // pixels. No-op unless the overlay is shown.
    void RenderCanvas() override
    {
      if (Renderer* r = platform_renderer())
        GuiOverlay::Render(r->clientWidth(), r->clientHeight());
    }
};
