#pragma once

// InputCommand - the client -> server flight/fire intent, as a catalog message.
//
// This is the first of the existing hand-rolled protocols folded onto the unified
// message system (the wire-folding phases). It replaces the bespoke 'NCMD' packet
// (the old WriteInput/ReadInput): the same fields now ride the 'NMSG' UNRELIABLE
// lane as a length-prefixed record, encoded/decoded by the generic Serialize codec
// from the single Fields() description below.
//
// Self-superseding, latest-sequence-wins (the server keeps the highest `sequence`
// and drops stale/duplicate datagrams), so it is sent unreliably and often - it is
// never queued on a reliable lane (a static trait enforces this). Shared protocol
// only: the server maps it onto its authoritative FlightIntent (no behaviour here).
//
// The field order/layout is byte-identical to the legacy 'NCMD' payload, so the
// migration is a framing change, not a wire-format change (see the parity test).
// `missileTarget` stays a bare entity index for now; promoting it to a generation-
// stamped NetEntityId is a separate, later step.

#include <cstdint>
#include <tuple>

#include "Messages/Registry.h"   // pulls MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  // Sentinel: the player has nothing locked for a missile.
  inline constexpr uint32_t NO_MISSILE_TARGET = 0xFFFFFFFFu;

  struct InputCommand
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0100);   // input range
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Unreliable;
    static constexpr Direction    Dir   = Direction::ClientToServer;

    uint32_t sequence = 0;       // monotonically increasing; latest wins on the server
    float    rollAxis = 0.0f;    // [-1, 1] desired roll  (right positive)
    float    pitchAxis = 0.0f;   // [-1, 1] desired pitch (up positive)
    float    throttle = 0.0f;    // [ 0, 1] desired forward throttle
    bool     fire = false;       // fire the front laser this frame
    bool     fireMissile = false;// launch a missile this frame
    uint32_t missileTarget = NO_MISSILE_TARGET;   // entity index the missile is locked to

    auto Fields()       { return std::tie(sequence, rollAxis, pitchAxis, throttle, fire, fireMissile, missileTarget); }
    auto Fields() const { return std::tie(sequence, rollAxis, pitchAxis, throttle, fire, fireMissile, missileTarget); }
  };
}

REGISTER_MESSAGE(InputCommand);
