#pragma once

// ReplicationClient - the client's receive loop for replicated world state.
//
// Owns a bound UDP socket and a SnapshotInterpolator. Pump() drains every
// datagram waiting on the socket and feeds it to the interpolator; the render
// code then asks for interpolated, floating-origin-rebased entities. This is
// where the client STOPS simulating and starts displaying the server's
// authoritative world.
//
// It is inert until Open() succeeds: a default (closed) client makes Pump() a
// no-op, so the existing single-player path is unchanged until replication is
// switched on. The header stays winsock-free (UdpSocket keeps its SOCKET opaque),
// so it is safe to include anywhere in the client.

#include <cstdint>
#include <vector>

#include "NetLib.h"
#include "SnapshotInterpolator.h"
#include "ReliableChannel.h"

namespace Neuron::Client
{
  class ReplicationClient
  {
  public:
    // Bind to `_port` and start listening for snapshots. Returns false on failure.
    bool Open(uint16_t _port);
    void Close();
    [[nodiscard]] bool IsOpen() const { return m_open; }

    // Drain all datagrams currently queued on the socket into the interpolator.
    // No-op when closed. Bounded so a flood cannot stall the frame.
    void Pump();

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

    // Pop the next reliably-delivered event (despawn/death/chat), in order, or
    // false if none are ready. Decode with the GameEvents.h helpers.
    bool PollEvent(Net::ReliableMessage& _out) { return m_events.Receive(_out); }

    // The entity id the local player controls. Its replicated position is used as
    // the floating origin for rendering, and it is not drawn as a separate ship.
    void SetLocalPlayer(uint32_t _id) { m_localPlayer = _id; }
    [[nodiscard]] uint32_t LocalPlayer() const { return m_localPlayer; }

  private:
    Net::UdpSocket m_socket;
    Net::SnapshotInterpolator m_interp;   // unreliable bulk state
    Net::ReliableChannel m_events;        // reliable ordered events
    uint32_t m_localPlayer = 0;
    bool m_open = false;
  };

  // Process-wide replication client (mirrors GameUniverse()'s temporary global).
  // Closed by default; call Open() to begin consuming replicated state.
  [[nodiscard]] ReplicationClient& ReplicationClientInstance();
}
