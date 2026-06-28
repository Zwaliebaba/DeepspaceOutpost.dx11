#pragma once

// MessageTraits - the orthogonal policy axes every message carries (NeuronCore).
//
// Mechanism only (no behaviour): these enums separate the three concerns the
// design keeps apart - what a message IS (schema, elsewhere), how it routes
// LOCALLY (scope/kind), and how it crosses the WIRE (lane/direction). A message
// declares one constant per axis as a `static constexpr` member, so the send/
// dispatch paths can route it without a runtime table and the wrong combination
// is a compile error rather than a packet on the wire.

#include <cstdint>

namespace Neuron::Msg
{
  // Where a message is allowed to travel. The wire ABI is deliberately a SUBSET:
  // a LocalOnly event (e.g. a key press) can never be serialised, so it cannot
  // accidentally become part of the permanent network protocol.
  enum class MessageScope : uint8_t
  {
    LocalOnly = 0,   // in-process only; never serialised (e.g. KeyPressed)
    Wire,            // client <-> server gameplay traffic; permanent network ABI
    Control,         // wire, but session/handshake/clock - rides the Control lane
    DebugOnly,       // dev builds only; stripped from the release wire
    Tooling,         // capture/trace/inspector streams; never gameplay
  };

  // A request that may be rejected and MUST be validated (Command) versus a fact
  // that already happened and is trusted when it originates server-side (Event).
  // Tells the inbound pipeline what to check and handlers what they may assume.
  enum class MessageKind : uint8_t
  {
    Command = 0,   // a request (usually client -> server); validate before acting
    Event,         // a fact; observe-only
  };

  // The delivery lane a wire message rides. Separate reliable lanes stop a large
  // cold/bulk payload (a galaxy manifest) head-of-line-blocking a gameplay death
  // or a session-control message. Unreliable carries self-superseding traffic.
  enum class MessageLane : uint8_t
  {
    Control = 0,   // reliable ordered, dedicated channel: handshake/assign/clock/session
    Gameplay,      // reliable ordered: death, despawn, chat, station req/resp
    Bulk,          // reliable ordered, separate channel: manifest, catalogs (cold/large)
    Unreliable,    // self-superseding, latest-wins: input
  };

  // Permitted direction across the wire. None marks a non-wire (LocalOnly/etc.)
  // message; the inbound pipeline rejects a peer that sends the wrong direction.
  enum class Direction : uint8_t
  {
    None = 0,         // not a wire message
    ClientToServer,
    ServerToClient,
    Both,
  };

  // A scope that is serialised onto the wire (its id must be in the low,
  // wire-ABI half of the id space - see MessageId.h).
  [[nodiscard]] constexpr bool IsWireScope(MessageScope _s)
  {
    return _s == MessageScope::Wire || _s == MessageScope::Control;
  }
}
