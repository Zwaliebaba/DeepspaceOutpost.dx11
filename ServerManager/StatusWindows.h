#pragma once

// StatusWindows - the ServerManager's dashboard windows (sm.md §5.2).
//
// Four GuiWindow subclasses drawn on the shared Canvas 2D layer, each rendering its
// content in its Render() override via Neuron::Graphics::Render2D + the GUI fonts
// (the same pattern the game's own windows use). They read a live mirror straight off
// the AdminClient; only the Connect window has controls (field buttons + Connect /
// Disconnect), driven by mouse clicks through GuiButton::MouseUp.

#include <functional>
#include <string>

#include "GuiWindow.h"
#include "GuiButton.h"
#include "AdminClient.h"

namespace DSOManager
{
  class ManagerApp;

  // A button that runs an arbitrary callback on mouse-up (the GUI has no generic
  // action button; the game's are all bespoke subclasses).
  class CallbackButton : public GuiButton
  {
  public:
    std::function<void()> onClick;
    void MouseUp() override { if (onClick) onClick(); }
  };

  // Connect: shows/edits the target host, port and admin key, the connection state,
  // and the server identity once connected. Typed characters (routed from ManagerApp)
  // edit the active field; the Connect/Disconnect buttons drive the AdminClient.
  class ConnectWindow : public GuiWindow
  {
  public:
    ConnectWindow(ManagerApp* _app);
    void Create() override;                 // build the field + action buttons (no close box)
    void Update() override {}               // input is driven by ManagerApp, not menu nav
    void Render(bool _hasFocus) override;

    // Feed one typed character to the active field (append, or backspace on 0x08).
    void TypeChar(int _ch);

    enum class Field { Host, Port, Key };
    void SetActiveField(Field _f) { m_active = _f; }

    [[nodiscard]] const std::string& Host() const { return m_host; }
    [[nodiscard]] const std::string& Port() const { return m_port; }
    [[nodiscard]] const std::string& Key() const { return m_key; }
    void SetTarget(const std::string& _host, const std::string& _port) { m_host = _host; m_port = _port; }

  private:
    ManagerApp* m_app;
    std::string m_host = "127.0.0.1";
    std::string m_port = "40001";
    std::string m_key;
    Field m_active = Field::Host;
  };

  // Players: the connected-player roster as text rows (name, id, address, rtt, score,
  // state, connected-for), row count in the title.
  class PlayersWindow : public GuiWindow
  {
  public:
    explicit PlayersWindow(const Neuron::Client::AdminClient* _client);
    void Create() override {}
    void Update() override {}
    void Render(bool _hasFocus) override;
  private:
    const Neuron::Client::AdminClient* m_client;
  };

  // Events: the scrolling event feed, newest at the bottom, kind-coloured.
  class EventsWindow : public GuiWindow
  {
  public:
    explicit EventsWindow(const Neuron::Client::AdminClient* _client);
    void Create() override {}
    void Update() override {}
    void Render(bool _hasFocus) override;
  private:
    const Neuron::Client::AdminClient* m_client;
  };

  // Health: the latest health sample as labelled values + tick-time meters, with a
  // banner when the server went silent or overruns grew.
  class HealthWindow : public GuiWindow
  {
  public:
    explicit HealthWindow(const Neuron::Client::AdminClient* _client);
    void Create() override {}
    void Update() override {}
    void Render(bool _hasFocus) override;
  private:
    const Neuron::Client::AdminClient* m_client;
  };
}
