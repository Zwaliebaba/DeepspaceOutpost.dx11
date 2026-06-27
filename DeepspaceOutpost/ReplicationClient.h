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
#include <vector>

#include "NetLib.h"
#include "SnapshotInterpolator.h"
#include "ReliableChannel.h"
#include "ClientInput.h"
#include "StationProtocol.h"
#include "GalaxyManifest.h"

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
    // AssignPlayer handshake. No-op when closed; bounded so a flood can't stall.
    void Pump();

    // Send the player's intent to the server (no-op until the server endpoint is
    // known and the socket is open).
    void SendInput(const Net::ClientInput& _input);

    // Queue a reliable station request (dock/undock/buy/sell) to the server. The
    // authoritative StationResponse arrives later via PollEvent(). No-op until
    // the client is open.
    void SendStationRequest(const Net::StationRequest& _request)
    {
      if (m_open)
        Net::SendStationRequest(m_events, _request);
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

    void EvictStale(uint32_t _maxAge) { m_interp.EvictStale(_maxAge); }

    [[nodiscard]] std::size_t Count() const { return m_interp.Count(); }
    [[nodiscard]] uint32_t LatestTick() const { return m_interp.LatestTick(); }

    // Pop the next reliably-delivered application event (despawn/death/chat), in
    // order, or false if none are ready. (AssignPlayer and the galaxy manifest are
    // consumed internally.)
    bool PollEvent(Net::ReliableMessage& _out);

    // The galaxy's system list, as delivered by the server's manifest (empty until
    // it arrives). The galactic chart renders and teleports from this.
    [[nodiscard]] const std::vector<Net::GalaxySystemInfo>& Galaxy() const { return m_galaxy; }
    [[nodiscard]] bool HasGalaxy() const { return !m_galaxy.empty(); }

    // The entity id the local player controls - its replicated position is the
    // floating origin and it is not drawn. Learned primarily from the snapshot
    // header (reliable via the working snapshot channel); the AssignPlayer
    // handshake is a fallback.
    void SetLocalPlayer(uint32_t _id) { m_localPlayer = _id; }
    [[nodiscard]] uint32_t LocalPlayer() const
    {
      const uint32_t fromSnapshot = m_interp.ViewerId();
      return (fromSnapshot != 0xFFFFFFFFu) ? fromSnapshot : m_localPlayer;
    }

  private:
    Net::UdpSocket m_socket;
    Net::SnapshotInterpolator m_interp;            // unreliable bulk state
    Net::ReliableChannel m_events;                 // reliable ordered events
    std::deque<Net::ReliableMessage> m_appEvents;  // events for the app (AssignPlayer filtered out)
    std::vector<Net::GalaxySystemInfo> m_galaxy;   // the galaxy chart manifest (filled on connect)
    Net::Endpoint m_server;
    uint32_t m_localPlayer = 0xFFFFFFFFu;   // sentinel until assigned (never entity 0)
    bool m_haveServer = false;
    bool m_open = false;
  };

  // Process-wide replication client (mirrors GameUniverse()'s temporary global).
  // Closed by default; call Open() to begin consuming replicated state.
  [[nodiscard]] ReplicationClient& ReplicationClientInstance();
}
