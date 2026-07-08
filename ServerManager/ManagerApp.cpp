#include "pch.h"

#include "ManagerApp.h"
#include "StatusWindows.h"

#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string>

#include "Canvas.h"
#include "GraphicsCore.h"
#include "NetLib.h"
#include "input_win.h"

namespace DSOManager
{
  namespace
  {
    constexpr const char* CONFIG_FILE = "servermanager.cfg";

    // Parse a dotted-quad "a.b.c.d" into four octets. Returns false if malformed.
    bool ParseIPv4(const std::string& _s, uint8_t _out[4])
    {
      int parts[4] = { 0, 0, 0, 0 };
      int idx = 0, val = 0;
      bool any = false;
      for (char c : _s)
      {
        if (c >= '0' && c <= '9')
        {
          val = val * 10 + (c - '0');
          if (val > 255)
            return false;
          any = true;
        }
        else if (c == '.')
        {
          if (!any || idx >= 3)
            return false;
          parts[idx++] = val;
          val = 0;
          any = false;
        }
        else
        {
          return false;
        }
      }
      if (!any || idx != 3)
        return false;
      parts[3] = val;
      for (int i = 0; i < 4; ++i)
        _out[i] = static_cast<uint8_t>(parts[i]);
      return true;
    }
  }

  void ManagerApp::Startup()
  {
    const int screenW = static_cast<int>(Neuron::Graphics::Core::GetOutputSize().Width);

    // Layout across the 1920x1080 virtual canvas: Connect + Health stack top-left,
    // Players below them, Events fills the right column.
    m_connect = new ConnectWindow(this);
    m_connect->SetName("Connect");
    m_connect->SetTitle("Connection");
    m_connect->SetPosition(24, 24);
    m_connect->SetSize(560, 176);
    LoadConfig();   // apply the saved host/port to the connect window (if any)

    m_health = new HealthWindow(&m_admin);
    m_health->SetName("Health");
    m_health->SetTitle("Server Health");
    m_health->SetPosition(24, 216);
    m_health->SetSize(560, 300);

    m_players = new PlayersWindow(&m_admin);
    m_players->SetName("Players");
    m_players->SetTitle("Connected Players");
    m_players->SetPosition(24, 532);
    m_players->SetSize(560, 520);

    m_events = new EventsWindow(&m_admin);
    m_events->SetName("Events");
    m_events->SetTitle("Event Feed");
    m_events->SetPosition(608, 24);
    m_events->SetSize(screenW - 608 - 24, 1028);

    Canvas::EclRegisterWindow(m_health);
    Canvas::EclRegisterWindow(m_players);
    Canvas::EclRegisterWindow(m_events);
    Canvas::EclRegisterWindow(m_connect);   // last => front (focused for typing)
  }

  void ManagerApp::Shutdown()
  {
    m_admin.Disconnect();
    // The windows are owned by Canvas (deleted in Canvas::Shutdown by ClientEngine).
  }

  void ManagerApp::Update(float /*_deltaSeconds*/)
  {
    m_admin.Pump();

    // Drive window dragging + button clicks from the pointer.
    int mx = 0, my = 0;
    bool lmb = false, rmb = false;
    input_mouse_state(mx, my, lmb, rmb);
    Canvas::EclUpdateMouse(mx, my, lmb, rmb);

    // Route typed characters to the Connect window's active field; Enter connects.
    for (int c = input_take_char(); c != 0; c = input_take_char())
    {
      if (c == '\r' || c == '\n')
        DoConnect();
      else if (m_connect != nullptr)
        m_connect->TypeChar(c);
    }
  }

  void ManagerApp::RenderCanvas()
  {
    ID3D11RenderTargetView* rtv = Neuron::Graphics::Core::GetRenderTargetView();
    if (rtv == nullptr)
      return;
    const int w = static_cast<int>(Neuron::Graphics::Core::GetOutputSize().Width);
    const int h = static_cast<int>(Neuron::Graphics::Core::GetOutputSize().Height);
    Canvas::Start(rtv, w, h);
    Canvas::Render();
    Canvas::End();
  }

  void ManagerApp::DoConnect()
  {
    if (m_connect == nullptr)
      return;

    uint8_t oct[4];
    if (!ParseIPv4(m_connect->Host(), oct))
      return;   // invalid IP: leave the state as-is (the window shows the bad host)
    const int port = std::atoi(m_connect->Port().c_str());
    if (port <= 0 || port > 65535)
      return;

    const Neuron::Net::Endpoint server =
        Neuron::Net::MakeEndpoint(oct[0], oct[1], oct[2], oct[3], static_cast<uint16_t>(port));
    if (m_admin.Connect(server, m_connect->Key()))
      SaveConfig();
  }

  void ManagerApp::DoDisconnect()
  {
    m_admin.Disconnect();
  }

  void ManagerApp::LoadConfig()
  {
    if (m_connect == nullptr)
      return;
    std::ifstream in(CONFIG_FILE);
    if (!in)
      return;
    std::string host = m_connect->Host();
    std::string port = m_connect->Port();
    std::string line;
    while (std::getline(in, line))
    {
      const std::size_t eq = line.find('=');
      if (eq == std::string::npos)
        continue;
      const std::string key = line.substr(0, eq);
      const std::string val = line.substr(eq + 1);
      if (key == "host")
        host = val;
      else if (key == "port")
        port = val;
    }
    m_connect->SetTarget(host, port);
  }

  void ManagerApp::SaveConfig()
  {
    if (m_connect == nullptr)
      return;
    std::ofstream out(CONFIG_FILE, std::ios::trunc);
    if (!out)
      return;
    // Persist only the target address - never the admin key (sm.md §5.2).
    out << "host=" << m_connect->Host() << "\n";
    out << "port=" << m_connect->Port() << "\n";
  }
}
