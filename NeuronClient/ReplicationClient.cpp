#include "pch.h"

#include "ReplicationClient.h"

#include <chrono>

#include "Messages/Framing.h"
#include "Messages/Reliable.h"
#include "Messages/Defs/CoreEvents.h"
#include "Messages/Defs/GalaxyChunks.h"

namespace Neuron::Client
{
  namespace
  {
    // Largest datagram we expect (a single MTU-bounded snapshot packet, with a
    // little slack); anything bigger is a malformed packet and is discarded.
    constexpr int kRecvBufferSize = 2048;

    // Cap datagrams processed per Pump() so a flood can never stall the frame.
    constexpr int kMaxDrainPerPump = 256;

    // How often the client probes the server for a time-sync round trip (E1). ~1 Hz
    // keeps the RTT estimate fresh at negligible cost.
    constexpr double kPingIntervalMs = 1000.0;

    // Monotonic wall clock in milliseconds (presentation timing only - this is the
    // client render path, not the deterministic simulation).
    [[nodiscard]] double NowMs()
    {
      using namespace std::chrono;
      return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }

    // NowMs() truncated into the 32-bit millisecond field the time-sync messages
    // carry. RTT is computed as an UNSIGNED 32-bit difference so it stays correct
    // across the ~49.7-day wrap (a short round trip never spans it).
    [[nodiscard]] uint32_t NowMs32()
    {
      return static_cast<uint32_t>(static_cast<uint64_t>(NowMs()));
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
        {
          // The snapshot stream is now full-or-delta (E2b): the decoder reconstructs
          // the whole tick (applying a delta against its acked baseline) and tracks
          // the latest baseline to acknowledge; feed the reconstruction to the
          // interpolator exactly as a full snapshot used to be applied.
          Net::WorldSnapshot recon;
          if (m_stream.Decode(buffer, size, recon))
            m_interp.Ingest(recon);
          break;
        }
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

    // Drain reliable events: HelloAck/HelloReject is the handshake reply and
    // GalaxyChunk is the chart data (all consumed here); the rest are queued for
    // the application via PollEvent().
    Net::ReliableMessage msg;
    while (m_events.Receive(msg))
    {
      Msg::HelloAck ack;
      Msg::HelloReject reject;
      Msg::GalaxyChunk chunk;
      Msg::Pong pong;
      if (Msg::TryDecode(msg, pong))
      {
        // Time sync (E1): close the round trip we opened with the matching Ping.
        // The Pong echoes our send timestamp, so RTT is the unsigned 32-bit
        // difference now - clientTimeMs; fold it into the smoothed estimate and
        // note the server's tick for a coarse clock reference. Consumed internally
        // - never surfaced as an app event.
        const uint32_t rtt = NowMs32() - pong.clientTimeMs;
        m_latency.AddRttSample(static_cast<double>(rtt));
        m_lastServerTick = pong.serverTick;
      }
      else if (Msg::TryDecode(msg, ack))
      {
        // The handshake reply: our player identity (C) + primary entity + the
        // session token (B2). From here on every outbound datagram carries the
        // token so the server can authenticate us by identity, not by source
        // address - stamp it on the reliable endpoint (SendInput stamps the
        // unreliable lane directly).
        m_localPlayer = ack.entityId;
        m_playerId = ack.playerId;
        m_sessionToken = ack.sessionToken;
        m_events.SetToken(ack.sessionToken);
      }
      else if (Msg::TryDecode(msg, reject))
      {
        // The server refused the handshake (e.g. protocol-version mismatch);
        // the client surfaces a connect error rather than a world.
        m_helloRejected = true;
      }
      else if (Msg::TryDecode(msg, chunk))
      {
        // Chunks arrive in request order on the ordered Bulk lane, so they
        // append contiguously; the first one sizes the table.
        m_galaxyTotal = chunk.total;
        m_galaxyKnownTotal = true;
        m_galaxy.reserve(chunk.total);
        if (chunk.baseIndex == m_galaxy.size())
          for (const Msg::GalaxySystemEntry& e : chunk.systems)
            m_galaxy.push_back(Msg::FromWireEntry(e));
      }
      else
      {
        m_appEvents.push_back(std::move(msg));
      }
    }

    // Pull the galaxy chart in bounded ranges - but only once CONNECTED (we have
    // our entity from HelloAck): the server ignores everything but the hello from
    // an unconnected endpoint, so requesting earlier would stall the pull. Ask for
    // the next range as each completes, until the whole table is here.
    if (LocalPlayer() != 0xFFFFFFFFu && !GalaxyComplete() && m_galaxy.size() >= m_galaxyRequestedUpTo)
    {
      Msg::GalaxyChunkRequest req;
      req.baseIndex = static_cast<uint32_t>(m_galaxy.size());
      req.count = Msg::GALAXY_CHUNK_MAX_REQUEST;
      m_events.Send(req);
      m_galaxyRequestedUpTo = req.baseIndex + req.count;
    }

    // Time sync (E1): once connected, probe the server ~1 Hz. The Ping carries our
    // local clock (echoed back to close the round trip) and our own latest smoothed
    // RTT, which the server records per session for lag compensation. Queued on the
    // reliable Control lane, so it goes out with the acks below.
    if (LocalPlayer() != 0xFFFFFFFFu)
    {
      const double now = NowMs();
      if (m_lastPingMs == 0.0 || now - m_lastPingMs >= kPingIntervalMs)
      {
        m_events.Send(Msg::Ping{ NowMs32(), static_cast<uint32_t>(m_latency.rttMs) });
        m_lastPingMs = now;
      }
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
    // record (replacing the old bespoke 'NCMD' packet), stamped with our session
    // token (B2) so the server accepts it as ours. Before HelloAck the token is 0
    // and input is ignored server-side anyway (we aren't connected yet).
    //
    // Piggyback the snapshot-stream ack (E2b): the latest baseline tick we hold, so
    // the server deltas its next snapshot against a snapshot we provably have.
    Msg::InputCommand input = _input;
    input.ackSnapshotTick = m_stream.AckTick();
    Msg::PacketWriter writer(Msg::MessageLane::Unreliable, m_sessionToken);
    writer.Add(input);
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
