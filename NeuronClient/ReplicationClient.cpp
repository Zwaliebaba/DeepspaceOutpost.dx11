#include "pch.h"

#include "ReplicationClient.h"

#include "GameEvents.h"
#include "GalaxyManifest.h"
#include "Messages/Framing.h"

namespace Neuron::Client
{
  namespace
  {
    // Largest datagram we expect (a single MTU-bounded snapshot packet, with a
    // little slack); anything bigger is a malformed packet and is discarded.
    constexpr int kRecvBufferSize = 2048;

    // Cap datagrams processed per Pump() so a flood can never stall the frame.
    constexpr int kMaxDrainPerPump = 256;
  }

  bool ReplicationClient::Open(uint16_t _port)
  {
    Close();
    if (!Net::NetStartup())
      return false;
    if (!m_socket.Open(_port))
    {
      Net::NetShutdown();
      return false;
    }
    m_open = true;
    return true;
  }

  void ReplicationClient::Close()
  {
    if (m_open)
    {
      m_socket.Close();
      Net::NetShutdown();
      m_open = false;
    }
  }

  void ReplicationClient::Pump()
  {
    if (!m_open)
      return;

    uint8_t buffer[kRecvBufferSize];
    Net::Endpoint from;

    for (int i = 0; i < kMaxDrainPerPump; ++i)
    {
      const int got = m_socket.RecvFrom(buffer, sizeof(buffer), from);
      if (got <= 0)
        break;   // 0 = nothing pending, <0 = error: stop draining this frame

      // Learn/refresh the server's address from whatever it sends us.
      m_server = from;
      m_haveServer = true;

      // One socket carries both streams; route each datagram by its magic.
      const std::size_t size = static_cast<std::size_t>(got);
      switch (Net::PeekMagic(buffer, size))
      {
        case Net::SNAPSHOT_MAGIC:
          m_interp.Apply(buffer, size);        // unreliable bulk state
          break;
        case Net::EVENT_MAGIC:
          m_events.ReadPacket(buffer, size);   // reliable ordered events
          break;
        default:
          break;   // unknown/foreign datagram: ignore
      }
    }

    // Drain reliable events: AssignPlayer is the handshake and GalaxyManifest is
    // the chart data (both consumed here); the rest are queued for the application
    // via PollEvent().
    Net::ReliableMessage msg;
    while (m_events.Receive(msg))
    {
      uint32_t playerId = 0;
      uint32_t total = 0;
      uint32_t base = 0;
      if (Net::DecodeAssignPlayer(msg, playerId))
        m_localPlayer = playerId;
      else if (Net::DecodeManifestChunk(msg, total, base, m_galaxy))
        m_galaxy.reserve(total);   // chunks arrive in order; just accumulate
      else
        m_appEvents.push_back(std::move(msg));
    }

    // Send our cumulative ack back so the server stops resending delivered
    // events (the packet also carries any client->server reliable messages).
    if (m_haveServer)
    {
      const std::vector<uint8_t> ack = m_events.WritePacket();
      m_socket.SendTo(m_server, ack.data(), ack.size());
    }
  }

  void ReplicationClient::SendInput(const Net::ClientInput& _input)
  {
    if (!m_open || !m_haveServer)
      return;

    // The intent rides the unified 'NMSG' unreliable lane as one InputCommand
    // record (replacing the old bespoke 'NCMD' packet).
    Msg::PacketWriter writer(Msg::MessageLane::Unreliable);
    writer.Add(_input);
    m_socket.SendTo(m_server, writer.Bytes().data(), writer.Size());
  }

  bool ReplicationClient::PollEvent(Net::ReliableMessage& _out)
  {
    if (m_appEvents.empty())
      return false;
    _out = std::move(m_appEvents.front());
    m_appEvents.pop_front();
    return true;
  }

  ReplicationClient& ReplicationClientInstance()
  {
    static ReplicationClient instance;
    return instance;
  }
}
