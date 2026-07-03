#pragma once

// EquipmentEvents - the wire facts G8 equipment produces (NeuronCore).
//
//   EcmPulse       - server -> everyone: a ship's ECM burst just fired (either a
//                    player's activation or an NPC's automatic defence). The
//                    missiles it downed arrive as ordinary EntityDeath events;
//                    this carries the classic sound/flash cue.
//   EscapePodUsed  - server -> the owning session only: your pod ejected - the
//                    ship is gone and you are docked at the nearest station
//                    (cargo lost, record cleared, tank refilled). The client
//                    flips to the docked flow on receipt.
//
// Data/protocol only; the validation and effects live server-side
// (GameLogic/EquipmentSystem.h).

#include <cstdint>
#include <tuple>

#include "Messages/Registry.h"   // MessageId/Traits/Serialize + REGISTER_MESSAGE

namespace Neuron::Msg
{
  struct EcmPulse
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0202);   // replication/lifecycle
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t source = 0;   // the ship whose ECM fired
    auto Fields()       { return std::tie(source); }
    auto Fields() const { return std::tie(source); }
  };

  struct EscapePodUsed
  {
    static constexpr MessageId    Id    = static_cast<MessageId>(0x0203);
    static constexpr MessageScope Scope = MessageScope::Wire;
    static constexpr MessageKind  Kind  = MessageKind::Event;
    static constexpr MessageLane  Lane  = MessageLane::Gameplay;
    static constexpr Direction    Dir   = Direction::ServerToClient;

    uint32_t entityId = 0;   // the ejecting ship (the receiving session's own)
    auto Fields()       { return std::tie(entityId); }
    auto Fields() const { return std::tie(entityId); }
  };
}

REGISTER_MESSAGE(EcmPulse);
REGISTER_MESSAGE(EscapePodUsed);
