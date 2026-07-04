#pragma once

// ReplicationClient - the client's network endpoint for the server world.
//
// Owns a bound UDP socket, a SnapshotInterpolator (unreliable bulk state), and a
// ReliableChannel (ordered events). Pump() drains every datagram waiting on the
// socket, routes it by magic, and feeds the right stream; the render code then
// asks for interpolated, floating-origin-rebased entities. SendInput() pushes the
// player's intent the other way. This is where the client STOPS simulating and
// becomes a thin presentation + input terminal for the server's authoritative
// world.
//
// It is inert until Open() succeeds: a default (closed) client makes Pump() a
// no-op, so the existing single-player path is unchanged until replication is
// switched on. The header stays winsock-free (UdpSocket keeps its SOCKET opaque),
// so it is safe to include anywhere in the client.

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

#include "NetLib.h"
#include "SnapshotInterpolator.h"
#include "SnapshotStream.h"                // Net::SnapshotStreamDecoder (delta stream, E2b)
#include "ReliableChannel.h"
#include "Messages/Defs/InputCommand.h"
#include "StationProtocol.h"
#include "Messages/Defs/GalaxyChunks.h"    // Net::GalaxySystemInfo / the chunk pull protocol
#include "Messages/Reliable.h"
#include "Messages/MessageEndpoint.h"
#include "Messages/Defs/PlayerSession.h"   // Msg::ClientHello / PlayerInfo / PlayerStatus
#include "Messages/Defs/TimeSync.h"        // Msg::Ping / Pong (E1 time sync)
#include "LatencyEstimate.h"               // Net::LatencyEstimate (smoothed RTT)

namespace Neuron::Client
{
  class ReplicationClient
  {
  public:
    // Bind to `_port` and start listening for snapshots. Returns false on failure.
    bool Open(uint16_t _port);
    void Close();
    [[nodiscard]] bool IsOpen() const { return m_open; }

    // Where to send input/acks. Set from config up front, and refreshed from the
    // source address of whatever the server actually sends us.
    void SetServerEndpoint(const Net::Endpoint& _ep) { m_server = _ep; m_haveServer = true; }

    // Drain all datagrams currently queued on the socket, routing each by magic
    // into the interpolator or the reliable channel, and auto-applying the
    // HelloAck/HelloReject handshake reply. No-op when closed; bounded so a flood
    // can't stall.
    void Pump();

    // Send the player's intent to the server (no-op until the server endpoint is
    // known and the socket is open).
    void SendInput(const Msg::InputCommand& _input);

    // Queue any reliable catalog message to the server on its declared lane
    // (station requests, travel requests, ...). No-op until the client is open.
    template <Msg::Message M>
    void Send(const M& _m)
    {
      if (m_open)
        m_events.Send(_m);
    }

    // Queue a reliable station request (dock/undock/buy/sell) to the server. The
    // authoritative StationResponse arrives later via PollEvent(). No-op until
    // the client is open.
    void SendStationRequest(const Net::StationRequest& _request) { Send(_request); }

    // Queue the opening handshake: protocol version + the player's commander name.
    // Since B1 this is the FRONT DOOR - the server spawns nothing until this valid,
    // version-checked hello arrives. Rides the reliable Control lane, redelivered
    // until the server replies with HelloAck (or HelloReject). No-op until open.
    void SendHello(uint32_t _protocolVersion, const std::string& _name)
    {
      if (m_open)
        m_events.Send(Msg::ClientHello{ _protocolVersion, _name });   // Control lane
    }

    // Interpolated state of `_id` at `_alpha` in [0,1], or false if unknown.
    [[nodiscard]] bool Sample(uint32_t _id, double _alpha, Net::EntitySnapshot& _out) const
    {
      return m_interp.Sample(_id, _alpha, _out);
    }

    [[nodiscard]] std::vector<Net::EntitySnapshot> SampleAll(double _alpha) const
    {
      return m_interp.SampleAll(_alpha);
    }

    // The interpolation alpha to sample at THIS frame: renders ~one snapshot
    // interval in the past and tweens prev->curr, from the measured snapshot
    // arrival cadence (presentation only; see Net::InterpolationAlpha). Returns
    // 1.0 (show latest) until two snapshots have been timed.
    [[nodiscard]] double InterpolationAlpha() const;

    void EvictStale(uint32_t _maxAge) { m_interp.EvictStale(_maxAge); }

    // Drop one entity now (on an authoritative despawn/death), so a destroyed
    // entity (e.g. a detonated missile or a killed ship) disappears immediately
    // instead of lingering as a ghost.
    void Forget(uint32_t _id) { m_interp.Forget(_id); }

    [[nodiscard]] std::size_t Count() const { return m_interp.Count(); }
    [[nodiscard]] uint32_t LatestTick() const { return m_interp.LatestTick(); }

    // Pop the next reliably-delivered application event (despawn/death/chat), in
    // order, or false if none are ready. (The HelloAck/HelloReject handshake and
    // the galaxy chunks are consumed internally.)
    bool PollEvent(Net::ReliableMessage& _out);

    // The session token from HelloAck (0 until connected). Stamped on every
    // outbound datagram (B2) so the server authenticates us by token, not address.
    [[nodiscard]] uint64_t SessionToken() const { return m_sessionToken; }

    // Our player identity from HelloAck (C: the §12 Account → Empire → owns N
    // entities layer - player, not hull). 0 until connected. LocalPlayer() stays
    // the PRIMARY controlled entity; rosters and future multi-unit ownership key
    // off this id.
    [[nodiscard]] uint32_t PlayerId() const { return m_playerId; }

    // The smoothed round-trip time to the server (ms), from the ~1 Hz Ping/Pong
    // exchange (E1). 0 and !HasLatency() until the first Pong closes a round trip.
    [[nodiscard]] double SmoothedRttMs() const { return m_latency.rttMs; }
    [[nodiscard]] bool HasLatency() const { return m_latency.valid; }

    // The server tick reported by the most recent Pong (0 until one arrives). A
    // coarse clock reference for presentation; the authoritative tick still rides
    // every snapshot header.
    [[nodiscard]] uint32_t LastServerTick() const { return m_lastServerTick; }

    // True once the server refused our ClientHello (e.g. a protocol-version
    // mismatch): the client shows a connect error instead of a world.
    [[nodiscard]] bool HelloRejected() const { return m_helloRejected; }

    // The galaxy's system list, pulled from the server in bounded chunk ranges
    // (empty until the first chunk arrives, growing until complete). The
    // galactic chart renders and teleports from this - progressively, so a
    // partially-pulled chart already works.
    [[nodiscard]] const std::vector<Net::GalaxySystemInfo>& Galaxy() const { return m_galaxy; }
    [[nodiscard]] bool HasGalaxy() const { return !m_galaxy.empty(); }
    [[nodiscard]] bool GalaxyComplete() const
    {
      return m_galaxyKnownTotal && m_galaxy.size() >= m_galaxyTotal;
    }

    // The entity id the local player controls - its replicated position is the
    // floating origin and it is not drawn. Learned from the HelloAck handshake
    // reply (B1), and also carried in every snapshot header (either sets it).
    void SetLocalPlayer(uint32_t _id) { m_localPlayer = _id; }
    [[nodiscard]] uint32_t LocalPlayer() const
    {
      const uint32_t fromSnapshot = m_interp.ViewerId();
      return (fromSnapshot != 0xFFFFFFFFu) ? fromSnapshot : m_localPlayer;
    }

  private:
    Net::UdpSocket m_socket;
    Net::SnapshotInterpolator m_interp;            // unreliable bulk state
    Net::SnapshotStreamDecoder m_stream;           // E2b: full+delta snapshot decode/baseline
    Msg::MessageEndpoint m_events;                 // reliable lanes (Control/Gameplay/Bulk)
    std::deque<Net::ReliableMessage> m_appEvents;  // events for the app (handshake/chunks filtered out)
    uint64_t m_sessionToken = 0;                   // from HelloAck; stamped on every outbound datagram (B2)
    uint32_t m_playerId = 0;                       // from HelloAck; our player identity (C)
    bool m_helloRejected = false;                  // server refused the handshake
    Net::LatencyEstimate m_latency;                // E1: smoothed RTT from Ping/Pong
    double m_lastPingMs = 0.0;                      // wall-clock of our last sent Ping (send cadence)
    uint32_t m_lastServerTick = 0;                 // server tick from the most recent Pong
    std::vector<Net::GalaxySystemInfo> m_galaxy;   // the galaxy chart, pulled chunk by chunk
    uint32_t m_galaxyTotal = 0;                    // the galaxy's size, learned from the first chunk
    bool m_galaxyKnownTotal = false;
    uint32_t m_galaxyRequestedUpTo = 0;            // exclusive end of the last chunk request
    Net::Endpoint m_server;
    uint32_t m_localPlayer = 0xFFFFFFFFu;   // sentinel until assigned (never entity 0)
    bool m_haveServer = false;
    bool m_open = false;

    // Snapshot arrival timing for render interpolation (presentation only).
    double m_currArrivalMs = 0.0;        // wall-clock when the latest tick first appeared
    double m_interpIntervalMs = 33.0;    // measured gap between the last two snapshots (~1 tick seed)
    uint32_t m_lastInterpTick = 0;       // latest tick we have timestamped
  };

  // Process-wide replication client (mirrors GameUniverse()'s temporary global).
  // Closed by default; call Open() to begin consuming replicated state.
  [[nodiscard]] ReplicationClient& ReplicationClientInstance();
}
