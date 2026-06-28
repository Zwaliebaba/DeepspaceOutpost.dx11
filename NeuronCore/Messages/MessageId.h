#pragma once

// MessageId - the stable, permanent identity of every message type (NeuronCore).
//
// A MessageId is part of the network ABI: once a value ships it is NEVER reused
// and a retired id stays reserved. Ids are partitioned into subsystem ranges so
// ownership is obvious, with the top half of the space (bit 15 set) reserved for
// LocalOnly/Tooling messages that NEVER touch the wire - giving a one-line check
// that a wire send was handed a non-wire message.
//
// Concrete gameplay ids (InputCommand, EntityDeath, ...) are added to this enum
// as the folding-in phases land; Phase 0 defines only the sentinel + ranges.

#include <cstdint>

namespace Neuron::Msg
{
  // Bit 15 set  => the message is NOT on the wire (LocalOnly / Tooling).
  inline constexpr uint16_t MESSAGE_ID_NONWIRE_BIT = 0x8000;

  // Subsystem-reserved id ranges (inclusive). Wire messages live below the
  // non-wire bit; local/tooling messages live at or above it.
  //   0x0000          Invalid (reserved)
  //   0x0001-0x00FF   core / session / control
  //   0x0100-0x01FF   input
  //   0x0200-0x02FF   replication control / lifecycle
  //   0x0300-0x03FF   chat / social
  //   0x0400-0x04FF   station / economy
  //   0x0F00-0x0FFF   debug / tooling (wire-visible diagnostics)
  //   0x1000-0x7FFF   game-specific extensions (wire)
  //   0x8000-0xFFFE   LocalOnly / Tooling (NEVER on the wire)
  //   0xFFFF          Invalid (reserved-max)
  enum class MessageId : uint16_t
  {
    Invalid = 0x0000,
  };

  [[nodiscard]] constexpr uint16_t Raw(MessageId _id) { return static_cast<uint16_t>(_id); }

  // True for ids in the top (non-wire) half of the space.
  [[nodiscard]] constexpr bool IsNonWireId(MessageId _id)
  {
    return (Raw(_id) & MESSAGE_ID_NONWIRE_BIT) != 0;
  }
}
