#include "pch.h"

#include "ReplicationClient.h"

#include <chrono>

#include "GalaxyManifest.h"
#include "Messages/Framing.h"
#include "Messages/Reliable.h"
#include "Messages/Defs/CoreEvents.h"

namespace Neuron::Client
{
  namespace
  {
    // Largest datagram we expect (a single MTU-bounded snapshot packet, with a
    // little slack); anything bigger is a malformed packet and is discarded.
    constexpr int kRecvBufferSize = 2048;

    // Cap datagrams processed per Pump() so a flood can never stall the frame.
    constexpr int kMaxDrainPerPump = 256;

    // Monotonic wall clock in milliseconds (presentation timing only - this is the
    // client render path, not the deterministic simulation).
    [[nodiscard]] double NowMs()
    {
      using namespace std::chrono;
      return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }
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
        case Msg::RELIABLE_MAGIC:
          m_events.OnDatagram(buffer, size);   // reliable lanes (Control/Gameplay/Bulk)
          break;
        default:
          break;   // unknown/foreign datagram: ignore
      }
    }

    // Presentation timing: when a newer snapshot tick first appears, stamp its
    // arrival and measure the interval since the previous stamp. The render then
    // samples one interval in the past (InterpolationAlpha) so motion tweens
    // smoothly between snapshots instead of snapping to the latest tick.
    const uint32_t latest = m_interp.LatestTick();
    if (latest != m_lastInterpTick)
    {
      const double now = NowMs();
      if (m_currArrivalMs > 0.0)
      {
        const double dt = now - m_currArrivalMs;
        if (dt > 1.0 && dt < 1000.0)   // ignore duplicate stamps and long stalls
          m_interpIntervalMs = dt;
      }
      m_currArrivalMs = now;
      m_lastInterpTick = latest;
    }

    // Drain reliable events: AssignPlayer is the handshake and GalaxyManifest is
    // the chart data (both consumed here); the rest are queued for the application
    // via PollEvent().
    Net::ReliableMessage msg;
    while (m_events.Receive(msg))
    {
      Msg::AssignPlayer assign;
      uint32_t total = 0;
      uint32_t base = 0;
      if (Msg::TryDecode(msg, assign))
        m_localPlayer = assign.entityId;
      else if (Net::DecodeManifestChunk(msg, total, base, m_galaxy))
        m_galaxy.reserve(total);   // chunks arrive in order; just accumulate
      else
        m_appEvents.push_back(std::move(msg));
    }

    // Send our cumulative acks back so the server stops resending delivered events
    // (these datagrams also carry any client->server reliable messages). One per
    // lane that has something to (re)send or a new ack to deliver.
    if (m_haveServer)
    {
      for (const std::vector<uint8_t>& dg : m_events.WriteDatagrams())
        m_socket.SendTo(m_server, dg.data(), dg.size());
    }
  }

  void ReplicationClient::SendInput(const Msg::InputCommand& _input)
  {
    if (!m_open || !m_haveServer)
      return;

    // The intent rides the unified 'NMSG' unreliable lane as one InputCommand
    // record (replacing the old bespoke 'NCMD' packet).
    Msg::PacketWriter writer(Msg::MessageLane::Unreliable);
    writer.Add(_input);
    m_socket.SendTo(m_server, writer.Bytes().data(), writer.Size());
  }

  double ReplicationClient::InterpolationAlpha() const
  {
    return Net::InterpolationAlpha(NowMs(), m_currArrivalMs, m_interpIntervalMs);
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
