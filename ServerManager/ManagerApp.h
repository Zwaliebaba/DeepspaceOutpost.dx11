#pragma once

// ManagerApp - the ServerManager application object the engine drives (sm.md §5.2).
//
// A Neuron::GameMain (so ClientEngine can hold it as a com_ptr and drive its per-frame
// lifecycle) that owns the AdminClient and the four dashboard windows. There is no 3D
// scene: Update() pumps the network + feeds input, and RenderCanvas() draws the GUI.

#include <string>

#include "GameMain.h"
#include "AdminClient.h"

namespace DSOManager
{
  class ConnectWindow;
  class PlayersWindow;
  class EventsWindow;
  class HealthWindow;

  class ManagerApp : public Neuron::GameMain
  {
  public:
    void Startup() override;
    void Shutdown() override;
    void Update(float _deltaSeconds) override;
    void RenderScene() override {}          // no 3D scene
    void RenderCanvas() override;

    // Actions invoked by the Connect window's buttons.
    void DoConnect();
    void DoDisconnect();

    [[nodiscard]] Neuron::Client::AdminClient& Client() { return m_admin; }

  private:
    void LoadConfig();      // read host/port from servermanager.cfg next to the exe
    void SaveConfig();      // persist host/port (never the key) after a connect

    Neuron::Client::AdminClient m_admin;

    // Owned by Canvas once registered (Canvas deletes them on Shutdown); kept as raw
    // pointers so we can read/feed them each frame.
    ConnectWindow* m_connect = nullptr;
    PlayersWindow* m_players = nullptr;
    EventsWindow*  m_events = nullptr;
    HealthWindow*  m_health = nullptr;
  };
}
