#pragma once

// InputActions - client-local input intent, as LocalOnly catalog messages.
//
// The bridge between raw key polling and the wire InputCommand: as the client
// detects discrete combat actions (fire, launch missile) it publishes an
// ActionTriggered onto the client MessageBus; a command-builder subscriber
// accumulates the frame's actions and the per-frame send assembles them into the
// InputCommand. This decouples "what the player did" from "how the command is
// built" (rebinding, recording/replay, headless bots all drive the same path).
//
// MessageScope::LocalOnly - these never travel the wire (id in the non-wire half),
// so they cannot leak into the permanent network protocol; only the resulting
// InputCommand is sent. Continuous flight (roll/pitch/throttle) is NOT modelled
// here: it stays the legacy rate-based PlayerFlight state, normalized to axes at
// send time.

#include <cstdint>
#include <tuple>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  enum class InputAction : uint8_t
  {
    Fire = 0,           // fire the front laser this frame (held)
    LaunchMissile = 1,  // launch a missile at `param` (the locked target index)
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
