#pragma once

// InputActions - client-local input intent, as LocalOnly catalog messages.
//
// The bridge between the input layer and the wire: as the client detects a
// discrete equipment action (launch missile, ECM, bomb, pod) it publishes an
// ActionTriggered onto the client MessageBus; a subscriber maps it onto the
// reliable AbilityRequest and sends it. This decouples "what the player did"
// from "what goes on the wire" (rebinding, recording/replay, headless bots all
// drive the same path). Movement is a UnitOrder and the per-frame InputCommand
// is only the heartbeat/ack - no flight or combat state rides it.
//
// MessageScope::LocalOnly - these never travel the wire (id in the non-wire half),
// so they cannot leak into the permanent network protocol; only the resulting
// AbilityRequest is sent.

#include <cstdint>
#include <tuple>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  enum class InputAction : uint8_t
  {
    Fire = 0,           // fire the front laser this frame (held)
    LaunchMissile = 1,  // launch a missile at `param` (the locked target index)
    Ecm = 2,            // fire the ECM burst (G8)
    EnergyBomb = 3,     // detonate the energy bomb (G8)
    EscapePod = 4,      // eject in the escape pod (G8)
  };

  struct ActionTriggered
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x8200);   // LocalOnly band
    static constexpr MessageScope Scope = MessageScope::LocalOnly;
    static constexpr MessageKind  Kind  = MessageKind::Command;
    static constexpr MessageLane  Lane  = MessageLane::Unreliable;          // unused (never sent)
    static constexpr Direction    Dir   = Direction::None;

    InputAction action = InputAction::Fire;
    uint32_t param = 0;   // LaunchMissile: locked target index; Fire: unused

    auto Fields()       { return std::tie(action, param); }
    auto Fields() const { return std::tie(action, param); }
  };
}

REGISTER_MESSAGE(ActionTriggered);
