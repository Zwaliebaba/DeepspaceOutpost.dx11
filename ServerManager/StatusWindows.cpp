#include "pch.h"

#include "StatusWindows.h"
#include "ManagerApp.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>

#include "Render2D.h"
#include "TextRenderer.h"

using Neuron::Graphics::Render2D;
using Neuron::Client::AdminClient;
namespace Msg = Neuron::Msg;

namespace DSOManager
{
  namespace
  {
    constexpr float LINE = 20.0f;   // row height
    constexpr float FONT = 14.0f;

    // Draw one left-aligned line at (x,y) in the given colour; return the next y.
    float Line(float _x, float _y, const std::string& _text, uint8_t _r, uint8_t _g, uint8_t _b)
    {
      g_gameFont.SetColor(_r, _g, _b, 255);
      g_gameFont.DrawText2DSimple(_x, _y, FONT, _text);
      return _y + LINE;
    }

    std::string FormatIPv4(uint32_t _addrHostOrder, uint16_t _port)
    {
      char buf[32];
      std::snprintf(buf, sizeof(buf), "%u.%u.%u.%u:%u",
                    (_addrHostOrder >> 24) & 0xFF, (_addrHostOrder >> 16) & 0xFF,
                    (_addrHostOrder >> 8) & 0xFF, _addrHostOrder & 0xFF, _port);
      return buf;
    }

    const char* StateText(AdminClient::State _s)
    {
      switch (_s)
      {
        case AdminClient::State::Disconnected: return "Disconnected";
        case AdminClient::State::Connecting:   return "Connecting...";
        case AdminClient::State::Connected:    return "Connected";
        case AdminClient::State::Rejected:     return "Rejected";
        case AdminClient::State::TimedOut:     return "Timed out";
      }
      return "?";
    }

    const char* RejectText(Msg::AdminRejectReason _r)
    {
      switch (_r)
      {
        case Msg::AdminRejectReason::ProtocolMismatch: return "protocol mismatch";
        case Msg::AdminRejectReason::BadKey:           return "bad admin key";
        case Msg::AdminRejectReason::ServerBusy:       return "server busy (admin cap)";
      }
      return "?";
    }

    const char* PlayerStateText(uint8_t _s)
    {
      switch (static_cast<Msg::AdminPlayerState>(_s))
      {
        case Msg::AdminPlayerState::Live:    return "live";
        case Msg::AdminPlayerState::Loading: return "loading";
        case Msg::AdminPlayerState::Pending: return "pending";
      }
      return "?";
    }
  }

  // ---- ConnectWindow --------------------------------------------------------

  ConnectWindow::ConnectWindow(ManagerApp* _app)
    : GuiWindow("Connect"), m_app(_app)
  {
  }

  void ConnectWindow::Create()
  {
    // Field-focus buttons (clicking selects which field the keyboard edits).
    auto* host = new CallbackButton();
    host->SetProperties("fHost", 10, 30, 70, 18, "Host", "Edit the server IP");
    host->onClick = [this] { m_active = Field::Host; };
    RegisterButton(host);

    auto* port = new CallbackButton();
    port->SetProperties("fPort", 10, 52, 70, 18, "Port", "Edit the admin port");
    port->onClick = [this] { m_active = Field::Port; };
    RegisterButton(port);

    auto* key = new CallbackButton();
    key->SetProperties("fKey", 10, 74, 70, 18, "Key", "Edit the admin key");
    key->onClick = [this] { m_active = Field::Key; };
    RegisterButton(key);

    auto* connect = new CallbackButton();
    connect->SetProperties("connect", 380, 110, 80, 22, "Connect", "Connect to the server");
    connect->onClick = [this] { m_app->DoConnect(); };
    RegisterButton(connect);

    auto* disconnect = new CallbackButton();
    disconnect->SetProperties("disconnect", 468, 110, 82, 22, "Disconnect", "Close the connection");
    disconnect->onClick = [this] { m_app->DoDisconnect(); };
    RegisterButton(disconnect);
  }

  void ConnectWindow::TypeChar(int _ch)
  {
    std::string* field = (m_active == Field::Host) ? &m_host
                       : (m_active == Field::Port) ? &m_port : &m_key;
    if (_ch == 8)   // backspace
    {
      if (!field->empty())
        field->pop_back();
      return;
    }
    if (_ch >= 32 && _ch < 127)   // printable ASCII only
      field->push_back(static_cast<char>(_ch));
  }

  void ConnectWindow::Render(bool _hasFocus)
  {
    GuiWindow::Render(_hasFocus);   // frame, title, field/action buttons

    const AdminClient& c = m_app->Client();
    const float vx = m_x + 90.0f;   // field value column (right of the label buttons)
    auto fieldColour = [&](Field _f, uint8_t& _r, uint8_t& _g, uint8_t& _b)
    { if (m_active == _f) { _r = 255; _g = 240; _b = 120; } else { _r = 220; _g = 220; _b = 220; } };

    uint8_t r, g, b;
    fieldColour(Field::Host, r, g, b);
    Line(vx, m_y + 32.0f, m_host + ((m_active == Field::Host) ? "_" : ""), r, g, b);
    fieldColour(Field::Port, r, g, b);
    Line(vx, m_y + 54.0f, m_port + ((m_active == Field::Port) ? "_" : ""), r, g, b);
    fieldColour(Field::Key, r, g, b);
    Line(vx, m_y + 76.0f, std::string(m_key.size(), '*') + ((m_active == Field::Key) ? "_" : ""), r, g, b);

    // State line, colour-coded.
    const AdminClient::State st = c.GetState();
    uint8_t sr = 220, sg = 220, sb = 220;
    if (st == AdminClient::State::Connected) { sr = 120; sg = 230; sb = 120; }
    else if (st == AdminClient::State::Rejected || st == AdminClient::State::TimedOut) { sr = 235; sg = 90; sb = 90; }
    else if (st == AdminClient::State::Connecting) { sr = 240; sg = 220; sb = 120; }

    std::string stateLine = std::string("State: ") + StateText(st);
    if (st == AdminClient::State::Rejected)
      stateLine += std::string(" (") + RejectText(c.RejectReason()) + ")";
    Line(m_x + 10.0f, m_y + 100.0f, stateLine, sr, sg, sb);

    if (st == AdminClient::State::Connected)
    {
      char buf[128];
      std::snprintf(buf, sizeof(buf), "Server: GameLogic v%u  tick %u ms  game port %u",
                    c.ServerIdentity().gameLogicVersion, c.ServerIdentity().tickRateMs,
                    c.ServerIdentity().gamePort);
      Line(m_x + 10.0f, m_y + 148.0f, buf, 180, 200, 230);
    }
  }

  // ---- PlayersWindow --------------------------------------------------------

  PlayersWindow::PlayersWindow(const AdminClient* _client)
    : GuiWindow("Players"), m_client(_client)
  {
  }

  void PlayersWindow::Render(bool _hasFocus)
  {
    GuiWindow::Render(_hasFocus);

    const auto& roster = m_client->Roster();

    float y = m_y + 30.0f;
    const float x = m_x + 10.0f;

    char hdr[160];
    std::snprintf(hdr, sizeof(hdr), "%-16s %-5s %-22s %-5s %-7s %s", "NAME", "ID", "ADDRESS", "RTT", "SCORE", "STATE");
    y = Line(x, y, hdr, 150, 180, 210);

    if (roster.empty())
    {
      Line(x, y, "(no players connected)", 170, 170, 170);
      return;
    }

    const float bottom = m_y + m_h - 8.0f;
    for (const auto& kv : roster)
    {
      if (y > bottom)
        break;
      const Msg::AdminPlayerInfo& p = kv.second;
      char row[192];
      std::snprintf(row, sizeof(row), "%-16.16s %-5u %-22s %-5u %-7d %s",
                    p.name.c_str(), p.playerId,
                    FormatIPv4(p.address, p.port).c_str(), p.rttMs, p.score,
                    PlayerStateText(p.state));
      y = Line(x, y, row, 225, 225, 225);
    }
  }

  // ---- EventsWindow ---------------------------------------------------------

  EventsWindow::EventsWindow(const AdminClient* _client)
    : GuiWindow("Events"), m_client(_client)
  {
  }

  void EventsWindow::Render(bool _hasFocus)
  {
    GuiWindow::Render(_hasFocus);

    const std::deque<Msg::AdminEvent>& events = m_client->Events();
    const float x = m_x + 10.0f;
    const float top = m_y + 30.0f;
    const float bottom = m_y + m_h - 8.0f;

    const int capacity = std::max(1, static_cast<int>((bottom - top) / LINE));
    const int count = static_cast<int>(events.size());
    const int first = std::max(0, count - capacity);   // show the newest that fit

    float y = top;
    for (int i = first; i < count; ++i)
    {
      const Msg::AdminEvent& e = events[static_cast<std::size_t>(i)];
      uint8_t r = 220, g = 220, b = 220;
      switch (static_cast<Msg::AdminEventKind>(e.kind))
      {
        case Msg::AdminEventKind::Kill:              r = 235; g = 100; b = 100; break;
        case Msg::AdminEventKind::PlayerJoined:      r = 120; g = 220; b = 120; break;
        case Msg::AdminEventKind::PlayerReconnected: r = 120; g = 220; b = 160; break;
        case Msg::AdminEventKind::PlayerLeft:        r = 200; g = 160; b = 120; break;
        case Msg::AdminEventKind::Crime:             r = 235; g = 170; b = 90;  break;
        case Msg::AdminEventKind::TickOverrun:       r = 240; g = 220; b = 90;  break;
        case Msg::AdminEventKind::ServerStarted:     r = 160; g = 190; b = 240; break;
        case Msg::AdminEventKind::Chat:              r = 220; g = 220; b = 220; break;
      }
      char row[320];
      std::snprintf(row, sizeof(row), "[%u] %s", e.tick, e.text.c_str());
      y = Line(x, y, row, r, g, b);
    }

    if (count == 0)
      Line(x, top, "(no events yet)", 170, 170, 170);
  }

  // ---- HealthWindow ---------------------------------------------------------

  HealthWindow::HealthWindow(const AdminClient* _client)
    : GuiWindow("Health"), m_client(_client)
  {
  }

  void HealthWindow::Render(bool _hasFocus)
  {
    GuiWindow::Render(_hasFocus);

    const float x = m_x + 10.0f;
    float y = m_y + 30.0f;

    const AdminClient::State st = m_client->GetState();
    if (st == AdminClient::State::TimedOut)
      y = Line(x, y, "!! No data - server silent or unreachable", 235, 90, 90);

    if (!m_client->HasHealth())
    {
      Line(x, y, (st == AdminClient::State::Connected) ? "Waiting for first health sample..."
                                                       : "Not connected.", 180, 180, 180);
      return;
    }

    const Msg::AdminHealth& h = m_client->LatestHealth();
    char buf[128];

    std::snprintf(buf, sizeof(buf), "Uptime:   %u s", h.uptimeSeconds);
    y = Line(x, y, buf, 220, 220, 220);
    std::snprintf(buf, sizeof(buf), "Sessions: %u    Entities: %u", h.sessions, h.entities);
    y = Line(x, y, buf, 220, 220, 220);
    std::snprintf(buf, sizeof(buf), "Tick avg: %.2f ms   max: %.2f ms (budget %u ms)",
                  h.avgTickMs, h.maxTickMs, m_client->ServerIdentity().tickRateMs);
    const bool hot = h.maxTickMs > static_cast<float>(m_client->ServerIdentity().tickRateMs);
    y = Line(x, y, buf, hot ? 240 : 200, hot ? 200 : 230, hot ? 90 : 200);

    // Tick-time meter: avg (green) over the budget bar, max marker.
    const float budgetMs = std::max(1.0f, static_cast<float>(m_client->ServerIdentity().tickRateMs));
    const float barX = x, barY = y + 2.0f, barW = m_w - 24.0f, barH = 12.0f;
    Render2D::FillRect(barX, barY, barX + barW, barY + barH, Render2D::Rgba(40, 40, 48, 255));
    const float avgFrac = std::min(1.0f, h.avgTickMs / budgetMs);
    Render2D::FillRect(barX, barY, barX + barW * avgFrac, barY + barH, Render2D::Rgba(90, 210, 110, 255));
    const float maxFrac = std::min(1.0f, h.maxTickMs / budgetMs);
    const float mx = barX + barW * maxFrac;
    Render2D::FillRect(mx - 1.0f, barY - 2.0f, mx + 1.0f, barY + barH + 2.0f, Render2D::Rgba(240, 220, 90, 255));
    y = barY + barH + 8.0f;

    std::snprintf(buf, sizeof(buf), "Overruns: %u", h.overruns);
    y = Line(x, y, buf, h.overruns > 0 ? 240 : 200, h.overruns > 0 ? 200 : 220, 120);
    std::snprintf(buf, sizeof(buf), "Send:     %llu B/s   dropped: %llu",
                  static_cast<unsigned long long>(h.bytesPerSecond),
                  static_cast<unsigned long long>(h.droppedEntities));
    Line(x, y, buf, 200, 210, 225);
  }
}
