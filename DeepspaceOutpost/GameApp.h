#pragma once

// GameApp - the concrete application object the engine drives.
//
// For now this is a dummy: the legacy game still runs through game_main(), so these
// GameMain hooks are stubs. As the game migrates onto the GameMain lifecycle
// (Update/RenderScene/RenderCanvas), they get filled in. ClientEngine holds one of
// these (created with winrt::make_self) for its window/lifecycle dispatch.

#include "GameMain.h"
#include "ClientEngine.h"

class GameApp : public Neuron::GameMain
{
  public:
    void Startup() override {}
    void Shutdown() override {}
    void Update(float _deltaSeconds) override {}
    void RenderScene() override {}
    void RenderCanvas() override {}
};
