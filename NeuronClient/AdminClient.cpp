#include "pch.h"

#include "AdminClient.h"

#include <chrono>
#include <utility>

#include "Messages/Framing.h"    // Msg::PROTOCOL_VERSION
#include "Messages/Reliable.h"   // Msg::TryDecode

namespace Neuron::Client
{
  namespace
  {
    constexpr int RECV_BUFFER_SIZE = 2048;
    constexpr int MAX_DRAIN_PER_PUMP = 256;

    // Monotonic wall clock in ms (presentation timing only - the manager renders and
    // times out on real time; the server owns all authoritative clocks).
    [[nodiscard]] double NowMs()
    {
      using namespace std::chrono;
      return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }
  }

  bool AdminClient::Connect(const Net::Endpoint& _server, const std::string& _key)
  {
    Disconnect();
    if (!Net::NetStartup())
      return false;
    if (!m_socket.Open(0))   // ephemeral local port; a client only needs to send + recv replies
    {
      Net::NetShutdown();
      return false;
    }
    m_open = true;
    m_server = _server;
    m_haveServer = true;
    BeginHandshake(_key, NowMs());
    return true;
  }

  void AdminClient::Disconnect()
  {
    if (m_open)
    {
      m_socket.Close();
      Net::NetShutdown();
      m_open = false;
    }
    m_haveServer = false;
    m_state = State::Disconnected;
  }

  void AdminClient::BeginHandshake(const std::string& _key, double _nowMs)
  {
    m_key = _key;
    m_events = Msg::MessageEndpoint{};   // fresh reliable sequence/ack state
    m_token = 0;
    ResetMirror();
    m_state = State::Connecting;
    m_connectStartMs = _nowMs;
    m_lastServerMs = _nowMs;
    m_events.Send(Msg::AdminHello{ Msg::PROTOCOL_VERSION, _key });   // Control lane
  }

  void AdminClient::ResetMirror()
  {
    m_roster.clear();
    m_eventLog.clear();
    m_healthLog.clear();
    m_identity = Msg::AdminHelloAck{};
  }

  bool AdminClient::Ingest(const uint8_t* _data, std::size_t _size, double _nowMs)
  {
    if (!m_events.OnDatagram(_data, _size))
      return false;   // foreign/malformed: not ours
    m_lastServerMs = _nowMs;   // any valid datagram from the server is liveness
    return true;
  }

  void AdminClient::Service(double _nowMs)
  {
    Net::ReliableMessage msg;
    while (m_events.Receive(msg))
    {
      m_lastServerMs = _nowMs;

      Msg::AdminHelloAck ack;
      Msg::AdminHelloReject reject;
      Msg::AdminPlayerInfo row;
      Msg::AdminPlayerGone gone;
      Msg::AdminEvent ev;
      Msg::AdminHealth health;
      if (Msg::TryDecode(msg, ack))
      {
        // Authenticated: adopt the admin token (stamped on every later datagram) and
        // the identity block the header bar shows.
        m_identity = ack;
        m_token = ack.adminToken;
        m_events.SetToken(ack.adminToken);
        m_state = State::Connected;
      }
      else if (Msg::TryDecode(msg, reject))
      {
        m_rejectReason = static_cast<Msg::AdminRejectReason>(reject.reason);
        m_state = State::Rejected;
      }
      else if (Msg::TryDecode(msg, row))
      {
        m_roster[row.playerId] = row;
      }
      else if (Msg::TryDecode(msg, gone))
      {
        m_roster.erase(gone.playerId);
      }
      else if (Msg::TryDecode(msg, ev))
      {
        m_eventLog.push_back(ev);
        if (m_eventLog.size() > MAX_EVENTS)
          m_eventLog.pop_front();
      }
      else if (Msg::TryDecode(msg, health))
      {
        m_healthLog.push_back(health);
        if (m_healthLog.size() > MAX_HEALTH)
          m_healthLog.pop_front();
      }
      // else: an unknown/foreign message id - ignore (forward compatibility).
    }

    // Timeout state machine: a rejected/disconnected link is terminal until the app
    // reconnects; otherwise a stalled handshake or a silent server trips TimedOut.
    if (m_state == State::Connecting && _nowMs - m_connectStartMs > CONNECT_TIMEOUT_MS)
      m_state = State::TimedOut;
    else if (m_state == State::Connected && _nowMs - m_lastServerMs > LIVE_TIMEOUT_MS)
      m_state = State::TimedOut;
  }

  void AdminClient::Pump()
  {
    if (!m_open)
      return;

    const double now = NowMs();
    uint8_t buffer[RECV_BUFFER_SIZE];
    Net::Endpoint from;
    for (int i = 0; i < MAX_DRAIN_PER_PUMP; ++i)
    {
      const int got = m_socket.RecvFrom(buffer, sizeof(buffer), from);
      if (got <= 0)
        break;
      if (Ingest(buffer, static_cast<std::size_t>(got), now))
      {
        m_server = from;   // learn/refresh the server's address from a valid datagram
        m_haveServer = true;
      }
    }

    Service(now);

    if (m_haveServer)
      for (const std::vector<uint8_t>& dg : TakeOutgoing())
        m_socket.SendTo(m_server, dg.data(), dg.size());
  }
}
