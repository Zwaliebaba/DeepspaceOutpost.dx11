#pragma once

// AdminClient - the ServerManager's network endpoint for the management channel.
//
// The manager-side sibling of ReplicationClient, an order of magnitude smaller: no
// snapshots, no interpolation - just a bound UDP socket, ONE reliable MessageEndpoint,
// and a typed local mirror (roster map, event log, health history) that the GUI reads
// every frame. It connects to a server's SEPARATE admin port (sm.md), authenticates
// with the shared secret, and thereafter only observes what the server pushes.
//
// The socket edge (Connect/Pump) is deliberately thin over a socket-free core
// (BeginHandshake / Ingest / Service / TakeOutgoing), so the whole decode + mirror +
// timeout logic is driven byte-for-byte in a unit test against a fake server, with no
// OS networking - the same discipline as the reliable lanes and AdminChannel.
//
// The header stays winsock-free (UdpSocket keeps its SOCKET opaque), so it is safe to
// include anywhere in the manager app.

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

#include "NetLib.h"
#include "Messages/MessageEndpoint.h"
#include "Messages/Defs/Admin.h"

namespace Neuron::Client
{
  class AdminClient
  {
  public:
    enum class State
    {
      Disconnected,   // never connected / disconnected
      Connecting,     // hello sent, awaiting AdminHelloAck
      Connected,      // authenticated; receiving roster/events/health
      Rejected,       // the server refused the hello (see RejectReason)
      TimedOut,       // no reply within the connect window, or the server went silent
    };

    // How long to keep the tail of the feed / health trend for the GUI.
    static constexpr std::size_t MAX_EVENTS = 512;
    static constexpr std::size_t MAX_HEALTH = 120;

    // Timeouts (ms): give up on a hello that never gets an ack; and, once connected,
    // declare the link dead if the server (which pushes health ~1 Hz) goes quiet.
    static constexpr double CONNECT_TIMEOUT_MS = 5000.0;
    static constexpr double LIVE_TIMEOUT_MS    = 5000.0;

    ~AdminClient() { Disconnect(); }

    // --- socket edge (the GUI app uses these) ---------------------------------

    // Bind an ephemeral UDP socket, target `_server` (the admin port), and queue the
    // AdminHello. Returns false only if the socket can't open. Re-calling reconnects.
    bool Connect(const Net::Endpoint& _server, const std::string& _key);
    void Disconnect();
    [[nodiscard]] bool IsOpen() const { return m_open; }

    // Once per frame: drain the socket, decode into the mirror, advance timeouts, and
    // send our acks (plus the unacked hello until it lands). No-op when closed.
    void Pump();

    // --- socket-free core (also the unit-test seam) ---------------------------

    // Start (or restart) the handshake at time `_nowMs`: reset the reliable state and
    // mirror, queue the AdminHello, and enter Connecting.
    void BeginHandshake(const std::string& _key, double _nowMs);

    // Feed one inbound datagram to the reliable lanes. Returns true if it was one of
    // ours (valid 'NRLB'); a handled datagram refreshes the liveness clock.
    bool Ingest(const uint8_t* _data, std::size_t _size, double _nowMs);

    // Drain delivered reliable messages into the mirror and advance the timeout state
    // machine for `_nowMs`.
    void Service(double _nowMs);

    // The datagrams we owe the server right now (acks, and the hello until acked).
    [[nodiscard]] std::vector<std::vector<uint8_t>> TakeOutgoing() { return m_events.WriteDatagrams(); }

    // --- observers (the GUI reads these) --------------------------------------
    [[nodiscard]] State GetState() const { return m_state; }
    [[nodiscard]] Msg::AdminRejectReason RejectReason() const { return m_rejectReason; }
    [[nodiscard]] const Msg::AdminHelloAck& ServerIdentity() const { return m_identity; }
    [[nodiscard]] const std::map<uint32_t, Msg::AdminPlayerInfo>& Roster() const { return m_roster; }
    [[nodiscard]] const std::deque<Msg::AdminEvent>& Events() const { return m_eventLog; }
    [[nodiscard]] const std::deque<Msg::AdminHealth>& HealthHistory() const { return m_healthLog; }
    [[nodiscard]] bool HasHealth() const { return !m_healthLog.empty(); }
    [[nodiscard]] const Msg::AdminHealth& LatestHealth() const { return m_healthLog.back(); }

  private:
    void ResetMirror();

    Net::UdpSocket m_socket;
    bool m_open = false;
    Net::Endpoint m_server;
    bool m_haveServer = false;

    Msg::MessageEndpoint m_events;   // reliable lanes to the server admin channel
    std::string m_key;

    State m_state = State::Disconnected;
    Msg::AdminRejectReason m_rejectReason = Msg::AdminRejectReason::ProtocolMismatch;
    Msg::AdminHelloAck m_identity;
    uint64_t m_token = 0;

    double m_connectStartMs = 0.0;   // when the hello was queued (connect timeout)
    double m_lastServerMs = 0.0;     // last time a datagram/message arrived (live timeout)

    std::map<uint32_t, Msg::AdminPlayerInfo> m_roster;
    std::deque<Msg::AdminEvent> m_eventLog;
    std::deque<Msg::AdminHealth> m_healthLog;
  };
}
