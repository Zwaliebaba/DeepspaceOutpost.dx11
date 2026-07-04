#pragma once

// TimeSync - clock/latency handshake messages (NeuronCore, Track E1).
//
// A ~1 Hz Control-lane exchange that lets the client estimate its round-trip time
// and the offset between its local clock and the server's tick clock, and lets the
// server learn each session's latency (needed by E1's lag-compensated fire).
//
//   Ping  - client -> server: the client's current local time and its own latest
//           smoothed RTT estimate. The server echoes the timestamp (so the client
//           can close the round trip) and RECORDS the reported RTT on the session.
//   Pong  - server -> client: the echoed clientTimeMs plus the server's current
//           tick, so the client both measures RTT (now - clientTimeMs) and aligns
//           its clock to the server's authoritative tick.
//
// Trust note: the RTT the server stores is the client's OWN estimate, not a
// server-authoritative measurement. E1's lag-compensation therefore CLAMPS the
// rewind it drives to the transform-history window (~15 ticks), so a client that
// over-reports its latency can rewind targets at most that far - the same bound the
// ring buffer imposes anyway. A hardened version would measure RTT from the
// reliable-ack loop instead; that is deferred (see IMPLEMENTATION.md E1).
//
// Data/protocol only: the smoothing (client) and the per-session store + clamp
// (server) live in NeuronClient / GameLogic. New additive ids (0x0006/0x0007) - no
// PROTOCOL_VERSION bump, since nothing existing changed layout. 0x0005 stays
// reserved (the never-shipped AssignControl; identity folded into HelloAck at C1).

#include <cstdint>
#include <tuple>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  // client -> server: a time-sync probe. `clientTimeMs` is the client's local clock
  // when it sent this (echoed back in Pong to close the round trip). `rttMs` is the
  // client's own latest smoothed RTT estimate (0 before the first Pong) - the server
  // stores it per session for lag compensation, clamped to the history window.
  struct Ping
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0006);   // core/session
    static constexpr MessageScope Scope = MessageScope::Control;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Control;
    static constexpr Direction    Dir   = Direction::ClientToServer;

    uint32_t clientTimeMs = 0;   // the client's local clock at send (echoed in Pong)
    uint32_t rttMs = 0;          // the client's own smoothed RTT estimate (0 until known)
    auto Fields()       { return std::tie(clientTimeMs, rttMs); }
    auto Fields() const { return std::tie(clientTimeMs, rttMs); }
  };

  // server -> client: the reply to a Ping. Echoes the client's timestamp so the
  // client measures RTT = now - clientTimeMs, and carries the server's current tick
  // so the client can align its clock to the authoritative simulation time.
  struct Pong
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0007);   // core/session
    static constexpr MessageScope Scope = MessageScope::Control;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Control;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t clientTimeMs = 0;   // the Ping's timestamp, echoed unchanged
    uint32_t serverTick = 0;     // the server's tick when it sent this Pong
    auto Fields()       { return std::tie(clientTimeMs, serverTick); }
    auto Fields() const { return std::tie(clientTimeMs, serverTick); }
  };
}

REGISTER_MESSAGE(Ping);
REGISTER_MESSAGE(Pong);
